# What changed since the verified K6 build

`QPool.h` here = the verified K6 build (`QPool-K6-verified.h`) plus the edits below. The edits were checked only by the
compiler (0 errors attributed to the contract, forbidden-syntax scan clean) and by reading. No simulated test was run
on them, at the owner's request: the testnet is the test. The old gtest file `contract_qpool.cpp` still encodes the old
80/20 split and the old hook and will fail on those tests; it was not updated.

## Implemented (owner decisions)
1. **No minimum payout.** `QPOOL_MIN_DISTRIBUTION` (676,000) is gone. END_EPOCH splits all pending revenue every epoch.
2. **Split 40 / 30 / 20 / 10.**
   - 40% to the 676 QPOOL shares (floor per share).
   - 30% as a plain `qpi.transfer` to the QPAYHUB contract (`QPOOL_QPAYHUB_CONTRACT_INDEX = 29`). QPAYHUB books plain
     transfers into its dividend pool.
   - 20% moves into `qpayBuyPot`, an earmark inside the contract balance. It is not revenue and END_EPOCH never pays it out.
   - 10% plus every rounding leftover is burned into the fee reserve. A leg that cannot be paid also falls into the burn.
   - Revenue under 1,690 QU in an epoch pays no shareholder (40 percent of it divides to less than 1 QU per share); that part
     falls into the burn. The 30 and 20 percent legs are still paid.
3. **Unattributed money becomes dividends.** In the incoming-transfer hook, anything that matches no ledger now goes to
   `pendingRevenue` and so through the split above: a plain wallet transfer, a contract dividend with no ledger, a
   revenue donation. Nothing is ever sent back. The one exception is `qpiTransfer` from QX or QSWAP, which stays parked in
   `globalUnattributed` because the procedure that made the call accounts for it and hands it back (UnparkArrivals).
4. **`BuyAndBurnQpay` (new procedure 22, permissionless).** Spends `qpayBuyPot`. Reads QX's asks for the QPAY token
   (issuer `QPAYNOWSWZMGHFEAEVJXGZAVSHABAZDDBDIHTEBOPCOGHRGBCYCUZOHCVLXG`, name `QPAY`) from the lowest price up, and for
   each price level at or below `QPOOL_QPAY_MAX_PRICE = 10000` places one exact bid, at most 8 levels per call. No
   percentage spend limits. Then takes the shares' management over from QX (fee 100 QU, from the pot) and transfers them
   to the all-zero key, which the core treats as a real burn. Anything unburned is retried by the next call. If nothing is
   on offer the pot waits for any number of epochs. A call that does nothing writes nothing.
   `GetStats` gained `qpayBuyPot, qpayUnburned, totalQpayhubDeposited, totalQpaySpent, totalQpayBurned` (appended).

## Decided but NOT in this build
- **Stateless hook for stray transfers.** The audit's cost vector (a 1 QU transfer every tick makes the node re-hash the whole
  state every tick) is NOT closed: each stray arrival now writes `pendingRevenue`. Closing it needs an exact total of
  everything the contract owes, so that END_EPOCH can derive the surplus from the balance. That is the riskiest accounting
  in the project (a mistake pays members' money out as dividends) and I did not write it without verification.
- **Pool-paid external fees.** SELL_QSWAP's 100,000 QU fee and QX's 100 QU hand-over fee are still fronted from the pooled
  balance and netted out of the sale's proceeds, as in K6. Debiting the pool's members pro rata before the call is not done.
- Vote-gated cancel of resting QX bids; the shared SELL plan and venue liveness probes.
- Recovery for an IPO holding that won seats under a name nothing proves.
- Fee-reserve hardening, including the rebid dust loop.
- QLOAN (index 31) in the table of known dividend payers; port of the test harness to the current core.

## Testnet notes for the new parts
- **QPAYHUB must exist at index 29 on the testnet.** Otherwise the 30% leg is transferred to an address nobody controls.
  If it does not exist, change `QPOOL_QPAYHUB_CONTRACT_INDEX` to a test contract that books plain transfers.
- **QPAY is a mainnet token.** On a testnet, issue your own test token on QX and replace the `ID(...)` issuer block inside
  `BuyAndBurnQpay` and `QPOOL_QPAY_ASSET_NAME` with it. List asks on QX at prices at and above the ceiling to test both sides.
- The README and CHANGELOG here describe K6. Where they mention an 80/20 split, a 676,000 threshold, or `GetStats` fields,
  this file wins.
