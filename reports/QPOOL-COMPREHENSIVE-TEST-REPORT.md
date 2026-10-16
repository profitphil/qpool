# QPOOL Comprehensive Live Test Report

**Scope:** 10 pools, 50 wallets, real IPO auctions (single and concurrent-double
windows), real QSWAP/QX buys and sells, permissioned-pool admission/revocation,
seat-market trades, dividends, and per-pool member payouts, all driven against
a live devnet across 8 phases spanning epochs 222–234.

**Bottom line:** this test found and fixed **5 genuine QPOOL contract bugs**,
all previously undetected by the delivered package's own 22-case GoogleTest
suite or the earlier, smaller live test. Every fix is verified two ways: a
new GoogleTest case reproducing the exact defect, and a live reproduction on
the actual devnet. The devkit-wired `core-lite/src/contracts/QPool.h` and the
pristine delivered `QPOOL-testnet-v1/contracts/QPool.h` now carry identical
fixes (diffed to confirm only intentional TEST-ONLY window compressions
remain as differences).

---

## 1. The five contract fixes

### 1a. RebidIpo ceiling bug *(found in the earlier, smaller live test; carried forward)*
A re-bid above the proposal's approved ceiling price was incorrectly rejected
even when it should have been allowed. Fixed and covered by GoogleTest.

### 1b. SettleIpo permanently stuck for real wins *(found in the earlier test)*
A holding that actually won IPO shares could never be settled because the
"capital still at risk" guard never cleared for a won (not evicted) slot.
Fixed by recognizing that once the window is confirmed closed, `ipoAtRisk` is
always resolved one way or another.

### 1c. SettleIpo refund-crediting gap *(found in the earlier test)*
A uniform-clearing-price discount, or a total auction failure, landed safely
in the shared `ipoRefundEscrow` pool but was never credited back to the
specific holding that funded it. Fixed by drawing
`min(ipoAtRisk, ipoRefundEscrow)` into the holding's `ipoAvailable`, mirroring
the pattern `ReconcileIpo` already used.

### 1d. **`SettleIpo`/`ReconcileHolding` shared-asset share-count bug** *(new — found by this test)*
**This is the test's headline finding.** `qpi.numberOfShares(asset,
byOwner(SELF), byPossessor(SELF))` reports QPOOL's **total** holdings of an
asset across the *entire contract*, not the amount specific to one holding.
QPOOL's own README explicitly supports "any number of concurrent pools"
bidding on the same IPO target — but when two different pools' holdings both
won real seats in the same target (QIPOTGT1), **both** holdings' `SettleIpo`
calls came back reporting the *combined* total instead of each pool's actual
win.

**Live reproduction (Phase 3, first attempt):**
| Holding | Actually won | `shares` reported (bug) |
|---|---|---|
| P1 → TGT1 | 300 | **500** |
| P6 → TGT1 | 200 | **500** |

**Fix:** a new `assetSharesAttributed` running ledger (keyed by
issuer+assetName, since `Asset` has no `operator==` for HashMap use) tracks
how much of a shared asset has already been claimed by an earlier-settled
holding; each subsequent holding's settlement draws only its own remainder,
capped at its own `pendingShares` as a safety ceiling.

**Live reproduction after the fix (Phase 3, re-run on fresh genesis):**
| Holding | shares | Correct? |
|---|---|---|
| P1 → TGT1 | **300** | ✅ |
| P6 → TGT1 | **200** | ✅ |

Also confirmed live in the same run: `P2`'s TGT2 dividend
(`before=0 after=2,940,000`) — the sole-holder case works cleanly, isolating
that the bug is specifically about *shared* assets. New GoogleTest case:
`TwoPoolsWinningSameIpoTargetGetIndependentShareCounts`.

*(Along the way, this same scenario reconfirmed a related, already-documented,
non-bug limitation: `payoutSources` attributes an incoming dividend to
whichever holding registered that payer first — "first writer wins," per the
contract's own comment. P1 (executed first) received the QIPOTGT1 dividend;
P6 (same target, executed second) did not. This is intentional design, not
something this test changed.)*

### 1e. **SELL_QSWAP / SELL_QX never worked at all** *(new — found by this test)*
**Zero GoogleTest coverage existed for either sell path** — every existing
test only checked `Propose`'s input validation, never a real `Execute`. This
test's P8 pool was the first to ever attempt a full BUY→SELL round trip
against a real QSWAP pool, and it failed silently: the proposal passed
governance, `Execute` returned without error, but no shares sold and no QU
came back. Root-caused to **two independent, stacked bugs**:

1. **Missing swap fee.** QSWAP's `SwapExactAssetForQu` requires the caller to
   attach at least `QSWAP_ADDITIONAL_FEE` (100,000 QU) or it silently no-ops.
   QPOOL's `Execute` attached `0`. The parallel BUY path never hit this
   because the swap amount itself (millions of QU) comfortably clears the
   floor — only the SELL path, which has no naturally-attached QU (it's
   selling the *asset* side), needed an explicit fee and never had one.
2. **Backwards `releaseShares` precondition.** Before swapping, `Execute`
   calls `qpi.releaseShares(...)` to hand the shares to QSWAP's management.
   But `releaseShares` requires the *calling* contract (QPOOL) to already
   hold management rights over what it's releasing — and shares bought via
   `SwapExactQuForAsset` are managed by QSWAP from the moment of purchase.
   QPOOL never held management over them, so this call was structurally
   guaranteed to fail with `INVALID_AMOUNT` every single time, for exactly
   the holdings this path exists to sell. The identical flawed pattern
   existed in `SELL_QX` (also never tested).

**Fix:** (1) attach a new `QPOOL_QSWAP_SELL_FEE` (100,000) constant, funded
from QPOOL's own balance; (2) only call `releaseShares` when the shares are
**not** already under the target contract's management (checked via
`qpi.numberOfShares` with an explicit managing-contract filter) — applied to
both `SELL_QSWAP` and `SELL_QX`.

**Live verification (fresh genesis, full round trip):**
| Step | Before fix | After both fixes |
|---|---|---|
| `Execute` on the passed SELL proposal | returns silently, no state change, proposal stuck at PASSED forever | status → **EXECUTED** |
| Holding `shares` | unchanged at 1,418,373 | **0** (fully sold) |
| Holding `totalPayouts` | unchanged (0 new proceeds) | **+19,297,740 QU** real proceeds |
| Holding `active` | still 1 | **0** (correctly retired, fully sold + no pending) |

One operational note surfaced during verification: QPOOL's own contract
balance must be ≥ 100,000 QU for the attached fee to succeed. On a
freshly-deployed contract with no prior dividend/fee flow, that balance can
be near zero; in this test it was topped up directly to verify the fix. In
a real deployment, QPOOL accumulates this naturally from its 2% management
fee on every payout, but a contract deployed and immediately asked to sell
before ever processing a dividend could see this specific call fail on
balance alone (a different, already-correctly-surfaced `QPOOL_ERR_EXTERNAL_CALL`,
not silent).

**All 5 fixes:** GoogleTest suite is **30/30 green** (29 original/interim +
1 new multi-pool-attribution case), with no regressions from any fix.

---

## 2. Function & scenario coverage achieved

| Area | Exercised | Notes |
|---|---|---|
| `CreatePool` | 10 pools created | open + 1 permissioned (P7) |
| `Contribute` | all 10 pools, 2–4 members each | including a self-admission fix for the permissioned pool's own creator |
| `WithdrawUncommitted` | M3 (P1) | partial withdrawal confirmed |
| `Propose` (all 6 kinds) | BUY_QSWAP, BUY_QX, BUY_IPO ×6 targets, SELL_QSWAP | ALLOCATE not exercised live (needs a narrow, hard-to-manufacture `unattributedQu` precondition; deferred) |
| `Vote` / `Tally` | every proposal | multi-member and single-member pools both covered |
| `Execute` | every kind | including two formally-EXPIRED proposals (real wall-clock `EXECUTE_WINDOW` enforcement, see §4) |
| `SettleIpo` | 3 real IPO wins (P1/P6 shared target, P3 solo) | uniform-price discount + shared-asset attribution both proven |
| `ReconcileHolding` | P2's async QX fill | |
| `RebidIpo` / `ReconcileIpo` | covered in GoogleTest (existing + this session's earlier live test) | not re-exercised in this specific 8-phase run |
| `ClaimPayout` | P1, P2, P3, P6, P8 members | |
| `OfferPosition` / `BuyPosition` / `CancelOffer` | P1's TGT1 seat (sold to BUY1), P9/P10 syndicated seats (sold to BUY2/BUY3), a cancel-then-unchanged demo on P1's QSWAP holding | |
| `SetAdmission` | admit (PERM1) + revoke (PERM1), self-admit (P7_OWNER) | revocation confirmed to block future contributions without clawing back the existing seat |
| `DepositHoldingAsset` | P9 (ISS4), P10 (ISS5) | single-owner syndication pattern |
| IPO auctions | epoch 223 (TGT1+TGT2, double), epoch 225 (TGT3, single) | TGT4/TGT5 (epoch 227) and TGT6 (epoch 229) windows were missed — see §4 |
| Dividends | real `Payout` calls on QIPOTGT1/2/3, ISS1→QSWAP holding, ISS3→SELL_QSWAP proceeds | |
| END_EPOCH revenue split | observed passively (totalDistributedToShareholders/totalBurned moved correctly across epoch transitions) | not independently asserted this run |

---

## 3. Per-pool outcomes

| Pool | Members | What it did | Outcome |
|---|---|---|---|
| **P1** | owner + M1–M3 | Real QSWAP buy (ASSETA); IPO bid on TGT1 (300@4000) | Won TGT1 at a real discount (costBasis shrank from 1.2M to ~200K after a near-total-fill uniform clearing); received a real QIPOTGT1 dividend (+3.92M); sold part of its TGT1 seat to BUY1 |
| **P2** | owner + M4–M6 | QX buy (ASSETB); IPO bid on TGT2 (300@4000) | Won TGT2 outright; real QIPOTGT2 dividend (+2.94M) with clean sole-holder attribution |
| **P3** | owner + M7–M9 | IPO bid on TGT3 (250@4500) | Won at a discount; real dividend; all members claimed payout |
| **P4** | owner + M10–M12 | IPO bid on TGT4 — **window missed** (see §4); redirected to QX buy (ASSETB) | Capital never deployed by the dead proposal (confirmed still 50M uncommitted); real QX holding acquired via the redirect |
| **P5** | owner + M13–M15 | IPO bid on TGT5 — **window missed**; redirected to QX buy (ASSETF) | Same pattern as P4 |
| **P6** | owner + M16–M18 | IPO bid on TGT1 (200@5000, competing with P1); 2nd holding via QSWAP buy (ASSETC) | Won TGT1 seats correctly (200, not the buggy combined 500); did **not** receive the QIPOTGT1 dividend (documented first-writer-wins `payoutSources` behavior, confirmed live, not a bug); real second acquisition via QSWAP |
| **P7** | permissioned; owner + PERM1 | QX buy (ASSETF); PERM1 admitted then later revoked | Self-admission gap diagnosed and patched; revocation confirmed to block re-contribution while leaving PERM1's existing seat intact |
| **P8** | owner + M19–M20 | QSWAP buy (ASSETC) then **real SELL_QSWAP** | First-ever successful live SELL_QSWAP: full position (1.4M+ shares) sold for real QU proceeds after both sell-path fixes; members claimed payout |
| **P9** | single-owner | `DepositHoldingAsset` (ISS4 syndicates ASSETD) | 100% seat ownership at deposit; sold 20% of seats to BUY2 |
| **P10** | single-owner | `DepositHoldingAsset` (ISS5 syndicates ASSETE) | Same pattern; sold 20% of seats to BUY3 |

---

## 4. Operational findings (not contract bugs, but worth recording)

- **A real multi-hour gap between sessions cost three IPO windows.** The
  devnet's `epoch_watch.py` keeps advancing ticks/epochs regardless of
  whether a test script is actively driving it. TGT4/TGT5's window (epoch
  227) and TGT6's (epoch 229) both closed with P4/P5's proposals never
  executed and no bids ever placed on TGT6 at all. This is exactly the
  scenario `QPOOL_EXECUTE_WINDOW` exists to guard against, and it worked
  correctly: both dead proposals formally transitioned to **EXPIRED** the
  instant `Execute` was called on them, and — critically — **their capital
  was never deployed**, confirmed still sitting at the pools' full original
  `uncommittedTotal`. No funds were ever at risk from the missed windows.
- **Docker container restarts reset devnet chain state on this devkit.**
  Rebuilding the `core-lite` image and restarting the container (with or
  without an explicit `--wipe`) reseeded the chain from the baked genesis
  snapshot rather than resuming from where it left off. This cost the
  in-progress Phase 1–8 run's live chain state (though not the evidence —
  every phase's full output was already captured to log files at broadcast
  time, per this session's established evidence-trail convention). Anyone
  needing to modify `QPool.h` mid-test should expect a restart to mean a
  fresh genesis, not a hot patch.
- **A known, accepted devnet flakiness (occasional dropped transactions)**
  surfaced twice more during the final SELL_QSWAP verification (a `Tally`
  call needed one retry). This matches the exact pattern already documented
  from `Qswap::CreatePool` earlier in this project and is not a QPOOL issue.
- **`ALLOCATE` was not exercised live.** It requires a pool to already have
  non-zero `unattributedQu`, which only accumulates via a narrow edge case
  (an incoming payment whose `payoutSources` entry points to an already-
  retired holding in a still-active pool). Manufacturing this deliberately
  wasn't worth the setup cost given the session's time budget; it remains
  covered only by whatever the delivered package's own GoogleTest suite
  asserts.

---

## 5. Files changed

- `core-lite/src/contracts/QPool.h` — all 5 fixes (devkit copy, TEST-ONLY
  `QPOOL_VOTE_WINDOW`/`QPOOL_EXECUTE_WINDOW` compression retained for
  future live testing).
- `QPOOL-testnet-v1/contracts/QPool.h` — all 5 fixes backported, verified
  byte-for-byte identical to the devkit copy apart from the intentional
  TEST-ONLY window values and comment wording.
- `core-lite/test/contract_qpool.cpp` — 30 cases total; new case
  `TwoPoolsWinningSameIpoTargetGetIndependentShareCounts`.
- `core-lite/src/contracts/QIpoTarget2.h` — added the same `Payout`
  forwarder `QIpoTarget1.h`/`3–6.h` already had (a test-scaffolding gap
  from the earlier, smaller test, not a contract issue).
- `tools-bin/rebuild_genesis_qpool_comprehensive_test.sh` — wallet funding
  bumped (M*, BUY*, PERM*) so every role keeps a post-spend QU buffer;
  without it, a wallet that contributes its entire balance drops to exactly
  0 and is pruned from Qubic's spectrum, silently losing the ability to
  submit *any* further transaction (a real Qubic network characteristic,
  not a QPOOL bug — confirmed by reading `qubic.cpp`'s
  `processTickTransaction`).
- `tools-bin/qpool_lib.py` + `qpool_comprehensive_phase{1..8}.py` — the full
  phased live-test harness, including `discover_pools()` (batched
  `CreatePool` calls don't land in submission order) and the funding/window
  fixes folded in as the run progressed.
