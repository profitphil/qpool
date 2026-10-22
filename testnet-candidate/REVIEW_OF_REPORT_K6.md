# Review of the security report on QPoolK6.h

Date of review: 2026-10-21. Method: static reading only. Nothing was compiled, run or modified.
Contract under review: `QPoolK6.h` (6,345 lines, SHA-256 c3b5dd4c...9093 per the report). All line numbers below are lines of `QPoolK6.h` unless another file is named. "Candidate" means `candidate_QPool.h`, the earlier testnet build already on GitHub, which the report authors did not scan.

How the verdicts were produced: for each of the report's findings an independent prover tried to confirm it and an independent refuter tried to knock it down. A separate delta review checked the testnet session's two fixes, a status check covered the owner's nine earlier findings, two sweeps looked for issues the report missed, and a verifier then tried to refute each new candidate. Where they disagree, this document says so and says which side the evidence supports.

## Bottom line

- The report is mechanically accurate (every cited line range lands on the code it describes) but it inflates severity on three of its seven findings, and its two "high" findings about the devkit build and about payer-index squatting are respectively not a contract flaw and a medium.
- Five of the seven findings are real contract-logic flaws on the latest file: R2 (squatting), R3 (IPO name registry), R4 (resting QX bids), R6 (proposal scan) and, weakly, R7 (vote snapshot). R1 is release hygiene for a devkit build. R5 is the direct composition of two owner decisions that were accepted by design.
- The most important result is one the report only half saw. The testnet session's F-01 fix (the declared IPO share name is claimed at the first bid) closes the narrow race it targeted but introduces a permanent name-squat and still leaves a way to freeze an honest pool's IPO capital. That makes the latest file worse than the candidate on the IPO name registry. Both problems are confirmed by independent verifiers, with severity High because the contract is immutable.
- No reviewer found a path to steal member funds, double-credit a payout or break the money arithmetic. That agrees with what the report itself says it found in its "no issue" rows. The real exposures are permanent lock-outs, frozen capital and availability.
- The three devkit constants must never leave the devkit. The most value-extracting leftover is the QPAY issuer set to a test wallet.

---

## 1. What the report is and how reliable it is

**What it is.** An automated static scan of one file, `QPoolK6.h`, run in "repository / directory snapshot" mode. It lists 14 entries, which are 7 distinct findings, most of them listed twice (the duplicates are [1]/[4], [2]/[3], [5]/[6], [7]/[12], [8]/[9], [10]/[14], [11]/[13]). The summary calls the result "three high and four medium". For the second entry of each pair, the report itself says validation details "were not recorded separately", so the duplicates add no evidence.

**Scope and limits the report states about itself.**
- Only `QPoolK6.h` was supplied. External QPI, QX, QSWAP, the IPO core, deployment manifests and runtime fee behaviour were excluded and could not be verified.
- No runtime reproduction was possible.
- One investigator hit an account usage limit and its packets were completed by the parent; the scan model's access status was reported inconsistently.
- It labels as assumptions that the file "may be a test/devnet build" and that deployment intent is unknown.
- Its "no issue found" rows (custody, double credit, arithmetic wrap, reentrancy, embedded keys) agree with this review's sweeps.

**Reliability, from this review.**
- Line citations and quoted code check out, with small drift (a few off-by-one ranges). Several code blocks are abridged or stitched together without saying so: the R1 quotes drop the "TESTNET ONLY" comments, the R4 procedure list prints 14 of 21 entries, and the R5 "policy" block is a paraphrase.
- The scan did not know about the owner's own register. Five of the seven findings are already tracked: R1 (owner R5, R15, R18), R2 (README 4d, R12), R3 wedge half (R9, K4b), R4 (R6, audit C010, K7), R6 (audit C009, K9). R5 is an accepted design decision. Only the R3 name-poisoning half (new in K6) and the R7 refund-stamp vector are new to the owner.
- Severity is inflated in R1, R2, R5 and R7, and partly in R3 (it overstates one variant and misses a worse one). The report's own downgrade conditions are met in R1 (labels and release controls) and R5 (seats cannot be bought until settlement), yet it keeps the high or medium rating.
- Remediation advice is often not implementable on this platform: there is no network-id check, no authoritative index-to-name lookup in QPI, no QX order id, and a "ceiling price" fix contradicts decision D1.
- It did not scan the candidate, so it says nothing about which findings were introduced by the testnet fixes. This review adds that.

---

## 2. Verdict table

Severity scale used here: High = permanent loss of function or frozen capital in an immutable contract; Medium = real, narrow or recoverable harm; Low = griefing or availability nuisance; Info = documentation or latent only.

| ID | Finding (report entries) | Report severity / confidence | My verdict | My severity | Testnet candidate | Latest K6 | Prover vs refuter |
|---|---|---|---|---|---|---|---|
| R1 | Devkit constants collapse windows and redirect value ([1],[4]) | High / medium | Devkit-only release hazard, not a logic flaw. One shared logic residue (no liveness probe on the QPAYHUB leg) | Info as labelled; Low for the residue | No (candidate has production values); residue yes | Yes, if shipped | Both NOT_A_CONTRACT_FLAW. Severity Low (prover) vs Info (refuter) |
| R2 | Unverified payer indices allow cross-pool ledger squatting ([2],[3]) | High / high | True positive, narrower than stated | Medium | Yes | Yes | Both CONFIRMED, both Medium |
| R3 | Arbitrary IPO names wedge settlement and poison the registry ([5],[6]) | High / high | True positive, but two halves with different origin (see section 3) | High on K6; Medium on candidate | Partly: wedge yes, zero-win poison no | Yes | Both CONFIRMED. Severity High (prover) vs Medium (refuter). Candidate yes (prover) vs no (refuter), each right for a different half |
| R4 | Uncancellable resting QX bids ([7],[12]) | Medium / high | True positive | Medium | Yes | Yes | Both CONFIRMED, both Medium |
| R5 | Any one-unit owner directs the IPO rebid budget ([8],[9]) | Medium / high | Mechanics true; it is the composition of two accepted design decisions | Low (accepted design) | Yes (identical code) | Yes | Prover PARTIALLY_CONFIRMED, Medium; refuter NOT_A_CONTRACT_FLAW, Low |
| R6 | Proposal attempts scan 262,144 slots ([10],[14]) | Medium / medium | True positive | Medium | Yes | Yes | Both CONFIRMED, both Medium |
| R7 | BUY vote snapshots can be pushed to unreachable quorum ([11],[13]) | Medium / high | Partly: one real stranger-triggered vector, the rest is ordinary abstention | Low | Yes | Yes | Both PARTIALLY_CONFIRMED, both Low |

---

## 3. Per finding

### R1. Devkit constants (report: High)

**Confirmed trace.** K6 differs from the candidate in exactly three deployment values, each labelled test-only in the source.
- `QPOOLK6_TICKS_PER_HOUR = 2` (302; candidate 3600). Windows become 144 (vote), 96 (execute), 48 (IPO vote) ticks, lifetimes 240 and 144 (302-319). Locks and expiry derive from the same constants (3314-3320, 3419), so the system stays consistent; only reaction time shrinks. Quorum (50 percent of eligible weight) and `yes > no` do not depend on tick length (3367-3383).
- `QPOOLK6_QPAYHUB_CONTRACT_INDEX = 50` (390; candidate 29). END_EPOCH sends the 30 percent leg to `id(50,0,0,0)` and books it as deposited if the transfer returns >= 0 (6288-6293). Core `__transfer` does not check that the destination is a contract, so if no contract sits at 50 the QU goes to an unspendable key and the books record success. Example: one pool-creation fee of 10,000,000 QU strands 3,000,000 QU.
- QPAY issuer in `BuyAndBurnQpay` is a test wallet (5800-5808; candidate has the real QPAYNOWS... issuer). On a network where nobody issued that token, the call returns NOTHING_TO_BUY and the pot stays locked, because the only writers of `qpayBuyPot` are 5962 and 6299. The only party who can turn the pot into a payout is the holder of the test wallet's key: they issue a worthless "QPAY" (QX issuance fee 1,000,000,000 QU at initialisation), list an ask at or below the 10,000 QU ceiling, and call `BuyAndBurnQpay`.

**Mitigations the report ignored.** Every substitution is labelled in the code (302, 383-390, 5794-5799). A production-shaped build exists (the candidate). The struct is named `QPOOLK6`, which does not match the core's `QPOOL` wiring (core `contract_def.h` 318-321, 440), so it cannot be dropped in unchanged. `BuyAndBurnQpay` fails closed. Member funds, pool holdings and IPO stakes are separate bookkeeping and are not touched. The owner already tracks the class (R5, R15, R18).

**Errors and overstatements in the report.**
- "Buys and burns the wrong token" is wrong as written: it buys nothing unless the test-key holder lists the token.
- "Redirects value" is imprecise: the 30 percent leg is lost to an ownerless key and the books hide it; no beneficiary is named. The "trader or unintended recipient" attacker is invented.
- "Collapses governance windows" overstates: quorum and majority are unchanged.
- The remediation "fail initialization on network-ID mismatch" cannot be built: QPI has no network-id API and there is no TESTNET macro. INITIALIZE is empty (6014-6018), so the "no source-local guard" claim is accurate.
- It missed stale header comments (91, 127, 176, 312 still describe 86,400-tick windows) and the wrong threshold comment at 381-382.

**Fix.** Make production values the compile default and put the three test values behind an explicit opt-in macro. Add a release gate that fails if the proposed file contains `TESTNET ONLY`, `TEST-ONLY`, `QPOOLK6_`, `RDOIGJSE` or an index-50 constant, and require a diff against the audited candidate to show only the intended fixes. Separately, add a run-time existence probe on the QPAYHUB leg so a wrong or absent index falls into the burn leg instead of being booked as deposited: `qpi.isContractId(id(index,0,0,0))` (public QPI, used in QMail.h:462) or `qpi.ipoBidPrice(index,0) != -1`. Either proves only that some contract exists there, not that it is QPAYHUB, so the proposal-day check of the index table stays.

**Regression test.** (a) Per build profile, assert the constants (3600/259,200/172,800/86,400/432,000/29 and the real issuer for production; 144/96/48/240/50 for devkit) and run the grep gate. (b) With `contractCount <= 50`, create one pool and run END_EPOCH: today the energy of `id(50,0,0,0)` is 3,000,000 and `totalQpayhubDeposited` is 3,000,000; after the probe fix both are 0 and the burn leg is 4,000,108. (c) With no asks for the test issuer, `BuyAndBurnQpay` returns NOTHING_TO_BUY and the pot is unchanged.

**Disagreement.** Prover Low, refuter Info. I side with Info for the file as labelled and Low only for the no-probe residue, which exists identically in the candidate.

### R2. Payer-index squatting (report: High)

**Confirmed trace.** For indices above the 30-entry table (31..1023) the pair (index, share name) cannot be verified on chain.
1. CreatePool (fee 10,000,000 QU, 332) and Contribute at least 20,000 QU.
2. Propose BUY_QX with `holdingId = v` (a registered index, not in its IPO epoch), asset `{NULL_ID, N}` where N is an issued name not in the table and not yet mapped, price 1, shares 1, quAmount 1. Validation passes (2985-2992; `ContractPairKnown` 1641-1650; `ipoBidPrice == -2` at 1677-1678).
3. Vote, Tally, Execute. The 1 QU bid rests on QX; `RegisterHolding` binds `sources[id(v)] = N` and `contractNames[N] = v` (1859-1866, 1911-1917).
4. Result: every honest holding of v under its real name fails `SOURCE_CONFLICT` (1722-1725); every holding of N at its real index fails `CONTRACT_MISMATCH` (1647-1650); a BUY_IPO into v fails at 3073.

**Why it is permanent.** `EvictSquat` refuses purchase-created holdings (5334; comment at 5327-5328). There is no procedure that cancels an unfilled BUY_QX bid. A pending-only holding (shares 0) cannot be sold, and a dust share that is filled can only leave by a `SELL_QX` that clears 200 QU of fixed costs. QX has no order expiry. So the squat can be unreleasable even by its creator. The deposit-eviction path works only while the target contract is in its IPO epoch right now (5373-5376).

**Mitigations the report ignored.** The index must be below `contractCount` and not mid-IPO. Table indices 1..30 are exact matches. One name maps to one index, so a decoy squats one index. An honest first registration of a pair protects it. The attacker's sunk cost is about 10.02M QU. The code, README 4d and R12 already document the limit.

**Errors and overstatements.** "Dividend attribution" loss is wrong: the squatter's ledger earns nothing and an honest ledger cannot be displaced. Severity High and likelihood high are overstated. It omitted the name-side victim and that QPOOL's own share name can serve as the decoy. It understated permanence and the limit of the eviction path (above). The remediation "authoritative bidirectional proof" cannot be built: QPI exposes no index-to-name lookup. The report's open question about QX expiry is answerable: QX has no expiry.

**Fix.** Layered: (1) extend the embedded table to every contract in the target network's node build at freeze (31..contractCount-1), which removes the surface for existing contracts; (2) add a permissionless, lossless release for a purchase binding that never filled and never paid, by cancelling the resting bid (`RemoveFromBidOrder`, the call pattern already used at 5896) and returning the refund; (3) operational: pre-bind live contracts at launch, because the first honest claim protects the pair.

**Regression test.** Create pool A, bid 1 QU on N at `holdingId = v`; assert `sources`/`contractNames` carry the false pair; from pool B assert the genuine pair (v, R) and (index of N, N) are refused (today) and accepted or evictable (after fix); assert `EvictSquat` returns NOT_FOUND today. Negative controls: index 29/30 with a wrong name, index >= contractCount, mid-IPO index and a second index for the same decoy name must stay refused.

### R3. IPO name registry (report: High)

This finding combines three mechanisms with different origins. The report found the first two.

**V1, zero-win name poison (K6 only, introduced by the F-01 fix).**
1. Open a one-member pool (10,000,000 QU, reusable).
2. Propose BUY_IPO at any live above-table target X declaring the announced, still-unissued share name N of a different future contract Y (checks at 1674 and 3069-3073 only test that N is unissued and unmapped).
3. Execute with the target in its IPO epoch (3470): the bid goes in, `RegisterHolding` creates the X ledger with `assetName = 0` (1865) and writes `contractNames[N] = X` at registration (1911-1917).
4. The holding wins no seats. `SettleIpo` retires it (5082), `ApplyHolding` deletes the ledger, but the only removal of a name is keyed to `led.assetName != 0` (2052-2056), which is 0 for a never-bound IPO ledger. N stays mapped to X for ever.
5. Every later QPOOL registration of (Y, N) fails `CONTRACT_MISMATCH` (1647) in Propose, Execute, deposit and SettleIpo. No admin can clear it, and `EvictSquat` refuses IPO holdings (5334). The candidate does not leak this way: it claims an IPO name only at the first proven winning settlement (candidate 1883-1892, 2035-2041).

**V2, wedge (both builds).** A holding that wins at least one seat under a name QPOOL cannot prove returns `NAME_UNPROVEN` (4913-4916) before any write. It never settles. The ledger's dividends are parked for every pool, `SELL_QX` and `BuyPosition` on it are refused, and one of the 8 IpoTarget rows stays pinned (5063-5071 unreachable). This is the owner's R9 (K4b not landed). The attacker's own seat capital is stuck too.

**V3, honest-capital freeze (K6 sequence; not described in the report).** Only the first registrant of a ledger claims a name (gate at 1911); later registrants on the same unbound ledger are compared with nothing (1722, 1875).
1. Attacker pool P1 is first on target X under a junk name W (claims W to X).
2. Honest pool H registers on X under the true name N. It passes, and claims nothing.
3. P2 registers on a second concurrently live target Y under N: new ledger, N unmapped and unissued, so it claims N to Y.
4. After the epoch `SettleIpo(H)` finds N mapped to Y, `proven = 0`, returns `NAME_UNPROVEN` for ever. H's committed capital is frozen and X's dividends stay parked.
A variant with no second IPO is possible after N is issued, by claiming N through a BUY_QX at a live registered index before `SettleIpo(H)` runs. The N2 verifier confirmed the trace at high confidence; whether concurrent IPOs occur on mainnet cannot be determined statically. In the candidate the end state is reachable through the original F-01 race (a later settlement), with no first-registrant step.

**Mitigations the report ignored.** Bids need a live IPO (3470). The claim is written only for a new ledger, so an honest first registrant protects its name. CheckSource refuses already-issued names and table-listed names. The zero-win path returns the whole bid budget; the real cost is the 10,000,000 QU pool fee, which the report never states. `SettleIpo` refuses rather than misattribute shares, so no theft results. The owner knows the wedge (R9, K4b).

**Errors and overstatements.** "Most of the bid budget" returns is wrong: all of it returns. "Cheap exhaustion of all eight IPO-target rows" is not supported: only a winning false-name seat pins a row and each seat costs its clearing price in a distinct IPO. The report does not say the poison half comes from the F-01 fix. Its remedy "do not write a global claim until proven" would reopen the cross-target race F-01 was meant to close.

**Fix.** Keep the claim at registration, store the claimed name in the ledger (`SourceLedger.claimedName`), and (a) refuse any later IPO registrant on an unbound ledger that declares a different name than the claim (SOURCE_CONFLICT), (b) release the claim on every terminal path of an unbound ledger: at ledger deletion for both `assetName` and `claimedName`, and when a bind sets a different name. A smaller variant proposed by a verifier is to set `led.assetName = declared name` at creation (1865) for every ledger; it makes the existing release work but changes the bind branch at 1976 and the SettleIpo comparison at 4898-4911, so it needs an owner check and has not been tested. What neither removes: a first registrant with a junk name who also wins seats still wedges his own target. Closing that needs the K4b write-off (after the epoch has moved on, settle an unprovable winner as lost, refund, retire, release the claim). Also widen the CheckSource name-table capacity check to IPO registrations (see section 6).

**Regression tests.** (1) Zero-win release: bid on X under unissued name F, let the IPO fail, settle; assert the ledger is gone, `contractNames` has no entry for F, and a BUY_IPO at another index under F is accepted (today: `CONTRACT_MISMATCH` 28). Keep a test that two live targets under the same unissued name are still refused while the first ledger lives, so F-01 stays closed. (2) Second registrant on an unbound ledger declaring a different name returns SOURCE_CONFLICT (today OK). (3) End-to-end V3: assert H's holding settles after the fix. (4) Document V2: a winning wrong-name holding returns NAME_UNPROVEN (34) until K4b lands.

**Disagreement.** The prover rates High and says the candidate is affected; the refuter rates Medium and says it is not. Both are right about different halves. The evidence supports High for K6 because of permanence plus V3, with the pure name-poison alone being Medium (no fund loss). The candidate is affected by the wedge (V2) and the original race, not by the zero-win poison.

### R4. Uncancellable resting QX bids (report: Medium)

**Confirmed trace.** A BUY_QX that does not fill at once leaves its remainder on QX and books it as `Holding.pendingShares` (3766-3767), added to the shared ledger's `pendingQx` (1892). `CheckSource` then refuses every later BUY_QX on that ledger with `QX_BID_PENDING` (1749-1754), at Propose (3000-3005) and Execute (3560-3565). `pendingQx` falls only through `ReconcileHolding` (4452), which needs shares to arrive. None of the 21 registered procedures (5990-6011) cancels a pool bid; the single `RemoveFromBidOrder` call (5896) is inside `BuyAndBurnQpay` and touches only its own residue. QX has no expiry and no order ids.
- Attacker: pay the 10M QU pool fee plus about 20,000 QU of join fee and minimum contribution; propose price 1, shares 1, quAmount 1; pass it alone; the 1 QU bid rests; every other pool's BUY_QX on that ledger is blocked. Cost to the attacker can be as low as 1 QU of stranded capital.
- No attacker is needed: any pool that votes a limit price below the market leaves the same state.

**Mitigations the report ignored.** The block is per source ledger (one issuer's single token, or one contract index), and only BUY_QX is refused; BUY_QSWAP, sells, deposits and payouts continue. For contract shares QX is the only buy venue, so there the block is complete. Victim funds are never locked: refusal comes before anything moves. Only the lock-holding pool's own YES voters are tied up. Members can still sell their seat through `OfferPosition`/`BuyPosition`. An unfilled bid causes no dividend escrow freeze (6090-6107).

**Errors and overstatements.** "Payer-global" is loose (one asset ledger). "Tiny resting order" understates the entry cost. "Lock funds" should read "lock the proposing pool's own voters' funds". The procedure listing in the report prints 14 of 21 entries; the conclusion holds. The suggested "external order identifier" is not possible: QX cancels by (issuer, asset, price, shares). The report did not note the owner already lists this (R6, K7).

**Fix.** Make BUY_QX immediate-or-cancel, as SELL_QX (4278-4292) and `BuyAndBurnQpay` (5889-5906) already behave: after computing `filled` at Execute 3760-3765, cancel `added - filled` with `RemoveFromBidOrder(price, rest)`, move the `BuyRefund` call after the cancel so the refund is taken back (the hook parks it in `globalUnattributed`, 6165-6169), and set `pendingShares = rest - removed` (normally 0). A zero-fill buy then ends in the existing "refund >= assigned" branch (3823-3829) with nothing recorded. About 20 lines. It removes passive limit orders, so it is an owner decision. The cancel call pattern has had only one short testnet run.

**Regression test.** Empty ask book, one-member pool, BUY_QX at price 1000: today Execute returns OK with `shares 0`, `pendingShares 1`, `pendingQx 1`, and a second pool's propose returns QX_BID_PENDING (25); after the fix Execute returns EXTERNAL_CALL, QPOOL has no resting bid, and the second pool's propose returns OK. Also a partial-fill case and the same assertions on a CONTRACT ledger.

### R5. One-unit owner directs the IPO rebid budget (report: Medium)

**Confirmed trace.** `RebidIpo` checks only that the caller has a nonzero seat (4593-4598). The caller picks price (above the book minimum, up to 1,479,289,940,828) and quantity (1..676) with `price x quantity <= avail` (4693-4694), where `avail` is the holding's free funds. There is no stake threshold, vote or cooldown. The call is free (4580). The attacker has to be a YES voter who deposited before the proposal opened (3251-3255) and the seats cannot be bought or sold until settlement (5438, 5557), so the report's own downgrade condition is met.
- LOCK: price = all free funds, quantity 1. The pool cannot respond in the last hour; principal returns at settlement except clearing price x seats won.
- BURN (prover's hand-derived example): book of 600 slots at 10,000 and 76 at 12,000, pool free funds 10,000,000. A rebid of 600 seats at 11,999 evicts the 600 low slots and becomes the lowest slot itself, so the pool pays 600 x 11,999 = 7,199,400 instead of 600 x 10,001 = 6,000,600 for the same seats: an extra irreversible 1,198,800 QU (about 20 percent), borne pro rata by all owners. The external top bidders pay more too.
- No mechanism moves value to a colluder: `ipo.h` has no payee, the QU for seats goes to the fee reserve and refunds return to the bidder.

**Why I classify it as accepted design.** It is the product of two owner decisions: no ceiling price (budget is the only limit) and any seat owner of any size may call `RebidIpo`. The report's remedies (vote-bound maximum price, quantity cap, expiry) contradict D1. A price band would block defensive last-hour jump bids and still allow the many-seat burn.

**Real observations the owner should note.**
- The owner's stated decision is "rebids are funded only by money already returned". The code also lets the never-bid remainder of the budget be rebid from tick one: Execute bids all-in with at most 676 seats (3777-3795) and Propose does not cap the budget at 676 x opening price (3033). The comment at 3775-3776 ("less than one seat") is wrong when the 676 cap bites. Sizing budgets near 676 x opening price removes the exposure.
- The burn variant converts an accepted decision into a quantified, irreversible loss. Whether it is a loss in value depends on what the target's shares are worth, which cannot be determined statically.

**Errors and overstatements in the report.** "Likelihood high" ignores the join, deposit-before-open and pass-the-vote preconditions. The collusion-transfer raise condition has no mechanism. The report never states that principal is protected by uniform clearing price, the premium refund and `ipoAtRisk + ipoAvailable == costBasis`. "Cheap exhaustion" is not supported.

**Options (owner decision).** Keep D1 and document it, fix the comment and the register wording, prefer private pools for IPO exposure. If narrowing is wanted: a roster-relative stake gate on the caller (`own.cost * 2 * max(1, rows) >= costBasis`, always satisfiable) removes dust seats without locking any pool out. A fixed percentage threshold can deadlock pools with up to 256 owners and is rejected.

**Test.** WHALE plus a 1-QU DUST member, budget 10,000,000 at opening 10,000. Characterise today's LOCK and BURN outcomes with the hand-derived numbers above (not run); post-fix, dust calls return NOT_AUTHORIZED. Control: with `budget == 676 x opening` and no outbid, any rebid returns INSUFFICIENT_BALANCE; with `10 x` that budget the same call succeeds from tick one.

**Disagreement.** Prover Medium, refuter Low. I side with the refuter on classification (accepted design, no principal loss) and with the prover that the BURN variant and the parked-remainder mismatch are real and under-documented. Overall Low.

### R6. Proposal scan (report: Medium)

**Confirmed trace.** In Propose, any member with no live proposal in that pool forces a full 262,144-iteration pass over the proposals array (2955-2967) before any kind-specific validation (2969 onward). The only exits before it are bad poolId/kind, inactive pool and non-member (2932-2948). Invalid input (for example `shares == 0`) returns INVALID_INPUT with no state write, so it repeats for free. Entry cost is a one-time 20,000 QU per (address, pool), of which 10,000 is a non-refundable join fee; membership is permanent (no `contributions.remove` exists). Each call reads one cache line per slot over a roughly 42 MiB array (168 bytes per slot).
- Harm path: core bills execution time to QPOOL's own fee reserve, and at reserve <= 0 refuses every user procedure including `WithdrawUncommitted` and `ClaimPayout` (core `qubic.cpp` 2943-2954). Recovery is by third-party burn or END_EPOCH's 10 percent burn leg. No principal is lost; exits are frozen.
- The size of the effect cannot be determined statically: per-call time (estimates 0.3 to 20 ms), fee multiplier (operator-set; default 1/1 in the owner's checkout), reserve size and tick inclusion are runtime facts.

**Errors and overstatements.** "Every proposal attempt" is wrong: non-members and bad kinds exit early, and a caller with a live proposal exits at its slot. The "second global scan" (3134-3144) is not global: it breaks at the first free, finished or expired slot. The report's excerpts omit lines (2937-2941, the lifetime break at 3142-3143). Its metering question has an answer: time is billed to the contract's reserve, not the caller. Entry cost is understated (20,000 QU, not 10,000). It does not know the owner tracks this as C009 (K9) and that R1 is the broader reserve-drain class; fixing the scan alone does not close R1.

**Fix.** One token: at 2957 change `continue` to `break`. Both reviewers verified the equivalence: `kind` is only ever written non-zero, allocation takes the lowest reusable slot (3134-3144), no code writes `kind = 0`, so used slots form a prefix and the scan becomes O(H) with H the high-water mark of concurrent proposals. For true O(1), store a live-slot pointer on the member's Contribution record (stays 48 bytes), and re-read the Contribution row after the `ReleaseVotes` call at 3163 so a stale copy does not overwrite `lockedForVotes`. Also move cheap field checks ahead of the guard and expose the fee reserve in a view.

**Regression test.** Probe counter under a test macro: with H = 200,000 pre-filled slots, 50 invalid Propose calls by a member with no live proposal must cost at most 50 x small constant after an O(1) fix (and 50 x H with the one-token fix); state hash unchanged. Semantic tests that must pass before and after: PROPOSAL_PENDING at the highest slot, expiry of OPEN and PASSED proposals for both lifetimes, slot reuse by another proposer, and a prefix-invariant test over random Propose/Vote/Tally/Execute traces.

### R7. Vote snapshot manipulation (report: Medium)

**Confirmed trace.** BUY `eligibleWeight` is one pool-wide number, `pool.uncommittedTotal` at Propose (3165-3173). Vote weighs a member by their whole current balance and rejects the whole balance if `lastDepositTick >= openTick` (3240-3255). Only two paths raise a balance: `Contribute` (self only) and `SettleIpo` (permissionless); both stamp `lastDepositTick` (2742, 4986-4987).
- Stranger vector: after a BUY opens, anyone calls `SettleIpo` on a settle-ready IPO holding of that pool. Each refunded owner who has not yet voted loses the vote on their whole pre-open balance. If those owners held more than ceil(E/2) of the snapshot, Tally must reject (3367-3373). Cost zero, single shot per holding (`ipoSettled`, 4823), at most 8 holdings per pool, no fund movement (BUY charges only YES voters), re-propose possible at once, and defusable by settling first and waiting one tick. An accidental trigger by a keeper at epoch start is as likely as an attack.
- Withdrawal variant: a member parks capital X greater than all honest uncommitted money before Propose, then withdraws. That is plain abstention and is no stronger than voting NO with the same weight. It adds only capital efficiency. A deposit made after Propose is not in the snapshot, so it cannot be timed to a proposal the attacker merely observes.

**Errors and overstatements.** The report never states the thresholds. "Force a selected proposal to fail" is wrong for the withdrawal path. It omits that the effect is single-shot, temporary and limited to non-voters. Its remedy ("lock the pre-open tranche") would let any member freeze others' withdrawals with a free Propose, which is worse. A sibling the report missed: the permissionless Execute of one PASSED BUY debits YES voters' balances without shrinking another open BUY's snapshot (3874), which is ordinary money-spent behaviour in the same stale-denominator class.

**Fix.** Zero-code: settle all ready IPO holdings, wait one tick, then Propose. Code: add `voteFloor` to Contribution; on a credit set `voteFloor = (tick - lastDepositTick > VOTE_WINDOW) ? before : min(voteFloor, before)`; on a debit `voteFloor = min(voteFloor, uncommitted)`; in Vote use `min(uncommitted, voteFloor)` when `lastDepositTick >= openTick`. The weight then never exceeds the balance at open and the sum stays under E (guard 3287). Alternatively keep the rule (owner decision D3) and document it.

**Test.** Members A 300,000, B 200,000, C 100,000 (E = 600,000). A and B own a failed IPO holding that refunds them. C proposes a BUY; a non-member calls `SettleIpo`; today A and B get DEPOSIT_AFTER_OPEN and Tally returns REJECTED; after the fix they vote with their pre-open balance and the BUY passes. Negative cases: a post-open Contribute adds no weight, two post-open credits do not launder the first, and votes cast before the settle keep weight and lock.

---

## 4. Review of the testnet session's two fixes and the devkit constants

### F-01 fix (IPO share name claimed at registration, including IPO ledgers; 1911-1917)

**Verdict: correct for the narrow case, incomplete, and it adds new flaws.**

It does close the exact scenario in its comment: two different unlisted targets, each first registrant of its own ledger, declaring the same unissued name. The first Execute claims the name; the second is refused with `CONTRACT_MISMATCH` before any bid, so the settle-order race in `SettleIpo` (4897-4911) is gone for that case. The theft route of the earlier finding A1 is closed in K6.

What it breaks or leaves open, all confirmed:
1. **Permanent name squat (N1, High, K6 only).** The claim is released only when the ledger had bound a name (2052-2056). A holding that settles with no seats, or a `NAME_UNPROVEN` holding, leaves the claim for ever. Section 3, R3 V1.
2. **Only the first registrant claims (N2, High, K6 sequence).** A later honest registrant on the same unbound ledger neither claims nor is compared with the claim, so a squatter can claim its true name for a second target and make its settlement unprovable. Capital frozen. Section 3, R3 V3.
3. **Same-index different-name wedge untouched.** A second holding on an unbound ledger with a junk name that wins one seat parks the whole ledger's dividends. This is the open R9/K4b.
4. **Invariant and comments are stale.** "At most one `contractNames` entry per contract index" (500-501, 1759-1762) is now false; the true bound is two per index, at most about 1,986 against capacity 2,048. The `ContractPairKnown` comment (1622-1625) and the F-01 comment (1900-1910, which says the second registration is refused although only the second new ledger is) are also stale. The CheckSource capacity pre-check (1764) still excludes IPO registrations while `RegisterHolding` can now return CAPACITY for them after the irreversible bid; Execute ignores that return code (3861-3862). Verifiers show the table cannot actually fill (each index has one IPO epoch, an IPO ledger cannot die before settlement, indices are capped at 1,024), so this is latent (info), but the guard and a stronger `static_assert` make it provable.
5. **Net risk change.** The old race needed real seats won (capital at risk). The squat needs only a refundable bid, so the fix lowers the attacker's cost and moves failure from settlement time to bid time.

**Recommendation.** Do not ship F-01 as written, and do not revert it either (reverting reopens the original race). Rework it as in R3's fix: claimed name stored on the ledger, one declared name per unbound ledger, release on every terminal path, plus the K4b write-off.

### F-02 fix (`Offer.sharesSnapshot` and `BuyPosition_input.expectedShares`)

**Verdict: correct as a buyer-side check of live shares, weaker than its comments say, fail-closed, with an ABI problem.**

- `BuyPosition` refunds with OFFER_CHANGED unless the live `Holding.shares` equals the value the buyer passes (5570-5574), placed after the offer, parcel, price and holding checks and before any write. I checked every path that changes `Holding.shares` (SELL execution, `ReconcileHolding`, `SettleIpo`, `RebidIpo`, `EvictSquat`) and found none that makes BuyPosition fail for ever. The seller can always refresh with `OfferPosition`, because a changed snapshot is no longer an identical re-offer (5460-5461).
- **The snapshot is never enforced (N6/N8, Low).** `sharesSnapshot` is written (5477) and returned by `GetPosition` (2399) but never compared. The Offer comment (731-738) says BuyPosition "refuses unless the holding's current shares still match what the offer last saw"; the code does not (the BuyPosition comment at 5562-5569 correctly describes the buyer-stated check, so only one comment is wrong). A buyer who passes live shares fills a stale offer.
- **The documented flow dead-ends.** The header (1280-1283) tells buyers to take `expectedShares` from `GetPosition(...).offeredShares`, which is the old snapshot. After any change of recorded shares the documented flow returns OFFER_CHANGED on every attempt until the seller re-offers; only `GetHolding.shares` shows the live number. All refusals refund and write nothing, so no funds are lost. The permissionless `ReconcileHolding` (which only raises shares, bounded by `pendingShares`, each change needs a real QX fill) can trigger this deliberately.
- **ABI.** `GetPosition_output` gained `offeredShares` before `voteLockUntilTick` (1445-1446), shifting the later field for existing readers; append instead. `BuyPosition_input` is 8 bytes longer; the core zero-pads short inputs (core `contract_exec.h` 1128-1139), so an old client sends `expectedShares = 0` and is refused unless shares are 0 (fail-safe, though 0 is also a legitimate value for a QX-bid holding). `Offer` is 8 bytes larger (about 256 KiB more state), so the layout differs from the candidate; fine only for a fresh deployment. README, CHANGELOG and TESTING_GUIDE do not mention `expectedShares`, and the ordering comment at 5513-5516 omits the new check.
- **Identity prong still open (earlier finding A2).** BuyPosition binds slot, seller, cost units and share count but not the holding's incarnation or asset. If the ABI is changing anyway, add `expectedIncarnation` now.

**Recommendation.** Either return the live shares from `GetPosition` and delete the snapshot (zero risk, buyer protection kept), or enforce the snapshot with its own return code and expose live shares. Fix the layout, the comments and the docs, and add the incarnation field in the same change.

### Devkit and testnet constants: what must never reach a real network

| Item | Where | Why it must not ship |
|---|---|---|
| `QPOOLK6_TICKS_PER_HOUR = 2` | 302 (candidate 3600) | Vote, execute and IPO-vote windows become 144, 96 and 48 ticks, a few minutes. A majority can propose, pass and execute before members can react. Stale 3-day, 2-day, 1-day comments at 91, 127, 176, 312 hide this. |
| `QPOOLK6_QPAYHUB_CONTRACT_INDEX = 50` | 390 (candidate 29) | On any network with fewer than 51 contracts, 30 percent of revenue goes to an unspendable key and is booked as deposited. A future contract at index 50 might inherit the balance (unproven). |
| QPAY issuer `RDOIGJSE...` (test wallet) | 5800-5808 (candidate has the real `QPAYNOWSWZ...` issuer) | The pot buys nothing, or is capturable by the test-key holder who issues a worthless "QPAY" and lists it under the 10,000 QU ceiling. Most directly value-extracting leftover. |
| Names `QPOOLK6`, `QPOOLK62`, all `QPOOLK6_` identifiers | 506-508 | Must match the registered contract name (the core wires `QPOOL`, `QPOOL2`). Not a logic constant, but the build cannot ship under these names. |
| Re-verify on proposal day (unchanged from the candidate) | 434-435, 1548-1578, 494 | Embedded name table for indices 1..30 (29 is QTREAT on the devkit, QPAYHUB in the table), QX = 1, QSWAP = 13, QPAY name constant, QX transfer fee 100, QSWAP sell fee 100,000. |

Also correct the comment at 381-382: it says anything under 845 QU pays no shareholder and is "burned in full". The code splits 40/30/20/10 (6269-6283), so the shareholder threshold is 1,690 QU, and only the 40 percent shareholder leg falls into the burn; the other legs are paid regardless. The candidate's comment was right; K6 regressed it.

The semantic diff between candidate and K6 is complete. Besides the five listed differences there is only a comment change at 476. K6 renamed with the prefix removed equals the candidate plus those five differences.

---

## 5. Status of the earlier independent assessment's nine findings on the latest contract

| ID | Topic | Status on K6 | My severity | Evidence and note |
|---|---|---|---|---|
| A1 | Contract-share name cross-claim (shared-name race at SettleIpo) | PARTIALLY_FIXED | Medium | Theft route closed (claim at bid time, 1911-1917, refused by 1647). Remaining: names unauthenticated (first registration binds), zero-win leak (2052-2056), first-registrant-only claim, wrong-name wedge R9, dust purchase naming a wrong index (R2). |
| A2 | `BuyPosition` stale backing and identity substitution | PARTIALLY_FIXED | Low | Partial-sale prong fixed by `expectedShares` (5570). Identity prong open: no incarnation or asset binding. Substitution needs the seller to retire the holding, redeposit a different asset with the same share count and re-offer. On the testnet window (minutes) it is more practical than on 72 hours. |
| A3 | Reserve-drain levers (Propose scan, RebidIpo, reserve <= 0, unmatched transfers) | OPEN | Medium | Propose scan confirmed (2955-2967). RebidIpo work is overstated (cheap pre-check at 4664). Reserve <= 0 freeze confirmed in core. Each unmatched arrival still writes state (6160-6173). Real cost not measurable statically. |
| A4 | Uncancellable QX bid | OPEN | Medium | Same as R4. |
| A5 | Roster fill: 256 members at 10,000 QU each (about 2.56M QU) fills an open pool for good | OPEN | Low | No removal path (2552-2570). Creator can prevent it with `SetPermissioned(1)` in time. |
| A6 | Retired-holding slots pinned by owners who never claim | OPEN | Low | Slot and row freed only by the owner's own ClaimPayout (4543-4551); erodes 128 holding slots and 512 seat rows. |
| A7 | Vote table can be filled (524,288 rows) | OPEN | Low | A pool of 256 members with 256 live proposals can hold 65,536 rows, so 8 such pools fill it, about 100.5M QU of fees. Every Vote then returns CAPACITY until windows expire. No funds lost. |
| A8 | `SELL_QSWAP` all-or-nothing management transfer | OPEN | Low | Mixed QSWAP/QX management of one asset can make the sale return TRANSFER_FAILED where SELL_QX would succeed. Liveness only. |
| A9 | 100 QU of QX hand-over fee booked as spent but refunded | OPEN | Low | Each QPAY hand-over leaves 100 QU in the balance counted by no bucket (5936-5965). One-line fix: `feePaid = 0`, or measure the balance difference. Valid against the supplied QX source; a deployed QX could differ. |

---

## 6. New findings the report missed, and candidates that were refuted or reduced

Every new candidate was checked by an independent verifier. None was refuted outright; several were reduced.

### Verified new or newly attributed findings

| ID | Finding | Severity | Candidate | K6 | Verifier verdict |
|---|---|---|---|---|---|
| N2 | F-01 claims only for the first registrant, so a later honest registrant can be made unprovable and its capital frozen | High (confidence medium in the delta review, high in verification; likelihood depends on concurrent IPOs) | Outcome yes (original race), sequence no | Yes | CONFIRMED. Fix: store claimed name, enforce one declared name per unbound ledger. Cost to attacker is one pool (about 10M QU), not two. |
| N1 | F-01 claim never released on zero-win or `NAME_UNPROVEN` (this is the report's R3 poison half; the report did not say it comes from the F-01 fix) | High | No | Yes | CONFIRMED. Fix: release on every terminal path. |
| N4 | `BuyAndBurnQpay`'s only working burn route needs QPOOL's own `PRE_ACQUIRE_SHARES` hook, which refuses when the QPAY issuer's token ledger is bound to another asset name; the pot is spent on QPAY that cannot be burned | Low (conditional on the issuer wallet having issued a second asset, unknown) | Yes | Yes | CONFIRMED. Fix: in the hook, accept `input.owner == SELF`, or burn directly via QX `TransferShareOwnershipAndPossession` to `NULL_ID` (verifier suggestions, untested). `EvictSquat` is a weak recovery here (a sybil seat purchase defeats it). The backlog burns in full once the hook accepts. |
| N5 | A voted IPO opening price can be pre-empted by anyone who registers that (target, price) first; the proposal stays PASSED and YES voters stay locked | Low | Yes | Yes | CONFIRMED. Fix: in the IPO_PRICE_TAKEN branch (3484-3487) mark FAILED and call `ReleaseVotes`, as the dead-mandate branch does (3615-3631). The same collision can happen between two honest pools on a round number. |
| N6 / N8 | F-02 `sharesSnapshot` never enforced; `GetPosition` returns a stale figure; the documented buyer flow dead-ends after any share change | Low | No (fields absent; the candidate has the unfixed race, which is worse) | Yes | CONFIRMED. Sweep and delta first rated it info and low respectively; I rate it Low. |
| N3 / N10 | F-01 added a failing `contractNames` write to RegisterHolding for IPO ledgers that CheckSource does not pre-check, and Execute ignores the return code | Info | No | Yes (latent) | PARTIALLY_CONFIRMED. Code facts true; unreachable because the table cannot fill (at most about 1,986 entries against 2,048; each index has one IPO epoch). `DepositHoldingAsset` is not affected. Fix: drop `&& input.ipoContractIndex == 0` from the guard at 1764 and tighten the `static_assert` at 504 to twice the index span. |
| N7 / N9 | `KIND_ALLOCATE` is still accepted by Propose but `Pool.unattributedQu` has no writer, so a passed ALLOCATE always returns NOTHING_OWED; voting on it locks the voter's own seat for the vote window | Info | Yes | Yes | CONFIRMED. The owner already documents ALLOCATE as "kept for ABI" (README 111, 362, 1423). Fix: reject kind 6 at Propose (`input.kind > QPOOLK6_KIND_SELL_QX`). It does not remove the seat-lock grief, which an unreachable-minimum `SELL_QX` gives too. |
| N11 | Comment at 381-382 states a wrong revenue threshold (845 QU, 80 percent); code gives 1,690 QU | Info | No | Yes | Corroborated by two reviewers; comment only. |

Note on duplicates: N3 and N10 are the same candidate from two sweeps; N6 and N8 are the same; N7 and N9 are the same.

### Candidates and claims that were refuted or reduced

- N3/N10 as an exploitable condition: the proposed reachability ("about 250 IPO epochs of stale entries", "many cheaper cycles") is wrong. A contract has one IPO epoch, so stale claims are bounded by the number of indices.
- N5: "the attacker keeps the entry alive by not calling RebidIpo" is wrong (any seat owner of the squatting holding clears a dead price; what protects the squat is that the victim cannot buy a seat in an unsettled holding). The zero-seat variant adds almost nothing. The proposer is blocked, but other pool members can propose at another price.
- N4: "pot value is stranded" is overstated: the QU went to QPAY sellers and the QPAY is locked in QPOOL, which removes it from circulation; and the claim that `EvictSquat` recovers it is wrong in the attacker's favour.
- N6: scenario (a) (a bot fills a stale-priced seat) is not a victim scenario, because a buyer who passes live shares knowingly buys that backing.
- N7/N9: the seat-lock grief is not specific to ALLOCATE.
- N2: the cost is one pool, not two.
- Report items reduced in section 3: R1 "wrong token bought", R1 "collapses windows", R2 "dividend attribution", R3 "cheap exhaustion of eight rows", R5 "collusion value transfer", R6 "every attempt" and "second global scan", R7 "force a selected proposal to fail".

### Surfaced by reviewers but not separately verified

- If a pool ever holds QPAY under QPOOL management, `BuyAndBurnQpay`'s first burn path (5927) might burn pool shares; nothing excludes QPAY from deposit or purchase. Needs its own check.
- The comment at 3775-3776 about "less than one seat" is wrong when the 676-seat cap bites (R5).
- The README states a pool costs 50,000,000 QU; the code says 10,000,000 (332). README about line 1456 says a wrong-name holding "can delay payouts until someone settles it"; an unprovable holding cannot settle.

---

## 7. Prioritized action list

The contract has no admin and no upgrade path, so everything under "before any real deployment" is a blocker for a mainnet proposal.

**Before any real deployment**
1. Rework F-01 (R3, N1, N2): claimed name stored on the ledger, one declared name per unbound IPO ledger, release on every terminal path, and the K4b write-off for an unprovable winner (R9). Do not ship F-01 as written; do not simply revert it. Add the four R3 tests.
2. Release gate (R1): production values as compile default, devkit values behind an opt-in macro, grep gate for `TESTNET ONLY|TEST-ONLY|QPOOLK6_|RDOIGJSE`, diff against the audited candidate, rename to the registered contract name, and re-verify every index (QPAYHUB, QX, QSWAP, the 1..30 table) on proposal day.
3. QPAYHUB leg probe in END_EPOCH (R1 residue, owner R5, also affects the candidate): fall into the burn leg when no contract exists at the index.
4. BUY_QX immediate-or-cancel (R4, A4, K7). This needs an owner decision on losing passive limit orders; the alternative (a stored bid price and a `CancelQxBid` procedure) is much larger.
5. Proposal scan (R6, A3, K9): change `continue` to `break` at 2957 now; plan the O(1) pointer and validation-before-scan; expose the fee reserve in a view. This does not close the wider reserve-drain class.

**High value, low effort**
6. N5: set FAILED and release votes in the IPO_PRICE_TAKEN branch.
7. F-02 cleanup (N6/N8, A2): return live shares from `GetPosition` or enforce the snapshot; append the new output field instead of inserting; add `expectedIncarnation` to `BuyPosition_input` in the same ABI change; fix comments and update README, CHANGELOG and TESTING_GUIDE.
8. R2: extend the embedded table to every contract in the target node build at freeze; add the lossless release for never-filled purchase bindings.
9. N4 hook fix and A9 (`feePaid = 0`).

**Owner decisions and housekeeping**
10. R7: add the `voteFloor` rule, or document "settle every ready IPO holding, wait a tick, then Propose".
11. R5: keep D1 (document it), correct the 3775-3776 comment and the register wording ("rebids funded only by returned money"), optionally add the roster-relative stake gate.
12. Reject kind 6 at Propose; widen the CheckSource name-table guard to IPO registrations and tighten the `static_assert`.
13. Correct stale comments (91, 127, 176, 312, 381-382, 500-501, 1622-1625, 1759-1762, 1900-1910, 3775-3776, 5513-5516) and README (50M versus 10M fee, wrong-name wording).
14. A5 to A8: accept and document, or fix when the state profile is revisited.

---

## 8. What remains unverified

Everything above is static. Nothing was compiled or run; every test is a sketch and the numeric examples (R5 burn example, R1 stranded amounts, the 1,986 name bound) are hand-derived from the source.

Cannot be determined statically:
- Execution time per call, the fee multiplier, the reserve size and tick inclusion policy, so the size of any reserve drain (R6, A3) is unknown.
- Whether the test wallet's key is public or kept private (R1), whether the real QPAY issuer ever issued a second asset (N4), and whether a later contract at index 50 would inherit stranded QU.
- The mainnet contract table and index layout at deployment time. The owner's core checkout is older (31 or 32 contracts); the supplied QX, QSWAP, IPO and QPI sources may differ from deployed versions.
- Transaction ordering inside a tick, keeper behaviour (for example settling at epoch start, R7), and how often two IPOs overlap or a pool wins seats (R3 V3).
- The real tick rate (owner R15 reports 0.4 to 0.6 seconds), which would shorten wall-clock windows further.
- Whether a failed IPO's name or index is ever reused later (the benign side of the claim leak).
- The behaviour of `qpi.bidInIPO` when a book has no room for a below-floor bid was read from the core source, not observed; the code treats a result of 0 as success.
- Off-repo transaction encoders for `BuyPosition` and `GetPosition` (the F-02 ABI changes).
- Whether the N3/N10 verifier's bound of two names per index would survive a future change to the IPO model.

Not re-derived: the owner's register (R1 to R7, A1 to A9) was used as given except where traced; no review lens re-examined QSWAP pool maths, QX collection ordering tie-breaks, tombstone behaviour of the large hash maps, or END_EPOCH wall-time against the core's timeout.
