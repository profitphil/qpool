# QPOOLK6 testnet-candidate — TESTING_GUIDE.md live run report (interim)

Target: `github.com/profitphil/qpool`, branch `testnet-candidate` (`QPool_testnet.h`,
the K6-audited baseline plus the new 40/30/20/10 revenue split, `BuyAndBurnQpay`,
and the changed unattributed-money hook). Wired at devkit index 49 (`QPoolK6.h`),
alongside a QPAYHUB stand-in (index 50, `QPayhubStub.h`) and a real-IPO-window
target contract (index 51, `QPoolK6IpoTgt.h`).

**Status: interim.** This report covers every suite actually run live against a
real chain so far. Several suites (full D/E edge cases, I/J/K extensions, L/M/N,
the exploit regression list as its own pass, negative/attacker cases, and the
randomized campaign) are not yet covered and are listed at the end as the
concrete continuation plan — tracked in the task list so a follow-up session
can pick up directly.

## Headline finding

**Zero contract bugs found in the testnet-candidate delta or the K6 baseline.**
Every single failure encountered while building this test campaign traced back
to a bug in *my own test script* — never the contract. That is a real, checked
claim: each one below was root-caused to a specific line of test code, not
asserted from absence of evidence.

## What was run

### Infrastructure (one-time)
- Read the full K6 audit history (`CHANGELOG-K6.md`, `CHANGES-SINCE-K6.md`):
  263 GoogleTest cases across 6+ hardening passes (K1–K6, KI, KI2, KX), real
  exploits found and fixed upstream (e.g. a crafted share count draining
  655,370 QU from a 10 QU bid booking). Trusted this baseline rather than
  re-auditing it from scratch; focused live testing on the genuinely new,
  previously-untested delta plus enough of the baseline to prove the
  integration (this specific index, this specific QPAYHUB stand-in) works.
- Wired `QPoolK6.h` (index 49), `QPayhubStub.h` (index 50, a minimal stand-in
  booking plain QU transfers — the real QPAYHUB doesn't exist on this devkit),
  `QPoolK6IpoTgt.h` (index 51, a real-construction-epoch IPO target +
  `distributeDividends`-based payout for suite F3). Fixed a `QPOOL_`-prefix
  naming collision against the pre-existing old QPOOL (index 39) and an
  `sadd()` type-ambiguity bug. Full regression build clean; a scary-looking
  crash in a full-suite run was confirmed via A/B test to be a pre-existing,
  already-tracked, unrelated issue (a mining-score test, nothing to do with
  this work).
- Generated a 20-wallet roster per the guide's section 2 (`qpoolk6_full_wallets.json`).
- Decided not to write new GoogleTest coverage for the delta: no baseline
  K6 test file was included in the branch to extend, and "the testnet is the
  test" is the upstream author's own explicit decision for this exact delta.

### Suite A — pools and membership: **24/24 passed**
CreatePool exact/under/over fee (with correct excess refund), pool counts,
first-time Contribute (join fee + minimum, below-minimum refusal), repeat
Contribute (no second join fee, no duplicate roster row), private-pool
admission (refuse → admit → retry), switching public↔private with members
inside (members never locked out, newcomers correctly blocked), non-creator
calls to admin-only procedures (refused, refunded), CloseEmptyPool (empty
succeeds, non-empty refused), a sample of refusing calls refunding their
reward exactly once, WithdrawUncommitted (part/all/over-balance), and
invariant I1 (sum of members' uncommitted == pool total) after concurrent
same-tick activity.

*Self-caught bugs along the way (both fixed, both test-script-only):* an
inverted balance-delta assertion; a wallet funded with *exactly* the
pool-creation fee (no margin), which zeroed+pruned it so a later call from
that wallet never reached the contract at all — confirmed by reviving the
wallet and retrying successfully.

### Suite C — governance, all 6 proposal kinds: **passed**
Real `BUY_QSWAP` (own QSWAP pool seeded with real liquidity) and `BUY_QX`
propose→vote→tally→execute cycles, abstainers never charged, invalid-input
refusals (asset name 0, amount above uncommitted, non-member proposer) write
nothing, double-voting and non-member voting don't count, `ALLOCATE`
confirmed as the documented dead path, vote-lock withdraw blocking (B2: locked
while vote is open; B3: still locked between Tally and Execute), and money
contributed after a proposal opens correctly carries zero vote weight (B4).

*Self-caught bugs (all test-script, all fixed):* proposal IDs are **not**
sequential by creation order — `Propose` scans for a free/expired slot, so I
built a `find_proposal_id()` lookup instead of guessing indices; a `Propose`
sent from a wallet that wasn't even a member of the target pool (correctly
refused — not a bug, my sender was wrong); `QSWAP::CreatePool` needs its real
fee attached (I sent 0, read the fee field under the wrong label from a stale
copy-paste); one vote with insufficient quorum correctly got REJECTED (not a
bug — my test assumed it would pass without enough voters); one
genuinely-PASSED proposal correctly EXPIRED when an extra interleaved test
step ate too much of the 96-tick execute-window margin — this is actually a
live confirmation of the documented "execute one tick late → EXPIRED" rule
(guide case C6), not a failure.

### Suite F — dividends: **passed**
A real token issuer paying a plain-transfer dividend: 1% management fee
correctly skimmed to revenue, net amount correctly credited to the holding's
payout accumulator, and the sum of members' owed-increase provably doesn't
exceed the net dividend (invariant I12). A payment from an unmatched sender
correctly routes to `pendingRevenue`, not the dividend ledger (F4). A real
`ClaimPayout` pays exactly what was owed; a second claim with nothing owed,
and a non-holder's claim, are both correct no-ops (F5).

### Suite H — seat market: **passed**
`OfferPosition` records correctly (H1). A stale `expectedCostUnits` (the
offer was replaced after the buyer read it) is correctly refused with
`OFFER_CHANGED`, full refund, nothing written (H3). A genuine `BuyPosition`
by a non-member both transfers the seat and joins them to the roster, paying
ask price + join fee in one call, with the seller paid (H2/H4 combined).

*Self-caught bugs (test-script, fixed):* first attempt used a buyer who
never actually held uncommitted funds (an earlier Contribute had silently
failed because the attached amount exceeded that wallet's real balance) and
then, even after switching buyers, forgot the join-fee surcharge a
non-member buyer must attach — both fixed by using a well-funded genuine
non-member and the correct ask+fee amount.

### Suite G — the full real IPO cycle (flagged "most important" by the guide): **core path confirmed**
End-to-end against a real IPO auction on `K6TGT` (a dedicated real-epoch
target contract): `Propose(BUY_IPO)` → vote → Tally → **Execute placing the
real opening bid** (budget 676,000 @ opening price 1,000 → all 676 seats,
confirmed via `GetIpoBids`) → real epoch transition (225→226) → **END_EPOCH's
snapshot correctly reads the book before the core's own `finishIPOs()` closes
it** (`GetIpoTarget` shows `finalPrice: 1000`, a real clearing price from a
real auction) → **`SettleIpo` reconciles real shares won (676) with cost
basis exactly `shares × finalPrice`** → a second `SettleIpo` call on the same
holding is correctly refused/idempotent. Also confirmed live: `RebidIpo` at
or below the book's minimum price is refused (`PRICE_TOO_LOW`, G7); a
stranger with no seat in the holding cannot `RebidIpo` it at all (G8).

**Not yet re-verified: the direct-outsider-bid / outbid / rebid-with-returned-money
sub-case (G3–G6, G9–G13).** Root cause found and fixed: a plain wallet IPO
bid is `struct ContractIPOBid {long long price; unsigned short quantity;}`,
dispatched on `inputSize == sizeof(ContractIPOBid)` exactly — and that
struct's *real* size is 16 bytes (padded to `long long`'s 8-byte alignment),
not the raw 8+2=10 bytes of its members. My first attempt sent a 10-byte
payload, which silently failed the size check and was treated as an ordinary
no-op transfer — no error, no effect, which is why the book never changed
after the "outbid." The payload encoder (`pay_contract_ipo_bid` in
`qpoolk6_lib.py`) is now fixed to pad to 16 bytes, but re-confirming the
outbid/refund/rebid sequence needs another fresh genesis and another ~40
minutes of real epoch transitions to reach a live IPO window, which hasn't
been run yet.

## Known, deliberate scope reductions (not silently skipped)

- **Suite L2** (filling the 1,024-pool / 262,144-proposal global tables to
  their real capacity) — the guide's own text calls this "impractical at full
  size" and suggests a shrunk-constant copy; that copy was not built.
- **The seeded randomized campaign** (10 seeds × 400 steps + a 5-epoch soak)
  — not run at that scale; each step costs real wall-clock ticks on a live
  chain, making the full spec impractical within this session.
- **Section 7's exploit regression list as a dedicated pass** — most of its
  16 items map directly to suite cases already run live above (e.g. C002→the
  membership/charge-once checks in suite A/C, C017→the join-fee checks, C033→
  H3's `OFFER_CHANGED`), but it hasn't been walked as its own explicit
  checklist yet.
- **Section 10's negative/attacker cases** and **suites D/E's remaining edge
  cases, I/J/K's further revenue-amount variations, and suites M/N** — not
  yet run.

## Continuation plan

The task list (tasks #10, #14–#19) tracks exactly what's left: suites D/E
remainder, I/J/K extensions, L/M/N, the exploit list as its own pass, negative/
attacker cases, the randomized campaign (at a disclosed, reduced scale), and a
final consolidated report. All scripts, the shared payload/parser library
(`qpoolk6_lib.py`), and the rebuild script (`rebuild_genesis_qpoolk6_comprehensive.sh`)
are in `tools-bin/` and are reusable as-is for a follow-up session.
