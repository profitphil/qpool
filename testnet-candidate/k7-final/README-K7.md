# K7: small fixes + CancelQxBid

Built 2026-10-21. Nothing here has been run on a testnet, and nothing was independently reviewed (no reviewer agents were run, at the owner's request). Your testnet is the test.

This SUPERSEDES `../k7-interim/`. The IPO name-claim rework in that interim file was dropped (the name-free IPO, K8, replaces it). Its small fixes are all here.

## Files
| File | What it is | sha256 |
|---|---|---|
| `QPoolK7-devkit.h` | **Use on the devkit testnet.** Same wiring as `QPoolK6.h`: structs `QPOOLK6` / `QPOOLK62`, `QPOOLK6_` prefix, `TICKS_PER_HOUR = 2`, QPAYHUB index 50, test QPAY issuer. Drop-in for the existing wiring. | a26f126ee765d967534bf5f0c8032895c0f3b6a789e1aea3df65240e8c898afd |
| `QPool-K7.h` | Production-valued source it is generated from. Never use it on the devkit, and never put the devkit file on a real network. | e6c8976ee74e70d88633d98466d90932ee046907ded09e3bf02629200321f6a8 |
| `K7-vs-QPoolK6.diff` | Exactly what changed against the `QPoolK6.h` you are running (devkit to devkit). Read first. | |
| `K7-vs-base.diff` | Same against the production-valued base (K6 + production constants). | |
| `K7-TESTNET-CHECKS.md` | 22 numbered checks with steps and expected results: 11 for the small fixes (S1 to S3 and the others named in each step) and 11 for CancelQxBid (a setup step plus C1 to C10, named inside the steps). | |
| `k7-tools/` | Compile check, lint, release gate, devkit generator, and the CancelQxBid gtest. See `k7-tools/HARNESS.md`. | |

## What changed against QPoolK6.h
**CancelQxBid (new procedure, id 23).** Takes back a pool's resting QX bid.
- Input: `poolId`, `holdingId`. Output: return code, `cancelledShares`, `refunded`, `pendingLeft`. No fee, and any attached QU is refunded.
- Who may call: the pool creator, or any seat owner of that holding (someone charged for the bid or who bought a seat from them), at any time; ANYONE once the bid is older than `QPOOL_QX_BID_MAX_AGE_TICKS` (72 hours = 72 x `TICKS_PER_HOUR`, strictly greater). Otherwise `NOT_AUTHORIZED (22)`.
- Effect: fills that already arrived are credited in the same call; the rest is cancelled on QX with `RemoveFromBidOrder`; the refund from QX is split over the seat owners by cost and credited to their spendable balance; their open offers on shrunk seats are withdrawn; the QX_BID_PENDING block on the ledger is lifted; an empty holding is freed or retired like a sold-out one.
- New return code `NO_PENDING_BID (40)`: nothing pending, or everything already filled (nothing written; call `ReconcileHolding`). `EXTERNAL_CALL (11)` if QX removes nothing (nothing written). `WRONG_STATUS (7)` on an IPO holding.
- The ledger now stores the resting bid's price and tick (`SourceLedger.qxBidPrice`, `qxBidTick`). `GetHolding_output` gains `qxBidPrice` and `qxBidTick` at the end (+16 bytes).

**Small fixes.**
1. Proposal scan stops at the first unused slot (`continue` became `break`, review R6).
2. END_EPOCH checks with `qpi.isContractId` that a contract exists at the QPAYHUB index; if not, the 30 percent leg falls into the burn leg and is not booked as deposited. (It proves some contract is there, not that it is QPAYHUB.)
3. A BUY_IPO whose price was taken by another pool now fails the proposal and unlocks its voters (N5). The old retry inside the execute window is gone.
4. BuyAndBurnQpay measures the hand-over fee from the balance instead of booking a nominal 100 QU (A9).
5. `Offer.sharesSnapshot` removed; `GetPosition.offeredShares` returns the LIVE shares so the buyer flow (`GetPosition` then `BuyPosition` with `expectedShares`) works after any share change. Layouts of `GetPosition_output` and `BuyPosition_input` are unchanged from K6.
6. Comment correction on the opening-bid remainder.

## State and ABI against QPoolK6.h
- State size: `Offer` 32 to 24 bytes (-262,144 in total), `SourceLedger` 104 to 120 bytes (+262,144 in total). Net zero, so the state is the same size as K6 (190,463,648 bytes); this is derived from the two agents' separate measurements, not measured on the merged file. The layout still differs, so a fresh deployment is needed.
- New procedure id 23, new return code 40, `GetHolding_output` +16 bytes appended. No other layout changed.

## NOT in this build
- The IPO share name is still typed by the proposer, and K6's registration-time name claim is still in. It still has the known K6 flaws (a zero-win bid can leave a name claimed for ever; a second honest registrant can be made unprovable). K8 (name read from the chain, settlement at epoch start) replaces all of it and is being built.
- K4b (a holding that wins seats under an unprovable name cannot settle).

## What was checked
Ran: the merged production file passes `check_contract.sh` (compile, link, smoke run of INITIALIZE plus the 9 read-only functions, Apple clang under Rosetta), `lint_contract.py` and `release_gate.py`; the small fixes and CancelQxBid merged with zero conflicts; the CancelQxBid agent's 14-test gtest (against the real QX code of the local core, in a ported scratch core) passes on the MERGED file (14 of 14). That agent also reports eight deliberate breakages of its own code that its tests caught.
Not done: no run on a real node or real QX; the devkit file itself was not compiled here (the harness builds the production names; the generator reproduces `QPoolK6.h` byte for byte from its base, 52 of 52 tool checks); the repo's own old gtest file was not run; no independent review.

## Things the CancelQxBid agent flagged (read before testing)
- **No real QX test.** All behaviour was exercised only against the local core's QX code. Start with C1 to C4 and C7 (conservation) on the testnet.
- **Re-placing a bid right after a cancel** is not prevented: a pool with a PASSED BUY_QX proposal can place the next bid in the next transaction. The block is bounded (72 hours before anyone can cancel; each proposal needs its own vote), not eliminated.
- **Shared with BuyAndBurnQpay**: QX has one order per (QPOOL, asset, price), so QPOOL's bids on the same asset and price merge. Pre-existing and not fixed: unburned QPAY shares can look like fills of a pending pool bid, and BuyAndBurnQpay burns by quantity.
- **Stray shares**: someone who donates shares of the asset can make the books think a bid has filled while the order still rests on QX; `CancelQxBid` then answers `NO_PENDING_BID`. Pre-existing.
- QX's `RemoveFromBidOrder` is believed to be all-or-nothing; the code also handles a partial removal. Not confirmed from `Qx.h` alone.

## First things to try on the testnet
1. C1, C2, C3: rest a bid, see the second pool blocked (25), creator cancels, funders are refunded, the second pool can now buy.
2. C4: a stranger is refused (22) until the age limit passes (144 ticks on the devkit), then allowed.
3. C7: conservation after every cancel (balance delta equals refunded; `globalUnattributed` unchanged).
4. S1 (proposal scan), S2 (QPAYHUB probe with and without a contract at the index), S3 (taken IPO price).
