#!/usr/bin/env python3
"""Reference model of a Qubic contract IPO book plus QPOOL's IPO accounting, for generating EXPECTED values.

It is an aid for the testers, not an authority: the real node's book is the truth, and a mismatch means "look", not
automatically "contract bug". It encodes:

CORE (verified in qubic/core src/contract_core/ipo.h):
  * 676 slots sorted by price descending; an equal price ranks BEHIND older bids.
  * bid(price, qty) deducts price*qty up front. Per unit: if price <= lowest price (0 while the book has empty slots)
    the unit is NOT registered and its price is refunded at once; otherwise the LOWEST slot (any owner, the bidder
    included) is replaced and its owner is refunded that slot's price at once.
  * at the end finalPrice = price of slot 675 (0 when the book is not full: the IPO fails, everything is refunded);
    every winner pays finalPrice per seat, so each slot is refunded (price - finalPrice).

QPOOL (build of this repository, KI2 rules):
  * a BUY_IPO holding opens ALL-IN at its opening price: seats = min(676, floor(budget / opening)).
  * RebidIpo(price, qty): refused with IPO_PRICE_TAKEN when the price belongs to ANOTHER holding; with PRICE_TOO_LOW when
    the book is full and price <= its lowest price; the call first RECONCILES (lost seats move price each from at-risk to
    free funds); the default qty is min(676, floor(free / price)); INSUFFICIENT_BALANCE when qty is 0 or price*qty > free;
    CAPACITY when a NEW price would be the 17th live price. A price the holding already owns is reused.
  * the refunds of seats that a bid pushes out of the holding's OWN book are freed only at the NEXT reconcile.
  * settlement uses the real final book: shares = seats won, cost basis = shares * finalPrice, the rest is returned.

Usage:  python3 ipo_reference_model.py w1|w2|w3|w4|w5|w6      print a scripted war with expected values
        python3 ipo_reference_model.py random SEED [STEPS]    print a random war (actions + expected results) as text
"""
import random
import sys

CAP = 676
MAX_LIVE_PRICES = 16


class Book:
    def __init__(self):
        self.slots = []  # (price, owner, seq), best first
        self.seq = 0

    def lowest(self):
        return self.slots[-1][0] if len(self.slots) == CAP else 0

    def count(self, owner, price=None):
        return sum(1 for p, o, _ in self.slots if o == owner and (price is None or p == price))

    def bid(self, owner, price, qty):
        """Returns {owner: refund} paid in the same transaction."""
        refunds = {}
        for _ in range(qty):
            if price <= self.lowest():
                refunds[owner] = refunds.get(owner, 0) + price
            else:
                if len(self.slots) == CAP:
                    p, o, _ = self.slots.pop()
                    refunds[o] = refunds.get(o, 0) + p
                self.seq += 1
                i = 0
                while i < len(self.slots) and self.slots[i][0] >= price:
                    i += 1
                self.slots.insert(i, (price, owner, self.seq))
        return refunds

    def final_price(self):
        return self.slots[-1][0] if len(self.slots) == CAP else 0

    def summary(self):
        by = {}
        for p, o, _ in self.slots:
            by.setdefault(o, {}).setdefault(p, 0)
            by[o][p] += 1
        return by


class Pool:
    def __init__(self, name, budget, opening):
        self.name, self.budget, self.opening = name, budget, opening
        self.free, self.atrisk = budget, 0
        self.listed = {}  # price -> seats as of the last reconcile (the contract's books)

    def reconcile(self, book):
        for price in list(self.listed):
            actual = book.count(self.name, price)
            if actual < self.listed[price]:
                lost = self.listed[price] - actual
                self.free += lost * price
                self.atrisk -= lost * price
                self.listed[price] = actual
                if actual == 0:
                    del self.listed[price]

    def open(self, book):
        n = min(CAP, self.budget // self.opening)
        cost = n * self.opening
        self.free -= cost
        self.atrisk += cost
        self.listed[self.opening] = n
        return book.bid(self.name, self.opening, n), n

    def rebid(self, book, pools, price, qty=None):
        """Returns (code, refunds, n). code is 'OK' or the refusal code. A refusal changes nothing."""
        for other in pools:
            if other is not self and price in other.listed:
                return "IPO_PRICE_TAKEN", {}, 0
        if len(book.slots) == CAP and price <= book.lowest():
            return "PRICE_TOO_LOW", {}, 0
        # the call reconciles first, then decides; a refusal after the reconcile still keeps the freed money
        self.reconcile(book)
        if price not in self.listed and len(self.listed) >= MAX_LIVE_PRICES:
            return "CAPACITY", {}, 0
        n = qty if qty else min(CAP, self.free // price)
        if n <= 0 or price * n > self.free:
            return "INSUFFICIENT_BALANCE", {}, 0
        refunds = book.bid(self.name, price, n)
        self.free -= price * n
        self.atrisk += price * n
        self.listed[price] = self.listed.get(price, 0) + n
        return "OK", refunds, n

    def state(self, book):
        actual = {p: book.count(self.name, p) for p in sorted(set(list(self.listed) + [p for p, o, _ in book.slots if o == self.name]))}
        return {"free": self.free, "atRisk": self.atrisk, "listed(books)": dict(sorted(self.listed.items())),
                "actual(core book)": {p: c for p, c in actual.items() if c}}


def settle(book, pools, outsiders):
    """Final outcome. Pools settle from the real book, so the result does not depend on the settle order."""
    F = book.final_price()
    out = {}
    for pl in pools:
        seats = book.count(pl.name)
        shares = seats if F > 0 else 0
        spent = shares * F
        out[pl.name] = {"seats": seats, "shares": shares, "costBasis": spent, "returned": pl.budget - spent}
    for name, paid in outsiders.items():
        seats = book.count(name)
        shares = seats if F > 0 else 0
        out[name] = {"seats": seats, "shares": shares, "spent": shares * F, "returned": paid - shares * F}
    return F, out


class Run:
    def __init__(self, pools, outsiders=()):
        self.book = Book()
        self.pools = pools
        self.out_paid = {o: 0 for o in outsiders}
        self.log = []
        self.step = 0

    def say(self, text):
        self.step += 1
        self.log.append(f"{self.step:>3}. {text}")

    def pool(self, name):
        return [p for p in self.pools if p.name == name][0]

    def try_open(self, name, opening):
        """Opening at a price another holding owns is refused (Propose and Execute check the registry)."""
        p = self.pool(name)
        for other in self.pools:
            if other is not p and opening in other.listed:
                self.say(f"{name} Execute at opening {opening:,} -> REFUSED IPO_PRICE_TAKEN (held by {other.name}); nothing written")
                return False
        p.opening = opening
        self.open_pool(name)
        return True

    def open_pool(self, name):
        p = self.pool(name)
        ref, n = p.open(self.book)
        self.say(f"{name} Execute: budget {p.budget:,} at opening {p.opening:,} -> {n} seats, free {p.free:,}"
                 + (f"  (unregistered units refunded {ref})" if ref else ""))

    def outsider(self, name, price, qty):
        self.out_paid[name] = self.out_paid.get(name, 0) + price * qty
        ref = self.book.bid(name, price, qty)
        txt = ", ".join(f"{o} +{a:,}" for o, a in sorted(ref.items()) if o != name) or "no seat pushed out"
        self.say(f"{name} bids {qty} @ {price:,} -> refunds in the same transaction: {txt}"
                 + (f"; own unregistered units back {ref[name]:,}" if name in ref else ""))
        for o, a in ref.items():
            if o in self.out_paid:
                self.out_paid[o] -= a

    def rebid(self, name, price, qty=None):
        p = self.pool(name)
        code, ref, n = p.rebid(self.book, self.pools, price, qty)
        if code == "OK":
            others = ", ".join(f"{o} +{a:,}" for o, a in sorted(ref.items()) if o != name) or "none to others"
            own = ref.get(name, 0)
            self.say(f"{name} RebidIpo @ {price:,} -> OK, {n} seats (cost {price*n:,}); pushed out: {others}; "
                     f"own seats pushed out refund {own:,} (freed at the NEXT reconcile); free now {p.free:,}")
            for o, a in ref.items():
                if o in self.out_paid:
                    self.out_paid[o] -= a
        else:
            self.say(f"{name} RebidIpo @ {price:,}{' qty '+str(qty) if qty else ''} -> REFUSED {code}; free {p.free:,}")
        return code

    def show(self):
        self.log.append("     core book: " + "; ".join(
            f"{o}: " + ",".join(f"{c}@{p:,}" for p, c in sorted(d.items(), reverse=True))
            for o, d in sorted(self.book.summary().items())) + f"   [{len(self.book.slots)} slots, lowest {self.book.lowest():,}]")
        for p in self.pools:
            self.log.append(f"     {p.name} books: {p.state(self.book)}")

    def finish(self):
        F, out = settle(self.book, self.pools, self.out_paid)
        self.log.append(f"\nEND OF EPOCH: final price {F:,}" + ("  (book not full: the IPO FAILS, everything is refunded)" if F == 0 else ""))
        for k, v in out.items():
            self.log.append(f"  {k}: {v}")
        total_pools = sum(p.budget for p in self.pools)
        got = sum(v["costBasis"] + v["returned"] for k, v in out.items() if k in [p.name for p in self.pools])
        assert got == total_pools, ("conservation", got, total_pools)
        self.log.append(f"  check: pools' costBasis + returned = {got:,} = total budgets {total_pools:,}")
        return "\n".join(self.log)


def four_pools():
    return [Pool("A", 200_000, 2_000), Pool("B", 150_000, 1_500), Pool("C", 120_000, 1_200), Pool("D", 90_000, 900)]


def w1():
    """Four pools and three outsiders: escalating war, rebids, collisions, a refusal, then settlement."""
    r = Run(four_pools(), ["OUT1", "OUT2", "OUT3"])
    for n in "ABCD":
        r.open_pool(n)
    r.outsider("OUT1", 1_800, 276); r.show()
    r.outsider("OUT2", 1_000, 30)
    r.rebid("D", 1_001); r.show()
    r.rebid("D", 1_001)
    r.rebid("C", 1_001)          # price owned by D
    r.rebid("B", 1_550)          # B has no free funds
    r.outsider("OUT3", 1_300, 60); r.show()
    r.rebid("D", 1_001)          # now at or below the minimum?
    r.rebid("C", 1_301); r.rebid("C", 1_301)
    r.outsider("OUT2", 1_600, 40); r.show()
    r.rebid("D", 1_700); r.rebid("C", 1_650); r.rebid("B", 1_620)
    r.outsider("OUT1", 1_900, 50); r.show()
    r.rebid("B", 1_901); r.rebid("C", 1_651); r.rebid("D", 1_701)
    r.show()
    return r.finish()


def w2():
    """Same book as W1 but settled to show order independence: the result is a function of the final book only."""
    return "W2: run W1's chain state, then settle the holdings in several different orders; every order must give the\n" \
           "per-member results printed at the end of W1 (shares = seats, costBasis = shares x final price, rest returned).\n" \
           "Use mirrored targets (same war, opposite settle orders) when the chain allows only one order per run."


def w3():
    """Three pools, book never fills: the IPO fails, everything returns."""
    r = Run([Pool("A", 300_000, 1_500), Pool("B", 200_000, 1_000), Pool("C", 100_000, 800)], ["OUT1"])
    for n in "ABC":
        r.open_pool(n)
    r.outsider("OUT1", 600, 100)
    r.rebid("C", 801); r.rebid("B", 1_001)
    r.show()
    return r.finish()


def w4():
    """A pool is wiped out: outsiders push it out completely and it cannot afford to come back."""
    r = Run([Pool("A", 200_000, 2_000), Pool("B", 150_000, 1_500), Pool("C", 120_000, 1_200), Pool("D", 90_000, 900)],
            ["OUT1", "OUT2"])
    for n in "ABCD":
        r.open_pool(n)
    r.outsider("OUT1", 1_800, 276)
    r.outsider("OUT2", 1_000, 100)          # replaces all of D's 100 seats
    r.rebid("D", 1_000)                      # at the minimum: refused
    r.rebid("D", 1_001); r.show()            # D comes back for what its 90,000 buys at 1,001
    r.outsider("OUT2", 1_100, 100)          # and is wiped out again
    r.rebid("D", 1_101); r.rebid("D", 1_101)
    r.show()
    return r.finish()


def w5():
    """Price collisions: one outsider bid hurts three pools at once and they race for the same new price."""
    r = Run([Pool("A", 100_000, 1_000), Pool("B", 100_000, 1_000), Pool("C", 100_000, 900), Pool("D", 100_000, 800)],
            ["OUT1", "OUT2"])
    r.try_open("A", 1_000)
    r.try_open("B", 1_000)       # the same price as A: refused
    r.try_open("B", 999)         # one lower
    r.try_open("C", 900); r.try_open("D", 800)
    r.outsider("OUT1", 1_050, 240)       # book full
    r.show()
    r.outsider("OUT2", 1_100, 250)       # pushes out D (all), C (all) and part of B
    r.show()
    r.log.append("     (D, C and B now all have free funds and want the same price 1,101. On a chain the order of the three"
                 " transactions decides; send them in separate ticks in this order, then repeat with the order reversed.)")
    r.rebid("D", 1_101)
    r.rebid("C", 1_101)                  # taken by D
    r.rebid("C", 1_102)
    r.rebid("B", 1_101)                  # taken by D
    r.rebid("B", 1_103)
    r.show()
    r.rebid("D", 1_101); r.rebid("C", 1_102); r.rebid("B", 1_103)   # second round with the freed money
    r.show()
    return r.finish()


def w6():
    """Sniping: an outsider bids a high price on many slots at the very end; pools rebid; the final price jumps."""
    r = Run(four_pools(), ["OUT1", "OUT2"])
    for n in "ABCD":
        r.open_pool(n)
    r.outsider("OUT1", 1_800, 276)
    r.show()
    r.outsider("OUT2", 3_000, 250)          # last-minute snipe: pushes out the lowest 250 slots
    r.show()
    for name, price in (("D", 3_001), ("C", 3_002), ("B", 3_003), ("A", 3_004)):
        r.rebid(name, price)
    r.rebid("D", 3_001); r.rebid("C", 3_002); r.rebid("B", 3_003)
    r.show()
    return r.finish()


def random_war(seed, steps=60):
    rng = random.Random(seed)
    pools = [Pool(n, rng.choice([80_000, 120_000, 200_000, 300_000]), rng.choice([900, 1_200, 1_500, 2_000]) + i)
             for i, n in enumerate("ABCD")]
    r = Run(pools, ["OUT1", "OUT2", "OUT3"])
    for p in pools:
        r.open_pool(p.name)
    r.outsider("OUT1", 1_000, max(1, CAP - sum(p.budget // p.opening for p in pools)))
    for _ in range(steps):
        kind = rng.choice(["rebid", "rebid", "rebid", "out", "out", "badrebid"])
        if kind == "out":
            r.outsider(rng.choice(["OUT1", "OUT2", "OUT3"]), r.book.lowest() + rng.randint(1, 400), rng.randint(1, 60))
        elif kind == "rebid":
            p = rng.choice(pools)
            r.rebid(p.name, max(1, r.book.lowest() + rng.randint(1, 300)))
        else:
            p = rng.choice(pools)
            r.rebid(p.name, rng.choice([1, r.book.lowest(), r.book.lowest() + 1]) or 1, rng.choice([None, 3]))
    r.show()
    return r.finish()


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "w1"
    if which == "random":
        print(random_war(int(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else 60))
    else:
        print({"w1": w1, "w2": w2, "w3": w3, "w4": w4, "w5": w5, "w6": w6}[which]())
