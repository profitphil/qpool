# K8 testnet checks (name-free IPO and settlement at epoch start)

Generated from the build agents' own check lists, with the NAME_PENDING return code corrected to 41 (K8 keeps 40 for CancelQxBid NO_PENDING_BID, as in K7). Nothing here has been run on a testnet. The CancelQxBid and small-fix checks are in `K7-TESTNET-CHECKS.md`.

## Name-free IPO (registry, END_EPOCH queue, BEGIN_EPOCH pairing)

1. **Steps:** BUY_IPO with a nonzero name field: Propose (procedure 4) {poolId, kind 3, asset {issuer all-zero, assetName = any nonzero}, quAmount >= price, shares any, price, holdingId = a live IPO target index above QPOOL's own}. Then the same call with asset all-zero.
   **Expect:** First call: returnCode 1 (INVALID_INPUT), no proposal created, any attached QU refunded. Second call: returnCode 0 and a proposalId. After Vote, Tally and Execute, GetHolding (function 3) shows assetName 0 and ipoContractIndex = the target.

2. **Steps:** Explorer before and after one IPO: note the list of assets issued by the zero address (AAAA...FXIB) in the explorer. Let ONE contract T (above index 30, deployed after QPOOL) finish its IPO with a full book (676 slots, last slot priced above 0). After the epoch change call GetNameRegistry (function 10) {index = T, assetName = the new name shown by the explorer}.
   **Expect:** The explorer lists exactly one new zero-issuer name. GetNameRegistry: name = that name, nameSource 2, nameState 2, nameIndex = T, paired +1, pairEpoch = the new epoch, mismatches unchanged, queueLength 0, overflow 0.

3. **Steps:** Baseline: before QPOOL's first epoch change, have at least one zero-issuer name outside the embedded table already on the chain (any contract above 30 that IPO'd before QPOOL's construction epoch). After QPOOL's first BEGIN_EPOCH call GetNameRegistry {index 0, assetName = that name} and then Propose BUY_QX (kind 2) for (its index, that name) and DepositHoldingAsset for the same pair.
   **Expect:** baselineDone 1, nameState 3 (nameIndex 0), mismatches 0, paired 0. Both the proposal and the deposit return 41 (NAME_PENDING) with nothing written. A table name (e.g. QX at index 1) shows nameState 1.

4. **Steps:** Two IPOs finishing in one epoch (contracts 31 and 32) with DIFFERENT bidder sets: e.g. wallet A takes 400 slots of 31 and 276 of 32, wallet B takes the rest of each (any two different multisets, QPOOL's own seats count too). After the epoch change call GetNameRegistry {index 31, assetName name31} and {index 32, assetName name32}.
   **Expect:** name for index 31 = contract 31's own share name and for index 32 = contract 32's, paired +2, mismatches unchanged, whatever order the explorer lists the two names in (the walk order is not the issuance order and the pairing does not use it). Do NOT use one wallet that takes all 676 slots of both books for this check: those books are identical and are quarantined (next check).

5. **Steps:** An epoch with a failed IPO and a winning IPO: contract 31's book left not full (e.g. 675 slots), contract 32's full. After the epoch change: GetNameRegistry {index 31} and {index 32, name32}; SettleIpo (procedure 14) on a holding that bid in 31 and on one with seats in 32.
   **Expect:** Only 32 is queued and paired: paired +1, name for 31 is 0, name for 32 = name32, mismatches unchanged. SettleIpo on the 31 holding returns 0 (whole bid back, no name needed, retired); SettleIpo on the 32 holding returns 0 with shares = seats won and GetHolding.assetName = name32.

6. **Steps:** Mismatch epoch (quarantine): make two IPOs end in one epoch with IDENTICAL books, e.g. QPOOL bids the same budget in 31 and 32 and gets the same number of seats in each, and one other wallet takes all the remaining slots of both. After the epoch change call GetNameRegistry {index 31, assetName name31}, then SettleIpo on QPOOL's holding in 31.
   **Expect:** mismatches +1, lastMismatchEpoch = the new epoch, lastMismatchNew 2, lastMismatchQueued 2, paired unchanged, name for 31 and 32 = 0, nameState 3 for both names. SettleIpo on a holding that won seats returns 41 (NAME_PENDING) and GetHolding / GetIpoTarget are unchanged (capital stays on the books; this is permanent for those two indices). SettleIpo on a holding that won no seat still returns 0.

7. **Steps:** Recovery in the next epoch: let a later single IPO (contract 33) finish with a full book after the quarantine epoch, then GetNameRegistry {index 33, assetName name33}.
   **Expect:** name = name33, paired +1, mismatches still the earlier count (no new quarantine), queueLength 0.

8. **Steps:** SettleIpo before pairing returns the new code and after pairing succeeds: SettleIpo (procedure 14) {holdingId} on QPOOL's holding with seats right after an epoch change in which its target was paired (normal case), and on a holding whose target was quarantined or never paired.
   **Expect:** Paired target: returnCode 0, GetHolding.assetName = the name, GetSource {id(T,0,0,0), kind 1}.assetName = the name, recognized = seats, unsettled 0. Unpaired target: 41 and nothing written. NOTE: on a live node BEGIN_EPOCH runs before the first transaction of the new epoch, so the window 'IPO shares exist but the name is not paired yet' is not observable on testnet; only the permanent unpaired case is. The window was exercised in the scratch harness (test H, below).

9. **Steps:** A purchase after pairing: Propose BUY_QX (kind 2) {asset {NULL_ID, the paired name}, holdingId = the paired index} and DepositHoldingAsset with the same pair; then the same with the name of ANOTHER paired contract.
   **Expect:** Paired pair: returnCode 0 (the later Execute works as before). Another contract's name: 28 (CONTRACT_MISMATCH). A name the registry never saw: 41.

10. **Steps:** Restart idempotency: restart the node so that BEGIN_EPOCH runs again at the first tick of an epoch in which a pairing just happened, then call GetNameRegistry.
   **Expect:** paired, pairEpoch, mismatches, lastMismatch* identical to before the restart; queueLength 0. (Not run anywhere: needs a real node.)

## Settlement at BEGIN_EPOCH

1. **Steps:** SEVERAL POOLS ON ONE TARGET. In the target's IPO epoch, for each of 3 pools (distinct creators): CreatePool(1){poolName, permissioned 0} with the creation fee attached; Contribute(2){poolId} with e.g. 1,000,000 QU; Propose(4){poolId, kind 3 (BUY_IPO), asset all zero, quAmount 10000, shares 0, price 2000+k (a different opening price per pool), holdingId = the target contract index}; Vote(5){proposalId, approve 1}; after the IPO vote window Tally(6){proposalId}; Execute(7){proposalId} inside the target's IPO epoch. Let the epoch change. Call NO SettleIpo. At the next epoch's first tick the node runs BEGIN_EPOCH.
   **Expect:** Tally status passed and Execute returnCode 0 (holdingId returned). After the epoch change GetStats(6): autoSettleEpoch == the new epoch, autoSettleDone == 3, autoSettleSkipped == 0, readyUnsettled == 0. GetHolding(3){holdingId} for each: ipoSettled == 1, shares > 0, assetName == GetNameRegistry(10){index = target, assetName 0}.name (nameSource 2). GetSource(7){source = id(target,0,0,0), kind 1}: found 1, unsettled == 0, recognized == the sum of the three holdings' shares. GetPool(1){poolId}: uncommittedTotal rose by budget minus seats x clearing price, committedTotal == the holding's costBasis.

2. **Steps:** A TARGET WITH MORE READY HOLDINGS THAN THE BOUND. Production bound is 32, so a real test needs 33 holdings; instead build the testnet candidate with the one constant QPOOL_AUTO_SETTLE_MAX = 2 and make 5 holdings on one target as in check 1. After the first epoch change call GetStats(6). Send a dividend path if available (the target pays dividends) or just read GetSource(7). Then SettleIpo(14){holdingId of a holding with ipoSettled 0}. Then let two more epochs pass.
   **Expect:** First pass: autoSettleDone == 2, autoSettleSkipped == 0, readyUnsettled == 3, GetSource unsettled == 3, the other three holdings untouched (ipoSettled 0, costBasis unchanged). A dividend arriving now is parked: GetSource escrow > 0 and accHigh/accLow unchanged. The manual SettleIpo returns returnCode 0 with shares > 0 and readyUnsettled drops by 1. Next epoch: autoSettleDone == 2 (or the remainder), cursor moves on, autoSettleTotal grows, nothing settled twice. After the last holding settles: GetSource unsettled == 0, escrow == 0, accHigh/accLow raised (the parked dividend was released per recognised share), readyUnsettled == 0. Scratch tests T2/T3 saw exactly this at bound 2.

3. **Steps:** A FAILED IPO RETIRES ITS HOLDINGS AUTOMATICALLY. A pool bids (same Propose/Vote/Tally/Execute as check 1) on a target whose book will not be full at the end of its IPO epoch (a small IPO). Note GetPool(1){poolId}.uncommittedTotal after Execute. Let the epoch change. Call no SettleIpo.
   **Expect:** GetStats: autoSettleDone counts the holding, autoSettleSkipped 0. GetHolding(3){holdingId}: found 0 (or costBasis 0 and active 0). GetPool(1): uncommittedTotal back to its value before Execute (the whole budget returned), committedTotal 0, holdingCount decreased. GetSource(7) for the target: found 0 (no ledger left). GetNameRegistry(10){index = target}: name 0 (no shares were issued).

4. **Steps:** MANUAL SettleIpo AFTER THE AUTOMATIC ONE. After check 1 or 3 call SettleIpo(14){holdingId} for every holding BEGIN_EPOCH settled, from a different wallet, with 0 QU attached; then once more with 1 QU attached.
   **Expect:** returnCode 18 (IPO_NOT_ACTIVE), shares 0, returnedToMembers 0 each time; the 1 QU attached is sent straight back to the caller (the only QU movement); GetStats, GetPool and GetSource are unchanged by the call.

5. **Steps:** DIVIDENDS OF THE NEW CONTRACT FLOW AFTER THE LAST HOLDING SETTLES. After check 1, wait for the new contract's own END_EPOCH distribution (or any dividend from it). Read GetSource(7){source = id(target,0,0,0), kind 1}, GetPosition(4){holdingId, member}, then ClaimPayout(9){holdingId} from a member.
   **Expect:** accHigh/accLow rise, escrow stays 0, unsettled 0; GetPosition owed > 0 for members with cost units; ClaimPayout returns 0 and pays the member (less the management fee).

6. **Steps:** RESTART IDEMPOTENCY (only if the owner restarts a node at the first tick of an epoch). Read GetStats(6) before and after the restart.
   **Expect:** autoSettleEpoch, autoSettleDone, autoSettleSkipped, autoSettleTotal identical before and after; no holding, pool or ledger number changes; no holding settled twice.

7. **Steps:** OPTIONAL, ONLY IF A QUARANTINE EVER HAPPENS (two IPOs of one epoch with identical bidder sets, see N1). Read GetStats(6) and GetNameRegistry(10){index 0, assetName 0}.mismatches after the epoch change; call SettleIpo(14) on a holding that won seats on a quarantined index.
   **Expect:** autoSettleSkipped > 0 for those holdings; SettleIpo returns 41 (NAME_PENDING) and writes nothing; holdings of the same epoch that won no seat are settled; the refused ones are tried again once per rotation of the cursor and never block others.
