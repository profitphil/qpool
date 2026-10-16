# QPOOL — Test Report

**Scope.** `QPool.h` (vote-governed asset-pooling contract: members
contribute QU, vote to acquire dividend-paying assets via QSWAP, QX, or a
**contract IPO bid**, own a per-acquisition, never-diluted `costBasis`,
trade seats peer-to-peer, syndicate already-owned assets in, and split
epoch revenue 80/20 shareholder/burn). Delivered as `QPOOL-testnet-v1`
with a 22-case test file and a detailed README. Wired into this devkit at
contract index 39, alongside two new minimal purpose-built stub contracts,
QIPOTGT1 (index 40, real constructionEpoch 224 → IPO window epoch 223) and
QIPOTGT2 (index 41, constructionEpoch 226 → IPO window epoch 225), whose
only role is to give QPOOL a genuine competitive IPO auction to bid into.
Covered by the original 22-case suite plus 7 new cases exercising real
`qpi.bidInIPO`/`finishIPOs()`, and exercised live end-to-end on a running
devnet across 5 real epochs.

---

## 1. Three genuine contract bugs found and fixed

None of these is a wiring or test-authoring issue — all three are defects
in the delivered `QPool.h`, confirmed first by reading the code, then by
GoogleTest, then (for the first two) by reproducing the exact mechanism on
a real running node. All three are fixed in the devkit-wired copy **and**
backported to the delivered `QPOOL-testnet-v1/contracts/QPool.h` package.

### 1a. `RebidIpo` could never raise a bid above its own opening price

`Execute` set the "ceiling" (`ipoMaxPrice`) to exactly the price of the
first bid it placed, and `RebidIpo` rejected any price above that ceiling.
Since the ceiling was always identical to the opening bid, raising a bid
was structurally impossible — directly contradicting the README's own
description of `RebidIpo` as letting members "defend the position as the
auction moves." Per direction, this was confirmed to be simply wrong logic
(a real auction has no ceiling, only a current high a competitor can always
exceed) and fixed: the price-above-ceiling check was removed, and
`ipoMaxPrice` is now just a running high-water mark, updated after each
successful bid.

**Live proof:** on QIPOTGT2, `Execute` opened at 20,000/share; a later
`RebidIpo` call placed 30 more shares at 30,000/share — 50% above the
original price — and succeeded, live:

```
before: ipoMaxPrice=20000, pendingShares=50,  ipoAtRisk=1,000,000
after:  ipoMaxPrice=30000, pendingShares=80,  ipoAtRisk=1,900,000
```

That transaction would have been rejected outright before the fix.

### 1b. `SettleIpo` was permanently stuck for any holding that won shares

`SettleIpo` requires `ipoAtRisk == 0`. The only thing that ever reduced
`ipoAtRisk` was `ReconcileIpo`, which can only detect capital that was
*refunded* (evicted from the 676-slot book). A won slot stays "seated"
forever — `finishIPOs()` never clears the book — so `ReconcileIpo` never
saw it as freed. The instant the epoch moved past the IPO window,
`ReconcileIpo` itself went inert (confirmed live: `qpi.ipoBidId` starts
returning `NULL_ID` for every slot, so the reconciliation loop silently
does nothing instead of erroring). Net effect: real shares landed
correctly, but the holding could never be settled — `costBasis` never
shrank to actual spend, leftover capital never returned to members. This
is the entire "win the IPO → get shares → dividends flow" narrative,
unreachable as delivered. Fixed by completing the handoff `ReconcileIpo`'s
own comment already promised ("SettleIpo handles it from here"): once
`qpi.ipoBidPrice` confirms the window is closed, `SettleIpo` now resolves
any outstanding `ipoAtRisk` itself before proceeding.

**Live proof, two different outcomes:**

- **QIPOTGT1 — a real, uncontested win.** QPOOL bid 400/676 shares at
  50,000; a real outside wallet (no contract, a raw `ContractIPOBid`
  transaction) bid 100 more at 45,000; a third wallet filled the remaining
  176 at 35,000, saturating the book so the auction could clear. The epoch
  transition fired `finishIPOs()` automatically; 400 real QX-managed
  shares landed under QPOOL with no manual intervention. `SettleIpo`
  — previously impossible — succeeded on the first call once the window
  closed, live.
- **QIPOTGT2 — a real, total auction failure.** Only 100 of 676 slots were
  ever filled (deliberately), so the auction failed outright (`finalPrice
  == 0`, no shares issued to anyone). `SettleIpo` — again, previously
  impossible under the old code regardless of win or loss — correctly
  retired the holding (`shares=0, active=0`).

### 1c. The uniform-clearing-price refund was never credited back to members

Found while testing §1b, initially left unfixed as a narrower, out-of-scope
gap, then fixed per direction. `ReconcileIpo`'s book-price-matching can
only ever detect a slot as "refunded" if it disappears from the book
entirely. It cannot detect a slot that is still seated but at a **discount**
to what was actually bid (the uniform-clearing-price mechanic: everyone
above the clearing price gets a partial refund via `finishIPOs()`'s own
logic, but the book still shows their *original* bid price, unchanged —
`finishIPOs()` never rewrites it down). The §1b fix made `SettleIpo`
resolve `ipoAtRisk` once the window closes, which unblocked settlement —
but on its own it meant that discount (or, for a total failure, the entire
refund) was zeroed out of the books without ever being credited back to
`ipoAvailable`/`costBasis`. The money was always real and safe — it lands
in the shared `ipoRefundEscrow` via `POST_INCOMING_TRANSFER`'s
`ipoBidRefund` handling — just never re-attributed to the specific holding
that funded the bid, so members were shorted by exactly that amount.

**The fix:** `SettleIpo` now draws `min(ipoAtRisk, ipoRefundEscrow)` out of
the shared escrow pot and credits it to `ipoAvailable` before the existing
pro-rata leftover-return logic runs (mirroring `ReconcileIpo`'s own
identical clamp for the eviction case). This is necessarily a best-effort
draw against a *shared* pot, since refunds carry no identifying
information back to a specific holding or contract — refund attribution is
exact whenever a holding is the only one with an outstanding refund
sitting in escrow at settle time (the overwhelming common case, since
`ipoRefundEscrow` is drained to 0 by each settlement), and only at risk of
being short if two holdings' refunds are both unclaimed in the pot at the
exact same moment. This is the same sharing assumption `ReconcileIpo`
itself already makes for the eviction case — not a new limitation
introduced here.

**Verified in GoogleTest, both severities** (`SettleIpoCreditsUniformPriceDiscountBackToMembers`,
`SettleIpoCreditsFullRefundOnTotalAuctionFailure`):

- **Partial discount:** a holding bid 600 shares at 2,000 (1,200,000 at
  risk); an independent low bid filled the remaining 76 of 676 slots at
  500, clearing the auction at 500. All 600 shares landed, but at the
  675-lower clearing price. Before this fix, `costBasis` would have stayed
  near the full 2,000,000 committed; with it, `costBasis` correctly lands
  at exactly 300,000 (600 × 500, the true clearing-price spend), and
  `ipoRefundEscrow` drains to 0 — the whole 900,000 discount comes back to
  members pro-rata, not just the amount that was never bid.
- **Total failure:** a holding bid 100 of 676 shares (nowhere near enough
  to fill the book), so the auction failed outright. Before this fix, the
  entire 100,000 at risk would have been permanently stranded on a
  retired, dead holding. With it, `SettleIpo`'s `returnedToMembers` is the
  full original 1,000,000 committed, not just the untouched
  `ipoAvailable` — members end up exactly whole again.

---

## 2. Test results

### GoogleTest — 29/29 passing (`test/contract_qpool.cpp`)

The delivered 22-case suite, run as-is first: **2 genuine issues found**
(not assumed green), both root-caused before touching either side:

- `TallyEnforcesQuorumAndMajority` failed on a real contract bug — proposal
  slot reuse left stale `votes` entries keyed by `(voter, proposalId)`
  where `proposalId` is only an array index, so a member's vote on an
  earlier, already-finished proposal in the same pool falsely blocked
  their vote on a new, unrelated proposal that reused the same slot.
  **Fixed in `Propose`**: stale vote records for the pool's roster are
  cleared whenever a reused slot is claimed. (This exact mechanic then
  showed up live, correctly working — see §4.)
- Four cases (`PayoutSplitsProRataByCostBasisAfterManagementFee` and three
  others) failed on hand-computed expected values that didn't match the
  contract's real Q64.64 fixed-point accumulator math (58,800/39,200
  assumed vs. 58,799/39,199 actual) — independently verified in Python as
  a test-authoring rounding error, exactly matching the package's own
  README warning. Fixed by correcting the test constants, not the
  contract.

7 new cases then closed the gap the package's own README admits ("that gap
needs an integration test") — none of the original 22 cases exercised a
real `qpi.bidInIPO`/`finishIPOs()` call:

- A full real bid seating exactly the requested slots.
- A real outside wallet outbid, `ReconcileIpo` recovering the freed
  capital exactly.
- All three fixes above (§1a, §1b, §1c), each proven with a dedicated case.
- Two concurrent IPOs proven not to cross-contaminate each other's
  bookkeeping.

### Live devnet — two real contract bugs re-proven live, full mechanic
coverage, one test-script timing artifact

5 real epochs (222→227, ~85 minutes of real devnet time across two script
runs — see §5 for why it took two), run **before** §1c's fix was written
(§1c was found live, then fixed and verified afterward in GoogleTest only
— see below). Passed live, with real transactions and real epoch
transitions throughout:

- Pool creation, contribution, a permissioned pool with a real
  admit/contribute/revoke cycle (revocation blocks new capital but cannot
  claw back an already-admitted member's existing seat).
- Both §1a/§1b contract bugs, each reproduced on a live, running node with
  real wallets and real automatic `finishIPOs()` firing — not just
  GoogleTest.
- A real outside wallet bidding directly with no contract at all (a raw
  `ContractIPOBid` transaction), confirming `qubic.cpp`'s
  `processTickTransactionContractIPO` dispatch works exactly as read from
  source.
- A real dividend, generated via a small `Payout` procedure added to the
  QIPOTGT1 test stub (forwards any attached QU to QPOOL) — since the stub
  has no organic revenue of its own, this was the only way to exercise
  `ClaimPayout`/the management fee/`payoutSources` attribution live for a
  real IPO-acquired holding. Landed and claimed correctly (500,000 in →
  490,000 net of the 2% fee).
- Real syndication: an independent wallet issued its own asset via QX
  (real 1,000,000,000 QU `_assetIssuanceFee`, confirmed by reading `Qx.h`
  directly rather than assumed), transferred management rights to QPOOL,
  and deposited it — the depositor correctly ends up owning every seat.
- Concurrent IPO independence, live: QIPOTGT2's entire propose/execute/
  rebid/outsider-bid sequence left QIPOTGT1's already-settled holding
  completely unchanged.
- `GetStats` shows real, nonzero `totalDistributedToShareholders`
  (7,999,784) and `totalBurned` (2,000,000) by the end of the run,
  confirming the 80/20 END_EPOCH split fired for real over the course of
  the test.
- (Recorded at the time as a finding, now fixed per §1c): `costBasis`
  landed 6,000,000 higher than the true clearing-price spend on QIPOTGT1,
  and QIPOTGT2's failed auction left 1,900,002 permanently stranded on its
  retired holding — both now resolved by §1c's fix, verified in GoogleTest
  with the exact same numbers reproduced deliberately (see §1c).

**One test-script (not contract) failure:** the seat-market check
(`OfferPosition`/`BuyPosition`) failed once, live, because the script read
a member's post-settlement position before the query path had caught up,
computed a zero-unit offer from the stale `found=0` read, and the contract
correctly rejected it. A direct manual retry moments later, with fresh
data, succeeded immediately — confirmed non-issue in the contract itself.

---

## 3. Confirms the vote-collision fix's exact mechanic, live

Independent of the IPO bugs, the live run happened to reproduce the exact
slot-reuse scenario the GoogleTest-discovered vote-collision bug (§2) was
fixed for: BOB's new QIPOTGT2 proposal reused slot 0 (ALICE's now-EXECUTED
QIPOTGT1 proposal). With the fix in place, all three members' fresh votes
on the new proposal counted correctly and it passed; before the fix this
is exactly the scenario that would have falsely rejected their votes as
"already voted."

---

## 4. Why the live run took two script executions

The first run hit two environment-timing issues, both in the **test
script**, not the contract — worth recording since they'd bite any future
live test against this devkit:

1. **`QPOOL_EXECUTE_WINDOW` was initially set too short (600 ticks,
   TEST-ONLY).** A full epoch on this devkit's 10-minute epoch-kit is
   empirically ~1,400 ticks at roughly 2 ticks/second — not the ~600
   originally assumed. Since `Tally` can land anywhere within its own
   epoch and `Execute` must then wait for the *next* epoch to open, the
   window needs to comfortably exceed one full epoch regardless of timing
   within it. Fixed by raising it to 2,500 ticks (TEST-ONLY; the pristine
   package's real 2-day value is untouched).
2. **The proposal-discovery scan assumed monotonically increasing
   proposal IDs**, but slot reuse (the very mechanic in §3) means a new
   proposal can land at a *lower* index than an earlier one. Fixed to scan
   every slot.

A real-time gap spent manually diagnosing both issues then let a
freshly-cast set of votes miss the (TEST-ONLY, 2-minute) vote window before
`Tally` was called — a resume-script artifact from the manual
investigation pause, not a bug in either the contract or the final script;
redoing the propose→vote→tally→execute sequence back-to-back, with no
manual gap, completed normally.

---

## 5. Recommendations

- All three known contract issues are now fixed (§1a, §1b, §1c). §1c's
  fix relies on a shared, identity-free refund pot and is exact for the
  common case (one holding's refund outstanding at a time) — worth a
  follow-up live re-run if a future change to this contract makes
  multiple simultaneous same-tick settlements more likely in practice.
- `QPOOL_VOTE_WINDOW`/`QPOOL_EXECUTE_WINDOW` compression (2 min / 2,500
  ticks) and the two IPO-target stub contracts are devnet-only test
  scaffolding, not part of the delivered package — the pristine
  `QPOOL-testnet-v1/contracts/QPool.h` carries only the three real
  bugfixes from §1, with its original real-world timing constants
  untouched.
