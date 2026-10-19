# QPOOL testnet candidate

Files here:
- `QPool.h`: the build with your decisions applied. It is the verified K6 build plus the edits listed in `CHANGES-SINCE-K6.md`.
  Compiles with 0 errors attributed to the contract; forbidden-syntax scan clean; StateData 190,201,504 bytes.
  The new edits have NOT been run anywhere. The testnet is their first test.
- `QPool_testnet.h`: the same file with ONE line changed, `QPOOL_TICKS_PER_HOUR = 2` (production 3600). Every window derives
  from it: vote 144 ticks, execute 96, IPO vote 48. Use it on the testnet and `QPool.h` on mainnet.
- `QPool-K6-verified.h`: the last build that passed independent simulated verification (263 of 263 tests), kept to diff against.
- `CHANGES-SINCE-K6.md`: exactly what changed, what is decided but not built, and testnet setup notes.
- `README-K6.md`, `CHANGELOG-K6.md`: describe K6; where they differ from CHANGES-SINCE-K6.md, that file wins.

Your repo's own `QPool.h`, `README.md` and `contract_qpool.cpp` are untouched.

## Wiring (current upstream core)
- QPAYHUB 29, QTREAT 30, QLOAN 31 (construction epoch 236). QPOOL needs index 32 or later and its own construction epoch.
- QPAYHUB must exist at 29 on the testnet, or point `QPOOL_QPAYHUB_CONTRACT_INDEX` at a test contract that books plain QU transfers.
- QPAY is a mainnet token. On the testnet, issue a test token on QX and replace the `ID(...)` block in `BuyAndBurnQpay` and
  `QPOOL_QPAY_ASSET_NAME`.
- Fee reserve after the IPO is `finalPrice x 676`; watch it throughout.

## Test order (what a real chain proves that the simulation could not)
A. Basics: CreatePool (10,000,000 QU), Contribute (first time pays 10,000), minimum contribution 10,000, public/private switch,
   creator-only DepositHoldingAsset, WithdrawUncommitted, a refused call refunds its reward.
B. Governance with real QX: Propose BUY_QX, Vote yes/no/abstain, Tally, Execute; ClaimPayout after a real issuer pays;
   SELL_QX; seat market (OfferPosition, BuyPosition with 1 percent fee, vote locks).
C. IPO (needs a contract in its IPO epoch):
   1. Propose BUY_IPO: budget and opening price only. Vote 48 ticks here. Execute bids all-in at the opening price.
   2. Outside bidders outbid the pool. Check the refund arrives inside the bid transaction and the books free it at the next RebidIpo.
   3. RebidIpo several times, reusing a price. Funded only by money already returned. A very high price buys few seats.
   4. Epoch change: END_EPOCH snapshot runs before finishIPOs. SettleIpo each holding in both orders with two pools; per-member
      refunds must add up to the budget.
   5. A failed IPO (fewer than 676 bids) returns everything. Two IPOs in one epoch.
D. END_EPOCH revenue (new): feed revenue (pool fee, join fees, a stray transfer, a donation) then change epoch. Check:
   40 percent paid per share, 30 percent arrives at QPAYHUB, 20 percent appears in `qpayBuyPot` (GetStats), 10 percent plus
   leftovers burned, `pendingRevenue` back to 0. Try revenue of 1, 100, 844, 845 and 1,000,000 QU. Check a stray wallet
   transfer lands in `pendingRevenue` (not `globalUnattributed`) and QX/QSWAP in-call payments still do not.
E. BuyAndBurnQpay (new): empty pot returns NOTHING_TO_BUY and writes nothing; no asks; asks above 10,000 untouched; several
   price levels bought lowest first at their own prices; more than 8 levels (rest waits); QX management hand-over and the
   burn to the zero key: the QPAY supply must drop by exactly the burned amount; a failed hand-over leaves `qpayUnburned`
   and the next call retries; the pot is never touched by END_EPOCH.
F. Dividend intake: qpiDistributeDividends from a contract that pays QPOOL shares; plain QU from a token issuer.
G. Cost: reserve drained per call; node CPU per dirty tick with a 190 MB state; the 1 QU stray-transfer every tick case
   (known, not fixed; see CHANGES-SINCE-K6.md); behaviour with the reserve at or below zero.
H. Windows in real ticks against the nominal one second.

## What to send back
For any failure: the transaction, the return code, and the read-backs (GetPool, GetHolding, GetIpoBids, GetStats).
I will fix from those, without running simulations.
