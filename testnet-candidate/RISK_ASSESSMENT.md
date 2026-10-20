# QPOOL risk assessment

Subject: the `testnet-candidate` build (`QPool.h` in this folder: the verified K6 build plus the untested revenue, hook and
QPAY buy-and-burn delta). Date: 2026-10-19. Status: pre-mainnet. Author: the same party that wrote the fixes, so this is a
self-assessment; section 11 asks for an independent one before any mainnet proposal.

**Bottom line.** QPOOL is a custodian: it holds members' QU, pools' assets and IPO stakes under one contract address, and its
code alone decides who may take what out. The audit found 58 confirmed defects, of which the ones that let one party take another
party's money (criticals C001, C002, C003, and the IPO attribution highs) are fixed and independently verified in simulation. The
build is NOT ready for mainnet: three audit clusters are unfixed (one of them a liveness freeze of all exits), the newest
revenue and QPAY code has never been independently verified, and nothing has been exercised on a real node beyond one short
testnet run. The honest risk level today is HIGH for a mainnet launch and ACCEPTABLE for a testnet.

## 1. What is at stake

| Asset held by the contract | How it can be lost |
|---|---|
| Members' uncommitted QU (withdrawable at any time) | a bug that pays it out to the wrong party; a freeze that stops `WithdrawUncommitted` |
| Capital committed to holdings (cost basis) | mis-attributed sales or settlements; a sale at a manipulated price; a holding that can never settle |
| IPO stakes in flight (at-risk money in the core's book plus the shared refund pot) | one pool drawing another's refund; a stale snapshot; a failed settlement path |
| Shares and tokens under QPOOL's or a venue's management | a release to the wrong owner; shares stranded under a venue; a burn of the wrong asset |
| Accrued dividends not yet claimed | mis-split; parked for ever by an unsettled holding of the same ledger |
| Revenue in transit (pending split, QPAY pot) | paid to the wrong leg; a QPAYHUB index that points at the wrong contract; a pot swept by mistake |
| The execution fee reserve | drained by free calls or per-tick re-hashing; at zero every user call is refused by the node and members cannot exit |

Only the contract's code can move any of this. There is no admin key, no pause and no upgrade path: Qubic contracts cannot be
patched in place, and no RESET or MIGRATE authority for QPOOL is on record (audit C116).

## 2. Assurance status: what has been checked, by what

| Area | Audit | Fix landed | Simulated verification | Real node |
|---|---|---|---|---|
| Roster, join fee, membership (K1) | yes | yes | independent verifier, mutants killed | basic suite A on testnet: pass |
| IPO bid size limits, price registry (K2) | yes | yes | yes | not yet (outsider bids never registered: 16-byte payload bug in the test tool, now fixed) |
| Vote binding, seat locks, windows (K3) | yes | yes | yes | governance cycle on testnet: pass |
| IPO settlement from the END_EPOCH snapshot (K4) | yes | yes | yes | one single-pool IPO on testnet: snapshot before `finishIPOs` confirmed |
| Money-governed bidding, rebids funded by returned money (KI, KI2) | n/a (owner redesign) | yes | yes, 220 of 220, 11 mutants killed | NO: no outbid or rebid has run on a node |
| Simultaneous IPOs (KX) | partly | yes | yes, with a third IPO contract in the harness | NO |
| Quotas, creator-only deposits, fees 10M / 10k, IPO holdings cap 2,048 (K5) | yes | yes | yes (one repair round) | partly |
| Seat market, offer binding, failed-transfer handling (K6) | yes | yes | yes, 263 of 263 | suite H on testnet: pass |
| Venue safety: pool-paid fees, SELL planning, QX bid cancel, AMM slippage (K7) | yes | NO | no | no |
| Dividend ingestion, in-kind and contract-id payers, stateless hook (K8) | yes | PARTLY (lean) | no | one dividend on testnet: pass |
| Fee-reserve hardening, state size, keeper fees (K9) | yes | NO | no | no |
| IPO wrong-name recovery (K4b) | found by K4 | NO | no | no |
| Revenue split 40/30/20/10, no threshold, leftovers burned (lean) | no | yes | NO, compile only | one epoch on testnet: arithmetic exact |
| Unattributed money to revenue (lean hook change) | no | yes | NO | one stray transfer on testnet: pass |
| `BuyAndBurnQpay` (lean) | no | yes | NO | one happy path on testnet (5,000 tokens at one level): pass |
| Port to the current upstream core (KP) | n/a | NO: harness is a July core | no | the testnet devkit is its own core |

"Simulated" means the x86 Rosetta port of a July 2026 core with gtest, never MSVC and never a production node.

## 3. Risk register (ranked)

Likelihood and impact are judged for a mainnet launch of this build as it stands. "Open" means nothing in the code addresses it.

| # | Risk | Likelihood | Impact | Status | Evidence |
|---|---|---|---|---|---|
| R1 | Fee reserve drained to zero by free or refunded calls and per-tick state re-hashing; at reserve <= 0 the node refuses every user call, so NO member can withdraw or claim until a third party burns QU into the reserve | High | Total freeze of exits (not loss) | Open (K9 not landed; audit C008, C021, C024, C104; KI2 verifier's rebid dust loop) | measured: 676-slot rebid pass free to any seat owner; 1 QU stray per tick writes state |
| R2 | Whole-state digest billing switched on by the core team: a 190 MB state re-hashed every dirty tick would exhaust the reserve in hours (same freeze as R1) | Medium (toggled three times in 2026, off today) | Freeze | Open (audit C098) | upstream `getComputerDigest` has the charge commented out with a TODO |
| R3 | The new revenue and QPAY code has an arithmetic or control-flow bug that misroutes revenue (wrong leg, double count, pot swept) | Medium | Loss of revenue, not of member money (the pot and revenue are separated from member funds in the books) | Untested except one testnet epoch | compile-only delta; `END_EPOCH` clamps to balance minus pot |
| R4 | `BuyAndBurnQpay` is manipulated: a lone ask just under 10,000 takes the whole pot (owner accepted), or a bid rests on QX after a partial fill the cancel could not remove | Medium | Loss of up to one epoch's pot per call | Accepted / partly mitigated | ceiling only; remainder cancel is best effort, stranded amount is booked as spent |
| R5 | QPAYHUB index wrong on mainnet (29 assumed; the mainnet table changes) or QPAYHUB rejects plain transfers: 30 percent of revenue sent to the wrong address every epoch | Medium | Loss of 30 percent of revenue, unrecoverable | Open: a constant, no run-time check | confirm against the mainnet contract list on proposal day; the contract's own index must be >= 32 |
| R6 | A resting BUY_QX bid can never be cancelled: capital stays on QX and the payer is blocked from other QX buys of that asset | High (happens with any unfilled limit order) | Capital tied up indefinitely, not lost | Open (K7; audit C010) | documented; owner has not decided IOC vs cancel |
| R7 | SELL_QSWAP's flat 100,000 fee and QX hand-over fees are fronted from the pooled balance and netted from proceeds; with no unowned float a sale could fail or dip into the surplus sweep | Medium | Small per-sale loss or refused sales | Open (K7, owner decision D22 not built) | lean build removed the float; path not re-verified |
| R8 | AMM sandwich between Propose and Execute on QSWAP; only the vote-time min-out protects | High for large swaps | Bounded by the voted min-out | Open (K7; audit C034) | no stored quote, no per-execute size cap |
| R9 | An IPO holding that declared a wrong share name stays unsettled for ever and parks every pool's dividends on that ledger; freezes seat trades and sells of the other holdings of that contract | Low per IPO, high over time (one typo) | Indefinite freeze of that ledger's dividends and trades | Open (K4b) | reproduced in simulation |
| R10 | Real-node behaviour of the IPO path differs from the harness: refund delivery inside `bidInIPO`, snapshot timing with several IPOs, settle order | Medium | Mis-attribution of IPO refunds between pools (real loss) | Verified in source and harness only | the testnet has not yet run an outbid or a rebid |
| R11 | Contract-id payers (contracts that pay income by `qpi.transfer` from their own id) are not matched to pools; by owner decision the money becomes QPOOL revenue, so the pools that hold those contracts' shares earn nothing from them | Certain for those payers | Members of such pools lose that income to QPOOL's shareholders | Accepted by design, must be documented to users | audit C096, C097, C099 |
| R12 | Dividend from a contract that QPOOL holds shares of but whose ledger is squatted or bound to the wrong asset name | Low | Dividends parked or mis-credited | Partly mitigated (K5 squat eviction, K6 name checks) | |
| R13 | Capacity exhaustion: 1,024 pools, 2,048 unsettled IPO holdings, 8 IPO targets, 16 live prices per holding, 32 offers per pool | Low with the fees (10.24 billion QU to fill the pool table) | Denial of service to newcomers, no loss | Mitigated by quotas (K5) | per-pool quotas; roster flood of an open pool costs 2.56 million QU |
| R14 | Governance capture: a 50 percent quorum of uncommitted money decides buys and sells; a whale member can pass any proposal in a small pool | Inherent | Members of that pool follow the whale's trades | Design; mitigated by yes-voters-only charging | abstainers and no-voters are never charged |
| R15 | Window timing: windows count ticks and assume one tick per second; a 24-hour IPO vote is 86,400 ticks whatever the real tick rate | Certain | Votes shorter or longer than advertised | Open (documentation) | mainnet tick rate observed around 0.4 to 0.6 s |
| R16 | Rounding dust: up to 1 QU per claim stays in the contract untracked; never paid, never swept | Certain | Negligible loss per user; small untracked balance | Accepted by owner | |
| R17 | No upgrade path: any bug found after launch needs a new contract and a migration of every member's funds | Certain | Any post-launch defect is permanent for the lifetime of the deployed contract | Inherent to Qubic | plan the launch as an IPO-gated, limit-capped rollout |
| R18 | The simulated harness differs from the production node (Rosetta x86, July core, inline hook emulation) and the testnet devkit differs from mainnet (index layout, no QPAYHUB) | Certain | Undetected platform-specific defects | Open (KP not done) | |

## 4. Open audit findings by severity

Confirmed in the audit and NOT fixed in this build (cluster in brackets):
- High: C008 reserve drain and exit freeze [K9]; C096 contract-paid income unmatched [K8, accepted as revenue].
- Medium: C010 resting bid cancel, C034 AMM slippage, C101 ledger-wide escrow freeze [K7]; C097, C099, C030, C021 [K8: C030 and part of C021 done lean, C021's per-arrival state write remains]; C009 Propose scans 262,144 slots before cheap validation, C104 attacker-controlled probe length [K9].
- Low: C035, C036, C102 [K7]; C029 [K8]; C023, C024, C026, C027 [K9].
- Unverified (60 reviewer-only findings, Appendix A of the audit): untouched except where a landed cluster happened to cover them.

Fixed and verified in simulation: every critical (C001, C002, C003), the IPO attribution highs (C004, C005, C012, C037), vote binding (C006), capacity floods (C007, C013), and the medium and low findings of K0 to K6.

## 5. Platform and dependency risks

- The contract's own index: the local core wires it at 30; mainnet would be 32 or later (QPAYHUB 29, QTREAT 30, QLOAN 31 on upstream main). The contract reads its own index at run time, but every embedded contract index (QX 1, QSWAP 13, QPAYHUB 29) is a constant to re-verify on proposal day.
- QX fee 0.3 percent and QSWAP flat fee 100,000 are hard-coded mirrors; a change by those contracts fails QPOOL's sales closed (refused), not open.
- `END_EPOCH` before `finishIPOs` and the in-call refund delivery are verified in the upstream source as of 2026-10-14; a core change there would break IPO settlement silently.
- QPAYHUB's dividend pool semantics (plain transfers booked, 1,000,000 QU reserve never distributed) are read from upstream source, not from a deployed contract.
- The core's 16-bit IPO quantity and 676 limit are enforced by QPOOL before every bid (C001 fixed).

## 6. Operational risks

- Keeper duty: `Tally`, `Execute`, `SettleIpo`, `ReconcileHolding` and `BuyAndBurnQpay` must be called by someone. Nothing happens by itself except `END_EPOCH`. Without a keeper, passed proposals expire and IPO holdings stay unsettled.
- Reserve monitoring: there is no in-contract reserve function; watch it externally (qfront, qubic.li, qubic-cli) and have a runbook to burn QU into it (QUtil `BurnQubicForContract`). The self-refill is only the 10 percent burn leg plus leftovers.
- Creator powers: admission and the public/private switch only; the creator cannot touch member funds. A creator who stops admitting leaves a private pool frozen for newcomers only.
- State size 190 MB: node CPU per dirty tick and the single largest cost driver if digest billing returns; the launch profile (smaller tables) is an owner decision not yet taken.
- Tooling: the wallet-side IPO bid payload must be exactly 16 bytes; a 10-byte payload is silently a plain transfer (revenue by the new hook). Document for every integrator.

## 7. Worst-case loss scenarios, as the build stands

1. Reserve freeze (R1, R2): all exits blocked; funds intact but inaccessible until someone burns QU into the reserve; repeatable by any seat owner at near-zero cost. This is the most likely serious event.
2. Revenue leak (R3, R5): up to 30 percent of every epoch's revenue to a wrong address, plus a whole pot per bad `BuyAndBurnQpay` call (R4). Member funds untouched.
3. IPO mis-attribution on a real node (R10): a pool draws another pool's refund; bounded by the refund pot; not reproduced anywhere, but not excluded until the testnet has run competitive IPOs.
4. Ledger freeze by a wrong share name (R9): all dividends and trades of that contract's holdings frozen for every pool.
5. Capital tied up on QX (R6): not a loss, but members cannot get the money back without a vote mechanism that does not exist.

## 8. What reduces the risk to launch level

Must-do before any mainnet proposal, in order:
1. Land K9 (reserve hardening): keeper fees burned on permissionless calls, a stake floor on Propose, O(1) refusals, reserve-aware END_EPOCH, a reserve read-out. This removes the only "all exits frozen" path that an attacker can trigger cheaply.
2. Land K7 (venue safety) including the owner's decisions on resting QX bids and pool-paid fees, and the AMM slippage cap.
3. Close the stray-transfer state write (owner decision D23) with the exact-obligations sweep, verified.
4. Independent verification of the revenue, hook and `BuyAndBurnQpay` delta: a second reader of the diff plus the simulated suite updated for the new split, or the full testnet suites I and J.
5. Land K4b (wrong-name recovery) or document the freeze as accepted and add a name whitelist for IPO targets.
6. Run the competitive IPO battery and the realistic season test on the testnet, including at least one failed IPO and one two-IPO epoch, and fix from the results.
7. Port the harness to the current core (KP), add QLOAN to the payer table, and re-run everything; build with `-fsanitize=unsigned-integer-overflow`.
8. Obtain the core team's written position on digest billing and on RESET/MIGRATE authority; size the state for billing ON.
9. Independent third-party review of the final build. This document and the audit are self-assessments by the author of the fixes.
10. Staged launch: cap the number of pools and the per-pool funds in the first epochs (constants), announce the keeper and reserve runbook, and publish the known limitations (R11, R15, R16) to users before they deposit.

## 9. Residual risk after that list

Even with every item done: no upgrade path (R17); governance capture inside a pool (R14); accepted design choices (contract-id payers to revenue R11, dust R16, lone-ask pot capture R4, nominal tick windows R15); and the gap between any test environment and mainnet (R18). These should be stated to users in plain language.

## 10. Risk acceptance record

Decisions taken by the owner that this assessment treats as accepted, not open: no ceiling on IPO bids (budget is the only limit); rebids funded only by returned money; no minimum payout; leftovers burned; unattributed money becomes revenue (never returned); QPAY bought with no spend limits under a 10,000 ceiling and burned to the zero key; creator-only deposits; fees 10,000,000 and 10,000; 2,048 unsettled IPO holdings.

## 11. Independence note

The audit (13 reviewers, 99 agents) was independent of the fix authors in the sense that different agents reviewed and verified, but all of it was run by the same operator and the same model family, and the lean delta had no verifier at all. For a contract that will hold third-party money, commission a human security review of the final build before the proposal, and give the reviewers this document, the audit report and the test guides as the starting point.
