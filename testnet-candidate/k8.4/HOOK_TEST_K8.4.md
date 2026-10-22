# K8.4: first thing to watch on the testnet — the dividend hook that calls QX

Use `QPoolK8.4-devkit.h`. This is the one part of K8.4 that has never run on a node. In the local harness it works (exact splits, no hang), but the harness is not a node.

## What is new and why it can fail
When a contract pays a dividend on shares QPOOL holds, QPOOL's `POST_INCOMING_TRANSFER` runs inside the paying contract's `distributeDividends`. On a token that has resting orders it now reads QX's books (`EntityBidOrders`, `EntityAskOrders` of QPOOL) to split the payment. Ledgers without orders do not call QX (same path as K8.3 and before).

What could go wrong on a node: the call is refused or aborts, the payment takes noticeably longer, or the paying contract reports an error. If QX cannot be read the hook falls back to a proportional split (nothing is lost), but an abort inside the callback is a different thing and would show as a contract error.

## The smallest test (about 10 minutes of actions)
1. A contract whose shares QPOOL can hold and that pays dividends (the test contract of your devkit). One pool buys 100 of its shares on QX (an outsider sells them into the book), so QPOOL holds 100 recognised shares.
2. **Control, no orders resting:** the contract pays a dividend. Read `GetSource` (acc), `GetPosition.owed` of the seats, `GetStats.globalUnattributed`. This is the K8.3 path; it must behave exactly as before.
3. **With a resting sell only:** a second pool buys 100 shares and rests a sell (`SELL_QX_LIMIT`, a price nobody takes). Pay a dividend. Expect: half the net to the first holding, half set aside for the sell (`GetHolding` of the sell's holding is not showing escrow, read it with the state/`GetPosition.owed` after a `ReconcileHolding`).
4. **With a resting bid as well (the new case):** the first pool rests a bid for 50 more shares below the sell's price. Pay a dividend, let an outsider fill the bid, pay a second dividend, call `ReconcileHolding` on any of the holdings. The expected split of this exact scene is in the guide, case V4 (198,000 net split 99,000 / 99,000; then 49,500 / 24,750 / 49,500).

## What to record
- Did every `distributeDividends` succeed, and did the paying contract or QPOOL report any contract error? (This is the main answer.)
- The time of the payment tick with orders resting versus without (the hook makes two QX reads per possession record: one page of 256 orders each while QPOOL has at most 256 orders resting, up to five pages each beyond that).
- `GetStats.globalUnattributed`, `restingBids`, `restingAsks` before and after.
- The credited amounts against the guide's V4 numbers (each within 3 QU).

## If it fails
Send me what the node logged for QPOOL or the paying contract and the tick it happened at. The rule (the owner's) stays: QX is the authority for every QX buy and sell, so the fix would be on the QX side (how the hook reaches QX, or what it reads), never a replacement of QX's books by QPOOL's own booked numbers. If a QX read fails inside the hook (QX in an error state), the hook does not guess either: it parks the slice (`GetSource.unresolved` shows it, `unresolvedRateX64` in the ledger records what a share of it is worth) and the next `ReconcileHolding` (anyone), `CancelQxBid` or `CancelQxAsk` of that ledger splits it per share (at that worth) with QX's books once QX answers; until then no new holding may join the ledger (`SETTLEMENT_PENDING`).
