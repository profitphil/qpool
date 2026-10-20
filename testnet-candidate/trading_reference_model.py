#!/usr/bin/env python3
"""Reference model for QX trades, QSWAP swaps and dividend splits as QPOOL books them. Generates EXPECTED values for the
trading and dividend battery (TESTING_GUIDE.md, section D+/E+/F+). An aid, not an authority: the callee's own read-only
functions and the real chain are the truth. Parameters that must be READ from the live contracts are marked PARAM.

QX (src/contracts/Qx.h, verified in source):
  * a bid walks the asks from the LOWEST price up while ask price <= bid price; each fill is at the ASK's own price; the buyer is
    refunded (bid - ask) x shares at once; whatever is not filled rests as a bid at the bid price.
  * an ask walks the bids from the HIGHEST price down while bid price >= ask price; each fill is at the BID's price.
  * the trade fee is taken from the SELLER's proceeds: fee = floor(value x tradeFee / 1e9) + 1, tradeFee in billionths (PARAM: QX.Fees).
QSWAP (src/contracts/Qswap.h): out = floor(reserveOut x in x (10000 - fee) / (reserveIn x 10000 + in x (10000 - fee))), fee in basis
  points of 10,000 (PARAM: swapFeeRate, 30 = 0.3 percent). Cross-check only: use QuoteExactQuInput, QuoteExactQuOutput,
  QuoteExactAssetInput, QuoteExactAssetOutput as the oracle (a protocol fee split may make the reserves differ from this simple model).
QPOOL: a SELL_QSWAP pays QSWAP's flat fee 100,000 out of the sale (net = QSWAP's output minus 100,000); a SELL_QX credits the proceeds
  after QX's trade fee and after any management hand-over cost (PARAM handover: 0 when the shares already sit under QX, 100 or 200
  when QPOOL must hand them over; the tester MEASURES it and reports it), then QPOOL's management fee 1 percent (floor) goes to
  revenue and the rest is credited to the holding's members by COST. Dividends: 1 percent management fee (floor), the net is split
  by recognised shares between holdings and by cost between members. Rounding: floor everywhere; up to 1 QU per member of difference.

Usage: python3 trading_reference_model.py t1|t2|t3|t4|t5
"""
import sys
from fractions import Fraction

MGMT_PERMILLE = 10
QSWAP_SELL_FEE = 100_000


def fl(x):
    return int(x) if isinstance(x, int) else x.numerator // x.denominator


class QX:
    def __init__(self, trade_fee_billionths):
        self.tf, self.asks, self.bids, self.seq = trade_fee_billionths, [], [], 0

    def fee(self, value):
        return value * self.tf // 10**9 + 1

    def _o(self, owner, price, qty):
        self.seq += 1
        return {"owner": owner, "price": price, "qty": qty, "seq": self.seq}

    def add_ask_resting(self, owner, price, qty):
        self.asks.append(self._o(owner, price, qty))

    def add_bid_resting(self, owner, price, qty):
        self.bids.append(self._o(owner, price, qty))

    def bid(self, buyer, price, qty):
        """returns (fills[(seller, askprice, n, sellerNet)], refund, rest). rest rests as a bid at `price`."""
        fills, refund, rest = [], 0, qty
        for a in sorted(self.asks, key=lambda o: (o["price"], o["seq"])):
            if rest == 0 or a["price"] > price:
                break
            n = min(a["qty"], rest)
            a["qty"] -= n
            rest -= n
            fills.append((a["owner"], a["price"], n, a["price"] * n - self.fee(a["price"] * n)))
            refund += (price - a["price"]) * n
        self.asks = [a for a in self.asks if a["qty"] > 0]
        if rest:
            self.add_bid_resting(buyer, price, rest)
        return fills, refund, rest

    def ask(self, seller, price, qty):
        """returns (fills[(buyer, bidprice, n, grossToSeller, fee)], rest). rest rests as an ask at `price`."""
        fills, rest = [], qty
        for b in sorted(self.bids, key=lambda o: (-o["price"], o["seq"])):
            if rest == 0 or b["price"] < price:
                break
            n = min(b["qty"], rest)
            b["qty"] -= n
            rest -= n
            fills.append((b["owner"], b["price"], n, b["price"] * n, self.fee(b["price"] * n)))
        self.bids = [b for b in self.bids if b["qty"] > 0]
        if rest:
            self.add_ask_resting(seller, price, rest)
        return fills, rest

    def cancel_ask(self, owner, price):
        self.asks = [a for a in self.asks if not (a["owner"] == owner and a["price"] == price)]


def split_by(total, weights):
    """exact shares of `total` by weights (Fractions) and their floors"""
    W = sum(weights.values())
    ex = {k: Fraction(total) * v / W for k, v in weights.items()}
    return ex, {k: fl(v) for k, v in ex.items()}


class Pool:
    def __init__(self, name, members):
        self.name, self.members = name, dict(members)       # member -> uncommitted QU
        self.shares = 0
        self.pending = 0
        self.cost = {}                                       # member -> cost in the holding
        self.revenue = 0
        self.claimable = {}

    def buy_qx(self, qx, price, shares, budget, say):
        W = {m: u for m, u in self.members.items() if u > 0}
        assert budget <= sum(W.values())
        ex, fls = split_by(budget, W)
        q = min(shares, budget // price)
        fills, refund_improve, rest = qx.bid(self.name, price, q)
        excess = budget - price * q
        filled = sum(f[2] for f in fills)
        spent = sum(f[1] * f[2] for f in fills)
        refund = excess + refund_improve
        resting = price * rest
        assert budget - refund == spent + resting
        for m in W:
            self.members[m] -= fls[m]
        ref_ex, _ = split_by(refund, W)
        self.shares += filled
        self.pending += rest
        for m in W:
            self.cost[m] = self.cost.get(m, 0) + ex[m] - ref_ex[m]
            self.members[m] += fl(ref_ex[m])
        say(f"{self.name} BUY_QX limit {price:,} x {shares} with budget {budget:,}: bid quantity {q}; fills "
            + (", ".join(f"{f[2]}@{f[1]:,}" for f in fills) or "none") + f"; spent {spent:,}; refunds {refund:,} "
            f"(excess {excess:,} + price improvement {refund_improve:,}); resting {rest} shares reserving {resting:,}; "
            f"holding {self.shares} shares + {self.pending} pending; costBasis {fl(sum(self.cost.values())):,}")

    def sell_qx(self, qx, limit, shares, handover, say):
        fills, rest = qx.ask(self.name, limit, shares)
        sold = sum(f[2] for f in fills)
        gross = sum(f[3] for f in fills)
        fees = sum(f[4] for f in fills)
        received = gross - fees - (handover if sold else 0)
        mgmt = received * MGMT_PERMILLE // 1000
        credit = received - mgmt
        qx.cancel_ask(self.name, limit)
        ex, fls = split_by(credit, {m: c for m, c in self.cost.items()})
        self.shares -= sold
        self.revenue += mgmt
        for m in ex:
            self.claimable[m] = self.claimable.get(m, 0) + ex[m]
        say(f"{self.name} SELL_QX limit {limit:,} x {shares}: fills "
            + (", ".join(f"{f[2]}@{f[1]:,}" for f in fills) or "none") + f"; gross {gross:,}, QX trade fee {fees:,}, hand-over {handover if sold else 0}; "
            f"received {received:,}; management fee {mgmt:,}; credited {credit:,} by cost -> "
            + ", ".join(f"{m} {fls[m]:,}" for m in ex) + f"; unsold {rest} cancelled; holding now {self.shares} shares")


class Amm:
    def __init__(self, qu, asset, fee_bps):
        self.qu, self.asset, self.fee = qu, asset, fee_bps

    def out(self, amount_in, reserve_in, reserve_out):
        w = amount_in * (10000 - self.fee)
        return reserve_out * w // (reserve_in * 10000 + w)

    def buy(self, qu_in):
        o = self.out(qu_in, self.qu, self.asset)
        self.qu += qu_in
        self.asset -= o
        return o

    def sell(self, asset_in):
        o = self.out(asset_in, self.asset, self.qu)
        self.asset += asset_in
        self.qu -= o
        return o


def dividend(label, payer_amount, holdings, say):
    """holdings: {holding: (recognisedShares, {member: cost})}"""
    fee = payer_amount * MGMT_PERMILLE // 1000
    net = payer_amount - fee
    R = sum(s for s, _ in holdings.values())
    say(f"{label}: payer sends {payer_amount:,}; management fee {fee:,} to revenue; net {net:,}; recognised shares {R}")
    for h, (s, costs) in holdings.items():
        part = Fraction(net) * s / R
        ex, fls = split_by(part, costs)
        say(f"    {h}: {s} shares -> {float(part):,.4f} (exact {part}); members by cost: "
            + ", ".join(f"{m} {fls[m]:,} (exact {float(ex[m]):,.4f})" for m in costs))


def run(fn):
    log = []
    fn(lambda t: log.append(t))
    return "\n".join(log)


def t1(say):
    """Four pools compete for the same token on QX asks, then a seller crosses their resting bids."""
    say("QX tradeFee = 3,000,000 billionths (0.3 percent, as stated by the owner); confirm with QX.Fees on the testnet")
    qx = QX(3_000_000)
    for p, q in ((1_000, 100), (1_010, 100), (1_020, 100), (1_050, 200)):
        qx.add_ask_resting("MM", p, q)
    say("QX asks (market maker MM): 100@1,000  100@1,010  100@1,020  200@1,050")
    pools = [Pool("A", {"A1": 60_000, "A2": 40_000}), Pool("B", {"B1": 70_000, "B2": 30_000}),
             Pool("C", {"C1": 90_000, "C2": 60_000}), Pool("D", {"D1": 150_000})]
    pools[0].buy_qx(qx, 1_000, 100, 100_000, say)    # exact fill at 1,000
    pools[1].buy_qx(qx, 1_015, 100, 100_000, say)    # quantity = what the money buys at the voted price (98); fills at 1,010 with improvement
    pools[2].buy_qx(qx, 1_050, 150, 150_000, say)    # quantity 142: 100@1,020 then 42@1,050
    pools[3].buy_qx(qx, 1_050, 150, 150_000, say)    # quantity 142 from what is left
    say("asks left: " + ", ".join(f"{a['qty']}@{a['price']:,}" for a in qx.asks) + "; resting bids: "
        + ", ".join(f"{b['owner']} {b['qty']}@{b['price']:,}" for b in qx.bids))
    say("--- second round: asks are gone; A and C bid again below the market, then MM sells into their resting bids")
    for m, amount in (("A1", 30_000), ("A2", 20_000)):
        pools[0].members[m] += amount
    for m, amount in (("C1", 25_000), ("C2", 15_000)):
        pools[2].members[m] += amount
    say("A and C members top up their uncommitted (A +50,000, C +40,000) before the second proposals")
    pools[0].buy_qx(qx, 990, 50, 49_500, say)
    pools[2].buy_qx(qx, 995, 40, 39_800, say)
    fills, rest = qx.ask("MM", 990, 90)
    say("MM sells 90 limit 990: " + ", ".join(f"{f[0]} {f[2]}@{f[1]:,} (fee {f[4]:,})" for f in fills) + f"; unsold {rest}")
    say("Expected for the pools: after the later fills A and C hold their shares only once ReconcileHolding has run (pendingShares -> shares)")
    return qx


def t2(say):
    """Three pools sell into the same bids; proceeds net of QX fee, hand-over cost and the 1 percent management fee."""
    say("QX tradeFee = 3,000,000 billionths (0.3 percent)")
    qx = QX(3_000_000)
    for p, q in ((1_100, 150), (1_090, 100), (1_070, 100)):
        qx.add_bid_resting("MM", p, q)
    say("QX bids (MM): 150@1,100  100@1,090  100@1,070")
    pools = [Pool("A", {}), Pool("B", {}), Pool("C", {})]
    pools[0].shares, pools[0].cost = 120, {"A1": Fraction(72_000), "A2": Fraction(48_000)}
    pools[1].shares, pools[1].cost = 90, {"B1": Fraction(90_000)}
    pools[2].shares, pools[2].cost = 140, {"C1": Fraction(70_000), "C2": Fraction(30_000), "C3": Fraction(20_000)}
    pools[0].sell_qx(qx, 1_090, 120, 0, say)    # QX-managed shares: no hand-over (PARAM handover = 0)
    pools[1].sell_qx(qx, 1_085, 90, 0, say)
    pools[2].sell_qx(qx, 1_080, 140, 0, say)
    say("bids left: " + (", ".join(f"{b['qty']}@{b['price']:,}" for b in qx.bids) or "none"))
    say("Variation: repeat with deposited shares (hand-over PARAM 100 or 200) and with a limit above every bid (nothing sells, shares kept)")
    return qx


def t3(say):
    """Partial fill, a resting bid that fills later, and a dividend arriving before the pool has reconciled."""
    qx = QX(3_000_000)
    qx.add_ask_resting("MM", 1_050, 40)
    p = Pool("A", {"A1": 63_000, "A2": 42_000})
    p.buy_qx(qx, 1_050, 100, 105_000, say)
    say("MM later sells 60 into the resting bid at 1,050: fill price 1,050 = the pool's bid price, so no refund")
    fills, rest = qx.ask("MM", 1_050, 60)
    say("    fills: " + ", ".join(f"{f[2]}@{f[1]:,}" for f in fills))
    say("Before ReconcileHolding the universe holds 100 shares but the ledger recognises 40: the shares of the unreconciled bid are")
    say("not recognised yet. A dividend of 10,000 arriving now: fee 100, net 9,900; the unrecognised part (pending 60 of 100) is")
    say("set aside: credited now = 9,900 x 40/100 = 3,960; escrow 5,940, paid to the holding when ReconcileHolding recognises the fills.")


def t4(say):
    """Four pools trade against ONE QSWAP pool in sequence: price impact, min-out protection, and the flat sell fee."""
    say("QSWAP PARAM: swapFeeRate 30 bps in this example (read it from QSWAP); reserves 1,000,000 QU / 10,000 tokens (price about 100)")
    amm = Amm(1_000_000, 10_000, 30)
    for name, qu in (("A", 100_000), ("B", 100_000), ("C", 100_000)):
        before = amm.qu / amm.asset
        got = amm.buy(qu)
        say(f"{name} BUY_QSWAP {qu:,} QU -> {got:,} tokens (average {qu/got:,.3f} QU per token); pool price before {before:,.3f}, after {amm.qu/amm.asset:,.3f}; reserves {amm.qu:,} / {amm.asset:,}")
    say("The same three buys with min-out set to the FIRST quote: B and C must fail their bound (the price moved against them) and get their QU back")
    held = 800
    got = amm.sell(held)
    say(f"D SELL_QSWAP {held} tokens -> QSWAP pays {got:,} QU; minus the flat fee {QSWAP_SELL_FEE:,} -> credited {got-QSWAP_SELL_FEE:,} to the holding's members by cost "
        f"(management fee 1 percent = {(got-QSWAP_SELL_FEE)//100:,})")
    say("Propose must refuse a SELL_QSWAP whose minimum proceeds are at or below 100,100. Use QSWAP's quote functions as the oracle on the testnet.")


def t5(say):
    """Dividends: two pools, uneven member costs; then seat trades and a sale change who earns the next dividend."""
    holdings = {"A": (100, {"A1": 60, "A2": 30, "A3": 10}), "B": (300, {"B1": 100})}
    dividend("1) token issuer ISSUER1 pays 100,000", 100_000, holdings, say)
    say("2) A1 sells 50 percent of his seat (cost units 30) to A4 at ANY price: the next dividend splits A's part by the new costs")
    holdings = {"A": (100, {"A1": 30, "A2": 30, "A3": 10, "A4": 30}), "B": (300, {"B1": 100})}
    dividend("   ISSUER1 pays 10,001", 10_001, holdings, say)
    say("3) pool B sells 100 of its 300 shares on QX: recognised shares fall to 200; B's members' costs stay")
    holdings = {"A": (100, {"A1": 30, "A2": 30, "A3": 10, "A4": 30}), "B": (200, {"B1": 100})}
    dividend("   ISSUER1 pays 90,000", 90_000, holdings, say)
    say("4) a third pool C buys 200 shares on QX: it earns only dividends paid AFTER its shares are recognised")
    holdings = {"A": (100, {"A1": 30, "A2": 30, "A3": 10, "A4": 30}), "B": (200, {"B1": 100}), "C": (200, {"C1": 50, "C2": 50})}
    dividend("   ISSUER1 pays 50,000", 50_000, holdings, say)
    say("5) 1 QU dividends: each pays fee floor(1 x 10/1000) = 0 and credits 1 QU through a fixed-point accumulator; the sum of all claims")
    say("   over 1,000 such payments must never exceed 1,000 QU, and each member's rounding error stays below 1 QU")
    say("6) a contract pays by qpiDistributeDividends: payer sends perShare x 676; QPOOL's ledger for that contract receives only the")
    say("   shares QPOOL holds: for example perShare 150 and QPOOL holds 300 shares of it -> 45,000 arrives, fee 450, net 44,550")


if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "t1"
    print(run({"t1": t1, "t2": t2, "t3": t3, "t4": t4, "t5": t5}[name]))
