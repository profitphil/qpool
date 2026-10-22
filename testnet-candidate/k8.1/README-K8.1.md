# K8.1: the creator and up to three admins run the pool

Built 2026-10-21 on top of K8 (`../k8/`). Not run on a testnet; no independent review (no reviewer agents, at the owner's request). Your testnet is the test.

## The rule (the owner's)
Only a pool's **operators** act for it: the **creator** and up to **three admins** the creator names. Nobody else can place or cancel an order or bid for the pool.

| Call | Who may call it in K8.1 |
|---|---|
| `Execute` (every proposal kind: IPO opening bid, QX buy, QX sell, QSWAP buy, QSWAP sell) | operators only. Anybody may still close a mandate whose execute window has passed (first call WINDOW_CLOSED); nothing is spent |
| `RebidIpo` | operators only (before: any seat owner) |
| `CancelQxBid` | operators any time; **anybody once the bid is older than 72 hours** (the one public exception, so a bid of a dead or hostile creator cannot block every other pool's BUY_QX on that asset for ever). Before: also any seat owner |
| `SetPoolAdmins` (new, procedure 24) | the creator only |
| `Tally`, `ReconcileHolding`, `SettleIpo`, the epoch-start settlement, `ClaimPayout` (own money), `BuyAndBurnQpay` | unchanged: open, they choose nothing |
| `SetAdmission`, `SetPermissioned`, `DepositHoldingAsset`, `CloseEmptyPool` | unchanged: creator only |

An admin has no vote weight, no access to anybody's QU, and cannot name admins. An admin may be any id (a member, a bot, a second wallet of the creator); it need not be on the roster.
`SetPoolAdmins{poolId, admin0, admin1, admin2}` replaces the whole list (NULL_ID = empty slot, three NULL_IDs clear it). Refused: a non-creator (INVALID for admins, NOT_AUTHORIZED), the creator himself, the contract's own id, the same id twice (INVALID_INPUT); the same list again writes nothing. `GetPool` ends with `admin0..2`.
Non-operators get `NOT_AUTHORIZED (22)`; the reward is returned and nothing is written. No new return code.

## Files
| File | What it is | sha256 |
|---|---|---|
| `QPoolK8.1-devkit.h` | **Use on the devkit testnet** (same wiring as K6/K7/K8: `QPOOLK6` names, 2 ticks per hour, QPAYHUB stub at 50) | c37ea408c57c97c7f20d7e62bb8353cd4e1bbdc3af9136dc7bfab547f17f4eeb |
| `QPool-K8.1.h` | production-valued source it is generated from | c3b83b10545f6042bf26323e16552a23f016b467b78076134caf91cd5c93a9da |
| `QPoolK8.1-season-devkit.h`, `QPool-K8.1-season.h` | the same with `QPOOL_AUTO_SETTLE_MAX = 3`, for the season tests only (the release gate fails it on purpose) | adaff3cd07d0b3fa100236866e3f5201a85b065a50161a17c44f0a1dd8ca8ddc (devkit) |
| `K8.1-vs-K8.diff` | exactly what changed against K8 (production-valued) | |
| `TESTING_GUIDE_K8.1.md` | the full every-function guide, with the operator rule and the new suite S | |
| `REALISTIC_SEASON_TEST_K8.1.md`, `season_plan_k81.py`, `season-plan-k81/` | the 40-wallet, 8-pool season test regenerated for K8.1 (2,028 actions) | |
| `REALISTIC_SEASON_TEST_K8.1_100.md`, `season_plan_k81_100.py`, `season-plan-k81-100/` | the 100-wallet, 10-pool season test regenerated for K8.1 (3,492 actions) | |
| `k8.1-tools/` | the gate (`release_gate.py`) and the operator and cancel gtest (`cancel_qx_test.cpp`, `build_run.sh`) | |

## Changes in the contract
`Pool` gains three `id` fields (admin0..2, +96 bytes per pool, +98,304 bytes of state in all; a fresh deployment is needed as before). New helper `IsPoolOperator`, new procedure `SetPoolAdmins` (id 24), the three gates above, `GetPool` output +3 ids.
State layout otherwise as K8. ABI: `GetPool_output` grows by three ids at the end; every other layout is as K8.

## What was checked
Ran: the production file passes `check_contract.sh` (compile, link, smoke run of INITIALIZE plus the 10 read-only functions), `lint_contract.py` and `release_gate.py`; the season build fails the gate as intended.
Ran (gtest against the real QX code of the local core, in the scratch core): the cancel suite adapted to the new rule plus two new operator tests, 15 tests, 14 passed and 1 is switched off (the old squat test, obsolete since K8). The operator tests cover SetPoolAdmins (creator only, refusals, identical list writes nothing, clearing, pools independent), Execute (stranger, YES voter, creator of another pool refused; an admin outside the roster succeeds; a removed admin refused; the dead mandate can be closed by anybody) and CancelQxBid (a funder who is not an operator refused, an admin with no seat succeeds).
Not run: RebidIpo's gate (it needs a live IPO; the code is the same operator check as in Execute and CancelQxBid), the K8 name and settlement tests on this file, anything on a real node, MSVC, the official verifier. The season plans are generator-checked only.

## Known trade-offs (read before relying on it)
- If the creator and every admin are unavailable, a passed mandate lapses unexecuted (nothing is spent) and no IPO rebid can happen. Name admins before the first vote.
- Operators are trusted: an admin can rebid at any price with all of the holding's free funds (the burn variant of review item R5); the creator removes him with `SetPoolAdmins`.
- Timing is now the operators' to choose inside the execute window; nobody else can fire a mandate at a price they moved.
- A hostile creator's resting QX bid still blocks other pools' BUY_QX on that asset for up to 72 hours; then anybody can cancel it. Say if you want that exception removed (then a dead creator's bid blocks for ever).
