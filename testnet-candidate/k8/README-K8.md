# K8: K7 + name-free IPO + settlement at epoch start

Built 2026-10-21. Nothing here has been run on a testnet, and nothing was independently reviewed (no reviewer agents, at the owner's request). Your testnet is the test.

K8 = K7 (`../k7-final/`: small fixes and CancelQxBid) + two changes: the IPO share name is read from the chain instead of typed, and IPO holdings settle themselves at the start of the epoch.

## Files
| File | What it is | sha256 |
|---|---|---|
| `QPoolK8-devkit.h` | **Use on the devkit testnet.** Same wiring as `QPoolK6.h` and `QPoolK7-devkit.h`: structs `QPOOLK6` / `QPOOLK62`, `TICKS_PER_HOUR = 2`, QPAYHUB index 50, test QPAY issuer. | 2b9af7412bd5a61acd41edf61306df099a7fba002a32346947253719ddc16965 |
| `QPool-K8.h` | Production-valued source it is generated from. Never use on the devkit; never put the devkit file on a real network. | 706127c4ea072d32fc5d0943d1f3aa17f4d0e7f108e3d72a307324a799ea543b |
| `K8-vs-K7.diff` | What K8 adds on top of K7 (production-valued). Read first. | |
| `K8-vs-QPoolK6.diff` | Everything against the `QPoolK6.h` you started with (devkit to devkit). | |
| `K8-TESTNET-CHECKS.md` | 17 checks for the name-free IPO and settlement at epoch start. K7's checks (CancelQxBid, small fixes) are in `../k7-final/K7-TESTNET-CHECKS.md`. | |
| `TESTING_GUIDE_K8.md` | **The full every-function guide** for K8: all suites A to N edited for K7/K8, plus new suites O (CancelQxBid), P (names from the chain), Q (settlement at epoch start), R (K7 small fixes); invariants I1 to I16; the multi-pool batteries; appendix generated from the K8 header. Supersedes `../TESTING_GUIDE.md` for K8. | |
| `REALISTIC_SEASON_TEST_K8.md`, `season_plan_k8.py`, `season-plan-k8/` | The 40-user, 8-pool, 7-epoch season test regenerated for K8 (1,970 actions, coverage asserted). Run it on the season build below. | |
| `REALISTIC_SEASON_TEST_K8_100.md`, `season_plan_k8_100.py`, `season-plan-k8-100/` | **The large season: 100 wallets, 10 pools** (pools of 9 to 42 members), 7 epochs, 6 IPO targets with 5, 6, 4, 8, 3 and 5 pools, 3,426 actions, coverage asserted. Same season build. Run the 40-wallet season first, then this one. | |
| `QPoolK8-season-devkit.h` (and `QPool-K8-season.h`) | K8 with `QPOOL_AUTO_SETTLE_MAX = 3` for the season test, so each IPO epoch change leaves holdings for manual `SettleIpo`. **Test only**: the marker `SEASON BUILD ONLY` and the bound make the release gate fail it. | 5635548dee2f2a1542fc5601d64ec710e007d83e6d67ffbf1226af24bdc1d0ef (devkit), 1f6325bd4c4fdbd949c1f2e3ae77f078dee1691994be12f2e9b220ad720d69cb (production-valued) |
| `k8-tools/release_gate.py` | Updated gate: also fails on `SEASON BUILD ONLY` and on `QPOOL_AUTO_SETTLE_MAX` other than 32; takes `--qpayhub-index N` (default 29) so the real network number is a setting at deployment. | |

## 1. The IPO name is no longer typed
An IPO bid needs only a contract index, a quantity and a price, so a `BUY_IPO` proposal no longer carries a share name.
- `Propose` for `BUY_IPO` must send `asset.assetName = 0` (an all-zero asset is simplest); a non-zero name returns `INVALID_INPUT (1)`. The `Propose_input` layout is unchanged.
- The K6 name claim at registration is gone. Nothing can be claimed, squatted or poisoned any more.
- **Where the name comes from.** END_EPOCH (which runs before the core finishes the IPOs) queues every contract index whose IPO book is full (final slot priced above 0), from index 31 up. At the start of the next epoch, `BEGIN_EPOCH` walks the assets issued by the zero issuer (the all-zero key, AAAA...FXIB; only the core creates these, when an IPO finishes) and finds the NEW names.
- **Pairing.** One new name and one queued index: paired (certain). Several in the same epoch: see the ordering note below; the contract matches each name to the book whose bidders hold its shares, using a fingerprint, and pairs only if every name fits exactly one book and every book exactly one name.
- **Refusal instead of guessing.** If the counts differ, the queue overflowed (more than 16), or the fingerprints do not match one to one, nothing is paired for that epoch ("quarantine"). Later epochs pair again.
- Indices 1 to 30 still use the embedded name table. Above 30, a purchase (`BUY_QX`, `BUY_QSWAP` path) or deposit of a contract's shares needs the (index, name) pair in the registry, else `NAME_PENDING (41)`.
- New read-only view `GetNameRegistry` (function 10): input `{index, assetName}`, output 14 values (last pairing epoch, counts, mismatches, the name for an index, and its state: unknown / table / paired / known-never-paired).

**Ordering note (a correction to what I said earlier).** The pairing does NOT rely on the order of the new names. I had assumed the core lists the new names in the order the IPOs finished. The agent checked the core code: right after `finishIPOs`, `endEpoch` calls `assetsEndEpoch`, which rebuilds the asset table in the order of the holders' key hashes. On the core's own code the order was the issuance order 12 of 12 times before that step and reversed 7 of 12 times after it. Pairing by order would have mis-paired about half of the epochs with two or more IPOs. The fingerprint pairing gave the right answer in 14 of 14 test epochs, 9 of them with a reversed order. This was read in your local core, not on a node.

## 2. Settlement at epoch start
- `SettleIpo`'s body moved into a private routine used by both the `SettleIpo` procedure and `BEGIN_EPOCH`. Manual `SettleIpo` stays as a backup with the same behaviour. A holding that `BEGIN_EPOCH` already settled returns `IPO_NOT_ACTIVE (18)` when called manually.
- `BEGIN_EPOCH` first pairs names, then settles holdings whose target is ready (snapshot taken, epoch over), walking the table of unsettled IPO holdings from a persistent cursor.
- Bounded: at most `QPOOL_AUTO_SETTLE_MAX = 32` holdings per epoch (settled or refused). A refused holding (for example a name still pending) writes nothing and the loop continues. One pass per epoch, so a node restart at the epoch start does nothing twice. For a backlog test on the testnet set the constant to 2.
- No QU leaves the contract in `BEGIN_EPOCH`. `GetStats` gains 5 trailing fields (21 to 26 values): `readyUnsettled`, `autoSettleEpoch`, `autoSettleDone`, `autoSettleSkipped`, `autoSettleTotal`.

## Return codes, ABI and state
- New return code `NAME_PENDING = 41` (the name agent used 40, which `CancelQxBid`'s `NO_PENDING_BID` already holds in K7, so it was moved to 41 in the merge). `NAME_UNPROVEN (34)` now means only an inventory shortfall.
- Changed meanings: `INVALID_INPUT` on a `BUY_IPO` with a name; `CONTRACT_MISMATCH (28)` only for a name the table or registry ties to another index.
- New: system procedure `BEGIN_EPOCH`, function 10 `GetNameRegistry`, `GetStats` +5 fields. No procedure or function id changed.
- A pool's `GetHolding.assetName` reads 0 for an IPO holding until it settles (and stays 0 if it won no seat).
- State: appended fields only. About +8,568 bytes against K7 (+8,528 name registry, +40 settle bookkeeping), derived from the agents' separate measurements, not measured on the merged file. A fresh deployment is needed.

## Known limits (read before testing)
- **A holding that won seats on a quarantined index cannot settle** (`NAME_PENDING` for ever; its capital stays on the books and that contract's dividends stay parked). There is no write-off path (the K4b idea is not built). It can only be triggered by two IPOs in the same epoch with identical bidder sets.
- **Contracts above index 30 that IPO'd before QPOOL's deployment** (or in the same epoch change as QPOOL's own IPO) are recorded as "known, never paired": their shares cannot be bought or deposited through QPOOL.
- **A single new name with a single queued index pairs by count alone.** A stray new name coinciding with a queued index whose own IPO failed to issue would be mis-paired; the agent found no way for that to arise on the chain.
- **Cost not measured.** The agent's estimate for a full pass of 32 settlements is 20 to 35 ms, a guess from counting hash operations. How the core charges `BEGIN_EPOCH` time to the fee reserve was not found. Lower the constant before deployment if the testnet shows a problem.
- **Not confirmed on a node:** the exact order of the core's epoch-end steps was read in your local core, not upstream main; a restart exactly at an epoch start; the cost of the zero-issuer walk on a populated table.

## What was checked
Ran on the merged production file: `check_contract.sh` (compile, link, smoke run of INITIALIZE plus the 10 read-only functions, Apple clang under Rosetta), `lint_contract.py`, `release_gate.py`: all PASS. The devkit file comes from the generator that reproduces `QPoolK6.h` byte for byte; it was not compiled here.
The agents' own scratch tests ran against the core's own `finishIPOs` and `assetsEndEpoch` and a real `qpi.bidInIPO` (name agent: 8 tests; settle agent: 4 tests). Re-run on the MERGED file:
- Settlement tests T1 and T4 pass (all ready holdings settle at the epoch start; the books are identical to manual settlement).
- Name tests A to E pass (baseline, two IPOs paired by book, identical books quarantined and recovery, counts differ, pairing right whatever the walk order).
- Name tests F, G, H fail only because they expect code 40 and the merge renumbered it to 41; H also expects a manual settle to succeed, but the epoch start has now settled that holding (18). These are expected differences, not defects, but the tests were not rewritten.
- CancelQxBid tests: 13 of 14 pass once the registry is seeded for the test contract (as `BEGIN_EPOCH` would have). The 14th asserts that a squatting name binding is removed on cancel; there is no such binding any more.
Not run: the settle agent's backlog tests T2 and T3 (bound 2) on the merged file (they passed on its own file); the repo's old gtest suite (outdated); MSVC; the official contract verifier; anything on a real node.

## First things to try on the testnet
1. One IPO with a full book: read the explorer's zero-issuer list before and after, then `GetNameRegistry` for the new index; the name must be paired.
2. At the same epoch change read `GetStats`: `autoSettleDone` should count every pool's holding; manual `SettleIpo` returns 18.
3. Two IPOs finishing in one epoch with different bidders (for example 31 and 32): each index must get its own name, whatever order the explorer lists them in.
4. A failed IPO (book not full): holdings are retired and the budget returned automatically.
5. With `QPOOL_AUTO_SETTLE_MAX = 2` and 5 holdings: 2, 2, 1 over three epochs.
