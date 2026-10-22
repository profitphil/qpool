# K7 interim build (small fixes + IPO name-claim rework)

Status: INTERIM. Built 2026-10-21. NOT the final K7. `CancelQxBid` is not in it yet, and no adversarial review has been run on this merge. It is meant for the testnet session to start testing what is already built.

## Files
| File | What it is | sha256 |
|---|---|---|
| `QPoolK7-interim-devkit.h` | **Use this on the devkit testnet.** Same wiring as `QPoolK6.h`: struct names `QPOOLK6` / `QPOOLK62`, `QPOOLK6_` prefix, `TICKS_PER_HOUR = 2`, QPAYHUB index 50, test QPAY issuer. Drop-in for the existing wiring (index 49, QPayhubStub at 50). | acf0247a46068a1f5e8afc3c5cbf816b00adbdef1662a60313838adf0f5218d1 |
| `QPool-K7-interim.h` | Production-valued source it was generated from (3600 ticks per hour, QPAYHUB 29, real QPAY issuer). Never use this on a devnet and never put the devkit file on a real network. | 316b966ce4a55409065a0dcf4487abda1673262c6ee5063cac6fa4111d71977b |
| `K7-interim-vs-QPoolK6.diff` | Exactly what changed against the `QPoolK6.h` you are running (devkit to devkit). Read this first. | |
| `K7-interim-vs-base.diff` | Same changes against the production-valued base (K6 plus production constants). | |

The devkit file is produced from the production file by `make_devkit.py`. That tool reproduces `QPoolK6.h` byte for byte from its production-valued base (52 of 52 tool checks pass), so the three devkit-only differences are the only ones.

## What is in it
Small fixes:
1. **Propose scan** (review R6): the search for the caller's live proposal stops at the first unused slot instead of reading all 262,144. Safe because used slots form a prefix: nothing writes `kind = 0` and slots are reused in place.
2. **QPAYHUB leg probe** (R1 residue): END_EPOCH checks with `qpi.isContractId` that a contract slot exists at the QPAYHUB index; if not, the 30 percent leg falls into the burn leg and is not booked as deposited. It proves only that the index is below the node's contract count, not that it is QPAYHUB.
3. **IPO_PRICE_TAKEN** (N5): the proposal is now marked FAILED and its YES voters are unlocked. Side effect: the old retry inside the execute window is gone; a new proposal is needed.
4. **BuyAndBurnQpay hand-over fee** (A9): the amount booked as paid is measured from the balance before and after the call instead of the nominal 100 QU.
5. **F-02 cleanup**: `Offer.sharesSnapshot` is removed. `GetPosition.offeredShares` now returns the LIVE shares of the holding, so the buyer flow (read `GetPosition`, pass `offeredShares` as `expectedShares`) works after any share change. Layouts of `GetPosition_output` and `BuyPosition_input` are unchanged from K6.
6. Comment correction on the opening-bid remainder (676-seat cap).

IPO name-claim rework (R3, F-01):
- `SourceLedger.claimedName` records the name claimed when the first IPO holding creates a ledger.
- A later IPO registrant on the same unbound ledger that declares a different name is refused with `SOURCE_CONFLICT (23)` at Propose and at Execute, before any bid. The same name is accepted.
- The claim is released when the ledger dies (last holding retired, including a holding that won nothing or could not prove its name), and cleared on a bind.
- The name-table capacity check now covers IPO registrations, so Execute can no longer fail after an irreversible bid. The static assert bound is tightened.
- `GetSource_output` gains `claimedName` appended at the end.
- A later registrant whose different name is already claimed for another index gets `CONTRACT_MISMATCH (28)`, as before.

## Known trade-off (decision for the owner)
The first registrant's name decides the name of the whole unbound IPO ledger. A first bidder that declares a wrong or junk name blocks every other pool from registering an IPO holding on that target under a different name until the ledger dies (`SOURCE_CONFLICT` at Propose; nothing is at risk). Attacker cost is one pool (10,000,000 QU, reusable) plus a refundable bid. It stops capital being frozen and names being poisoned for ever. The planned K8 (name read from the chain, no declared name) removes it.

## State and ABI against `QPoolK6.h`
- State layout differs: `Offer` is 8 bytes smaller (-262,144 bytes in total), `SourceLedger` is 8 bytes larger (+131,072 bytes in total). Net about -131,072 bytes, derived from the two separate measurements the agents made, not measured on the merged file. A fresh deployment is needed; there is no state migration.
- `GetSource_output` is 8 bytes longer (appended). No other input or output layout changed against K6.
- No new return codes. `SOURCE_CONFLICT (23)` is now also returned for the IPO-name case.

## What was checked, and what was not
Checked (ran): the merged production file passes `check_contract.sh` (compile, link, smoke run of INITIALIZE plus the 9 read-only functions under Rosetta, Apple clang), `lint_contract.py`, and `release_gate.py`. The two source changes merged with `git merge-file` with zero conflicts.
Not done: the devkit file itself was not compiled by the harness (the harness builds the production names); no procedure was run on the merge; the implementers report their own scratch tests on their separate copies; no independent review of the merge yet. Static and compile-level only.

## Not in this build
- `CancelQxBid` (being built; the final K7 will have it).
- K4b (a holding that wins seats under an unprovable name still cannot settle).
- K8: name read from chain, and settlement at epoch begin.
- Extending the built-in name table beyond index 30.

## Suggested things for the testing agent to try first
1. Propose with an invalid input from a member with no live proposal: still refused, and cheap.
2. Two pools bid on the same IPO target under different names: the second Propose returns 23. Zero-win IPO, then settle: the name is free again for another index.
3. Honest second registrant after a junk first registrant: refused with 23 (the trade-off above).
4. `BuyPosition` after a sale changed the holding's shares: refused with 38 using the old `offeredShares`, accepted with the live value from `GetPosition`.
5. A BUY_IPO whose price was taken by another pool: proposal FAILED and votes released.
6. END_EPOCH with the QPAYHUB stub present: the 30 percent leg arrives; with no contract at the index: it goes to the burn leg.
