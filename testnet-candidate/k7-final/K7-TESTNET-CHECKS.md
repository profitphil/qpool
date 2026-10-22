# K7 testnet checks (small fixes and CancelQxBid)

Generated from the build agents' own check lists. Nothing here has been run on a testnet. Return code numbers are those of the contract (see its QPOOL_ERR_ list). Check IDs S* are the small fixes, C* are CancelQxBid.

## Small fixes

1. **Steps:** S1 setup: pool P with members M1, M2, M3, each with at least 10,000 QU contributed. M1, M2, M3 each call Propose{poolId=P, kind=2 (BUY_QX), asset={a real issuer, name}, quAmount=Q, shares=10, price=Q/10, holdingId=0} in that order, tx value 0.
   **Expect:** Each returns returnCode 0 with proposalId 0, 1, 2 (lowest free slot, same as before the change). GetStats.proposalCount rises by 3.

2. **Steps:** S1: M3 calls Propose again with valid input; then M1 does. Next, M4, a member with no live proposal, calls Propose{kind=2, shares=0, ...}.
   **Expect:** M3 and M1 each get 20 (PROPOSAL_PENDING): the scan reaches slot 2 and slot 0 before it hits the first unused slot. M4 gets 1 (INVALID_INPUT) and GetStats.proposalCount is unchanged.

3. **Steps:** S1: after the vote window with no votes, anyone calls Tally{proposalId=0}. M1 then calls Propose again.
   **Expect:** Tally leaves GetProposal(0).status = 2 (REJECTED) (or 4 if lapsed). M1's Propose returns 0 with proposalId 0 (slot reuse). A member of another pool with no live proposal gets proposalId 3, the next slot after the used prefix.

4. **Steps:** S2 positive: on a network where a contract exists at QPOOL_QPAYHUB_CONTRACT_INDEX (29 in production), create one pool (CreatePool, attach 10,000,000 QU) and let the epoch end. Read GetStats before and after, and the energy of id(29,0,0,0).
   **Expect:** GetStats.totalQpayhubDeposited rises by the 30 percent leg (3,000,000 for a lone 10,000,000 QU fee) and the energy of id(29,0,0,0) rises by the same amount, as before the change.

5. **Steps:** S2 negative: on a testnet build whose QPOOL_QPAYHUB_CONTRACT_INDEX is at or above the node's contract count (for example the earlier devkit value 50 on a network with fewer than 51 contracts), do the same: CreatePool with 10,000,000 QU, end the epoch.
   **Expect:** GetStats.totalQpayhubDeposited stays 0 and the energy of id(50,0,0,0) stays 0. GetStats.totalBurned rises by the 10 percent leg plus the whole 30 percent leg plus rounding (about 4,000,000 for a lone pool fee). Before the change, 3,000,000 QU reached the unspendable key and was booked as deposited. pendingRevenue returns to 0 (totals conserved).

6. **Steps:** S3 setup: a target contract X in its IPO epoch. Pool A (proposer a1, voter v1) and pool B (proposer b1, voter v2) each Propose kind=3 (BUY_IPO) with holdingId=X, the same price P (no seat registered at P yet), their own budget quAmount, shares=0. Both voters Vote{approve=1} and the voters' GetMember.locked is above 0. After the IPO vote window (the short testnet window, or 86,400 ticks with production constants) call Tally on both so both are PASSED (status 1). Call Execute{A's proposalId}, then Execute{B's proposalId}, attaching 1,000 QU to the second call.
   **Expect:** Execute A returns 0. Execute B returns 17 (IPO_PRICE_TAKEN) and the 1,000 QU attached is refunded. Now GetProposal(B).status = 5 (FAILED) (before: 1, PASSED) and GetMember(B, v2).locked = 0 (before: unchanged until expiry). A second Execute on B returns 7 (WRONG_STATUS). b1 can call Propose in pool B at once and gets 0 (before: 20, PROPOSAL_PENDING). v2's WithdrawUncommitted for the freed amount succeeds.

7. **Steps:** S4 setup (devkit build with the test issuer, a token QPAY issued and an ask listed on QX at or below 10,000 QU, for example 10 shares at 100 QU): GetStats gives qpayBuyPot = P0 (at least 1,100) and qpayUnburned = 0; read the contract balance E0. Call BuyAndBurnQpay{maxLevels=0}, tx value 0.
   **Expect:** returnCode 0, bought = 10, spent = 1,000, burned = 10, unburned = 0. potLeft = P0 - 1,000 exactly (before the fix: P0 - 1,100). The contract balance E1 satisfies E0 - E1 = 1,000 = P0 - potLeft, so no QU is counted by no bucket (before: the balance was 100 higher than the buckets). GetStats.globalUnattributed is unchanged from before the call (the QX refund is unparked).

8. **Steps:** S5: seller S owns a seat in an active, settled holding H with GetHolding(H).shares = 100. S calls OfferPosition{holdingId=H, costUnits=C, askPrice=A}. Then GetPosition{holdingId=H, member=S}. Then change the holding's shares without touching the offer (SELL_QX Execute that sells 40 of them, or a ReconcileHolding that raises them) and call GetPosition again.
   **Expect:** OfferPosition returns 0. First GetPosition: offeredCostUnits = C, askPrice = A, offeredShares = 100. After the change: offeredShares equals the new GetHolding(H).shares (for example 60) with no re-offer (before: still 100). The output layout is unchanged, so an existing client reads the same field positions.

9. **Steps:** S5: buyer B calls BuyPosition{holdingId=H, seller=S, expectedCostUnits=C, expectedShares=100 (stale)} attaching A plus the join fee. Then B calls it again with expectedShares=60 (the value from GetPosition).
   **Expect:** Stale call: returnCode 38 (OFFER_CHANGED), all attached QU refunded, no state written. Second call: returnCode 0 and the seat moves to B, with no re-offer by S (before: 38 on every attempt after the share change until S re-offered).

10. **Steps:** S5 layout: compare the contract state size or the Offer record size on the testnet build against the previous testnet build.
   **Expect:** Offer is 24 bytes and StateData is 262,144 bytes smaller than base_K7 (190,201,504 against 190,463,648, measured by compiler static_asserts). A fresh deployment is needed, as with any layout change.

11. **Steps:** S6: no behaviour change.
   **Expect:** Nothing to run; the comment now matches the code.

## CancelQxBid

1. **Steps:** S0 setup (devkit or testnet): asset X = a token issued by some wallet and QX-managed, with the issuer holding shares to sell. Pool A (creator CREATOR) with ALICE 600,000 and BOB 400,000 uncommitted; pool B (creator DAVE) with CAROL 300,000 and DAVE 200,000. Let at least one tick pass between a contribution and a Propose (money added at the tick a proposal opens has no vote on it). Read the contract entity balance (RequestEntity) and GetStats/GetPool before each step.
   **Expect:** Equality that held in every test: contract balance == sum over pools of uncommittedTotal + pendingRevenue + globalUnattributed + dividendEscrowTotal + ipoRefundEscrow + qpayBuyPot (+ unclaimed payouts, none here). On a busy testnet compare DELTAS instead (steps below).

2. **Steps:** C1 resting bid with an empty ask book: ALICE Propose(poolId=A, kind=2 BUY_QX, asset=X, quAmount=150000, shares=100, price=1000, holdingId=0); ALICE and BOB Vote(approve=1); after QPOOL_VOTE_WINDOW ticks Tally; Execute(proposalId).
   **Expect:** Execute returnCode 0, acquired 0, refunded 50000, holdingId = H. GetHolding(H): shares 0, pendingShares 100, costBasis 100000, qxBidPrice 1000, qxBidTick = the tick of the Execute. GetSource(issuer of X, kind 0): pendingQx 100. GetPosition(H, ALICE).cost 60000, BOB 40000. GetMember: ALICE uncommitted 540000, BOB 360000. QX AssetBidOrders(X) shows 100 @ 1000 owned by the QPOOL entity. Balance fell by 100000 and pool A uncommittedTotal fell by 100000 (unaccounted stays 0).

3. **Steps:** C2 a second pool is blocked: CAROL Propose(poolId=B, kind=2, asset=X, quAmount=100000, shares=50, price=1500, holdingId=0).
   **Expect:** returnCode 25 (QX_BID_PENDING). Nothing written.

4. **Steps:** C3 creator cancels and the second pool can buy: record balance B0 and pool A uncommittedTotal U0; CREATOR CancelQxBid(poolId=A, holdingId=H) with no QU attached.
   **Expect:** returnCode 0, cancelledShares 100, refunded 100000, pendingLeft 0. Balance == B0 + 100000; pool A uncommittedTotal == U0 + 100000 and committedTotal 0; GetStats.globalUnattributed UNCHANGED; ALICE uncommitted 600000, BOB 400000, committed 0 for both; GetHolding(H).found 0; GetSource(X issuer, 0).found 0; no QPOOL bid on QX. Then CAROL Propose(poolB, BUY_QX, X, 100000, 50, 1500, 0) now returns 0; after Vote/Tally/Execute the new holding takes slot H and GetHolding(H).qxBidPrice is 1500 with a new tick; pool A is now blocked with 25.

5. **Steps:** C4 stranger refused before the age limit, allowed after: with a bid resting in pool A (repeat C1), OUTSIDER (no seat, not a creator), CAROL (member of pool B only) and DAVE (creator of pool B) each call CancelQxBid(A, H); attach e.g. 777 QU once. Then wait until tick - qxBidTick == QPOOL_QX_BID_MAX_AGE_TICKS exactly (72 * QPOOL_TICKS_PER_HOUR; 259,200 on production values, 144 on the devkit's TICKS_PER_HOUR 2) and call again; then one tick later.
   **Expect:** All three: returnCode 22 (NOT_AUTHORIZED), the 777 comes back, GetHolding/GetSource/GetStats unchanged. At age == limit: still 22. At age limit + 1: OUTSIDER gets returnCode 0, cancelledShares 100, refunded 100000, and OUTSIDER's own balance does not change; the refund lands on ALICE (+60000) and BOB (+40000). Also: CancelQxBid(poolB, H) with pool B names the wrong pool -> 3 (NOT_FOUND); CancelQxBid(A, 1048576) -> 1 (INVALID_INPUT); after a cancel a second CancelQxBid(A, H) -> 3.

6. **Steps:** C5 cancel after a partial fill: OUTSIDER places AddToAskOrder(X, price 1000, 60). Pool A BUY_QX quAmount=200000, shares=100, price=1500 through Execute. Then CAROL AddToAskOrder(X, price 1400, 10) (fills 10 silently against the resting bid). BOB calls CancelQxBid(A, H).
   **Expect:** Execute: acquired 60, refunded 80000, shares 60, pendingShares 40, costBasis 120000, ALICE cost 72000, BOB 48000, qxBidPrice 1500. Cancel: returnCode 0, cancelledShares 30 (40 minus the 10 filled), refunded 45000 (30 x 1500), pendingLeft 0. GetHolding(H): shares 70 (the 10 fill credited by the same call), pendingShares 0, costBasis 75000, active 1, qxBidPrice 0; GetSource pendingQx 0 and recognized 70. GetPosition: ALICE cost 45000, BOB 30000. ALICE uncommitted 555000, BOB 370000; pool committedTotal 75000. If ALICE had an OfferPosition open on H before the cancel it is gone (offeredCostUnits 0, pool offerCount -1). Balance +45000, globalUnattributed unchanged.

7. **Steps:** C6 cancel when the bid already filled completely: repeat C1, then a counterparty sells all 100 into the bid (AddToAskOrder X 100 @ 900), do NOT call ReconcileHolding. CREATOR (or anyone with the right) calls CancelQxBid(A, H), attaching e.g. 5000 QU. Then ReconcileHolding(H).
   **Expect:** CancelQxBid returnCode 40 (NO_PENDING_BID), cancelledShares 0, refunded 0, the 5000 comes back, and GetHolding/GetSource/GetStats are byte-for-byte as before the call (still pendingShares 100 and pendingQx 100 until someone reconciles). ReconcileHolding then returns 0: shares 100, pendingShares 0, qxBidPrice 0. A CancelQxBid on it now returns 40 again.

8. **Steps:** C7 QU conservation, any scenario above: record (a) contract entity balance (incoming - outgoing), (b) the sum over pools of GetPool.uncommittedTotal, (c) GetStats.globalUnattributed, (d) GetStats.pendingRevenue, immediately before and after the CancelQxBid.
   **Expect:** delta(a) == delta(b) == refunded; delta(c) == 0 (the QX refund the hook parked is taken back out); delta(d) == 0 in every normal case (it can only rise by a unit or two if a holding that keeps a unit of cost or a row set that does not add up leaves a leftover). Across the whole create/bid/cancel cycle the balance returns to its value before the bid.

9. **Steps:** C8 failure atomicity (optional, needs a hand-corrupted state or a QX where the order was removed by other means): when QX holds fewer shares at (asset, recorded price) than the books say pending.
   **Expect:** CancelQxBid returns 11 (EXTERNAL_CALL), cancelledShares 0, refunded 0, pendingLeft = the quantity asked of QX; no state changed (not even a pending fill credit) and no QU moved. Verified in my gtest by raising pendingShares to 50 while 40 rest on QX.

10. **Steps:** C9 contract-share squat release: BUY_QX of contract shares (asset {NULL_ID, name}, holdingId = the paying contract's index above 30) at price 1, shares 1, quAmount 1 so it rests; a stranger calls CancelQxBid before and after the age limit.
   **Expect:** Before: 22. After: 0 with cancelledShares 1, refunded 1; GetHolding.found 0, GetSource(id(index,0,0,0), kind 1).found 0, and the name -> index binding in contractNames is gone, so the honest pool can register the real (index, name) pair. (A squat bid that filled even one dust share is NOT freed by this.)

11. **Steps:** C10 sold-out holding with a bid still resting (retire path): after C5's Execute (60 shares + 40 resting), place a QX bid of 60 @ 1600 from another wallet, run a SELL_QX proposal of 60 shares (limit 1500, quAmount 90000) to Execute, then CancelQxBid(A, H), then ClaimPayout(H) by ALICE and BOB.
   **Expect:** After the sale: shares 0, pendingShares 40, costBasis 120000, totalPayouts = proceeds. Cancel: 0, cancelledShares 40, refunded 60000; GetHolding(H): found 1, active 0 (retired, not freed), costBasis 60000, rows 2; ALICE uncommitted +36000, BOB +24000; ledger for X gone. The two ClaimPayout calls pay the whole proceeds 60:40 (within 2 QU of dust) and the second one frees the slot: pool committedTotal 0, holdingCount 0.
