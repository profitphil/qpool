#!/usr/bin/env python3
"""100-WALLET, 10-POOL version of the K8 season plan (derived by patch_season_k8_100.py): wallets U01-U100, pools P1-P10 (P9 has 45 members, P2 and P6 about 28), outsiders OUT1-OUT6,
market makers MM1-MM4; every IPO round has two more pools. Everything else is as in the K8 plan below.

K8 version of the season plan (see REALISTIC_SEASON_TEST_K8.md). Differences from the K6 plan: BUY_IPO carries no share name; BEGIN_EPOCH pairs
the IPO names and settles up to AUTO_SETTLE_MAX ready holdings (the rest by hand); CancelQxBid scenes; GetNameRegistry reads; BuyPosition with live expectedShares;
IPO_PRICE_TAKEN fails the proposal; the QPAY hand-over fee is measured.

Generates the REALISTIC SEASON test plan for QPOOL: 40 users, 8 pools (+1 scratch pool), 7 epochs, 6 IPO targets
(one IPO in epoch 2, two simultaneous in epoch 4, three simultaneous in epoch 6), with every procedure exercised many times in
many ways, successes and refusals. It writes season-plan/: cast.csv, pools.csv, actions.csv, ipo-rounds.txt, checkpoints.md,
coverage.md and asserts the coverage thresholds, so the plan itself proves that every function is covered.

Expected values come from ipo_reference_model.py (IPO wars), trading_reference_model.py (QX, QSWAP, dividends) and a small
ledger kept here (pool balances, fees, revenue split, QPAY buy-and-burn). It is an aid, not an authority: the chain is the truth.
PARAMs to confirm on the first use (re-run the generator after changing them):
  JOIN_FEE_FROM_ATTACHED  True: the 10,000 join fee is taken out of the attached amount of a first Contribute.
  HANDOVER                cost QPOOL pays QX to take deposited shares under QX's management (0, 100 or 200).
  QX tradeFee 0.3 percent, QSWAP flat sell fee 100,000, QSWAP swapFeeRate (read it live).
Usage: python3 season_plan.py
"""
import csv
import os
import random
import sys
from collections import Counter, defaultdict
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
import ipo_reference_model as ipo   # noqa: E402
import trading_reference_model as trd   # noqa: E402

OUT = os.path.join(HERE, "season-plan-k83-100")
JOIN_FEE, MIN_CONTRIB, CREATE_FEE = 10_000, 10_000, 10_000_000
JOIN_FEE_FROM_ATTACHED = True
HANDOVER = 100
VOTE, EXEC, IPOVOTE = 144, 96, 48
SWAP_FEE_BPS = 30
AUTO_SETTLE_MAX = 3      # QPOOL_AUTO_SETTLE_MAX of the season build (production default 32): part of each IPO settles at the epoch start, the rest by hand
QPAY_HANDOVER = 0       # QU the balance really falls by during the QPAY hand-over to QPOOL (0 if QX refunds the attached reward, as in the local Qx.h; 100 if it keeps it). MEASURE.

PROCS = ["CreatePool", "Contribute", "WithdrawUncommitted", "Propose", "Vote", "Tally", "Execute", "ReconcileHolding",
         "ClaimPayout", "OfferPosition", "CancelOffer", "BuyPosition", "RebidIpo", "SettleIpo", "DepositHoldingAsset",
         "SetAdmission", "SetPermissioned", "ReleaseManagedShares", "CloseEmptyPool", "EvictSquat", "BuyAndBurnQpay", "CancelQxBid", "SetPoolAdmins", "CancelQxAsk"]
FUNCS = ["GetPool", "GetMember", "GetHolding", "GetPosition", "GetProposal", "GetStats", "GetSource", "GetIpoTarget", "GetIpoBids", "GetNameRegistry"]


def U(i):
    return f"U{i:02d}"


# ----------------------------------------------------------------------------------------------------------------- cast (100 wallets)
ROLE = {}
for i in range(1, 9):
    ROLE[U(i)] = f"creator of pool P{i}"
for i in range(9, 31):
    ROLE[U(i)] = "member (in 1 to 3 pools)"
ROLE.update({U(31): "market maker MM1 (sells TOK1 on QX)", U(32): "market maker MM2 (sells TOK2 on QX)",
             U(33): "market maker MM3 (QX bids, QPAY asks)", U(34): "LP: QSWAP liquidity TOK1/QU",
             U(35): "ISSUER1: issues TOK1, pays dividends", U(36): "ISSUER2: issues TOK2, pays dividends",
             U(37): "OUT1: IPO outsider and trader", U(38): "OUT2: IPO outsider and sniper", U(39): "OUT3: IPO outsider",
             U(40): "KEEPER and ATTACKER (non-member, permissionless calls, hostile inputs)",
             U(41): "creator of pool P9", U(42): "creator of pool P10",
             U(43): "OUT4: IPO outsider", U(44): "OUT5: IPO outsider and sniper", U(45): "OUT6: IPO outsider",
             U(46): "market maker MM4 (TOK1 asks from epoch 4 on)"})
for i in range(47, 101):
    ROLE[U(i)] = "member (in 1 to 3 pools)"
OUTW = {"OUT1": U(37), "OUT2": U(38), "OUT3": U(39), "OUT4": U(43), "OUT5": U(44), "OUT6": U(45)}
OLD_POOLS = [("P1", "Alpha", "public", 1, [9, 10, 11, 12, 13, 14, 15]), ("P2", "Beta", "public", 2, [16, 17, 18, 19, 20, 9, 10]),
             ("P3", "Gamma", "private", 3, [21, 22, 23, 24, 11]), ("P4", "Delta", "public", 4, [25, 26, 27, 28, 12]),
             ("P5", "Epsilon", "private", 5, [29, 30, 13, 21]), ("P6", "Zeta", "public", 6, [14, 15, 16, 22, 25, 29, 30]),
             ("P7", "Eta", "public", 7, [17, 23, 26, 30]), ("P8", "Theta", "private", 8, [18, 24, 27, 28])]
NEW_POOLS = [("P9", "Iota", "public", 41, [19, 20, 24]), ("P10", "Kappa", "private", 42, [28, 15])]
# new members per pool (U47..U100, each ends in 2 or 3 pools): the big pools get most of them
QUOTA = {"P1": 16, "P2": 20, "P3": 8, "P4": 12, "P5": 6, "P6": 20, "P7": 4, "P8": 8, "P9": 38, "P10": 14}
_count = Counter()
_new = {}
for _p in sorted(QUOTA, key=lambda k: (-QUOTA[k], k)):
    _cand = sorted(range(47, 101), key=lambda w: (_count[w], w))[:QUOTA[_p]]
    _new[_p] = sorted(_cand)
    for _w in _cand:
        _count[_w] += 1
assert max(_count.values()) <= 3 and min(_count.values()) >= 1, dict(_count)
POOLS = [(p, n, k, c, ms + _new[p]) for p, n, k, c, ms in OLD_POOLS + NEW_POOLS]
PERSONA = {"P1": "QX trader + IPO regular", "P2": "aggressive IPO hunter", "P3": "dividend-yield pool, private, creator deposits TOK2",
           "P4": "QSWAP trader", "P5": "conservative private IPO pool", "P6": "mixed: QX, QSWAP and IPO",
           "P7": "small pool, seat-market heavy", "P8": "private pool that toggles public, deposits TOK1",
           "P9": "large public pool: QX trader and IPO regular, many small members", "P10": "private IPO syndicate"}
MEMBERS = {p[0]: [U(p[3])] + [U(x) for x in p[4]] for p in POOLS}
KIND = {p[0]: p[2] for p in POOLS}
CREATOR = {p[0]: U(p[3]) for p in POOLS}
rng = random.Random(2026)

# ---- K8.1 operators: the pool's creator and up to three admins named by the creator (SetPoolAdmins) ----
ADMINS = {}
for _p in MEMBERS:
    _n = {"P5": 0, "P3": 3, "P7": 3}.get(_p, 2)          # P5: creator only; P3 and P7: three admins; the others two
    ADMINS[_p] = MEMBERS[_p][1:1 + _n]
OPS = {p: [CREATOR[p]] + ADMINS[p] for p in MEMBERS}
COUNTERS = Counter()


def operator(pool, k=0):
    return OPS[pool][k % len(OPS[pool])]


def non_operators(pool, users):
    return [u for u in users if u not in OPS[pool]]


# --------------------------------------------------------------------------------------------------------------- ledger
ACT = []
SCENES = []
unc = defaultdict(dict)          # pool -> user -> uncommitted
roster = defaultdict(set)
rev = Counter()                  # epoch -> revenue collected
rev_note = defaultdict(list)
pot = 0
pending = {}                     # epoch -> list of text lines for checkpoints
CHECK = []


def act(ep, scene, t, actor, pool, fn, variant, expect="OK", params="", note=""):
    n = sum(1 for a in ACT if a["epoch"] == ep) + 1
    ACT.append(dict(id=f"E{ep}-{n:03d}", epoch=ep, tick=t, scene=scene, actor=actor, pool=pool, fn=fn, variant=variant,
                    expect=expect, params=params, note=note))


def ref(code):
    return "REFUSED:" + code


def total(pool):
    return sum(unc[pool].values())


def charge(pool, amount, voters):
    """charge `amount` from yes-voters pro rata to their uncommitted; returns {user: assigned}"""
    w = {u: unc[pool].get(u, 0) for u in voters if unc[pool].get(u, 0) > 0}
    assert sum(w.values()) >= amount, (pool, amount, sum(w.values()))
    ex = {u: Fraction(amount) * x / sum(w.values()) for u, x in w.items()}
    got = {u: int(v) for u, v in ex.items()}
    rem = amount - sum(got.values())
    for u in sorted(w, key=lambda k: -w[k])[:rem]:
        got[u] += 1
    for u, a in got.items():
        unc[pool][u] -= a
    return got


def credit(pool, amount, weights):
    W = sum(weights.values())
    out = {u: int(Fraction(amount) * x / W) for u, x in weights.items()}
    rem = amount - sum(out.values())
    for u in sorted(weights, key=lambda k: -weights[k])[:rem]:
        out[u] += 1
    for u, a in out.items():
        unc[pool][u] = unc[pool].get(u, 0) + a
    return out


def contribute(ep, scene, t, pool, user, amount, variant="first-time" , note=""):
    first = user not in roster[pool]
    if first:
        roster[pool].add(user)
        rev[ep] += JOIN_FEE
        rev_note[ep].append(f"join fee {pool}/{user}")
    net = amount - (JOIN_FEE if first and JOIN_FEE_FROM_ATTACHED else 0)
    attach = amount if (not first or JOIN_FEE_FROM_ATTACHED) else amount + JOIN_FEE
    unc[pool][user] = unc[pool].get(user, 0) + net
    act(ep, scene, t, user, pool, "Contribute", variant if first else "top-up (no second join fee)",
        params=f"attach {attach:,}", note=f"uncommitted +{net:,} -> {unc[pool][user]:,}" + (f"; revenue +{JOIN_FEE:,}" if first else "") + note)


def ensure(ep, scene, t, pool, need):
    """top-ups so the pool can fund `need`, spread over members round-robin"""
    ms = MEMBERS[pool]
    i = 0
    while total(pool) < need:
        contribute(ep, scene, t + i, pool, ms[i % len(ms)], 200_000, "top-up before a proposal")
        i += 1


def votes_for(pool, pattern):
    ms = MEMBERS[pool]
    return [(u, pattern[i % len(pattern)]) for i, u in enumerate(ms)]


def gov(ep, scene, t, pool, kind, params, desc, pattern=("yes", "yes", "yes", "no", "abstain", "yes", "yes", "yes"),
        budget=None, late_execute=False, early_probe=True, execute_ok=True, quorum_fail=False):
    """Propose, votes (with refusal probes), Tally, Execute. Returns yes-voters and their weights."""
    prop = CREATOR[pool]
    act(ep, scene, t, prop, pool, "Propose", f"{kind}", params=params, note=desc)
    act(ep, scene, t + 1, U(40), pool, "GetProposal", f"{kind} right after Propose", note="EXPECT status OPEN and the proposed fields; BUY_IPO shows the derived opening quantity")
    ms = MEMBERS[pool]
    vs = votes_for(pool, ("yes", "no", "no", "abstain") if quorum_fail else pattern)
    yes = []
    for k, (u, v) in enumerate(vs):
        if v == "abstain":
            continue
        act(ep, scene, t + 3 + k, u, pool, "Vote", v.upper(), params=f"proposal {kind}")
        if v == "yes":
            yes.append(u)
    window = IPOVOTE if kind == "BUY_IPO" else VOTE
    act(ep, scene, t + 3 + len(vs), ms[0], pool, "Vote", "ALREADY_VOTED probe", ref("ALREADY_VOTED"), "same voter votes twice")
    act(ep, scene, t + 4 + len(vs), U(40), pool, "Vote", "non-member probe", ref("NOT_MEMBER"), "KEEPER is not a member")
    if early_probe:
        act(ep, scene, t + 10 + len(vs), U(40), pool, "Tally", "too early", ref("WINDOW_OPEN"), f"vote window {window} ticks")
        act(ep, scene, t + 11 + len(vs), U(40), pool, "Execute", "before Tally", ref("WRONG_STATUS"), "proposal still OPEN")
    act(ep, scene, t + window + 3, U(40), pool, "Vote", "after the window", ref("WINDOW_CLOSED"), "one tick after the vote window")
    weight = sum(unc[pool].get(u, 0) for u in yes)
    elig = total(pool)
    passed = (2 * weight >= elig) and not quorum_fail
    act(ep, scene, t + window + 4, U(40), pool, "Tally", "after the window (KEEPER)", "OK",
        note="PASSED" if passed else "REJECTED (quorum or yes <= no)")
    act(ep, scene, t + window + 5, U(40), pool, "GetProposal", f"{kind} after Tally", note="EXPECT status " + ("PASSED" if passed else "REJECTED") + "; vote locks released as specified")
    if passed and execute_ok:
        if late_execute:
            act(ep, scene, t + window + EXEC + 8, U(40), pool, "Execute", "after the execute window: anybody closes the dead mandate", ref("WINDOW_CLOSED") + "|" + ref("WRONG_STATUS"),
                "nobody carried the mandate out in time: it lapses and nothing was spent (closing it is not an operator-only act)")
        else:
            COUNTERS["exec"] += 1
            actor = operator(pool, COUNTERS["exec"])
            who = "the creator" if actor == CREATOR[pool] else "an admin"
            if COUNTERS["probe:" + pool] < 2:
                COUNTERS["probe:" + pool] += 1
                act(ep, scene, t + window + 6, U(40), pool, "Execute", "a stranger tries to fire the passed mandate", ref("NOT_AUTHORIZED"), "only the creator and the admins place the orders; nothing is written")
                act(ep, scene, t + window + 7, non_operators(pool, MEMBERS[pool])[-1], pool, "Execute", "a member who is not an operator tries to fire it", ref("NOT_AUTHORIZED"),
                    "a vote does not give the right to execute; nothing is written")
            act(ep, scene, t + window + 8, actor, pool, "Execute", f"within the execute window ({who})", "OK", note=desc)
            act(ep, scene, t + window + 9, U(40), pool, "GetProposal", f"{kind} after Execute", note="EXPECT status EXECUTED")
    elif not passed:
        act(ep, scene, t + window + 8, U(40), pool, "Execute", "on a REJECTED proposal", ref("WRONG_STATUS"))
    return yes, passed


# ---------------------------------------------------------------------------------------------------- market simulation
qx = trd.QX(3_000_000)
tp = {p[0]: trd.Pool(p[0], {}) for p in POOLS}


def sync_in(pool):
    tp[pool].members = dict(unc[pool])


def sync_out(pool):
    unc[pool] = dict(tp[pool].members)


def qx_buy(ep, scene, t, pool, price, shares, budget, desc):
    yes, passed = gov(ep, scene, t, pool, "BUY_QX", f"limit {price:,} x {shares}, budget {budget:,}", desc,
                      pattern=("yes", "yes", "yes", "yes", "abstain", "yes", "yes", "yes"))
    if not passed:
        return
    sync_in(pool)
    tp[pool].members = {u: v for u, v in tp[pool].members.items() if u in yes}
    full = dict(unc[pool])
    log = []
    tp[pool].buy_qx(qx, price, shares, min(budget, sum(tp[pool].members.values())), log.append)
    for u, v in tp[pool].members.items():
        full[u] = v
    unc[pool] = full
    ACT[-1]["note"] += " | EXPECT: " + log[-1]


amm = trd.Amm(20_000_000, 20_000, SWAP_FEE_BPS)


def qswap_buy(ep, scene, t, pool, qu, desc):
    yes, passed = gov(ep, scene, t, pool, "BUY_QSWAP", f"{qu:,} QU for TOK1, min-out = quote x 0.99", desc)
    if passed:
        got = amm.buy(qu)
        charge(pool, qu, yes)
        ACT[-1]["note"] += f" | EXPECT: {got:,} TOK1 (QSWAP quote at execution; reserves now {amm.qu:,} / {amm.asset:,}); costBasis {qu:,}"


def qx_holder(p):
    return (tp[p].shares, {u: c for u, c in tp[p].cost.items() if c > 0})


def ipo_holder(target, p):
    F, res = RESULTS[target]
    sh, cb, ret, bud = res[p]
    w = ASSIGNED[(target, p)]
    W = sum(w.values())
    return (sh, {u: Fraction(cb) * x / W for u, x in w.items()})


def dividend_event(ep, scene, t, payer, token, amount, holders, label):
    """holders: {pool: (recognised shares, {member: cost})}. Dividends reach ONLY the holdings of this asset and, inside a
    holding, ONLY the members who paid for the seats; every other pool and every voter who rejected or abstained gets 0."""
    log = []
    trd.dividend(label, amount, holders, log.append)
    rev[ep] += amount * 10 // 1000
    rev_note[ep].append(f"1% dividend fee on {amount:,} ({token})")
    zero_pools = [p for p in MEMBERS if p not in holders]
    zero_members = {p: [u for u in MEMBERS[p] if costs.get(u, 0) <= 0] for p, (sh, costs) in holders.items()}
    note = (" | EXPECT: " + " || ".join(log) + " | MUST RECEIVE 0: pools " + (", ".join(zero_pools) or "none") + "; members with no seat in a holding: "
            + "; ".join(f"{p}: {', '.join(v) or 'none'}" for p, v in zero_members.items()))
    act(ep, scene, t, payer, "-", "EXT:pay dividend", f"{token} payer pays by plain transfer / distributeDividends", params=f"{amount:,} QU arrives at QPOOL", note=note)
    k = 0
    for p, zs in zero_members.items():
        for u in zs[:2]:
            act(ep, scene, t + 8 + k, u, p, "ClaimPayout", f"voter who rejected or abstained (no seat in the {token} holding)", ref("NOT_MEMBER"),
                params=f"holding of {token} in {p}", note="NOT a share of the dividend: claimable and uncommitted stay unchanged")
            k += 1
    for p in zero_pools[:3]:
        act(ep, scene, t + 12, MEMBERS[p][1], p, "GetMember", f"pool without {token}: nothing received", note="EXPECT claimable and uncommitted unchanged by this payment")
    act(ep, scene, t + 13, "-", "-", "GetSource", f"ledger of {token}", note="EXPECT credited total = payment minus the 1% fee; escrow 0 unless a holding is unsettled or a fill unreconciled")


# ------------------------------------------------------------------------------------------------------------ IPO wars
IPO_LOG = []
RESULTS = {}
ASSIGNED = {}        # (target, pool) -> {user: money charged}: who PAID for the IPO seats


def war(ep, scene, t0, target, specs, fill_price, fill_qty, seed, steps, snipe=None, cap_slots=None):
    """specs: [(pool, budget, opening)]. Returns {pool: (shares, costBasis, returned, budget)}"""
    r = random.Random(seed)
    book = ipo.Book()
    pools = [ipo.Pool(p, b, o) for p, b, o in specs]
    out_paid = Counter()
    lines = [f"### {target} (epoch {ep}): {len(specs)} pools"]
    t = t0
    for p in pools:
        ref_, n = p.open(book)
        lines.append(f"  {p.name} Execute: budget {p.budget:,} at opening {p.opening:,} -> {n} seats, free {p.free:,}")
        act(ep, scene, t, CREATOR[p.name], p.name, "Execute", f"BUY_IPO opening bid on {target}", "OK",
            params=f"budget {p.budget:,} at opening {p.opening:,}", note=f"EXPECT {n} seats all-in, free {p.free:,}")
        t += 4
    if fill_qty:
        book.bid("OUT1", fill_price, fill_qty)
        out_paid["OUT1"] += fill_price * fill_qty
        lines.append(f"  OUT1 bids {fill_qty} @ {fill_price:,}  -> book {len(book.slots)} slots, lowest {book.lowest():,}")
        act(ep, scene, t, U(37), "-", "EXT:IPO bid", f"{target} wallet bid (16 byte ContractIPOBid)", params=f"{fill_qty} @ {fill_price:,}",
            note=f"book {len(book.slots)} slots")
        t += 4
    for k in range(steps):
        kind = r.choice(["rebid", "rebid", "rebid", "out", "out", "bad"])
        low = book.lowest()
        if kind == "out":
            o = r.choice(list(OUTW))
            price, qty = low + r.randint(1, 350), r.randint(1, 50)
            if cap_slots is not None:               # a target that must FAIL keeps its book below 676 slots
                qty = min(qty, cap_slots - len(book.slots))
                if qty <= 0:
                    continue
            res = book.bid(o, price, qty)
            hurt = ", ".join(f"{x} +{a:,}" for x, a in sorted(res.items()) if x != o) or "none"
            lines.append(f"  {o} bids {qty} @ {price:,} -> refunds {hurt}")
            act(ep, scene, t, OUTW[o], "-", "EXT:IPO bid", f"{target} outsider", params=f"{qty} @ {price:,}",
                note=f"refunds in the same tx: {hurt}")
        else:
            p = r.choice(pools)
            price = max(1, low + r.randint(1, 300)) if kind == "rebid" else r.choice([max(1, low), low + 1, 1])
            qty = None if kind == "rebid" else r.choice([None, 3])
            code, refs, n = p.rebid(book, pools, price, qty)
            stranger = (kind == "bad" and r.random() < 0.2)
            if stranger:
                act(ep, scene, t, U(40), p.name, "RebidIpo", "stranger (no seat)", ref("NOT_AUTHORIZED"),
                    params=f"{target} price {price:,}")
                lines.append(f"  STRANGER rebids for {p.name}: refused")
            else:
                actor = operator(p.name, MEMBERS[p.name].index(r.choice(MEMBERS[p.name])))      # same random draw as before; only operators rebid
                lines.append(f"  {p.name} RebidIpo @ {price:,} -> {code}" + (f", {n} seats" if code == "OK" else ""))
                act(ep, scene, t, actor, p.name, "RebidIpo", f"{target} " + ("funded by returned money" if code == "OK" else "refusal variant"),
                    "OK" if code == "OK" else ref(code), params=f"price {price:,}" + (f" qty {qty}" if qty else ""),
                    note=(f"EXPECT {n} seats" if code == "OK" else "state must not change"))
        t += 3
        if k % 9 == 4:
            pp = pools[k % len(pools)]
            nonop = non_operators(pp.name, list(ASSIGNED.get((target, pp.name), {})))
            if nonop:
                act(ep, scene, t, nonop[0], pp.name, "RebidIpo", "a seat owner who is not an operator", ref("NOT_AUTHORIZED"),
                    params=f"{target} price {book.lowest() + 5:,}", note="K8.1: only the creator and the admins rebid; nothing is written, the reward returns")
                t += 1
        if k % 6 == 5:
            for p in pools:
                act(ep, scene, t, U(40), p.name, "GetIpoBids", f"{target} after step {k + 1}",
                    note=f"EXPECT per ipo-rounds.txt: free {p.free:,}, atRisk {p.atrisk:,}, listed seats {dict(sorted(p.listed.items()))}; compare with the core book")
                act(ep, scene, t, U(40), p.name, "GetHolding", f"{target} after step {k + 1}", note="EXPECT free + atRisk = costBasis (I4)")
            t += 1
    if snipe:
        o, price, qty = snipe
        res = book.bid(o, price, qty)
        lines.append(f"  SNIPE {o} bids {qty} @ {price:,} in the last ticks")
        act(ep, scene, t, OUTW[o], "-", "EXT:IPO bid", f"{target} last-minute snipe", params=f"{qty} @ {price:,}")
        t += 3
        for p in pools:
            code, refs, n = p.rebid(book, pools, price + 1 + [q.name for q in pools].index(p.name), None)
            act(ep, scene, t, operator(p.name, MEMBERS[p.name].index(r.choice(MEMBERS[p.name]))), p.name, "RebidIpo", f"{target} answer to the snipe",
                "OK" if code == "OK" else ref(code), params=f"price {price + 1 + [q.name for q in pools].index(p.name):,}")
            t += 3
    F, res = ipo.settle(book, pools, out_paid)
    lines.append(f"  END OF EPOCH {ep}: final price {F:,}" + ("  (book not full: THE IPO FAILS, everything refunded)" if F == 0 else ""))
    for name, v in res.items():
        lines.append(f"    {name}: {v}")
    IPO_LOG.append("\n".join(lines))
    act(ep, scene, t + 5, "-", "-", "GetIpoTarget", f"{target} after END_EPOCH", note=f"EXPECT final price {F:,}; snapshot present")
    out = {}
    for p in pools:
        v = res[p.name]
        out[p.name] = (v["shares"], v["costBasis"], v["returned"], p.budget)
    RESULTS[target] = (F, out)
    return F, out


IPO_PATTERN = ("yes", "yes", "yes", "no", "yes", "abstain", "yes", "yes")


def ensure_yes(ep, scene, t, pool, need, pattern):
    """top-ups by the future YES voters BEFORE the proposal opens (money added later carries no vote weight)"""
    yes = [u for u, v in votes_for(pool, pattern) if v == "yes"]
    i = 0
    while sum(unc[pool].get(u, 0) for u in yes) < need:
        contribute(ep, scene, t + i, pool, yes[i % len(yes)], 200_000, "top-up before a proposal")
        i += 1


def ipo_pipeline(ep, scene, t, specs, target, quorum_fail_pool=None):
    """BUY_IPO proposals for every pool of a target. Only the YES voters pay and later earn; 'no' and 'abstain' voters do not."""
    for p, b, o in specs:
        ensure_yes(ep, scene, t, p, b, IPO_PATTERN)
        yes, passed = gov(ep, scene, t, p, "BUY_IPO", f"{target}: budget {b:,}, opening price {o:,}, asset name 0 (no typed name)", f"{target} IPO bid",
                          pattern=IPO_PATTERN, quorum_fail=(p == quorum_fail_pool), execute_ok=False)
        if p == quorum_fail_pool:
            ensure_yes(ep, scene, t + 60, p, b, ("yes",))
            yes, passed = gov(ep, scene, t + 60, p, "BUY_IPO", f"{target}: budget {b:,}, opening price {o:,}, asset name 0 (second attempt, all vote yes)",
                              f"{target} IPO bid again", pattern=("yes",), early_probe=False, execute_ok=False)
        assert passed, ("proposal must pass in the plan", target, p)
        ASSIGNED[(target, p)] = charge(p, b, yes)
        t += 2


# ------------------------------------------------------------------------------------------------ END_EPOCH + QPAY burn
def reads(ep, t):
    for p in MEMBERS:
        act(ep, "end-of-epoch reads", t, U(40), p, "GetPool", "end-of-epoch read", note="EXPECT uncommittedTotal as in checkpoints.md; invariants I1, I2")
        act(ep, "end-of-epoch reads", t, MEMBERS[p][1], p, "GetMember", "a member and the creator", note="EXPECT uncommitted and claimable consistent with the claims made")
        act(ep, "end-of-epoch reads", t, U(40), p, "GetHolding", "every holding of the pool", note="EXPECT shares, pendingShares, costBasis; I3, I4")
        act(ep, "end-of-epoch reads", t, MEMBERS[p][2], p, "GetPosition", "seats of every member of the pool", note="EXPECT cost shares add up to costBasis (I3)")
    act(ep, "end-of-epoch reads", t, U(40), "-", "GetSource", "every ledger", note="EXPECT credited = paid in minus fees; escrow as explained")


def end_epoch(ep, t=900):
    global pot
    reads(ep, t - 5)
    r = rev[ep]
    sh = r * 400 // 1000
    ps = sh // 676
    paid = ps * 676
    hub = r * 300 // 1000
    potc = r * 200 // 1000
    burn = r - paid - hub - potc
    pot += potc
    act(ep, "end", t, "-", "-", "EXT:epoch change", "END_EPOCH", note=f"revenue {r:,}: per share {ps:,} (paid {paid:,}), QPAYHUB {hub:,}, pot +{potc:,}, burn {burn:,}; pot now {pot:,}")
    act(ep, "end", t + 1, "-", "-", "GetStats", "after END_EPOCH", note=f"EXPECT pendingRevenue 0, qpayBuyPot {pot:,}")
    CHECK.append((ep, r, ps, paid, hub, potc, burn, pot, {p: total(p) for p in MEMBERS}, list(rev_note[ep])))


def buy_burn(ep, scene, t, asks, note):
    """replicates the contract: exact bid per ask level at or below 10,000, at most 8 levels, QX hand-over fee 100"""
    global pot
    budget = max(0, pot - QPAY_HANDOVER)
    spent = bought = lv = 0
    for price, qty in asks:
        if price > 10_000 or lv >= 8:
            break
        n = min(qty, budget // price)
        if n == 0:
            break
        spent += n * price
        bought += n
        budget -= n * price
        lv += 1
    if bought:
        pot -= spent + QPAY_HANDOVER
        act(ep, scene, t, U(40), "-", "BuyAndBurnQpay", note, params="maxLevels 8",
            note=f"EXPECT bought {bought:,}, spent {spent:,} + hand-over {QPAY_HANDOVER} (the amount the balance really fell by: MEASURE and report 0 or 100), burned {bought:,}; pot left {pot:,}; balance = sum of buckets")
    else:
        act(ep, scene, t, U(40), "-", "BuyAndBurnQpay", note, ref("NOTHING_TO_BUY"), note="no write, reward returned")


def breadth(ep, t):
    """every pool uses the seat market and the reconcile call; creators deposit; non-creators are refused"""
    s = f"E{ep}.S-breadth: all 8 pools use the seat market, creators deposit assets"
    for p in MEMBERS:
        a, b = MEMBERS[p][1], MEMBERS[p][2]
        act(ep, s, t, a, p, "OfferPosition", "offer 10% of a seat", params="ask 90,000")
        act(ep, s, t + 1, b, p, "BuyPosition", "a member buys the offer", params="attach 90,000; expectedCostUnits and expectedShares from GetPosition", note="fee 1% = 900 to revenue; seller +89,100 claimable")
        rev[ep] += 900
        act(ep, s, t + 2, a, p, "OfferPosition", "second offer", params="5% of a seat, ask 50,000")
        act(ep, s, t + 3, a, p, "CancelOffer", "cancel the second offer")
        act(ep, s, t + 4, a, p, "CancelOffer", "nothing left to cancel", ref("NO_OFFER"))
        act(ep, s, t + 5, MEMBERS[p][3], p, "ReconcileHolding", "every holding of the pool", note="OK or nothing to do; must not write when nothing is pending")
        if KIND[p] == "private":
            act(ep, s, t + 6, U(40), p, "BuyPosition", "outsider in a private pool", ref("NOT_ADMITTED"), "attach any")
        else:
            act(ep, s, t + 6, U(40), p, "BuyPosition", "outsider joins a public pool by buying a seat", "OK", "attach ask + 10,000 join fee")
        if p in ("P1", "P2", "P4", "P5", "P6", "P7"):
            act(ep, s, t + 7, CREATOR[p], p, "DepositHoldingAsset", "creator deposits " + ("TOK1" if p in ("P1", "P4", "P6") else "TOK2"), params="x 1,500")
        act(ep, s, t + 7, MEMBERS[p][1], p, "DepositHoldingAsset", "non-creator", ref("NOT_AUTHORIZED"), "x 100")
        t += 9


# =================================================================================================================== epochs
def e1():
    ep = 1
    act(ep, "E1.S0 epoch start", 0, U(40), "-", "GetNameRegistry", "after QPOOL's first BEGIN_EPOCH (baseline)", note="EXPECT baselineDone 1, paired 0, mismatches 0, queueLength 0; a zero-issuer name outside the table that already existed shows nameState 3")
    act(ep, "E1.S0 epoch start", 0, U(40), "-", "GetStats", "after QPOOL's first BEGIN_EPOCH", note="EXPECT autoSettleEpoch 1, autoSettleDone 0, readyUnsettled 0")
    s = "E1.S1 create the pools"
    for k, p in enumerate(POOLS):
        act(ep, s, k, CREATOR[p[0]], p[0], "CreatePool", "exact fee 10,000,000", params=f"name {p[1]}, attach {CREATE_FEE:,}",
            note="revenue +10,000,000")
        rev[ep] += CREATE_FEE
    act(ep, s, 9, U(40), "P0", "CreatePool", "under-paid", ref("INSUFFICIENT_FEE"), "attach 9,999,999")
    act(ep, s, 10, U(40), "P0", "CreatePool", "over-paid (scratch pool for CloseEmptyPool)", "OK", "attach 11,000,000",
        "excess 1,000,000 returned; revenue +10,000,000")
    rev[ep] += CREATE_FEE
    s = "E1.S2 admission and visibility"
    for p in ("P3", "P5", "P8", "P10"):
        act(ep, s, 12, CREATOR[p], p, "SetPermissioned", "make private", params="1")
    act(ep, s, 13, U(40), "P3", "SetPermissioned", "non-creator", ref("NOT_AUTHORIZED"))
    act(ep, s, 13, U(40), "P3", "SetAdmission", "non-creator", ref("NOT_AUTHORIZED"))
    for p in ("P3", "P5", "P8", "P10"):
        for u in MEMBERS[p][1:]:
            act(ep, s, 14, CREATOR[p], p, "SetAdmission", "admit a member", params=f"admit {u}")
    act(ep, s, 16, U(25), "P3", "Contribute", "not admitted to a private pool", ref("NOT_ADMITTED"), "attach 100,000")
    s = "E1.S3 onboarding contributions"
    t = 20
    for p, _, _, _, _ in POOLS:
        base = rng.choice([700_000, 900_000, 1_200_000, 1_500_000])
        contribute(ep, s, t, p, CREATOR[p], base, "creator first contribution")
        t += 1
        for u in MEMBERS[p][1:]:
            contribute(ep, s, t, p, u, rng.choice([120_000, 250_000, 400_000, 700_000, 1_000_000]))
            t += 1
    act(ep, s, t, U(12), "P4", "Contribute", "below the minimum", ref("INVALID_INPUT") + "|" + ref("INSUFFICIENT_FEE"), "attach 5,000")
    for p in ("P1", "P2", "P6"):
        for u in MEMBERS[p][2:4]:
            contribute(ep, s, t, p, u, 150_000)
    s = "E1.S3b operators: each creator names the pool's admins (SetPoolAdmins)"
    for k, p in enumerate(MEMBERS):
        a = ADMINS[p] + [None] * (3 - len(ADMINS[p]))
        names = ", ".join(x if x else "empty" for x in a)
        act(ep, s, 60 + 4 * k, CREATOR[p], p, "SetPoolAdmins", "creator names the admins" if ADMINS[p] else "creator names no admin (the list stays empty)", "OK",
            params=f"admins: {names}", note="EXPECT GetPool shows the three admin ids (NULL_ID for an empty slot); the creator and the admins are the pool's operators")
    act(ep, s, 100, ADMINS["P1"][0], "P1", "SetPoolAdmins", "an admin tries to name admins", ref("NOT_AUTHORIZED"), "admins cannot name admins; nothing is written")
    act(ep, s, 100, MEMBERS["P1"][-1], "P1", "SetPoolAdmins", "a member tries to name admins", ref("NOT_AUTHORIZED"))
    act(ep, s, 100, CREATOR["P2"], "P1", "SetPoolAdmins", "the creator of another pool", ref("NOT_AUTHORIZED"))
    act(ep, s, 101, CREATOR["P1"], "P1", "SetPoolAdmins", "the creator names himself", ref("INVALID_INPUT"), "the creator is an operator already")
    act(ep, s, 101, CREATOR["P1"], "P1", "SetPoolAdmins", "the same id twice", ref("INVALID_INPUT"), f"admins: {ADMINS['P1'][0]}, {ADMINS['P1'][0]}, empty")
    act(ep, s, 102, CREATOR["P1"], "P1", "SetPoolAdmins", "the same list again", "OK", f"admins: {', '.join(ADMINS['P1'])}, empty; nothing is written")
    act(ep, s, 103, U(40), "P1", "GetPool", "the admin list is public", note="EXPECT admin0 and admin1 as named, admin2 empty (NULL_ID)")
    s = "E1.S4 withdrawals"
    t = 80
    act(ep, s, t, U(10), "P1", "WithdrawUncommitted", "partial", params="50,000")
    unc["P1"][U(10)] -= 50_000
    act(ep, s, t + 1, U(10), "P1", "WithdrawUncommitted", "more than the balance", ref("INSUFFICIENT_BALANCE"), "balance + 1")
    act(ep, s, t + 2, U(10), "P1", "WithdrawUncommitted", "zero amount", ref("INVALID_INPUT"), "0")
    act(ep, s, t + 3, U(40), "P1", "WithdrawUncommitted", "non-member", ref("NOT_MEMBER"), "1")
    v = unc["P7"][U(23)]
    act(ep, s, t + 4, U(23), "P7", "WithdrawUncommitted", "withdraw everything", params=f"{v:,}")
    unc["P7"][U(23)] = 0
    contribute(ep, s, t + 6, "P7", U(23), 100_000, "re-contribute after a full withdrawal (still on the roster: no new join fee)")
    s = "E1.S5 market and asset deposits"
    act(ep, s, 90, U(35), "-", "EXT:setup", "ISSUER1 issues TOK1 on QX; MM1 asks 1,500@1,000, 1,500@1,020, 2,000@1,050")
    act(ep, s, 90, U(36), "-", "EXT:setup", "ISSUER2 issues TOK2 on QX; MM2 asks 3,000@500, 3,000@520, 4,000@560")
    act(ep, s, 91, U(33), "-", "EXT:setup", "MM3 bids TOK1 1,000@1,100, 1,000@1,080, 2,000@1,060 and TOK2 2,000@580, 2,000@560")
    act(ep, s, 92, U(34), "-", "EXT:setup", "LP seeds QSWAP TOK1/QU with 20,000,000 QU and 20,000 TOK1; swapFeeRate read live")
    for price, q in ((1000, 1500), (1020, 1500), (1050, 2000)):
        qx.add_ask_resting("MM1", price, q)
    act(ep, s, 100, CREATOR["P3"], "P3", "DepositHoldingAsset", "creator deposits TOK2 (token ledger bound)", params="TOK2 x 5,000",
        note="creator owns every seat; ledger of ISSUER2 bound to QPOOL")
    act(ep, s, 101, CREATOR["P8"], "P8", "DepositHoldingAsset", "creator deposits TOK1", params="TOK1 x 3,000")
    act(ep, s, 102, U(21), "P3", "DepositHoldingAsset", "member, not the creator", ref("NOT_AUTHORIZED"), "TOK2 x 100")
    act(ep, s, 103, CREATOR["P3"], "P3", "DepositHoldingAsset", "second holding in the same pool", params="TOK2 x 2,000")
    tp["P3"].shares, tp["P3"].cost = 7_000, {CREATOR["P3"]: Fraction(1)}
    tp["P8"].shares, tp["P8"].cost = 3_000, {CREATOR["P8"]: Fraction(1)}
    s = "E1.S6 first governance wave (all four proposal types in use)"
    qx_buy(ep, s, 110, "P1", 1_030, 1_200, 1_200_000, "P1 buys TOK1 on QX")
    qx_buy(ep, s, 112, "P2", 1_025, 700, 700_000, "P2 buys TOK1 on QX against P1 for the same asks")
    qx_buy(ep, s, 114, "P7", 520, 200, 100_000, "P7 buys TOK2 on QX (proposal left to expire)")
    ACT[-1]["note"] += " | NOTE: nobody calls Execute for P7 inside its window"
    qx_buy(ep, s, 115, "P9", 1_060, 300, 320_000, "P9 (the large public pool) buys TOK1 on QX against the remaining asks")
    qswap_buy(ep, s, 116, "P4", 400_000, "P4 buys TOK1 on QSWAP")
    qswap_buy(ep, s, 117, "P10", 300_000, "P10 buys TOK1 on QSWAP")
    gov(ep, s, 118, "P6", "BUY_QSWAP", "300,000 QU for TOK1", "P6 BUY_QSWAP is REJECTED", quorum_fail=True)
    act(ep, s, 120, U(40), "P1", "Propose", "non-member", ref("NOT_MEMBER"), "any kind")
    act(ep, s, 121, MEMBERS["P2"][1], "P2", "Propose", "asset name 0", ref("INVALID_INPUT"), "BUY_QX")
    act(ep, s, 122, MEMBERS["P3"][1], "P3", "Propose", "amount above the pool's uncommitted", ref("INSUFFICIENT_BALANCE"), "BUY_QX 999,999,999")
    act(ep, s, 123, CREATOR["P1"], "P1", "Propose", "second live proposal by the same proposer", ref("PROPOSAL_PENDING"), "BUY_QX")
    s = "E1.S7 reconcile, dividends, claims"
    t = 420
    for p in ("P1", "P2"):
        act(ep, s, t, U(40), p, "ReconcileHolding", "after the buys", "OK", note="OK or NOTHING to do; must not write when nothing is pending")
        act(ep, s, t + 1, U(40), p, "ReconcileHolding", "again with nothing pending", ref("NOTHING_OWED") + "|OK", "no state write")
    dividend_event(ep, s, t + 5, U(35), "TOK1", 600_000, {p: qx_holder(p) for p in ("P1", "P2", "P8", "P9") if tp[p].shares > 0},
                   "ISSUER1 pays TOK1 dividend 600,000: P1, P2, P8 and P9 hold TOK1 (P10 holds TOK1 through QSWAP and is a holder only if its purchase is a token holding: see the note)")
    for p in ("P1", "P2", "P8", "P9"):
        for u in [x for x in MEMBERS[p] if tp[p].cost.get(x, 0) > 0][:3]:
            act(ep, s, t + 10, u, p, "ClaimPayout", "claim by a member who PAID for the seat", note="EXPECT amount from the dividend line above, +/-1 QU")
    act(ep, s, t + 12, MEMBERS["P1"][1], "P1", "ClaimPayout", "second claim, nothing owed", ref("NOTHING_OWED"))
    act(ep, s, t + 13, U(40), "P1", "ClaimPayout", "non-member", ref("NOT_MEMBER"))
    s = "E1.S8 seat market"
    t = 440
    a, b = MEMBERS["P1"][1], MEMBERS["P1"][2]
    act(ep, s, t, a, "P1", "OfferPosition", "part of a seat", params="30% of seat, ask 400,000")
    act(ep, s, t + 1, b, "P1", "BuyPosition", "member buys, exact ask", params="attach 400,000; expectedCostUnits and expectedShares from GetPosition", note="fee 1% = 4,000 to revenue; seller +396,000")
    rev[ep] += 4_000
    act(ep, s, t + 2, a, "P1", "OfferPosition", "second offer", params="10% of seat, ask 150,000")
    act(ep, s, t + 3, U(40), "P1", "BuyPosition", "non-member buys in a public pool (ask + join fee)", params="attach 160,000", note="roster +1; revenue +1,500 fee +10,000 join")
    rev[ep] += 1_500 + JOIN_FEE
    roster["P1"].add(U(40))
    act(ep, s, t + 4, a, "P1", "OfferPosition", "third offer", params="5% of seat, ask 90,000")
    act(ep, s, t + 5, a, "P1", "CancelOffer", "own offer")
    act(ep, s, t + 6, a, "P1", "CancelOffer", "no offer left", ref("NO_OFFER"))
    act(ep, s, t + 7, U(40), "P3", "BuyPosition", "non-member of a private pool", ref("NOT_ADMITTED"), "attach any")
    act(ep, s, t + 8, b, "P1", "BuyPosition", "stale expectedCostUnits (offer replaced)", ref("OFFER_CHANGED"), "refund, no write")
    act(ep, s, t + 9, b, "P1", "BuyPosition", "under-paid", ref("INSUFFICIENT_FEE") + "|" + ref("INSUFFICIENT_BALANCE"))
    act(ep, s, t + 10, MEMBERS["P1"][3], "P1", "OfferPosition", "more than owned", ref("INVALID_INPUT"), "150% of seat")
    s = "E1.S9 managed shares, closing, squatting, strays"
    t = 470
    act(ep, s, t, MEMBERS["P2"][1], "-", "ReleaseManagedShares", "take back shares owned by the caller", params="TOK1 x 50 after QX hand-over to QPOOL")
    act(ep, s, t + 1, MEMBERS["P2"][2], "-", "ReleaseManagedShares", "shares not under QPOOL's management", ref("TRANSFER_FAILED") + "|" + ref("INVALID_INPUT"))
    act(ep, s, t + 2, U(40), "P0", "WithdrawUncommitted", "scratch pool: nothing to withdraw", ref("INVALID_INPUT") + "|" + ref("NOT_MEMBER"))
    act(ep, s, t + 3, U(40), "P0", "CloseEmptyPool", "empty scratch pool", "OK", note="pool slot freed")
    act(ep, s, t + 4, U(40), "P7", "CloseEmptyPool", "non-empty pool", ref("INVALID_INPUT") + "|" + ref("WRONG_STATUS"))
    act(ep, s, t + 5, U(40), "P8", "EvictSquat", "ledger is fresh and has an owner", ref("WRONG_STATUS") + "|" + ref("INVALID_INPUT") + "|" + ref("NOT_FOUND"))
    act(ep, s, t + 6, U(40), "-", "EXT:stray", "plain transfer of 1,234 QU from a wallet that matches no ledger", note="pendingRevenue +1,234 (not globalUnattributed)")
    rev[ep] += 1_234
    act(ep, s, t + 7, "-", "-", "EXT:donation", "revenue donation 50,000", note="pendingRevenue +50,000")
    rev[ep] += 50_000
    for f, args in (("GetPool", "every pool"), ("GetMember", "every user of P1 to P3"), ("GetHolding", "every holding"),
                    ("GetPosition", "every member of P1"), ("GetProposal", "every proposal of this epoch"), ("GetStats", "once"),
                    ("GetSource", "TOK1 and TOK2 ledgers")):
        act(ep, "E1.S10 checkpoint reads", 480, U(40), "-", f, args, note="compare with checkpoints.md and run invariants I1 to I12")
    end_epoch(ep)


def e2():
    ep = 2
    s = "E2.S1 IPO round 1: target T1 (single IPO), 5 pools"
    specs = [("P1", 200_000, 2_000), ("P2", 260_000, 2_100), ("P5", 150_000, 1_800), ("P6", 240_000, 2_200), ("P9", 180_000, 2_050)]
    ipo_pipeline(ep, s, 0, specs, "T1", quorum_fail_pool="P5")
    act(ep, s, 60, MEMBERS["P5"][1], "P5", "Propose", "BUY_IPO opening price owned by another holding", "OK", note="passes the vote; Execute must be refused IPO_PRICE_TAKEN if P1 holds 2,000", params="opening 2,000")
    act(ep, s, 70, CREATOR["P5"], "P5", "Execute", "opening price taken by P1", ref("IPO_PRICE_TAKEN"), "nothing written; the proposal now FAILS")
    act(ep, s, 71, U(40), "P5", "GetProposal", "after IPO_PRICE_TAKEN", note="EXPECT status FAILED (5), the YES voters' locks released (GetMember.locked 0): K7 fix N5")
    act(ep, s, 72, U(40), "P5", "Execute", "again on the FAILED proposal", ref("WRONG_STATUS"), "nothing written")
    act(ep, s, 73, MEMBERS["P5"][1], "P5", "Propose", "a new proposal right after the failed one", "OK", note="not PROPOSAL_PENDING any more (before K7 the PASSED proposal kept the proposer blocked); left to expire")
    act(ep, s, 74, MEMBERS["P2"][1], "P2", "Propose", "BUY_IPO with a NON-ZERO asset name", ref("INVALID_INPUT"), "asset name = any non-zero value; a BUY_IPO takes no name; no proposal created")
    act(ep, s, 80, MEMBERS["P2"][1], "P2", "Propose", "BUY_IPO with budget below the opening price", ref("INVALID_INPUT"), "budget 1,000, opening 2,000")
    act(ep, s, 81, MEMBERS["P2"][1], "P2", "Propose", "BUY_IPO opening price 0", ref("INVALID_INPUT"))
    F, res = war(ep, s, 100, "T1", specs, 1_900, 676 - sum(b // o for _, b, o in specs), seed=11, steps=34, snipe=("OUT2", 2_650, 60))
    s = "E2.S2 settle too early"
    act(ep, s, 400, U(40), "P1", "SettleIpo", "before END_EPOCH has taken the snapshot", ref("WINDOW_OPEN") + "|" + ref("IPO_NO_SNAPSHOT"), "no write")
    s = "E2.S3 other pools keep trading while the IPO runs"
    qx.add_bid_resting("MM3", 1_100, 1_000)
    qx.add_bid_resting("MM3", 1_080, 1_000)
    qx.add_bid_resting("MM3", 1_060, 2_000)
    act(ep, s, 410, MEMBERS["P2"][1], "P2", "Propose", "SELL_QX of TOK1 (QX-managed)", params="limit 1,070 x 300; min proceeds > 200",
        note="see trading-expected/t2.txt for the formula; handover 0 for QX-managed shares")
    sync_in("P2")
    log = []
    tp["P2"].sell_qx(qx, 1_070, 300, 0, log.append)
    ACT[-1]["note"] += " | EXPECT: " + log[-1]
    rev[ep] += int(log[-1].split("management fee ")[1].split(";")[0].replace(",", ""))
    act(ep, s, 420, CREATOR["P3"], "P3", "Propose", "SELL_QX of deposited TOK2 shares", params="limit 560 x 2,000", note=f"hand-over cost {HANDOVER} (MEASURE: 0/100/200)")
    qswap_buy(ep, s, 430, "P6", 300_000, "P6 buys TOK1 on QSWAP")
    got = amm.sell(500)
    act(ep, s, 440, MEMBERS["P4"][1], "P4", "Propose", "SELL_QSWAP of 500 TOK1", params="min proceeds 100,200",
        note=f"EXPECT QSWAP pays {got:,} QU; minus 100,000 flat fee -> credited {got - 100_000:,}; management fee {(got - 100_000) // 100:,}")
    rev[ep] += (got - 100_000) // 100
    act(ep, s, 441, MEMBERS["P4"][1], "P4", "Propose", "SELL_QSWAP with min proceeds at or below 100,100", ref("INVALID_INPUT"))
    act(ep, s, 442, MEMBERS["P4"][2], "P4", "Propose", "SELL_QSWAP of a contract share", ref("QX_ONLY"), "contract shares trade on QX only")
    act(ep, s, 450, MEMBERS["P7"][1], "P7", "Propose", "ALLOCATE (documented dead path)", "OK", note="Execute returns NOTHING_OWED")
    act(ep, s, 451, CREATOR["P7"], "P7", "Execute", "ALLOCATE", ref("NOTHING_OWED"))
    dividend_event(ep, s, 460, U(36), "TOK2", 120_000, {"P3": qx_holder("P3")}, "ISSUER2 pays TOK2 dividend 120,000: only P3's deposited holdings hold TOK2")
    s = "E2.S4 membership churn and visibility"
    act(ep, s, 470, CREATOR["P8"], "P8", "SetPermissioned", "private to public", params="0")
    contribute(ep, s, 471, "P8", U(40), 300_000, "newcomer joins a public pool")
    act(ep, s, 472, CREATOR["P8"], "P8", "SetPermissioned", "public back to private", params="1", note="U40 stays a member")
    act(ep, s, 473, U(19), "P8", "Contribute", "not admitted after switching back", ref("NOT_ADMITTED"), "attach 100,000")
    act(ep, s, 474, U(40), "P8", "WithdrawUncommitted", "member of a private pool can still withdraw", params="100,000")
    unc["P8"][U(40)] -= 100_000
    for p in ("P2", "P4"):
        for u in MEMBERS[p][3:5]:
            contribute(ep, s, 480, p, u, 100_000, "mid-epoch top-up")
    end_epoch(ep)



def settle_scene(ep, s, plan, dt=4, mid_dividend=None):
    """K8: at the epoch start BEGIN_EPOCH pairs the IPO names and settles up to AUTO_SETTLE_MAX ready holdings; the plan settles the rest by hand.
    plan: [(target, [pools, in the order the plan settles them by hand])]. A manual call on a holding BEGIN_EPOCH already settled is refused IPO_NOT_ACTIVE."""
    holdings = [(tgt, p) for tgt, order in plan for p in order]
    n = len(holdings)
    auto = min(AUTO_SETTLE_MAX, n)
    wins = [tgt for tgt, _ in plan if RESULTS[tgt][0] > 0]
    act(ep, s, 0, "-", "-", "EXT:epoch change", f"BEGIN_EPOCH of epoch {ep}: pairs the IPO names, then settles up to {AUTO_SETTLE_MAX} ready holdings",
        note=f"EXPECT autoSettleEpoch {ep}, autoSettleDone {auto}, autoSettleSkipped 0, readyUnsettled {n - auto}; which {auto} holdings were taken follows the table's order: read GetHolding.ipoSettled for each")
    for tgt, _ in plan:
        F = RESULTS[tgt][0]
        if F > 0:
            how = "by book fingerprint (several IPOs finished in this epoch change)" if len(wins) > 1 else "by count (one IPO finished)"
            act(ep, s, 1, U(40), "-", "GetNameRegistry", f"{tgt}: the name read from the chain",
                note=f"EXPECT nameState 2 (paired), nameIndex = {tgt}'s contract index, name = {tgt}'s share name as the explorer shows it under the zero issuer; paired {how}; mismatches unchanged")
        else:
            act(ep, s, 1, U(40), "-", "GetNameRegistry", f"{tgt}: the IPO failed, so no name exists",
                note=f"EXPECT name 0 for {tgt}'s index (no shares were issued), nothing paired for it; {len(wins)} name(s) paired in this epoch change")
    act(ep, s, 1, U(40), "-", "GetStats", "automatic settlement fields", note=f"EXPECT autoSettleDone {auto}, readyUnsettled {n - auto}; invariant I16")
    k = 0
    t = 2
    for tgt, order in plan:
        F, res = RESULTS[tgt]
        for p in order:
            sh, cb, ret, bud = res[p]
            who = U(40) if k % 2 == 0 else MEMBERS[p][2]
            act(ep, s, t, who, p, "SettleIpo", f"{tgt} holding of {p}, manual call {k + 1} of {n} (by {'KEEPER' if k % 2 == 0 else 'a member'})", "OK|" + ref("IPO_NOT_ACTIVE"),
                note=f"OK if BEGIN_EPOCH did not take it: EXPECT shares {sh}, costBasis {cb:,}, returned {ret:,} to members by cost"
                     + (f" (FAILED IPO: whole budget {bud:,} back, the holding retires)" if F == 0 else "")
                     + "; IPO_NOT_ACTIVE (state unchanged) if it was already settled; the end result must be the same either way")
            credit(p, ret, ASSIGNED[(tgt, p)])
            if mid_dividend and k == mid_dividend[0]:
                act(ep, s, t + 2, mid_dividend[1], "-", "EXT:pay dividend", mid_dividend[2],
                    note="the part of this dividend that belongs to holdings still unsettled is parked in escrow until the LAST holding settles (read GetStats.readyUnsettled first)")
            k += 1
            t += dt
    return t


def cancel_scene_a(ep):
    """K7 CancelQxBid on resting bids of TOK2 in pool P1 (every TOK2 ask is above 410, so the bids rest). Part 1: authority, the blocked pool, a full cancel by the creator.
    Part 2: a partial fill, then a funder cancels. Part 3: the age limit (a stranger is refused until the bid is older than 72 hours = 144 devkit ticks)."""
    s = "E3.S7 CancelQxBid: resting bids on QX (K7)"
    pool, other = "P1", "P6"       # P1's creator has no live proposal in this stretch; P6's creator is busy with E3.S5
    outsider_member = [u for u in MEMBERS[other] if u not in MEMBERS[pool]][0]
    # ---- part 1
    t = 200
    ensure_yes(ep, s, t, pool, 40_000, ("yes",))
    yes, passed = gov(ep, s, t + 10, pool, "BUY_QX", "limit 400 x 100 of TOK2, budget 40,000 (every ask is above 400: the whole bid rests)", f"{pool} resting BUY_QX of TOK2 at 400", pattern=("yes",))
    assert passed
    a1 = charge(pool, 40_000, yes)
    t1 = t + 10 + VOTE + 20
    act(ep, s, t1, U(40), pool, "GetHolding", "the holding with a resting bid", note="EXPECT shares 0, pendingShares 100, costBasis 40,000, qxBidPrice 400, qxBidTick = the Execute tick; GetSource(TOK2 ledger).pendingQx 100 (I14)")
    act(ep, s, t1 + 1, MEMBERS[other][1], other, "Propose", f"BUY_QX of TOK2 while {pool}'s bid rests on the same ledger (not blocked)", "OK", "limit 450 x 10, budget 4,500; K8.2: several pools may rest a bid on one token, so the second pool is not blocked (left to expire)")
    act(ep, s, t1 + 2, U(40), pool, "CancelQxBid", "stranger before the age limit", ref("NOT_AUTHORIZED"), "attach 777 QU: returned; nothing changes")
    act(ep, s, t1 + 3, outsider_member, pool, "CancelQxBid", "member of another pool, no seat here", ref("NOT_AUTHORIZED"), "nothing changes")
    act(ep, s, t1 + 4, CREATOR[pool], other, "CancelQxBid", "the right holding id but the wrong pool", ref("NOT_FOUND"), "nothing changes")
    act(ep, s, t1 + 5, CREATOR[pool], pool, "CancelQxBid", "the creator cancels the whole bid", "OK",
        note="EXPECT cancelledShares 100, refunded 40,000 credited to the funders by what each paid; balance +40,000; globalUnattributed unchanged (I15); the holding and the ledger entry are gone; no QPOOL bid left on QX")
    credit(pool, 40_000, a1)
    act(ep, s, t1 + 6, CREATOR[pool], pool, "CancelQxBid", "the same cancel again", ref("NOT_FOUND"), "nothing changes")
    act(ep, s, t1 + 7, MEMBERS[other][1], other, "Propose", "BUY_QX of TOK2 after the cancel", ref("PROPOSAL_PENDING"), note="the same member's proposal above is still live (a second one is refused)")
    # ---- part 2
    t = 380
    ensure_yes(ep, s, t, pool, 41_000, ("yes",))
    yes, passed = gov(ep, s, t + 10, pool, "BUY_QX", "limit 410 x 100 of TOK2, budget 41,000 (rests)", f"{pool} second resting BUY_QX of TOK2", pattern=("yes",))
    assert passed
    a2 = charge(pool, 41_000, yes)
    t2 = t + 10 + VOTE + 20
    act(ep, s, t2, U(32), "-", "EXT:setup", "MM2 lists 30 TOK2 at 410: it fills 30 shares of the resting bid at the bid's own price 410 (QPOOL does not see it yet)")
    act(ep, s, t2 + 2, non_operators(pool, yes)[0], pool, "CancelQxBid", "a funder who is not an operator", ref("NOT_AUTHORIZED"), "K8.1: funding the bid does not give the right to cancel it; nothing is written")
    act(ep, s, t2 + 3, ADMINS[pool][0], pool, "CancelQxBid", "an admin (not the creator) cancels after a partial fill", "OK",
        note="EXPECT cancelledShares 70, refunded 28,700 (70 x 410); the 30 filled shares are credited by the same call: shares 30, pendingShares 0, costBasis 12,300; funders' seats shrink by the refund; offers on shrunk seats withdrawn")
    credit(pool, 70 * 410, a2)
    act(ep, s, t2 + 4, U(40), pool, "GetHolding", "after the cancel", note="EXPECT shares 30, pendingShares 0, qxBidPrice 0; GetSource pendingQx 0 (I14)")
    # ---- part 3: the age limit
    t = 560
    ensure_yes(ep, s, t, pool, 40_500, ("yes",))
    yes, passed = gov(ep, s, t + 10, pool, "BUY_QX", "limit 405 x 100 of TOK2, budget 40,500 (rests)", f"{pool} third resting BUY_QX of TOK2", pattern=("yes",))
    assert passed
    a3 = charge(pool, 40_500, yes)
    te = t + 10 + VOTE + 8          # the Execute row of the pipeline above; the bid is placed at this tick
    act(ep, s, te + 60, U(40), pool, "CancelQxBid", "stranger, bid younger than the age limit", ref("NOT_AUTHORIZED"), "attach 500 QU: returned")
    act(ep, s, te + VOTE, U(40), pool, "CancelQxBid", "stranger, bid age exactly equal to the limit (72 hours = 144 devkit ticks)", ref("NOT_AUTHORIZED"), "strictly greater is required; the Execute tick may differ by a tick or two from the plan: compute the age from GetHolding.qxBidTick")
    act(ep, s, te + VOTE + 2, U(40), pool, "CancelQxBid", "stranger, bid older than the limit", "OK",
        note="EXPECT cancelledShares 100, refunded 40,500 credited to the funders, nothing to the caller; I15")
    credit(pool, 40_500, a3)


def cancel_scene_b(ep):
    """K7 CancelQxBid when the bid filled completely before anyone reconciled."""
    s = "E5.S7 CancelQxBid: a bid that already filled (K7)"
    pool = "P1"
    t = 200
    ensure_yes(ep, s, t, pool, 21_000, ("yes",))
    yes, passed = gov(ep, s, t + 10, pool, "BUY_QX", "limit 420 x 50 of TOK2, budget 21,000 (rests)", "P1 resting BUY_QX of TOK2 at 420", pattern=("yes",))
    assert passed
    charge(pool, 21_000, yes)
    t1 = t + 10 + VOTE + 20
    act(ep, s, t1, U(32), "-", "EXT:setup", "MM2 lists 50 TOK2 at 420: it fills P1's whole resting bid (QPOOL does not see it yet)")
    act(ep, s, t1 + 3, CREATOR[pool], pool, "CancelQxBid", "every share already filled, nobody reconciled", ref("NO_PENDING_BID"), "attach 5,000 QU: returned; state byte for byte unchanged (pendingShares still 50 until a reconcile)")
    act(ep, s, t1 + 5, U(40), pool, "ReconcileHolding", "credits the 50 filled shares", "OK", note="EXPECT shares 50, pendingShares 0, costBasis 21,000")
    act(ep, s, t1 + 7, CREATOR[pool], pool, "CancelQxBid", "nothing pending after the reconcile", ref("NO_PENDING_BID"), "nothing changes")


def e3():
    ep = 3
    s = "E3.S1 BEGIN_EPOCH pairs T1's name and settles T1's holdings; the rest by hand; a dividend arrives in the middle"
    F, res = RESULTS["T1"]
    order = ["P6", "P9", "P1", "P5", "P2"]
    settle_scene(ep, s, [("T1", order)], mid_dividend=(1, U(35), "T1 contract pays distributeDividends while some holdings may still be unsettled"))
    act(ep, s, 40, U(40), "P6", "SettleIpo", "second call on a settled holding", ref("IPO_NOT_ACTIVE") + "|" + ref("NOT_FOUND") + "|" + ref("WRONG_STATUS"), "idempotent; state unchanged")
    act(ep, s, 41, MEMBERS["P1"][1], "P1", "SettleIpo", "holding id that does not exist", ref("NOT_FOUND") + "|" + ref("INVALID_INPUT"))
    s = "E3.S2 contract dividends and claims"
    h = {p: ipo_holder("T1", p) for p in order if RESULTS["T1"][1][p][0] > 0}
    dividend_event(ep, s, 60, U(35), "T1 shares (distributeDividends)", 350 * sum(v[0] for v in h.values()), h,
                   "T1 pays perShare 350: QPOOL receives 350 x the shares its pools hold, split by shares between the holdings and by cost inside each")
    for p in h:
        for u in list(h[p][1])[:3]:
            act(ep, s, 70, u, p, "ClaimPayout", "claim T1 dividend (a member who paid)")
    act(ep, s, 75, U(40), "P5", "ReconcileHolding", "IPO holding after settlement", ref("NOTHING_OWED") + "|OK")
    s = "E3.S3 selling contract shares (QX only); contract shares are trusted only through the registry"
    act(ep, s, 78, MEMBERS["P3"][1], "P3", "Propose", "BUY_QX of T1 shares by a pool that never held them (name paired)", "OK", params="asset {NULL_ID, T1 name}, holdingId = T1's index", note="the pair is in the registry; left to expire")
    act(ep, s, 79, MEMBERS["P7"][1], "P7", "Propose", "BUY_QX of T1's index with ANOTHER contract's name", ref("CONTRACT_MISMATCH"), "asset name = a table contract's name (for example QX), holdingId = T1's index; nothing written")
    act(ep, s, 79, MEMBERS["P4"][1], "P4", "Propose", "BUY_QX of a contract above index 30 whose name was never paired", ref("NAME_PENDING") + "|" + ref("CONTRACT_NOT_LIVE"), "an index that never IPO'd; nothing written")
    act(ep, s, 80, MEMBERS["P1"][1], "P1", "Propose", "SELL_QX of T1 shares", params="limit = 2,000 x all seats", note="contract shares sell on QX only")
    act(ep, s, 81, MEMBERS["P6"][1], "P6", "Propose", "SELL_QSWAP of T1 shares", ref("QX_ONLY"))
    s = "E3.S4 QPAY buy-and-burn"
    buy_burn(ep, s, 100, [], "no asks listed yet")
    act(ep, s, 101, U(33), "-", "EXT:setup", "MM3 lists QPAY asks: 3,000@40, 5,000@50, 1,000@10,000, 500@10,001")
    buy_burn(ep, s, 110, [(40, 3_000), (50, 5_000), (10_000, 1_000), (10_001, 500)], "levels 40, 50 and 10,000 bought; 10,001 left (ceiling)")
    buy_burn(ep, s, 120, [(10_001, 500)], "only an ask above the ceiling")
    s = "E3.S5 governance variety"
    gov(ep, s, 140, "P3", "SELL_QX", "limit 570 x 1,000 of TOK2", "P3 SELL_QX with yes votes only", pattern=("yes",))
    gov(ep, s, 142, "P6", "BUY_QX", "limit 515 x 400, budget 200,000 of TOK2", "P6 BUY_QX TOK2 expires unexecuted", late_execute=True)
    gov(ep, s, 144, "P8", "BUY_QX", "limit 515 x 100, budget 50,000 of TOK2", "P8 BUY_QX", quorum_fail=True)
    act(ep, s, 160, MEMBERS["P6"][2], "P6", "Vote", "vote on an expired proposal", ref("WINDOW_CLOSED") + "|" + ref("WRONG_STATUS"))
    s = "E3.S6 stress the seat market"
    for k in range(5):
        act(ep, s, 170 + k, MEMBERS["P7"][1], "P7", "OfferPosition", f"offer {k + 1} of 5", params="5% of seat")
    act(ep, s, 176, MEMBERS["P7"][1], "P7", "CancelOffer", "cancel one")
    act(ep, s, 190, MEMBERS["P4"][1], "-", "ReleaseManagedShares", "owner takes back shares under QPOOL's management", params="TOK2 x 20")
    act(ep, s, 191, U(40), "-", "ReleaseManagedShares", "caller owns none of these shares", ref("TRANSFER_FAILED") + "|" + ref("INVALID_INPUT"))
    cancel_scene_a(ep)
    end_epoch(ep)


def e4():
    ep = 4
    s = "E4.S1 IPO round 2: TWO IPOs in the same epoch, T2 (wins) and T3 (fails), 10 pools"
    t2 = [("P2", 300_000, 2_000), ("P3", 150_000, 1_500), ("P4", 200_000, 1_800), ("P6", 180_000, 1_700), ("P7", 80_000, 1_600), ("P10", 100_000, 1_900)]
    t3 = [("P1", 240_000, 1_200), ("P5", 120_000, 1_000), ("P8", 100_000, 900), ("P9", 45_000, 950)]
    ipo_pipeline(ep, s, 0, t2, "T2")
    ipo_pipeline(ep, s, 30, t3, "T3")
    act(ep, s, 90, U(40), "-", "GetIpoTarget", "two targets live", note="EXPECT two rows; the ninth target is refused (capacity 8)")
    war(ep, s, 100, "T2", t2, 1_550, 676 - sum(b // o for _, b, o in t2), seed=21, steps=36)
    war(ep, s + " (T3 interleaved)", 130, "T3", t3, 800, 90, seed=25, steps=18, cap_slots=640)
    act(ep, s, 400, CREATOR["P2"], "P2", "RebidIpo", "after the opening bid of T3 was never made by P2", ref("IPO_NOT_ACTIVE") + "|" + ref("NOT_FOUND"), "no holding in T3")
    act(ep, s, 405, MEMBERS["P2"][1], "P2", "CancelQxBid", "an IPO holding (its pending shares are seats, not a QX bid)", ref("WRONG_STATUS"), "holding of T2; nothing written")
    s = "E4.S2 trading and dividends continue"
    qx.add_ask_resting("MM4", 1_040, 2_000)
    qx_buy(ep, s, 420, "P1", 1_040, 300, 300_000, "P1 second buy of TOK1")
    qx_buy(ep, s, 422, "P6", 1_040, 200, 210_000, "P6 buys TOK1 on QX")
    qx_buy(ep, s, 423, "P9", 1_045, 250, 270_000, "P9 second buy of TOK1 (MM4's new asks)")
    qswap_buy(ep, s, 424, "P4", 150_000, "P4 second QSWAP buy")
    dividend_event(ep, s, 440, U(35), "TOK1", 250_000, {p: qx_holder(p) for p in ("P1", "P2", "P6", "P8", "P9") if tp[p].shares > 0}, "ISSUER1 pays TOK1 dividend 250,000")
    s = "E4.S3 seat market and withdrawals"
    for p in ("P2", "P6"):
        for u in MEMBERS[p][1:3]:
            act(ep, s, 450, u, p, "OfferPosition", "offer")
        act(ep, s, 452, MEMBERS[p][3], p, "BuyPosition", "buy the offer")
    act(ep, s, 455, MEMBERS["P2"][1], "P2", "OfferPosition", "seat locked by an open SELL vote", ref("SEAT_LOCKED"), "after voting on a SELL")
    for u in MEMBERS["P4"][:3]:
        act(ep, s, 460, u, "P4", "WithdrawUncommitted", "partial")
    breadth(ep, 470)
    _old = list(ADMINS["P4"])
    _new = [MEMBERS["P4"][2], MEMBERS["P4"][3]]
    act(ep, "E4.S5 operators: P4's creator replaces the admin list", 880, CREATOR["P4"], "P4", "SetPoolAdmins", "replace the admin list (one admin leaves, one joins)", "OK",
        params=f"admins: {', '.join(_new)}, empty", note=f"{_old[0]} is no longer an operator: from now on his Execute, RebidIpo and CancelQxBid return NOT_AUTHORIZED")
    act(ep, "E4.S5 operators: P4's creator replaces the admin list", 881, _old[0], "P4", "SetPoolAdmins", "the removed admin tries to name admins", ref("NOT_AUTHORIZED"))
    ADMINS["P4"] = _new
    OPS["P4"] = [CREATOR["P4"]] + _new
    act(ep, s, 700, U(40), "P3", "EvictSquat", "ledger with an owner who earned dividends", ref("WRONG_STATUS") + "|" + ref("INVALID_INPUT"))
    end_epoch(ep)


def multibid_scene(ep):
    """K8.2: two pools rest a bid on TOK2 at the same time at different prices; a seller fills them in price order (the higher price first)."""
    s = "E5.S8 several pools rest QX bids on one token (K8.2)"
    pa, pb = "P1", "P6"
    t = 420
    ensure_yes(ep, s, t, pa, 20_000, ("yes",))
    ensure_yes(ep, s, t, pb, 20_500, ("yes",))
    ya, _ = gov(ep, s, t + 10, pa, "BUY_QX", "limit 400 x 50 of TOK2, budget 20,000 (rests)", f"{pa} bids 50 @ 400", pattern=("yes",))
    yb, _ = gov(ep, s, t + 12, pb, "BUY_QX", "limit 410 x 50 of TOK2, budget 20,500 (rests)", f"{pb} bids 50 @ 410 while {pa}'s bid rests", pattern=("yes",))
    aa = charge(pa, 20_000, ya)
    charge(pb, 20_500, yb)
    te = t + 10 + VOTE + 24
    act(ep, s, te, U(40), "-", "GetSource", "the TOK2 ledger with two resting bids", note="EXPECT pendingQx 100 (plus any other bid of the ledger); P6's bid at 410 ranks first, P1's at 400 second (I14)")
    act(ep, s, te + 1, U(32), "-", "EXT:setup", "MM2 sells 60 TOK2 at 390 into the book: QX serves the highest bid first, so P6's 50 fill at 410 and 10 of P1's at 400")
    act(ep, s, te + 3, U(40), pa, "ReconcileHolding", "anyone credits both bids", "OK",
        note="EXPECT P6's holding: shares 50, pendingShares 0 (it leaves the list); P1's holding: shares 10, pendingShares 40. Dividends set aside for these fills go to the bid that won them")
    act(ep, s, te + 4, U(40), pb, "ReconcileHolding", "the other pool's holding: nothing more to credit", "OK", note="writes nothing")
    act(ep, s, te + 6, CREATOR[pa], pa, "CancelQxBid", "P1's creator takes back the 40 that still rest", "OK",
        note="EXPECT cancelledShares 40, refunded 16,000 (40 x 400) to P1's funders; the 10 filled shares stay (cost 4,000)")
    credit(pa, 40 * 400, aa)
    act(ep, s, te + 7, CREATOR[pb], pb, "CancelQxBid", "P6's bid filled completely: nothing to cancel", ref("NO_PENDING_BID"), "nothing changes")
    act(ep, s, te + 8, U(40), "-", "GetSource", "the ledger afterwards", note="EXPECT the list holds no bid of these two pools; I14")


def restingsell_scene(ep):
    """K8.3: P6 rests a SELL on QX for the 50 TOK2 shares it holds; a buyer takes 20 of them; anyone credits the proceeds; the other 30 are taken back."""
    s = "E5.S9 a resting sell on QX (K8.3)"
    pool = "P6"
    t = 900
    gov(ep, s, t, pool, "SELL_QX_LIMIT", "limit 520 x 50 of P6's TOK2 holding; no minimum (quAmount 0)", "P6 rests a sell of its 50 TOK2 shares at 520", pattern=("yes",))
    te = t + VOTE + 24
    act(ep, s, te, U(40), pool, "GetHolding", "P6's TOK2 holding after the Execute", note="EXPECT shares 0, askShares 50, qxBidPrice 520 (the shares are on QX's book, not in the weight); I14 and I16")
    act(ep, s, te + 1, U(40), "-", "GetSource", "the TOK2 ledger", note="EXPECT qxSide 2 (asks), askTotal 50, qxCount 1, recognized without these 50")
    act(ep, s, te + 2, CREATOR["P1"], "P1", "Propose", "BUY_QX of TOK2 while P6's sell rests on the ledger", ref("QX_SIDE_BUSY"),
        "a ledger lists resting bids OR resting asks: nothing is written")
    act(ep, s, te + 3, U(33), "-", "EXT:setup", "MM3 bids 540 for 20 TOK2: QX fills it against P6's ask at the ask's own price 520",
        note="20 x 520 = 10,400; QX's fee floor(10,400 x 0.3%) + 1 = 32; QPOOL receives 10,368 outside any call (the hook files it under globalUnattributed)")
    act(ep, s, te + 4, U(40), "-", "GetStats", "globalUnattributed after the fill", note="EXPECT it rose by 10,368; nothing is credited to the holding yet")
    act(ep, s, te + 5, U(40), pool, "CancelQxAsk", "stranger, order younger than the age limit", ref("NOT_AUTHORIZED"), "attach 500 QU: returned; nothing changes")
    act(ep, s, te + 6, U(40), pool, "ReconcileHolding", "anyone credits the sale", "OK",
        note="EXPECT askShares 30; credited 10,349 (gross 10,400 less the fee BOUND floor(10,400 x 0.3%) + 20 shares = 51; QX really took 32) less the 1% management fee (103) = 10,246 to the holding's owners (claimable with ClaimPayout); globalUnattributed keeps the 19 that are left over until the last sell is gone; revenue +103")
    rev[ep] += 103
    act(ep, s, te + 7, U(40), pool, "ReconcileHolding", "the same reconcile again", "OK", note="writes nothing (byte for byte)")
    act(ep, s, te + 8, MEMBERS[pool][0], pool, "ClaimPayout", "a seat owner claims his part of the sale proceeds", note="EXPECT his cost share of 10,246")
    act(ep, s, te + 9, ADMINS[pool][0] if ADMINS[pool] else CREATOR[pool], pool, "CancelQxAsk", "an admin (or the creator) takes back the 30 that did not sell", "OK",
        note="EXPECT cancelledShares 30, pendingLeft 0; the holding has shares 30 and askShares 0; the TOK2 ledger lists no ask any more; the 19 left in globalUnattributed go to revenue (+19), so globalUnattributed is back to 0")
    rev[ep] += 19
    act(ep, s, te + 10, CREATOR[pool], pool, "CancelQxAsk", "the same cancel again", ref("NO_PENDING_ASK"), "nothing changes")
    act(ep, s, te + 11, U(40), "-", "GetSource", "the TOK2 ledger afterwards", note="EXPECT qxSide 0, askTotal 0; I14 and I16")


def e5():
    ep = 5
    s = "E5.S1 BEGIN_EPOCH pairs T2's name (T3 failed: no name) and settles part of T2 and T3; the rest by hand in two different orders"
    F2, r2 = RESULTS["T2"]
    F3, r3 = RESULTS["T3"]
    settle_scene(ep, s, [("T2", ["P7", "P10", "P2", "P6", "P4", "P3"]), ("T3", ["P8", "P9", "P5", "P1"])], dt=3)
    s = "E5.S2 contract T2 pays dividends; claims; resting bids fill later"
    h = {p: ipo_holder("T2", p) for p in r2 if r2[p][0] > 0}
    dividend_event(ep, s, 60, U(35), "T2 shares", 420 * sum(v[0] for v in h.values()), h, "T2 pays perShare 420 x the shares QPOOL's pools hold")
    for p in h:
        act(ep, s, 70, list(h[p][1])[0], p, "ClaimPayout", "claim T2 dividend (a member who paid)")
    act(ep, s, 72, MEMBERS["P1"][1], "P1", "OfferPosition", "offer a seat of the holding whose shares the next reconcile changes", params="10% of seat, ask 80,000")
    act(ep, s, 75, U(40), "P1", "ReconcileHolding", "resting TOK1 bid filled later by MM")
    act(ep, s, 76, U(40), "P1", "GetPosition", "offeredShares is the LIVE shares", note="EXPECT offeredShares = GetHolding.shares after the reconcile (K7: not a snapshot)")
    act(ep, s, 77, MEMBERS["P1"][2], "P1", "BuyPosition", "expectedShares from BEFORE the reconcile changed the holding", ref("OFFER_CHANGED"), "refund, no write; applies if the reconcile changed the shares (the plan expects it did)")
    act(ep, s, 78, MEMBERS["P1"][2], "P1", "BuyPosition", "expectedShares = GetPosition.offeredShares (live)", "OK", "attach 80,000; fee 1% = 800 to revenue; no re-offer needed")
    rev[ep] += 800
    s = "E5.S3 strays, donations and a spam burst"
    for k in range(20):
        act(ep, s, 80 + k, U(40), "-", "EXT:stray", f"1 QU stray {k + 1} of 20, one per tick", note="known cost vector: each writes state; measure node load")
    rev[ep] += 20
    act(ep, s, 105, "-", "-", "EXT:donation", "revenue donation 30,000")
    rev[ep] += 30_000
    s = "E5.S4 squatting and eviction"
    act(ep, s, 110, U(40), "P8", "EvictSquat", "dust holding that never earned", "OK", note="succeeds only when the predicate holds; record the epoch of the first success")
    act(ep, s, 111, U(40), "P3", "EvictSquat", "holding with real dividends credited", ref("WRONG_STATUS") + "|" + ref("INVALID_INPUT"))
    s = "E5.S5 QPAY buy-and-burn again, then capacity cases"
    buy_burn(ep, s, 120, [(45, 4_000), (60, 6_000), (9_999, 10)], "three levels incl. one just under the ceiling")
    for k in range(8):
        act(ep, s, 140 + k, MEMBERS["P7"][2], "P7", "OfferPosition", f"offer {k + 6}: capacity of 32 offers per pool", "OK" if k < 3 else "OK|" + ref("CAPACITY"))
    act(ep, s, 150, MEMBERS["P6"][1], "-", "ReleaseManagedShares", "owner takes back shares after a failed deposit", params="TOK1 x 30")
    act(ep, s, 151, U(40), "-", "ReleaseManagedShares", "shares owned by QPOOL itself (pool holdings)", ref("INVALID_INPUT") + "|" + ref("NOT_AUTHORIZED"), "owner is the contract")
    s = "E5.S6 many small members trade"
    for p in ("P1", "P6"):
        for u in (U(37), U(38), U(39)):
            act(ep, s, 160, u, p, "BuyPosition", "outsider buys a seat in a public pool", note="ask + join fee; roster +1")
            roster[p].add(u)
            rev[ep] += JOIN_FEE
    act(ep, s, 170, U(38), "P1", "WithdrawUncommitted", "member of a public pool withdraws everything")
    breadth(ep, 500)
    act(ep, s, 700, U(40), "P5", "EvictSquat", "second attempt on a dust ledger", "OK|" + ref("WRONG_STATUS"), "record the epoch of the first success")
    cancel_scene_b(ep)
    multibid_scene(ep)
    restingsell_scene(ep)
    end_epoch(ep)


def e6():
    ep = 6
    s = "E6.S1 IPO round 3: THREE IPOs in the same epoch (T4 big war, T5 fails, T6 a second war)"
    t4 = [("P1", 250_000, 2_500), ("P2", 350_000, 2_600), ("P4", 200_000, 2_400), ("P6", 300_000, 2_550), ("P7", 100_000, 2_300), ("P8", 150_000, 2_200),
          ("P9", 150_000, 2_450), ("P10", 80_000, 2_350)]
    t5 = [("P3", 80_000, 1_000), ("P5", 60_000, 900), ("P9", 50_000, 950)]
    t6 = [("P2", 200_000, 3_000), ("P6", 150_000, 2_900), ("P1", 100_000, 2_800), ("P8", 60_000, 2_500), ("P10", 90_000, 2_950)]
    ipo_pipeline(ep, s, 0, t4, "T4")
    ipo_pipeline(ep, s, 40, t5, "T5")
    ipo_pipeline(ep, s, 80, t6, "T6")
    act(ep, s, 120, MEMBERS["P8"][1], "P8", "Propose", "BUY_IPO for T6 with another contract's name in the name field", ref("INVALID_INPUT"), "asset name = T4's share name: a BUY_IPO takes no name, so no wrong name can be declared; nothing written")
    war(ep, s, 150, "T4", t4, 2_250, 676 - sum(b // o for _, b, o in t4), seed=31, steps=44, snipe=("OUT5", 3_200, 70))
    war(ep, s, 260, "T5", t5, 800, 200, seed=32, steps=14, cap_slots=640)
    war(ep, s, 300, "T6", t6, 2_700, 676 - sum(b // o for _, b, o in t6), seed=33, steps=30)
    act(ep, s, 480, operator("P1"), "P1", "RebidIpo", "one pool in three targets: rebid in the right holding", note="P1 bids in T4 and T6; the creator of P1 rebids")
    s = "E6.S2 other activity"
    qx_buy(ep, s, 500, "P2", 1_040, 150, 160_000, "P2 buys TOK1 while its IPO budgets are locked")
    act(ep, s, 510, MEMBERS["P4"][1], "P4", "Propose", "SELL_QSWAP", params="min proceeds 130,000")
    act(ep, s, 520, U(36), "-", "EXT:pay dividend", "ISSUER2 pays TOK2 dividend")
    end_epoch(ep)


def e7():
    ep = 7
    s = "E7.S1 BEGIN_EPOCH pairs the names of T4 and T6 by book fingerprint (T5 failed) and settles part of the 16 holdings; the rest by hand"
    settle_scene(ep, s, [("T4", ["P6", "P4", "P1", "P8", "P2", "P7", "P10", "P9"]), ("T5", ["P5", "P3", "P9"]), ("T6", ["P8", "P6", "P2", "P1", "P10"])], dt=3)
    act(ep, s, 58, U(40), "-", "GetNameRegistry", "T4 and T6 hold different bidders, so no quarantine", note="EXPECT mismatches unchanged from epoch 5 (0); paired +2 in this epoch change")
    s = "E7.S2 final dividends and claims"
    dividend_event(ep, s, 60, U(35), "TOK1", 500_000, {p: qx_holder(p) for p in ("P1", "P2", "P6", "P8", "P9") if tp[p].shares > 0}, "ISSUER1 final TOK1 dividend")
    for p in MEMBERS:
        for u in MEMBERS[p]:
            act(ep, s, 70, u, p, "ClaimPayout", "final claim by every member (each pool, each member)")
    s = "E7.S3 final QPAY burn and shutdown of the season"
    buy_burn(ep, s, 100, [(55, 8_000), (70, 8_000)], "final burn")
    for p in MEMBERS:
        act(ep, s, 110, CREATOR[p], p, "WithdrawUncommitted", "creator withdraws what is uncommitted")
    act(ep, s, 120, U(40), "-", "GetStats", "final", note="EXPECT globalUnattributed 0; pendingRevenue 0 after END_EPOCH; compare every total with checkpoints.md")
    for p in ("P7", "P8"):
        act(ep, s, 121, U(40), p, "CloseEmptyPool", "pool still has members or holdings", ref("INVALID_INPUT") + "|" + ref("WRONG_STATUS"))
    for f in FUNCS:
        act(ep, "E7.S4 final audit reads", 130, U(40), "-", f, "final sweep of every pool, member, holding, position, proposal, source, target", note="run invariants I1 to I12 on the final state")
    end_epoch(ep)


# ====================================================================================================== outputs + checks
def main():
    for fn in (e1, e2, e3, e4, e5, e6, e7):
        fn()
    os.makedirs(OUT, exist_ok=True)
    with open(f"{OUT}/cast.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["user", "role", "pools"])
        for u, r in ROLE.items():
            w.writerow([u, r, ",".join(p for p in MEMBERS if u in MEMBERS[p])])
    with open(f"{OUT}/pools.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["pool", "name", "kind", "creator", "members", "persona"])
        for p, nme, kind, c, ms in POOLS:
            w.writerow([p, nme, kind, U(c), " ".join(U(x) for x in ms), PERSONA[p]])
    cols = ["id", "epoch", "tick", "scene", "actor", "pool", "fn", "variant", "expect", "params", "note"]
    with open(f"{OUT}/actions.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for a in sorted(ACT, key=lambda a: (a["epoch"], a["tick"], a["id"])):
            w.writerow(a)
    with open(f"{OUT}/ipo-results.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["target", "final_price", "pool", "seats", "cost_basis", "returned", "budget"])
        for tgt, (F, res) in RESULTS.items():
            for p, (sh, cb, ret, bud) in res.items():
                w.writerow([tgt, F, p, sh, cb, ret, bud])
    with open(f"{OUT}/ipo-rounds.txt", "w") as f:
        f.write("\n\n".join(IPO_LOG) + "\n")
    cov = ["# Coverage (generated; the generator asserts these thresholds)\n", "| Function | Calls | OK | Refusals | Distinct variants | Pools |", "|---|---|---|---|---|---|"]
    bad = []
    for fn in PROCS + FUNCS:
        rows = [a for a in ACT if a["fn"] == fn]
        ok = [a for a in rows if not a["expect"].startswith("REFUSED")]
        rf = [a for a in rows if a["expect"].startswith("REFUSED")]
        var = {a["variant"] for a in rows}
        pools = {a["pool"] for a in rows if a["pool"] not in ("-", "")}
        cov.append(f"| {fn} | {len(rows)} | {len(ok)} | {len(rf)} | {len(var)} | {len(pools)} |")
        if fn in ("Contribute", "WithdrawUncommitted", "Propose", "Vote", "Tally", "Execute", "ClaimPayout", "OfferPosition", "CancelOffer",
                  "BuyPosition", "ReconcileHolding", "DepositHoldingAsset") and len(pools) < 6:
            bad.append((fn, "pools", len(pools)))
        if fn in PROCS:
            if len(rows) < 3 or len(var) < 2 or not ok or (not rf and fn not in ("RebidIpo",)):
                bad.append((fn, len(rows), len(var), len(ok), len(rf)))
        elif len(rows) < 5:
            bad.append((fn, len(rows)))
    cov.append(f"\nTotal actions: {len(ACT)} in {len({a['epoch'] for a in ACT})} epochs; users {len(ROLE)}; working pools 10 (+1 scratch).")
    open(f"{OUT}/coverage.md", "w").write("\n".join(cov) + "\n")
    chk = ["# Expected checkpoints at the end of each epoch (generated)\n"]
    for ep, r, ps, paid, hub, potc, burn, p_, tot, notes in CHECK:
        chk.append(f"## Epoch {ep}\nRevenue collected {r:,}. END_EPOCH split: per share {ps:,} (shareholders paid {paid:,}), QPAYHUB deposit {hub:,}, "
                   f"pot +{potc:,}, burned {burn:,}. `qpayBuyPot` after the epoch: {p_:,}.\n")
        chk.append("Sum of uncommitted per pool according to the plan's ledger (compare with `GetPool.uncommittedTotal`, +/- the unverified PARAMs):\n")
        chk.append("| " + " | ".join(tot) + " |\n|" + "---|" * len(tot) + "\n| " + " | ".join(f"{v:,}" for v in tot.values()) + " |\n")
    open(f"{OUT}/checkpoints.md", "w").write("\n".join(chk) + "\n")
    print(f"actions {len(ACT)}; epochs 7; IPO targets {len(IPO_LOG)}; coverage problems: {bad}")
    assert not bad, bad


if __name__ == "__main__":
    main()
