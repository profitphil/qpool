# K8.2: several pools can rest QX bids on the same token

Built 2026-10-22 on top of K8.1 (`../k8.1/`). Not run on a testnet; no independent review (no reviewer agents, at the owner's request). Your testnet is the test.

## What changed
Before: only ONE resting QX bid per payer's ledger; every other pool's BUY_QX on that token was refused (`QX_BID_PENDING`) until it filled or was cancelled.
Now: up to **8 resting bids per ledger**, at any prices, and **the same price too**.

How the shares of a fill are attributed (they arrive without any call into QPOOL):
- The ledger keeps a **table of its resting bids in priority order**: highest limit price first and, at one price, the bid placed first (`Holding.qxSeq`).
- QX fills bids from the highest price down (read in `Qx.h`: its matching loop starts at the head of the bid queue). QPOOL's bids at one price are ONE merged order at QX (QX cannot tell the pools apart), which QPOOL serves **earliest first** (an internal rule that mirrors what QX would do with separate orders).
- The shares that arrived and nobody has credited are handed to the bids in that order (a "waterfall"): the first bid takes up to its pending shares, the next the rest. Anyone's `ReconcileHolding` credits ALL the ledger's bids.
- The dividends the hook sets aside for such shares are split the same way and kept per bid (`Holding.qxEscrow`); a bid's holding is paid its own when its fills are credited.
- Before a new bid is placed, `Execute` credits the fills not yet credited to the bids that already rest, so a new bid at a higher price can never take them.
- A bid leaves the table where its holding's pending shares reach 0 (fully filled, cancelled, or the holding retires) in the one place that changes holdings (`ApplyHolding`); the bids behind it close up in order. `CancelQxBid` works per bid with the same rights as in K8.1 (creator and admins any time, anybody after 72 hours).
- A ninth bid on a full ledger is refused with `QX_BID_PENDING (25)` (its meaning is now "ledger full").

## Files
| File | What it is | sha256 |
|---|---|---|
| `QPoolK8.2-devkit.h` | **Use on the devkit testnet** (same wiring as before: `QPOOLK6` names, 2 ticks per hour, QPAYHUB stub at 50) | 36a8ad3e9395bdceafb21b214ec5295d2ff96b343e4f560fc054cbbc54949b70 |
| `QPool-K8.2.h` | production-valued source it is generated from | dd84fbecaae4cb232fa2fdfcda9db9d56ea0ee0733085f67f67a620c69f6266b |
| `QPoolK8.2-season-devkit.h`, `QPool-K8.2-season.h` | the same with `QPOOL_AUTO_SETTLE_MAX = 3` for the season tests only (the release gate fails it on purpose) | de65da16c5f4705caf11594e0e13e3ff6c2b3cf1256697e1b232fad3267a392c (devkit) |
| `K8.2-vs-K8.1.diff` | exactly what changed against K8.1 (production-valued) | |
| `TESTING_GUIDE_K8.2.md` | the full every-function guide, with the new suite T (several resting bids) and updated suites O, S | |
| `REALISTIC_SEASON_TEST_K8.2.md`, `season_plan_k82.py`, `season-plan-k82/` | the 40-wallet, 8-pool season test (2,073 actions) with the new scene E5.S8 (two pools rest bids, a seller fills them in price order) | |
| `REALISTIC_SEASON_TEST_K8.2_100.md`, `season_plan_k82_100.py`, `season-plan-k82-100/` | the 100-wallet, 10-pool season test (3,573 actions) | |
| `k8.2-tools/` | the gate and the gtest (`cancel_qx_test.cpp`, `build_run.sh`) | |

## State and ABI against K8.1
- `Holding` gains `qxBidPrice`, `qxBidTick`, `qxSeq`, `qxEscrow` (4 x 8 bytes). `SourceLedger` loses `qxBidPrice`/`qxBidTick` and gains `qxCount` and `qxHold[8]`. `StateData` gains `qxSeq`. About +5.1 MB of state in all (derived from the struct sizes, not measured). A fresh deployment is needed, as before.
- No new procedure or function. `GetHolding` keeps its fields (`qxBidPrice`, `qxBidTick` now come from the holding). No new return code; `QX_BID_PENDING` now means a full ledger.

## What was checked
Ran: the production file passes `check_contract.sh` (compile, link, smoke), `lint_contract.py` and `release_gate.py` (the season build fails the gate as intended).
Ran (gtest against the real QX code of the local core, in the scratch core): **19 tests pass** (1 more is switched off, the obsolete squat test): the 15 operator and cancel tests adapted to the new rule, plus four new multi-bid tests: two pools at different prices fill in price order; two pools at the same price fill earliest first and the second cancels exactly what still rests; a new higher bid cannot take earlier fills; dividends on a contract's shares are set aside for the bid that won them (59,400 to the higher bid, then 19,800 and 29,700 split by the waterfall).
Ran again on this file: the IPO settlement tests (T1, T4) pass; the IPO name tests A to E pass; F, G and H fail exactly as they did on K8 (they expect return code 40 where the code is 41, and H expects a manual settle after the epoch-start settlement has done it), not because of K8.2.
Not run: anything on a real node; MSVC; the official contract verifier; RebidIpo's operator gate (needs a live IPO); a ninth bid refusal and the 8-pool capacity case (T8 in the guide).

## Known limits
- The attribution rests on QX filling the highest bid first, which is in `Qx.h`'s matching loop; the tie order at one price is QPOOL's own rule (QX merges those orders). If a later QX changed the matching order, the waterfall would credit the wrong bid.
- Donated shares of the token are credited as fills to the first bids in priority order, capped at each bid's pending shares (guide T9). Reading QX's own order book would close this; not built.
- `BuyAndBurnQpay` bids at ask levels and could merge with a pool's bid at the same price on the QPAY token; it cancels only its own remainder.
- **Resting SELL limit orders do not exist.** A SELL_QX sells at once into the current bids and keeps the rest. Resting sells are the mirror image (asks fill lowest price first) but their QU proceeds arrive silently with no label and would be taken for unattributed revenue today: they need a proceeds pot in the incoming-transfer hook. Proposed as K8.3.
