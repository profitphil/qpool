# QPOOL — what changed against the previous package

For the contract owner. This compares the package in this folder (`QPool.h`
6,016 lines, 263 tests, `README.md` rewritten) with the previous one (`QPool.h`
2,633 lines, 27 tests, the old `README.md`). It says what users, members and
integrators will notice, in plain words. `README.md` has the exact rules; section
numbers below point into it.

**Status:** everything here was checked in a unit-test harness (all 263 tests
pass, README section 5). None of
it has run on a devnet, a testnet node or the MSVC build, and the two reports in
`reports/` describe the previous contract.

---

## 1. The big fix: shared dividends are now split fairly

**Before:** when two pools held shares of the same asset — two pools that each won
seats in the same contract IPO is the normal case — the pool that registered first
took the **whole** dividend (less the 1% fee) and the other got **nothing**.
The old README and the live reports called this intended; it was a bug.

**Now:** every pool that holds the paying asset is paid in proportion to its
shares. Example: pool A holds 300 shares and pool B 200 of the same contract. A
1,000,000 QU dividend nets 990,000 after the 1% fee; A's holding earns 594,000 and
B's 396,000 (each pool's members then split their pool's amount by their cost).
The same holds for a token held by several pools, and for shares that came in
different ways (won in an IPO, bought on QX, deposited) — README section 4c.

What members will notice because of this:

- **Dividends on IPO-won shares wait for `SettleIpo`.** Until every IPO holding of
  that contract has been settled, the contract's dividends are held, then released
  by shares when the last one settles. This includes a pool that is the only IPO
  holder: its first dividend used to be credited at once and now waits. `SettleIpo`
  is open to anyone and can be called as soon as the IPO window has closed — so
  somebody has to call it (the runbook, README section 9).
- **Seat sales and sell executions pause while a payer has dividends set aside.**
  `BuyPosition` and `Execute` of a sell return the new `SETTLEMENT_PENDING` (24)
  until `SettleIpo` or `ReconcileHolding` has run. Claims, offers and cancels never
  pause. A sell proposal simply stays PASSED and can be retried inside its window.
- **QX bids: one open bid per payer.** A second `BUY_QX` on the same payer is refused
  with `QX_BID_PENDING` (25) until the first has been reconciled. A bid that never
  fills keeps blocking, and there is still no way to cancel it.
- **Fills of a QX bid are recognised by `ReconcileHolding`**, which now serves QX
  bids only (it refuses IPO holdings with `WRONG_STATUS`). Dividends earned by shares
  that have filled but are not yet reconciled are set aside for that bid's holding, not
  handed to its siblings, and paid when it reconciles.
- **One asset per payer.** A second asset from an issuer already held is refused
  (`SOURCE_CONFLICT`, 23) while the first is held.
- **`SettleIpo` can be run once per holding** (a second call is refused) and now
  also binds the contract's asset name when it wins shares. A pool that declared a
  different name than the one bound wins nothing.
- **`ALLOCATE` (proposal kind 6) can no longer pay anything.** Nothing feeds the
  unattributed balance it drew on any more, so a passed `ALLOCATE` returns
  `NOTHING_OWED`. It is kept so ids and numbering do not move.
- Escrow left behind when a payer's last holding leaves is booked as protocol
  revenue (`totalUnownedSwept`).

## 2. Contract shares (shares of contracts like QX or QUTIL)

New, and asked for by the owner:

- **They earn.** IPO-won, QX-bought and deposited holdings of one contract all earn
  from the same ledger and split its dividends by shares. Nothing in QPOOL needs
  updating when a new contract is added to Qubic.
- **Pools can buy them on QX.** A `BUY_QX` proposal for `{NULL_ID, name}` now carries
  the paying contract's index in `holdingId` (1 to 1,023, not QPOOL's own).
- **They can be deposited.** `DepositHoldingAsset` now takes a trailing
  `contractIndex` (required for contract shares, 0 for tokens); a private pool owner
  can deposit their own contract shares and sell parts of the seats to entrants.
- **They can only be sold through QX.** `BUY_QSWAP` of them is `INVALID_INPUT`;
  `SELL_QSWAP` of them is the new `QX_ONLY` (27), at `Propose` and again at `Execute`.
- **Name and index are checked as far as that is possible.** For contracts 1 to 30
  the pair must match a table embedded in the contract (copied from the explorer on
  2026-10-17), otherwise `CONTRACT_MISMATCH` (28). For later contracts the link cannot
  be proved on chain: QPOOL keeps it one-to-one, requires the contract to exist and not
  be mid-IPO (`CONTRACT_NOT_LIVE`, 29), and otherwise **the vote is the only check** —
  voters must verify the index of an unlisted contract themselves (README 4d).
- IPO proposals can no longer declare a share name that the table lists for another
  contract, or that is already an issued asset, and the target index is limited to
  1,023.
- **Check the table against your network.** It matches the local core checkout at
  indices 1 to 28 and differs at 29 and 30 (QPAYHUB / QTREAT in the explorer; QTREAT /
  QPOOL in the checkout).

## 3. Buying

- **Members are charged only what was really spent.** QX and QSWAP hand back what
  they do not use (QX: the excess over price × shares and every price improvement; a
  refused order: everything). Before, that refund landed in the unattributed counter
  while the holding's cost still counted it as spent — in the measured example
  80,000 of a 200,000 QU `BUY_QX`. Now the refund stays in the members' uncommitted
  balances, the holding's cost is the amount spent (split exactly, to the QU, over the
  members), and `Execute` reports it in the new `refunded` field (README 4f).
- **A failed buy leaves no trace.** Before, capital was moved into "committed" before
  the call to QX/QSWAP/IPO, so a failed call could strand state. Now a refused order, a
  swap that bought nothing, or a call that fails returns `EXTERNAL_CALL` with no capital
  moved, no holding created, and the proposal still PASSED.
- `BUY_QX` and `BUY_QSWAP` of an asset that is not issued are refused at `Execute`.
- A non-zero `holdingId` on a token `BUY_QX` / `BUY_QSWAP` is now refused (it used to be
  ignored): the field only ever carries a contract index.
- A newcomer cannot buy a seat when the pool's roster is full (`CAPACITY`, the buyer
  is refunded).
- The budget of a `BUY_QX` should cover price × shares, and of a `BUY_QSWAP` exceed
  QSWAP's 100,000 QU fee; otherwise the order is refused, nothing changes, and the vote
  was wasted (README 4a).

### 3b. Membership is one fact, and every way in pays the join fee (audit step K1)

- **Before:** `BuyPosition` put a buyer on the pool's roster *without* a Contribution
  record; if he then topped up with `Contribute` he was listed a second time, so
  `Execute` charged him twice (a live run paid out 1,320,000 QU against a 910,000 QU
  mandate, the surplus out of other pools' money), `SettleIpo` refunded him twice and
  `Tally` released his vote lock twice. A record-less buyer on a full roster could
  neither contribute nor propose. `BuyPosition` and `DepositHoldingAsset` also joined
  for free, so a roster could be filled with throwaway identities for a few QU.
- **Now:** a member is on the roster exactly when he has a Contribution record, both are
  created by one helper, and `Contribute`, `BuyPosition` and `DepositHoldingAsset` all
  use it. **The 100,000 QU join fee is charged once per (address, pool) whichever way he
  arrives:** a seat buyer who is not a member attaches the ask price **plus** the fee
  (`INSUFFICIENT_FEE` and a full refund otherwise), a depositor who is not a member
  attaches the fee (what is attached beyond it comes back). Members pay nothing extra.
  A newcomer on a full roster is refused with `CAPACITY` before anything moves, by every
  route (a depositor used to be let in as an unlisted owner).
- `Execute` refuses a buy whose members' buy-ins add up to more than the mandate
  (new error code 31, `BOOKS_INCONSISTENT`, never expected), and `Execute`, `SettleIpo`
  and `Tally` act on a member once even if the books ever listed him twice.
- **Cost:** the Contribution record grew by 8 bytes (his roster position): the state was
  2,097,152 bytes larger, 190,231,280 bytes in all at that step (step K2 made it smaller, section 7). Membership questions no longer scan
  the 256-entry roster (one hash lookup), and `BuyPosition` no longer scans it twice.
- A `BuyPosition` that is refused because the holding is frozen (`SETTLEMENT_PENDING`),
  the buyer is not admitted, the roster is full or the join fee is not covered is decided
  **before** the holding is synced (the freeze is read with the read-only `HoldingEscrow`),
  so it writes nothing: no whole-state re-hash for the owner. The order of the refusals is
  unchanged. Only a stale offer and a full ownership table are still found after the sync.
- Integrators: a `BuyPosition` or `DepositHoldingAsset` client must attach the fee for
  a newcomer (README 4g, 6b, 6c). Contract ABI is otherwise unchanged.

### 3c. IPO bids: bounded as the core bounds them, placed once, one holding per price (audit step K2)

- **Before:** `Propose`, `Execute` and `RebidIpo` multiplied price by shares in plain 64-bit
  arithmetic and passed the share count on to a core that keeps only its low 16 bits and takes
  price × quantity out of the contract's WHOLE balance, from every pool. A crafted share count
  booked a bid cost of 10 QU while the core took 655,370 QU (another one: booked 57,700, taken
  100,000,100). `RebidIpo` needed no membership and no vote, so a stranger could raise any IPO
  holding's bid to any price and quantity his free budget allowed, or fill its list of 8 prices
  with junk so that nobody could raise it again. Two pools could bid at the same price (the
  check ran only when a proposal was opened), after which the pools' slots were
  indistinguishable and the settlement gave the shares and the refund to whichever pool settled
  first. `Propose(BUY_IPO)` walked all 131,072 holdings to look for a price collision.
- **Now:**
  - `shares` must be 1 to 676 and `price` 1 to 1,479,289,940,828 (the core's own limits), checked
    in `Propose` and again in `Execute` before anything is multiplied; the cost is computed
    with a saturating multiply. 676 slots at the highest price (999,999,999,999,728 QU) are bid,
    seated and settled in a test.
  - **The bid is placed once, by `Execute`, at the price the vote named. `RebidIpo` is deleted**
    (owner decision D1; procedure id 13 stays reserved). *(Steps KI and KI2, section 3f, reinstated `RebidIpo` under the owner's final decision on D1,
    bounded by the budget the vote names and by money alone; the paragraph below is the state after K2.)* Under the IPO's uniform clearing price a
    bid at the voted maximum cannot overpay, so nothing is lost by not raising it. A pool that
    wants a higher price proposes again. The unspent part of the budget stays with the
    members and comes back at `SettleIpo`.
  - A registry of (target contract, price) keeps the unsettled IPO holdings of one IPO on
    different prices. `Propose` looks it up (advisory), `Execute` is the binding check
    (`IPO_PRICE_TAKEN`; the proposal that loses the race stays PASSED until it expires),
    `SettleIpo` removes the entry.
  - `Execute` measures the contract's balance around the bid: the QU that left can never be
    more than price × quantity. If it is (the core would have changed), the holding is recorded
    anyway, the proposal is used up and the call returns `BOOKS_INCONSISTENT`.
  - `Execute` of a `BUY_IPO` outside the IPO epoch is refused first, with `IPO_REJECTED` (it used to
    search for a free holding slot and run the roster pass before the core said no); a `ReconcileIpo`
    with nothing to book no longer rewrites the holding; one that finds more slots at the
    holding's price than it funded reports `BOOKS_INCONSISTENT` instead of returning success.
- **Not fixed in K2, fixed in K4** (section 3e): who gets which refund and which shares when two pools bid in one IPO
  used to depend on the order in which they were settled.
- **Cost:** the state is 6,258,672 bytes smaller (section 7). Integrators: a client of procedure 13
  must go; `Propose` of a `BUY_IPO` outside the limits is refused.

### 3d. Votes bind the people who cast them (audit step K3)

- **Before:** (a) A sell or allocate vote weighed the voter's seat units *as they were when he voted*, and seats move freely during a
  vote: a holder with 5 percent could vote, sell his seat for 1 QU to a fresh wallet, which joined the pool for free and voted with the same
  units, and so on; after about twenty hops the same 50,000 cost units outweighed the honest 95 percent and a sale of the whole holding passed.
  (b) A buy vote weighed the voter's balance at the moment he voted, including money he had deposited after the proposal opened; his lock
  ended at `Tally`; and `Execute` charged *everybody's* balance pro rata except explicit NO voters. So a member with 60 percent could
  propose the whole pool, vote, take his money out after `Tally`, and the 40 percent who never voted paid all of it; a stranger's
  flash deposit decided or vetoed any open proposal; money added between `Tally` and `Execute` was drafted too. (c) The execute window
  started when somebody called `Tally`, so a passed vote that nobody tallied could be tallied and executed weeks later. (d) A budget of
  exactly `price × shares` died by rounding (three members: 99,999 of 100,000 assigned, QX refused the order for 1 QU); a NO voter or a
  1 QU withdrawal after `Tally` killed a mandate for good. (e) Vote rows were keyed by the proposal's slot and cleaned with the roster of
  the pool that reused it, so a member of another pool kept his old row: a late joiner was refused `ALREADY_VOTED`.
  (f) A proposal nobody ended blocked its proposer for ever.
- **Now:**
  - **A voted seat stays with its owner until the vote window ends** (`SEAT_LOCKED`, 32, on `OfferPosition` and on `BuyPosition`
    against it; nothing is written and the buyer's QU comes back). Units sold before the vote vote once, with their new owner.
  - **Only money that was in the pool a tick before a buy opened has a vote on it** (`DEPOSIT_AFTER_OPEN`, 33): a `Contribute` or a
    `SettleIpo` refund at the tick of the proposal or later is refused on that proposal. Deposit one tick earlier.
  - **A buy charges only the members who voted YES**, each for at most the weight he voted with and at most what he still has. Abstainers
    and NO voters are never drafted (this reverses the old rule that abstainers were; owner decision D3). A YES voter's money is locked
    until the mandate is carried out or its life is over (a NO voter's until `Tally`); the lock lapses by itself, nobody has to call anything.
  - **The order is sized to the money:** `min(voted size, what the YES voters' money buys)` shares at the voted limit price; a swap's
    minimum out is scaled to the money and rounded up, so the price limit never widens; the money is split over the payers by running
    total, so an exact budget works to the QU. A mandate whose money cannot buy one share is closed once as FAILED and frees its proposer
    (a market refusal still leaves it PASSED for a retry).
  - **The execute window is anchored at the end of the vote** (`openTick + 3 days`, 2 days long), whenever `Tally` runs, and a vote that
    would have passed but is tallied after 5 days is EXPIRED at once. After 5 days an untallied or unexecuted proposal blocks nobody:
    its slot is reused and its vote rows deleted by the next `Propose`.
  - **Vote rows are keyed by the proposal, not by its slot**, and deleted when the vote ends (`Tally`, `Execute`, or the slot's next
    `Propose`), so the vote table no longer fills with dead rows.
  - A SELL or ALLOCATE remembers its holding's pool and asset; `Vote` and `Execute` answer `NOT_FOUND` when the slot has been reused by
    something else (a holding that lost its IPO has its slot reused; since the K5 repair the proposal records the holding's incarnation, so even the same asset created in the slot later is refused, 3h). `ALLOCATE` is still dead (README 10).
- **Not changed:** quorum (50 percent of the weight that was eligible), the 3-day vote, the 2-day execute window (now measured from the
  end of the vote), one live proposal per member, and that the caller of `Execute` chooses the moment. `Vote`, `Tally` and `Execute` still
  name a proposal by its slot, so a transaction built for one proposal can land on a later one in the same slot.
- **Cost:** the state is 8,388,608 bytes larger (4.6 percent: section 7). A voted seat cannot be sold for up to 3 days; a YES voter's money
  cannot be withdrawn until `Execute` or the 5th day; a member who tops up loses his vote on the proposals opened before the top-up.
  Integrators: `GetMember` and `GetPosition` return one more field each; two new error codes.

### 3e. IPO settlement from an END_EPOCH snapshot (audit step K4)

- **Before:** when a pool settled its IPO holding, `SettleIpo` (a) drew `min(what the holding had at risk, the shared refund pot)`, so
  the holding that settled FIRST took refunds that belonged to another pool (measured: pool A owed 180,000 and pool B nothing; if B settled
  first B got 200,000 back and A 100,000 instead of 20,000 and 280,000), and the whole principal of a FAILED IPO could be drawn by the winner of
  another IPO; (b) took its shares as `min(shares QPOOL holds in all - shares the ledger counts, its own stale count of seated slots)`, so after
  an eviction nobody had reconciled the first settler took another pool's shares and refunds and the loser lost its whole stake; (c) treated
  "the book cannot be read" as "the auction is over" (the core answers the same in every other epoch), and checked the window only while capital
  was still booked at risk, so a stranger could cancel an outbid pool's participation in a running auction; (d) retired a holding that had
  shares when its refund happened to cover its whole cost, leaving ghost recognised shares in the ledger that swallowed a share of every later
  dividend; (e) counted shares under whatever name the vote had declared: a typo or a deliberate wrong name made the holding win nothing and retire
  with its capital on the books, the real shares sitting in QPOOL owned by no holding, and with overlapping IPOs the first settler could bind
  another contract's name; (f) left up to roster - 1 QU of every refund unowned.
- **Now:**
  - **The book is read once, at the last moment it can be read.** `Execute` keeps a table of at most 8 IPO targets with unsettled holdings (a
    ninth is refused with `CAPACITY` before anything moves; `Propose` says so up front). At the end of the target's IPO epoch, QPOOL's END_EPOCH
    (the node runs it before `finishIPOs()`, with the epoch still the IPO's) reads the clearing price and counts QPOOL's slots per bid price
    in one 676-slot pass; a price belongs to one holding (K2's registry), so that is the holding's seats. It writes only what there is to store
    and does not wait for the revenue gate. New function `GetIpoTarget` shows the snapshot.
  - **`SettleIpo` works from it** and from nothing else that depends on order: the holding gets back its never-bid budget plus
    `bid - seats × clearing price` (clamped to what the refund pot holds, which is now only a solvency clamp), its cost becomes
    `seats × clearing price`, and exactly its seats are recognised. Settling in any order gives every member the same result
    (three pools, three orders, tested), and a failed IPO's principal always reaches its own pool.
  - **The auction is over only when the snapshot says so** (and the epoch has moved on): before that `WINDOW_OPEN`, whatever the book shows;
    if the epoch moved on without a snapshot, the new `IPO_NO_SNAPSHOT` (35). Nothing is written by a refusal.
  - **A holding that has shares is never retired**, and the refund never takes the last unit of its cost; the ledger update treats `retire` as
    authoritative; when the last holding of a ledger leaves, what the ledger took in and nobody was credited is booked as revenue.
  - **The refund is split exactly** over the seats (a running total of cost units): nothing is stranded.
  - **A holding that won seats under a name nothing proves is neither settled nor retired:** `SettleIpo` returns the new `NAME_UNPROVEN` (34),
    writes nothing and keeps the members' capital on the books. A holding that won nothing settles whatever name it declared.
  - **`ReconcileIpo` is deleted** (owner decision D1; procedure id 15 stays reserved). It could only move an evicted stake from one book entry to
    another while the auction ran, which gave the members nothing before the auction ended, left `pendingShares` stale in between and cost 676
    cross-contract reads per call.
- **Cost:** the state is 264,528 bytes larger (the table of targets and the snapshot rows, section 7). END_EPOCH reads at most 8 books once per
  epoch; no loop over pools, holdings or proposals was added.
- **Not fixed here:** a holding that cannot prove its name stalls its ledger (its dividends, and the seat market and sells of the other
  holdings of that IPO once a dividend is parked, stay parked/frozen; nobody can settle it and there is no rename or abandon path), and the name of an UNLISTED contract
  still cannot be proved on chain when two IPO windows overlap (README 4d, 10). Owner decisions.

### 3f. IPO bidding, finalized: a one-day vote, a BUDGET and no ceiling, repeatable rebids funded by returned money, several prices per holding (audit steps KI and KI2; owner decision D1, final)

- **Why:** the owner's rule is that an IPO is won and lost in the last hour of the epoch: a pool is typically outbid and re-bids three to five
  times in the final half hour, and once it has been outbid and its money has come back it must be able to bid again; a pool must never be
  deprived of the chance to keep rebidding. Steps K2 and K4 had removed `RebidIpo` and assumed one bid price per holding. Step KI brought it back but
  bounded it by a **voted quantity** and a **ceiling price**; an independent verifier showed the result: 100 seats voted, 576 outsider slots at 2,500, an
  outsider takes 5 of the pool's seats at 2,100, and because the core evicts the LOWEST slots whoever owns them every rebid of 5 only upgraded 5 of the pool's
  own bottom seats, so after fifteen rebids the pool still held 95 of 100 and the sixteenth was refused with `CAPACITY`: locked out. The owner's final rule
  (step KI2): **the IPO bidding is governed by the total amount of QU allocated, because the higher the IPO price becomes the fewer shares a pool can buy**,
  and there is no ceiling price "because how does the pool know how high the price will go; the only governing factor for IPO bidding is the proposed Q budget".
- **The vote:** a `BUY_IPO` is voted on for **24 hours** (`QPOOL_IPO_VOTE_WINDOW`, 86,400 ticks; every other kind keeps 72 hours). `Vote`, `Tally`,
  the execute window (still 2 days, from the END OF THE VOTE) and the vote locks all ask one private function for the
  window of the proposal's kind (the slot reuse of `Propose` uses the matching lifetime constants inline). **Ticks are nominal:** a tick is one second only at the core's target speed (`TARGET_TICK_DURATION` is 1,000 ms), so
  24 hours is 86,400 ticks only at that speed.
- **The proposal carries a budget and an opening price, nothing else:** `quAmount` is the budget (at most the pool's uncommitted money and at least the opening price: one
  seat), `price` the opening price (1 to 1,479,289,940,828, the core's own limit, a starting point and **not** a limit on later bids). **`Propose_input.shares` is ignored for a
  BUY_IPO** (it stays in the input so that clients keep their layout; any value is accepted): `Propose` stores the opening quantity it derives, `min(676, budget / opening price)`,
  and `GetProposal` shows it to the voters. The ceiling field of step KI (`Propose_input`, `Proposal`, `GetProposal`, `GetHolding.ipoCeiling`, `GetIpoBids.ceiling`, the list) is
  **removed**, as is the holding's voted quantity (`Holding.ipoQuantity`, `GetHolding.ipoQuantity`, `GetIpoBids.quantity` and `remaining`).
- **The opening bid is all-in:** `Execute` takes the budget from the YES voters as for every buy and bids `n0 = min(676, floor(assigned / opening price))` seats at the opening
  price; the rest of the budget (less than one seat at that price) is the holding's free funds. The measured-spend defence is unchanged.
- **`RebidIpo` (procedure 13) is bounded by money and by nothing else,** with no cooldown and no limit on the number of calls: `RebidIpo(holdingId, price, quantity)`.
  - *Who:* the caller must own a seat in the holding (so only a person with money at stake can act; a stranger cannot grief it). A seat owner of any size can spend the holding's
    free funds: the vote approved a budget, not a single bid.
  - *THE MONEY RULE:* a rebid at price p for n seats is allowed when **p × n is at most the holding's free funds at the start of the call** (after the call's own pass over the book has
    turned the refunds of EARLIER evictions into free funds) **and at most the contract's physical balance**; otherwise it is refused, writing nothing. `quantity` 0 means as many seats as
    the free funds buy at that price (at most 676). The refunds of the seats the bid itself pushes out are **not counted and not predicted**: the core deducts the whole `price × quantity`
    first (and refuses a bid its balance cannot pay), walks the units, and pays every refund only AFTER the loop, so a bid that replaces the pool's own lowest seats gets their price back
    only after it has paid in full. Those refunds fund the NEXT rebid. The books after a call may therefore still show the own seats it pushed out as held, until the next reconcile (the next
    rebid or the settlement, which reads the book itself).
  - *The price:* any price the money pays for, above the book's minimum when the book is full (`PRICE_TOO_LOW`). The price of **another** holding of the same IPO is refused (`IPO_PRICE_TAKEN`);
    a price **this** holding already has may be used again (the bid adds seats at that price and takes no entry in the list), so a pool that keeps bidding at one price never fills its list.
  - *Seats shrink as the price rises:* nothing bounds the seats but the money and the core's 676 slots. The same budget of 200,000 buys 100 seats at 2,000 and 95 at 2,101; a pool that was
    outbid recovers step by step, each step funded only by returned money (the tests show the verifier's scenario call by call: 43 rebids, never locked out, ending with exactly the seats its
    money buys).
  - *Funds that came back are usable at once:* a rebid first reconciles the holding against the live book in ONE pass over the 676 slots, counts the slots
    QPOOL still holds at each price the holding has registered, and moves the price of every seat lost BEFORE the call from at-risk to free funds, clamped by the shared refund
    pot, which is debited by the same amount. Every live price belongs to exactly one holding (the registry of K2), so this is exact whatever the order of calls
    and however many pools are outbid in the same tick (a pool never draws another pool's refund from the shared pot). There is no separate reconcile call: **`ReconcileIpo` stays deleted**.
  - *Prices die and are released:* a price with no seat left leaves the registry and the holding's list at the next rebid, so the list (16 live prices at most,
    `QPOOL_MAX_IPO_BIDS`) counts LIVE prices only (audit C016). A list that is full of distinct prices that all still have seats refuses a further NEW price with `CAPACITY` (the pool bids at a
    price it already has, or waits for one to die).
  - *Cheap when it fails:* every refusal that needs only the input, the caller's seat, the registry, the window, the book's minimum and an upper bound of the holding's money costs a few
    lookups and two reads of the book and writes nothing (the state is not even marked dirty). The bound uses the holding's list only for the seats at prices ABOVE the book's minimum,
    which cannot have been lost, so it never refuses something the money could do.
  - *What a rebid does to a full book:* it evicts the lowest slots priced below it, whoever owns them, and a pool being outbid is at the bottom, so its own remaining seats are usually the lowest:
    the rebid upgrades them.
- **Settlement with several prices per holding:** END_EPOCH counts QPOOL's slots at every registered price, one row per price, and `SettleIpo` adds up the rows of
  the holding's list: refund = free funds + (still at risk − seats × clearing price), `costBasis` = seats × clearing price, shares = seats; independent of the settle order,
  of how often the holding rebid, of the pot (a solvency clamp) and of `pendingShares`. A pool outbid right up to the end gets everything back. All K4 safety rules hold.
- **Views:** `GetIpoBids` (9): budget, highest price, seats as of the last reconcile, free funds, at-risk, the pot, and the live prices with their seat counts.
- **Interface:** `Propose` input back to 88 bytes (the trailing `ceiling` is gone); `GetProposal` output back to 168 bytes; `GetHolding` output 160 bytes (`ipoCeiling` and `ipoQuantity` are gone); `GetIpoBids` 8/328; `RebidIpo` output 56 to 64 bytes
  (a trailing `bidQuantity`: the seats bid for). Error codes `ABOVE_CEILING` (19) and `SEATS_FULL` (37) are never returned any more (the numbers stay reserved); `PRICE_TOO_LOW` (36) stays.
- **Cost:** the state is 3,211,264 bytes smaller than after step KI (1.6%): `Proposal.ceiling` (2,097,152), `Holding.ipoQuantity` (1,048,576) and `IpoBids.ceiling` (65,536). A rebid is one pass over
  676 slots and one walk of at most 16 list entries; no new loop over pools, holdings or proposals; the settlement's loop is over the holding's at most 16 prices.
- **Not changed / for the owner:** a seat owner of any size can spend the holding's free funds on rebids at any price they pay for (no new vote): the vote is the budget. A pool outbid by a rising price
  recovers fewer seats than it lost, and only as fast as its money comes back; a bot should rebid as soon as the money has come back. The pot clamp means an outbid stake that never reached the hook stays booked as cost.

### 3g. Several IPOs at the same time (audit step KX; the owner's question "if multiple IPOs are active, that must not affect the logic")

- **Why:** the core lets any number of contracts share one construction epoch, so their IPOs run in the same epoch, each with its own 676-slot book and its own clearing
  price, all ended by one `finishIPOs()` that pays each bidder one anonymous refund per IPO. K4 could test only two IPOs one after the other, because the core's examples
  have two contracts with an IPO book in two different epochs.
- **Now proven on real books:** seven IPO-book test contracts (`TESTEXE` to `TESTEXK`) were added to the scratch copy of the core (script `add_ipo_test_contracts.sh`, README section 5),
  so eight books end in one epoch (and `TESTEXC` is a ninth, earlier one). 14 new tests and a kitchen-sink test with two simultaneous IPOs (10 enforced seeds, 24 more explored) show, on those books:
  two pools bidding in both IPOs, outbid in both and rebidding in turns against one shared pot, each drawing only its own money; the same price allowed in two IPOs and refused in one
  (`Propose`, `Execute`, `RebidIpo`); one IPO failing while the other wins in the same epoch, each pool getting its own principal back; one `END_EPOCH` snapshotting both books; the
  four holdings of a won and a failed IPO settling in all 24 orders with the identical result; an eviction after the last reconcile in one IPO while the other is quiet; the
  eight-row table of targets filled by eight real bids, the ninth refused with `CAPACITY` until a settled row frees up, and ONE `END_EPOCH` reading eight books; a stuck
  (`NAME_UNPROVEN`) holding in one IPO leaving the other IPO's settlement and dividends alone; the dividends of two books (paid through the core) parked and released independently;
  the same money unable to fund two mandates; and the registry/list guard of `RebidIpo`.
- **Result:** no defect in the arithmetic or the attribution. `QPool.h` changed in a comment only (the cost statement below); the state is unchanged (193,872,992 bytes).
  One problem was found, and it is the known name-binding limit (owner decision D9, README 4d and 10) made visible: with two unlisted IPOs ending together a pool that declares the OTHER IPO's name
  and settles first binds its ledger to that name, and the honest holdings of both IPOs are then `NAME_UNPROVEN` for good. Nothing is lost and nothing can be taken (the capital stays on the books), but
  the dividends stay parked. It is reproduced by `KxADeclaredNameThatBelongsToTheOtherSimultaneousIpo...` (switch `D9`) and needs your decision (the embedded table before an IPO, or a reserved name, and the
  rename/abandon path K4b).
- **Documentation corrected:** the runbook and the contract's header said a refused `RebidIpo` costs a handful of lookups and two reads of the book and that only a call that can win something pays
  for the 676-slot pass. That is true only of the calls the cheap bound refuses; a call it lets through (for example because the pot holds only another pool's money) pays for the pass and can still be
  refused after it (`INSUFFICIENT_BALANCE`, `CAPACITY`, `BOOKS_INCONSISTENT`, `IPO_REJECTED`). README 4g (codes 6 and 31), 7a, 9 and 10 and the header of `QPool.h` now say so.
- **Test checker:** the `ipo-funds` property now adds up the money outbid in all live books against the one pot.

### 3h. Capacity quotas, deposits that cost a pool, slot lifecycle, squats and stuck shares (audit step K5; owner decisions D8 and D16, and the lower IPO cap)

- **Why:** several tables are shared by every pool (offers 32,768, allow-list entries 262,144, seat rows 524,288, holding slots 131,072, unsettled IPO holdings, votes) and one pool, or one attacker with a pool of his
  own, could fill them: sock puppets posting 32,768 one-unit offers made `OfferPosition` fail in EVERY pool; a creator could fill the allow-list table with junk; a few full pools filled the seat rows; and
  128 free dust deposits (one share each, into ANY open pool) used a pool's 128 holding slots for good, because `holdingCount` never went down, after which no purchase of that pool could execute. A deposit of one
  dust share also bound a whole issuer's dividend ledger to that share's name (nobody could deposit or buy the issuer's other assets), and shares moved under QPOOL's management for a deposit that was then refused were stuck.
- **The first things done, as you decided:** the pool fee is **10,000,000 QU** and the join fee **10,000 QU** (D8); the cap of unsettled IPO holdings is **2,048** (it was 8,192) and its price registry 32,768 (131,072),
  which took 4,105,728 bytes out of the state. Said plainly, the fees are now the only price of what the quotas do not stop: exhausting the 1,024 pools costs about **10.24 billion QU**, filling one roster
  of 256 about **2.56 million QU**, and filling the IPO holding table takes 256 pools (**2.56 billion QU** of registration fees, plus budgets that come back).
- **Now, per pool:** 32 offers, 256 allow-list entries, 512 seat rows, 32 deposits and 8 unsettled IPO holdings (README 4b); each is a counter in the pool, checked before anything is written and given back where the row
  goes, and a `static_assert` ties each shared table to pools × quota. One pool can no longer fill another pool's room. (Two limits are stated honestly: the IPO holding table is not pools × 8, the quota of 8 only makes
  it cost 256 pools to fill; and the **vote rows have no per-pool quota** -- filling them takes about 120 million QU and half a million transactions and refuses `Vote` everywhere. A quota would cap a large pool's
  parallel governance, so it is your launch-profile decision, D6. README 10.) A pool with a 256-member roster that all votes yes uses its 512 seat rows in two purchases; that is the size of the table.
- **Only the pool's creator can deposit** (D16), in public and private pools alike: anybody else gets `NOT_AUTHORIZED`, everything attached is refunded and nothing is read or written. The creator pays no join fee
  and is put on the roster as the owner of every seat. A deposit therefore costs a whole pool, not one share. The join fee of a deposit is gone from the fee table; `Contribute` and `BuyPosition` still charge it.
- **Slots and rows come back (C039).** A holding that gets its whole bid back (a failed IPO) gives back its slot, seat rows, offers and IPO quota; a sold-out holding is freed when its LAST member has claimed (each claim removes the claimant's row;
  `committedTotal` falls with it); a seat sold whole before it earned anything, or claimed after it earned, leaves no row; the search for a free holding slot starts at a cursor, so it is O(1) (it walked up to 131,072 slots);
  the dead `poolHoldings` table (1 MiB, never read) is gone. A pool nobody ever used can be closed by its creator (`CloseEmptyPool`) and its slot is the next one `CreatePool` takes (the fee is not given back).
- **Squats (C031).** `EvictSquat(holdingId, rivalName)` lets anybody give up a binding that never paid and is contested: a deposit alone in its ledger, never paid, with every seat still its depositor's, and either (token)
  the caller owns another asset of the issuer and the binding is four epochs old, or (contract shares) the contract at that index is in its IPO epoch now, which proves the claim wrong. The shares go back to the depositor in full first;
  an honest early depositor is only evicted by somebody who needs his place after it stood unused (README 4d). What it cannot do: a squat of an unlisted contract's index AFTER its IPO is not provable on chain (D9), and a binding made by a pool's own purchase of a dust share on QX (the venue manages those shares; only a sale by the pool releases it).
- **Stuck shares (C032).** `ReleaseManagedShares(asset, amount, destination)` takes back shares the CALLER owns and QPOOL manages (attach QX's 100 QU transfer fee; the rest comes back); it can never move a pool's holding.
  `PRE_ACQUIRE_SHARES` now refuses the move to QPOOL's management where a deposit could never succeed (a token of an issuer whose ledger is bound to another name, or when the payer table is full), so the shares stay tradable
  and the wrong comment above it is fixed. There is no POST hook: nothing is accounted when shares arrive.
- **Interface:** procedures 19 (`ReleaseManagedShares`), 20 (`CloseEmptyPool`), 21 (`EvictSquat`); `GetPool` gained five counters, `GetHolding` gained `rows`; `Holding` 184 bytes, `Pool` 168, `Offer` 24, `SourceLedger` 104.
  No error code was added. `SetAdmission` now accepts a revocation on an open pool and writes nothing when it changes nothing. **Integrators:** a deposit by anyone but the creator is refused; a first `Contribute` needs 20,000 QU.
- **Result of K5 itself:** `QPool.h` was 5,855 lines; the state **190,201,456 bytes** (3,671,536 less than before K5); 253 tests, all pass: 18 new ones, every one failing on the K5-before contract (`ck/K5/proof-new-tests-against-KX.summary.txt`), and the
  invariant checker has nine new properties. 17 existing tests were changed, each for a stated reason (README 5, step K5).
- **Repair of K5 after its independent check (two defects, both found by tests the checker wrote and both fixed):**
  1. *A refused seat purchase wrote the state.* `BuyPosition` refused a buyer who needed a new seat row beyond his pool's row budget only after it had synced the holding, which writes the state; anybody could aim a refunded
     purchase at a pool whose budget was used up and make the node re-hash the whole 190 MB state every tick for nothing (your rule D23). The check is a plain read made first now; the refusal writes nothing.
  2. *A slot is not an identity.* K5 gives a freed holding slot to the next holding, and `Vote`/`Execute` knew the holding of a SELL or ALLOCATE mandate only by pool and asset. So a mandate passed by the owners of an OLD holding could be
     fired (by anyone: Execute is permissionless) against a NEW holding of the same pool and asset that took the slot: in the checker's test an old sell mandate sold 1,000 of the creator's 2,000 freshly deposited shares, an old ALLOCATE paid the pool's
     unowned QU to the new holding's owners, and the creator could vote with his new seat on the old owners' open proposals. Every holding now carries an **incarnation** (a serial number drawn when it is created; a free slot has 0), a SELL or ALLOCATE
     records the incarnation of its holding, and `Vote` and `Execute` refuse (`NOT_FOUND`) a slot that holds another incarnation. It costs 8 bytes of state (the counter); the tag and the incarnation share 8 bytes with fields that became 32 bits.
     I checked everything else that keeps a holding's slot number (seat rows and offers go with the holding, IPO lists go at settlement, votes are keyed by proposal, closed pools have no members): README 5, "K5 repair".
  Also: `GetStats.poolCount` has counted the ACTIVE pools since K5 (it used to be the highest pool id plus one), so once a pool has been closed the live pool ids have a gap -- a client must scan `GetPool(id).found` (README 4h). And one K5 limit is stated plainly:
  **the creator can be locked out of depositing into his own public pool** if strangers fill its 256 roster places first (cost to them: 2.56 million QU of join fees; their contributions are withdrawable); deposit first, or create the pool private. Reserving a place for the creator
  would change the roster layout of every pool, so it is your decision (D16, README 10).
  Tests: 3 new (`K5ASellMandate...`, `K5AnAllocateMandate...`, `K5NobodyCanVoteOnAnOpenProposal...`), all failing on the first K5; `K5ASeatBuyer...` extended; one existing test changed (`ContractSharesAreNeverSoldThroughQswap` writes a PASSED proposal by hand and now gives it the holding's incarnation, as `Propose` does); one invariant added (`holding-incarnation`); the fixture lifts the starting tick to 2 so that a test run alone means what it means in the suite.
  **Result after the repair:** `QPool.h` 5,901 lines, state **190,201,464 bytes** (3,671,528 less than before K5), 256 tests, all pass.

### 3i. The seat market and payment correctness (audit step K6; owner decision D7)

- **Why:** the seat market is the exit the design promises (no vote is needed to leave), and six findings were about it: a buyer was bound to a PRICE but not to a PARCEL, so a seller who replaced his offer a moment before the buy sold
  one cost unit for the price of 300,000 (C033); the seats of a sold-out holding, which can never earn again, could be offered and bought for real QU (C100); the creator's one-call switch to private locked every member out of the seat
  market and of adding capital (C018); a withdrawal that the contract could not pay said OK and erased the member's balance (C028); a member with nothing owed could make the node re-hash the whole state every tick for free by claiming (C022); and
  an offer written in cost units re-priced itself when `SettleIpo` shrank the costs (C055). One more finding was a test gap (C045).
- **The buyer names the parcel.** `BuyPosition` has a new last input field, `expectedCostUnits`: the size of the offer the buyer read (`GetPosition(holdingId, seller).offeredCostUnits`). If it is not exactly the live offer's the call returns the new
  `OFFER_CHANGED` (38), everything attached comes back and nothing is written. The price needs no new field: the ask is taken from the attachment, so the attachment is the buyer's cap. 0 is not a parcel (`INVALID_INPUT`), so a wallet built for the old 40-byte
  input (the node pads it with zeros) is refused with a full refund instead of being filled blind. **Wallets and bots must send the new 48-byte input.**
- **Only a seat that is worth something is for sale.** `OfferPosition` refuses a RETIRED holding (`WRONG_STATUS`: sold out, no ledger, its cost units kept only so that the claims on the proceeds can be paid) and an IPO holding that `SettleIpo` has not run for
  (`SETTLEMENT_PENDING`): the settlement hands part of the budget back and shrinks every seat's cost units, so an offer written before it would sell a larger fraction than the seller meant. I chose to block rather than to rebase the offers
  (rebasing needs a loop over the offers or a second number in each); the cost is that an IPO seat cannot be sold while the auction runs. `BuyPosition` does not fill an offer that is still there on such a holding; the seller can cancel it, and a claim removes it with the row.
- **A switch to private never locks the members out (owner decision D7).** The creator may still switch the pool between open and private in both directions, at any time, but a MEMBER of the pool always counts as admitted for `Contribute` and `BuyPosition`, whatever the
  allow-list says, and so does the creator. The switch now decides who may ENTER: a newcomer needs an entry, a member trades seats and adds capital as before. Revoking an entry stops an address that has not joined yet.
- **A withdrawal that cannot be paid keeps the books.** `WithdrawUncommitted` puts the member's balance and the pool's total back exactly as they were and returns `TRANSFER_FAILED` (the pattern `ClaimPayout` already had). It cannot happen while the contract is solvent.
- **Calls that change nothing write nothing (your rule D23).** `ClaimPayout` works out what it would pay from a read-only preview of the sync, so a claim that finds nothing owed no longer marks the whole state for re-hashing; `SyncHolding` writes only when the ledger has
  accrued something; offering the very offer that stands and switching a pool to the mode it already has write nothing; every refusal of `BuyPosition` comes before its first write (the checks that were left after the sync are gone).
- **The branches of `BuyPosition` no test reached** (over-payment refund, a buyer who already owns seats keeps what he earned, the stale-offer drop) now have tests, with the audit's figures, and the invariant checker has two new properties (an offer never names more units than its seller owns;
  no offer sits on an unsettled IPO holding).
- **Interface:** `BuyPosition` input 40 to 48 bytes (trailing `expectedCostUnits`); error code 38 `OFFER_CHANGED`; `SETTLEMENT_PENDING` and `WRONG_STATUS` also come from `OfferPosition`/`BuyPosition` as above; `NOT_ADMITTED` no longer applies to a member. No procedure or function id changed, no output struct changed,
  the state did not change (**190,201,464 bytes**).
- **Not done, and why:** the plan also asked to fold `CreditProceeds` into `ApplyHolding` so that a sale writes a holding once instead of up to three times. A write is a 184-byte copy inside a call that has marked the state anyway, so the fold saves nothing measurable and touches every sell path;
  I left it. `SyncHolding`'s own write-on-change is equally invisible to the dirty flag (every caller that survives it writes something else), so no test can tell it from the old behaviour; the preview in `ClaimPayout` is what the finding needed.
- **Result of K6:** `QPool.h` 6,016 lines (about 115 more than K5, mostly comments), 263 tests (7 new), all pass; six of the seven new tests fail on the K5 contract (`ck/K6/proof-new-tests-against-K5.summary.txt`), and the seventh (the `BuyPosition` branches, C045) passes there by nature: it closes a coverage gap and its teeth are mutants of the K5
  contract that the whole K5 suite does not kill (three of four survive it; README 5, step K6). Five existing tests changed for stated reasons (README 5).

## 4. Selling

- **`SELL_QX` is rebuilt as an immediate sale.** Before, it placed an ask that
  stayed on QX, took the shares out of the holding at once, and expected the proceeds
  to arrive later and be routed by sender (which sent them to the wrong holding). Now
  one `Execute` places an ask at the proposal's limit price, QX fills it against
  standing bids inside the same call, and whatever did not fill is cancelled — **no ask
  of QPOOL ever rests on QX.** The shares that sold and the QU that arrived are known
  exactly and go to the one holding that sold.
  - `price` is the lowest price per share; `quAmount` is the least the visible bid book
    must pay *now*, after QX's 0.3% fee (and it must exceed 200). A book that cannot pay
    it gives the new `NO_FILL` (30), nothing changes, and the proposal stays PASSED to be
    retried.
  - Deposited shares and QSWAP-bought shares are first handed to QX (100 QU each, paid
    from the selling holding's proceeds).
  - A partial fill uses the proposal up; the rest needs a new proposal. Only the first 256
    standing bids are reached in one call. `Execute_output` gained `sold`.
- **`SELL_QSWAP`:** minimum proceeds must now exceed 100,100 QU (QSWAP's fee plus
  QX's), the QSWAP fee and any hand-over fee come out of the selling holding's proceeds,
  and a sale whose proceeds disappointed can no longer leave a holding earning on shares
  QPOOL no longer owns (the error used to come after the shares had left).
- Sell proposals on contract shares go through `SELL_QX` only (section 2).
- Fees users will see on a sale: QX 0.3% per fill (`SELL_QX`), the 100 QU hand-over
  fee(s), QSWAP's 100,000 QU (`SELL_QSWAP`), then QPOOL's 1% — all borne by the selling
  holding.

## 5. Other money-safety fixes

- **`Propose` refunded an attached reward twice** on some error paths, paying the
  caller from the contract's own money (measured in the harness against the old
  code: 777,777 QU attached returned 1,555,554). Now once.
- **A failed `ClaimPayout` transfer no longer erases the claim.**
- `SettleIpo` now credits each member's earned payouts *before* their cost shrinks
  with the refund, and clears stale seat and offer records when a holding is fully
  refunded, so a reused holding slot cannot inherit them.
- A vote on a full vote table, a refused sale and other refusals no longer mark the
  contract state as changed, so they do not trigger a whole-state re-hash (README 2a).
  A few refusals still do (README section 10).
- `GetHolding.totalPayouts` and `GetPosition.owed` now include dividends a holding has
  not pulled yet, so they match what `ClaimPayout` pays.

## 6. Interface changes for wallets and integrators

| What | Change |
|---|---|
| Functions | New `GetSource` (id 7): one dividend ledger; `GetIpoTarget` (id 8, step K4): the IPO snapshot of one target contract; `GetIpoBids` (id 9, steps KI and KI2): budget, free funds, seats and live prices of an IPO holding |
| `GetHolding` output | appended `ipoSettled`, `sourceEscrow`, `payerIndex`; step KI added an `ipoCeiling`, step KI2 removed it and `ipoQuantity` (160 bytes now, 168 before KI) |
| `GetProposal` output / `Propose` input | unchanged in size (168 and 88 bytes): step KI had appended a `ceiling` to both, step KI2 removed it again. For a `BUY_IPO` `Propose` ignores `shares` and stores the opening quantity it derives, `min(676, budget / opening)`, which `GetProposal` shows |
| `GetStats` output | appended `dividendEscrowTotal`, `totalUnownedSwept`; since step K5 `poolCount` is the number of ACTIVE pools (`CloseEmptyPool` takes one off), no longer the highest pool id plus one: scan `GetPool(id).found` up to 1,023 |
| `Execute` output | appended `sold`, `refunded` (32 to 48 bytes) |
| `DepositHoldingAsset` input | appended `contractIndex` (64 to 72 bytes) |
| `Propose` | `holdingId` carries a contract index for `BUY_QX` of contract shares and must be 0 for tokens; `SELL_QX` fields change meaning (section 4 above); `SELL_QSWAP` minimum must exceed 100,100 |
| Procedure 13 | `RebidIpo(holdingId, price, quantity)` (deleted in step K2, **reinstated in step KI**, rebuilt in step KI2, owner decision D1 final): a seat owner of an unsettled IPO holding bids again, bounded by money only: `price × quantity` must be at most the free funds at the start of the call and the contract's balance (3f, README 7a). Output: returnCode, bidsRegistered, seatsBefore, freed, atRisk, available, livePrices, **bidQuantity** (56 to 64 bytes); QU attached is refunded |
| `Propose(BUY_IPO)` | `quAmount` is the BUDGET (at least the opening price, at most the pool's uncommitted money), `price` the OPENING price 1 to 1,479,289,940,828 (the core's own limit); `shares` is ignored; `INVALID_INPUT` otherwise; voted on for 24 hours (step KI) |
| `Execute(BUY_IPO)` | places the FIRST bid all-in at the opening price, `min(676, floor(assigned / opening))` seats; refusals `IPO_REJECTED` (target's IPO not running: decided first) and `IPO_PRICE_TAKEN` (another unsettled holding bids at the price) |
| Procedure 15 | **`ReconcileIpo` is deleted** (step K4, owner decision D1). The id stays reserved and nothing is registered under it; a transaction sent to it invokes nothing and its attached QU is not refunded by the node (README 7a) |
| `SettleIpo` | refuses `WINDOW_OPEN` until the END_EPOCH snapshot exists and the epoch has moved on, `IPO_NO_SNAPSHOT` (35) if the epoch moved on without one, `NAME_UNPROVEN` (34) for seats whose declared name nothing proves, `BOOKS_INCONSISTENT` (31) for a snapshot that does not fit the holding; the hand-back is the never-bid budget plus `bid - seats × clearing price`, split exactly over the seats |
| `GetMember` / `GetPosition` output | appended `locked` (QU that backs votes now) / `voteLockUntilTick` (the seat cannot move up to this tick) |
| `Vote` | refuses `DEPOSIT_AFTER_OPEN` (33) on a buy for money that arrived at the proposal's tick or later; `NOT_FOUND` on a SELL/ALLOCATE whose holding slot holds another holding now (another pool, another asset, or the same asset created in the slot later: another incarnation, K5 repair); `BOOKS_INCONSISTENT` if the votes would add up to more than the weight the proposal opened with. A vote locks capital (buys) or the seat (sells) as described in 3d |
| `OfferPosition` / `BuyPosition` | refuse `SEAT_LOCKED` (32) for a seat that voted on an open SELL/ALLOCATE; since step K6 also `WRONG_STATUS` (7) on a retired holding and `SETTLEMENT_PENDING` (24) on an IPO holding that is not settled. **`BuyPosition`'s input grew by a trailing `expectedCostUnits` (40 to 48 bytes)**: the size of the offer the buyer read; `OFFER_CHANGED` (38) unless it is exactly the live offer's, `INVALID_INPUT` if it is 0 (section 3i) |
| `Contribute` / `BuyPosition` in a permissioned pool | a member of the pool, and the pool's creator, are admitted without an allow-list entry (owner decision D7, step K6); `NOT_ADMITTED` is for newcomers only |
| `WithdrawUncommitted` | returns `TRANSFER_FAILED` (12), with both records put back, if the contract cannot pay (step K6) |
| `Tally` / `Execute` | the execute window ends 2 days after the VOTE closed; a late `Tally` can EXPIRE a vote; `Execute` of a buy charges only YES voters and sizes the order to their money (3d) |
| Error codes | new 38 `OFFER_CHANGED` (step K6); 36 `PRICE_TOO_LOW` (step KI); 37 `SEATS_FULL` and 19 `ABOVE_CEILING` exist but are never returned (steps KI and KI2: an IPO holding has neither a voted quantity nor a ceiling; the numbers stay reserved); 34 `NAME_UNPROVEN`, 35 `IPO_NO_SNAPSHOT`; 32 `SEAT_LOCKED`, 33 `DEPOSIT_AFTER_OPEN`; earlier: 23 `SOURCE_CONFLICT`, 24 `SETTLEMENT_PENDING`, 25 `QX_BID_PENDING`, 27 `QX_ONLY`, 28 `CONTRACT_MISMATCH`, 29 `CONTRACT_NOT_LIVE`, 30 `NO_FILL`; 26 `DISABLED` exists but is never returned |

Procedure ids 1 to 18 (15 reserved, see above) and function ids 1 to 9 are unchanged (step K5 appended procedures 19, 20 and 21),
nothing was reordered, and no error code was renumbered. Output structs only grew at the end; `ReconcileIpo`'s input and output structs are gone.

## 7. Size and running cost

- `StateData`: 188,657,888 bytes before, **190,201,464 bytes now** (about 181.4 MiB),
  1,543,576 bytes (0.8%) larger (it was 4,685,280 bytes smaller after the earlier steps; step K3 added 8,388,608, step K4 264,528, step KI 4,458,512 and step KI2 took 3,211,264 out again). The ledger-table rewrite alone made it 523,760 bytes
  smaller (188,134,128 bytes): replacing two older tables (the payer-to-holding map and
  the per-asset share counts) with the ledger table saved more than the new fields cost
  (a contract index per holding, a name table for contract shares). Step K1 then added
  8 bytes (the roster position, `memberIdx`) to each of the 262,144 Contribution slots,
  +2,097,152 bytes (section 3b). Step K2 removed the list of up to 8 bid prices and its counter from each
  of the 131,072 holdings (72 bytes each, 9,437,184 bytes) and added the registry of IPO bid
  prices (3,178,512 bytes): 6,258,672 bytes smaller than after K1. **Step K3 added 8,388,608 bytes**: 192,361,216 bytes now (about 183.5 MiB, 4.6 percent more).
  The seat lock is 8 bytes on each of the 524,288 ownership slots and three 32-bit ticks (the lock's start and expiry, the last deposit) are 16 bytes
  on each of the 262,144 contribution slots (a record is 48 bytes now). **Step K4 added 264,528 bytes**: 192,625,744 now (about 183.7 MiB):
  the table of IPO targets (8 rows of 40 bytes) and the snapshot rows (8,192 slots of 16 + 16 bytes; at most 8 x 676 are ever used).
  **Step KI added 4,458,512 bytes** (2.3%): 197,084,256 now (about 188.0 MiB): `Proposal.ceiling` on each of the 262,144 proposal slots (2,097,152) and the lists of bid
  prices of the unsettled IPO holdings, 8,192 lists of 280 bytes (2,361,360). The registry of bid prices kept its 131,072 slots.
  **Step KI2 removed 3,211,264 bytes** (1.6%): 193,872,992 then: the ceiling is gone (`Proposal.ceiling`, 2,097,152, and `IpoBids.ceiling`, 65,536) and so is the holding's voted quantity (`Holding.ipoQuantity`, 1,048,576).
  **Step K5 removed 3,671,536 bytes net** (1.9%): the IPO cap of 2,048 took out 4,105,728 (the registry 3,178,512 to 794,640, the lists 2,295,824 to 573,968) and the quotas added 434,192 (`Holding.rows` 1,048,576, the dead `poolHoldings` -1,048,576, `Offer.poolId` 262,144, `SourceLedger.bindEpoch` 131,072, five counters in each pool 40,960): 190,201,456 then; **the repair of K5 added 8 bytes** (the counter of holdings created, `holdingSerial`; the incarnation and the proposal's tag fit in fields that became 32 bits): 190,201,464 now.
- **Step K6 did not change the state** (it added one read-only function, `SyncPreview`).
- No new loop over pools, holdings or proposals was added, and every new helper is
  constant-time. The hook is constant-time except that a payer with an unreconciled QX
  bid costs one pass over QPOOL's share records of that asset.
- The execution-fee model is now written down (README 2a): the owner's reserve pays for
  every call's execution time and for each whole-state re-hash. The expensive loop that
  `Propose` runs over its 262,144 proposal slots was **not** changed. `Propose(BUY_IPO)` no longer
  walks the 131,072 holdings (one registry lookup), `Execute(BUY_IPO)` looks at the IPO window before
  it searches for a free holding slot, and `ReconcileIpo` is gone (K4): END_EPOCH reads, once per epoch, the book of each IPO
  target that has unsettled holdings (at most 8 x 676 prices, and the bidder of the slots at a holding's price) and writes only
  what it stores.

## 8. What did not change

Minimum contribution (10,000 QU; the pool and join fees changed in step K5, section 3h: 10,000,000 QU and 10,000 QU), the
1% management fee, the 80/20 revenue split, the voting windows and quorum (3-day vote, except 1 day for a `BUY_IPO` (step KI), 50% quorum,
2-day execute window: the window now starts where the vote ends; the vote locks and who is charged by a buy changed in step K3, section 3d), one live proposal per
member, admission lists and the seat market (both only refined in step K6: a member is always admitted, the buyer names the parcel, section 3i), the IPO bid itself (budget committed once, refunds
pooled, `SettleIpo` returns the rest), and the absence of an admin key. What changed is who may bid
and how often: the opening bid in `Execute` (all-in at the opening price), then any number of rebids by seat owners, each paid by free funds only (sections 3f and 6).

## 9. For the owner: decisions and chores

1. **Index and epoch.** Pick QPOOL's index (31 or higher on the embedded mainnet table;
   QPOOL can bid only in IPOs of contracts with a higher index than its own, so take the
   lowest one available) and set the real construction epoch (still the placeholder 236).
2. **Check the contract table** against the network you deploy to.
3. **Seed the contract balance** (at least 100,000 QU, for the QSWAP sell fee) and plan
   the execution-fee reserve.
4. **Arrange for `RebidIpo`** to be called during the last hours of an IPO by a bot that owns a seat of each holding (README 9: it reads
   `GetIpoBids` and the book, and rebids with the free funds, `quantity` 0, at a price above the book's minimum, reusing a price the holding already has when it can), and for `SettleIpo`
   to be called for every IPO holding when a window closes
   (from the epoch after the IPO's; the order no longer matters), and for `ReconcileHolding`
   after QX fills (README 9).
5. **Open decisions** (README 10): IPO late binding when two unlisted contracts' IPO windows
   overlap (add the contract to the embedded table before its IPO, or reserve declared names);
   a rename or abandon path for an IPO holding whose declared name nothing proves (it stalls
   its ledger today); an exit for the unattributed counter; a cancel path for QX
   bids; the unrecorded 100 QU hand-over fee in a failed `SELL_QSWAP`; the hot loops in
   `Propose`/`Execute`; whether a flood of free, permissionless calls to a contract that
   charges nothing for them is acceptable.
6. **Run it live** on a devnet before mainnet: contract-share buys and deposits, real
   `finishIPOs` refunds, `SELL_QX` against real liquidity, buy refunds, an overlapping-IPO
   case, and node timing at 181 MiB (README 6).
7. **K5 decisions you may want to revisit:** the quotas (32 offers, 256 allow-list entries, 512 seat rows, 32 deposits, 8 unsettled IPO holdings per pool) and their consequences (a 256-member roster fits two
   purchases in the seat-row budget); whether the vote rows also need a per-pool quota (D6; README 10); the age after which a token squat can be evicted (4 epochs, `QPOOL_SQUAT_EPOCHS`); a listing fee or lazy expiry of offers
   (not built); and the squat of an unlisted contract's index after its IPO (D9).

## 10. Documents and tests

- **`README.md` is rewritten** and is the reference. Two statements in the old package
  are false and gone: that, with several holdings on one issuer, the first registered
  wins ("confirmed live"), and the same remark in `reports/` ("intentional design").
  `reports/` was not edited; its live P1/P6 scenario should be re-run and now expect a
  60 : 40 split. `docs/QUBIC-CONTRACT-GUIDE.md` was **not** updated with the rules
  learned here (list in README section 10).
- **Tests: 27 became 114** (27 original, 42 added with the dividend fix, 18 for contract
  shares, 18 for `SELL_QX` and 9 for refunds and refusals). Of the original 27, three
  differ from before: `IpoProposalValidatesIndexNameAndBudget` uses the placeholder
  share name `NEWCON1` (the old placeholder `QPAYHUB` is now a table name for index 29),
  and `LostIpoReturnsCapitalAndRetiresHolding` registers its hand-built holding in the
  ledger (one added line) and `StatsAggregate` has a blank line added. Other tests changed
  because behaviour changed on purpose: `SellQxProposalsAreDisabled` became
  `SellQxProposalsAreNoLongerDisabled`, and the QX buy end-to-end tests now expect the
  holding's cost to be what was spent (for example 72,000 / 48,000 instead of 120,000 /
  80,000 for a 200,000 QU bid that spent 120,000). No test was deleted or weakened.
- **Membership (step K1):** the suite grew from 131 to 140 tests (9 new regression tests that fail on the previous
  contract) and sixteen existing tests were adapted to the join fee (a seat buyer or depositor who is not a member now
  attaches it): README section 5.
- **IPO bids (step K2):** the suite grew from 140 to 149 tests (9 new; `RebidAndReconcileIpoAreExecutedAgainstARealBook` became
  `ReconcileIpoIsExecutedAgainstARealBook` without its `RebidIpo` half, and a few other tests lost their `RebidIpo` rows or
  gained registry assertions): README section 5. Nothing was deleted from the suite except what exercised the deleted procedure.
- **Capacity quotas, deposits, slot lifecycle (step K5 and its repair):** the suite grew from 235 to 253 tests with K5 and to 256 with its repair (3 new: the stale SELL mandate, the stale ALLOCATE mandate, the votes on a proposal about an earlier holding; plus `K5ASeatBuyer...` extended and a tenth new invariant). K5 itself: 18 new (`K5...`), all failing on the contract before K5, and 17 existing tests changed (the three revenue tests for the new fees; eleven that deposited as someone other than the pool's creator or
  expected the old join fee on a deposit; the holding-ceiling test, which now fills the deposit quota and sets the ceiling by hand; the eight-row IPO target test, whose pool is now at its quota of 8 unsettled IPO holdings; and the procedure-id test). Nothing was deleted or weakened; two things were only extended: `TheInvariantCheckerNamesEveryPropertyItGuards` (one deliberate break per new invariant, nine new properties) and the kitchen-sink harness (the creator deposits, a refused depositor takes his shares back, an operation for the three new procedures) (README 5, step K5).
- **The seat market and payment correctness (step K6):** the suite grew from 256 to 263 tests (7 new, `K6...`; six fail on the K5 contract, the seventh closes a coverage gap) and five existing tests changed: `RevokingAdmissionCannotClawBackOwnedSeats` and `SetPermissionedTogglesGateWithoutTouchingExistingMembers` (a member of a private pool is admitted now, owner decision D7), `LostIpoDropsItsLedgerAndSlotReuseIsSafe` and `K5AFailedIposHoldingGivesBackItsSlotItsSeatRowsAndItsQuotaAndTheLowestFreeSlotIsTheNextOnesTaken`
  (they showed that `SettleIpo` wipes the offers of a holding that got its bid back, with an offer made by `OfferPosition` on an unsettled IPO holding, which is refused now: the offer is put into the state by hand), and `KiOnlyAPersonWithASeatInTheHoldingMayRebidAStrangerCannot` (it bought a seat of an unsettled IPO holding, refused now: the purchase is asserted refused and the seat it would have left is built by hand). Nothing was deleted or weakened. The fixture's `buyPosition` sends the live offer's parcel unless a test says otherwise, so no other test changed. The invariant checker has two new properties.
- **Several IPOs at the same time (step KX):** the suite grew from 220 to 235 tests (15 new, `Kx...` and `KitchenSinkWithTwoSimultaneousIpos...`); no existing test was changed. They need the seven extra
  IPO-book test contracts that `add_ipo_test_contracts.sh` adds to a copy of the core (README 5): without them the file does not compile.
- **IPO bidding by budget (step KI2):** the suite grew from 209 to 220 tests. New: `Ki2AnOutbidPoolRebidsCallAfterCall...` (the verifier's scenario in money terms, 43 rebids, the list never fills),
  `Ki2TheRefundsOfTheSeatsARebidPushesOutCannotFundThatRebid`, `Ki2APoolWhoseSeatsAreAtTheBookMinimumIsRefusedByThePass...` (another pool's refunds in the pot), `Ki2ARebidAndAnExecuteAreRefusedWhenThePhysicalBalance...` (the balance is cut by hand), `Ki2ARebidFarAboveTheOpeningPrice...`,
  `Ki2SeatsShrinkAsTheOpeningPriceRises...` (five openings, settled in reverse order), `Ki2ExecuteBidsExactlyWhatTheMoneyBuys...` (the old wrap vectors cannot wrap), the two lifetime lines of `Propose`
  (`Ki2TheGuardOfOneLiveProposal...`, `Ki2AProposalSlotIsReused...`), and three tests of the core's own rules against the real `bidInContractIPO` and `finishIPOs`
  (`Ki2TheCoreDeductsTheWholeBidFirst...`, `Ki2TheCoreWalksTheUnitsOneByOne...`, `Ki2TheCoreRefundsASecondTime...`). Rewritten in money terms: the last half hour (five rebids, two pools, both settle orders),
  the money tests (`KiTheMoneyDecides...`, `KiTenRebidsAcrossTheEpoch...`, `KiAHoldingWhoseMoneyIsCertainlyInTheBook...`, `KiTheListHoldsSixteen...`), the proposal and Execute checks, the all-in opening bid and the views.
  The harness now runs a bid in the order of the core: the hook reaches QPOOL after the call, and every bid checks the contract's real balance against the core's accounting. Tests that changed because
  the behaviour changed on purpose are these. The quantity of an IPO is no longer voted but derived from the budget, so every test that voted a budget larger than `quantity × price` now votes a budget
  that buys the quantity it means (the leftover free funds it used to assert are gone, and its expected refunds changed by exactly that amount):
  `IpoProposalValidatesIndexNameAndBudget` (the budget must buy one seat), `SettleIpoRefusesWhileCapitalStillAtRisk` (500 seats), `FailedIpoBidLeavesNoTrace`,
  `EndToEndTwoPoolsBidInARealIpoThenShareItsDividend`, `RealIpoWinnerAndDepositedSharesOfTheSameContractShareItsDividend`, `IpoBidCannotJoinALedgerWhoseEscrowBelongsToAQxFill`,
  `IpoChronologyEvictionRefundsSelfRefundsAndTheCoreFinishRunThroughTheRealBook`, `TheEndOfEpochSnapshotReadsTheIpoBookExactlyAndOnlyAtTheEndOfTheIpoEpoch`,
  `SettleIpoRefundsAPlantedDuplicateSeatOwnerOnce` and `SettleIpoNeverHandsBackMoreThanCameBackAndGivesARecordlessMemberHisRecord` (through their shared helper; the second also restores BOB's whole record),
  `EveryProcedureRefundsAnAttachedRewardExactlyOnceOnEveryPath` (the "more shares than slots" row became "a budget below the opening price"), `HandBuiltHoldingsMirrorTheRealPaths` (166 seats, 2,000 free),
  `TwoHoldingsNeverBidAtTheSamePriceOnOneIpoTheRegistryDecidesAtExecute`, `ARefusedBidClaimsNoPriceAndAnUnseatedBidIsReturnedAtSettlement`,
  `RefusedIpoCallsDoNotDirtyTheState` (the "677 shares" edit of a stored proposal became "a budget below the opening price"), `ExecuteOfABuyIpoOutsideTheWindowIsRefusedBeforeTheCapacityChecks`,
  `ProposeBuyIpoAsksTheRegistryAndDoesNotScanTheHoldings`, `SettleOrderDecidesWhoGetsTheSharedIpoRefundFindingC004`, `ThreePoolsSettleToIdenticalPerMemberResultsInEveryOrder`,
  `AFailedIposPrincipalReachesItsOwnPoolEvenWhenAnotherIposWinnerSettlesFirst`, `AHoldingThatWonSeatsUnderAnUnprovenNameIsNeitherSettledNorRetired`, `TheRefundIsSplitExactlyOverTheSeatsAndNothingIsStranded`,
  `NobodyCanSettleAPoolOutOfARunningAuctionNoMatterWhatTheBookShows`, `ABidInAnIpoTargetBeyondTheEighthIsRefusedBeforeAnythingMoves`, `TheSnapshotIsTakenOnceAndALaterEndEpochDoesNotTouchIt`,
  `TheSnapshotDoesNotWaitForTheRevenueGateOfEndEpoch`, `AHoldingOutbidInFullGetsItsWholeBidBackWhateverTheOrder`, `AnIpoBidIsSizedToTheFundedPartAndAnAbstainerIsNotCharged` (one assertion, `ipoQuantity` became `pendingShares`),
  `ASnapshotThatSaysMoreWasWonOrSpentThanWasBidIsRefusedWithoutAWrite` (a snapshot of 677 seats instead of 101: the bound is the book's 676), `Ipo676SlotsAtTheMaximumPriceAreBidSeatedAndSettled` (the 677-quantity proposal line is gone),
  and `TheInvariantCheckerNamesEveryPropertyItGuards` (the ceiling rows became a row for seats worth more than the holding put into the book). Rewritten because their purpose was the voted-quantity or the ceiling rule:
  `IpoBidLimitsAreTheCoresAndAreRefusedAtProposeBeforeAnythingIsMultiplied` (the wrap vectors are refused as budgets below one seat, accepted as ignored `shares`), `ExecuteRefusesAPassedProposalOutsideTheCoresBidLimitsAndNothingLeavesTheContract`
  (price and budget cases only; the old quantity cases moved to `Ki2ExecuteBidsExactlyWhatTheMoneyBuys...`), `NobodyCanBidInAnIpoBeyondWhatTheVoteApprovedAndProcedureFifteenDoesNotExist` (the vote approved a budget).
  Of the 18 `Ki...` tests of step KI, replaced by a money version: `KiAPoolOutbidBelowItsCeiling...` (by `Ki2AnOutbidPoolRebidsCallAfterCall...`), `KiARebidMayNotAskForMoreSeats...` (by `KiTheMoneyDecides...`),
  `KiNineRebids...` (by `KiTenRebids...`), `KiAHoldingThatAlreadyHasEverySeat...` (by `KiAHoldingWhoseMoneyIsCertainlyInTheBook...`), `KiAnIpoProposalMustCarryACeiling...`, `KiExecuteRefusesAStoredIpoProposalWhoseCeilingOrBudgetIsWrong`,
  `KiTheOpeningBidIsSizedToWhatTheYesMoneyBuysAtTheCeiling` and `KiTheViewsShowTheCeiling...` (by the budget versions); the other ten were re-expressed with budgets (the last half hour, who may rebid, the minimum, the window, the short pot, the
  list of 16, outbid until the end, the three vote-window tests). The fuzz generator proposes budgets and rebids without a ceiling. No test was deleted without a replacement for the rule it guarded.
- **IPO bidding, finalized (step KI; the ceiling and the voted quantity it tested were removed by KI2, see the bullet above):** the suite grew from 191 to 209 tests (18 new, all named `Ki...`: the last half hour with five rebids settled in both orders, the ceiling,
  who may rebid, the book's minimum and the registry, the closed window, the quantity, the short pot, nine rebids and the compaction of dead prices, the
  full list of 16 prices, the cheap refusal from the list and the stale list, the pool outbid until the end, the one-day vote and the execute window and
  the vote locks of an IPO, the proposal's ceiling and budget at `Propose` and `Execute`, the sizing at the ceiling, and the views). Tests that changed because the behaviour
  changed on purpose: `NobodyCanBidInAnIpoAfterTheVoteAndProcedureThirteenDoesNotExist` became `NobodyCanBidInAnIpoBeyondWhatTheVoteApprovedAndProcedureFifteenDoesNotExist`
  (13 is registered again; it now asserts that a stranger, the ceiling, the registry and the seat bound hold), `TwoHoldingsNeverBidAtTheSamePriceOnOneIpoTheRegistryDecidesAtExecute`,
  `RefusedIpoCallsDoNotDirtyTheState` and `AnIpoBidIsSizedToTheFundedPartAndAnAbstainerIsNotCharged` advance by the one-day IPO vote window, the helper `passAlone` takes the window of the
  proposal's kind, `ExecuteRefusesAPassedProposalOutsideTheCoresBidLimitsAndNothingLeavesTheContract` keeps the edited proposal's ceiling equal to its price, and
  `TheInvariantCheckerNamesEveryPropertyItGuards` has rows for the new properties. The fixture's `propose()` takes a ceiling (default: the price, i.e. the old single bid), `proposeIpo`, `rebidIpo`
  and `getIpoBids` are new, the checker reads the vote window and life per kind and has four new properties (`ipo-bids`, `ipo-list-seats`, `pool-ipo-funds`, and the `ipo-registry`
  property now also covers the highest price), and the randomised tests rebid (two new operations). No test was deleted or weakened. Every new test fails on the K4 contract (with the new interface shapes added to it, 19 of 19 fail; against the unmodified K4 header the file does not compile), and 25 of 26 mutants of `QPool.h` are killed (the 26th is equivalent): README section 5, `ck/KI`.
- **IPO settlement (step K4):** the suite grew from 173 to 191 tests (18 new, listed in README section 5; every one that tests the new
  behaviour fails on the K3 contract). Tests that changed because the behaviour changed on purpose: `ReconcileIpoIsExecutedAgainstARealBook` became
  `TheEndOfEpochSnapshotReadsTheIpoBookExactlyAndOnlyAtTheEndOfTheIpoEpoch` (the procedure is deleted; the same scenario now asserts the snapshot and the
  settlement), `RefusedIpoBidsAndNoOpReconcilesDoNotDirtyTheState` became `RefusedIpoCallsDoNotDirtyTheState`, the reward table lost its four `ReconcileIpo` rows and gained
  `SettleIpo` rows for the new refusals, `IpoChronologyEvictionRefundsSelfRefundsAndTheCoreFinishRunThroughTheRealBook`,
  `Ipo676SlotsAtTheMaximumPriceAreBidSeatedAndSettled`, `TwoHoldingsNeverBidAtTheSamePriceOnOneIpoTheRegistryDecidesAtExecute`,
  `ARefusedBidClaimsNoPriceAndAnUnseatedBidIsReturnedAtSettlement` and `NobodyCanBidInAnIpoAfterTheVoteAndProcedureThirteenDoesNotExist` lost their reconcile calls (the snapshot
  figures are asserted instead), `EndToEndTwoPoolsBidInARealIpoThenShareItsDividend` and `RealIpoWinnerAndDepositedSharesOfTheSameContractShareItsDividend`
  run END_EPOCH at the IPO epoch (the node does), `IpoAssetNameIsBoundByTheFirstSettlementThatWinsShares`,
  `SettlementRefusesNamesThatBelongToAnotherContract` and `IpoSettlementClaimsTheNameAndTheLedgerReleasesIt` expect `NAME_UNPROVEN` instead of a retired holding
  (and the last one no longer builds the ghost-shares state of the defect), the two known-bug tests of C004 and C005 run both settle orders, and the fixture's
  `execute()` takes back the part of its pre-credited self-refund that the contract did not bid (a harness defect that a new randomised seed found). The
  checker has three new properties (`ipo-targets`, `ipo-seats`, `ipo-pot`); the known-bug table has C004, C005 and C038 switched on and no known randomised violation left;
  the randomised test also runs two IPO epochs in a row. No test was deleted except what exercised the deleted procedure.
- **Governance binding (step K3):** the suite grew from 149 to 173 tests (24 new; every one fails on the K2 contract except the control `AMandateThatMatchesTheBookRestsItsBidAndNothingComesBack`, which fails there only through the end-of-test checker). Twelve existing tests (and the known-bug table of the randomised test) had their
  expectations changed because the behaviour changed on purpose: `VoteThenWithdrawIsBlockedUntilProposalResolves` (a YES vote's lock outlasts `Tally`),
  `TwoOpenProposalsAddUpAMembersVoteLocksAndEachTallyReleasesItsOwn` (same), `TallyReleasesAVoteLockOnceEvenForAPlantedDuplicateRosterEntry` (the voter
  votes NO on the proposal that is tallied, a YES lock stays), `GovernanceQuorumTieAndWindowEdgesAreExact` (the execute window ends one tick earlier
  than when it started at `Tally`), `ANoVoterIsNeverBoughtInAndAnAbstainerIsAgainstTheRealQx` (an abstainer is not drafted),
  `BuyQxRefundSplitsExactlyOverThreeMembers` (the budget is split exactly: refund 129,662, costs 35,169 / 23,445 / 11,724),
  `RandomisedBuysAgainstRealQxKeepEveryBookBalanced` (same, and the order is sized instead of refused),
  `ASeatBuyerWhoTopsUpIsChargedOnceByExecuteFindingC002` (the buyer votes YES, or he is not charged),
  `ExecuteRefusesToChargeMoreThanTheMandate` (a record that says more than the vote is not charged more),
  `CheckerSeesTheStaleVoteRowOfFindingC051` (a late BUY voter is refused for his deposit, not for a stale row),
  `TheInvariantCheckerNamesEveryPropertyItGuards` (rows for the new properties, the new vote key) and
  `BuyQxThatQxRefusesIsRefundedInFullAndLeavesNoTrace`, whose refusal can no longer happen (the order is sized to its budget): it became
  `AnUnderfundedBuyQxIsSizedToItsBudgetAndTheRestStaysWithTheBuyers`, and its second half `AMandateThatMatchesTheBookRestsItsBidAndNothingComesBack`. The fixture's
  `propose()` waits one tick first (money that arrives at a proposal's tick has no vote on it), the fixture's `retireProposal()` ends a vote the way `Tally` does
  (rows deleted, locks lifted), and the checker's vote and lock properties are rewritten for the new keys and rules. No test was deleted; the refusal
  branch of a QX buy (`addedNumberOfShares <= 0`) can no longer be reached with the real QX.
- **Test foundation (after the audit):** the suite grew from 114 to 131 tests without a change to `QPool.h`: a global invariant checker that
  runs at the end of every test, a kitchen-sink randomised test over all 18 procedures (17 since K4: `ReconcileIpo` is gone; `RebidIpo` was out between K2 and KI), the IPO book driven by the core's own code,
  tests for the governance edges, the vote locks, the refund of an attached reward on every path, END_EPOCH against real
  shareholders and the revenue fee sites, and a rewrite of the one red test (README section 5).
- **Where the tests ran:** a substitute harness (Apple clang, Rosetta, a ported copy of the
  core), not the supported Linux/MSVC build; the stock command was not run (README 5).
