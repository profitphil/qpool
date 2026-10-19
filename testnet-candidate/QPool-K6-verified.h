using namespace QPI;

// QPOOL — vote-governed asset pooling.
//
// Users create a pool and contribute QU. The pool acquires dividend-paying
// assets by vote (QSWAP swap, QX bid order, or contract IPO bid) and disposes
// of them by vote (QSWAP swap or QX ask order). Every pool action is decided by
// a weighted vote of the members whose money is at stake; there is no admin
// that can move funds.
//
// OWNERSHIP MODEL — per acquisition, not pool-wide
//   Contributions sit as `uncommitted` QU until a passed acquisition deploys
//   them. At that moment each YES voter's share of the deployed QU is written
//   as their permanent `cost` in that holding (an abstainer or a NO voter is never charged). Later contributions therefore
//   fund only later acquisitions and never dilute an existing holding's
//   payouts. This sidesteps NAV valuation entirely: nothing has to be priced to
//   decide who is owed what.
//
//   `Holding.costBasis` is the ownership DENOMINATOR, never a valuation. It is
//   fixed for the life of the holding. Shares may fall to zero through partial
//   sales without disturbing anyone's fraction.
//
//   What a buy costs is what was SPENT, not what was assigned. QX and QSWAP hand back
//   whatever they do not use (the excess of the budget over price x shares, every price
//   improvement, a refused order). That QU never leaves the members: Execute reads it back
//   (BuyRefund), makes the holding's cost and every member's cost the amount really spent
//   (the costs add up to it exactly), leaves the rest in the members' uncommitted balances
//   and takes the refund out of globalUnattributed again, where the hook files every
//   arrival it cannot match (TakeBackRefund). BUY_IPO has its own refund path.
//
// PAYOUTS — one accumulator for dividends and sale proceeds
//   Both end up in a holding's `payoutPerCostX64` accumulator. A member's
//   entitlement is cost * (payoutPerCostX64 - payoutDebtX64), claimed per
//   holding. No loop over members is ever needed to distribute.
//
//   Sale proceeds belong to ONE named holding and are credited straight to it.
//   Dividends do not: several holdings, in several pools, can own shares of the
//   SAME asset, and the incoming transfer names only its sender. So every holding
//   registers with a SourceLedger keyed by its payer (the paying contract for an IPO holding or for
//   bought or deposited contract shares, the asset's issuer otherwise). The hook advances ONE per-share
//   accumulator on the ledger, in O(1), and each holding pulls its slice lazily
//   (SyncHolding) before anything reads or changes its weight. Shares nobody has
//   claimed yet (an IPO not settled, a QX bid filled but not reconciled) have their
//   slice set aside as escrow and paid when they are claimed.
//
// CONTRACT SHARES — assets whose issuer is NULL_ID (a contract's own IPO shares)
//   They can be IPO-won (BUY_IPO), bought on QX (BUY_QX; the proposal's holdingId carries
//   the paying contract's index) or deposited by the pool's creator (DepositHoldingAsset; its contractIndex field).
//   They are never swapped on QSWAP (BUY_QSWAP and SELL_QSWAP refuse them), so QX is their
//   only venue. All three kinds of holding of one contract earn from ONE CONTRACT ledger
//   keyed id(index,0,0,0) and split a dividend by shares, whatever way they came in.
//   The asset name does not say which contract pays on it and contracts cannot read
//   contractDescriptions, so the (index, name) pair is checked as far as it can be: exactly
//   against an embedded mainnet table for indices 1..30, and for any other index by keeping
//   the pairing one-to-one (contractNames) plus a check that the index is a registered contract
//   that is not in its IPO epoch (this does not prove its IPO has ended). See ContractPairKnown /
//   CheckContractPair for what that does and does not prove.
//
// SELLING ON QX — immediate, never resting
//   KIND_SELL_QX sells at once: Execute places ONE ask at the proposal's limit price, QX
//   matches it against resting bids inside that same call, and whatever did not fill is
//   cancelled before Execute returns, so no ask of QPOOL ever rests on the book. The shares
//   that left and the QU that arrived are therefore both known exactly when Execute ends and
//   are credited to the ONE holding that sold, the way SELL_QSWAP credits its proceeds. Nothing
//   about a QX fill has to be guessed later, and shares never sit on an ask earning dividends
//   the pool no longer owns. The proposal's price is the binding per-share limit (QX never
//   fills below it) and its quAmount a floor on the total the visible bid book must pay NOW,
//   checked before anything moves (see QxBidBook); a sale the book cannot meet is refused with
//   QPOOL_ERR_NO_FILL and the proposal stays PASSED, free to be retried inside its window.
//
// SECONDARY MARKET — buy-outs
//   A member may offer all or part of a position in one holding at a price they
//   set; anyone may fill it. QU goes to the seller and the cost units transfer
//   to the buyer, so the asset itself is untouched — the seat changes hands.
//   Exits are deliberately NOT vote-gated: requiring pool approval to leave
//   would let a majority trap a member.
//   (Audit step K6.) The buyer names the PARCEL as well as the price: BuyPosition carries expectedCostUnits and is refused, refunded and
//   writes nothing, unless it is exactly the live offer's (a seller can replace his offer at any moment; the attached QU only caps the
//   price). Only a seat that is worth something can be offered or bought: not the seats of a RETIRED holding (nothing can accrue on them
//   again) and not those of an IPO holding that SettleIpo has not run for (it shrinks every seat's cost units, which would re-price an offer
//   written in cost units). In a permissioned pool a MEMBER always counts as admitted, and so does the creator (owner decision D7): the
//   creator's switch to private decides who may ENTER, and never closes the seat market on the people who are in the pool. Every refusal of
//   OfferPosition and BuyPosition, and a claim that finds nothing owed, comes before the first write.
//
// IPO BIDDING AND REBIDDING -- the budget is the only limit; a pool keeps bidding for as long as the auction runs
//   A BUY_IPO proposal carries a target contract, a share name, a BUDGET (quAmount: the QU the pool allocates) and an OPENING price (the price of
//   the first bid, a starting point the voters look at, not a limit on later bids). It carries NO quantity and NO ceiling price (owner decision
//   D1, final): the higher the price of an IPO goes, the fewer seats the same money buys, and a pool cannot know how high an auction will go, so
//   the money is the only governing factor. The seats a holding holds are whatever its money buys at the prices it bids, and they shrink as the price
//   rises; nothing else bounds them but the core's own 676 slots. (Propose_input.shares is ignored for a BUY_IPO; Propose stores the opening
//   quantity it derives, min(676, budget / opening price), for GetProposal to show.) It is voted on for QPOOL_IPO_VOTE_WINDOW (one day, 86,400
//   ticks at the nominal one tick per second: the auction moves faster than the other proposals) and its mandate then lasts the usual two days.
//   Execute spends the YES voters' money (up to the budget) ALL-IN on the first bid: min(676, floor(assigned / opening price)) seats at the opening
//   price. It keeps the holding's bid prices in a small list (StateData::ipoBids) next to the (target, price) registry (ipoPrices).
//   An auction is lost and won in the last hour, and a pool that has been outbid and has its money back must be able to bid again, so
//   RebidIpo (permissionless for the people with money at stake: the caller must own a seat of the holding) places a further bid whenever it is
//   wanted, as often as it is wanted, with no cooldown. It is bounded by MONEY and by nothing else: a bid of n seats at price p is allowed when
//   p x n is at most the holding's FREE FUNDS and at most the contract's physical balance. Free funds are the never-bid rest of the budget plus
//   everything that has been outbid and has COME BACK: a RebidIpo first reconciles the holding against the live book in ONE pass over the 676
//   slots, counts the slots QPOOL still holds at each of the holding's prices, and moves the price of every seat that was lost EARLIER from
//   at-risk to free (clamped by what the refund pot ipoRefundEscrow holds, which is debited by the same amount). Because every live price belongs to
//   exactly one holding, this is exact whatever the order of the calls and however many pools are outbid in the same tick.
//   THE MONEY RULE. A rebid cannot calculate the refunds of the seats it pushes out in the same transaction: the core DEDUCTS the whole price x
//   quantity from the contract's physical balance first (a bid the balance cannot pay is refused with nothing done), walks the units one by one
//   and pays every refund only AFTER the loop (ipo.h, bidInContractIPO). So the refunds of THIS bid (the holding's own lowest seats that it
//   replaces, units that were not seated) are neither counted nor predicted: they arrive in the hook during the bid, are pooled, and become free
//   funds at the NEXT reconcile (the next rebid, or the settlement), where they fund the next rebid. A pool that was outbid and wants its seats
//   back therefore recovers step by step, each step funded only by money that has already returned. After a call the books may still show the own
//   seats that call pushed out as held, until the next reconcile (the settlement does not depend on it: it reads the book itself).
//   A price with no seat left is dead: it leaves the list and the registry at the next reconcile. A rebid may REUSE a price that is already one of
//   THIS holding's own prices (it adds seats at that price; the registry still refuses a price of ANOTHER holding), so a pool that keeps bidding at a
//   price it holds never grows its list. The list holds at most QPOOL_MAX_IPO_BIDS live prices: a further NEW price while all of them have seats
//   is refused with CAPACITY, and the pool bids at one it already holds.
//   (There is no separate reconcile procedure: id 15, ReconcileIpo of earlier revisions, stays reserved.)
//   THE COST OF A REFUSED CALL. A call refused because of the input, the holding, the caller, the registry (a price of another holding, or a full
//   registry), the target's row, the IPO window, the book's minimum or the money bound taken from the holding's own list (a holding whose funds cannot
//   pay for the seats asked for even if every seat that could have been lost has been, or for one seat by default) costs a few lookups and two reads of
//   the book, and writes nothing. A call that PASSES that cheap bound pays for the 676-slot pass whether or not it then wins anything, and can still be
//   refused after it (nothing is written): INSUFFICIENT_BALANCE when the money has not really come back (the bound is only an upper bound -- for example
//   the pot holds only ANOTHER pool's money, or the default quantity works out at 0 seats), CAPACITY when the list already has 16 live prices and the
//   price is a new one, BOOKS_INCONSISTENT when the pass finds more seated than the holding ever put in or the cost is above the contract's physical
//   balance, IPO_REJECTED when the core itself refuses the bid. Those are the refusals a bot that cannot see the book exactly will meet, and each one has
//   been paid for.
//   What a rebid does to a full book: a bid of n slots at price p evicts the n LOWEST slots of the book that are priced below p (the youngest of
//   equal prices first), whoever owns them, and a pool that is being outbid is at the bottom, so its own remaining seats are usually the lowest
//   slots there are: the rebid then UPGRADES them (their price comes back to the pot, the same number of slots is seated at the new price).
//   Time: every window here is in ticks and a tick is NOMINALLY one second (TARGET_TICK_DURATION is 1000 ms in the core), so a day is 86,400
//   ticks only at that speed; a network that runs slower or faster stretches or shrinks every window in real time.
//
// IPO SETTLEMENT — from a snapshot taken at the last moment the book can be read
//   The core shows the book -- all 676 slots, price and bidder -- until the IPO epoch ends, and nothing after that: the
//   node runs every contract's END_EPOCH first (system.epoch is still the IPO epoch there) and finishIPOs()
//   afterwards, which pays each bidder ONE refund (the premium over the final price, or the whole bid
//   when the book was not full) and issues the shares, one per slot. The refund carries no identity.
//   So QPOOL reads the book in its own END_EPOCH (the snapshot) and settles from that:
//     * Execute keeps a table of at most QPOOL_MAX_IPO_TARGETS target contracts that have unsettled holdings;
//     * END_EPOCH, for each of them, reads the final price (the price of the last slot, 0 when the book is not full:
//       the IPO failed) and counts QPOOL's slots per bid price in one pass over the 676 slots; a price belongs to ONE
//       holding (ipoPrices), so this is exact, and a holding with several prices simply has several rows. It stores, per
//       price, the seats won and what they cost at the final price;
//     * SettleIpo (permissionless, only after the epoch in which the snapshot was taken) adds up the rows of the holding's
//       prices, hands back the budget that was never bid plus everything the holding bid and did not spend (evicted slots,
//       premium over the final price, the whole bid of a failed IPO) and recognises exactly the seats it won. Nothing it does
//       depends on the order in which holdings settle, on a stale count, on how often the holding was reconciled or on the pot
//       ipoRefundEscrow that collects the refunds (the pot is only a solvency clamp). Evidence that the auction is over is
//       positive: the snapshot exists for this target, and the epoch has moved on. A stranger cannot end a running auction, and a
//       book that cannot be read is not mistaken for a finished one.
//   A holding that won seats under a name the universe cannot prove (the shares are not there under the name the
//   vote declared, or another ledger owns the name) is NOT settled and NOT retired: SettleIpo returns NAME_UNPROVEN
//   and writes nothing, so the members' capital stays on the books.
//
// SHARED TABLES, PER-POOL QUOTAS AND THE LIFE OF A SLOT (audit step K5)
//   Every table that all pools share -- offers, allow-list entries, seat rows (ownership), holding slots, unsettled IPO holdings -- is sized
//   as (number of pools) x (a per-pool quota), and a static_assert at the constants ties each pair. A pool can never use more than its quota:
//   every call that would add a row checks the pool's own counter (Pool.offerCount, admittedCount, ownershipRows, holdingCount, depositCount,
//   ipoHoldings) BEFORE it writes anything, and every call that removes a row gives the place back, so one pool -- or one attacker with pools
//   of his own -- can fill only his own share and can never make another pool's call fail with CAPACITY. Two honest exceptions: the table of
//   unsettled IPO holdings is sized by the owner for a handful of active IPOs (QPOOL_MAX_IPO_HOLDINGS 2,048), not for 1,024 pools x 8, so the
//   quota of 8 makes it cost 256 pools to fill it, not one; and the vote rows have no per-pool quota (README 4b).
//   DEPOSITS. Only the pool's CREATOR may call DepositHoldingAsset (owner decision D16), in a public and a private pool alike; he pays no
//   join fee and is put on the roster as the owner of every seat. A pool holds at most QPOOL_MAX_DEPOSITS_PER_POOL deposits, so deposits
//   can never fill the slots acquisitions need.
//   THE LIFE OF A SLOT. A holding slot is occupied from its creation (Execute, DepositHoldingAsset) until it is freed (costBasis 0 and
//   not active); the search for a free slot starts at holdingFreeHint (every slot below it is occupied), so it is O(1). It is freed by
//   SettleIpo when a holding gets its whole bid back (a failed IPO), by the claim of the last member of a RETIRED (sold-out) holding (every
//   claim there removes the claimant's row: nothing can accrue on it), and by EvictSquat. A seat row that has no cost units and nothing owed
//   is removed where it empties (BuyPosition, SettleIpo, ClaimPayout), together with the owner's offer on it; Holding.rows counts the rows
//   of a holding, Pool.ownershipRows those of a pool, and a pool that was never used can be closed (CloseEmptyPool) to free its slot.
//   A SLOT IS NOT AN IDENTITY (audit K5 repair). Because a freed slot is taken again, a number kept across time can come to name another
//   holding. Everything that keeps a holding's slot number for later is therefore either removed with the holding (seat rows, offers, the IPO
//   bid list and registry entries) or tied to the holding's INCARNATION: Holding.incarnation is a serial number that every creation draws
//   from StateData::holdingSerial, and a SELL or ALLOCATE proposal records the incarnation of the holding it was opened on
//   (Proposal.holdingTag). Vote and Execute refuse a proposal whose tag is not the incarnation sitting in the slot now, so a mandate voted
//   by the owners of one holding can never be carried out on another holding that took its slot -- not even one of the same pool and the
//   same asset. The tag is 32 bits because it only has to tell apart the holdings that can sit in one slot during the life of ONE proposal
//   (QPOOL_PROPOSAL_LIFETIME, 432,000 ticks): the chain takes at most NUMBER_OF_TRANSACTIONS_PER_TICK = 4,096 transactions a tick
//   (core, network_messages/common_def.h), so fewer than 1.8 billion holdings can be created in that time, fewer than 2^32.
//   SQUATS AND STUCK SHARES. A payer's ledger is bound to ONE asset name, so the first holding decides what the payer's other assets can do
//   here: the binding is priced (only a creator deposits; a pool costs QPOOL_CREATE_POOL_FEE) and one that never paid can be given up when
//   somebody else needs it (EvictSquat: see there for the exact predicate). Shares that a user moved under this contract's management and
//   could not deposit come back with ReleaseManagedShares, and PRE_ACQUIRE_SHARES refuses the move where the deposit could never succeed.
//
// INTEGRATION CONSTRAINTS (verified against qpi_ipo_impl.h and ipo.h)
//   qpi.bidInIPO returns -1 if called while a contract callback is running, so
//   IPO bids can never fire from END_EPOCH. All execution is therefore done by
//   permissionless user procedures. bidInIPO also requires
//   SELF_INDEX < IPOContractIndex, so a pool can only bid on IPOs of contracts
//   deployed AFTER it, and only during constructionEpoch - 1.

// ---- Capacities (powers of two where a HashMap needs it) ----
constexpr uint64 QPOOL_MAX_POOLS                 = 1024;
constexpr uint64 QPOOL_MAX_CONTRIBUTORS_PER_POOL = 256;
constexpr uint64 QPOOL_MAX_HOLDINGS              = 131072; // = MAX_POOLS * MAX_HOLDINGS_PER_POOL
constexpr uint64 QPOOL_MAX_HOLDINGS_PER_POOL     = 128;
// A live proposal is capped at one per (member, pool) pair (see Propose's
// own "one live proposal per member per pool" guard), so the number of
// proposals that could ever be simultaneously open is bounded by the number
// of (pool, member) pairs that can exist -- exactly QPOOL_MAX_CONTRIBUTIONS.
// Setting the cap any lower risks a busy, many-pool contract genuinely
// running out of proposal slots under legitimate concurrent load (not just
// reclaimable-by-waiting congestion); this ceiling makes that structurally
// impossible.
constexpr uint64 QPOOL_MAX_PROPOSALS             = 262144; // = MAX_CONTRIBUTIONS
constexpr uint64 QPOOL_MAX_CONTRIBUTIONS         = 262144; // (pool, member) records = MAX_POOLS * MAX_CONTRIBUTORS_PER_POOL
constexpr uint64 QPOOL_MAX_OWNERSHIP             = 524288; // (holding, member) records
constexpr uint64 QPOOL_MAX_OFFERS                = 32768;  // open position offers
constexpr uint64 QPOOL_MAX_ADMITTED              = 262144; // allow-list entries of permissioned pools
// ---- Per-pool quotas (audit C007, C013) ----
// Every table that all pools share is sized as pools x a per-pool quota, and a pool may never use more than its quota (each procedure
// that adds a row checks the pool's own counter BEFORE anything is written, and every procedure that removes one gives it back). So one
// pool, or one attacker with a pool of his own, can fill only his own share of a shared table and can never make another pool's
// call fail with CAPACITY. The static_asserts below tie each table to its quota. (The launch profile -- how many pools, and how much each
// may hold -- is the owner's decision; shrinking a table means shrinking its quota with it.)
constexpr uint64 QPOOL_MAX_OFFERS_PER_POOL       = 32;     // open offers of seats in one pool
constexpr uint64 QPOOL_MAX_ADMITTED_PER_POOL     = 256;    // allow-list entries of one pool (a roster has 256 places)
constexpr uint64 QPOOL_MAX_OWNERSHIP_PER_POOL    = 512;    // seat rows (member, holding) of one pool
// A pool's holdings are of two kinds: those a vote bought (BUY_QSWAP, BUY_QX, BUY_IPO) and those its CREATOR deposited. Deposits are
// capped well below the holding quota so that a pool can never fill its holding slots with deposits and be left unable to buy.
constexpr uint64 QPOOL_MAX_DEPOSITS_PER_POOL     = 32;
// IPO holdings of one pool that are not settled yet. At most QPOOL_MAX_IPO_TARGETS IPOs can be live at once, so eight is one holding for
// every IPO a pool can bid in. The table of unsettled IPO holdings (QPOOL_MAX_IPO_HOLDINGS) is NOT pools x this quota -- the owner sized it
// for a handful of active IPOs, far below 1,024 x 8 -- so the quota does not make it unfillable, it makes it cost 2,048 / 8 = 256 pools
// (QPOOL_CREATE_POOL_FEE each) to fill it, instead of one: see the README.
constexpr uint64 QPOOL_MAX_IPO_HOLDINGS_PER_POOL = 8;
// A TOKEN or CONTRACT ledger (the payer's binding to ONE asset name) that has never paid anything and whose only holding is a deposit that
// is alone with its depositor can be evicted when somebody else needs the binding, once it is this many epochs old (see EvictSquat).
constexpr uint64 QPOOL_SQUAT_EPOCHS              = 4;
constexpr uint64 QPOOL_MAX_SOURCES               = 16384;  // dividend sources (payers) with a live holding
// A HashMap stays ~constant-time below 80% fill and degrades towards linear above
// 90% (qpi_containers.h). Every transfer to this contract whose sender is NOT a
// registered payer is a hash MISS, and a miss probes up to the first empty slot,
// so the table must keep empty slots: no NEW payer is admitted above 75% fill.
constexpr uint64 QPOOL_SOURCE_SOFT_CAP           = QPOOL_MAX_SOURCES - (QPOOL_MAX_SOURCES >> 2); // 12288
// IPO bid limits: EXACTLY the core's (bidInContractIPO in contract_core/ipo.h accepts a price in
// 1..MAX_AMOUNT/NUMBER_OF_COMPUTORS and a quantity in 1..NUMBER_OF_COMPUTORS, and takes price x quantity from the
// calling contract's WHOLE balance, i.e. from every pool). The core narrows the quantity it is given to 16 bits,
// so a bigger one would be debited as something else than what the vote approved (audit C001): both limits are
// checked in Propose and again in Execute, before any multiplication or cast.
constexpr uint64 QPOOL_IPO_MAX_QUANTITY          = NUMBER_OF_COMPUTORS;                              // slots in an IPO book: 676
constexpr uint64 QPOOL_IPO_MAX_PRICE             = div((uint64)MAX_AMOUNT, (uint64)NUMBER_OF_COMPUTORS); // MAX_AMOUNT over 676: 1,479,289,940,828 QU per share
static_assert(QPOOL_IPO_MAX_PRICE * QPOOL_IPO_MAX_QUANTITY <= (uint64)MAX_AMOUNT);   // a bid's cost can never leave the QU range
// A holding bids its opening price once in Execute and may bid again (RebidIpo) at any price its money pays for (there is no ceiling
// price: the vote names a budget), so it can have several live bid prices at a time. (IPO price, target contract) is unique among the
// live bid prices of all unsettled IPO holdings: see ipoPrices.
static_assert((QPOOL_MAX_POOLS & (QPOOL_MAX_POOLS - 1)) == 0);
static_assert((QPOOL_MAX_HOLDINGS & (QPOOL_MAX_HOLDINGS - 1)) == 0);
static_assert((QPOOL_MAX_PROPOSALS & (QPOOL_MAX_PROPOSALS - 1)) == 0);
static_assert((QPOOL_MAX_CONTRIBUTIONS & (QPOOL_MAX_CONTRIBUTIONS - 1)) == 0);
static_assert((QPOOL_MAX_OWNERSHIP & (QPOOL_MAX_OWNERSHIP - 1)) == 0);
static_assert((QPOOL_MAX_OFFERS & (QPOOL_MAX_OFFERS - 1)) == 0);
static_assert((QPOOL_MAX_SOURCES & (QPOOL_MAX_SOURCES - 1)) == 0);
static_assert(QPOOL_SOURCE_SOFT_CAP < QPOOL_MAX_SOURCES);
static_assert((QPOOL_MAX_CONTRIBUTORS_PER_POOL & (QPOOL_MAX_CONTRIBUTORS_PER_POOL - 1)) == 0);
// Shared tables are sized as pools x per-pool quota, so no pool can run another pool out of room: the roster
// array holds every pool's whole roster, the Contribution map one record per roster entry (a member is on a
// pool's roster exactly when he has a record there, see EnsureMember, so the map can never be full while a
// roster has room), the holding table every pool's whole holding quota.
static_assert(QPOOL_MAX_CONTRIBUTIONS == QPOOL_MAX_POOLS * QPOOL_MAX_CONTRIBUTORS_PER_POOL);
static_assert(QPOOL_MAX_PROPOSALS == QPOOL_MAX_CONTRIBUTIONS);
static_assert(QPOOL_MAX_HOLDINGS == QPOOL_MAX_POOLS * QPOOL_MAX_HOLDINGS_PER_POOL);
static_assert(QPOOL_MAX_OFFERS == QPOOL_MAX_POOLS * QPOOL_MAX_OFFERS_PER_POOL);
static_assert(QPOOL_MAX_ADMITTED == QPOOL_MAX_POOLS * QPOOL_MAX_ADMITTED_PER_POOL);
static_assert(QPOOL_MAX_OWNERSHIP == QPOOL_MAX_POOLS * QPOOL_MAX_OWNERSHIP_PER_POOL);
static_assert((QPOOL_MAX_ADMITTED & (QPOOL_MAX_ADMITTED - 1)) == 0);
static_assert(QPOOL_MAX_DEPOSITS_PER_POOL < QPOOL_MAX_HOLDINGS_PER_POOL);        // a pool always keeps slots for acquisitions
static_assert(QPOOL_MAX_IPO_HOLDINGS_PER_POOL <= QPOOL_MAX_HOLDINGS_PER_POOL - QPOOL_MAX_DEPOSITS_PER_POOL);   // ... and for every IPO it may bid in
// One acquisition writes a seat row for every member who paid for it, so a pool's row budget must hold at least one full roster (and a
// deposit's one row): a pool whose roster is full can still buy at least once.
static_assert(QPOOL_MAX_OWNERSHIP_PER_POOL >= QPOOL_MAX_CONTRIBUTORS_PER_POOL);
// The prices an IPO holding has at a time (the list in StateData::ipoBids). A price enters the list when a bid seats at least one slot and
// leaves it when the last slot at that price is lost and the holding reconciles, so the list counts LIVE prices. Eviction takes the
// lowest slot of the book first, so a holding's old low prices die first. A rebid at a price the holding already has adds seats to it and
// takes no entry, so a pool that keeps bidding at one price never fills the list (the last half hour is said to need three to five
// rebids; the list holds sixteen live prices). A further NEW price while all sixteen have seats is refused with CAPACITY, nothing
// moves, and the pool bids at a price it already holds; the next reconcile that finds a price dead makes room.
constexpr uint64 QPOOL_MAX_IPO_BIDS              = 16;
static_assert((QPOOL_MAX_IPO_BIDS & (QPOOL_MAX_IPO_BIDS - 1)) == 0);
// Unsettled IPO holdings at one time. An IPO holding costs a vote and a pool's budget, there are at most QPOOL_MAX_IPO_TARGETS books of
// 676 slots, and a holding is gone when its IPO has been settled, so this is far above anything an IPO season needs; Execute refuses
// a further one with CAPACITY before anything moves.
constexpr uint64 QPOOL_MAX_IPO_HOLDINGS          = 2048;
static_assert((QPOOL_MAX_IPO_HOLDINGS & (QPOOL_MAX_IPO_HOLDINGS - 1)) == 0);
static_assert(QPOOL_MAX_IPO_HOLDINGS <= QPOOL_MAX_HOLDINGS);
// The IPO price registry holds one entry per bid price of an unsettled IPO holding, and a holding has at most QPOOL_MAX_IPO_BIDS of them, so
// the registry can never be full while an IPO holding has room for one more price. (The launch profile can shrink the tables together,
// see the README.)
constexpr uint64 QPOOL_MAX_IPO_PRICES            = QPOOL_MAX_IPO_HOLDINGS * QPOOL_MAX_IPO_BIDS;
static_assert((QPOOL_MAX_IPO_PRICES & (QPOOL_MAX_IPO_PRICES - 1)) == 0);
static_assert(QPOOL_MAX_IPO_PRICES == QPOOL_MAX_IPO_HOLDINGS * QPOOL_MAX_IPO_BIDS);
// IPO targets with unsettled holdings, at most this many at one time (Execute refuses a bid in a further one with CAPACITY).
// END_EPOCH reads the book of each of them (676 slots), so this bounds its work: no loop over pools, holdings or proposals.
constexpr uint64 QPOOL_MAX_IPO_TARGETS           = 8;
static_assert(QPOOL_MAX_IPO_HOLDINGS_PER_POOL >= QPOOL_MAX_IPO_TARGETS);     // a pool may hold an unsettled holding in every IPO that can be live
static_assert(QPOOL_MAX_IPO_HOLDINGS_PER_POOL <= QPOOL_MAX_IPO_HOLDINGS);
// The snapshot stores one row per (target, bid price) at which QPOOL holds slots. A book has 676 slots, so there are at most
// 676 such rows per target; the table is kept below 80 percent full (the load a HashMap stays fast at).
constexpr uint64 QPOOL_MAX_IPO_SEATS             = 8192;
static_assert((QPOOL_MAX_IPO_SEATS & (QPOOL_MAX_IPO_SEATS - 1)) == 0);
static_assert(QPOOL_MAX_IPO_SEATS * 4 >= QPOOL_MAX_IPO_TARGETS * NUMBER_OF_COMPUTORS * 5);

// ---- Timing. TARGET_TICK_DURATION is 1000 ms, so 1 tick ~ 1 second.
constexpr uint64 QPOOL_TICKS_PER_HOUR   = 3600;
constexpr uint64 QPOOL_VOTE_WINDOW      = 72 * QPOOL_TICKS_PER_HOUR; // 3 days
// A passed proposal must be executed within this window or it expires, so a
// stale mandate cannot be fired months later at a very different price. The window starts
// where the VOTE ENDS (openTick + the vote window of the proposal's kind, see VoteWindowOf), not when somebody calls Tally, so the day on
// which a mandate stops being valid is fixed when the proposal opens (audit C019, owner
// decision D18).
constexpr uint64 QPOOL_EXECUTE_WINDOW   = 48 * QPOOL_TICKS_PER_HOUR; // 2 days
// BUY_IPO proposals are voted on for one day, not three: an IPO auction is won and lost in the last hours of its epoch (owner decision D1),
// so a pool that has decided to bid must be able to do it while it matters. Every other kind keeps QPOOL_VOTE_WINDOW. The execute window
// after the vote is the same for every kind. Ticks are NOMINAL: TARGET_TICK_DURATION is 1000 ms, so these are 86,400 ticks (one day) only
// while the network runs at one tick per second; a slower or faster network stretches or shrinks every window in real time.
constexpr uint64 QPOOL_IPO_VOTE_WINDOW  = 24 * QPOOL_TICKS_PER_HOUR; // 1 day
// The whole life of a proposal of every kind but BUY_IPO: it can be voted on for VOTE_WINDOW ticks and executed for EXECUTE_WINDOW more
// (a BUY_IPO has its own, QPOOL_IPO_PROPOSAL_LIFETIME below). After openTick + the life of its kind nothing can be done with it any more (Tally expires a late pass, Execute
// expires a passed mandate, Propose treats the slot as free), and the QU a vote locked is free again by itself.
constexpr uint64 QPOOL_PROPOSAL_LIFETIME = QPOOL_VOTE_WINDOW + QPOOL_EXECUTE_WINDOW;
constexpr uint64 QPOOL_IPO_PROPOSAL_LIFETIME = QPOOL_IPO_VOTE_WINDOW + QPOOL_EXECUTE_WINDOW;   // the same for a BUY_IPO (see VoteWindowOf)
static_assert(QPOOL_IPO_VOTE_WINDOW <= QPOOL_VOTE_WINDOW);
// qpi.tick() is a 32-bit number; a tick stored in a record is clamped to it (an expiry cannot pass the last tick there is).
constexpr uint64 QPOOL_TICK_LIMIT        = 0xFFFFFFFFULL;

// ---- Vote thresholds (permille of eligible weight) ----
constexpr uint64 QPOOL_QUORUM_PERMILLE  = 500; // 50% of eligible weight must vote
static_assert(QPOOL_QUORUM_PERMILLE <= 1000);

// ---- Fees ---------------------------------------------------------------
// 10,000,000 QU (owner decision D8, final). Twice MSVAULT's 5M vault-registration fee, the corpus norm for "create a container that holds
// funds": enough to price spam. It is also the only brake on exhausting the pool table: all 1,024 pools cost 10.24 billion QU, and a pool
// is the unit every per-pool quota is counted in, so the same figure prices every attack that needs more than one pool's quota.
constexpr uint64 QPOOL_CREATE_POOL_FEE  = 10000000ULL;
// Charged once per (address, pool): the first time the address becomes a member of
// the GIVEN pool (a roster slot and a Contribution record, which are one fact: see
// EnsureMember), whichever way it arrives -- Contribute (taken from the attached QU),
// BuyPosition (taken from the QU attached beyond the ask price) or
// DepositHoldingAsset (the pool's creator only, since K5; he pays NO join fee: see there). Everything a member does
// afterwards is free of it -- this only prices becoming a NEW member, the same
// way QPOOL_CREATE_POOL_FEE prices becoming a new pool, so spinning up many
// identities just to flood a pool's roster isn't free by ANY route.
// 10,000 QU (owner decision D8, final): filling the 256 places of one roster costs 256 x 10,000 = 2.56 million QU, exhausting nothing but
// that pool.
constexpr uint64 QPOOL_JOIN_FEE         = 10000ULL;
// Proposing is FREE. With a 2% management fee on payouts the contract already
// earns from pools that perform, so charging per proposal would double-dip and
// tax the governance the design depends on.
//
// Spam is held off structurally instead, the same way QPI's own ProposalVoting
// does it ("each proposer/computor can have one proposal at a time"): a member
// may have only ONE live proposal per pool. Without that, one member could fill
// all 256 slots and block governance for every pool.
constexpr uint64 QPOOL_PROPOSAL_FEE     = 0ULL;

// ---- Running (management) fee -------------------------------------------
// Charged as a share of every PAYOUT — dividends, sale proceeds, allocations —
// skimmed before the payout accumulator advances. Members receive the rest.
//
// Deliberately NOT a flat per-epoch charge. MSVAULT does that, and when a vault
// cannot pay its 500,000 QU holding fee the vault is wiped: balance seized,
// owners cleared, and its own code notes assets get stuck needing "manual
// patches". Applied here that would destroy member-owned holdings over an
// unpaid fee. A percentage of payouts cannot fail: it is self-funding, never
// touches principal or committed capital, needs no per-member debit to keep
// sum(member.uncommitted) == pool.uncommittedTotal, and only earns when a pool
// actually earns.
constexpr uint64 QPOOL_MANAGEMENT_PERMILLE = 10; // 1.0% of each payout
// Also charged on position-market sales (BuyPosition): a seat changing
// hands is still revenue-generating activity for the pool, same category
// as a dividend or a sale back to the market -- taken off the ask price
// before the seller is paid, never off the buyer's side.
static_assert(QPOOL_MANAGEMENT_PERMILLE < 1000);

// ---- Revenue split to QPOOL's own 676 IPO shares ------------------------
// The burn share is not only tokenomics: qpi.burn credits the contract's
// execution fee reserve, which QPOOL needs to keep running.
constexpr uint64 QPOOL_REVENUE_SHAREHOLDER_PERMILLE = 800; // 80%
constexpr uint64 QPOOL_REVENUE_BURN_PERMILLE        = 200; // 20%
static_assert(QPOOL_REVENUE_SHAREHOLDER_PERMILLE + QPOOL_REVENUE_BURN_PERMILLE == 1000);
// Below this, integer division per 676 shares rounds to nothing, so carry over.
constexpr uint64 QPOOL_MIN_DISTRIBUTION = 676000ULL;
constexpr sint64 QPOOL_QX_TRANSFER_FEE  = 100LL;
// SELL_QX needs the shares under QX's management. Handing them over costs QPOOL the QX
// management-rights fee once per source: shares QPOOL manages itself (deposits) and shares
// QSWAP manages (BUY_QSWAP) are two sources, so at most two fees. The minimum proceeds of a
// QX sale must exceed them, so a sale can never cost the other members of the contract money.
constexpr uint64 QPOOL_QX_SELL_FIXED_COSTS = 2 * (uint64)QPOOL_QX_TRANSFER_FEE;
// Dust floor: below this a contribution cannot be meaningfully split pro-rata.
constexpr uint64 QPOOL_MIN_CONTRIBUTION = 10000ULL;
// Mirrors QSWAP_ADDITIONAL_FEE (Qswap.h): SwapExactAssetForQu silently
// no-ops (output.quAmountOut stays 0, whatever was attached is refunded)
// unless the CALLER attaches at least this much QU with the invocation --
// unlike SwapExactQuForAsset (the BUY_QSWAP path), where the swap amount
// itself easily clears this floor, a SELL has no naturally-attached QU
// (the caller is selling the ASSET side), so it must be funded explicitly
// out of QPOOL's own balance.
constexpr sint64 QPOOL_QSWAP_SELL_FEE   = 100000LL;

// ---- Proposal kinds ----
constexpr uint64 QPOOL_KIND_BUY_QSWAP   = 1; // atomic swap QU -> asset
constexpr uint64 QPOOL_KIND_BUY_QX      = 2; // place QX bid order
constexpr uint64 QPOOL_KIND_BUY_IPO     = 3; // bid in a contract IPO
constexpr uint64 QPOOL_KIND_SELL_QSWAP  = 4; // atomic swap asset -> QU
constexpr uint64 QPOOL_KIND_SELL_QX     = 5; // sell now on QX: limit ask matched in the call, unfilled rest cancelled
constexpr uint64 QPOOL_KIND_ALLOCATE    = 6; // assign unattributed QU to a holding

// ---- Proposal status ----
constexpr uint64 QPOOL_PROP_OPEN      = 0;
constexpr uint64 QPOOL_PROP_PASSED    = 1;
constexpr uint64 QPOOL_PROP_REJECTED  = 2;
constexpr uint64 QPOOL_PROP_EXECUTED  = 3;
constexpr uint64 QPOOL_PROP_EXPIRED   = 4;
constexpr uint64 QPOOL_PROP_FAILED    = 5;

// ---- External contracts ----
constexpr uint16 QPOOL_QX_CONTRACT_INDEX    = 1;
constexpr uint16 QPOOL_QSWAP_CONTRACT_INDEX = 13;

// ---- Return codes ----
constexpr uint32 QPOOL_OK = 0;
constexpr uint32 QPOOL_ERR_INVALID_INPUT = 1;
constexpr uint32 QPOOL_ERR_CAPACITY = 2;
constexpr uint32 QPOOL_ERR_NOT_FOUND = 3;
constexpr uint32 QPOOL_ERR_NOT_MEMBER = 4;
constexpr uint32 QPOOL_ERR_INSUFFICIENT_FEE = 5;
constexpr uint32 QPOOL_ERR_INSUFFICIENT_BALANCE = 6;
constexpr uint32 QPOOL_ERR_WRONG_STATUS = 7;
constexpr uint32 QPOOL_ERR_WINDOW_OPEN = 8;
constexpr uint32 QPOOL_ERR_WINDOW_CLOSED = 9;
constexpr uint32 QPOOL_ERR_ALREADY_VOTED = 10;
constexpr uint32 QPOOL_ERR_EXTERNAL_CALL = 11;
constexpr uint32 QPOOL_ERR_TRANSFER_FAILED = 12;
constexpr uint32 QPOOL_ERR_NOTHING_OWED = 13;
constexpr uint32 QPOOL_ERR_PRICE_TOO_HIGH = 14;
constexpr uint32 QPOOL_ERR_NO_OFFER = 15;
constexpr uint32 QPOOL_ERR_IPO_REJECTED = 16;
constexpr uint32 QPOOL_ERR_IPO_PRICE_TAKEN = 17;
constexpr uint32 QPOOL_ERR_IPO_NOT_ACTIVE = 18;
constexpr uint32 QPOOL_ERR_ABOVE_CEILING = 19;      // unused: a BUY_IPO has no ceiling price (owner decision D1, final: the budget is the only limit); kept so numbering stays append-only
constexpr uint32 QPOOL_ERR_PROPOSAL_PENDING = 20;
constexpr uint32 QPOOL_ERR_NOT_ADMITTED = 21;
constexpr uint32 QPOOL_ERR_NOT_AUTHORIZED = 22;
constexpr uint32 QPOOL_ERR_SOURCE_CONFLICT = 23;    // this payer is already bound to a different asset
constexpr uint32 QPOOL_ERR_SETTLEMENT_PENDING = 24; // dividends for this asset are parked awaiting settlement; also: the seats of an IPO holding that has not been settled yet cannot be offered or bought
constexpr uint32 QPOOL_ERR_QX_BID_PENDING = 25;     // a QX bid on this payer's asset has not been reconciled yet
constexpr uint32 QPOOL_ERR_DISABLED = 26;           // unused since KIND_SELL_QX was repaired; kept so numbering stays append-only
constexpr uint32 QPOOL_ERR_QX_ONLY = 27;            // contract shares can only be sold through QX, never swapped on QSWAP
constexpr uint32 QPOOL_ERR_CONTRACT_MISMATCH = 28;  // the asset name and the paying contract's index do not belong together
constexpr uint32 QPOOL_ERR_CONTRACT_NOT_LIVE = 29;  // the paying contract does not exist, or is still in its IPO
constexpr uint32 QPOOL_ERR_NO_FILL = 30;            // SELL_QX: the visible bid book cannot pay the minimum at the limit price now
constexpr uint32 QPOOL_ERR_BOOKS_INCONSISTENT = 31; // defence in depth: a pool's records do not add up (a roster or balance defect); refused before anything moved
constexpr uint32 QPOOL_ERR_SEAT_LOCKED = 32;        // the seat took part in a vote on a SELL or ALLOCATE proposal that is still open: it cannot be offered or sold until that vote ends
constexpr uint32 QPOOL_ERR_DEPOSIT_AFTER_OPEN = 33; // QU was added to the member's balance after this BUY proposal opened: that money has no vote on it
constexpr uint32 QPOOL_ERR_NAME_UNPROVEN = 34;      // SettleIpo: the holding won seats but the shares of its declared name cannot be proven to be them; nothing was settled
constexpr uint32 QPOOL_ERR_IPO_NO_SNAPSHOT = 35;    // SettleIpo: the target's IPO is over but QPOOL's END_EPOCH left no snapshot of its book; nothing can be settled
constexpr uint32 QPOOL_ERR_PRICE_TOO_LOW = 36;      // RebidIpo: the book is full and the price does not exceed its minimum, so the bid could not win a seat
constexpr uint32 QPOOL_ERR_SEATS_FULL = 37;         // unused: a BUY_IPO has no voted quantity (seats are what the money buys); kept so numbering stays append-only
constexpr uint32 QPOOL_ERR_OFFER_CHANGED = 38;      // BuyPosition: the live offer is not the parcel the buyer expected (the seller replaced it after the buyer read it); refunded, nothing written

// ---- Dividend source kinds ----
constexpr uint64 QPOOL_SOURCE_TOKEN    = 0; // QU sent by the asset's issuer (standardTransaction / qpiTransfer)
constexpr uint64 QPOOL_SOURCE_CONTRACT = 1; // qpi.distributeDividends() of a contract whose shares we hold

// ---- Contract shares: assets whose issuer is NULL_ID ----
// A contract's shares are issued by the protocol, so the asset says nothing about WHICH
// contract pays on it, and contracts cannot read contractDescriptions. The paying
// contract's index therefore always comes from the user (the proposal's holdingId field
// for BUY_QX, an input field for DepositHoldingAsset, the IPO target for BUY_IPO), and
// the link between that index and the asset name is checked as far as it can be:
//   - indices 1..QPOOL_KNOWN_CONTRACTS: against the mainnet table (KnownContractName),
//     exactly;
//   - higher indices: not verifiable. A name already bound to one index is refused for
//     another (contractNames), a ledger bound to one name refuses another, and for shares
//     that already exist the index must be a contract that exists and has left its IPO.
constexpr uint64 QPOOL_KNOWN_CONTRACTS   = 30;   // size of the embedded table
// The protocol caps the number of contracts at MAX_NUMBER_OF_CONTRACTS = 1024 (common_def.h),
// so no valid index is above 1023. The live bound (the contract must exist) is read from the
// core with qpi.ipoBidPrice, so nothing here needs updating when a contract is added.
constexpr uint64 QPOOL_MAX_CONTRACT_INDEX = 1023;
constexpr uint64 QPOOL_MAX_ASSET_NAME    = 0xFFFFFFFFFFFFFFULL; // asset names are at most 7 bytes
// name -> index for bound ledgers above the table: at most one entry per contract index, so
// at most 1023 - 30 = 993 entries can ever exist; 2048 keeps the table under 50 percent full.
constexpr uint64 QPOOL_MAX_CONTRACT_NAMES = 2048;
static_assert((QPOOL_MAX_CONTRACT_NAMES & (QPOOL_MAX_CONTRACT_NAMES - 1)) == 0);
static_assert(QPOOL_MAX_CONTRACT_NAMES > QPOOL_MAX_CONTRACT_INDEX - QPOOL_KNOWN_CONTRACTS);

struct QPOOL2 {};

struct QPOOL : public ContractBase
{
    struct Pool
    {
        id creator;
        uint64 poolName;
        uint64 uncommittedTotal; // QU contributed and not yet deployed
        // QU deployed into the holdings this pool BOUGHT (not deposited) and still has on its books: the sum of their costBasis. It grows
        // with every acquisition and falls where a holding's cost leaves the books -- SettleIpo hands back the part of an IPO holding's budget
        // that did not win seats, and ClaimPayout frees a retired holding at its last claim (until then a retired holding's cost stays in it). It is not a
        // lifetime total.
        uint64 committedTotal;
        uint64 contributorCount;
        uint64 holdingCount;     // holding slots this pool occupies now (at most QPOOL_MAX_HOLDINGS_PER_POOL); a slot is given back when its holding is freed
        uint64 unattributedQu;   // QU received that could not be mapped to a holding
        // 0 = open to anyone, 1 = permissioned (creator curates the member set).
        //
        // IMPORTANT: this is ACCESS CONTROL, not confidentiality. Every byte of
        // contract state is replicated to every node, so member addresses,
        // contributions and holdings are public whatever this is set to. A
        // "private" pool controls who may JOIN; it cannot hide who has joined.
        uint64 permissioned;
        // Reserved for a future zero-knowledge membership set: with the planned
        // BN254+Groth16 verifier in core, a member could prove inclusion in an
        // admitted set via a Merkle proof without revealing which member they
        // are. Held as a field NOW because changing StateData's size later
        // forces a state migration; reserving it costs one id and avoids that.
        // Zero means unused, and the explicit allowlist governs.
        id admissionRoot;
        uint64 active;
        // ---- what this pool uses of the tables all pools share (see "Per-pool quotas" at the constants) ----
        // Each counter is written at the very place the row it counts is added or removed, and the check against the quota is made
        // BEFORE the first write of the call that would add the row.
        uint64 offerCount;      // open offers of seats of this pool's holdings, at most QPOOL_MAX_OFFERS_PER_POOL
        uint64 admittedCount;   // allow-list entries of this pool, at most QPOOL_MAX_ADMITTED_PER_POOL
        uint64 ownershipRows;   // seat rows (member, holding) of this pool's holdings, at most QPOOL_MAX_OWNERSHIP_PER_POOL
        uint64 depositCount;    // holdings of this pool that its creator deposited, at most QPOOL_MAX_DEPOSITS_PER_POOL
        uint64 ipoHoldings;     // unsettled IPO holdings of this pool, at most QPOOL_MAX_IPO_HOLDINGS_PER_POOL
    };

    struct ContribKey
    {
        id member;
        uint64 poolId;
        bool operator==(const ContribKey& o) const { return member == o.member && poolId == o.poolId; }
        bool operator!=(const ContribKey& o) const { return member != o.member || poolId != o.poolId; }
    };

    struct Contribution
    {
        uint64 uncommitted; // withdrawable until deployed
        uint64 committed;   // lifetime QU deployed on this member's behalf
        // QU currently backing this member's cast BUY-kind votes in this pool. WithdrawUncommitted may never take
        // uncommitted below this, so a member cannot vote with capital and then reclaim it before the vote has done
        // its work -- a free, riskless veto or mandate that would otherwise let one wallet serially block or carry
        // every proposal in a pool by recycling the same QU (audit C006, owner decision D3). The lock of a YES vote
        // lasts until the mandate is carried out (Execute) or lapses; that of a NO vote until Tally.
        //
        // It binds only while qpi.tick() <= lockExpiryTick: a lock that has expired reads as 0 and costs no loop to
        // lift. lockExpiryTick is the latest end of life of the proposals the member voted on (openTick + the life of the
        // proposal's KIND, see VoteWindowOf: QPOOL_IPO_PROPOSAL_LIFETIME for a BUY_IPO, QPOOL_PROPOSAL_LIFETIME for every other
        // kind), so a vote that nobody ever tallies or executes cannot hold the QU for ever. Read the lock only
        // through the rule above (WithdrawUncommitted, GetMember); the number itself is meaningless after the expiry.
        uint64 lockedForVotes;
        // The member's position in the pool's roster: members[poolId * 256 + memberIdx] is this member.
        // Written once, by EnsureMember, when the roster slot and this record are created together; it
        // never changes (the roster is append-only). The roster loops (Execute, SettleIpo, Tally) act on
        // a roster slot only when it is the slot this record names, so an id that was ever listed twice
        // would still be charged, refunded and unlocked once.
        uint64 memberIdx;
        // Ticks are 32 bits (qpi.tick()), so the three tick fields below take 12 bytes and the record is 48 bytes (as it would be
        // with two 64-bit ticks), not 56.
        //
        // lockedForVotes is a sum, kept exact without a loop over anything: a vote that arrives when the lock has expired
        // starts it again from nothing (lockStartTick = that tick); a vote that arrives while it binds adds to it; the
        // expiry is the latest end of life of the proposals voted on. A vote row is part of the sum exactly when the lock
        // started no later than the end of that proposal's vote window (lockStartTick <= openTick + the vote window of its KIND,
        // QPOOL_IPO_VOTE_WINDOW for a BUY_IPO, QPOOL_VOTE_WINDOW otherwise): a lock that started after it did so after the lock the
        // vote belonged to had expired (expiry >= openTick + the life of the kind, and the new lock starts later), and that lock
        // took the vote's weight with it. ReleaseVotes relies on this to lift
        // a row's weight exactly once and never a weight that is not there.
        uint32 lockStartTick;
        // The last tick at which lockedForVotes still binds (see there).
        uint32 lockExpiryTick;
        // The tick at which QU was last added to `uncommitted` (Contribute; SettleIpo handing capital back). A BUY
        // proposal weighs only the money that was there when it opened, so a member whose balance grew after the
        // proposal opened (lastDepositTick >= openTick) has no vote on it: money cannot be brought in to carry or
        // veto a decision that was already open (audit C006, owner decision D3).
        uint32 lastDepositTick;
    };

    struct Holding
    {
        uint64 poolId;
        Asset asset;
        uint64 shares;            // shares currently held
        uint64 costBasis;         // permanent ownership denominator
        uint128 payoutPerCostX64; // dividends + sale proceeds per QU of cost
        uint64 totalPayouts;
        // 1 = the asset was DEPOSITED by the pool's creator rather than bought with
        // pooled QU. costBasis then means "seat units" declared by the
        // depositor, not QU spent, so it is excluded from pool.committedTotal.
        uint64 deposited;
        uint64 ipoContractIndex;  // non-zero for an IPO-acquired holding
        uint64 pendingShares;     // QX bid: ordered but not yet confirmed. IPO holding: the seats the holding held when it was last
                                  // reconciled plus what its last bid seated -- a number for the eye only, the book moves on; the
                                  // snapshot at the end of the IPO epoch decides (SettleIpo)
        // An IPO holding bids in Execute (its opening price) and in RebidIpo (any price its free funds pay for). A bid that is outbid is
        // refunded immediately (TransferType::ipoBidRefund) into the pot. The budget (costBasis) is committed once, at Execute, and it is the
        // ONLY limit of the holding: the seats it holds are whatever the money buys at the prices it bids. ipoAtRisk + ipoAvailable == costBasis
        // for as long as the holding is unsettled: ipoAtRisk is what the holding's bids put into the book and has not been moved back,
        // ipoAvailable the FREE funds (the never-bid rest of the budget and everything outbid that a reconcile has moved back from the pot),
        // which a rebid spends. SettleIpo hands back ipoAvailable plus what the book returned of ipoAtRisk (everything it did not spend on
        // the seats it won) and sets both to 0.
        // ipoMaxPrice is the highest price the holding has bid so far. The holding's bid prices are in StateData::ipoBids; every slot of
        // the target's book that QPOOL holds at one of them belongs to this holding, which is why a price is never used by two live holdings
        // of one target.
        uint64 ipoMaxPrice;
        uint64 ipoAtRisk;         // QU its bids put into the book and not moved back to free funds
        uint64 ipoAvailable;      // free funds: QU not in the book that a rebid can spend
        uint64 active;
        // ---- dividend accounting (see SourceLedger) ----
        // The holding's dividend source is DERIVED, never stored: id(ipoContractIndex,0,0,0) for an IPO
        // holding or id(payerIndex,0,0,0) for bought or deposited contract shares (kind CONTRACT),
        // otherwise asset.issuer (kind TOKEN).
        uint128 srcSnapX64;       // SourceLedger.accX64 at this holding's last sync
        uint64 ipoSettled;        // 1 once SettleIpo has run (IPO holdings only)
        // Contract shares that are NOT an IPO holding (bought on QX or deposited): the
        // index of the contract that pays on them, so the holding earns from the same
        // CONTRACT ledger as an IPO holding of that contract. 0 for every other holding.
        uint64 payerIndex;
        // The seat rows (member, holding) that exist for this holding. Written where a row is added or removed. A RETIRED holding (sold
        // out: active 0, costBasis kept for the claims still open on it) is freed when its last row is claimed and removed, which is the
        // only reason this count exists. (A roster has at most 256 members, so 32 bits are plenty: the field shares its 8 bytes with
        // `incarnation`, which is how the tag below costs the state nothing.)
        uint32 rows;
        // Which holding this slot holds NOW: StateData::holdingSerial after the increment that its creation (Execute, DepositHoldingAsset)
        // made (modulo 2^32), so a holding that is freed and replaced in the same slot has another number (the slot is not an identity: see
        // "A SLOT IS NOT AN IDENTITY" at the top for why 32 bits tell apart everything a proposal can meet). 0 in a free slot -- the places that
        // free a slot blank it or clear this field. A SELL or ALLOCATE proposal records it (Proposal.holdingTag); Vote and Execute compare it.
        uint32 incarnation;
    };

    struct OwnKey
    {
        id member;
        uint64 holdingId;
        bool operator==(const OwnKey& o) const { return member == o.member && holdingId == o.holdingId; }
        bool operator!=(const OwnKey& o) const { return member != o.member || holdingId != o.holdingId; }
    };

    struct Ownership
    {
        uint64 cost;            // this member's permanent share of costBasis
        uint128 payoutDebtX64;  // accumulator snapshot at last crystallisation
        uint64 claimable;       // crystallised QU owed
        // A SELL or ALLOCATE vote weighs the voter's seat units in the holding, and the same units must not be able
        // to vote twice. So a member who has voted keeps his seat until the vote can no longer be cast: while
        // qpi.tick() <= voteLockUntilTick the seat can neither be offered (OfferPosition) nor sold (BuyPosition),
        // otherwise the units could be sold on to a second wallet, vote again, and so on (audit C003). The lock is
        // openTick + VOTE_WINDOW of the latest proposal he voted on (a SELL or ALLOCATE: the default window) and lifts by itself at that tick; the seat
        // keeps paying dividends and can be claimed as always.
        uint64 voteLockUntilTick;
    };

    // One ledger per dividend SOURCE (a paying contract or a paying token issuer),
    // shared by every holding of the asset that source pays on.
    struct SourceKey
    {
        id source;      // id(contractIndex,0,0,0) for QPOOL_SOURCE_CONTRACT, the issuer for QPOOL_SOURCE_TOKEN
        uint64 kind;
        bool operator==(const SourceKey& o) const { return source == o.source && kind == o.kind; }
        bool operator!=(const SourceKey& o) const { return source != o.source || kind != o.kind; }
    };

    struct SourceLedger
    {
        uint128 accX64;      // cumulative NET dividend per recognised share, x 2^64
        uint128 accruedX64;  // cumulative net moved into accX64 (QU x 2^64): the solvency ceiling
        uint128 creditedX64; // cumulative amount holdings have synced out of accX64 (never exceeds accruedX64)
        uint64 assetName;    // the single asset this source is bound to (CONTRACT: 0 for an IPO-only ledger until its first winning settlement, bound at registration otherwise)
        uint64 holdings;     // live holdings registered here
        uint64 recognized;   // R: sum(shares) over live holdings
        uint64 unsettled;    // IPO holdings not yet through SettleIpo (CONTRACT kind only)
        uint64 escrow;       // net QU parked until it can be paid to the right holdings
        uint64 pendingQx;    // sum(pendingShares) over live QX-bid holdings (TOKEN or CONTRACT kind)
        uint64 bindEpoch;    // qpi.epoch() when the ledger was created: how long a binding that never paid has stood (EvictSquat)
    };

    struct Proposal
    {
        uint64 poolId;
        uint32 kind;
        // SELL / ALLOCATE only: the incarnation (Holding.incarnation) of the holding the proposal was opened on; 0 for every other kind. The
        // field sits beside `kind` in the 8 bytes that `kind` took alone, so it adds nothing to the state.
        uint32 holdingTag;
        id proposer;
        Asset asset;
        uint64 quAmount;    // QU to deploy (buys) or min proceeds (sells); for a BUY_IPO the BUDGET, the only limit of the holding
        uint64 shares;      // shares to sell, or QX share count; for a BUY_IPO NOT voted: the opening quantity min(676, budget / opening price), derived by Propose
        uint64 price;       // QX limit price, or the OPENING price per share of an IPO (the first bid, a starting point and not a limit)
        uint64 holdingId;   // target holding for sells / allocations
        uint64 yesWeight;
        uint64 noWeight;
        uint64 eligibleWeight; // weight snapshot when the vote opened
        uint64 openTick;
        uint64 passedTick;
        uint64 status;
    };

    struct OfferKey
    {
        id seller;
        uint64 holdingId;
        bool operator==(const OfferKey& o) const { return seller == o.seller && holdingId == o.holdingId; }
        bool operator!=(const OfferKey& o) const { return seller != o.seller || holdingId != o.holdingId; }
    };

    struct Offer
    {
        uint64 costUnits; // cost basis offered for sale
        uint64 askPrice;  // QU demanded for the whole parcel
        uint64 poolId;    // the pool whose quota (Pool.offerCount) the offer counts against: whoever removes the offer gives it back
    };

    // (target contract, bid price) of a live IPO holding: see StateData::ipoPrices.
    struct IpoPriceKey
    {
        uint64 ipoContractIndex;
        uint64 price;
        bool operator==(const IpoPriceKey& o) const { return ipoContractIndex == o.ipoContractIndex && price == o.price; }
        bool operator!=(const IpoPriceKey& o) const { return ipoContractIndex != o.ipoContractIndex || price != o.price; }
    };

    // The bid prices of ONE unsettled IPO holding (StateData::ipoBids, keyed by the holding's slot). The list holds every price the holding
    // has a registry entry for (ipoPrices), live ones and, until the next reconcile, prices whose last slot has been lost. seatsAt is the number
    // of slots QPOOL held at each price when the holding was last reconciled (RebidIpo) or placed the bid (a number for the eye and for a rebid
    // bot; the contract never trusts it with money: every rebid and the settlement read the book again; its one use is a cheap refusal, and
    // only for the prices above the book's minimum, where it is exact).
    struct IpoBids
    {
        uint64 count;                                 // prices in the list: prices.get(0 .. count - 1)
        uint64 seats;                                 // sum of seatsAt over the list
        Array<uint64, QPOOL_MAX_IPO_BIDS> prices;
        Array<uint64, QPOOL_MAX_IPO_BIDS> seatsAt;
    };

    // One row per IPO target contract that has unsettled QPOOL holdings (StateData::ipoTargets). A row is created by the first
    // bid (Execute), is filled by END_EPOCH at the end of the target's IPO epoch (the snapshot) and is freed by the settlement of
    // the last holding.
    struct IpoTarget
    {
        uint64 ipoContractIndex; // 0 = the row is free
        uint64 unsettled;        // QPOOL's IPO holdings on this target that have not been through SettleIpo (= registry entries of the target)
        uint64 bidEpoch;         // qpi.epoch() of the first bid: the target's IPO epoch
        uint64 snapshotEpoch;    // the epoch END_EPOCH took the snapshot in; 0 = not taken yet (the auction is running, or END_EPOCH has not run)
        uint64 finalPrice;       // the clearing price every winner pays: the price of the book's last slot; 0 = the book was not full, the IPO failed
    };

    // What QPOOL's slots at ONE bid price of ONE target came to when the book was read for the last time (StateData::ipoSeats,
    // keyed like ipoPrices by (target, price)). A price belongs to one holding, so this is that holding's result at that price; a holding
    // with several prices has several rows and SettleIpo adds them up. No row = nothing won at that price. Both numbers are 0 for a failed IPO.
    struct IpoSeat
    {
        uint64 shares; // seats won: one share each
        uint64 spent;  // what they cost: shares x finalPrice
    };

    // proposalId is NOT the proposal's slot: slots are reused, and a vote row of an earlier proposal must never be
    // taken for a vote on the one that sits in the slot now (audit C051). It is openTick * QPOOL_MAX_PROPOSALS + slot,
    // which names one proposal for ever: a slot is reused only after its proposal's vote window has passed, so no two
    // proposals of one slot open at the same tick. Callers keep passing the slot; the key is built from the proposal.
    struct VoteKey
    {
        id voter;
        uint64 proposalId;
        bool operator==(const VoteKey& o) const { return voter == o.voter && proposalId == o.proposalId; }
        bool operator!=(const VoteKey& o) const { return voter != o.voter || proposalId != o.proposalId; }
    };

    // A vote row lives from Vote until the proposal's vote is over for good (see ReleaseVotes): it is how Execute knows
    // who said yes and how much (a BUY charges only them), and how the lock of a BUY vote is lifted.
    struct VoteChoice
    {
        uint64 weight;   // recorded at vote time: the QU (buys) or seat units (sells) the vote counts, and the QU it locks
        uint64 approve;  // 1 = yes, 0 = no -- only a YES voter is charged by a BUY
    };

    struct StateData
    {
        Array<Pool, QPOOL_MAX_POOLS> pools;
        uint64 poolCount;

        // Flattened member roster: pool p owns [p*256, p*256+256).
        Array<id, QPOOL_MAX_POOLS * QPOOL_MAX_CONTRIBUTORS_PER_POOL> members;
        // The lowest pool slot that may be free: every slot below it is an active pool (CreatePool starts its search here, CloseEmptyPool lowers it).
        uint64 poolFreeHint;

        HashMap<ContribKey, Contribution, QPOOL_MAX_CONTRIBUTIONS> contributions;
        // (member, poolId) -> 1 for permissioned pools. Unused for open pools.
        HashMap<ContribKey, uint64, QPOOL_MAX_ADMITTED> admitted;
        Array<Holding, QPOOL_MAX_HOLDINGS> holdings;
        uint64 holdingCount;        // one more than the highest holding slot ever used
        // The lowest holding slot that may be free (a slot is free when costBasis == 0 and active == 0): every slot below it is occupied, so a
        // search for a free slot starts here and is O(1) in the usual case. A creation moves it past the slot it takes, a freed slot lowers it.
        uint64 holdingFreeHint;
        // The number of holdings ever created (its low 32 bits are what is stored in a holding): every creation takes the next one as the new
        // holding's incarnation, so the live holdings' numbers are all different (see Holding::incarnation).
        uint64 holdingSerial;
        HashMap<OwnKey, Ownership, QPOOL_MAX_OWNERSHIP> ownership;
        Array<Proposal, QPOOL_MAX_PROPOSALS> proposals;
        uint64 proposalCount;
        HashMap<VoteKey, VoteChoice, QPOOL_MAX_OWNERSHIP> votes;
        HashMap<OfferKey, Offer, QPOOL_MAX_OFFERS> offers;
        // payer -> ledger of the asset it pays on. Holds NO holding ids, so a
        // retired or reused holding slot can never be a stale target.
        HashMap<SourceKey, SourceLedger, QPOOL_MAX_SOURCES> sources;
        // asset name -> contract index, for CONTRACT ledgers above the embedded table that
        // are bound to a name. Keeps the pairing one-to-one: a name bound to one index is
        // refused for another, so one contract's shares can never be counted in another
        // contract's ledger. Entry lifetime = the ledger's.
        HashMap<uint64, uint64, QPOOL_MAX_CONTRACT_NAMES> contractNames;
        // (target contract, bid price) -> the IPO holding that bid at that price, for every UNSETTLED IPO holding. The
        // order book of an IPO shows every slot QPOOL holds with its price and no owner, so slots are attributed to
        // holdings by price alone: two live holdings of one target must never bid at the same price. Execute checks
        // this map before it bids (Propose looks it up too, as an advisory O(1) check that spares the voters a doomed
        // vote) and inserts after the bid succeeded; RebidIpo releases the entries of prices that lost their last slot and adds the
        // price of a new bid; SettleIpo removes all of a holding's entries. Population == the prices in the lists of ipoBids.
        HashMap<IpoPriceKey, uint64, QPOOL_MAX_IPO_PRICES> ipoPrices;
        // holding slot -> the bid prices of every UNSETTLED IPO holding (see IpoBids). Population == the unsettled IPO holdings;
        // every price in a list has an ipoPrices entry naming the holding, and every ipoPrices entry is in its holding's list.
        HashMap<uint64, IpoBids, QPOOL_MAX_IPO_HOLDINGS> ipoBids;
        // The IPO snapshot (see "IPO SETTLEMENT" at the top): the targets that have unsettled holdings, and what QPOOL's slots at each
        // bid price came to when END_EPOCH read the book. Rows of ipoSeats are removed by the settlement of their holding.
        Array<IpoTarget, QPOOL_MAX_IPO_TARGETS> ipoTargets;
        HashMap<IpoPriceKey, IpoSeat, QPOOL_MAX_IPO_SEATS> ipoSeats;
        uint64 dividendEscrowTotal; // sum of SourceLedger.escrow, for accounting checks
        uint64 totalUnownedSwept;   // escrow nobody could claim, moved to revenue when its source emptied

        uint64 totalContributed;
        uint64 totalDeployed;
        uint64 totalPayoutsDistributed;
        uint64 globalUnattributed; // arrivals that match no pool or holding
        // Refunds carry no IPO identity, so they are NOT attributed on arrival: every ipoBidRefund (an evicted slot, a bid the book
        // did not seat, the premium over the final price, the whole bid of a failed IPO) is added here. SettleIpo does NOT take
        // "whatever is in the pot": it works out what ITS holding is owed from the snapshot (budget bid - cost of the seats won) and
        // draws exactly that, so the pot cannot be drained by whoever settles first (audit C004). The pot is the solvency clamp: a
        // holding is never credited more than has arrived.
        uint64 ipoRefundEscrow;
        uint64 totalIpoRefunded;

        // ---- revenue ----
        uint64 pendingRevenue;              // collected, awaiting distribution
        uint64 totalRevenue;                // lifetime fees collected
        uint64 totalManagementFees;         // lifetime skimmed from payouts
        uint64 totalDistributedToShareholders;
        uint64 totalBurned;
    };

    // ======================== I/O structs ========================

    // ---- dividend-source helpers (private) ----
    // Every helper initialises its OWN output first: CALL() zeroes the callee's locals
    // but not the caller's output struct, so a reused output would carry stale values.

    // payerIndex: Holding.payerIndex (contract shares that are not an IPO holding).
    struct SourceKeyOf_input { Asset asset; uint64 ipoContractIndex; uint64 payerIndex; };
    // contractIndex: the paying contract for a CONTRACT key, 0 for a TOKEN key.
    struct SourceKeyOf_output { SourceKey key; uint32 valid; uint64 contractIndex; };

    // The embedded mainnet table: index -> asset name, asset name -> index (0 = not listed).
    struct KnownContractName_input { uint64 index; };
    struct KnownContractName_output { uint64 name; };
    struct KnownContractIndex_input { uint64 name; };
    struct KnownContractIndex_output { uint64 index; };

    // Static part of the pairing check (table + one-to-one map). Safe to call anywhere,
    // including when an IPO holding is settled (the shares exist by then).
    struct ContractPairKnown_input { uint64 index; uint64 assetName; };
    struct ContractPairKnown_output { uint32 returnCode; };
    struct ContractPairKnown_locals
    {
        KnownContractName_input nIn; KnownContractName_output nOut;
        KnownContractIndex_input iIn; KnownContractIndex_output iOut;
        uint64 mapped;
    };
    // Registration-time pairing check: the static part plus what the core can prove now.
    // ipo != 0: a bid in the contract's IPO (its shares cannot exist yet); ipo == 0:
    // shares that already exist (bought on QX or deposited).
    struct CheckContractPair_input { uint64 index; uint64 assetName; uint64 ipo; };
    struct CheckContractPair_output { uint32 returnCode; };
    struct CheckContractPair_locals
    {
        ContractPairKnown_input kIn; ContractPairKnown_output kOut;
        sint64 phase;
    };

    struct CheckSource_input { Asset asset; uint64 ipoContractIndex; uint64 payerIndex; uint64 qxBid; };
    struct CheckSource_output { uint32 returnCode; };
    struct CheckSource_locals
    {
        SourceKeyOf_input kin; SourceKeyOf_output kout; SourceLedger led;
        CheckContractPair_input pIn; CheckContractPair_output pOut;
    };

    struct SyncMath_input { Holding h; SourceLedger led; };
    struct SyncMath_output { uint128 ppc; uint128 snap; uint128 credited; };
    struct SyncMath_locals { uint128 credit; uint128 room; };

    // The read-only half of SyncHolding (audit C022): the holding as it will be once it has pulled what its source ledger has accrued since its
    // last sync, with `changed` = 1 when that is not the holding it was given (the ledger has moved on since the holding's snapshot). It writes nothing,
    // so a call that only wants to know what a claim would pay can refuse without having dirtied the state. key/led are the holding's ledger (only
    // meaningful when changed == 1), credited what the sync moves out of it.
    struct SyncPreview_input { Holding h; };
    struct SyncPreview_output { Holding h; uint64 escrow; uint64 changed; uint128 credited; SourceKey key; SourceLedger led; };
    struct SyncPreview_locals
    {
        SourceKeyOf_input kin; SourceKeyOf_output kout;
        SyncMath_input mIn; SyncMath_output mOut;
    };

    struct SyncHolding_input { uint64 holdingId; Holding h; };
    struct SyncHolding_output { Holding h; uint64 escrow; };
    struct SyncHolding_locals
    {
        SyncPreview_input pIn; SyncPreview_output pOut;
    };

    struct RegisterHolding_input { Holding h; };
    struct RegisterHolding_output { Holding h; uint32 returnCode; };
    struct RegisterHolding_locals
    {
        SourceKeyOf_input kin; SourceKeyOf_output kout; SourceLedger led;
        uint64 isNew;
    };

    struct ApplyHolding_input
    {
        uint64 holdingId; Holding h;      // h: the caller's CURRENT copy; shares/pendingShares are the OLD values
        uint64 newShares; uint64 newPending;
        uint64 markSettled;               // IPO holding: SettleIpo has run
        uint64 claimEscrow;               // QX-bid holding that just recognised fills: take the source's escrow
        uint64 retire;                    // the holding leaves its source
    };
    struct ApplyHolding_output { Holding h; uint32 returnCode; };
    struct ApplyHolding_locals
    {
        SourceKeyOf_input kin; SourceKeyOf_output kout; SourceLedger led;
        SyncHolding_input sIn; SyncHolding_output sOut;
        uint128 scaled; uint64 bound;
        uint64 newShares; uint128 rem; uint64 remQu;
    };

    struct GetSource_input { id source; uint64 kind; };
    struct GetSource_output
    {
        uint32 found; uint64 assetName; uint64 holdings; uint64 recognized; uint64 unsettled;
        uint64 escrow; uint64 pendingQx; uint64 accHigh; uint64 accLow;
    };
    struct GetSource_locals { SourceKey key; SourceLedger led; };

    // ---- the window of a proposal kind (private) ----
    // How long a proposal of this kind is voted on (window) and how long it lives in all, vote plus mandate (lifetime). BUY_IPO is voted on for
    // QPOOL_IPO_VOTE_WINDOW, every other kind for QPOOL_VOTE_WINDOW; the execute window is the same. One definition for Vote, Tally, the
    // execute window's anchor and the vote locks (ReleaseVotes), so that they cannot disagree. Propose's two scans over the proposal slots use
    // the two constants (QPOOL_PROPOSAL_LIFETIME and QPOOL_IPO_PROPOSAL_LIFETIME, each defined as window + execute window at the constants) inline:
    // a CALL per slot of a 262,144-slot scan would cost more than the scan.
    struct VoteWindowOf_input { uint64 kind; };
    struct VoteWindowOf_output { uint64 window; uint64 lifetime; };

    // ---- the table of IPO targets (private) ----
    // Where the target contract `index` sits in StateData::ipoTargets: found = 1 and `row`; if it has no row, hasFree = 1 and the first free
    // row in `free`. Read-only and O(QPOOL_MAX_IPO_TARGETS).
    struct IpoRowOf_input { uint64 index; };
    struct IpoRowOf_output { uint64 found; uint64 row; uint64 hasFree; uint64 free; };
    struct IpoRowOf_locals { uint64 i; IpoTarget r; };

    // ---- reconciling an IPO holding against the live book (private) ----
    // One pass over the 676 slots of the target's book: how many slots QPOOL holds at each price of the holding's list (seatsNow, in the order
    // of the list), their total (seats) and what they are worth at their own prices (value). Read-only; procedures only (the core read takes the
    // target's state lock while it is in its IPO epoch); the book must be readable (the caller has checked the IPO window).
    struct IpoSeatsNow_input { uint64 ipoContractIndex; IpoBids bids; };
    struct IpoSeatsNow_output { Array<uint64, QPOOL_MAX_IPO_BIDS> seatsNow; uint64 seats; uint64 value; uint64 live; };
    struct IpoSeatsNow_locals { uint64 i; uint64 k; sint64 price; uint64 n; };

    // The IPO snapshot of one target (found = 0: no unsettled QPOOL holding bids in it) and, if `price` is not 0, what QPOOL's slots at that bid
    // price came to: seats won and what they cost (both 0 until the snapshot exists, and for a holding that won nothing).
    struct GetIpoTarget_input { uint64 ipoContractIndex; uint64 price; };
    struct GetIpoTarget_output
    {
        uint32 found; uint64 unsettled; uint64 bidEpoch; uint64 snapshotEpoch; uint64 finalPrice;
        uint64 seats; uint64 spent;
    };
    struct GetIpoTarget_locals { IpoRowOf_input rIn; IpoRowOf_output rOut; IpoTarget row; IpoPriceKey key; IpoSeat seat; };

    // What a rebid bot needs to decide (a read of the contract's books; the book itself is read off-chain). found = 0: not an unsettled IPO
    // holding. budget is the money the holding was allocated (its cost basis: the ONLY limit it has; budget = freeFunds + atRisk), seats the
    // seats it held when it was last reconciled plus what its last bid seated (a number as of the last call: seats lost since are only found by
    // the next RebidIpo), freeFunds what a rebid can spend now (ipoAvailable; money that was outbid and has come back is added to it by the
    // next RebidIpo's reconcile, up to refundPot, the pot it comes from), atRisk the rest of the budget (what the holding's bids put into the
    // book and has not been moved back). prices/seatsAt: the holding's list.
    struct GetIpoBids_input { uint64 holdingId; };
    struct GetIpoBids_output
    {
        uint32 found; uint64 ipoContractIndex; uint64 budget; uint64 maxPrice; uint64 seats;
        uint64 freeFunds; uint64 atRisk; uint64 refundPot; uint64 count;
        Array<uint64, QPOOL_MAX_IPO_BIDS> prices;
        Array<uint64, QPOOL_MAX_IPO_BIDS> seatsAt;
    };
    struct GetIpoBids_locals { Holding h; IpoBids bids; };

    // ---- helpers of the immediate QX sale (KIND_SELL_QX) ----

    // What QX's visible bid book would pay RIGHT NOW for `shares` of `asset` sold at no less than
    // `limitPrice` each: the shares it would take (sold), the QU QPOOL would receive after QX's
    // trade fee (net), and how many of those shares go to QPOOL's OWN resting bids (ownTaken: they
    // move from QPOOL to QPOOL, so they do not leave its books). A read of QX, no state change.
    // Only the first AssetBidOrders window of bids is read. If the sale would still have shares left
    // after that window (windowFull), the caller offers only `sold`, so that the replay is exact
    // for everything the ask can reach.
    struct QxBidBook_input { Asset asset; uint64 limitPrice; uint64 shares; };
    struct QxBidBook_output { uint32 returnCode; uint64 sold; uint64 net; uint64 ownTaken; uint64 windowFull; };
    struct QxBidBook_locals
    {
        QX::Fees_input fIn; QX::Fees_output fOut;
        QX::AssetBidOrders_input bIn; QX::AssetBidOrders_output bOut;
        QX::AssetBidOrders_output::Order o;
        uint64 i; uint64 remaining; uint64 take; sint64 gross; sint64 fee; sint64 tradeFee;
    };

    // How many shares of `asset` QPOOL still has resting on an ask at `price` on QX (a read of QX).
    struct QxAskRest_input { Asset asset; uint64 price; };
    struct QxAskRest_output { uint32 returnCode; uint64 rest; };
    struct QxAskRest_locals
    {
        QX::EntityAskOrders_input aIn; QX::EntityAskOrders_output aOut;
        QX::EntityAskOrders_output::Order o;
        uint64 i;
    };

    // Credit the QU a sale brought in to the ONE holding that sold, after the management fee.
    struct CreditProceeds_input { uint64 holdingId; Holding h; uint64 received; };
    struct CreditProceeds_output { Holding h; uint64 net; };
    struct CreditProceeds_locals { uint128 scaled; uint64 fee; };

    // Hand globalUnattributed back to its value before a sale's external calls (see UnparkArrivals).
    struct UnparkArrivals_input { uint64 before; };
    struct UnparkArrivals_output { uint32 unused; };

    // ---- helpers of the BUY refund accounting (BUY_QX and BUY_QSWAP) ----

    // How much of the QU Execute handed to QX or QSWAP came back during that call. Both contracts hand
    // back what they did not use (QX: the excess over price times shares and every price improvement;
    // QSWAP: the whole amount when it refuses the swap) with qpi.transfer, so it reaches the hook as
    // an ordinary transfer from their id and is filed under globalUnattributed. Two independent
    // measures, and the refund is the SMALLER of them (and never more than was handed over):
    //   - the balance: balAfter + assigned - balBefore (all QU that reached the contract meanwhile);
    //   - the hook: how far globalUnattributed rose since gaBefore (what the hook parked meanwhile).
    // They agree whenever only QX or QSWAP paid us during the call. If they ever differ, the smaller
    // one is claimed and the rest stays where it is, so the books can only err towards leaving QU
    // unowned, never towards crediting members with QU the contract did not receive.
    struct BuyRefund_input { uint64 assigned; uint64 balBefore; uint64 gaBefore; };
    struct BuyRefund_output { uint64 refund; };
    struct BuyRefund_locals { Entity ent; uint64 balAfter; uint64 viaBalance; uint64 viaHook; };

    // Take a refund back out of globalUnattributed, where the hook filed it when it arrived.
    struct TakeBackRefund_input { uint64 amount; };
    struct TakeBackRefund_output { uint32 unused; };

    // The escrow of the ledger a holding earns from (0 when it has none), read without syncing.
    struct HoldingEscrow_input { Asset asset; uint64 ipoContractIndex; uint64 payerIndex; };
    struct HoldingEscrow_output { uint64 escrow; };
    struct HoldingEscrow_locals { SourceKeyOf_input kin; SourceKeyOf_output kout; SourceLedger led; };

    // ---- roster membership (private) ----
    // A member is on a pool's roster exactly when he has a Contribution record in that pool: ONE fact
    // with ONE writer. JoinCheck is its read-only question (call it BEFORE any QU or asset moves),
    // EnsureMember its only writer (call it once the call can no longer be refused).
    // isNew: not a member yet. full: he is new and there is no room for him (roster or record table).
    struct JoinCheck_input { uint64 poolId; id member; };
    struct JoinCheck_output { uint64 isNew; uint64 full; };
    struct JoinCheck_locals { ContribKey key; };

    // fee: what the new member paid to join (QPOOL_JOIN_FEE, 0 when he is a member already); booked as revenue
    // only if he really joins. pool: the pool after the call, with its new roster size; the caller continues
    // from THIS copy (it was persisted here), never from an older one.
    struct EnsureMember_input { uint64 poolId; id member; uint64 fee; };
    struct EnsureMember_output { uint32 returnCode; uint64 isNew; Pool pool; };
    struct EnsureMember_locals { ContribKey key; Contribution c; Pool pool; };

    // ---- the end of a vote (private) ----
    // Deletes the vote rows of ONE proposal and lifts the locks they hold, in one bounded pass over the pool's roster. It is
    // the only place that deletes vote rows and the only place that lifts a vote's lock, so every row is released exactly once
    // (the row is deleted when it is released, and a deleted row cannot be released again). Called when a vote is over for
    // good: Tally (a rejected, a late or a sell/allocate proposal: every row; a passed BUY: the NO rows only, the YES rows are
    // the mandate's payers and stay until Execute), Execute (every row left: the YES voters, once charged, once the mandate
    // failed or has expired) and Propose (a slot being reused whose proposal was left untallied or unexecuted after its life).
    //   which: 0 = every row of the proposal, 1 = the NO rows only.
    // The lock of a row is lifted only if the row is part of the member's lock sum (see Contribution::lockStartTick: the lock
    // started no later than the end of the proposal's vote window) and the lock still binds (a lapsed lock reads as 0 whatever
    // is stored). A late call, for a proposal nobody ended in time, therefore never takes QU out of the lock of a LATER vote.
    struct ReleaseVotes_input { uint64 slot; uint64 poolId; uint64 kind; uint64 openTick; uint64 which; };
    struct ReleaseVotes_output { uint64 released; };
    struct ReleaseVotes_locals
    {
        Pool pool; uint64 base; uint64 j; uint64 voteEnd;
        VoteKey vk; VoteChoice v; ContribKey ck; Contribution c;
        VoteWindowOf_input wIn; VoteWindowOf_output wOut;
    };

    struct CreatePool_input { uint64 poolName; uint64 permissioned; };
    struct CreatePool_output { uint32 returnCode; uint64 poolId; };
    struct CreatePool_locals { Pool pool; uint64 i; };

    struct Contribute_input { uint64 poolId; };
    struct Contribute_output { uint32 returnCode; uint64 uncommitted; };
    struct Contribute_locals
    {
        Pool pool; ContribKey key; Contribution c; uint64 amount; uint64 joinFee;
        JoinCheck_input jcIn; JoinCheck_output jcOut;
        EnsureMember_input emIn; EnsureMember_output emOut;
    };

    struct WithdrawUncommitted_input { uint64 poolId; uint64 amount; };
    struct WithdrawUncommitted_output { uint32 returnCode; uint64 withdrawn; };
    struct WithdrawUncommitted_locals { Pool pool; ContribKey key; Contribution c; uint64 available; uint64 locked; uint64 totalBefore; };

    struct SetAdmission_input { uint64 poolId; id member; uint64 allowed; };
    struct SetAdmission_output { uint32 returnCode; };
    struct SetAdmission_locals { Pool pool; ContribKey key; };

    struct SetPermissioned_input { uint64 poolId; uint64 permissioned; };
    struct SetPermissioned_output { uint32 returnCode; };
    struct SetPermissioned_locals { Pool pool; };

    // Close a pool that was never used (procedure id 20), so its slot can be created again. Creator only.
    struct CloseEmptyPool_input { uint64 poolId; };
    struct CloseEmptyPool_output { uint32 returnCode; };
    struct CloseEmptyPool_locals { Pool pool; Pool blank; };

    // BUY_IPO: asset = {NULL_ID, the target's share name}, holdingId = the target contract's index, quAmount = the BUDGET (the QU the pool
    // allocates: the ONLY limit of the bidding, there is no voted quantity and no ceiling price), price = the OPENING price (the price of the
    // first bid, a starting point shown to the voters: later bids may be at any price the money pays for, 1 .. 1,479,289,940,828), and the
    // budget must buy at least one seat at it (quAmount >= price). `shares` is IGNORED for a BUY_IPO (any value is accepted; the field stays
    // in the input so that clients keep their layout): Propose stores the opening quantity it derives, min(676, budget / opening price), in the
    // proposal's `shares`, which GetProposal shows to the voters. Every other kind reads `shares` as before.
    struct Propose_input
    {
        uint64 poolId; uint64 kind; Asset asset;
        uint64 quAmount; uint64 shares; uint64 price; uint64 holdingId;
    };
    struct Propose_output { uint32 returnCode; uint64 proposalId; };
    struct Propose_locals
    {
        Pool pool; ContribKey key; Contribution c; Proposal p; Holding h;
        uint64 i; uint64 weight; uint64 slotStatus; uint64 life;
        uint64 contractShares; IpoPriceKey ipoKey;
        CheckSource_input srcIn; CheckSource_output srcOut;
        ReleaseVotes_input rvIn; ReleaseVotes_output rvOut;
        IpoRowOf_input rowIn; IpoRowOf_output rowOut;
    };

    struct Vote_input { uint64 proposalId; uint64 approve; };
    struct Vote_output { uint32 returnCode; uint64 weight; };
    struct Vote_locals
    {
        Proposal p; VoteKey vk; ContribKey ck; Contribution c;
        OwnKey ok; Ownership own; uint64 weight; uint64 end; Holding h;
        VoteWindowOf_input wIn; VoteWindowOf_output wOut;
    };

    struct Tally_input { uint64 proposalId; };
    struct Tally_output { uint32 returnCode; uint64 status; };
    struct Tally_locals
    {
        Proposal p; uint64 quorum; uint64 cast;
        ReleaseVotes_input rvIn; ReleaseVotes_output rvOut;
        VoteWindowOf_input wIn; VoteWindowOf_output wOut;
    };

    struct Execute_input { uint64 proposalId; };
    // sold: shares that left the holding (SELL kinds). refunded: QU that QX or QSWAP handed back to a BUY
    // (BUY_QX / BUY_QSWAP), which stayed with the buyers. Both appended after the original fields.
    struct Execute_output { uint32 returnCode; uint64 holdingId; uint64 acquired; uint64 proceeds; uint64 sold; uint64 refunded; };
    struct Execute_locals
    {
        Proposal p; Pool pool; Holding h; ContribKey ck; Contribution c;
        OwnKey ok; Ownership own; uint128 scaled;
        VoteKey vk; VoteChoice voteChoice;
        uint64 i; uint64 base; uint64 slot; uint64 share; uint64 assigned; uint64 remaining;
        uint64 newHoldingId; uint64 serial; uint64 hbase; uint64 filled; uint64 sold; uint64 payerIdx;
        Entity ent; uint64 balBefore; uint64 balAfter; uint64 received; uint64 fee;
        sint64 rel; sint64 heldBefore; sint64 heldAfter; sint64 ipoResult; sint64 alreadyManaged;
        sint64 managedBySelf;
        CheckSource_input srcIn; CheckSource_output srcOut;
        RegisterHolding_input regIn; RegisterHolding_output regOut;
        ApplyHolding_input apIn; ApplyHolding_output apOut;
        // Per-member buy-in computed in a read-only first pass, written only after
        // the external acquisition has succeeded (see the BUY branch of Execute).
        Array<uint64, QPOOL_MAX_CONTRIBUTORS_PER_POOL> memberShare;
        // The same members' costs once QX / QSWAP have handed part of the QU back (only filled when
        // something came back; see the BUY branch).
        Array<uint64, QPOOL_MAX_CONTRIBUTORS_PER_POOL> memberCost;
        BuyRefund_input refIn; BuyRefund_output refOut;
        TakeBackRefund_input tbIn; TakeBackRefund_output tbOut;
        HoldingEscrow_input escIn; HoldingEscrow_output escOut;
        uint64 refund; uint64 spent; uint64 prefix; uint64 cum; uint64 prevCum;
        QSWAP::SwapExactQuForAsset_input swapBuyIn; QSWAP::SwapExactQuForAsset_output swapBuyOut;
        QSWAP::SwapExactAssetForQu_input swapSellIn; QSWAP::SwapExactAssetForQu_output swapSellOut;
        QX::AddToBidOrder_input qxBidIn; QX::AddToBidOrder_output qxBidOut;
        // Named by which contract is CALLED (asked to hand shares over),
        // not by the destination -- e.g. xferToQswapIn is passed to QX's
        // own TransferShareManagementRights (QX currently holds the shares
        // and is asked to move them to QSWAP), so its type is QX's input.
        QX::TransferShareManagementRights_input xferToQswapIn;
        QX::TransferShareManagementRights_output xferToQswapOut;
        // KIND_SELL_QX. Same naming rule: the contract that is CALLED.
        QSWAP::TransferShareManagementRights_input xferToQxIn;
        QSWAP::TransferShareManagementRights_output xferToQxOut;
        QX::AddToAskOrder_input qxAskIn; QX::AddToAskOrder_output qxAskOut;
        QX::RemoveFromAskOrder_input qxRemIn; QX::RemoveFromAskOrder_output qxRemOut;
        QxBidBook_input bookIn; QxBidBook_output bookOut;
        QxAskRest_input restIn; QxAskRest_output restOut;
        CreditProceeds_input crIn; CreditProceeds_output crOut;
        UnparkArrivals_input unpIn; UnparkArrivals_output unpOut;
        sint64 managedQx; sint64 heldQxBefore; sint64 heldQxAfter;
        uint64 fromSelf; uint64 fromQswap; uint64 costs; uint64 rest; uint64 gaBefore; uint64 parked;
        uint64 placed; uint64 cancelFailed; uint64 askShares; uint64 soldByHeld; uint32 code;
        IpoPriceKey ipoKey; uint64 overspent;
        // BUY_IPO: the target's row in StateData::ipoTargets (see IpoRowOf) and the holding's bid list.
        IpoRowOf_input rowIn; IpoRowOf_output rowOut; IpoTarget row; uint64 rowIdx; IpoBids bids;
        // BUY sizing (see the BUY branch): the QU the YES voters can pay, the part of the mandate that is funded, the
        // order size that money buys, and the swap's minimum out scaled to it.
        uint64 yesSum; uint64 quantity; uint64 minOut; uint64 yesCount; uint64 created;
        ReleaseVotes_input rvIn; ReleaseVotes_output rvOut;
    };

    struct ClaimPayout_input { uint64 holdingId; };
    struct ClaimPayout_output { uint32 returnCode; uint64 paid; };
    struct ClaimPayout_locals
    {
        Holding h; Holding blank; Pool pool; OwnKey ok; Ownership own; OfferKey fk; uint128 pending; uint64 owed;
        uint64 retired; uint64 offerDropped;
        SyncPreview_input pvIn; SyncPreview_output pvOut;
        SyncHolding_input syIn; SyncHolding_output syOut;
    };

    struct OfferPosition_input { uint64 holdingId; uint64 costUnits; uint64 askPrice; };
    struct OfferPosition_output { uint32 returnCode; };
    struct OfferPosition_locals { OwnKey ok; Ownership own; OfferKey fk; Offer f; Holding h; Pool pool; uint64 isNew; };

    struct CancelOffer_input { uint64 holdingId; };
    struct CancelOffer_output { uint32 returnCode; };
    struct CancelOffer_locals { OfferKey fk; Offer f; Pool pool; };

    // expectedCostUnits: the cost units of the offer the buyer read (GetPosition(holdingId, seller).offeredCostUnits). The seller can replace
    // his offer at any moment (OfferPosition overwrites it), and the attached QU only caps the PRICE; so the buyer states the PARCEL too, and
    // the call is refused (OFFER_CHANGED, everything refunded, nothing written) unless it is exactly the live offer's (audit C033). 0 is never
    // an offer's size, so a call that leaves it out -- a client of an earlier layout, whose 40-byte input the node pads with zeros -- is refused.
    struct BuyPosition_input { uint64 holdingId; id seller; uint64 expectedCostUnits; };
    struct BuyPosition_output { uint32 returnCode; uint64 costUnitsBought; };
    struct BuyPosition_locals
    {
        Holding h; Pool pool; OfferKey fk; Offer f;
        OwnKey sellerKey; Ownership sellerOwn; OwnKey buyerKey; Ownership buyerOwn;
        uint128 pending; uint64 buyerExisted; uint64 sellerDropped;
        ContribKey admitKey; uint64 fee; uint64 joinFee;
        SyncHolding_input syIn; SyncHolding_output syOut;
        HoldingEscrow_input escIn; HoldingEscrow_output escOut;
        JoinCheck_input jcIn; JoinCheck_output jcOut;
        EnsureMember_input emIn; EnsureMember_output emOut;
    };

    struct ReconcileHolding_input { uint64 holdingId; };
    struct ReconcileHolding_output { uint32 returnCode; uint64 shares; uint64 pendingShares; };
    struct ReconcileHolding_locals
    {
        Holding h; sint64 held;
        SourceKeyOf_input kin; SourceKeyOf_output kout; SourceLedger led;
        ApplyHolding_input apIn; ApplyHolding_output apOut;
        uint64 unclaimed; uint64 newClaim;
    };

    // RebidIpo (procedure id 13; deleted in K2 and reinstated by the owner's final IPO decision D1): a further bid for an unsettled IPO
    // holding, by a seat owner, bounded by MONEY and nothing else. price: the price per share (1 .. 1,479,289,940,828, above the minimum of a
    // full book; a price of ANOTHER holding of this IPO is refused, a price this holding already has is reused: it adds seats there).
    // quantity: the slots to bid; 0 = as many as the free funds buy at this price (at most 676). The bid is allowed when price x quantity is at
    // most the holding's free funds at the START of the call (after the call's own reconcile pass has turned the refunds of EARLIER evictions into
    // free funds) and at most the contract's physical balance; otherwise it is refused with nothing written. The refunds of the seats this very
    // bid pushes out are NOT counted: they arrive during the bid and fund the next one (see "THE MONEY RULE" at the top).
    //   bidQuantity     the slots bid for (the defaulted quantity when `quantity` was 0)
    //   bidsRegistered  slots the core seated (the unseated ones were refunded at once)
    //   seatsBefore     slots QPOOL held at the holding's prices when the book was read, before this bid
    //   freed           QU moved from at-risk to free funds by the reconcile (outbid earlier and returned), clamped by the refund pot
    //   atRisk/available/livePrices  the holding after the call (its own seats this bid pushed out still count as held until the next reconcile)
    struct RebidIpo_input { uint64 holdingId; uint64 price; uint64 quantity; };
    struct RebidIpo_output
    {
        uint32 returnCode; uint64 bidsRegistered; uint64 seatsBefore; uint64 freed; uint64 atRisk; uint64 available; uint64 livePrices;
        uint64 bidQuantity;
    };
    struct RebidIpo_locals
    {
        Holding h; OwnKey ok; Ownership own; IpoBids bids; IpoBids nb; IpoPriceKey key; IpoTarget row; uint64 rowIdx;
        IpoRowOf_input rowIn; IpoRowOf_output rowOut;
        IpoSeatsNow_input snIn; IpoSeatsNow_output snOut;
        ApplyHolding_input apIn; ApplyHolding_output apOut;
        Entity ent; uint64 balBefore; uint64 balAfter; uint64 overspent;
        sint64 minPrice; sint64 ipoResult;
        uint64 maxFunds; uint64 shouldBe; uint64 freed; uint64 drawn; uint64 avail; uint64 quantity; uint64 cost;
        uint64 k; uint64 n; uint64 atRisk; uint64 sureValue; uint64 owner; uint64 reuse; uint64 listed; uint64 floorPrice; uint64 added;
    };

    struct DepositHoldingAsset_input
    {
        uint64 poolId; Asset asset; uint64 amount; uint64 seatUnits;
        // Contract shares (asset.issuer == NULL_ID) only: the index of the contract that
        // pays dividends on them. Required for such an asset, and must be 0 for any other.
        uint64 contractIndex;
    };
    struct DepositHoldingAsset_output { uint32 returnCode; uint64 holdingId; };
    struct DepositHoldingAsset_locals
    {
        Pool pool; Holding h; OwnKey ok; Ownership own;
        uint64 newHoldingId; uint64 serial;
        sint64 managed; sint64 xfer;
        CheckSource_input srcIn; CheckSource_output srcOut;
        RegisterHolding_input regIn; RegisterHolding_output regOut;
        JoinCheck_input jcIn; JoinCheck_output jcOut;
        EnsureMember_input emIn; EnsureMember_output emOut;
    };

    // Take back shares that YOU own and that this contract manages (procedure id 19): the way out for shares whose deposit was refused
    // (audit C032). Moving shares to QPOOL's management is the first step of a deposit (QX TransferShareManagementRights), and QPOOL
    // accepts the move before it knows whether the deposit will be taken; if it is refused (SOURCE_CONFLICT, CONTRACT_MISMATCH, a full
    // ledger table ...), the shares stay yours but QX can no longer trade them, and nothing else could move them. destinationIndex is the
    // contract that is to manage them (QX is 1). Attach what the destination asks for moving them (QX asks its transfer fee, 100 QU, and
    // refunds nothing of it): whatever is not taken goes back to you. requiredFee is what the destination asked for when the attachment
    // was too small. The call only ever moves shares owned by the CALLER, never one of the pools' holdings (they are owned by this contract).
    struct ReleaseManagedShares_input { Asset asset; uint64 amount; uint64 destinationIndex; };
    struct ReleaseManagedShares_output { uint32 returnCode; uint64 released; uint64 feePaid; uint64 requiredFee; };
    struct ReleaseManagedShares_locals { sint64 managed; sint64 rel; };

    // Evict a squat from the dividend ledger of an asset (procedure id 21): see the procedure for the whole predicate.
    //   holdingId   the deposited holding that holds the binding
    //   rivalName   TOKEN ledgers: the name of another asset of the same issuer that the caller owns and needs the binding for
    //               (ignored for a CONTRACT ledger)
    struct EvictSquat_input { uint64 holdingId; uint64 rivalName; };
    struct EvictSquat_output { uint32 returnCode; uint64 sharesReturned; };
    struct EvictSquat_locals
    {
        Holding h; Holding blank; Pool pool; OwnKey ok; Ownership own; OfferKey fk; uint64 offerDropped;
        SourceKeyOf_input kin; SourceKeyOf_output kout; SourceLedger led;
        ApplyHolding_input apIn; ApplyHolding_output apOut;
        Asset rival; sint64 xfer;
    };


    // Procedure id 15 was ReconcileIpo. It is DELETED (owner decision D1): the END_EPOCH snapshot reads the book exactly, once, at
    // the last moment it can be read, and SettleIpo works from that. A mid-auction refresh of a holding's books is part of RebidIpo (id 13:
    // its pass over the book turns the money that was outbid and has come back into free funds, which is the only thing that
    // can fund the next rebid), so a separate procedure for it would give the members nothing that a rebid does not, would leave the seat
    // counts stale between calls and would hand every caller a pass over the book at the fee reserve's expense. The id stays reserved:
    // nothing is registered under it and no later procedure may take it.

    struct SettleIpo_input { uint64 holdingId; };
    struct SettleIpo_output { uint32 returnCode; uint64 shares; uint64 returnedToMembers; };
    struct SettleIpo_locals
    {
        Holding h; Pool pool; ContribKey ck; Contribution c; OwnKey ok; Ownership own;
        uint64 i; uint64 base; uint64 refundEach; uint64 returned; uint64 leftover;
        uint64 fromIpo; uint64 draw; uint64 potUse; uint64 prefix; uint64 cum; uint64 prevCum;
        sint64 held;
        uint128 pending; OfferKey fk;
        SourceKeyOf_input kin; SourceKeyOf_output kout; SourceLedger led;
        ApplyHolding_input apIn; ApplyHolding_output apOut;
        ContractPairKnown_input pairIn; ContractPairKnown_output pairOut;
        uint64 myShare; uint64 proven; IpoPriceKey ipoKey; IpoSeat seat;
        IpoRowOf_input rowIn; IpoRowOf_output rowOut; IpoTarget row; uint64 rowIdx;
        IpoBids bids; uint64 j; uint64 seatsWon; uint64 spentOnSeats; uint64 valueOfSeats;
        uint64 dropped; uint64 offersDropped; uint64 freed;      // seat rows and offers removed by this call, and whether the holding's slot is free again
    };

    struct GetPool_input { uint64 poolId; };
    struct GetPool_output
    {
        uint32 found; id creator; uint64 poolName; uint64 uncommittedTotal;
        uint64 committedTotal; uint64 contributorCount; uint64 holdingCount;
        uint64 unattributedQu; uint64 permissioned;
        // what the pool uses of the shared tables, and the quotas those are measured against (QPOOL_MAX_*_PER_POOL)
        uint64 offerCount; uint64 admittedCount; uint64 ownershipRows; uint64 depositCount; uint64 ipoHoldings;
    };
    struct GetPool_locals { Pool pool; };

    struct GetMember_input { uint64 poolId; id member; };
    // locked: the part of `uncommitted` that backs the member's votes right now and cannot be withdrawn (0 once its expiry has passed).
    struct GetMember_output { uint32 found; uint64 uncommitted; uint64 committed; uint64 locked; };
    struct GetMember_locals { ContribKey key; Contribution c; };

    struct GetHolding_input { uint64 holdingId; };
    struct GetHolding_output
    {
        uint32 found; uint64 poolId; id issuer; uint64 assetName;
        uint64 shares; uint64 costBasis; uint64 totalPayouts;
        uint64 pendingShares; uint64 ipoContractIndex; uint64 active;
        uint64 ipoMaxPrice; uint64 ipoAtRisk; uint64 ipoAvailable;
        uint64 deposited;
        uint64 ipoSettled; uint64 sourceEscrow;
        uint64 payerIndex;
        uint64 rows;      // seat rows that exist for the holding
    };
    struct GetHolding_locals
    {
        Holding h; SourceKeyOf_input kin; SourceKeyOf_output kout; SourceLedger led;
        SyncMath_input mIn; SyncMath_output mOut;
    };

    struct GetPosition_input { uint64 holdingId; id member; };
    struct GetPosition_output
    {
        uint32 found; uint64 cost; uint64 costBasis; uint64 owed;
        uint64 offeredCostUnits; uint64 askPrice;
        uint64 voteLockUntilTick;   // the seat cannot be offered or sold while qpi.tick() <= this (0 or a past tick: free)
    };
    struct GetPosition_locals
    {
        Holding h; OwnKey ok; Ownership own; OfferKey fk; Offer f; uint128 pending;
        SourceKeyOf_input kin; SourceKeyOf_output kout; SyncMath_input mIn; SyncMath_output mOut;
    };

    struct GetProposal_input { uint64 proposalId; };
    struct GetProposal_output
    {
        uint32 found; uint64 poolId; uint64 kind; id proposer;
        id issuer; uint64 assetName; uint64 quAmount; uint64 shares; uint64 price;
        uint64 holdingId; uint64 yesWeight; uint64 noWeight; uint64 eligibleWeight;
        uint64 openTick; uint64 status;
    };
    struct GetProposal_locals { Proposal p; };

    struct GetStats_input {};
    struct GetStats_output
    {
        uint64 poolCount; uint64 holdingCount; uint64 proposalCount;
        uint64 totalContributed; uint64 totalDeployed; uint64 totalPayoutsDistributed;
        uint64 globalUnattributed;
        uint64 ipoRefundEscrow; uint64 totalIpoRefunded;
        uint64 pendingRevenue; uint64 totalRevenue; uint64 totalManagementFees;
        uint64 totalDistributedToShareholders; uint64 totalBurned;
        uint64 dividendEscrowTotal; uint64 totalUnownedSwept;
    };

    // ======================== Functions ========================

    // ======================== Dividend source helpers ========================
    //
    // WHY A LEDGER PER SOURCE AND NOT A HOLDING POINTER.
    // POST_INCOMING_TRANSFER sees only {sourceId, amount, type}. Several holdings
    // (in several pools) can own shares of the SAME asset, so the payer cannot
    // name a holding. The contract therefore keeps ONE ledger per payer and every
    // holding of that payer's asset earns from it in proportion to the shares it
    // has RECOGNISED. The ledger holds no holding ids, so a retired holding or a
    // reused holding slot can never be a stale target.
    //
    // The hook is O(1) in the number of holdings: it advances a per-share
    // accumulator. Each holding pulls its slice lazily (SyncHolding) before
    // anything that reads or changes its weight.
    //
    // MIXED CONTRACT LEDGERS. One CONTRACT ledger can hold IPO-won, QX-bought and
    // deposited shares of the same contract. Its escrow has ONE owner at a time:
    //   - while any IPO holding is unsettled the whole net is parked, to be released per
    //     recognised share by the settlement that clears the last of them. So bought and
    //     deposited shares may only JOIN once every IPO holding has settled (CheckSource);
    //     joining earlier would hand them a slice of dividends that arrived before them;
    //   - after that, escrow only ever exists for the fills of ONE unreconciled QX bid
    //     (pendingQx > 0), and is paid to that bid's holding alone (ReconcileHolding).
    // An IPO holding therefore cannot join a ledger with a bid in flight or escrow parked
    // for one, ReconcileHolding refuses a ledger with unsettled IPO holdings, and the IPO
    // release in ApplyHolding fires only on the settlement that clears the last of them.
    //
    // LAZY-SYNC RULE (the one invariant a future edit can break): every code path
    // that changes Holding.shares / pendingShares / costBasis, or reads
    // payoutPerCostX64 for a payout, must CALL SyncHolding (or ApplyHolding, which
    // syncs first) BEFORE it, and must continue with the Holding those helpers
    // return (they persist it). Holding.shares may be written only by
    // RegisterHolding and ApplyHolding.
    //
    // NEVER call qpi.numberOfShares / qpi.numberOfPossessedShares from
    // POST_INCOMING_TRANSFER (core runs the hook inside qpi.distributeDividends
    // while it HOLDS universeLock, a non-reentrant spinlock that those calls also
    // take) and NEVER from a PUBLIC_FUNCTION of this contract (a function holds the
    // contract's state read lock while it waits for universeLock, and the hook that
    // is running under universeLock waits for the state write lock). The hook reads
    // the universe only through AssetPossessionIterator, which takes no lock.

    // Which ledger a holding earns from. valid == 0 when the holding names no source:
    // a token asset needs a non-NULL issuer, contract shares need the paying contract's
    // index (ipoContractIndex for an IPO holding, payerIndex otherwise), and the two
    // never mix. contractIndex is the paying contract for a CONTRACT key.
    PRIVATE_FUNCTION(SourceKeyOf)
    {
        output.valid = 0;
        output.contractIndex = 0;
        if (input.ipoContractIndex != 0 || input.payerIndex != 0)
        {
            if (input.asset.issuer != NULL_ID) return;
            output.contractIndex = (input.ipoContractIndex != 0) ? input.ipoContractIndex : input.payerIndex;
            output.key.source = id(output.contractIndex, 0, 0, 0);
            output.key.kind = QPOOL_SOURCE_CONTRACT;
        }
        else
        {
            if (input.asset.issuer == NULL_ID) return;
            output.key.source = input.asset.issuer;
            output.key.kind = QPOOL_SOURCE_TOKEN;
        }
        output.valid = 1;
    }

    // Mainnet contract table (explorer.qubic.org, smart contracts, 2026-10-17): contract index ->
    // asset name, packed as uint64 little-endian ASCII. The shares of every one of these have issuer
    // NULL_ID and the contract's address is id(index,0,0,0). It is only a CHECK on user input: a
    // contract added later is simply not listed, and nothing here needs to change for it.
    PRIVATE_FUNCTION(KnownContractName)
    {
        output.name = 0;
        if (input.index == 1) { output.name = 22609ULL; return; } // QX
        if (input.index == 2) { output.name = 1498567761ULL; return; } // QTRY
        if (input.index == 3) { output.name = 85002843734354ULL; return; } // RANDOM
        if (input.index == 4) { output.name = 327647778129ULL; return; } // QUTIL
        if (input.index == 5) { output.name = 5065805ULL; return; } // MLM
        if (input.index == 6) { output.name = 22605213090009415ULL; return; } // GQMPROP
        if (input.index == 7) { output.name = 79454013577043ULL; return; } // SWATCH
        if (input.index == 8) { output.name = 4604739ULL; return; } // CCF
        if (input.index == 9) { output.name = 336387458385ULL; return; } // QEARN
        if (input.index == 10) { output.name = 92686824592977ULL; return; } // QVAULT
        if (input.index == 11) { output.name = 23727827095802701ULL; return; } // MSVAULT
        if (input.index == 12) { output.name = 1497449041ULL; return; } // QBAY
        if (input.index == 13) { output.name = 344693625681ULL; return; } // QSWAP
        if (input.index == 14) { output.name = 1414745934ULL; return; } // NOST
        if (input.index == 15) { output.name = 374758065233ULL; return; } // QDRAW
        if (input.index == 16) { output.name = 19538ULL; return; } // RL
        if (input.index == 17) { output.name = 293371593297ULL; return; } // QBOND
        if (input.index == 18) { output.name = 5261649ULL; return; } // QIP
        if (input.index == 19) { output.name = 19505638103142993ULL; return; } // QRAFFLE
        if (input.index == 20) { output.name = 1096241745ULL; return; } // QRWA
        if (input.index == 21) { output.name = 5263953ULL; return; } // QRP
        if (input.index == 22) { output.name = 4609105ULL; return; } // QTF
        if (input.index == 23) { output.name = 327580730449ULL; return; } // QDUEL
        if (input.index == 24) { output.name = 297750254928ULL; return; } // PULSE
        if (input.index == 25) { output.name = 86128393998166ULL; return; } // VOTTUN
        if (input.index == 26) { output.name = 87197656241489ULL; return; } // QUSINO
        if (input.index == 27) { output.name = 95998194176837ULL; return; } // ESCROW
        if (input.index == 28) { output.name = 1347897159ULL; return; } // GGWP
        if (input.index == 29) { output.name = 18671117686362193ULL; return; } // QPAYHUB
        if (input.index == 30) { output.name = 92639312630865ULL; return; } // QTREAT
    }

    PRIVATE_FUNCTION(KnownContractIndex)
    {
        output.index = 0;
        if (input.name == 22609ULL) { output.index = 1; return; } // QX
        if (input.name == 1498567761ULL) { output.index = 2; return; } // QTRY
        if (input.name == 85002843734354ULL) { output.index = 3; return; } // RANDOM
        if (input.name == 327647778129ULL) { output.index = 4; return; } // QUTIL
        if (input.name == 5065805ULL) { output.index = 5; return; } // MLM
        if (input.name == 22605213090009415ULL) { output.index = 6; return; } // GQMPROP
        if (input.name == 79454013577043ULL) { output.index = 7; return; } // SWATCH
        if (input.name == 4604739ULL) { output.index = 8; return; } // CCF
        if (input.name == 336387458385ULL) { output.index = 9; return; } // QEARN
        if (input.name == 92686824592977ULL) { output.index = 10; return; } // QVAULT
        if (input.name == 23727827095802701ULL) { output.index = 11; return; } // MSVAULT
        if (input.name == 1497449041ULL) { output.index = 12; return; } // QBAY
        if (input.name == 344693625681ULL) { output.index = 13; return; } // QSWAP
        if (input.name == 1414745934ULL) { output.index = 14; return; } // NOST
        if (input.name == 374758065233ULL) { output.index = 15; return; } // QDRAW
        if (input.name == 19538ULL) { output.index = 16; return; } // RL
        if (input.name == 293371593297ULL) { output.index = 17; return; } // QBOND
        if (input.name == 5261649ULL) { output.index = 18; return; } // QIP
        if (input.name == 19505638103142993ULL) { output.index = 19; return; } // QRAFFLE
        if (input.name == 1096241745ULL) { output.index = 20; return; } // QRWA
        if (input.name == 5263953ULL) { output.index = 21; return; } // QRP
        if (input.name == 4609105ULL) { output.index = 22; return; } // QTF
        if (input.name == 327580730449ULL) { output.index = 23; return; } // QDUEL
        if (input.name == 297750254928ULL) { output.index = 24; return; } // PULSE
        if (input.name == 86128393998166ULL) { output.index = 25; return; } // VOTTUN
        if (input.name == 87197656241489ULL) { output.index = 26; return; } // QUSINO
        if (input.name == 95998194176837ULL) { output.index = 27; return; } // ESCROW
        if (input.name == 1347897159ULL) { output.index = 28; return; } // GGWP
        if (input.name == 18671117686362193ULL) { output.index = 29; return; } // QPAYHUB
        if (input.name == 92639312630865ULL) { output.index = 30; return; } // QTREAT
    }

    // Static pairing check for contract shares: is `assetName` plausibly the share asset of
    // contract `index`? Read-only; no call into the core. What it can and cannot prove:
    //   - index 1..QPOOL_KNOWN_CONTRACTS: the pair must match the embedded table EXACTLY;
    //   - any index: a name the table lists for ANOTHER index is refused;
    //   - index above the table: a name already bound (contractNames) to another index is
    //     refused. For a name nobody has bound yet nothing on chain can say which contract
    //     it belongs to; the first registration binds it (see RegisterHolding / ApplyHolding).
    // The index itself must be a possible contract index and not QPOOL's own.
    PRIVATE_FUNCTION_WITH_LOCALS(ContractPairKnown)
    {
        output.returnCode = QPOOL_OK;
        if (input.index == 0 || input.index == (uint64)SELF_INDEX || input.index > QPOOL_MAX_CONTRACT_INDEX
            || input.assetName == 0 || input.assetName > QPOOL_MAX_ASSET_NAME)
        {
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.nIn.index = input.index;
        CALL(KnownContractName, locals.nIn, locals.nOut);
        if (input.index <= QPOOL_KNOWN_CONTRACTS)
        {
            if (locals.nOut.name != input.assetName) output.returnCode = QPOOL_ERR_CONTRACT_MISMATCH;
            return;
        }
        locals.iIn.name = input.assetName;
        CALL(KnownContractIndex, locals.iIn, locals.iOut);
        if (locals.iOut.index != 0)
        {
            output.returnCode = QPOOL_ERR_CONTRACT_MISMATCH; return;      // a listed contract's name
        }
        if (state.get().contractNames.get(input.assetName, locals.mapped) && locals.mapped != input.index)
        {
            output.returnCode = QPOOL_ERR_CONTRACT_MISMATCH; return;      // already bound to another index
        }
    }

    //   ipo == 0 (shares that already exist: a QX purchase or a deposit): above the table, the
    //     contract index must be a registered contract that is NOT in its IPO epoch
    //     (qpi.ipoBidPrice: -1 no such contract, -2 registered and not in its IPO epoch, >= 0 in its
    //     IPO). NOTE: -2 also covers a contract whose IPO has not started yet, so this does NOT rule
    //     out squatting the index of a registered but not yet live contract (shown by a probe).
    //     contract whose shares cannot exist yet.
    //   ipo != 0 (a bid in the contract's IPO): the declared name must NOT already be an
    //     issued asset. The IPO's own shares do not exist before the IPO ends, so an issued
    //     name is some other contract's, and an honest declaration is never refused.
    // Procedures only (the core read takes the target contract's state lock while it is in
    // its IPO epoch); never from a PUBLIC_FUNCTION.
    PRIVATE_FUNCTION_WITH_LOCALS(CheckContractPair)
    {
        output.returnCode = QPOOL_OK;
        locals.kIn.index = input.index;
        locals.kIn.assetName = input.assetName;
        CALL(ContractPairKnown, locals.kIn, locals.kOut);
        if (locals.kOut.returnCode != QPOOL_OK) { output.returnCode = locals.kOut.returnCode; return; }
        if (input.index <= QPOOL_KNOWN_CONTRACTS) return;
        if (input.ipo != 0)
        {
            if (qpi.isAssetIssued(NULL_ID, input.assetName)) output.returnCode = QPOOL_ERR_CONTRACT_MISMATCH;
            return;
        }
        locals.phase = qpi.ipoBidPrice((uint32)input.index, 0);
        if (locals.phase != -2) output.returnCode = QPOOL_ERR_CONTRACT_NOT_LIVE;
    }

    // Pre-flight for registering a new holding: call BEFORE any capital or asset
    // moves, so a refusal is free. Read-only. Never call from a PUBLIC_FUNCTION.
    PRIVATE_FUNCTION_WITH_LOCALS(CheckSource)
    {
        output.returnCode = QPOOL_OK;
        locals.kin.asset = input.asset;
        locals.kin.ipoContractIndex = input.ipoContractIndex;
        locals.kin.payerIndex = input.payerIndex;
        CALL(SourceKeyOf, locals.kin, locals.kout);
        if (locals.kout.valid == 0) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        // SELF, QX and QSWAP pay us as a side effect of our own calls (refunds, sale
        // proceeds) as ordinary transfers, so a token payer with one of those ids
        // would have them mistaken for dividends and booked twice.
        if (locals.kout.key.kind == QPOOL_SOURCE_TOKEN
            && (locals.kout.key.source == SELF
                || locals.kout.key.source == id(QPOOL_QX_CONTRACT_INDEX, 0, 0, 0)
                || locals.kout.key.source == id(QPOOL_QSWAP_CONTRACT_INDEX, 0, 0, 0)))
        {
            output.returnCode = QPOOL_ERR_SOURCE_CONFLICT; return;
        }
        if (locals.kout.key.kind == QPOOL_SOURCE_CONTRACT)
        {
            // Contract shares: does this name belong to this contract? (see CheckContractPair)
            locals.pIn.index = locals.kout.contractIndex;
            locals.pIn.assetName = input.asset.assetName;
            locals.pIn.ipo = (input.ipoContractIndex != 0) ? 1 : 0;
            CALL(CheckContractPair, locals.pIn, locals.pOut);
            if (locals.pOut.returnCode != QPOOL_OK) { output.returnCode = locals.pOut.returnCode; return; }
        }
        if (state.get().sources.get(locals.kout.key, locals.led))
        {
            // One payer, one asset: a transfer does not say which asset it is for.
            // A CONTRACT source stays unbound (assetName 0) until its first
            // settlement proves the name, so only a bound name can conflict.
            if (locals.kout.key.kind == QPOOL_SOURCE_TOKEN)
            {
                if (locals.led.assetName != input.asset.assetName)
                {
                    output.returnCode = QPOOL_ERR_SOURCE_CONFLICT; return;
                }
            }
            else if (locals.led.assetName != 0 && locals.led.assetName != input.asset.assetName)
            {
                output.returnCode = QPOOL_ERR_SOURCE_CONFLICT; return;
            }
            if (locals.kout.key.kind == QPOOL_SOURCE_CONTRACT)
            {
                // A CONTRACT ledger mixes IPO holdings with bought and deposited ones, but
                // never while its escrow could belong to either:
                //  - while an IPO holding is unsettled every dividend is parked and later
                //    released per recognised share, so a share registered NOW would be paid
                //    a slice of dividends that arrived before it was bought or deposited
                //    (and could be shares the depositor already collected a dividend on);
                //    a ledger still unbound has nothing to bind a new name against either;
                //  - an IPO holding cannot join a ledger whose escrow is set aside for an
                //    unreconciled QX fill (it would be released to the wrong holders).
                if (input.ipoContractIndex == 0)
                {
                    if (locals.led.unsettled != 0 || locals.led.assetName == 0)
                    {
                        output.returnCode = QPOOL_ERR_SETTLEMENT_PENDING; return;
                    }
                }
                else if (locals.led.pendingQx != 0 || (locals.led.unsettled == 0 && locals.led.escrow != 0))
                {
                    output.returnCode = QPOOL_ERR_SETTLEMENT_PENDING; return;
                }
            }
            // Only one QX bid per payer may be unreconciled at a time, so that fills
            // (which arrive silently) can only belong to that one holding.
            if (input.qxBid != 0 && locals.led.pendingQx != 0)
            {
                output.returnCode = QPOOL_ERR_QX_BID_PENDING; return;
            }
            return;
        }
        if (state.get().sources.population() >= QPOOL_SOURCE_SOFT_CAP)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        // A new bound CONTRACT ledger above the table also needs a contractNames entry. The
        // table cannot actually fill (one entry per contract index), but a registration must
        // never half-succeed, so refuse before anything moves.
        if (locals.kout.key.kind == QPOOL_SOURCE_CONTRACT && input.ipoContractIndex == 0
            && locals.kout.contractIndex > QPOOL_KNOWN_CONTRACTS
            && state.get().contractNames.population() >= QPOOL_MAX_CONTRACT_NAMES)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
    }

    // Pure arithmetic shared by SyncHolding (writes) and GetPosition (reads).
    // credit = weight * (accX64 - snapshot), clamped to what the ledger really took in.
    PRIVATE_FUNCTION_WITH_LOCALS(SyncMath)
    {
        output.ppc = input.h.payoutPerCostX64;
        output.snap = input.h.srcSnapX64;
        output.credited = uint128(0, 0);
        if (input.led.accX64 > input.h.srcSnapX64)
        {
            if (input.h.shares > 0 && input.h.costBasis > 0)
            {
                locals.credit = (uint128)input.h.shares * (input.led.accX64 - input.h.srcSnapX64);
                if (input.led.accruedX64 > input.led.creditedX64)
                {
                    locals.room = input.led.accruedX64 - input.led.creditedX64;
                }
                else
                {
                    locals.room = uint128(0, 0);
                }
                if (locals.credit > locals.room) locals.credit = locals.room;
                output.ppc = input.h.payoutPerCostX64 + div(locals.credit, (uint128)input.h.costBasis);
                output.credited = locals.credit;
            }
            output.snap = input.led.accX64;
        }
    }

    // What SyncHolding would do, without doing it: see SyncPreview_input.
    PRIVATE_FUNCTION_WITH_LOCALS(SyncPreview)
    {
        output.h = input.h;
        output.escrow = 0;
        output.changed = 0;
        output.credited = uint128(0, 0);
        locals.kin.asset = input.h.asset;
        locals.kin.ipoContractIndex = input.h.ipoContractIndex;
        locals.kin.payerIndex = input.h.payerIndex;
        CALL(SourceKeyOf, locals.kin, locals.kout);
        if (locals.kout.valid == 0) return;
        if (!state.get().sources.get(locals.kout.key, output.led)) return;
        output.key = locals.kout.key;
        output.escrow = output.led.escrow;
        locals.mIn.h = input.h;
        locals.mIn.led = output.led;
        CALL(SyncMath, locals.mIn, locals.mOut);
        output.h.payoutPerCostX64 = locals.mOut.ppc;
        output.h.srcSnapX64 = locals.mOut.snap;
        output.h.totalPayouts = sadd(input.h.totalPayouts, locals.mOut.credited.high);
        output.credited = locals.mOut.credited;
        // The only thing a sync can change is the holding's snapshot of the ledger's accumulator (a credit, if there is one, goes with it): when the
        // ledger has not moved on since, the holding is what it was.
        output.changed = (locals.mOut.snap == input.h.srcSnapX64) ? 0 : 1;
    }

    // Move the dividends a holding has earned since its last sync into its own
    // accumulator and PERSIST both the holding and the ledger together, so a credit
    // can never be recorded on one side only. Callers adopt output.h.
    // It writes ONLY when the sync changes something (the ledger has accrued since the holding last looked): a call that finds nothing new
    // leaves the state clean, so that a refusal that came after it did not cost the node a whole-state re-hash for nothing (audit C022, D23).
    PRIVATE_PROCEDURE_WITH_LOCALS(SyncHolding)
    {
        locals.pIn.h = input.h;
        CALL(SyncPreview, locals.pIn, locals.pOut);
        output.h = locals.pOut.h;
        output.escrow = locals.pOut.escrow;
        if (locals.pOut.changed == 0) return;
        if (locals.pOut.credited > uint128(0, 0))
        {
            locals.pOut.led.creditedX64 = locals.pOut.led.creditedX64 + locals.pOut.credited;
            state.mut().sources.set(locals.pOut.key, locals.pOut.led);
        }
        state.mut().holdings.set(input.holdingId, output.h);
    }

    // Enrol a brand-new holding in its source's ledger. Call after the acquisition
    // succeeded and BEFORE the holding is written; CheckSource must have passed.
    PRIVATE_PROCEDURE_WITH_LOCALS(RegisterHolding)
    {
        output.h = input.h;
        output.returnCode = QPOOL_OK;
        locals.kin.asset = input.h.asset;
        locals.kin.ipoContractIndex = input.h.ipoContractIndex;
        locals.kin.payerIndex = input.h.payerIndex;
        CALL(SourceKeyOf, locals.kin, locals.kout);
        if (locals.kout.valid == 0) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.isNew = 0;
        if (!state.get().sources.get(locals.kout.key, locals.led))
        {
            // An IPO source is bound by its first settlement that wins shares; a TOKEN
            // source, or a CONTRACT source created by bought or deposited shares (whose
            // shares exist already), is bound now.
            locals.isNew = 1;
            locals.led.assetName = (input.h.ipoContractIndex != 0) ? 0 : input.h.asset.assetName;
            locals.led.bindEpoch = qpi.epoch();         // how long this binding has stood (EvictSquat)
        }
        else if (locals.kout.key.kind == QPOOL_SOURCE_TOKEN)
        {
            if (locals.led.assetName != input.h.asset.assetName)
            {
                output.returnCode = QPOOL_ERR_SOURCE_CONFLICT; return;
            }
        }
        else if (locals.led.assetName != 0 && locals.led.assetName != input.h.asset.assetName)
        {
            output.returnCode = QPOOL_ERR_SOURCE_CONFLICT; return;
        }
        else if (input.h.ipoContractIndex == 0 && (locals.led.unsettled != 0 || locals.led.assetName == 0))
        {
            // Bought or deposited shares may only join a CONTRACT ledger whose IPO holdings
            // have all settled (CheckSource refuses the same case up front).
            output.returnCode = QPOOL_ERR_SETTLEMENT_PENDING; return;
        }
        locals.led.holdings = locals.led.holdings + 1;
        if (input.h.ipoContractIndex != 0)
        {
            if (input.h.ipoSettled == 0) locals.led.unsettled = locals.led.unsettled + 1;
        }
        else
        {
            locals.led.pendingQx = sadd(locals.led.pendingQx, input.h.pendingShares);
        }
        locals.led.recognized = sadd(locals.led.recognized, input.h.shares);
        output.h.srcSnapX64 = locals.led.accX64;
        if (state.mut().sources.set(locals.kout.key, locals.led) == NULL_INDEX)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        // A new bound CONTRACT ledger above the embedded table claims its asset name for its
        // index (one-to-one, see ContractPairKnown). An IPO ledger claims it at settlement.
        if (locals.isNew != 0 && input.h.ipoContractIndex == 0
            && locals.kout.key.kind == QPOOL_SOURCE_CONTRACT && locals.kout.contractIndex > QPOOL_KNOWN_CONTRACTS)
        {
            if (state.mut().contractNames.set(input.h.asset.assetName, locals.kout.contractIndex) == NULL_INDEX)
            {
                output.returnCode = QPOOL_ERR_CAPACITY; return;
            }
        }
    }

    // THE choke point for changing what a holding earns on. Every change to
    // shares / pendingShares of a registered holding, every IPO settlement and every
    // retirement goes through here:
    //   1. crystallise earnings on the OLD weight (SyncHolding),
    //   2. adjust the ledger's recognised / pending totals by the delta,
    //   3. apply the settle / retire flags (an IPO source is bound by its first
    //      settlement that wins shares),
    //   4. pay out escrow: a QX-bid holding takes its source's escrow directly;
    //      an IPO source releases it per recognised share once nothing is unsettled,
    //   5. drop the ledger when its last holding leaves (unowned escrow -> revenue),
    //   6. persist the holding.
    // The holding's snapshot is taken BEFORE any release, so the holding whose
    // settlement triggers a release takes part in it.
    PRIVATE_PROCEDURE_WITH_LOCALS(ApplyHolding)
    {
        output.returnCode = QPOOL_OK;
        // `retire` is authoritative: a holding that leaves its ledger keeps no shares in it (audit C038). Nothing that retires a holding
        // passes shares; this makes sure that no caller can leave recognised shares in a ledger for a holding that is gone.
        locals.newShares = (input.retire != 0) ? 0 : input.newShares;
        locals.sIn.holdingId = input.holdingId;
        locals.sIn.h = input.h;
        CALL(SyncHolding, locals.sIn, locals.sOut);
        output.h = locals.sOut.h;

        locals.kin.asset = input.h.asset;
        locals.kin.ipoContractIndex = input.h.ipoContractIndex;
        locals.kin.payerIndex = input.h.payerIndex;
        CALL(SourceKeyOf, locals.kin, locals.kout);
        if (locals.kout.valid == 0 || !state.get().sources.get(locals.kout.key, locals.led))
        {
            // Not registered with any ledger (only a hand-built holding in a test).
            output.h.shares = locals.newShares;
            output.h.pendingShares = input.newPending;
            if (input.markSettled != 0) output.h.ipoSettled = 1;
            if (input.retire != 0) output.h.active = 0;
            state.mut().holdings.set(input.holdingId, output.h);
            output.returnCode = QPOOL_ERR_NOT_FOUND; return;
        }

        locals.led.recognized = (locals.led.recognized > output.h.shares)
            ? locals.led.recognized - output.h.shares : 0;
        locals.led.recognized = sadd(locals.led.recognized, locals.newShares);
        if (output.h.ipoContractIndex == 0)
        {
            locals.led.pendingQx = (locals.led.pendingQx > output.h.pendingShares)
                ? locals.led.pendingQx - output.h.pendingShares : 0;
            locals.led.pendingQx = sadd(locals.led.pendingQx, input.newPending);
        }
        output.h.shares = locals.newShares;
        output.h.pendingShares = input.newPending;
        locals.bound = 0;
        if (input.markSettled != 0)
        {
            output.h.ipoSettled = 1;
            if (locals.led.unsettled > 0) locals.led.unsettled = locals.led.unsettled - 1;
            if (locals.newShares > 0 && locals.led.assetName == 0)
            {
                locals.led.assetName = output.h.asset.assetName;
                locals.bound = 1;
            }
        }
        if (input.retire != 0)
        {
            output.h.active = 0;
            if (locals.led.holdings > 0) locals.led.holdings = locals.led.holdings - 1;
        }
        output.h.srcSnapX64 = locals.led.accX64;

        // Escrow of a CONTRACT ledger belongs to the IPO holdings while any is unsettled and
        // to an unreconciled QX fill after that (CheckSource keeps the two from coexisting),
        // so a QX-bid holding may only take it once every IPO holding has settled.
        if (input.claimEscrow != 0 && locals.led.escrow > 0 && output.h.costBasis > 0
            && (locals.kout.key.kind == QPOOL_SOURCE_TOKEN || locals.led.unsettled == 0))
        {
            locals.scaled.high = locals.led.escrow;
            locals.scaled.low = 0;
            output.h.payoutPerCostX64 = output.h.payoutPerCostX64
                + div(locals.scaled, (uint128)output.h.costBasis);
            output.h.totalPayouts = sadd(output.h.totalPayouts, locals.led.escrow);
            state.mut().dividendEscrowTotal = (state.get().dividendEscrowTotal > locals.led.escrow)
                ? state.get().dividendEscrowTotal - locals.led.escrow : 0;
            locals.led.escrow = 0;
        }
        // IPO escrow: released per recognised share by the settlement that clears the last
        // unsettled IPO holding (and only by it: a later call that finds escrow in a settled
        // CONTRACT ledger is looking at a QX fill's escrow, which is not ours to release).
        if (locals.kout.key.kind == QPOOL_SOURCE_CONTRACT && input.markSettled != 0
            && locals.led.unsettled == 0 && locals.led.recognized > 0 && locals.led.escrow > 0)
        {
            locals.scaled.high = locals.led.escrow;
            locals.scaled.low = 0;
            locals.led.accX64 = locals.led.accX64 + div(locals.scaled, (uint128)locals.led.recognized);
            locals.led.accruedX64 = locals.led.accruedX64 + locals.scaled;
            state.mut().dividendEscrowTotal = (state.get().dividendEscrowTotal > locals.led.escrow)
                ? state.get().dividendEscrowTotal - locals.led.escrow : 0;
            locals.led.escrow = 0;
        }

        if (locals.led.holdings == 0)
        {
            // Nobody left to pay. Whatever is still parked belongs to no holding;
            // it is already in this contract's balance, so book it as revenue
            // instead of leaving it in a dead ledger.
            if (locals.led.escrow > 0)
            {
                state.mut().pendingRevenue = sadd(state.get().pendingRevenue, locals.led.escrow);
                state.mut().totalRevenue = sadd(state.get().totalRevenue, locals.led.escrow);
                state.mut().totalUnownedSwept = sadd(state.get().totalUnownedSwept, locals.led.escrow);
                state.mut().dividendEscrowTotal = (state.get().dividendEscrowTotal > locals.led.escrow)
                    ? state.get().dividendEscrowTotal - locals.led.escrow : 0;
                state.mut().totalPayoutsDistributed = (state.get().totalPayoutsDistributed > locals.led.escrow)
                    ? state.get().totalPayoutsDistributed - locals.led.escrow : 0;
            }
            // What the ledger took in for its recognised shares and no holding ever credited (rounding, and the slice of shares that no
            // holding carried) is nobody's either: book it as revenue too, so that it does not stay in the contract's balance unaccounted
            // for (audit C038).
            if (locals.led.accruedX64 > locals.led.creditedX64)
            {
                locals.rem = locals.led.accruedX64 - locals.led.creditedX64;
                locals.remQu = locals.rem.high;
                if (locals.remQu > 0)
                {
                    state.mut().pendingRevenue = sadd(state.get().pendingRevenue, locals.remQu);
                    state.mut().totalRevenue = sadd(state.get().totalRevenue, locals.remQu);
                    state.mut().totalUnownedSwept = sadd(state.get().totalUnownedSwept, locals.remQu);
                    state.mut().totalPayoutsDistributed = (state.get().totalPayoutsDistributed > locals.remQu)
                        ? state.get().totalPayoutsDistributed - locals.remQu : 0;
                }
            }
            state.mut().sources.removeByKey(locals.kout.key);
            // The ledger is gone, so is its claim on its asset name.
            if (locals.led.assetName != 0 && locals.kout.contractIndex > QPOOL_KNOWN_CONTRACTS
                && locals.kout.key.kind == QPOOL_SOURCE_CONTRACT)
            {
                state.mut().contractNames.removeByKey(locals.led.assetName);
            }
        }
        else
        {
            state.mut().sources.set(locals.kout.key, locals.led);
            // An IPO ledger claims its asset name for its index at the first settlement that
            // wins shares (SettleIpo has already checked it is free, see ContractPairKnown).
            if (locals.bound != 0 && locals.kout.key.kind == QPOOL_SOURCE_CONTRACT
                && locals.kout.contractIndex > QPOOL_KNOWN_CONTRACTS)
            {
                state.mut().contractNames.set(locals.led.assetName, locals.kout.contractIndex);
            }
        }
        state.mut().holdings.set(input.holdingId, output.h);
    }

    // ======================== Immediate QX sale helpers ========================

    // Replays QX's matching of an ask against its visible bid book WITHOUT placing anything.
    // QX fills an ask at the resting bids' own prices, best first, for every bid priced at the
    // ask's limit or better, and charges the seller its trade fee per fill:
    //   fee = floor(price * shares * tradeFee / 1e9) + 1       (the form QX uses below ~3e12 QU)
    //   fee = floor(price * shares / (1e9 / tradeFee)) + 1      (QX's overflow-safe form above that)
    // The replay uses QX's own formulas, so within one transaction (nothing can touch the book
    // between this read and the ask) it is exact for the bids it covers. It reads only the first
    // AssetBidOrders window (windowFull tells the caller the sale reaches past it, and the caller then
    // offers no more than the window can take). Read-only: never writes state.
    PRIVATE_FUNCTION_WITH_LOCALS(QxBidBook)
    {
        output.returnCode = QPOOL_OK;
        output.sold = 0;
        output.net = 0;
        output.ownTaken = 0;
        output.windowFull = 0;
        CALL_OTHER_CONTRACT_FUNCTION_E(QX, Fees, locals.fIn, locals.fOut, feeErr);
        if (feeErr != NoCallError) { output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return; }
        locals.tradeFee = (sint64)locals.fOut.tradeFee;
        locals.bIn.issuer = input.asset.issuer;
        locals.bIn.assetName = input.asset.assetName;
        locals.bIn.offset = 0;
        CALL_OTHER_CONTRACT_FUNCTION_E(QX, AssetBidOrders, locals.bIn, locals.bOut, bookErr);
        if (bookErr != NoCallError) { output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return; }
        locals.remaining = input.shares;
        for (locals.i = 0; locals.i < locals.bOut.orders.capacity() && locals.remaining > 0; locals.i++)
        {
            locals.o = locals.bOut.orders.get(locals.i);
            // The list ends at the first empty entry; bids are listed best price first.
            if (locals.o.numberOfShares <= 0 || locals.o.price <= 0
                || (uint64)locals.o.price < input.limitPrice) break;
            locals.take = ((uint64)locals.o.numberOfShares < locals.remaining)
                ? (uint64)locals.o.numberOfShares : locals.remaining;
            locals.gross = locals.o.price * (sint64)locals.take;
            if (smul(locals.o.price, (sint64)locals.take) >= div<sint64>(INT64_MAX, locals.tradeFee))
            {
                locals.fee = div<sint64>(locals.gross, div<sint64>(1000000000LL, locals.tradeFee)) + 1;
            }
            else
            {
                locals.fee = div<sint64>(locals.gross * locals.tradeFee, 1000000000LL) + 1;
            }
            if (locals.gross > locals.fee) output.net = sadd(output.net, (uint64)(locals.gross - locals.fee));
            output.sold = sadd(output.sold, locals.take);
            if (locals.o.entity == SELF) output.ownTaken = sadd(output.ownTaken, locals.take);
            locals.remaining = locals.remaining - locals.take;
        }
        if (locals.remaining > 0 && locals.i >= locals.bOut.orders.capacity()) output.windowFull = 1;
    }

    // What QX reports as still resting on QPOOL's asks at this asset and price. This is the
    // authority for how much of an ask did NOT fill: counting shares that left QPOOL would be
    // wrong when the ask fills against QPOOL's own resting bid (the shares then move from QPOOL
    // to QPOOL and the count does not change). Read-only.
    PRIVATE_FUNCTION_WITH_LOCALS(QxAskRest)
    {
        output.returnCode = QPOOL_OK;
        output.rest = 0;
        locals.aIn.entity = SELF;
        locals.aIn.offset = 0;
        CALL_OTHER_CONTRACT_FUNCTION_E(QX, EntityAskOrders, locals.aIn, locals.aOut, askErr);
        if (askErr != NoCallError) { output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return; }
        for (locals.i = 0; locals.i < locals.aOut.orders.capacity(); locals.i++)
        {
            locals.o = locals.aOut.orders.get(locals.i);
            if (locals.o.numberOfShares <= 0) break;
            if (locals.o.issuer == input.asset.issuer && locals.o.assetName == input.asset.assetName
                && locals.o.price == (sint64)input.price)
            {
                output.rest = sadd(output.rest, (uint64)locals.o.numberOfShares);
            }
        }
    }

    // Credit what a sale brought in to the ONE holding that sold it. The management fee comes off
    // first, then the rest rides the same accumulator as dividends; costBasis is untouched, so
    // every member's fraction is preserved. Call AFTER ApplyHolding (which syncs the holding and
    // takes its shares down) and continue with output.h.
    PRIVATE_PROCEDURE_WITH_LOCALS(CreditProceeds)
    {
        output.h = input.h;
        output.net = 0;
        if (input.received == 0) return;
        locals.fee = div((uint128)input.received * (uint128)QPOOL_MANAGEMENT_PERMILLE, (uint128)1000ULL).low;
        output.net = input.received - locals.fee;
        if (locals.fee > 0)
        {
            state.mut().pendingRevenue = sadd(state.get().pendingRevenue, locals.fee);
            state.mut().totalRevenue = sadd(state.get().totalRevenue, locals.fee);
            state.mut().totalManagementFees = sadd(state.get().totalManagementFees, locals.fee);
        }
        locals.scaled.high = output.net;
        locals.scaled.low = 0;
        output.h.payoutPerCostX64 = output.h.payoutPerCostX64 + div(locals.scaled, (uint128)output.h.costBasis);
        output.h.totalPayouts = sadd(output.h.totalPayouts, output.net);
        state.mut().holdings.set(input.holdingId, output.h);
        state.mut().totalPayoutsDistributed = sadd(state.get().totalPayoutsDistributed, output.net);
    }

    // WHY SALES TOUCH globalUnattributed AT ALL. Everything QX or QSWAP pays QPOOL in the middle of
    // a call (sale proceeds, a refunded fee) reaches POST_INCOMING_TRANSFER as an ordinary transfer
    // from that contract's id (type qpiTransfer). That id is deliberately not a registered dividend
    // payer (CheckSource), so the hook files the amount under globalUnattributed, the counter of
    // money that belongs to nobody. A sale then credits the same money to its holding from the
    // balance difference. Without this step the counter would count it twice and show members'
    // money as stranded. A sale reads the counter before its first external call and gives it
    // back here once every call has returned; whatever the hook parked meanwhile is exactly what
    // the sale has accounted for (credited, charged as a fee, or returned to QPOOL).
    // Only writes when something was parked, so a refused or empty sale does not dirty the state.
    PRIVATE_PROCEDURE(UnparkArrivals)
    {
        if (state.get().globalUnattributed > input.before)
        {
            state.mut().globalUnattributed = input.before;
        }
    }

    // WHY BUYS TAKE THEIR REFUND BACK. A BUY hands QX or QSWAP the QU the members' buy-ins add up
    // to (`assigned`) and they hand back whatever they did not use. That refund is real money of the
    // buyers that never left their uncommitted balances, but the hook cannot tell it from any other
    // arrival and files it under globalUnattributed (a counter of money that belongs to nobody) while
    // the holding's cost would still count it as spent. Execute therefore reads what came back
    // (BuyRefund), charges the members only what was really spent and takes the refund out of the
    // counter again (TakeBackRefund). Read-only.
    PRIVATE_FUNCTION_WITH_LOCALS(BuyRefund)
    {
        output.refund = 0;
        qpi.getEntity(SELF, locals.ent);
        locals.balAfter = locals.ent.incomingAmount - locals.ent.outgoingAmount;
        locals.viaBalance = (sadd(locals.balAfter, input.assigned) > input.balBefore)
            ? sadd(locals.balAfter, input.assigned) - input.balBefore : 0;
        locals.viaHook = (state.get().globalUnattributed > input.gaBefore)
            ? state.get().globalUnattributed - input.gaBefore : 0;
        output.refund = input.assigned;
        if (locals.viaBalance < output.refund) output.refund = locals.viaBalance;
        if (locals.viaHook < output.refund) output.refund = locals.viaHook;
    }

    // Writes only when there is something to take back, so a buy that was refunded nothing does not
    // dirty the state.
    PRIVATE_PROCEDURE(TakeBackRefund)
    {
        if (input.amount > 0)
        {
            state.mut().globalUnattributed = (state.get().globalUnattributed > input.amount)
                ? state.get().globalUnattributed - input.amount : 0;
        }
    }

    // The escrow parked on a holding's source, or 0. Read-only: sales use it to refuse BEFORE anything is
    // written (escrowed dividends belong to whoever held the shares when they arrived).
    PRIVATE_FUNCTION_WITH_LOCALS(HoldingEscrow)
    {
        output.escrow = 0;
        locals.kin.asset = input.asset;
        locals.kin.ipoContractIndex = input.ipoContractIndex;
        locals.kin.payerIndex = input.payerIndex;
        CALL(SourceKeyOf, locals.kin, locals.kout);
        if (locals.kout.valid != 0 && state.get().sources.get(locals.kout.key, locals.led))
        {
            output.escrow = locals.led.escrow;
        }
    }

    PRIVATE_FUNCTION_WITH_LOCALS(IpoRowOf)
    {
        output.found = 0;
        output.row = 0;
        output.hasFree = 0;
        output.free = 0;
        for (locals.i = 0; locals.i < QPOOL_MAX_IPO_TARGETS; locals.i++)
        {
            locals.r = state.get().ipoTargets.get(locals.i);
            if (locals.r.ipoContractIndex == input.index && input.index != 0)
            {
                output.found = 1;
                output.row = locals.i;
                return;
            }
            if (locals.r.ipoContractIndex == 0 && output.hasFree == 0)
            {
                output.hasFree = 1;
                output.free = locals.i;
            }
        }
    }

    PRIVATE_FUNCTION(VoteWindowOf)
    {
        output.window = (input.kind == QPOOL_KIND_BUY_IPO) ? QPOOL_IPO_VOTE_WINDOW : QPOOL_VOTE_WINDOW;
        output.lifetime = output.window + QPOOL_EXECUTE_WINDOW;
    }

    // The reconcile pass. Reads both numbers of every slot of the book only for slots whose bidder is QPOOL (the id is read first). The book is not
    // assumed to be sorted: every slot is looked at, so a slot can only be missed if the core fails to report it. A slot of QPOOL at a price
    // that is not in the holding's list belongs to another holding (every price is registered to one holding) and is not counted here.
    PRIVATE_FUNCTION_WITH_LOCALS(IpoSeatsNow)
    {
        output.seats = 0;
        output.value = 0;
        output.live = 0;
        for (locals.k = 0; locals.k < QPOOL_MAX_IPO_BIDS; locals.k++) output.seatsNow.set(locals.k, 0);
        locals.n = (input.bids.count < QPOOL_MAX_IPO_BIDS) ? input.bids.count : QPOOL_MAX_IPO_BIDS;
        for (locals.i = 0; locals.i < (uint64)NUMBER_OF_COMPUTORS; locals.i++)
        {
            if (qpi.ipoBidId((uint32)input.ipoContractIndex, (uint32)locals.i) != SELF) continue;
            locals.price = qpi.ipoBidPrice((uint32)input.ipoContractIndex, (uint32)locals.i);
            if (locals.price <= 0) continue;
            for (locals.k = 0; locals.k < locals.n; locals.k++)
            {
                if (input.bids.prices.get(locals.k) != (uint64)locals.price) continue;
                output.seatsNow.set(locals.k, output.seatsNow.get(locals.k) + 1);
                break;
            }
        }
        for (locals.k = 0; locals.k < locals.n; locals.k++)
        {
            output.seats = output.seats + output.seatsNow.get(locals.k);
            output.value = sadd(output.value, smul(output.seatsNow.get(locals.k), input.bids.prices.get(locals.k)));
            if (output.seatsNow.get(locals.k) != 0) output.live = output.live + 1;
        }
    }

    PUBLIC_FUNCTION_WITH_LOCALS(GetPool)
    {
        if (input.poolId >= QPOOL_MAX_POOLS) return;
        locals.pool = state.get().pools.get(input.poolId);
        if (locals.pool.active == 0) return;
        output.found = 1;
        output.creator = locals.pool.creator;
        output.poolName = locals.pool.poolName;
        output.uncommittedTotal = locals.pool.uncommittedTotal;
        output.committedTotal = locals.pool.committedTotal;
        output.contributorCount = locals.pool.contributorCount;
        output.holdingCount = locals.pool.holdingCount;
        output.unattributedQu = locals.pool.unattributedQu;
        output.permissioned = locals.pool.permissioned;
        output.offerCount = locals.pool.offerCount;
        output.admittedCount = locals.pool.admittedCount;
        output.ownershipRows = locals.pool.ownershipRows;
        output.depositCount = locals.pool.depositCount;
        output.ipoHoldings = locals.pool.ipoHoldings;
    }

    PUBLIC_FUNCTION_WITH_LOCALS(GetMember)
    {
        locals.key.member = input.member;
        locals.key.poolId = input.poolId;
        if (!state.get().contributions.get(locals.key, locals.c)) return;
        output.found = 1;
        output.uncommitted = locals.c.uncommitted;
        output.committed = locals.c.committed;
        output.locked = ((uint64)qpi.tick() <= locals.c.lockExpiryTick) ? locals.c.lockedForVotes : 0;
    }

    PUBLIC_FUNCTION_WITH_LOCALS(GetHolding)
    {
        if (input.holdingId >= QPOOL_MAX_HOLDINGS) return;
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.costBasis == 0 && locals.h.active == 0) return;
        output.found = 1;
        output.poolId = locals.h.poolId;
        output.issuer = locals.h.asset.issuer;
        output.assetName = locals.h.asset.assetName;
        output.shares = locals.h.shares;
        output.costBasis = locals.h.costBasis;
        output.totalPayouts = locals.h.totalPayouts;
        output.pendingShares = locals.h.pendingShares;
        output.ipoContractIndex = locals.h.ipoContractIndex;
        output.active = locals.h.active;
        output.ipoMaxPrice = locals.h.ipoMaxPrice;
        output.ipoAtRisk = locals.h.ipoAtRisk;
        output.ipoAvailable = locals.h.ipoAvailable;
        output.deposited = locals.h.deposited;
        output.ipoSettled = locals.h.ipoSettled;
        output.payerIndex = locals.h.payerIndex;
        output.rows = locals.h.rows;
        locals.kin.asset = locals.h.asset;
        locals.kin.ipoContractIndex = locals.h.ipoContractIndex;
        locals.kin.payerIndex = locals.h.payerIndex;
        CALL(SourceKeyOf, locals.kin, locals.kout);
        if (locals.kout.valid != 0 && state.get().sources.get(locals.kout.key, locals.led))
        {
            output.sourceEscrow = locals.led.escrow;
            // Show the payouts the holding would credit itself right now, so the view
            // agrees with GetPosition / ClaimPayout between syncs.
            locals.mIn.h = locals.h;
            locals.mIn.led = locals.led;
            CALL(SyncMath, locals.mIn, locals.mOut);
            output.totalPayouts = sadd(locals.h.totalPayouts, locals.mOut.credited.high);
        }
    }

    PUBLIC_FUNCTION_WITH_LOCALS(GetPosition)
    {
        if (input.holdingId >= QPOOL_MAX_HOLDINGS) return;
        locals.h = state.get().holdings.get(input.holdingId);
        output.costBasis = locals.h.costBasis;
        locals.ok.member = input.member;
        locals.ok.holdingId = input.holdingId;
        if (!state.get().ownership.get(locals.ok, locals.own)) return;
        output.found = 1;
        output.cost = locals.own.cost;
        output.voteLockUntilTick = locals.own.voteLockUntilTick;
        // Show what ClaimPayout would pay right now, including dividends the
        // holding has not pulled from its source ledger yet.
        locals.kin.asset = locals.h.asset;
        locals.kin.ipoContractIndex = locals.h.ipoContractIndex;
        locals.kin.payerIndex = locals.h.payerIndex;
        CALL(SourceKeyOf, locals.kin, locals.kout);
        if (locals.kout.valid != 0 && state.get().sources.get(locals.kout.key, locals.mIn.led))
        {
            locals.mIn.h = locals.h;
            CALL(SyncMath, locals.mIn, locals.mOut);
            locals.h.payoutPerCostX64 = locals.mOut.ppc;
        }
        locals.pending = (uint128)locals.own.cost
            * (locals.h.payoutPerCostX64 - locals.own.payoutDebtX64);
        output.owed = sadd(locals.own.claimable, locals.pending.high);
        locals.fk.seller = input.member;
        locals.fk.holdingId = input.holdingId;
        if (state.get().offers.get(locals.fk, locals.f))
        {
            output.offeredCostUnits = locals.f.costUnits;
            output.askPrice = locals.f.askPrice;
        }
    }

    PUBLIC_FUNCTION_WITH_LOCALS(GetProposal)
    {
        if (input.proposalId >= QPOOL_MAX_PROPOSALS) return;
        locals.p = state.get().proposals.get(input.proposalId);
        if (locals.p.kind == 0) return;
        output.found = 1;
        output.poolId = locals.p.poolId;
        output.kind = locals.p.kind;
        output.proposer = locals.p.proposer;
        output.issuer = locals.p.asset.issuer;
        output.assetName = locals.p.asset.assetName;
        output.quAmount = locals.p.quAmount;
        output.shares = locals.p.shares;
        output.price = locals.p.price;
        output.holdingId = locals.p.holdingId;
        output.yesWeight = locals.p.yesWeight;
        output.noWeight = locals.p.noWeight;
        output.eligibleWeight = locals.p.eligibleWeight;
        output.openTick = locals.p.openTick;
        output.status = locals.p.status;
    }

    PUBLIC_FUNCTION(GetStats)
    {
        output.poolCount = state.get().poolCount;
        output.holdingCount = state.get().holdingCount;
        output.proposalCount = state.get().proposalCount;
        output.totalContributed = state.get().totalContributed;
        output.totalDeployed = state.get().totalDeployed;
        output.totalPayoutsDistributed = state.get().totalPayoutsDistributed;
        output.globalUnattributed = state.get().globalUnattributed;
        output.ipoRefundEscrow = state.get().ipoRefundEscrow;
        output.totalIpoRefunded = state.get().totalIpoRefunded;
        output.pendingRevenue = state.get().pendingRevenue;
        output.totalRevenue = state.get().totalRevenue;
        output.totalManagementFees = state.get().totalManagementFees;
        output.totalDistributedToShareholders = state.get().totalDistributedToShareholders;
        output.totalBurned = state.get().totalBurned;
        output.dividendEscrowTotal = state.get().dividendEscrowTotal;
        output.totalUnownedSwept = state.get().totalUnownedSwept;
    }

    PUBLIC_FUNCTION_WITH_LOCALS(GetSource)
    {
        locals.key.source = input.source;
        locals.key.kind = input.kind;
        if (!state.get().sources.get(locals.key, locals.led)) return;
        output.found = 1;
        output.assetName = locals.led.assetName;
        output.holdings = locals.led.holdings;
        output.recognized = locals.led.recognized;
        output.unsettled = locals.led.unsettled;
        output.escrow = locals.led.escrow;
        output.pendingQx = locals.led.pendingQx;
        output.accHigh = locals.led.accX64.high;
        output.accLow = locals.led.accX64.low;
    }

    PUBLIC_FUNCTION_WITH_LOCALS(GetIpoTarget)
    {
        locals.rIn.index = input.ipoContractIndex;
        CALL(IpoRowOf, locals.rIn, locals.rOut);
        if (locals.rOut.found == 0) return;
        locals.row = state.get().ipoTargets.get(locals.rOut.row);
        output.found = 1;
        output.unsettled = locals.row.unsettled;
        output.bidEpoch = locals.row.bidEpoch;
        output.snapshotEpoch = locals.row.snapshotEpoch;
        output.finalPrice = locals.row.finalPrice;
        if (input.price != 0)
        {
            locals.key.ipoContractIndex = input.ipoContractIndex;
            locals.key.price = input.price;
            if (state.get().ipoSeats.get(locals.key, locals.seat))
            {
                output.seats = locals.seat.shares;
                output.spent = locals.seat.spent;
            }
        }
    }

    PUBLIC_FUNCTION_WITH_LOCALS(GetIpoBids)
    {
        if (input.holdingId >= QPOOL_MAX_HOLDINGS) return;
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.active == 0 || locals.h.ipoContractIndex == 0 || locals.h.ipoSettled != 0) return;
        if (!state.get().ipoBids.get(input.holdingId, locals.bids)) return;
        output.found = 1;
        output.ipoContractIndex = locals.h.ipoContractIndex;
        output.budget = locals.h.costBasis;
        output.maxPrice = locals.h.ipoMaxPrice;
        output.seats = locals.bids.seats;
        output.freeFunds = locals.h.ipoAvailable;
        output.atRisk = locals.h.ipoAtRisk;
        output.refundPot = state.get().ipoRefundEscrow;
        output.count = locals.bids.count;
        output.prices = locals.bids.prices;
        output.seatsAt = locals.bids.seatsAt;
    }

    // ======================== Pool membership ========================
    //
    // ROSTER = RECORD. A member is listed in the pool's roster (`members`, append-only) exactly when he has a
    // Contribution record for the pool, and the record names his roster position (memberIdx). Both are
    // created by EnsureMember (the single exception is SettleIpo, which gives a roster member who has no
    // record one at his own roster position when it refunds him; no procedure can create that state), so
    // every route in (Contribute, BuyPosition, and the pool creator's DepositHoldingAsset) joins a member the
    // same way and refuses the same way when there is no room; the first two charge QPOOL_JOIN_FEE, the
    // creator's deposit puts him on the roster for nothing (owner decision D16). Membership questions are
    // therefore one map lookup (no roster scan), and every loop over the roster meets each member exactly
    // once.

    // Read-only: is `member` new to the pool, and is there room for him? Call it before any QU or asset moves
    // so that a refusal costs nothing.
    PRIVATE_FUNCTION_WITH_LOCALS(JoinCheck)
    {
        output.isNew = 0;
        output.full = 0;
        locals.key.member = input.member;
        locals.key.poolId = input.poolId;
        if (state.get().contributions.contains(locals.key)) return;
        output.isNew = 1;
        if (state.get().pools.get(input.poolId).contributorCount >= QPOOL_MAX_CONTRIBUTORS_PER_POOL
            || state.get().contributions.population() >= QPOOL_MAX_CONTRIBUTIONS)
        {
            output.full = 1;
        }
    }

    // The ONLY place that adds a member: the zeroed Contribution record (with its roster position), the
    // roster slot and the pool's roster size are written together, and the join fee (when he pays one) is
    // booked as revenue. A member that exists already is left alone (isNew 0, nothing written, no fee
    // booked). It refuses with CAPACITY, writing nothing, when a new member has no room; a caller that has
    // already moved money or assets has called JoinCheck first and cannot reach that branch.
    // The pool is persisted here with its new roster size: the caller continues from output.pool.
    PRIVATE_PROCEDURE_WITH_LOCALS(EnsureMember)
    {
        output.returnCode = QPOOL_OK;
        output.isNew = 0;
        locals.key.member = input.member;
        locals.key.poolId = input.poolId;
        locals.pool = state.get().pools.get(input.poolId);
        output.pool = locals.pool;
        if (state.get().contributions.contains(locals.key)) return;
        if (locals.pool.contributorCount >= QPOOL_MAX_CONTRIBUTORS_PER_POOL
            || state.get().contributions.population() >= QPOOL_MAX_CONTRIBUTIONS)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        locals.c.uncommitted = 0;
        locals.c.committed = 0;
        locals.c.lockedForVotes = 0;
        locals.c.lockStartTick = 0;
        locals.c.lockExpiryTick = 0;
        locals.c.lastDepositTick = 0;
        locals.c.memberIdx = locals.pool.contributorCount;
        if (state.mut().contributions.set(locals.key, locals.c) == NULL_INDEX)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;      // cannot happen after the check above
        }
        state.mut().members.set(input.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL + locals.pool.contributorCount, input.member);
        locals.pool.contributorCount = locals.pool.contributorCount + 1;
        state.mut().pools.set(input.poolId, locals.pool);
        if (input.fee > 0)
        {
            state.mut().pendingRevenue = sadd(state.get().pendingRevenue, input.fee);
            state.mut().totalRevenue = sadd(state.get().totalRevenue, input.fee);
        }
        output.isNew = 1;
        output.pool = locals.pool;
    }

    // See ReleaseVotes_input. O(roster) lookups and writes, once per vote that ends, exactly like the loop it replaces in Tally.
    PRIVATE_PROCEDURE_WITH_LOCALS(ReleaseVotes)
    {
        output.released = 0;
        locals.pool = state.get().pools.get(input.poolId);
        locals.base = input.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL;
        locals.wIn.kind = input.kind;
        CALL(VoteWindowOf, locals.wIn, locals.wOut);
        locals.voteEnd = input.openTick + locals.wOut.window;
        locals.vk.proposalId = input.openTick * QPOOL_MAX_PROPOSALS + input.slot;
        for (locals.j = 0; locals.j < locals.pool.contributorCount; locals.j++)
        {
            locals.vk.voter = state.get().members.get(locals.base + locals.j);
            if (!state.get().votes.get(locals.vk, locals.v)) continue;
            if (input.which == 1 && locals.v.approve != 0) continue;
            if (input.kind <= QPOOL_KIND_BUY_IPO)
            {
                locals.ck.member = locals.vk.voter;
                locals.ck.poolId = input.poolId;
                if (state.get().contributions.get(locals.ck, locals.c))
                {
                    // Act at the member's own roster slot only (an id listed twice would otherwise be released twice).
                    if (locals.c.memberIdx != locals.j) continue;
                    if ((uint64)locals.c.lockStartTick <= locals.voteEnd && (uint64)qpi.tick() <= locals.c.lockExpiryTick)
                    {
                        locals.c.lockedForVotes = (locals.c.lockedForVotes > locals.v.weight)
                            ? locals.c.lockedForVotes - locals.v.weight : 0;
                        state.mut().contributions.set(locals.ck, locals.c);
                    }
                }
            }
            state.mut().votes.removeByKey(locals.vk);
            output.released = output.released + 1;
        }
    }

    PUBLIC_PROCEDURE_WITH_LOCALS(CreatePool)
    {
        if ((uint64)qpi.invocationReward() < QPOOL_CREATE_POOL_FEE)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_INSUFFICIENT_FEE; return;
        }
        if (input.poolName == 0 || input.permissioned > 1)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        // The lowest free pool slot. Every slot below poolFreeHint is an active pool (CloseEmptyPool lowers the hint when it frees one), so the
        // search starts there and is O(1) in the usual case instead of walking all 1,024 slots.
        for (locals.i = state.get().poolFreeHint; locals.i < QPOOL_MAX_POOLS; locals.i++)
        {
            if (state.get().pools.get(locals.i).active == 0) break;
        }
        if (locals.i >= QPOOL_MAX_POOLS)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        if ((uint64)qpi.invocationReward() > QPOOL_CREATE_POOL_FEE)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward() - (sint64)QPOOL_CREATE_POOL_FEE);
        }

        locals.pool = state.get().pools.get(locals.i);
        locals.pool.creator = qpi.invocator();
        locals.pool.poolName = input.poolName;
        locals.pool.uncommittedTotal = 0;
        locals.pool.committedTotal = 0;
        locals.pool.contributorCount = 0;
        locals.pool.holdingCount = 0;
        locals.pool.unattributedQu = 0;
        locals.pool.permissioned = input.permissioned;
        locals.pool.admissionRoot = NULL_ID;
        locals.pool.active = 1;
        locals.pool.offerCount = 0;
        locals.pool.admittedCount = 0;
        locals.pool.ownershipRows = 0;
        locals.pool.depositCount = 0;
        locals.pool.ipoHoldings = 0;
        state.mut().pools.set(locals.i, locals.pool);
        state.mut().poolCount = state.get().poolCount + 1;
        state.mut().poolFreeHint = locals.i + 1;

        // Fees accrue as revenue and are split to shareholders and burn at
        // END_EPOCH, rather than being burned outright here.
        state.mut().pendingRevenue = sadd(state.get().pendingRevenue, QPOOL_CREATE_POOL_FEE);
        state.mut().totalRevenue = sadd(state.get().totalRevenue, QPOOL_CREATE_POOL_FEE);

        output.poolId = locals.i;
        output.returnCode = QPOOL_OK;
    }

    // Contribute QU. It stays withdrawable until a passed proposal deploys it. The first contribution
    // of an address to a pool makes it a member (EnsureMember) and pays QPOOL_JOIN_FEE out of the
    // attached QU; an address that is a member already (however it joined: a seat bought with
    // BuyPosition, an asset deposited) pays none and is never listed twice. In a permissioned pool a newcomer must be on
    // the allow-list (or be the pool's creator); a member needs no entry (owner decision D7: roster members always count as admitted).
    PUBLIC_PROCEDURE_WITH_LOCALS(Contribute)
    {
        if (input.poolId >= QPOOL_MAX_POOLS)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.pool = state.get().pools.get(input.poolId);
        if (locals.pool.active == 0)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_NOT_FOUND; return;
        }
        // Is he a member already? (one lookup; read-only)
        locals.jcIn.poolId = input.poolId;
        locals.jcIn.member = qpi.invocator();
        CALL(JoinCheck, locals.jcIn, locals.jcOut);
        // Permissioned pool: only an ADMITTED address may become a member (owner decision D7). A member of the pool, however he got there, always
        // counts as admitted -- switching a pool to private never locks the people who are in it out of adding capital -- and so does the pool's
        // creator. Everybody else needs an entry on the allow-list (SetAdmission).
        if (locals.pool.permissioned != 0 && locals.jcOut.isNew != 0 && qpi.invocator() != locals.pool.creator)
        {
            locals.key.member = qpi.invocator();
            locals.key.poolId = input.poolId;
            if (!state.get().admitted.contains(locals.key))
            {
                if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
                output.returnCode = QPOOL_ERR_NOT_ADMITTED; return;
            }
        }

        // A new member also pays a flat, one-time join fee (see QPOOL_JOIN_FEE) -- collected as pool
        // revenue, same bucket as QPOOL_CREATE_POOL_FEE. A repeat top-up from a member pays none of this.
        locals.joinFee = (locals.jcOut.isNew != 0) ? QPOOL_JOIN_FEE : 0;
        if ((uint64)qpi.invocationReward() < locals.joinFee + QPOOL_MIN_CONTRIBUTION)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        // Capacity BEFORE the QU is retained, so a full roster refunds rather than swallowing the contribution.
        if (locals.jcOut.full != 0)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        locals.amount = (uint64)qpi.invocationReward() - locals.joinFee;

        if (locals.jcOut.isNew != 0)
        {
            locals.emIn.poolId = input.poolId;
            locals.emIn.member = qpi.invocator();
            locals.emIn.fee = locals.joinFee;
            CALL(EnsureMember, locals.emIn, locals.emOut);
            if (locals.emOut.returnCode != QPOOL_OK)
            {
                qpi.transfer(qpi.invocator(), qpi.invocationReward());
                output.returnCode = locals.emOut.returnCode; return;      // cannot happen after JoinCheck; nothing was written
            }
            locals.pool = locals.emOut.pool;
        }
        locals.key.member = qpi.invocator();
        locals.key.poolId = input.poolId;
        state.get().contributions.get(locals.key, locals.c);
        locals.c.uncommitted = sadd(locals.c.uncommitted, locals.amount);
        locals.c.lastDepositTick = qpi.tick();       // money that arrives now has no vote on a proposal that is open already (see Vote)
        state.mut().contributions.set(locals.key, locals.c);
        locals.pool.uncommittedTotal = sadd(locals.pool.uncommittedTotal, locals.amount);
        state.mut().pools.set(input.poolId, locals.pool);
        state.mut().totalContributed = sadd(state.get().totalContributed, locals.amount);

        output.uncommitted = locals.c.uncommitted;
        output.returnCode = QPOOL_OK;
    }

    // Pull back QU that has not been deployed. Committed capital is not
    // withdrawable — exit an active holding through the position market.
    // If the payment fails the member's balance and the pool's total are put back and TRANSFER_FAILED is returned (nothing is lost).
    PUBLIC_PROCEDURE_WITH_LOCALS(WithdrawUncommitted)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.poolId >= QPOOL_MAX_POOLS || input.amount == 0)
        {
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.pool = state.get().pools.get(input.poolId);
        if (locals.pool.active == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }
        locals.key.member = qpi.invocator();
        locals.key.poolId = input.poolId;
        if (!state.get().contributions.get(locals.key, locals.c))
        {
            output.returnCode = QPOOL_ERR_NOT_MEMBER; return;
        }
        if (locals.c.uncommitted < input.amount)
        {
            output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
        }
        // Capital backing a vote cannot be withdrawn until the vote has done its work (a YES vote: until the
        // mandate is carried out or lapses; a NO vote: until Tally) -- see Contribution::lockedForVotes. A lock
        // whose expiry has passed binds nothing.
        locals.locked = ((uint64)qpi.tick() <= locals.c.lockExpiryTick) ? locals.c.lockedForVotes : 0;
        locals.available = (locals.c.uncommitted > locals.locked)
            ? locals.c.uncommitted - locals.locked : 0;
        if (locals.available < input.amount)
        {
            output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
        }

        // Effects before the transfer.
        locals.c.uncommitted = locals.c.uncommitted - input.amount;
        state.mut().contributions.set(locals.key, locals.c);
        locals.totalBefore = locals.pool.uncommittedTotal;
        locals.pool.uncommittedTotal = (locals.pool.uncommittedTotal > input.amount)
            ? locals.pool.uncommittedTotal - input.amount : 0;
        state.mut().pools.set(input.poolId, locals.pool);

        if (qpi.transfer(qpi.invocator(), (sint64)input.amount) < 0)
        {
            // The contract could not pay (its balance is below the amount: cannot happen while the books balance, but a failed payment must
            // never erase the member's balance, audit C028): put BOTH records back, exactly as they were, as ClaimPayout does.
            locals.c.uncommitted = locals.c.uncommitted + input.amount;
            state.mut().contributions.set(locals.key, locals.c);
            locals.pool.uncommittedTotal = locals.totalBefore;
            state.mut().pools.set(input.poolId, locals.pool);
            output.returnCode = QPOOL_ERR_TRANSFER_FAILED; return;
        }
        output.withdrawn = input.amount;
        output.returnCode = QPOOL_OK;
    }

    // Curate a permissioned pool's member set. Creator only.
    //
    // Creator control is safe HERE specifically because of the per-acquisition
    // ownership model: a new member's contribution funds only later
    // acquisitions and can never touch an existing holding's cost basis. So
    // admitting someone cannot dilute anyone already in — which would not be
    // true of a NAV-priced fund, where late entry at a stale price transfers
    // value from existing members.
    PUBLIC_PROCEDURE_WITH_LOCALS(SetAdmission)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.poolId >= QPOOL_MAX_POOLS || input.member == NULL_ID || input.allowed > 1)
        {
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.pool = state.get().pools.get(input.poolId);
        if (locals.pool.active == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }
        // The list is only ever consulted by a permissioned pool, so only a permissioned pool takes new entries; an entry can be revoked
        // whatever the pool is now (a pool that was permissioned and has been opened again still counts its old entries against its
        // quota, and must be able to clear them).
        if (locals.pool.permissioned == 0 && input.allowed != 0) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        if (qpi.invocator() != locals.pool.creator) { output.returnCode = QPOOL_ERR_NOT_AUTHORIZED; return; }

        locals.key.member = input.member;
        locals.key.poolId = input.poolId;
        if (input.allowed == 0)
        {
            // Revoking blocks FUTURE joining only. It cannot claw back seats
            // already owned — those are the member's property and leave only
            // through the position market.
            // Nothing to revoke: nothing is written (the state is not marked dirty for a call that changes nothing).
            if (!state.get().admitted.contains(locals.key)) { output.returnCode = QPOOL_OK; return; }
            state.mut().admitted.removeByKey(locals.key);
            locals.pool.admittedCount = (locals.pool.admittedCount > 0) ? locals.pool.admittedCount - 1 : 0;
            state.mut().pools.set(input.poolId, locals.pool);
            output.returnCode = QPOOL_OK; return;
        }
        // Already admitted: nothing to change, nothing written.
        if (state.get().admitted.contains(locals.key)) { output.returnCode = QPOOL_OK; return; }
        // The pool's own quota first (it cannot be filled by another pool), then the table (defence in depth: with the quotas it cannot be full).
        if (locals.pool.admittedCount >= QPOOL_MAX_ADMITTED_PER_POOL
            || state.get().admitted.population() >= QPOOL_MAX_ADMITTED)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        if (state.mut().admitted.set(locals.key, 1) == NULL_INDEX)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        locals.pool.admittedCount = locals.pool.admittedCount + 1;
        state.mut().pools.set(input.poolId, locals.pool);
        output.returnCode = QPOOL_OK;
    }

    // Toggle a pool between open and permissioned after creation, in BOTH directions and at any time (owner decision D7, final).
    // Creator only.
    //
    // Safe for the same reason SetAdmission is: the per-acquisition ownership
    // model means neither direction can touch an existing holding's cost
    // basis or claw back a seat -- this only ever gates FUTURE joining (Contribute/
    // BuyPosition by a NEWCOMER; a deposit is the creator's alone, whatever the pool is), exactly like SetAdmission's own
    // admit/revoke. Existing seats and voting rights are untouched either way.
    //
    // A member of the pool always counts as admitted, however he joined (owner decision D7): switching to private never locks the people who
    // are in the pool out of adding capital, and never blocks a seat trade between members (BuyPosition by a member), so the creator cannot use
    // the switch to close the seat market on the members (audit C018). What the switch does decide is who may ENTER: in a permissioned pool a
    // newcomer needs an entry on the allow-list (SetAdmission), and the creator himself is admitted automatically.
    //
    // Switching permissioned -> open does not clear `admitted` -- it simply
    // stops being consulted while permissioned == 0. If the pool is later
    // switched back to permissioned, that old list becomes active again as-is.
    //
    // Setting the value the pool has already writes nothing.
    PUBLIC_PROCEDURE_WITH_LOCALS(SetPermissioned)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.poolId >= QPOOL_MAX_POOLS || input.permissioned > 1)
        {
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.pool = state.get().pools.get(input.poolId);
        if (locals.pool.active == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }
        if (qpi.invocator() != locals.pool.creator) { output.returnCode = QPOOL_ERR_NOT_AUTHORIZED; return; }

        if (locals.pool.permissioned == input.permissioned) { output.returnCode = QPOOL_OK; return; }      // nothing changes: nothing is written
        locals.pool.permissioned = input.permissioned;
        state.mut().pools.set(input.poolId, locals.pool);
        output.returnCode = QPOOL_OK;
    }

    // Close a pool in which nothing was ever done, and make its slot free for CreatePool. Creator only. The registration fee is not
    // given back (it is revenue, the price of a place in the table); this only keeps a place from being occupied by a pool nobody uses.
    // "Nothing was ever done" is checked from the pool's own counters, all O(1): nobody ever joined (the roster is append-only, so an
    // empty roster means no member, no record, no vote, no proposal, no seat and no offer ever existed in it), it holds no holding, no money
    // and no allow-list entry (revoke those first with SetAdmission). Such a pool has left nothing behind in any shared table, so
    // the slot can be taken by a new pool without inheriting anything.
    PUBLIC_PROCEDURE_WITH_LOCALS(CloseEmptyPool)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.poolId >= QPOOL_MAX_POOLS) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.pool = state.get().pools.get(input.poolId);
        if (locals.pool.active == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }
        if (qpi.invocator() != locals.pool.creator) { output.returnCode = QPOOL_ERR_NOT_AUTHORIZED; return; }
        if (locals.pool.contributorCount != 0 || locals.pool.holdingCount != 0 || locals.pool.admittedCount != 0
            || locals.pool.uncommittedTotal != 0 || locals.pool.committedTotal != 0 || locals.pool.unattributedQu != 0
            || locals.pool.offerCount != 0 || locals.pool.ownershipRows != 0 || locals.pool.depositCount != 0
            || locals.pool.ipoHoldings != 0)
        {
            output.returnCode = QPOOL_ERR_WRONG_STATUS; return;
        }
        state.mut().pools.set(input.poolId, locals.blank);       // (locals are zero at the start of a call: the blank pool is all zero)
        state.mut().poolCount = (state.get().poolCount > 0) ? state.get().poolCount - 1 : 0;
        if (input.poolId < state.get().poolFreeHint) state.mut().poolFreeHint = input.poolId;
        output.returnCode = QPOOL_OK;
    }

    // ======================== Governance ========================

    // Open a proposal. Buy votes are weighted by uncommitted QU (the money that
    // would actually be spent); sell and allocate votes are weighted by cost
    // basis in the target holding (the people who own the thing being sold).
    PUBLIC_PROCEDURE_WITH_LOCALS(Propose)
    {
        // Free: refund anything attached, whatever happens below.
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.poolId >= QPOOL_MAX_POOLS || input.kind < QPOOL_KIND_BUY_QSWAP
            || input.kind > QPOOL_KIND_ALLOCATE)
        {
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.pool = state.get().pools.get(input.poolId);
        if (locals.pool.active == 0)
        {
            output.returnCode = QPOOL_ERR_NOT_FOUND; return;
        }
        // Only members may propose.
        locals.key.member = qpi.invocator();
        locals.key.poolId = input.poolId;
        if (!state.get().contributions.get(locals.key, locals.c))
        {
            output.returnCode = QPOOL_ERR_NOT_MEMBER; return;
        }
        // One live proposal per member per pool — this is what replaces the fee
        // as the spam guard now that proposing is free. A proposal is live while it can still be voted on or
        // executed: one that nobody tallied or executed is finished once its life is over (openTick + the life of ITS kind:
        // QPOOL_IPO_PROPOSAL_LIFETIME for a BUY_IPO, QPOOL_PROPOSAL_LIFETIME otherwise; the two constants are used inline because a
        // CALL per slot of this scan would cost more than the scan), so its proposer is not held up by a housekeeping call nobody has a
        // reason to make (C076).
        for (locals.i = 0; locals.i < QPOOL_MAX_PROPOSALS; locals.i++)
        {
            if (state.get().proposals.get(locals.i).kind == 0) continue;
            if (state.get().proposals.get(locals.i).poolId != input.poolId) continue;
            if (state.get().proposals.get(locals.i).proposer != qpi.invocator()) continue;
            locals.slotStatus = state.get().proposals.get(locals.i).status;
            locals.life = (state.get().proposals.get(locals.i).kind == QPOOL_KIND_BUY_IPO) ? QPOOL_IPO_PROPOSAL_LIFETIME : QPOOL_PROPOSAL_LIFETIME;
            if ((locals.slotStatus == QPOOL_PROP_OPEN || locals.slotStatus == QPOOL_PROP_PASSED)
                && (uint64)qpi.tick() - state.get().proposals.get(locals.i).openTick <= locals.life)
            {
                output.returnCode = QPOOL_ERR_PROPOSAL_PENDING; return;
            }
        }

        // Per-kind validation.
        if (input.kind == QPOOL_KIND_BUY_QSWAP || input.kind == QPOOL_KIND_BUY_QX)
        {
            // `shares` is mandatory for both: it is the minimum shares out on a
            // QSWAP swap and the order size on QX. A zero min-out would let the
            // swap be sandwiched, so the vote must commit to a bound.
            //
            // CONTRACT SHARES (issuer NULL_ID) can only be bought on QX: QSWAP swaps
            // issuer-issued tokens only. For them `holdingId` carries the index of the
            // contract that pays their dividends (the asset name alone cannot say, and
            // contracts cannot read contractDescriptions); it must be 0 for any other asset.
            // CheckSource validates the pair below (range, table, one-to-one binding, and
            // that the contract exists and has left its IPO).
            locals.contractShares = (input.asset.issuer == NULL_ID) ? 1 : 0;
            if (input.quAmount == 0 || input.quAmount > locals.pool.uncommittedTotal
                || input.asset.assetName == 0 || input.shares == 0
                || (locals.contractShares != 0 && input.kind == QPOOL_KIND_BUY_QSWAP)
                || (locals.contractShares == 0 && input.holdingId != 0))
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
            if (input.kind == QPOOL_KIND_BUY_QX && input.price == 0)
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
            // Reject early what Execute would reject anyway (a payer already bound to
            // another asset, a full payer table, an unreconciled QX bid, a name that does
            // not belong to the contract), so the vote is not wasted. Execute checks again.
            locals.srcIn.asset = input.asset;
            locals.srcIn.ipoContractIndex = 0;
            locals.srcIn.payerIndex = (locals.contractShares != 0) ? input.holdingId : 0;
            locals.srcIn.qxBid = (input.kind == QPOOL_KIND_BUY_QX) ? 1 : 0;
            CALL(CheckSource, locals.srcIn, locals.srcOut);
            if (locals.srcOut.returnCode != QPOOL_OK)
            {
                output.returnCode = locals.srcOut.returnCode; return;
            }
        }
        else if (input.kind == QPOOL_KIND_BUY_IPO)
        {
            // bidInIPO requires SELF_INDEX < IPOContractIndex, so a pool can
            // only bid on contracts registered AFTER it.
            //
            // The proposal must also carry the target's share-asset name in
            // asset.assetName (issuer NULL_ID, because contract shares are
            // protocol-issued). Contracts cannot read contractDescriptions —
            // it is defined after the contract includes precisely to keep it
            // out of reach — so the name has to come from the vote.
            //
            // What the members vote is MONEY (owner decision D1, final): quAmount is the budget, the QU the pool allocates, and it is the
            // ONLY limit of the bidding. There is no voted quantity and no ceiling price: the higher the price goes the fewer seats the
            // budget buys, and the pool cannot know how high an auction will go. price is the OPENING price, the price of the first bid
            // (placed by Execute, all-in: as many seats as the money buys at it, at most 676); it is a starting point for the voters to look
            // at, and later bids (RebidIpo) may be at any price the money pays for. `shares` is not read (it stays in the input so that
            // clients keep their layout): the quantity shown to the voters is derived below.
            //
            // The core takes price x quantity from the contract's whole balance and narrows the quantity to 16 bits, so the price is bounded
            // EXACTLY as the core bounds it before anything is multiplied (audit C001): a price above MAX_AMOUNT/676 would be rejected by
            // the core for ever (a wasted vote). The quantity is always min(676, budget / price), so the product price x quantity never
            // exceeds the budget and never leaves the QU range (QPOOL_IPO_MAX_PRICE x 676 <= MAX_AMOUNT). A budget below the opening price
            // buys no seat at all: refused here.
            if (input.price == 0 || input.price > QPOOL_IPO_MAX_PRICE
                || input.holdingId <= (uint64)SELF_INDEX || input.holdingId > QPOOL_MAX_CONTRACT_INDEX
                || input.asset.assetName == 0 || input.asset.issuer != NULL_ID
                || input.quAmount < input.price || input.quAmount > locals.pool.uncommittedTotal)
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
            // Attribution is by PRICE: the order book shows every slot this contract holds, and slots at price P
            // belong to the bid made at price P. So a new bid must not collide with a live bid on the same IPO,
            // otherwise two pools' slots would be indistinguishable. One hash lookup (the registry of the live
            // IPO holdings). ADVISORY only: two proposals can be open at once, so Execute is the binding check.
            locals.ipoKey.ipoContractIndex = input.holdingId;
            locals.ipoKey.price = input.price;
            if (state.get().ipoPrices.contains(locals.ipoKey))
            {
                output.returnCode = QPOOL_ERR_IPO_PRICE_TAKEN; return;
            }
            // At most QPOOL_MAX_IPO_TARGETS targets can have unsettled holdings at one time: Execute refuses a bid in a further one
            // (binding there), so the voters are told now (advisory: a row can be taken or freed while the vote runs).
            locals.rowIn.index = input.holdingId;
            CALL(IpoRowOf, locals.rowIn, locals.rowOut);
            if (locals.rowOut.found == 0 && locals.rowOut.hasFree == 0)
            {
                output.returnCode = QPOOL_ERR_CAPACITY; return;
            }
            if (state.get().ipoBids.population() >= QPOOL_MAX_IPO_HOLDINGS)      // (advisory too: Execute is binding)
            {
                output.returnCode = QPOOL_ERR_CAPACITY; return;
            }
            // A pool may have QPOOL_MAX_IPO_HOLDINGS_PER_POOL unsettled IPO holdings, so that none can fill the table all pools share
            // (advisory too: Execute is binding).
            if (locals.pool.ipoHoldings >= QPOOL_MAX_IPO_HOLDINGS_PER_POOL)
            {
                output.returnCode = QPOOL_ERR_CAPACITY; return;
            }
            // One asset per paying contract: a second pool bidding on the same
            // IPO must name the same share asset. CheckSource also refuses a name that
            // cannot be this contract's (listed for another index, already claimed by another
            // ledger, or already an issued asset: the IPO's own shares do not exist yet).
            locals.srcIn.asset = input.asset;
            locals.srcIn.ipoContractIndex = input.holdingId;
            locals.srcIn.payerIndex = 0;
            locals.srcIn.qxBid = 0;
            CALL(CheckSource, locals.srcIn, locals.srcOut);
            if (locals.srcOut.returnCode != QPOOL_OK)
            {
                output.returnCode = locals.srcOut.returnCode; return;
            }
        }
        else
        {
            // Sell (QSWAP or QX) or allocate: the holding must exist and belong to this pool.
            if (input.holdingId >= QPOOL_MAX_HOLDINGS)
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
            locals.h = state.get().holdings.get(input.holdingId);
            if (locals.h.active == 0 || locals.h.poolId != input.poolId)
            {
                output.returnCode = QPOOL_ERR_NOT_FOUND; return;
            }
            // Contract shares are sold through QX only: QSWAP swaps issuer-issued tokens, and
            // the cheapest place to say so is before a vote is spent on it. (The holding's
            // own asset decides, not the proposal's asset field.)
            if (input.kind == QPOOL_KIND_SELL_QSWAP && locals.h.asset.issuer == NULL_ID)
            {
                output.returnCode = QPOOL_ERR_QX_ONLY; return;
            }
            // For a sell, quAmount is the MINIMUM acceptable proceeds. Requiring
            // it to be non-zero stops a passed mandate being executed into a
            // sandwiched swap that returns almost nothing.
            if (input.kind != QPOOL_KIND_ALLOCATE
                && (input.shares == 0 || input.shares > locals.h.shares
                    || input.quAmount == 0))
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
            // QPOOL pays QSWAP's flat fee (and the QX management-rights fee) out of its
            // own balance when it sells. A sale whose minimum proceeds do not exceed
            // those fixed costs could net nothing for the holding while costing every
            // other member of the contract money.
            if (input.kind == QPOOL_KIND_SELL_QSWAP
                && input.quAmount <= (uint64)(QPOOL_QSWAP_SELL_FEE + QPOOL_QX_TRANSFER_FEE))
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
            // SELL_QX sells at once, so the two numbers of the proposal mean:
            //   price    -- the lowest price per share QX may fill at (QX itself enforces it);
            //   quAmount -- the least the visible bid book must pay for the whole sale NOW,
            //               after QX's trade fee and before QPOOL's own management fee. It is
            //               checked in Execute before anything moves, and it must exceed the
            //               most QPOOL can pay to put the shares under QX's management.
            // QX rejects an order whose price times shares reaches MAX_AMOUNT, so refuse it
            // here instead of at Execute.
            if (input.kind == QPOOL_KIND_SELL_QX
                && (input.price == 0 || input.quAmount <= QPOOL_QX_SELL_FIXED_COSTS
                    || smul(input.price, input.shares) >= (uint64)MAX_AMOUNT))
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
        }

        // Find a reusable slot: never touched, finished, or live in name only (an OPEN or PASSED proposal whose life is
        // over and that nobody tallied or executed). A live proposal is skipped.
        for (locals.i = 0; locals.i < QPOOL_MAX_PROPOSALS; locals.i++)
        {
            if (state.get().proposals.get(locals.i).kind == 0) break;
            locals.slotStatus = state.get().proposals.get(locals.i).status;
            if (locals.slotStatus == QPOOL_PROP_EXECUTED
                || locals.slotStatus == QPOOL_PROP_REJECTED
                || locals.slotStatus == QPOOL_PROP_EXPIRED
                || locals.slotStatus == QPOOL_PROP_FAILED) break;
            locals.life = (state.get().proposals.get(locals.i).kind == QPOOL_KIND_BUY_IPO) ? QPOOL_IPO_PROPOSAL_LIFETIME : QPOOL_PROPOSAL_LIFETIME;
            if ((uint64)qpi.tick() - state.get().proposals.get(locals.i).openTick > locals.life) break;
        }
        if (locals.i >= QPOOL_MAX_PROPOSALS)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        // A slot is reused only after its proposal's vote is over, and every way a vote ends deletes its rows (Tally,
        // Execute: see ReleaseVotes) -- except the one nobody ended: an OPEN or PASSED proposal left alone past its life.
        // Its rows are deleted here, with the roster of ITS pool (not the new proposal's: votes are keyed by the
        // proposal, so a row of the old proposal could never be mistaken for a vote on the new one anyway -- audit C051 --
        // but a row nobody deletes would stay in the shared vote table for ever).
        if (state.get().proposals.get(locals.i).kind != 0
            && (state.get().proposals.get(locals.i).status == QPOOL_PROP_OPEN
                || state.get().proposals.get(locals.i).status == QPOOL_PROP_PASSED))
        {
            locals.rvIn.slot = locals.i;
            locals.rvIn.poolId = state.get().proposals.get(locals.i).poolId;
            locals.rvIn.kind = state.get().proposals.get(locals.i).kind;
            locals.rvIn.openTick = state.get().proposals.get(locals.i).openTick;
            locals.rvIn.which = 0;
            CALL(ReleaseVotes, locals.rvIn, locals.rvOut);
        }
        // Eligible weight snapshot: whoever can vote on this kind.
        if (input.kind <= QPOOL_KIND_BUY_IPO)
        {
            locals.weight = locals.pool.uncommittedTotal;
        }
        else
        {
            locals.weight = state.get().holdings.get(input.holdingId).costBasis;
        }

        locals.p.poolId = input.poolId;
        locals.p.kind = (uint32)input.kind;
        locals.p.proposer = qpi.invocator();
        locals.p.asset = input.asset;
        // A SELL or ALLOCATE is about ONE holding, named by its slot; a freed slot is taken again (a failed IPO, a sold-out holding, a
        // squat that was evicted). So the proposal also records which holding it was about: its incarnation (Holding::incarnation,
        // different for every holding that can sit in the slot while the proposal lives), beside the pool (already in the proposal) and the asset (stored here, replacing the caller's field, which
        // nothing validated). Vote and Execute refuse a slot that holds anything else now -- even the same asset in the same pool again.
        if (input.kind > QPOOL_KIND_BUY_IPO)
        {
            locals.p.asset = locals.h.asset;
            locals.p.holdingTag = locals.h.incarnation;
        }
        locals.p.quAmount = input.quAmount;
        locals.p.shares = input.shares;
        locals.p.price = input.price;
        if (input.kind == QPOOL_KIND_BUY_IPO)
        {
            // Derived, not voted: the seats the opening bid takes if every voter pays (see Execute). GetProposal shows it.
            locals.p.shares = div(input.quAmount, input.price);
            if (locals.p.shares > QPOOL_IPO_MAX_QUANTITY) locals.p.shares = QPOOL_IPO_MAX_QUANTITY;
        }
        locals.p.holdingId = input.holdingId;
        locals.p.yesWeight = 0;
        locals.p.noWeight = 0;
        locals.p.eligibleWeight = locals.weight;
        locals.p.openTick = qpi.tick();
        locals.p.passedTick = 0;
        locals.p.status = QPOOL_PROP_OPEN;
        state.mut().proposals.set(locals.i, locals.p);
        state.mut().proposalCount = state.get().proposalCount + 1;

        output.proposalId = locals.i;
        output.returnCode = QPOOL_OK;
    }

    // Cast a vote. One vote per member per proposal, weighed when it is cast:
    //   BUY kinds         the member's uncommitted QU (the money that would be spent). Only money that was in the pool when the
    //                     proposal opened counts: a member whose balance grew at that tick or later (lastDepositTick >= openTick) cannot vote
    //                     on it, so QU brought in to carry or veto a decision that is already open has no say. The weight is
    //                     LOCKED until the mandate is carried out or lapses (YES) or until Tally (NO), and a BUY charges only the
    //                     members who voted YES (Execute): a vote binds the people who cast it.
    //   SELL / ALLOCATE   the member's seat units in the holding. The seat is LOCKED until the vote window ends, so the same
    //                     units cannot be sold on to another wallet and vote again.
    PUBLIC_PROCEDURE_WITH_LOCALS(Vote)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.proposalId >= QPOOL_MAX_PROPOSALS || input.approve > 1)
        {
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.p = state.get().proposals.get(input.proposalId);
        if (locals.p.kind == 0 || locals.p.status != QPOOL_PROP_OPEN)
        {
            output.returnCode = QPOOL_ERR_WRONG_STATUS; return;
        }
        locals.wIn.kind = locals.p.kind;
        CALL(VoteWindowOf, locals.wIn, locals.wOut);
        if ((uint64)qpi.tick() - locals.p.openTick > locals.wOut.window)
        {
            output.returnCode = QPOOL_ERR_WINDOW_CLOSED; return;
        }

        // Weight depends on the kind: money at stake for buys, ownership of the
        // asset for sells and allocations.
        if (locals.p.kind <= QPOOL_KIND_BUY_IPO)
        {
            locals.ck.member = qpi.invocator();
            locals.ck.poolId = locals.p.poolId;
            if (!state.get().contributions.get(locals.ck, locals.c))
            {
                output.returnCode = QPOOL_ERR_NOT_MEMBER; return;
            }
            // Money that arrived when the proposal opened or later has no vote on it (C006, D3). The tick of the proposal
            // itself counts as later: inside one tick the order of two transactions is not something to rely on, and a
            // deposit that follows the Propose of its own tick must not weigh more than the eligible weight it opened with.
            if (locals.c.lastDepositTick >= locals.p.openTick)
            {
                output.returnCode = QPOOL_ERR_DEPOSIT_AFTER_OPEN; return;
            }
            locals.weight = locals.c.uncommitted;
        }
        else
        {
            // The holding the proposal was about must still be what sits in that slot (see Propose): the same incarnation, not merely a
            // holding of the same pool and asset that was created in the freed slot later.
            locals.h = state.get().holdings.get(locals.p.holdingId);
            if (locals.h.incarnation != locals.p.holdingTag || locals.h.poolId != locals.p.poolId
                || locals.h.asset.issuer != locals.p.asset.issuer || locals.h.asset.assetName != locals.p.asset.assetName)
            {
                output.returnCode = QPOOL_ERR_NOT_FOUND; return;
            }
            locals.ok.member = qpi.invocator();
            locals.ok.holdingId = locals.p.holdingId;
            if (!state.get().ownership.get(locals.ok, locals.own))
            {
                output.returnCode = QPOOL_ERR_NOT_MEMBER; return;
            }
            locals.weight = locals.own.cost;
        }
        if (locals.weight == 0) { output.returnCode = QPOOL_ERR_NOT_MEMBER; return; }

        // The proposal is named by what it is, not by its slot (slots are reused): see VoteKey.
        locals.vk.voter = qpi.invocator();
        locals.vk.proposalId = locals.p.openTick * QPOOL_MAX_PROPOSALS + input.proposalId;
        if (state.get().votes.contains(locals.vk))
        {
            output.returnCode = QPOOL_ERR_ALREADY_VOTED; return;
        }
        // Defence in depth. With the lock (sells) and the deposit rule (buys) every unit of weight votes at most once
        // and nothing votes that was not there when the proposal opened, so the votes can never add up to more than the
        // weight the proposal opened with. If they ever would, the books are wrong: refuse before anything is written.
        if (sadd(sadd(locals.p.yesWeight, locals.p.noWeight), locals.weight) > locals.p.eligibleWeight)
        {
            output.returnCode = QPOOL_ERR_BOOKS_INCONSISTENT; return;
        }
        // A HashMap refuses a NEW key only when it is full (qpi_hash_map_impl.h), so the capacity
        // check is a plain read, made BEFORE the first write: a vote that cannot be recorded must
        // not mark the whole state dirty for nothing.
        if (state.get().votes.population() >= state.get().votes.capacity())
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        // Record approve/reject alongside the weight: a BUY charges only its YES voters, and the weight is what the
        // lock must give back.
        if (state.mut().votes.set(locals.vk, VoteChoice{ locals.weight, input.approve }) == NULL_INDEX)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;      // cannot happen after the check above
        }

        if (locals.p.kind <= QPOOL_KIND_BUY_IPO)
        {
            // Lock the capital backing this vote (see Contribution::lockedForVotes). A lock that has lapsed starts again
            // from nothing; one that binds grows by this weight. Voting on several simultaneously-open proposals with the
            // same balance sums the locks rather than deduplicating them, so the same QU can look locked more than once
            // until each proposal it backed is over. That only ever over-blocks a withdrawal, never lets one through
            // that it shouldn't -- it never reopens the vote-then-withdraw hole this exists to close.
            locals.end = locals.p.openTick + locals.wOut.lifetime;
            if (locals.end > QPOOL_TICK_LIMIT) locals.end = QPOOL_TICK_LIMIT;
            if ((uint64)qpi.tick() > locals.c.lockExpiryTick)
            {
                locals.c.lockedForVotes = 0;
                locals.c.lockStartTick = qpi.tick();
            }
            locals.c.lockedForVotes = sadd(locals.c.lockedForVotes, locals.weight);
            if (locals.c.lockExpiryTick < locals.end) locals.c.lockExpiryTick = (uint32)locals.end;
            state.mut().contributions.set(locals.ck, locals.c);
        }
        else
        {
            // Lock the seat until this vote can no longer be cast (see Ownership::voteLockUntilTick).
            locals.end = locals.p.openTick + locals.wOut.window;
            if (locals.own.voteLockUntilTick < locals.end)
            {
                locals.own.voteLockUntilTick = locals.end;
                state.mut().ownership.set(locals.ok, locals.own);
            }
        }

        if (input.approve == 1) locals.p.yesWeight = sadd(locals.p.yesWeight, locals.weight);
        else locals.p.noWeight = sadd(locals.p.noWeight, locals.weight);
        state.mut().proposals.set(input.proposalId, locals.p);
        output.weight = locals.weight;
        output.returnCode = QPOOL_OK;
    }

    // Close the vote once its window expires. Permissionless. The window and the life are those of the proposal's KIND (VoteWindowOf): a
    // BUY_IPO is voted on for QPOOL_IPO_VOTE_WINDOW (one day) and lives QPOOL_IPO_PROPOSAL_LIFETIME, every other kind for QPOOL_VOTE_WINDOW
    // (three days) and QPOOL_PROPOSAL_LIFETIME; the execute window is QPOOL_EXECUTE_WINDOW for all of them.
    //   - A vote that failed the quorum or the majority is REJECTED and every vote row is deleted with its lock.
    //   - A vote that carried is PASSED, and the mandate may be executed until EXECUTE_WINDOW ticks after the vote CLOSED
    //     (openTick + the vote window of its kind), whenever Tally was called: waiting does not buy time (audit C019, owner decision D18).
    //     For a BUY the NO votes are deleted (their money was never at stake) and the YES votes stay: they are the
    //     payers (Execute) and their locks hold until it. For a SELL or ALLOCATE nothing else reads the votes, so all
    //     of them are deleted.
    //   - A vote that would have carried but is tallied after the mandate's window has run out (openTick + the life of its
    //     kind, the vote window plus QPOOL_EXECUTE_WINDOW) is EXPIRED at once: nobody can execute a decision that stale. Its rows go the same way.
    PUBLIC_PROCEDURE_WITH_LOCALS(Tally)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.proposalId >= QPOOL_MAX_PROPOSALS) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.p = state.get().proposals.get(input.proposalId);
        if (locals.p.kind == 0 || locals.p.status != QPOOL_PROP_OPEN)
        {
            output.returnCode = QPOOL_ERR_WRONG_STATUS; return;
        }
        locals.wIn.kind = locals.p.kind;
        CALL(VoteWindowOf, locals.wIn, locals.wOut);
        if ((uint64)qpi.tick() - locals.p.openTick <= locals.wOut.window)
        {
            output.returnCode = QPOOL_ERR_WINDOW_OPEN; return;
        }
        locals.cast = sadd(locals.p.yesWeight, locals.p.noWeight);
        locals.quorum = div((uint128)locals.p.eligibleWeight * (uint128)QPOOL_QUORUM_PERMILLE,
            (uint128)1000ULL).low;
        locals.rvIn.which = 0;
        if (locals.cast < locals.quorum || locals.p.yesWeight <= locals.p.noWeight)
        {
            locals.p.status = QPOOL_PROP_REJECTED;
        }
        else if ((uint64)qpi.tick() - locals.p.openTick > locals.wOut.lifetime)
        {
            locals.p.status = QPOOL_PROP_EXPIRED;
        }
        else
        {
            locals.p.status = QPOOL_PROP_PASSED;
            locals.p.passedTick = locals.p.openTick + locals.wOut.window;
            if (locals.p.kind <= QPOOL_KIND_BUY_IPO) locals.rvIn.which = 1;      // the YES voters stay locked until Execute
        }
        state.mut().proposals.set(input.proposalId, locals.p);

        // Delete the vote rows that are not needed any more and lift their locks (see ReleaseVotes). Bounded by
        // contributorCount (<= 256).
        if (locals.cast != 0)
        {
            locals.rvIn.slot = input.proposalId;
            locals.rvIn.poolId = locals.p.poolId;
            locals.rvIn.kind = locals.p.kind;
            locals.rvIn.openTick = locals.p.openTick;
            CALL(ReleaseVotes, locals.rvIn, locals.rvOut);
        }

        output.status = locals.p.status;
        output.returnCode = QPOOL_OK;
    }

    // ======================== Execution ========================

    // Carry out a passed proposal. Permissionless — anyone may fire it, because
    // the mandate came from the vote, not the caller. Must run inside the
    // execute window (EXECUTE_WINDOW ticks after the VOTE closed) so a stale mandate cannot be fired at a new
    // price. A BUY is paid by the members who voted YES and only by them, each for at most the weight he voted
    // with, and the order is sized to their money (see the BUY branch); a SELL or ALLOCATE needs no votes any more
    // (Tally deleted the rows) but must still find the holding it was voted on.
    //
    // IPO bids are executed here rather than in END_EPOCH because
    // qpi.bidInIPO refuses to run while a contract callback is active.
    PUBLIC_PROCEDURE_WITH_LOCALS(Execute)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.proposalId >= QPOOL_MAX_PROPOSALS) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.p = state.get().proposals.get(input.proposalId);
        if (locals.p.status != QPOOL_PROP_PASSED) { output.returnCode = QPOOL_ERR_WRONG_STATUS; return; }
        if ((uint64)qpi.tick() - locals.p.passedTick > QPOOL_EXECUTE_WINDOW)
        {
            locals.p.status = QPOOL_PROP_EXPIRED;
            state.mut().proposals.set(input.proposalId, locals.p);
            // The YES votes of an expired BUY are still on file: delete them (their locks lapsed with the mandate).
            if (locals.p.kind <= QPOOL_KIND_BUY_IPO)
            {
                locals.rvIn.slot = input.proposalId;
                locals.rvIn.poolId = locals.p.poolId;
                locals.rvIn.kind = locals.p.kind;
                locals.rvIn.openTick = locals.p.openTick;
                locals.rvIn.which = 0;
                CALL(ReleaseVotes, locals.rvIn, locals.rvOut);
            }
            output.returnCode = QPOOL_ERR_WINDOW_CLOSED; return;
        }
        locals.pool = state.get().pools.get(locals.p.poolId);
        if (locals.pool.active == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }

        // ---------------- BUY paths ----------------
        // ORDER OF OPERATIONS (money safety): every check first, then a READ-ONLY
        // pass that works out each member's buy-in, then the external acquisition
        // (the only step that can fail), and only then the writes. A failed QSWAP /
        // QX / IPO call therefore leaves no trace: no capital moved to `committed`,
        // no ownership record at a free slot for a later holding to inherit.
        //
        // REFUNDS (BUY_QX, BUY_QSWAP). QX and QSWAP hand back whatever they did not use
        // (QX: the excess over price times shares and every price improvement; QSWAP: the
        // whole amount when it refuses the swap). That QU never leaves the buyers: the
        // members are charged only what was really spent, the refund stays in their
        // uncommitted balances, and it is taken out of globalUnattributed again, where the
        // hook filed it when it arrived (see BuyRefund). A refused acquisition therefore
        // leaves no trace in the counter either. BUY_IPO has its own refund path
        // (the END_EPOCH snapshot and SettleIpo) and is not touched.
        if (locals.p.kind <= QPOOL_KIND_BUY_IPO)
        {
            // The contract's balance, read once: the funded part of the mandate (below) must fit in it.
            qpi.getEntity(SELF, locals.ent);
            locals.balBefore = locals.ent.incomingAmount - locals.ent.outgoingAmount;

            // BUY_IPO: every refusal that needs no scan comes first, so a bid that cannot be placed costs a few
            // lookups and not the free-slot search and the roster pass below.
            if (locals.p.kind == QPOOL_KIND_BUY_IPO)
            {
                // The proposal is checked again, as it will be multiplied and narrowed by the core (audit C001): a
                // proposal only gets here through Propose, which refuses all of this, but the core takes
                // price x quantity from the WHOLE contract balance, so nothing is taken on trust.
                // (The proposal carries a budget and an opening price and nothing else: the quantity is what the money buys, below.)
                if (locals.p.price == 0 || locals.p.price > QPOOL_IPO_MAX_PRICE
                    || locals.p.holdingId <= (uint64)SELF_INDEX || locals.p.holdingId > QPOOL_MAX_CONTRACT_INDEX
                    || locals.p.quAmount < locals.p.price)
                {
                    output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
                }
                // The auction must be running: outside the target's IPO epoch bidInIPO returns -1 (and
                // ipoBidPrice a negative number), so the call cannot succeed and nothing else needs to be read.
                if (qpi.ipoBidPrice((uint32)locals.p.holdingId, 0) < 0)
                {
                    output.returnCode = QPOOL_ERR_IPO_REJECTED; return;
                }
                // The binding price check: no other live holding of this IPO may bid at this price (its slots in the
                // book would be indistinguishable from this holding's). A proposal that loses this race stays PASSED
                // and can never win it back while the other holding is unsettled, so it simply expires.
                locals.ipoKey.ipoContractIndex = locals.p.holdingId;
                locals.ipoKey.price = locals.p.price;
                if (state.get().ipoPrices.contains(locals.ipoKey))
                {
                    output.returnCode = QPOOL_ERR_IPO_PRICE_TAKEN; return;
                }
                if (state.get().ipoPrices.population() >= QPOOL_MAX_IPO_PRICES
                    || state.get().ipoBids.population() >= QPOOL_MAX_IPO_HOLDINGS)
                {
                    output.returnCode = QPOOL_ERR_CAPACITY; return;
                }
                // The pool's own quota of unsettled IPO holdings (C013): one pool, or one attacker with a throw-away pool, can never fill the
                // table all pools share and make everybody else's bid fail with CAPACITY. Before anything is written.
                if (locals.pool.ipoHoldings >= QPOOL_MAX_IPO_HOLDINGS_PER_POOL)
                {
                    output.returnCode = QPOOL_ERR_CAPACITY; return;
                }
                // The target's row of the IPO snapshot: the holding is counted in it, and END_EPOCH reads this target's book at
                // the end of the epoch. A ninth target is refused (before anything moves) because END_EPOCH's work is bounded by the
                // number of rows. A row that already has its snapshot belongs to an IPO that is over.
                locals.rowIn.index = locals.p.holdingId;
                CALL(IpoRowOf, locals.rowIn, locals.rowOut);
                if (locals.rowOut.found != 0)
                {
                    locals.rowIdx = locals.rowOut.row;
                    locals.row = state.get().ipoTargets.get(locals.rowIdx);
                    if (locals.row.snapshotEpoch != 0)
                    {
                        output.returnCode = QPOOL_ERR_IPO_REJECTED; return;
                    }
                }
                else
                {
                    if (locals.rowOut.hasFree == 0)
                    {
                        output.returnCode = QPOOL_ERR_CAPACITY; return;
                    }
                    locals.rowIdx = locals.rowOut.free;
                    locals.row.ipoContractIndex = locals.p.holdingId;
                    locals.row.unsettled = 0;
                    locals.row.bidEpoch = qpi.epoch();
                    locals.row.snapshotEpoch = 0;
                    locals.row.finalPrice = 0;
                }
            }

            // The pool's own quotas first: a pool with all its holding slots in use, or all its seat rows, is refused without reading the table.
            if (locals.pool.holdingCount >= QPOOL_MAX_HOLDINGS_PER_POOL
                || locals.pool.ownershipRows >= QPOOL_MAX_OWNERSHIP_PER_POOL)
            {
                output.returnCode = QPOOL_ERR_CAPACITY; return;
            }
            // Allocate a holding slot first, so ownership can be written. Every slot below holdingFreeHint is occupied, so the search starts
            // there (a freed slot lowers the hint): O(1) unless the first slots after it are occupied.
            for (locals.newHoldingId = state.get().holdingFreeHint; locals.newHoldingId < QPOOL_MAX_HOLDINGS; locals.newHoldingId++)
            {
                if (state.get().holdings.get(locals.newHoldingId).costBasis == 0
                    && state.get().holdings.get(locals.newHoldingId).active == 0) break;
            }
            if (locals.newHoldingId >= QPOOL_MAX_HOLDINGS || state.get().ownership.population() >= QPOOL_MAX_OWNERSHIP)
            {
                output.returnCode = QPOOL_ERR_CAPACITY; return;
            }
            // QX accepts a bid for an asset that does not exist; a payer is only ever
            // bound to an asset that does.
            if (locals.p.kind != QPOOL_KIND_BUY_IPO
                && !qpi.isAssetIssued(locals.p.asset.issuer, locals.p.asset.assetName))
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
            // Dividend-source capacity and binding, checked BEFORE anyone's capital
            // is committed and before any asset moves.
            // Contract shares bought on QX: the proposal's holdingId is the paying contract.
            locals.payerIdx = (locals.p.kind == QPOOL_KIND_BUY_QX && locals.p.asset.issuer == NULL_ID)
                ? locals.p.holdingId : 0;
            locals.srcIn.asset = locals.p.asset;
            locals.srcIn.ipoContractIndex = (locals.p.kind == QPOOL_KIND_BUY_IPO) ? locals.p.holdingId : 0;
            locals.srcIn.payerIndex = locals.payerIdx;
            locals.srcIn.qxBid = (locals.p.kind == QPOOL_KIND_BUY_QX) ? 1 : 0;
            CALL(CheckSource, locals.srcIn, locals.srcOut);
            if (locals.srcOut.returnCode != QPOOL_OK)
            {
                output.returnCode = locals.srcOut.returnCode; return;
            }

            // PASS 1 (read-only): who pays and how much. A BUY charges ONLY the members whose recorded vote is YES (an
            // abstainer or a NO voter is never drafted: audit C006, owner decision D3), each for at most the weight he
            // voted with and at most what he still has (his balance can have been spent by another mandate he backed).
            // This is the one bounded loop in the contract; it runs once per acquisition and fixes ownership of this
            // holding forever.
            locals.base = locals.p.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL;
            locals.yesSum = 0;
            locals.yesCount = 0;
            locals.vk.proposalId = locals.p.openTick * QPOOL_MAX_PROPOSALS + input.proposalId;
            for (locals.i = 0; locals.i < locals.pool.contributorCount; locals.i++)
            {
                locals.memberShare.set(locals.i, 0);
                locals.ck.member = state.get().members.get(locals.base + locals.i);
                locals.ck.poolId = locals.p.poolId;
                if (!state.get().contributions.get(locals.ck, locals.c)) continue;
                // Each member is charged once: only the roster slot his own record names counts, so an id
                // listed twice (it cannot be since EnsureMember) could not be assigned his share twice.
                if (locals.c.memberIdx != locals.i) continue;
                if (locals.c.uncommitted == 0) continue;
                locals.vk.voter = locals.ck.member;
                if (!state.get().votes.get(locals.vk, locals.voteChoice) || locals.voteChoice.approve == 0) continue;
                locals.share = (locals.voteChoice.weight < locals.c.uncommitted) ? locals.voteChoice.weight : locals.c.uncommitted;
                locals.memberShare.set(locals.i, locals.share);
                locals.yesSum = sadd(locals.yesSum, locals.share);
                if (locals.share != 0) locals.yesCount = locals.yesCount + 1;
            }
            // The funded part of the mandate: what was voted, or all the YES voters can pay if that is less. The order is
            // scaled to it below, at the voted limit price, so a mandate is never dead for a rounding reason or because
            // some voter said no or spent his balance elsewhere (audit C015).
            locals.assigned = (locals.p.quAmount < locals.yesSum) ? locals.p.quAmount : locals.yesSum;
            // The order size that money buys. QX and an IPO bid are sized by price x quantity, so a mandate whose funded
            // part cannot buy a single share is dead and is closed (FAILED) once, releasing its proposer and its voters.
            // A swap is sized by its minimum out, scaled to the funded part so that the voted price limit never widens.
            if (locals.p.kind == QPOOL_KIND_BUY_QSWAP)
            {
                locals.quantity = (locals.p.shares > 0 && locals.assigned > 0) ? 1 : 0;
                if (locals.quantity != 0)
                {
                    locals.minOut = div((uint128)locals.p.shares * (uint128)locals.assigned + (uint128)(locals.p.quAmount - 1),
                        (uint128)locals.p.quAmount).low;           // rounded UP: never a looser price than the vote's
                }
            }
            else
            {
                // QX: what the money buys at the limit price, at most the voted share count. IPO: ALL-IN, what the money buys at the opening
                // price and nothing else bounds it but the core's 676 slots (there is no voted quantity: the seats a holding holds are
                // whatever its money buys at the prices it bids, and they shrink as the price rises).
                locals.quantity = div(locals.assigned, locals.p.price);
                if (locals.p.kind == QPOOL_KIND_BUY_IPO)
                {
                    if (locals.quantity > QPOOL_IPO_MAX_QUANTITY) locals.quantity = QPOOL_IPO_MAX_QUANTITY;
                }
                else if (locals.quantity > locals.p.shares) locals.quantity = locals.p.shares;
            }
            if (locals.quantity == 0)
            {
                locals.p.status = QPOOL_PROP_FAILED;
                state.mut().proposals.set(input.proposalId, locals.p);
                locals.rvIn.slot = input.proposalId;
                locals.rvIn.poolId = locals.p.poolId;
                locals.rvIn.kind = locals.p.kind;
                locals.rvIn.openTick = locals.p.openTick;
                locals.rvIn.which = 0;
                CALL(ReleaseVotes, locals.rvIn, locals.rvOut);
                output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
            }
            // Room for the seat rows this buy writes, one for every member who pays (at most yesCount: a share that rounds to nothing
            // writes none): in the pool's own row budget and in the table (which the budgets keep from filling). Before anything moves.
            if (locals.pool.ownershipRows + locals.yesCount > QPOOL_MAX_OWNERSHIP_PER_POOL
                || state.get().ownership.population() + locals.yesCount > QPOOL_MAX_OWNERSHIP)
            {
                output.returnCode = QPOOL_ERR_CAPACITY; return;
            }
            // The contract must hold what it is about to spend.
            if (locals.assigned > locals.balBefore)
            {
                output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
            }
            // Defence in depth, before anything moves: what is charged to the members can never exceed the mandate or the
            // pool's uncommitted total. With unique members and sum(uncommitted) == uncommittedTotal it cannot; if the books
            // were ever wrong, QU of other pools would be spent, so refuse instead.
            if (locals.assigned > locals.p.quAmount || locals.assigned > locals.pool.uncommittedTotal)
            {
                output.returnCode = QPOOL_ERR_BOOKS_INCONSISTENT; return;
            }
            // Split the funded part over the payers in proportion to the weight each can pay, by rounding the RUNNING total
            // down: share(i) = floor(P(i) x funded / yesSum) - floor(P(i-1) x funded / yesSum), P(i) the sum of the weights
            // up to member i. The shares add up to `funded` EXACTLY, so an exact budget is never short by a unit of
            // rounding, and each share is at most that member's weight (the funded part is at most yesSum).
            locals.prefix = 0;
            locals.prevCum = 0;
            for (locals.i = 0; locals.i < locals.pool.contributorCount; locals.i++)
            {
                locals.prefix = locals.prefix + locals.memberShare.get(locals.i);
                locals.cum = div((uint128)locals.prefix * (uint128)locals.assigned, (uint128)locals.yesSum).low;
                locals.memberShare.set(locals.i, locals.cum - locals.prevCum);
                locals.prevCum = locals.cum;
            }

            // Create the holding on the ACTUAL assigned total, so integer
            // remainders never leave costBasis overstated. (For BUY_QX and BUY_QSWAP this is
            // provisional: costBasis becomes what was really spent once the refund is known.)
            locals.refund = 0;
            locals.spent = locals.assigned;
            locals.h.poolId = locals.p.poolId;
            locals.h.asset = locals.p.asset;
            locals.h.shares = 0;
            locals.h.costBasis = locals.assigned;
            locals.h.payoutPerCostX64 = uint128(0, 0);
            locals.h.totalPayouts = 0;
            locals.h.deposited = 0;
            locals.h.ipoContractIndex = 0;
            locals.h.payerIndex = locals.payerIdx;
            locals.h.pendingShares = 0;
            locals.h.active = 1;
            locals.h.ipoSettled = 0;
            locals.h.srcSnapX64 = uint128(0, 0);
            // A new holding is a new incarnation of its slot (see Holding::incarnation). Taken here, written with the holding below.
            locals.serial = state.get().holdingSerial + 1;
            locals.h.incarnation = (uint32)locals.serial;

            if (locals.p.kind == QPOOL_KIND_BUY_QSWAP)
            {
                locals.heldBefore = qpi.numberOfShares(locals.p.asset,
                    AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
                locals.swapBuyIn.assetIssuer = locals.p.asset.issuer;
                locals.swapBuyIn.assetName = locals.p.asset.assetName;
                locals.swapBuyIn.assetAmountOutMin = (sint64)locals.minOut;   // slippage bound, scaled to the funded part
                // The baselines of the refund accounting, read right before the call.
                qpi.getEntity(SELF, locals.ent);
                locals.balBefore = locals.ent.incomingAmount - locals.ent.outgoingAmount;
                locals.gaBefore = state.get().globalUnattributed;
                INVOKE_OTHER_CONTRACT_PROCEDURE_E(QSWAP, SwapExactQuForAsset,
                    locals.swapBuyIn, locals.swapBuyOut, (sint64)locals.assigned, swapErr);
                // A call that failed before it ran moved nothing and parked nothing.
                if (swapErr != NoCallError) { output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return; }
                locals.refIn.assigned = locals.assigned;
                locals.refIn.balBefore = locals.balBefore;
                locals.refIn.gaBefore = locals.gaBefore;
                CALL(BuyRefund, locals.refIn, locals.refOut);
                locals.refund = locals.refOut.refund;
                locals.heldAfter = qpi.numberOfShares(locals.p.asset,
                    AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
                locals.h.shares = (locals.heldAfter > locals.heldBefore)
                    ? (uint64)(locals.heldAfter - locals.heldBefore) : 0;
                // A swap that bought nothing was refunded: nothing to record. The QU it handed back
                // is not a stray arrival, so it leaves the counter the hook filed it under.
                if (locals.h.shares == 0)
                {
                    locals.tbIn.amount = locals.refund;
                    CALL(TakeBackRefund, locals.tbIn, locals.tbOut);
                    output.refunded = locals.refund;
                    output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return;
                }
                output.acquired = locals.h.shares;
            }
            else if (locals.p.kind == QPOOL_KIND_BUY_QX)
            {
                // Order-book buy. QX matches the bid against resting asks inside this
                // call and delivers those shares at once; the rest of the order rests
                // and fills later, silently, so it is held as pendingShares until
                // ReconcileHolding.
                locals.heldBefore = qpi.numberOfShares(locals.p.asset,
                    AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
                locals.qxBidIn.issuer = locals.p.asset.issuer;
                locals.qxBidIn.assetName = locals.p.asset.assetName;
                locals.qxBidIn.price = (sint64)locals.p.price;
                locals.qxBidIn.numberOfShares = (sint64)locals.quantity;      // what the funded part buys at the voted price
                // The baselines of the refund accounting, read right before the call.
                qpi.getEntity(SELF, locals.ent);
                locals.balBefore = locals.ent.incomingAmount - locals.ent.outgoingAmount;
                locals.gaBefore = state.get().globalUnattributed;
                INVOKE_OTHER_CONTRACT_PROCEDURE_E(QX, AddToBidOrder,
                    locals.qxBidIn, locals.qxBidOut, (sint64)locals.assigned, qxErr);
                // A call that failed before it ran moved nothing and parked nothing.
                if (qxErr != NoCallError) { output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return; }
                locals.refIn.assigned = locals.assigned;
                locals.refIn.balBefore = locals.balBefore;
                locals.refIn.gaBefore = locals.gaBefore;
                CALL(BuyRefund, locals.refIn, locals.refOut);
                locals.refund = locals.refOut.refund;
                // addedNumberOfShares is the WHOLE order size, immediate fills
                // included; 0 means QX rejected the order and refunded the QU, which leaves
                // the counter the hook filed it under again.
                if (locals.qxBidOut.addedNumberOfShares <= 0)
                {
                    locals.tbIn.amount = locals.refund;
                    CALL(TakeBackRefund, locals.tbIn, locals.tbOut);
                    output.refunded = locals.refund;
                    output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return;
                }
                locals.heldAfter = qpi.numberOfShares(locals.p.asset,
                    AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
                locals.filled = (locals.heldAfter > locals.heldBefore)
                    ? (uint64)(locals.heldAfter - locals.heldBefore) : 0;
                if (locals.filled > (uint64)locals.qxBidOut.addedNumberOfShares)
                {
                    locals.filled = (uint64)locals.qxBidOut.addedNumberOfShares;
                }
                locals.h.shares = locals.filled;
                locals.h.pendingShares = (uint64)locals.qxBidOut.addedNumberOfShares - locals.filled;
                output.acquired = locals.h.shares;
            }
            else
            {
                // IPO bid. holdingId carries the target contract index, and the
                // asset is {NULL_ID, shareAssetName} from the proposal. The FIRST bid, ALL-IN at the voted opening price: as many seats as
                // the funded money buys at it, at most 676 (inside the core's limits: the price was checked at the top of this branch
                // and quantity <= 676, so price x quantity <= assigned never wraps). The rest of the money (less than one seat at the
                // opening price) is the holding's free funds; later bids (RebidIpo) are funded by free funds alone, at any price they pay for.
                locals.h.ipoContractIndex = locals.p.holdingId;
                locals.h.ipoMaxPrice = locals.p.price;
                locals.h.ipoAvailable = locals.assigned;

                locals.share = smul(locals.p.price, locals.quantity);
                if (locals.share > locals.h.ipoAvailable)
                {
                    output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
                }
                // The baseline of the spend check below, read right before the call.
                qpi.getEntity(SELF, locals.ent);
                locals.balBefore = locals.ent.incomingAmount - locals.ent.outgoingAmount;
                locals.ipoResult = qpi.bidInIPO((uint32)locals.p.holdingId,
                    (sint64)locals.p.price, (uint32)locals.quantity);
                if (locals.ipoResult < 0) { output.returnCode = QPOOL_ERR_IPO_REJECTED; return; }
                // The full amount is deducted even on a partial fill; anything
                // not seated is refunded straight back as ipoBidRefund.
                locals.h.ipoAtRisk = locals.share;
                locals.h.ipoAvailable = locals.h.ipoAvailable - locals.share;
                locals.h.pendingShares = (uint64)locals.ipoResult;
                // The holding's list of bid prices: the opening price, with the seats the first bid seated (0 if the book was full above it:
                // the price is dead on arrival and the first reconcile releases it).
                locals.bids.count = 1;
                locals.bids.seats = (uint64)locals.ipoResult;
                locals.bids.prices.set(0, locals.p.price);
                locals.bids.seatsAt.set(0, (uint64)locals.ipoResult);
                // Defence in depth (audit C001): the core takes price x quantity out of the WHOLE contract
                // balance, i.e. out of every pool. The QU that left the contract in this call can only be
                // that amount less what the core handed straight back to us (an unseated or evicted slot of
                // QPOOL's own is refunded to QPOOL), so it can never exceed it. If it does, the core has
                // taken more than this holding books and the difference is other pools' money. The bid
                // cannot be undone, so the call REFUSES NOTHING here (a failed Execute would leave the bid
                // in the book unbooked and the proposal free to be run again, losing the same again): the
                // holding is recorded as bid, the proposal is consumed, and the call reports the defect.
                qpi.getEntity(SELF, locals.ent);
                locals.balAfter = locals.ent.incomingAmount - locals.ent.outgoingAmount;
                if (locals.balBefore > locals.balAfter && locals.balBefore - locals.balAfter > locals.share)
                {
                    locals.overspent = (locals.balBefore - locals.balAfter) - locals.share;
                }
            }

            // What QX or QSWAP handed back stays with the buyers: only `spent` was really paid.
            // (A buy that reports success yet spent nothing is not a purchase.)
            if (locals.p.kind != QPOOL_KIND_BUY_IPO)
            {
                if (locals.refund >= locals.assigned)
                {
                    locals.tbIn.amount = locals.refund;
                    CALL(TakeBackRefund, locals.tbIn, locals.tbOut);
                    output.refunded = locals.refund;
                    output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return;
                }
                locals.spent = locals.assigned - locals.refund;
                if (locals.refund > 0)
                {
                    // Split `spent` over the members in proportion to their buy-ins by rounding the
                    // RUNNING total down: cost(i) = floor(P(i) x spent / assigned) - floor(P(i-1) x
                    // spent / assigned), where P(i) is the sum of the buy-ins of the roster up to
                    // member i. The costs add up to `spent` EXACTLY (the last running total is
                    // `spent`), so the uncommitted balances drop by exactly the QU that left the
                    // contract and no dust is left unowned; each cost is within one unit of its exact
                    // pro-rata value and never above the member's buy-in.
                    locals.prefix = 0;
                    locals.prevCum = 0;
                    for (locals.i = 0; locals.i < locals.pool.contributorCount; locals.i++)
                    {
                        locals.prefix = locals.prefix + locals.memberShare.get(locals.i);
                        locals.cum = div((uint128)locals.prefix * (uint128)locals.spent,
                            (uint128)locals.assigned).low;
                        locals.memberCost.set(locals.i, locals.cum - locals.prevCum);
                        locals.prevCum = locals.cum;
                    }
                }
                locals.h.costBasis = locals.spent;
                // The refund is no stray arrival: it leaves the counter the hook filed it under.
                locals.tbIn.amount = locals.refund;
                CALL(TakeBackRefund, locals.tbIn, locals.tbOut);
                output.refunded = locals.refund;
            }

            // The acquisition succeeded: from here on nothing can fail. Enrol the
            // holding in its payer's ledger (CheckSource guarantees there is room).
            locals.regIn.h = locals.h;
            CALL(RegisterHolding, locals.regIn, locals.regOut);
            locals.h = locals.regOut.h;

            // PASS 2 (writes): move each member's buy-in from uncommitted to committed
            // and record their seat in the new holding.
            for (locals.i = 0; locals.i < locals.pool.contributorCount; locals.i++)
            {
                locals.share = (locals.refund > 0) ? locals.memberCost.get(locals.i)
                                                   : locals.memberShare.get(locals.i);
                if (locals.share == 0) continue;
                locals.ck.member = state.get().members.get(locals.base + locals.i);
                locals.ck.poolId = locals.p.poolId;
                if (!state.get().contributions.get(locals.ck, locals.c)) continue;
                locals.c.uncommitted = (locals.c.uncommitted > locals.share) ? locals.c.uncommitted - locals.share : 0;
                locals.c.committed = sadd(locals.c.committed, locals.share);
                state.mut().contributions.set(locals.ck, locals.c);

                locals.ok.member = locals.ck.member;
                locals.ok.holdingId = locals.newHoldingId;
                locals.own.cost = locals.share;
                locals.own.payoutDebtX64 = uint128(0, 0);
                locals.own.claimable = 0;
                locals.own.voteLockUntilTick = 0;
                state.mut().ownership.set(locals.ok, locals.own);
                locals.created = locals.created + 1;
            }
            // The mandate is carried out: the YES voters have paid, so their locks end and their rows are deleted (the
            // proposal is EXECUTED below). A YES voter whose share rounded to nothing is released all the same.
            locals.rvIn.slot = input.proposalId;
            locals.rvIn.poolId = locals.p.poolId;
            locals.rvIn.kind = locals.p.kind;
            locals.rvIn.openTick = locals.p.openTick;
            locals.rvIn.which = 0;
            CALL(ReleaseVotes, locals.rvIn, locals.rvOut);
            locals.pool.uncommittedTotal = (locals.pool.uncommittedTotal > locals.spent)
                ? locals.pool.uncommittedTotal - locals.spent : 0;
            locals.pool.committedTotal = sadd(locals.pool.committedTotal, locals.spent);

            locals.h.rows = (uint32)locals.created;
            state.mut().holdings.set(locals.newHoldingId, locals.h);
            state.mut().holdingSerial = locals.serial;
            if (locals.p.kind == QPOOL_KIND_BUY_IPO)
            {
                // The bid is in the book: claim its price for this holding (checked free at the top of this
                // branch; the tables were checked to have room there as well, see QPOOL_MAX_IPO_PRICES), record the holding's list of prices
                // and count the holding in its target's row (checked at the top as well) and in the pool's quota.
                state.mut().ipoPrices.set(locals.ipoKey, locals.newHoldingId);
                state.mut().ipoBids.set(locals.newHoldingId, locals.bids);
                locals.row.unsettled = locals.row.unsettled + 1;
                state.mut().ipoTargets.set(locals.rowIdx, locals.row);
                locals.pool.ipoHoldings = locals.pool.ipoHoldings + 1;
            }
            if (locals.newHoldingId >= state.get().holdingCount)
            {
                state.mut().holdingCount = locals.newHoldingId + 1;
            }
            state.mut().holdingFreeHint = locals.newHoldingId + 1;      // (the slot just taken was the first free one at or after the hint)
            locals.pool.holdingCount = locals.pool.holdingCount + 1;
            locals.pool.ownershipRows = sadd(locals.pool.ownershipRows, locals.created);
            state.mut().pools.set(locals.p.poolId, locals.pool);
            state.mut().totalDeployed = sadd(state.get().totalDeployed, locals.spent);

            output.holdingId = locals.newHoldingId;
        }
        // ---------------- SELL paths ----------------
        // A SELL or ALLOCATE mandate is about the holding it was voted on (see Propose): a slot that holds something else
        // now (a freed slot is taken again) is refused before anything is read from it or moved. The incarnation is what tells the
        // holding apart; the pool and the asset are checked as well.
        else if (locals.p.kind > QPOOL_KIND_BUY_IPO
            && (state.get().holdings.get(locals.p.holdingId).incarnation != locals.p.holdingTag
                || state.get().holdings.get(locals.p.holdingId).poolId != locals.p.poolId
                || state.get().holdings.get(locals.p.holdingId).asset.issuer != locals.p.asset.issuer
                || state.get().holdings.get(locals.p.holdingId).asset.assetName != locals.p.asset.assetName))
        {
            output.returnCode = QPOOL_ERR_NOT_FOUND; return;
        }
        else if (locals.p.kind == QPOOL_KIND_SELL_QSWAP)
        {
            locals.h = state.get().holdings.get(locals.p.holdingId);
            // Defence in depth for what Propose already refuses: contract shares are never
            // sold through QSWAP. Checked before anything is written and before any call into
            // QX or QSWAP. The proposal stays as it is.
            if (locals.h.asset.issuer == NULL_ID)
            {
                output.returnCode = QPOOL_ERR_QX_ONLY; return;
            }
            if (locals.h.active == 0 || locals.h.shares < locals.p.shares)
            {
                output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
            }
            // Refuse while the holding's source has escrow. Escrowed dividends belong to
            // whoever held the shares when they arrived; selling now would drop this holding
            // out of that split. The proposal stays PASSED, so it can be retried inside its
            // execute window once the source has settled (SettleIpo / ReconcileHolding are
            // permissionless). A plain read: nothing is written (so a refusal, or an external
            // call that fails below, does not dirty the state); the holding is synced by
            // ApplyHolding once the shares have really left.
            locals.escIn.asset = locals.h.asset;
            locals.escIn.ipoContractIndex = locals.h.ipoContractIndex;
            locals.escIn.payerIndex = locals.h.payerIndex;
            CALL(HoldingEscrow, locals.escIn, locals.escOut);
            if (locals.escOut.escrow != 0)
            {
                output.returnCode = QPOOL_ERR_SETTLEMENT_PENDING; return;
            }
            qpi.getEntity(SELF, locals.ent);
            locals.balBefore = locals.ent.incomingAmount - locals.ent.outgoingAmount;
            locals.heldBefore = qpi.numberOfShares(locals.h.asset,
                AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
            // What QX or QSWAP pay back during the calls below reaches the hook as arrivals
            // that match no ledger (see UnparkArrivals); read the counter now, give it back after.
            locals.gaBefore = state.get().globalUnattributed;

            {
                // BUGFIX: releaseShares() requires the CALLING contract
                // (QPOOL, SELF) to already hold management rights over the
                // shares being released -- confirmed live: shares bought via
                // BUY_QSWAP (SwapExactQuForAsset) are already managed by
                // QSWAP from the moment of purchase, so an unconditional
                // release here always fails with INVALID_AMOUNT for exactly
                // the holdings this path exists to sell (QPOOL never held
                // management rights over them to transfer away). Only
                // release if NOT already under QSWAP's management -- e.g. a
                // DepositHoldingAsset-sourced holding, still under whatever
                // management its depositor originally had.
                //
                // BUGFIX #8: that "release if not already there" logic only
                // ever tried releasing shares QPOOL itself manages directly
                // (releaseShares can only move shares the CALLING contract
                // holds -- see its own implementation) -- it never accounted
                // for a holding bought via QX (BUY_QX) whose members later
                // vote to sell through QSWAP instead. Confirmed live: a
                // cross-venue sell (bought on one venue, voted to sell on
                // the other) always failed at Execute with
                // QPOOL_ERR_TRANSFER_FAILED, since the shares are managed by
                // QX, not by QPOOL itself, and QPOOL cannot release shares
                // it doesn't hold. Members can and do buy on either venue
                // and vote to sell on either venue independently of how they
                // bought, so a holding's shares can genuinely be sitting
                // under QSWAP, under QX, or under QPOOL itself (a deposited
                // holding) at the moment a sell executes -- check all three
                // and, if it's the other venue, ask THAT venue's own
                // TransferShareManagementRights to hand it over first.
                locals.alreadyManaged = qpi.numberOfShares(locals.h.asset,
                    AssetOwnershipSelect{ SELF, QPOOL_QSWAP_CONTRACT_INDEX, false, false },
                    AssetPossessionSelect{ SELF, QPOOL_QSWAP_CONTRACT_INDEX, false, false });
                if (locals.alreadyManaged < (sint64)locals.p.shares)
                {
                    locals.managedBySelf = qpi.numberOfShares(locals.h.asset,
                        AssetOwnershipSelect{ SELF, SELF_INDEX, false, false },
                        AssetPossessionSelect{ SELF, SELF_INDEX, false, false });
                    if (locals.managedBySelf >= (sint64)locals.p.shares)
                    {
                        locals.rel = qpi.releaseShares(locals.h.asset, SELF, SELF, (sint64)locals.p.shares,
                            QPOOL_QSWAP_CONTRACT_INDEX, QPOOL_QSWAP_CONTRACT_INDEX, QPOOL_QX_TRANSFER_FEE);
                        if (locals.rel < 0) { output.returnCode = QPOOL_ERR_TRANSFER_FAILED; return; }
                    }
                    else
                    {
                        // Must be under QX's management (bought via BUY_QX,
                        // now being sold via SELL_QSWAP) -- ask QX itself to
                        // hand it to QSWAP, since QPOOL cannot release
                        // shares it does not itself hold.
                        locals.xferToQswapIn.asset = locals.h.asset;
                        locals.xferToQswapIn.numberOfShares = (sint64)locals.p.shares;
                        locals.xferToQswapIn.newManagingContractIndex = QPOOL_QSWAP_CONTRACT_INDEX;
                        INVOKE_OTHER_CONTRACT_PROCEDURE_E(QX, TransferShareManagementRights,
                            locals.xferToQswapIn, locals.xferToQswapOut, QPOOL_QX_TRANSFER_FEE, xferQswapErr);
                        if (xferQswapErr != NoCallError
                            || locals.xferToQswapOut.transferredNumberOfShares < (sint64)locals.p.shares)
                        {
                            // QX hands the attached fee straight back; that is not an unattributed arrival.
                            locals.unpIn.before = locals.gaBefore;
                            CALL(UnparkArrivals, locals.unpIn, locals.unpOut);
                            output.returnCode = QPOOL_ERR_TRANSFER_FAILED; return;
                        }
                    }
                }
                locals.swapSellIn.assetIssuer = locals.h.asset.issuer;
                locals.swapSellIn.assetName = locals.h.asset.assetName;
                locals.swapSellIn.assetAmountIn = (sint64)locals.p.shares;
                locals.swapSellIn.quAmountOutMin = (sint64)locals.p.quAmount; // min proceeds
                // BUGFIX: SwapExactAssetForQu requires invocationReward() >=
                // QSWAP_ADDITIONAL_FEE or it silently does nothing (confirmed
                // live: a SELL_QSWAP proposal reached PASSED and Execute
                // returned without error, but no shares ever sold and no QU
                // ever received -- attaching 0 here always hits QSWAP's own
                // early-return path). Fund it from QPOOL's own balance, the
                // same way the BUY_QSWAP path above clears this floor simply
                // because the swap amount itself is large enough.
                INVOKE_OTHER_CONTRACT_PROCEDURE_E(QSWAP, SwapExactAssetForQu,
                    locals.swapSellIn, locals.swapSellOut, QPOOL_QSWAP_SELL_FEE, sellErr);
                if (sellErr != NoCallError) { output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return; }

                // Everything QSWAP paid back (the proceeds, or the fee when it sold nothing) is
                // accounted for below, so it is no longer an unattributed arrival.
                locals.unpIn.before = locals.gaBefore;
                CALL(UnparkArrivals, locals.unpIn, locals.unpOut);

                // What actually left is read back from the universe: QSWAP gives the QU back
                // and sells nothing when its slippage bound is not met. Once shares HAVE left,
                // they must leave the holding's weight whatever the proceeds turn out to be --
                // an error return here would leave a holding earning on shares QPOOL no
                // longer owns, at its siblings' expense.
                locals.heldAfter = qpi.numberOfShares(locals.h.asset,
                    AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
                locals.sold = (locals.heldBefore > locals.heldAfter)
                    ? (uint64)(locals.heldBefore - locals.heldAfter) : 0;
                if (locals.sold == 0) { output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return; }
                if (locals.sold > locals.h.shares) locals.sold = locals.h.shares;

                qpi.getEntity(SELF, locals.ent);
                locals.balAfter = locals.ent.incomingAmount - locals.ent.outgoingAmount;
                locals.received = (locals.balAfter > locals.balBefore)
                    ? locals.balAfter - locals.balBefore : 0;

                // Weight change goes through the choke point: it first credits what
                // the holding earned on the shares it is about to lose, then shrinks
                // the ledger's recognised total, and retires the holding if it is empty.
                locals.apIn.holdingId = locals.p.holdingId;
                locals.apIn.h = locals.h;
                locals.apIn.newShares = locals.h.shares - locals.sold;
                locals.apIn.newPending = locals.h.pendingShares;
                locals.apIn.markSettled = 0;
                locals.apIn.claimEscrow = 0;
                locals.apIn.retire = (locals.apIn.newShares == 0 && locals.h.pendingShares == 0) ? 1 : 0;
                CALL(ApplyHolding, locals.apIn, locals.apOut);
                locals.h = locals.apOut.h;
                // Management fee first, then proceeds ride the SAME accumulator
                // as dividends. costBasis is untouched, so every member's
                // fraction is preserved. Sale proceeds belong to THIS holding
                // alone, so they are added straight to its accumulator. (Propose
                // keeps min proceeds above the fixed costs QPOOL pays, so `received`
                // is positive; if it ever is not, there is simply nothing to credit.)
                locals.crIn.holdingId = locals.p.holdingId;
                locals.crIn.h = locals.h;
                locals.crIn.received = locals.received;
                CALL(CreditProceeds, locals.crIn, locals.crOut);
                locals.h = locals.crOut.h;
                output.proceeds = locals.crOut.net;
                output.sold = locals.sold;
            }
        }
        // ---------------- SELL_QX: sell now on QX, never leave an ask resting ----------------
        // ORDER OF OPERATIONS (money safety), in the same spirit as the BUY paths:
        //   1. CHECKS, reads only. A refusal writes nothing (no state hash, no fee paid) and
        //      leaves the proposal PASSED, so it can be retried inside its execute window. The
        //      QX bid book is read here: the proposal's minimum proceeds can only protect the
        //      members BEFORE the shares go, because a sale cannot be undone.
        //   2. EFFECTS. Put the shares under QX's management if they are not yet, place ONE ask
        //      at the limit price (QX matches it against resting bids inside that call),
        //      read back how much of it did not fill, cancel exactly that, and only then
        //      record the sale. After step 1 the only thing that can still go wrong is QX
        //      itself failing, and then the sale is recorded for whatever did sell.
        // The shares that sold and the QU that arrived are both known exactly when the calls
        // return, so the sale is credited to THIS holding alone, like SELL_QSWAP.
        else if (locals.p.kind == QPOOL_KIND_SELL_QX)
        {
            locals.h = state.get().holdings.get(locals.p.holdingId);
            if (locals.h.active == 0 || locals.p.shares == 0 || locals.h.shares < locals.p.shares)
            {
                output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
            }
            // Propose refuses these; a mandate built by other means must not reach QX either.
            if (locals.p.price == 0 || smul(locals.p.price, locals.p.shares) >= (uint64)MAX_AMOUNT)
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
            // Dividends parked for this holding's source belong to whoever held the shares when
            // they arrived; selling now would drop the holding out of that split. Same rule as
            // SELL_QSWAP, but read without syncing (a sync would write): retry once the source
            // has settled (SettleIpo / ReconcileHolding are permissionless).
            locals.escIn.asset = locals.h.asset;
            locals.escIn.ipoContractIndex = locals.h.ipoContractIndex;
            locals.escIn.payerIndex = locals.h.payerIndex;
            CALL(HoldingEscrow, locals.escIn, locals.escOut);
            if (locals.escOut.escrow != 0)
            {
                output.returnCode = QPOOL_ERR_SETTLEMENT_PENDING; return;
            }
            // The shares must exist, whoever manages them.
            locals.heldBefore = qpi.numberOfShares(locals.h.asset,
                AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
            if (locals.heldBefore < (sint64)locals.p.shares)
            {
                output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
            }
            // QX only sells shares under its own management. IPO-won shares and shares bought on
            // QX already are; shares bought on QSWAP sit with QSWAP and deposited shares with
            // QPOOL itself, and whatever is short is handed over first (a fee of
            // QPOOL_QX_TRANSFER_FEE per source). Work out the plan now, before anything moves.
            locals.managedQx = qpi.numberOfShares(locals.h.asset,
                AssetOwnershipSelect{ SELF, QPOOL_QX_CONTRACT_INDEX, false, false },
                AssetPossessionSelect{ SELF, QPOOL_QX_CONTRACT_INDEX, false, false });
            locals.fromSelf = 0;
            locals.fromQswap = 0;
            locals.costs = 0;
            if (locals.managedQx < (sint64)locals.p.shares)
            {
                locals.remaining = locals.p.shares - (uint64)locals.managedQx;
                locals.managedBySelf = qpi.numberOfShares(locals.h.asset,
                    AssetOwnershipSelect{ SELF, SELF_INDEX, false, false },
                    AssetPossessionSelect{ SELF, SELF_INDEX, false, false });
                locals.alreadyManaged = qpi.numberOfShares(locals.h.asset,
                    AssetOwnershipSelect{ SELF, QPOOL_QSWAP_CONTRACT_INDEX, false, false },
                    AssetPossessionSelect{ SELF, QPOOL_QSWAP_CONTRACT_INDEX, false, false });
                if (locals.managedBySelf < 0) locals.managedBySelf = 0;
                if (locals.alreadyManaged < 0) locals.alreadyManaged = 0;
                locals.fromSelf = ((uint64)locals.managedBySelf < locals.remaining)
                    ? (uint64)locals.managedBySelf : locals.remaining;
                locals.fromQswap = locals.remaining - locals.fromSelf;
                if (locals.fromQswap > (uint64)locals.alreadyManaged)
                {
                    // The rest is under some other contract's management: not ours to move.
                    output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
                }
                if (locals.fromSelf > 0) locals.costs = locals.costs + (uint64)QPOOL_QX_TRANSFER_FEE;
                if (locals.fromQswap > 0) locals.costs = locals.costs + (uint64)QPOOL_QX_TRANSFER_FEE;
            }
            // The visible book must pay the proposal's minimum, at its limit price, right now.
            // Nothing can change the book between this read and the ask below (one transaction).
            locals.bookIn.asset = locals.h.asset;
            locals.bookIn.limitPrice = locals.p.price;
            locals.bookIn.shares = locals.p.shares;
            CALL(QxBidBook, locals.bookIn, locals.bookOut);
            if (locals.bookOut.returnCode != QPOOL_OK)
            {
                output.returnCode = locals.bookOut.returnCode; return;
            }
            if (locals.bookOut.sold == 0 || locals.bookOut.net < locals.p.quAmount
                || locals.bookOut.net <= locals.costs)
            {
                output.returnCode = QPOOL_ERR_NO_FILL; return;
            }

            // ---- effects ----
            qpi.getEntity(SELF, locals.ent);
            locals.balBefore = locals.ent.incomingAmount - locals.ent.outgoingAmount;
            locals.gaBefore = state.get().globalUnattributed;
            locals.code = QPOOL_OK;
            locals.placed = 0;
            locals.rest = 0;
            locals.sold = 0;
            locals.cancelFailed = 0;

            if (locals.fromSelf > 0)
            {
                locals.rel = qpi.releaseShares(locals.h.asset, SELF, SELF, (sint64)locals.fromSelf,
                    QPOOL_QX_CONTRACT_INDEX, QPOOL_QX_CONTRACT_INDEX, QPOOL_QX_TRANSFER_FEE);
                if (locals.rel < 0) locals.code = QPOOL_ERR_TRANSFER_FAILED;
            }
            if (locals.code == QPOOL_OK && locals.fromQswap > 0)
            {
                locals.xferToQxIn.asset = locals.h.asset;
                locals.xferToQxIn.numberOfShares = (sint64)locals.fromQswap;
                locals.xferToQxIn.newManagingContractIndex = QPOOL_QX_CONTRACT_INDEX;
                INVOKE_OTHER_CONTRACT_PROCEDURE_E(QSWAP, TransferShareManagementRights,
                    locals.xferToQxIn, locals.xferToQxOut, QPOOL_QX_TRANSFER_FEE, xferQxErr);
                if (xferQxErr != NoCallError
                    || locals.xferToQxOut.transferredNumberOfShares < (sint64)locals.fromQswap)
                {
                    locals.code = QPOOL_ERR_TRANSFER_FAILED;
                }
            }
            if (locals.code == QPOOL_OK)
            {
                // The ask. QX takes the shares from QPOOL's QX-managed pile, matches it against
                // the resting bids at their own prices (best first, never below the limit) and
                // pays each fill to QPOOL during this call. No reward is attached: QX's ask
                // procedure charges nothing.
                locals.heldQxBefore = qpi.numberOfShares(locals.h.asset,
                    AssetOwnershipSelect{ SELF, QPOOL_QX_CONTRACT_INDEX, false, false },
                    AssetPossessionSelect{ SELF, QPOOL_QX_CONTRACT_INDEX, false, false });
                // If the sale reaches past the window of bids that was replayed, offer only what
                // that window takes, so the replay stays exact for everything the ask can reach.
                locals.askShares = (locals.bookOut.windowFull != 0) ? locals.bookOut.sold : locals.p.shares;
                locals.qxAskIn.issuer = locals.h.asset.issuer;
                locals.qxAskIn.assetName = locals.h.asset.assetName;
                locals.qxAskIn.price = (sint64)locals.p.price;
                locals.qxAskIn.numberOfShares = (sint64)locals.askShares;
                INVOKE_OTHER_CONTRACT_PROCEDURE_E(QX, AddToAskOrder,
                    locals.qxAskIn, locals.qxAskOut, 0, qxAskErr);
                // addedNumberOfShares is the whole order size, fills included; 0 means QX
                // rejected the order and nothing rests.
                if (qxAskErr != NoCallError || locals.qxAskOut.addedNumberOfShares <= 0)
                {
                    locals.code = QPOOL_ERR_EXTERNAL_CALL;
                }
                else
                {
                    locals.placed = 1;
                }
            }
            if (locals.placed != 0)
            {
                // Shares that left QPOOL's QX-managed pile. Not the measure of what sold (a fill against
                // QPOOL's own resting bid moves shares from QPOOL to QPOOL and leaves the pile as it
                // was), but evidence that bounds it, and the fallback should QX not answer below.
                locals.heldQxAfter = qpi.numberOfShares(locals.h.asset,
                    AssetOwnershipSelect{ SELF, QPOOL_QX_CONTRACT_INDEX, false, false },
                    AssetPossessionSelect{ SELF, QPOOL_QX_CONTRACT_INDEX, false, false });
                locals.soldByHeld = (locals.heldQxBefore > locals.heldQxAfter)
                    ? (uint64)(locals.heldQxBefore - locals.heldQxAfter) : 0;
                // How much of the ask did NOT fill, according to QX's own book.
                locals.restIn.asset = locals.h.asset;
                locals.restIn.price = locals.p.price;
                CALL(QxAskRest, locals.restIn, locals.restOut);
                if (locals.restOut.returnCode == QPOOL_OK)
                {
                    locals.rest = locals.restOut.rest;
                }
                else
                {
                    // QX did not answer (cannot happen right after it accepted the order).
                    locals.rest = (locals.soldByHeld < locals.askShares) ? locals.askShares - locals.soldByHeld : 0;
                }
                if (locals.rest > locals.askShares) locals.rest = locals.askShares;
                // Cancel the unfilled rest, exactly, so no ask of QPOOL rests on the book.
                if (locals.rest > 0)
                {
                    locals.qxRemIn.issuer = locals.h.asset.issuer;
                    locals.qxRemIn.assetName = locals.h.asset.assetName;
                    locals.qxRemIn.price = (sint64)locals.p.price;
                    locals.qxRemIn.numberOfShares = (sint64)locals.rest;
                    INVOKE_OTHER_CONTRACT_PROCEDURE_E(QX, RemoveFromAskOrder,
                        locals.qxRemIn, locals.qxRemOut, 0, qxRemErr);
                    if (qxRemErr != NoCallError
                        || locals.qxRemOut.removedNumberOfShares != (sint64)locals.rest)
                    {
                        locals.cancelFailed = 1;
                    }
                }
                locals.sold = locals.askShares - locals.rest;
                // Never book more shares as sold than the evidence allows: what left the pile, plus
                // what was filled against QPOOL's own bids (which the replay counted).
                if (locals.sold > sadd(locals.soldByHeld, locals.bookOut.ownTaken))
                {
                    locals.sold = sadd(locals.soldByHeld, locals.bookOut.ownTaken);
                }
            }

            // Every call is done: give back what the hook parked meanwhile (see UnparkArrivals).
            locals.unpIn.before = locals.gaBefore;
            CALL(UnparkArrivals, locals.unpIn, locals.unpOut);

            if (locals.sold == 0)
            {
                // Nothing sold and no ask rests. The pre-checks make this unreachable unless QX
                // changed its mind about an order it was shown. A management hand-over that
                // already happened only changed which contract manages the shares, but its fee
                // (QX's, at most 200 QU) has left the balance and belongs to no member: record
                // it against QPOOL's own revenue so that no holding's books are short of it.
                qpi.getEntity(SELF, locals.ent);
                locals.balAfter = locals.ent.incomingAmount - locals.ent.outgoingAmount;
                if (locals.balBefore > locals.balAfter)
                {
                    locals.fee = locals.balBefore - locals.balAfter;
                    state.mut().pendingRevenue = (state.get().pendingRevenue > locals.fee)
                        ? state.get().pendingRevenue - locals.fee : 0;
                }
                output.returnCode = (locals.code != QPOOL_OK) ? locals.code : QPOOL_ERR_EXTERNAL_CALL;
                return;
            }

            // Something sold: from here the sale is recorded whatever else happened.
            qpi.getEntity(SELF, locals.ent);
            locals.balAfter = locals.ent.incomingAmount - locals.ent.outgoingAmount;
            locals.received = (locals.balAfter > locals.balBefore) ? locals.balAfter - locals.balBefore : 0;
            // Weight change goes through the choke point: it first credits what the holding
            // earned on the shares it is about to lose, then shrinks the ledger's recognised
            // total, and retires the holding if nothing is left of it.
            locals.apIn.holdingId = locals.p.holdingId;
            locals.apIn.h = locals.h;
            locals.apIn.newShares = locals.h.shares - locals.sold;
            locals.apIn.newPending = locals.h.pendingShares;
            locals.apIn.markSettled = 0;
            locals.apIn.claimEscrow = 0;
            locals.apIn.retire = (locals.apIn.newShares == 0 && locals.h.pendingShares == 0) ? 1 : 0;
            CALL(ApplyHolding, locals.apIn, locals.apOut);
            locals.h = locals.apOut.h;
            locals.crIn.holdingId = locals.p.holdingId;
            locals.crIn.h = locals.h;
            locals.crIn.received = locals.received;
            CALL(CreditProceeds, locals.crIn, locals.crOut);
            locals.h = locals.crOut.h;
            output.proceeds = locals.crOut.net;
            output.sold = locals.sold;
            if (locals.cancelFailed != 0)
            {
                // QX did not take the cancel back (it just reported the rest itself, so this
                // cannot happen). The sale is recorded and the mandate is used up; the caller
                // is told that part of the ask may still rest on QX.
                locals.p.status = QPOOL_PROP_EXECUTED;
                state.mut().proposals.set(input.proposalId, locals.p);
                output.returnCode = QPOOL_ERR_EXTERNAL_CALL;
                return;
            }
        }
        // ---------------- ALLOCATE unattributed QU ----------------
        else
        {
            locals.h = state.get().holdings.get(locals.p.holdingId);
            if (locals.h.costBasis == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }
            locals.received = locals.p.quAmount;
            if (locals.received > locals.pool.unattributedQu)
            {
                locals.received = locals.pool.unattributedQu;
            }
            if (locals.received == 0) { output.returnCode = QPOOL_ERR_NOTHING_OWED; return; }
            locals.pool.unattributedQu = locals.pool.unattributedQu - locals.received;
            state.mut().pools.set(locals.p.poolId, locals.pool);

            locals.fee = div((uint128)locals.received * (uint128)QPOOL_MANAGEMENT_PERMILLE,
                (uint128)1000ULL).low;
            locals.received = locals.received - locals.fee;
            if (locals.fee > 0)
            {
                state.mut().pendingRevenue = sadd(state.get().pendingRevenue, locals.fee);
                state.mut().totalRevenue = sadd(state.get().totalRevenue, locals.fee);
                state.mut().totalManagementFees = sadd(state.get().totalManagementFees, locals.fee);
            }
            locals.scaled.high = locals.received;
            locals.scaled.low = 0;
            locals.h.payoutPerCostX64 = locals.h.payoutPerCostX64
                + div(locals.scaled, (uint128)locals.h.costBasis);
            locals.h.totalPayouts = sadd(locals.h.totalPayouts, locals.received);
            state.mut().holdings.set(locals.p.holdingId, locals.h);
            state.mut().totalPayoutsDistributed =
                sadd(state.get().totalPayoutsDistributed, locals.received);
            output.proceeds = locals.received;
        }

        locals.p.status = QPOOL_PROP_EXECUTED;
        state.mut().proposals.set(input.proposalId, locals.p);
        // An IPO bid that cost the contract more than it booked (see the spend check in the BUY_IPO branch): recorded
        // and used up like any other, but the caller is told the books are short.
        output.returnCode = (locals.overspent != 0) ? QPOOL_ERR_BOOKS_INCONSISTENT : QPOOL_OK;
    }

    // Refresh a QX-bid holding's share count against on-chain reality. A QX bid
    // fills asynchronously and silently (shares arrive without any call into this
    // contract), so its holding learns of the fill here. Permissionless.
    // IPO holdings settle through SettleIpo only; every other acquisition path
    // recognises its shares immediately.
    //
    // The ledger's recognised total (SourceLedger.recognized) is exactly the shares
    // already attributed to holdings, so the universe's total minus that total is
    // what is still unclaimed. A payer may have only ONE unreconciled QX bid at a
    // time (CheckSource), so unclaimed shares can only be that bid's fills; the
    // draw is still capped at this holding's own pendingShares, which makes repeated
    // calls idempotent and keeps stray donated shares from being over-claimed.
    // The dividends the hook set aside for those fills (escrow) are paid to this
    // holding in the same step.
    PUBLIC_PROCEDURE_WITH_LOCALS(ReconcileHolding)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.holdingId >= QPOOL_MAX_HOLDINGS) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.costBasis == 0 || locals.h.active == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }
        if (locals.h.ipoContractIndex != 0)
        {
            output.returnCode = QPOOL_ERR_WRONG_STATUS; return;
        }
        locals.kin.asset = locals.h.asset;
        locals.kin.ipoContractIndex = locals.h.ipoContractIndex;
        locals.kin.payerIndex = locals.h.payerIndex;
        CALL(SourceKeyOf, locals.kin, locals.kout);
        if (locals.kout.valid == 0 || !state.get().sources.get(locals.kout.key, locals.led))
        {
            output.returnCode = QPOOL_ERR_NOT_FOUND; return;
        }
        // In a CONTRACT ledger the fills of a QX bid are only told apart from won IPO shares
        // once every IPO holding has settled (CheckSource keeps the two from coexisting;
        // this keeps a hand-built state from mixing them).
        if (locals.kout.key.kind == QPOOL_SOURCE_CONTRACT && locals.led.unsettled != 0)
        {
            output.returnCode = QPOOL_ERR_SETTLEMENT_PENDING; return;
        }

        locals.held = qpi.numberOfShares(locals.h.asset,
            AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
        if (locals.held < 0) locals.held = 0;

        locals.unclaimed = ((uint64)locals.held > locals.led.recognized)
            ? (uint64)locals.held - locals.led.recognized : 0;
        locals.newClaim = (locals.unclaimed < locals.h.pendingShares) ? locals.unclaimed : locals.h.pendingShares;
        if (locals.newClaim > 0)
        {
            locals.apIn.holdingId = input.holdingId;
            locals.apIn.h = locals.h;
            locals.apIn.newShares = locals.h.shares + locals.newClaim;
            locals.apIn.newPending = locals.h.pendingShares - locals.newClaim;
            locals.apIn.markSettled = 0;
            locals.apIn.claimEscrow = 1;
            locals.apIn.retire = 0;
            CALL(ApplyHolding, locals.apIn, locals.apOut);
            locals.h = locals.apOut.h;
        }
        output.shares = locals.h.shares;
        output.pendingShares = locals.h.pendingShares;
        output.returnCode = QPOOL_OK;
    }

    // ======================== Payouts ========================

    // Claim this member's share of one holding's dividends and sale proceeds.
    //
    // A seat row that is worth nothing any more is removed here, so that the tables all pools share do not fill with dead rows (C013):
    //   - the member's cost is 0 and everything owed on it has just been paid (he sold his whole seat; the row stayed for the claim);
    //   - the holding is RETIRED (sold out: it no longer has shares, a ledger, an IPO bid or free funds, so nothing can ever accrue on a
    //     seat in it): whatever is owed is paid and the seat, worthless now, goes whatever its cost. A member who has nothing owed on a
    //     retired holding gets his row cleaned up (OK, paid 0) instead of NOTHING_OWED, because that call frees something.
    // The last row of a retired holding frees the holding's slot (the pool gets its place back, the slot can be taken again).
    // A member with nothing owed on a holding that is not retired gets NOTHING_OWED and the call writes NOTHING: what the claim would pay is worked
    // out first from a read-only preview of the sync (SyncPreview), and only a call that has something to do runs the real sync (audit C022, D23).
    PUBLIC_PROCEDURE_WITH_LOCALS(ClaimPayout)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.holdingId >= QPOOL_MAX_HOLDINGS) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.costBasis == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }
        locals.ok.member = qpi.invocator();
        locals.ok.holdingId = input.holdingId;
        if (!state.get().ownership.get(locals.ok, locals.own))
        {
            output.returnCode = QPOOL_ERR_NOT_MEMBER; return;
        }
        locals.retired = (locals.h.active == 0 && locals.h.ipoAvailable == 0 && locals.h.ipoAtRisk == 0) ? 1 : 0;
        // What would the claim pay? Worked out from the holding as it WILL be once it has pulled what it has earned from its source ledger
        // (read-only: SyncPreview writes nothing), so that a claim that finds nothing owed returns without having written anything.
        locals.pvIn.h = locals.h;
        CALL(SyncPreview, locals.pvIn, locals.pvOut);
        locals.pending = (uint128)locals.own.cost
            * (locals.pvOut.h.payoutPerCostX64 - locals.own.payoutDebtX64);
        locals.owed = sadd(locals.own.claimable, locals.pending.high);
        if (locals.owed == 0 && locals.retired == 0 && locals.own.cost != 0)
        {
            output.returnCode = QPOOL_ERR_NOTHING_OWED; return;
        }
        // The call has something to do. Pull whatever the holding has earned from its source ledger since it
        // last looked. SyncHolding persists the holding and the ledger together (and nothing at all if there is nothing new).
        locals.syIn.holdingId = input.holdingId;
        locals.syIn.h = locals.h;
        CALL(SyncHolding, locals.syIn, locals.syOut);
        locals.h = locals.syOut.h;
        locals.pending = (uint128)locals.own.cost
            * (locals.h.payoutPerCostX64 - locals.own.payoutDebtX64);
        locals.owed = sadd(locals.own.claimable, locals.pending.high);

        if (locals.owed != 0)
        {
            // Effects before the transfer.
            locals.own.payoutDebtX64 = locals.h.payoutPerCostX64;
            locals.own.claimable = 0;
            state.mut().ownership.set(locals.ok, locals.own);

            if (qpi.transfer(qpi.invocator(), (sint64)locals.owed) < 0)
            {
                // The contract could not pay. Cannot happen while the books balance,
                // but a failed payment must never erase the claim: put it back.
                locals.own.claimable = locals.owed;
                state.mut().ownership.set(locals.ok, locals.own);
                output.returnCode = QPOOL_ERR_TRANSFER_FAILED; return;
            }
            output.paid = locals.owed;
        }

        // Paid (or nothing owed): a seat that is worth nothing now goes.
        if (locals.retired != 0 || locals.own.cost == 0)
        {
            state.mut().ownership.removeByKey(locals.ok);
            locals.fk.seller = qpi.invocator();
            locals.fk.holdingId = input.holdingId;
            if (state.get().offers.contains(locals.fk))
            {
                state.mut().offers.removeByKey(locals.fk);
                locals.offerDropped = 1;
            }
            locals.pool = state.get().pools.get(locals.h.poolId);
            locals.pool.ownershipRows = (locals.pool.ownershipRows > 0) ? locals.pool.ownershipRows - 1 : 0;
            locals.pool.offerCount = (locals.pool.offerCount >= locals.offerDropped) ? locals.pool.offerCount - locals.offerDropped : 0;
            locals.h.rows = (locals.h.rows > 0) ? locals.h.rows - 1 : 0;
            if (locals.retired != 0 && locals.h.rows == 0)
            {
                // The last seat of a retired holding is gone: the slot is free (every stale field of it is blanked), the pool has its place back,
                // and what the holding cost is no longer committed capital of the pool (it was spent: the sale's proceeds have been claimed).
                locals.pool.holdingCount = (locals.pool.holdingCount > 0) ? locals.pool.holdingCount - 1 : 0;
                if (locals.h.deposited != 0) locals.pool.depositCount = (locals.pool.depositCount > 0) ? locals.pool.depositCount - 1 : 0;
                else locals.pool.committedTotal = (locals.pool.committedTotal > locals.h.costBasis) ? locals.pool.committedTotal - locals.h.costBasis : 0;
                if (input.holdingId < state.get().holdingFreeHint) state.mut().holdingFreeHint = input.holdingId;
                state.mut().holdings.set(input.holdingId, locals.blank);
            }
            else
            {
                state.mut().holdings.set(input.holdingId, locals.h);
            }
            state.mut().pools.set(locals.h.poolId, locals.pool);
        }
        output.returnCode = QPOOL_OK;
    }

    // A further bid for an unsettled IPO holding, by a seat owner, bounded by MONEY and nothing else: the price is any the free funds pay
    // for (there is no ceiling), the quantity is whatever the free funds buy at it (there is no voted quantity), any number of times, no
    // cooldown. See "IPO BIDDING AND REBIDDING" and "THE MONEY RULE" at the top. The order of the work is the order of the cost:
    //   1. input ranges, the holding, the caller's seat, the registry (a price of ANOTHER holding of this IPO is taken; one of this holding's
    //      own prices is reused), the target's row, the IPO window and the book's minimum (two reads of the target), and an upper bound of the
    //      money the holding could have after a reconcile (free funds plus what the pot could hand back of the money not certainly still in the
    //      book): every refusal that needs nothing but a few lookups, and costs the caller nothing;
    //   2. ONE pass over the 676 slots of the book (IpoSeatsNow): the slots QPOOL still holds at each of the holding's prices. Whatever the
    //      holding bid and no longer holds was refunded to the pot BEFORE this call; its price per lost slot moves from at-risk to free funds,
    //      clamped by the pot (the pot is debited by the same amount). The refusals that need the pass (the money does not cover the seats, no
    //      room in the list for a new price, the contract's physical balance) still write nothing;
    //   3. the bid, then every write: the pot (relative to what it holds NOW: the core refunds the evicted slots, including QPOOL's own,
    //      into the pot from inside the bid), the registry (dead prices out, a new price in), the list, the holding.
    // THE MONEY RULE: the cost of the bid is paid from the free funds the pass found, and from nothing else. The refunds of the seats this very
    // bid pushes out (the holding's own lowest seats, or units the book does not seat) are not predicted and not counted: the core pays them
    // only after the bid, into the hook, and the next reconcile turns them into free funds that fund the next rebid.
    PUBLIC_PROCEDURE_WITH_LOCALS(RebidIpo)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        // ---- 1. refusals that need only lookups ----
        if (input.holdingId >= QPOOL_MAX_HOLDINGS || input.price == 0 || input.price > QPOOL_IPO_MAX_PRICE
            || input.quantity > QPOOL_IPO_MAX_QUANTITY)
        {
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.active == 0 || locals.h.ipoContractIndex == 0 || locals.h.ipoSettled != 0)
        {
            output.returnCode = QPOOL_ERR_IPO_NOT_ACTIVE; return;
        }
        // Only a seat owner: the people with money at stake act for the holding, a stranger cannot grief it.
        locals.ok.member = qpi.invocator();
        locals.ok.holdingId = input.holdingId;
        if (!state.get().ownership.get(locals.ok, locals.own) || locals.own.cost == 0)
        {
            output.returnCode = QPOOL_ERR_NOT_MEMBER; return;
        }
        if (!state.get().ipoBids.get(input.holdingId, locals.bids))
        {
            output.returnCode = QPOOL_ERR_BOOKS_INCONSISTENT; return;      // every unsettled IPO holding has its list
        }
        // The registry: a price any OTHER holding of this IPO uses is taken (its slots would be indistinguishable from this holding's). A
        // price this holding has itself is its own: the bid adds seats there and the list takes no new entry.
        locals.key.ipoContractIndex = locals.h.ipoContractIndex;
        locals.key.price = input.price;
        locals.reuse = 0;
        if (state.get().ipoPrices.get(locals.key, locals.owner))
        {
            if (locals.owner != input.holdingId)
            {
                output.returnCode = QPOOL_ERR_IPO_PRICE_TAKEN; return;
            }
            locals.reuse = 1;
        }
        locals.n = (locals.bids.count < QPOOL_MAX_IPO_BIDS) ? locals.bids.count : QPOOL_MAX_IPO_BIDS;
        locals.rowIn.index = locals.h.ipoContractIndex;
        CALL(IpoRowOf, locals.rowIn, locals.rowOut);
        if (locals.rowOut.found == 0)
        {
            output.returnCode = QPOOL_ERR_BOOKS_INCONSISTENT; return;
        }
        locals.row = state.get().ipoTargets.get(locals.rowOut.row);
        if (locals.row.snapshotEpoch != 0)
        {
            output.returnCode = QPOOL_ERR_IPO_REJECTED; return;            // the auction has been read for the last time
        }
        // The auction must be running (outside the target's IPO epoch bidInIPO returns -1 and the book cannot be read) ...
        if (qpi.ipoBidPrice((uint32)locals.h.ipoContractIndex, 0) < 0)
        {
            output.returnCode = QPOOL_ERR_IPO_REJECTED; return;
        }
        // ... and a bid must be able to win a seat: on a full book that is a price above the lowest slot (a bid at or below it is
        // refunded in full by the core and only registers a price).
        locals.minPrice = qpi.ipoBidPrice((uint32)locals.h.ipoContractIndex, (uint32)NUMBER_OF_COMPUTORS - 1);
        if (locals.minPrice > 0 && input.price <= (uint64)locals.minPrice)
        {
            output.returnCode = QPOOL_ERR_PRICE_TOO_LOW; return;
        }
        // The most the holding could have to spend after a reconcile, found without the pass. Money that is at risk may have come back, and
        // the pot may hold it; but the seats the list shows at a price ABOVE today's book minimum cannot have been lost: the book only ever
        // evicts its LOWEST slot and its minimum never falls, so a slot at a price above the minimum has never been the lowest, and the list is
        // exact for it (the pass or the bid that wrote it counted them, and a bid evicts nothing above the minimum it leaves behind). What those
        // seats are worth at their own prices is therefore still at risk. The list is believed for those prices only (seats at the minimum or
        // below may be gone, and only the pass can say), and only if it does not claim more than the holding has at risk. (On a book with free
        // slots the minimum is 0 and nothing was ever evicted.) A rebid that this bound cannot pay for is refused from one list, not from 676 reads.
        locals.floorPrice = (locals.minPrice > 0) ? (uint64)locals.minPrice : 0;
        locals.sureValue = 0;
        locals.listed = 0;
        for (locals.k = 0; locals.k < locals.n; locals.k++)
        {
            if (locals.bids.prices.get(locals.k) == input.price) locals.listed = 1;
            if (locals.bids.prices.get(locals.k) > locals.floorPrice)
            {
                locals.sureValue = sadd(locals.sureValue, smul(locals.bids.seatsAt.get(locals.k), locals.bids.prices.get(locals.k)));
            }
        }
        if (locals.reuse != locals.listed)
        {
            output.returnCode = QPOOL_ERR_BOOKS_INCONSISTENT; return;      // the registry and the holding's list agree on its prices (Execute and this call keep them so)
        }
        locals.atRisk = (locals.sureValue <= locals.h.ipoAtRisk) ? locals.h.ipoAtRisk - locals.sureValue : locals.h.ipoAtRisk;
        locals.maxFunds = sadd(locals.h.ipoAvailable, (locals.atRisk < state.get().ipoRefundEscrow) ? locals.atRisk : state.get().ipoRefundEscrow);
        if (smul(input.price, (input.quantity != 0) ? input.quantity : 1) > locals.maxFunds)
        {
            output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;    // not even the seats asked for (one, by default) could be paid for
        }
        if (locals.reuse == 0 && state.get().ipoPrices.population() >= QPOOL_MAX_IPO_PRICES)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }

        // ---- 2. the reconcile: one pass over the book ----
        locals.snIn.ipoContractIndex = locals.h.ipoContractIndex;
        locals.snIn.bids = locals.bids;
        CALL(IpoSeatsNow, locals.snIn, locals.snOut);
        locals.shouldBe = locals.snOut.value;               // what the slots the holding still holds are worth at their own prices
        if (locals.shouldBe > locals.h.ipoAtRisk)
        {
            output.returnCode = QPOOL_ERR_BOOKS_INCONSISTENT; return;      // more is seated than was ever put in: never invent money
        }
        // What has come back: the price of every seat lost BEFORE this call, whose refund the hook has pooled, up to what the pot holds.
        locals.freed = locals.h.ipoAtRisk - locals.shouldBe;
        locals.drawn = (locals.freed < state.get().ipoRefundEscrow) ? locals.freed : state.get().ipoRefundEscrow;
        locals.avail = sadd(locals.h.ipoAvailable, locals.drawn);      // the free funds at the start of the call: the only money this bid may spend
        // How many seats: what was asked for, or (0) as many as the free funds buy at this price, at most the core's 676.
        locals.quantity = input.quantity;
        if (locals.quantity == 0)
        {
            locals.quantity = div(locals.avail, input.price);
            if (locals.quantity > QPOOL_IPO_MAX_QUANTITY) locals.quantity = QPOOL_IPO_MAX_QUANTITY;
        }
        locals.cost = smul(input.price, locals.quantity);              // (price and quantity are inside the core's limits: no wrap, no saturation)
        if (locals.quantity == 0 || locals.cost > locals.avail)
        {
            output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;    // the money has not come back (yet), or the call asks for more than it has
        }
        if (locals.reuse == 0 && locals.snOut.live >= QPOOL_MAX_IPO_BIDS)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;                // the list is full of live prices and this one would be a further one
        }
        // The core deducts price x quantity from the contract's PHYSICAL balance first and does nothing when the balance cannot pay it. The
        // free funds are physically in the contract (the refunds that made them free have been paid into it), so a balance below the cost
        // means the books and the balance disagree: refused before anything moves, with the books untouched.
        qpi.getEntity(SELF, locals.ent);
        locals.balBefore = locals.ent.incomingAmount - locals.ent.outgoingAmount;
        if (locals.cost > locals.balBefore)
        {
            output.returnCode = QPOOL_ERR_BOOKS_INCONSISTENT; return;
        }

        // ---- 3. the bid, then the books ----
        locals.ipoResult = qpi.bidInIPO((uint32)locals.h.ipoContractIndex, (sint64)input.price, (uint32)locals.quantity);
        if (locals.ipoResult < 0)
        {
            output.returnCode = QPOOL_ERR_IPO_REJECTED; return;           // nothing was taken, nothing is written
        }
        // The full cost leaves the contract, and whatever is not seated comes straight back as a refund (also from QPOOL's own evicted
        // slots). Defence in depth (audit C001): the net loss of the contract can never exceed the cost; if it does the books are short
        // and the call says so after recording the bid like any other.
        qpi.getEntity(SELF, locals.ent);
        locals.balAfter = locals.ent.incomingAmount - locals.ent.outgoingAmount;
        locals.overspent = 0;
        if (locals.balBefore > locals.balAfter && locals.balBefore - locals.balAfter > locals.cost)
        {
            locals.overspent = (locals.balBefore - locals.balAfter) - locals.cost;
        }
        // The pot, relative to what it holds now (the hook has added the refunds of this very bid, which this call does not count).
        if (locals.drawn > 0)
        {
            state.mut().ipoRefundEscrow = (state.get().ipoRefundEscrow > locals.drawn) ? state.get().ipoRefundEscrow - locals.drawn : 0;
        }
        // The list: the prices that kept a slot (a reused price takes the slots this bid seated), then a new price if the bid seated a slot
        // at one. The others leave the registry. The counts are those of the pass, taken before the bid: the holding's own seats this bid
        // pushed out are still in them until the next reconcile.
        locals.nb.count = 0;
        locals.nb.seats = 0;
        for (locals.k = 0; locals.k < locals.n; locals.k++)
        {
            locals.added = (locals.reuse != 0 && locals.bids.prices.get(locals.k) == input.price) ? (uint64)locals.ipoResult : 0;
            if (locals.snOut.seatsNow.get(locals.k) == 0 && locals.added == 0)
            {
                locals.key.price = locals.bids.prices.get(locals.k);
                state.mut().ipoPrices.removeByKey(locals.key);
                continue;
            }
            locals.nb.prices.set(locals.nb.count, locals.bids.prices.get(locals.k));
            locals.nb.seatsAt.set(locals.nb.count, locals.snOut.seatsNow.get(locals.k) + locals.added);
            locals.nb.seats = locals.nb.seats + locals.snOut.seatsNow.get(locals.k) + locals.added;
            locals.nb.count = locals.nb.count + 1;
        }
        if (locals.reuse == 0 && locals.ipoResult > 0)
        {
            locals.key.price = input.price;
            locals.nb.prices.set(locals.nb.count, input.price);
            locals.nb.seatsAt.set(locals.nb.count, (uint64)locals.ipoResult);
            locals.nb.seats = locals.nb.seats + (uint64)locals.ipoResult;
            locals.nb.count = locals.nb.count + 1;
            state.mut().ipoPrices.set(locals.key, input.holdingId);
        }
        state.mut().ipoBids.set(input.holdingId, locals.nb);
        // The holding: at-risk + free funds stay equal to costBasis. The seat count is a number for the eye, written like every pendingShares,
        // through ApplyHolding.
        locals.atRisk = (locals.h.ipoAtRisk - locals.drawn) + locals.cost;
        locals.h.ipoAtRisk = locals.atRisk;
        locals.h.ipoAvailable = locals.avail - locals.cost;
        if (locals.ipoResult > 0 && input.price > locals.h.ipoMaxPrice) locals.h.ipoMaxPrice = input.price;
        locals.apIn.holdingId = input.holdingId;
        locals.apIn.h = locals.h;
        locals.apIn.newShares = locals.h.shares;
        locals.apIn.newPending = locals.nb.seats;
        locals.apIn.markSettled = 0;
        locals.apIn.claimEscrow = 0;
        locals.apIn.retire = 0;
        CALL(ApplyHolding, locals.apIn, locals.apOut);
        locals.h = locals.apOut.h;

        output.bidQuantity = locals.quantity;
        output.bidsRegistered = (uint64)locals.ipoResult;
        output.seatsBefore = locals.snOut.seats;
        output.freed = locals.drawn;
        output.atRisk = locals.h.ipoAtRisk;
        output.available = locals.h.ipoAvailable;
        output.livePrices = locals.nb.count;
        output.returnCode = (locals.overspent != 0) ? QPOOL_ERR_BOOKS_INCONSISTENT : QPOOL_OK;
    }

    // Close out an IPO position once the auction has resolved. Permissionless.
    //
    // WHEN. Only when the target's snapshot exists (END_EPOCH took it at the end of the IPO epoch) and the epoch has moved on: positive
    // evidence that the auction is over, never "the book cannot be read" (the core also answers -2 in every epoch that is not the IPO
    // epoch). Before that: WINDOW_OPEN (IPO_NO_SNAPSHOT if the epoch has moved on but END_EPOCH left no snapshot). Nobody can end
    // a running auction for a pool (audit C020, C110).
    //
    // WHAT. The holding's result is read from the snapshot under (target, price) for every price in its list and added up: `shares` seats won
    // (0 when it won nothing or the IPO failed) and what they `spent` (seats x the clearing price: every seat pays the same, whatever it bid).
    // Everything it bid and did not spend came back to QPOOL as ipoBidRefund (evicted slots at once, the premium over the clearing price at
    // the end, the whole bid of a failed IPO), and the budget that was never bid and the funds a rebid reconcile moved back are
    // still here, so
    //     members are handed back  ipoAvailable + (ipoAtRisk - spent)         (the second part clamped to what the refund pot holds)
    //     costBasis becomes        what the seats cost                         (shares x finalPrice)
    //     shares recognised        exactly the seats won
    // for this holding alone: the order in which holdings settle, a stale count of seats, the refund pot and the number of shares
    // QPOOL holds in total play no part in what a holding gets (audit C004, C005, C038).
    //
    // THE NAME. The vote declared the share asset's name, which nothing could check then. A holding that won seats gets them only if
    // the universe proves the name: the ledger of the target is not bound to another name, the pairing is possible (ContractPairKnown:
    // the embedded table, and no other ledger owns the name) and QPOOL holds at least recognised + seats shares of it. Otherwise
    // NAME_UNPROVEN: nothing is written and the holding stays as it is, with its capital on the books (audit C037). Such a holding
    // keeps the ledger of its target unsettled, which parks that ledger's dividends; see README.
    //
    // THE REFUND is split over the seat owners by their cost units with a running total, so the shares add up to the amount exactly and
    // nobody is more than a unit from his exact pro-rata share (audit C068).
    //
    // A holding that has shares is never retired, and the refund never takes the last unit of its cost.
    PUBLIC_PROCEDURE_WITH_LOCALS(SettleIpo)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.holdingId >= QPOOL_MAX_HOLDINGS) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.h = state.get().holdings.get(input.holdingId);
        // ipoSettled makes this single-shot: a second call used to recompute
        // shares from a ledger that already counted them and zero the holding.
        if (locals.h.ipoContractIndex == 0 || locals.h.costBasis == 0 || locals.h.ipoSettled != 0)
        {
            output.returnCode = QPOOL_ERR_IPO_NOT_ACTIVE; return;
        }

        // ---- 1. Is the auction over? (positive evidence only; everything up to the first write is read-only) ----
        locals.rowIn.index = locals.h.ipoContractIndex;
        CALL(IpoRowOf, locals.rowIn, locals.rowOut);
        if (locals.rowOut.found == 0)
        {
            // every unsettled IPO holding is counted in the row of its target (Execute): the books are wrong
            output.returnCode = QPOOL_ERR_BOOKS_INCONSISTENT; return;
        }
        locals.rowIdx = locals.rowOut.row;
        locals.row = state.get().ipoTargets.get(locals.rowIdx);
        if (locals.row.snapshotEpoch == 0)
        {
            output.returnCode = ((uint64)qpi.epoch() > locals.row.bidEpoch) ? QPOOL_ERR_IPO_NO_SNAPSHOT : QPOOL_ERR_WINDOW_OPEN;
            return;
        }
        if ((uint64)qpi.epoch() <= locals.row.snapshotEpoch)
        {
            output.returnCode = QPOOL_ERR_WINDOW_OPEN; return;
        }

        // ---- 2. What the holding won and what it is owed, from the snapshot ----
        // The rows of all the prices in the holding's list (a price whose last slot was lost has no row): seats won, what they cost at the clearing
        // price, and what the slots were worth at their own prices (which the holding must have put into the book).
        if (!state.get().ipoBids.get(input.holdingId, locals.bids))
        {
            output.returnCode = QPOOL_ERR_BOOKS_INCONSISTENT; return;      // every unsettled IPO holding has its list (Execute)
        }
        locals.ipoKey.ipoContractIndex = locals.h.ipoContractIndex;
        locals.seatsWon = 0;
        locals.spentOnSeats = 0;
        locals.valueOfSeats = 0;
        for (locals.j = 0; locals.j < locals.bids.count && locals.j < QPOOL_MAX_IPO_BIDS; locals.j++)
        {
            locals.ipoKey.price = locals.bids.prices.get(locals.j);
            if (!state.get().ipoSeats.get(locals.ipoKey, locals.seat)) continue;
            locals.seatsWon = sadd(locals.seatsWon, locals.seat.shares);
            locals.spentOnSeats = sadd(locals.spentOnSeats, locals.seat.spent);
            locals.valueOfSeats = sadd(locals.valueOfSeats, smul(locals.seat.shares, locals.ipoKey.price));
        }
        locals.seat.shares = locals.seatsWon;
        locals.seat.spent = locals.spentOnSeats;
        if (locals.spentOnSeats > locals.h.ipoAtRisk || locals.valueOfSeats > locals.h.ipoAtRisk || locals.seatsWon > QPOOL_IPO_MAX_QUANTITY)
        {
            // it cannot have won more seats than a book has, nor can its seats have cost (or been worth) more than it put into the book
            output.returnCode = QPOOL_ERR_BOOKS_INCONSISTENT; return;
        }
        locals.fromIpo = locals.h.ipoAtRisk - locals.spentOnSeats;
        locals.draw = (locals.fromIpo < state.get().ipoRefundEscrow) ? locals.fromIpo : state.get().ipoRefundEscrow;
        locals.leftover = sadd(locals.h.ipoAvailable, locals.draw);

        // ---- 3. The shares: proven by the universe, or not settled at all ----
        locals.kin.asset = locals.h.asset;
        locals.kin.ipoContractIndex = locals.h.ipoContractIndex;
        locals.kin.payerIndex = locals.h.payerIndex;
        CALL(SourceKeyOf, locals.kin, locals.kout);
        if (locals.kout.valid == 0) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        // A missing ledger (impossible for a registered holding) must never strand the members' refund, so it is read as
        // "nothing recognised yet".
        locals.led.recognized = 0;
        locals.led.assetName = 0;
        state.get().sources.get(locals.kout.key, locals.led);
        locals.myShare = 0;
        if (locals.seat.shares > 0)
        {
            locals.held = qpi.numberOfShares(locals.h.asset,
                AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
            if (locals.held < 0) locals.held = 0;
            // The shares of the declared name that the ledger has not counted yet must cover the seats: this is a ceiling on
            // what the universe proves, not what the holding gets (that is the snapshot's number, whatever the others did).
            locals.proven = ((uint64)locals.held >= sadd(locals.led.recognized, locals.seat.shares)) ? 1 : 0;
            if (locals.led.assetName != 0)
            {
                // the target's ledger is bound to a name already: a holding that declared another one cannot be proven
                if (locals.led.assetName != locals.h.asset.assetName) locals.proven = 0;
            }
            else if (locals.proven != 0)
            {
                // This settlement would BIND the target to the declared name. Shares of an asset are counted from the whole
                // universe, so the name must not belong to another contract: refuse a name the table lists for another index or
                // that another ledger has already claimed.
                locals.pairIn.index = locals.kout.contractIndex;
                locals.pairIn.assetName = locals.h.asset.assetName;
                CALL(ContractPairKnown, locals.pairIn, locals.pairOut);
                if (locals.pairOut.returnCode != QPOOL_OK) locals.proven = 0;
            }
            if (locals.proven == 0)
            {
                output.returnCode = QPOOL_ERR_NAME_UNPROVEN; return;
            }
            locals.myShare = locals.seat.shares;
        }
        // A holding that has shares keeps at least one unit of cost, whatever the books say.
        if (locals.myShare > 0 && locals.leftover >= locals.h.costBasis) locals.leftover = locals.h.costBasis - 1;

        // ---- 4. Everything is decided; from here on nothing refuses. Hand the money back. ----
        locals.returned = 0;
        if (locals.leftover > 0)
        {
            // Hand back pro-rata and shrink each member's cost by the same
            // fraction, so the ownership denominator keeps matching real spend.
            locals.pool = state.get().pools.get(locals.h.poolId);
            locals.base = locals.h.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL;
            locals.prefix = 0;
            locals.prevCum = 0;
            for (locals.i = 0; locals.i < locals.pool.contributorCount; locals.i++)
            {
                locals.ok.member = state.get().members.get(locals.base + locals.i);
                locals.ok.holdingId = input.holdingId;
                if (!state.get().ownership.get(locals.ok, locals.own)) continue;
                if (locals.own.cost == 0) continue;
                // Each member is refunded once: only the roster slot his own record names counts, so an id listed twice
                // (it cannot be since EnsureMember) would not be refunded on the cost that the first refund already shrank.
                // A roster member without a record (impossible since EnsureMember) is given one at his own position.
                locals.ck.member = locals.ok.member;
                locals.ck.poolId = locals.h.poolId;
                locals.c.uncommitted = 0; locals.c.committed = 0; locals.c.lockedForVotes = 0;
                locals.c.lockStartTick = 0; locals.c.lockExpiryTick = 0; locals.c.lastDepositTick = 0;
                locals.c.memberIdx = locals.i;
                if (state.get().contributions.get(locals.ck, locals.c) && locals.c.memberIdx != locals.i) continue;
                // Split `leftover` by the RUNNING total of the cost units, rounded down: refund(i) = floor(P(i) x leftover / costBasis)
                // - floor(P(i-1) x leftover / costBasis), P(i) the cost units of the members up to and including i. The refunds add
                // up to `leftover` exactly when the seats add up to costBasis (nothing is stranded), and each is within one unit of
                // the exact pro-rata amount and never above the member's cost.
                locals.prefix = locals.prefix + locals.own.cost;
                locals.cum = div((uint128)locals.prefix * (uint128)locals.leftover, (uint128)locals.h.costBasis).low;
                locals.refundEach = (locals.cum > locals.prevCum) ? locals.cum - locals.prevCum : 0;
                locals.prevCum = locals.cum;
                if (locals.refundEach > locals.own.cost) locals.refundEach = locals.own.cost;
                // Never hand back more than is left to hand back: the refunds of unique members add up to
                // at most `leftover`, and this keeps it so whatever the cost units say.
                if (locals.refundEach > locals.leftover - locals.returned) locals.refundEach = locals.leftover - locals.returned;
                if (locals.refundEach == 0) continue;

                // Crystallise what this seat has accrued BEFORE its cost shrinks, or
                // the shrink would re-price an already credited payout.
                locals.pending = (uint128)locals.own.cost
                    * (locals.h.payoutPerCostX64 - locals.own.payoutDebtX64);
                locals.own.claimable = sadd(locals.own.claimable, locals.pending.high);
                locals.own.payoutDebtX64 = locals.h.payoutPerCostX64;
                locals.own.cost = locals.own.cost - locals.refundEach;
                if (locals.own.cost == 0 && locals.own.claimable == 0)
                {
                    // Nothing left of the seat and nothing owed on it: the row (and his offer on it, if he made one) goes.
                    state.mut().ownership.removeByKey(locals.ok);
                    locals.dropped = locals.dropped + 1;
                    locals.fk.seller = locals.ok.member;
                    locals.fk.holdingId = input.holdingId;
                    if (state.get().offers.contains(locals.fk))
                    {
                        state.mut().offers.removeByKey(locals.fk);
                        locals.offersDropped = locals.offersDropped + 1;
                    }
                }
                else
                {
                    state.mut().ownership.set(locals.ok, locals.own);
                }

                locals.c.uncommitted = sadd(locals.c.uncommitted, locals.refundEach);
                locals.c.lastDepositTick = qpi.tick();        // capital handed back is new money to the pool's open votes (see Vote)
                locals.c.committed = (locals.c.committed > locals.refundEach)
                    ? locals.c.committed - locals.refundEach : 0;
                state.mut().contributions.set(locals.ck, locals.c);
                locals.returned = sadd(locals.returned, locals.refundEach);
            }
            locals.pool.uncommittedTotal = sadd(locals.pool.uncommittedTotal, locals.returned);
            locals.pool.committedTotal = (locals.pool.committedTotal > locals.returned)
                ? locals.pool.committedTotal - locals.returned : 0;
            state.mut().pools.set(locals.h.poolId, locals.pool);

            locals.h.costBasis = (locals.h.costBasis > locals.returned)
                ? locals.h.costBasis - locals.returned : 0;
        }
        // What came out of the refund pot: the part of the refund that is not the budget that was never bid (which was never in the
        // pot). Normally all of `draw`; less only if the books could not hand all of it out.
        locals.potUse = (locals.returned > locals.h.ipoAvailable) ? locals.returned - locals.h.ipoAvailable : 0;
        if (locals.potUse > locals.draw) locals.potUse = locals.draw;
        if (locals.potUse > 0) state.mut().ipoRefundEscrow = state.get().ipoRefundEscrow - locals.potUse;
        locals.h.ipoAvailable = (locals.leftover > locals.returned) ? locals.leftover - locals.returned : 0;
        locals.h.ipoAtRisk = 0;
        locals.h.pendingShares = 0;

        // Fully refunded and never paid: the slot is about to be reusable, so clear
        // every seat and offer of this holding id. A stale record would otherwise be
        // inherited by whatever holding takes the slot next.
        if (locals.h.costBasis == 0 && locals.h.totalPayouts == 0)
        {
            locals.pool = state.get().pools.get(locals.h.poolId);
            locals.base = locals.h.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL;
            for (locals.i = 0; locals.i < locals.pool.contributorCount; locals.i++)
            {
                locals.ok.member = state.get().members.get(locals.base + locals.i);
                locals.ok.holdingId = input.holdingId;
                if (state.get().ownership.contains(locals.ok))
                {
                    state.mut().ownership.removeByKey(locals.ok);
                    locals.dropped = locals.dropped + 1;
                }
                locals.fk.seller = locals.ok.member;
                locals.fk.holdingId = input.holdingId;
                if (state.get().offers.contains(locals.fk))
                {
                    state.mut().offers.removeByKey(locals.fk);
                    locals.offersDropped = locals.offersDropped + 1;
                }
            }
            // The slot is free again: the pool gets its place back and the search for a free slot can start at it.
            locals.freed = 1;
        }

        // The pool's books: the quotas this holding used are given back (a fresh read, the pool was written above).
        locals.pool = state.get().pools.get(locals.h.poolId);
        locals.pool.ownershipRows = (locals.pool.ownershipRows > locals.dropped) ? locals.pool.ownershipRows - locals.dropped : 0;
        locals.pool.offerCount = (locals.pool.offerCount > locals.offersDropped) ? locals.pool.offerCount - locals.offersDropped : 0;
        locals.pool.ipoHoldings = (locals.pool.ipoHoldings > 0) ? locals.pool.ipoHoldings - 1 : 0;
        if (locals.freed != 0)
        {
            locals.pool.holdingCount = (locals.pool.holdingCount > 0) ? locals.pool.holdingCount - 1 : 0;
            if (input.holdingId < state.get().holdingFreeHint) state.mut().holdingFreeHint = input.holdingId;
        }
        state.mut().pools.set(locals.h.poolId, locals.pool);
        locals.h.rows = (locals.h.rows > locals.dropped) ? (uint32)(locals.h.rows - locals.dropped) : 0;
        // A slot that is free again is no holding: it carries no incarnation, so no mandate (which names a holding by its incarnation) can be
        // about it, whatever else the record still shows.
        if (locals.freed != 0) locals.h.incarnation = 0;

        // The holding is settled from here on: its bid prices are free again (ipoPrices lists the prices of UNSETTLED IPO holdings only), its
        // snapshot rows and its list are spent, and it no longer counts in its target's row (the last one frees the row).
        for (locals.j = 0; locals.j < locals.bids.count && locals.j < QPOOL_MAX_IPO_BIDS; locals.j++)
        {
            locals.ipoKey.price = locals.bids.prices.get(locals.j);
            state.mut().ipoPrices.removeByKey(locals.ipoKey);
            state.mut().ipoSeats.removeByKey(locals.ipoKey);
        }
        state.mut().ipoBids.removeByKey(input.holdingId);
        locals.row.unsettled = (locals.row.unsettled > 0) ? locals.row.unsettled - 1 : 0;
        if (locals.row.unsettled == 0)
        {
            locals.row.ipoContractIndex = 0;
            locals.row.bidEpoch = 0;
            locals.row.snapshotEpoch = 0;
            locals.row.finalPrice = 0;
        }
        state.mut().ipoTargets.set(locals.rowIdx, locals.row);

        // Hand the result to the choke point: it records the shares as the holding's
        // weight, marks it settled (binding the payer's asset name and releasing any
        // parked dividends), retires the holding if it won nothing, and persists it.
        locals.apIn.holdingId = input.holdingId;
        locals.apIn.h = locals.h;
        locals.apIn.newShares = locals.myShare;
        locals.apIn.newPending = 0;
        locals.apIn.claimEscrow = 0;
        locals.apIn.markSettled = 1;
        locals.apIn.retire = (locals.myShare == 0) ? 1 : 0;
        CALL(ApplyHolding, locals.apIn, locals.apOut);
        locals.h = locals.apOut.h;

        output.shares = locals.h.shares;
        output.returnedToMembers = locals.returned;
        output.returnCode = QPOOL_OK;
    }

    // ======================== Syndication ========================

    // Deposit an asset you already own into your pool and receive 100% of the
    // resulting holding's seats. You may then sell any fraction of those seats
    // for QU via OfferPosition, and buyers share every future dividend on the
    // asset pro-rata -- without the pool ever having to buy anything.
    //
    // ONLY THE POOL'S CREATOR MAY DEPOSIT (owner decision D16, final), in a public and in a private pool alike; anybody else is refused
    // with NOT_AUTHORIZED, everything attached refunded, before anything is read or written. A deposit binds the payer's dividend ledger to
    // its asset (one payer, one asset), takes a holding slot and a seat row, and could be made for the price of one dust share: who
    // deposits into a pool is therefore the pool's owner, who paid QPOOL_CREATE_POOL_FEE for it, and not whoever finds a pool open (audit
    // C007, C031). Somebody who wants to fractionalise his own shares creates his own pool. The creator needs no Contribution of QU and
    // pays no join fee to deposit: he is put on the roster (a roster slot and a zero record, see EnsureMember) as the owner of every
    // seat, so that he can vote and be counted by Execute and SettleIpo. A pool may hold QPOOL_MAX_DEPOSITS_PER_POOL deposits at a
    // time, so that deposits can never fill the slots acquisitions need; a deposit is counted until its holding is freed.
    //
    // The shares must already sit under this contract's management (transfer
    // them with QX TransferShareManagementRights first), same as QTREAT and
    // IEscrow's general-asset deposits. If a deposit is refused after that, take them back with ReleaseManagedShares.
    //
    // `seatUnits` is the ownership denominator you are carving the holding into.
    // It is a free choice of granularity, not a valuation: pick 1,000,000 and
    // you can sell seats in millionths. No QU is committed, so this holding is
    // deliberately kept out of pool.committedTotal.
    //
    // CONTRACT SHARES (asset.issuer == NULL_ID, e.g. shares won in a contract's IPO,
    // which sit under QX's management until the owner hands them to QPOOL the same way)
    // are deposited like any other asset, with `contractIndex` set to the index of the
    // contract that pays dividends on them (0 for every other asset). The pair (index, name) is checked before the shares
    // move; a deposit also waits (and refunds) while an IPO holding of the same contract is unsettled.
    //
    // Whatever is attached to the call is refunded in full on every path (there is no fee).
    PUBLIC_PROCEDURE_WITH_LOCALS(DepositHoldingAsset)
    {
        if (input.poolId >= QPOOL_MAX_POOLS || input.amount == 0 || input.seatUnits == 0
            || input.asset.assetName == 0
            || (input.asset.issuer != NULL_ID && input.contractIndex != 0))
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.pool = state.get().pools.get(input.poolId);
        if (locals.pool.active == 0)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_NOT_FOUND; return;
        }
        // The creator, and nobody else (D16): checked before any other read or write.
        if (qpi.invocator() != locals.pool.creator)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_NOT_AUTHORIZED; return;
        }
        // The pool's own quotas: deposits (so acquisitions keep slots), holding slots, seat rows. Before anything moves.
        if (locals.pool.depositCount >= QPOOL_MAX_DEPOSITS_PER_POOL
            || locals.pool.holdingCount >= QPOOL_MAX_HOLDINGS_PER_POOL
            || locals.pool.ownershipRows >= QPOOL_MAX_OWNERSHIP_PER_POOL
            || state.get().ownership.population() >= QPOOL_MAX_OWNERSHIP)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        // Membership: the owner of every seat must be on the roster (and have a record), so a creator who is not there yet needs room for
        // himself (no fee) -- refused BEFORE the shares move if the roster is full.
        locals.jcIn.poolId = input.poolId;
        locals.jcIn.member = qpi.invocator();
        CALL(JoinCheck, locals.jcIn, locals.jcOut);
        if (locals.jcOut.full != 0)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        for (locals.newHoldingId = state.get().holdingFreeHint; locals.newHoldingId < QPOOL_MAX_HOLDINGS; locals.newHoldingId++)
        {
            if (state.get().holdings.get(locals.newHoldingId).costBasis == 0
                && state.get().holdings.get(locals.newHoldingId).active == 0) break;
        }
        if (locals.newHoldingId >= QPOOL_MAX_HOLDINGS)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        // The payer's ledger must have room and agree on the asset, BEFORE the shares move.
        locals.srcIn.asset = input.asset;
        locals.srcIn.ipoContractIndex = 0;
        locals.srcIn.payerIndex = input.contractIndex;
        locals.srcIn.qxBid = 0;
        CALL(CheckSource, locals.srcIn, locals.srcOut);
        if (locals.srcOut.returnCode != QPOOL_OK)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = locals.srcOut.returnCode; return;
        }

        // The depositor must genuinely hold the shares under SC management.
        locals.managed = qpi.numberOfShares(input.asset,
            AssetOwnershipSelect::byOwner(qpi.invocator()),
            AssetPossessionSelect::byPossessor(qpi.invocator()));
        if (locals.managed < (sint64)input.amount)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
        }
        locals.xfer = qpi.transferShareOwnershipAndPossession(
            input.asset.assetName, input.asset.issuer,
            qpi.invocator(), qpi.invocator(), (sint64)input.amount, SELF);
        if (locals.xfer < 0)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_TRANSFER_FAILED; return;
        }

        // From here on nothing can be refused (room was checked above): join the roster first, then record the holding.
        if (locals.jcOut.isNew != 0)
        {
            locals.emIn.poolId = input.poolId;
            locals.emIn.member = qpi.invocator();
            locals.emIn.fee = 0;
            CALL(EnsureMember, locals.emIn, locals.emOut);
            locals.pool = locals.emOut.pool;
        }

        locals.h.poolId = input.poolId;
        locals.h.asset = input.asset;
        locals.h.shares = input.amount;
        locals.h.costBasis = input.seatUnits;
        locals.h.payoutPerCostX64 = uint128(0, 0);
        locals.h.totalPayouts = 0;
        locals.h.deposited = 1;
        locals.h.ipoContractIndex = 0;
        locals.h.payerIndex = input.contractIndex;
        locals.h.ipoMaxPrice = 0;
        locals.h.ipoAtRisk = 0;
        locals.h.ipoAvailable = 0;
        locals.h.pendingShares = 0;
        locals.h.active = 1;
        locals.h.ipoSettled = 0;
        locals.h.srcSnapX64 = uint128(0, 0);
        locals.h.rows = 1;
        locals.serial = state.get().holdingSerial + 1;           // a new holding is a new incarnation of its slot (see Holding::incarnation)
        locals.h.incarnation = (uint32)locals.serial;
        locals.regIn.h = locals.h;
        CALL(RegisterHolding, locals.regIn, locals.regOut);
        locals.h = locals.regOut.h;
        state.mut().holdings.set(locals.newHoldingId, locals.h);
        state.mut().holdingSerial = locals.serial;
        if (locals.newHoldingId >= state.get().holdingCount)
        {
            state.mut().holdingCount = locals.newHoldingId + 1;
        }
        state.mut().holdingFreeHint = locals.newHoldingId + 1;

        // Depositor starts owning every seat.
        locals.ok.member = qpi.invocator();
        locals.ok.holdingId = locals.newHoldingId;
        locals.own.cost = input.seatUnits;
        locals.own.voteLockUntilTick = 0;
        locals.own.payoutDebtX64 = uint128(0, 0);
        locals.own.claimable = 0;
        if (state.mut().ownership.set(locals.ok, locals.own) == NULL_INDEX)
        {
            // Cannot happen: the map had room (checked above) and nothing added to it since. Should it ever
            // happen, what was attached goes back, exactly once.
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }

        locals.pool.holdingCount = locals.pool.holdingCount + 1;
        locals.pool.depositCount = locals.pool.depositCount + 1;
        locals.pool.ownershipRows = locals.pool.ownershipRows + 1;
        state.mut().pools.set(input.poolId, locals.pool);

        // Interactions last: whatever was attached goes back (a deposit costs nothing but the transfer fee of the venue that moved the shares).
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        output.holdingId = locals.newHoldingId;
        output.returnCode = QPOOL_OK;
    }

    // Take back shares you own that this contract manages. The caller attaches the fee the destination asks for (QX: its transfer fee), the
    // rest of what he attached is given back. Nothing in the contract's books is read or written: the shares are the CALLER's (owner and
    // possessor are both him), so no pool's holding (owned by this contract) can ever be moved by it, and a caller who owns none is
    // refused by a read. See ReleaseManagedShares_input.
    PUBLIC_PROCEDURE_WITH_LOCALS(ReleaseManagedShares)
    {
        if (input.amount == 0 || input.amount > (uint64)MAX_AMOUNT || input.asset.assetName == 0
            || input.destinationIndex == 0 || input.destinationIndex == (uint64)SELF_INDEX
            || input.destinationIndex > QPOOL_MAX_CONTRACT_INDEX)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        // The caller's own shares of the asset that are under THIS contract's management (a select on the owner AND the manager).
        locals.managed = qpi.numberOfShares(input.asset,
            AssetOwnershipSelect{ qpi.invocator(), (uint16)SELF_INDEX, false, false },
            AssetPossessionSelect{ qpi.invocator(), (uint16)SELF_INDEX, false, false });
        if (locals.managed < (sint64)input.amount)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
        }
        locals.rel = qpi.releaseShares(input.asset, qpi.invocator(), qpi.invocator(), (sint64)input.amount,
            (uint16)input.destinationIndex, (uint16)input.destinationIndex, qpi.invocationReward());
        if (locals.rel < 0)
        {
            // Nothing moved (a fee that was asked for and not offered is reported back, a refusal of the destination is not a fee).
            if (locals.rel >= -(sint64)MAX_AMOUNT) output.requiredFee = (uint64)(-locals.rel);
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_TRANSFER_FAILED; return;
        }
        // The destination took `rel` out of what was attached (it was paid from this contract's balance, which received the attachment).
        if (qpi.invocationReward() > locals.rel) qpi.transfer(qpi.invocator(), qpi.invocationReward() - locals.rel);
        output.released = input.amount;
        output.feePaid = (uint64)locals.rel;
        output.returnCode = QPOOL_OK;
    }

    // Evict a squat. Permissionless. A payer's ledger is bound to ONE asset name ("one payer, one asset": a transfer does not say which
    // asset it is for), so whoever binds a ledger first decides what its payer's other assets can do in this contract: a deposit of one
    // dust share of T2 makes every deposit or purchase of T1 of the same issuer fail with SOURCE_CONFLICT (audit C031). Pricing the binding is
    // the first defence: only a pool's creator can deposit (D16), and a pool costs QPOOL_CREATE_POOL_FEE. This is the second: a binding
    // that has never been used can be given up when somebody else needs it. ALL of the following must hold, each an O(1) lookup:
    //   - the holding is a DEPOSIT (nobody bought it with the pool's money; no member's capital is in it) and is alone in its ledger;
    //   - the ledger has never taken in a dividend (accrued 0), has no escrow and no QX bid, and the holding has never been paid or sold;
    //   - its depositor, the pool's creator, still owns every seat (nobody paid for a seat: nobody else has a stake in it) and has no
    //     other seat row in it;
    //   - the binding is contested, which is a fact the contract can check, in one of two ways:
    //       TOKEN ledger   the caller owns a share of ANOTHER asset of the same issuer (rivalName, issued and held by the caller) and the
    //                      binding is at least QPOOL_SQUAT_EPOCHS epochs old, so a depositor of an asset that pays rarely is not
    //                      pushed out the week after he deposits it, and anybody who needs the binding must wait the same
    //                      time he did;
    //       CONTRACT ledger the contract the ledger is for is in its IPO epoch NOW: its shares cannot exist before the IPO ends, so a
    //                      ledger that holds shares of it, bound by a deposit made before the IPO started, is wrong beyond doubt (a contract
    //                      that has not started its IPO is "registered, not in its IPO epoch" to the check made at deposit).
    // An honest early depositor therefore loses his place only to somebody who really needs it and only after it stood unused, and he
    // loses nothing else: his shares go back to him in full (owned by him, still managed by this contract: take them with
    // ReleaseManagedShares) before any book is touched, the holding is freed and the ledger is gone. If the return fails nothing is written.
    // Not covered: a binding made by a PURCHASE (BUY_QX of a dust share by a pool of one member): the shares sit under the venue's management, which
    // this contract cannot take back, so only a sale by the pool releases it (README 4d, 10).
    PUBLIC_PROCEDURE_WITH_LOCALS(EvictSquat)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.holdingId >= QPOOL_MAX_HOLDINGS) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.active == 0 || locals.h.deposited == 0 || locals.h.ipoContractIndex != 0 || locals.h.shares == 0)
        {
            output.returnCode = QPOOL_ERR_NOT_FOUND; return;
        }
        locals.kin.asset = locals.h.asset;
        locals.kin.ipoContractIndex = locals.h.ipoContractIndex;
        locals.kin.payerIndex = locals.h.payerIndex;
        CALL(SourceKeyOf, locals.kin, locals.kout);
        if (locals.kout.valid == 0 || !state.get().sources.get(locals.kout.key, locals.led))
        {
            output.returnCode = QPOOL_ERR_NOT_FOUND; return;
        }
        // alone in its ledger, never paid, nothing in flight
        if (locals.led.holdings != 1 || locals.led.accruedX64 != uint128(0, 0) || locals.led.escrow != 0 || locals.led.pendingQx != 0
            || locals.led.unsettled != 0 || locals.h.totalPayouts != 0 || locals.h.rows != 1)
        {
            output.returnCode = QPOOL_ERR_WRONG_STATUS; return;
        }
        // its depositor still owns every seat
        locals.pool = state.get().pools.get(locals.h.poolId);
        locals.ok.member = locals.pool.creator;
        locals.ok.holdingId = input.holdingId;
        if (!state.get().ownership.get(locals.ok, locals.own) || locals.own.cost != locals.h.costBasis)
        {
            output.returnCode = QPOOL_ERR_WRONG_STATUS; return;
        }
        // the binding is contested
        if (locals.kout.key.kind == QPOOL_SOURCE_TOKEN)
        {
            if (input.rivalName == 0 || input.rivalName == locals.led.assetName
                || (uint64)qpi.epoch() < locals.led.bindEpoch + QPOOL_SQUAT_EPOCHS)
            {
                output.returnCode = QPOOL_ERR_WRONG_STATUS; return;
            }
            locals.rival.issuer = locals.kout.key.source;
            locals.rival.assetName = input.rivalName;
            if (qpi.numberOfShares(locals.rival, AssetOwnershipSelect::byOwner(qpi.invocator()),
                    AssetPossessionSelect::byPossessor(qpi.invocator())) <= 0)
            {
                output.returnCode = QPOOL_ERR_WRONG_STATUS; return;
            }
        }
        else if (qpi.ipoBidPrice((uint32)locals.kout.contractIndex, 0) < 0)
        {
            output.returnCode = QPOOL_ERR_WRONG_STATUS; return;
        }

        // Interaction first, the one step that can fail: the shares go back to the depositor, who is the only owner of the holding.
        locals.xfer = qpi.transferShareOwnershipAndPossession(locals.h.asset.assetName, locals.h.asset.issuer,
            SELF, SELF, (sint64)locals.h.shares, locals.pool.creator);
        if (locals.xfer < 0) { output.returnCode = QPOOL_ERR_TRANSFER_FAILED; return; }

        // The books: the holding leaves its ledger (the ledger is gone with it), its only seat row and offer go, the pool has its place back.
        locals.apIn.holdingId = input.holdingId;
        locals.apIn.h = locals.h;
        locals.apIn.newShares = 0;
        locals.apIn.newPending = 0;
        locals.apIn.markSettled = 0;
        locals.apIn.claimEscrow = 0;
        locals.apIn.retire = 1;
        CALL(ApplyHolding, locals.apIn, locals.apOut);
        state.mut().ownership.removeByKey(locals.ok);
        locals.fk.seller = locals.pool.creator;
        locals.fk.holdingId = input.holdingId;
        if (state.get().offers.contains(locals.fk))
        {
            state.mut().offers.removeByKey(locals.fk);
            locals.offerDropped = 1;
        }
        locals.pool = state.get().pools.get(locals.h.poolId);
        locals.pool.ownershipRows = (locals.pool.ownershipRows > 0) ? locals.pool.ownershipRows - 1 : 0;
        locals.pool.offerCount = (locals.pool.offerCount >= locals.offerDropped) ? locals.pool.offerCount - locals.offerDropped : 0;
        locals.pool.holdingCount = (locals.pool.holdingCount > 0) ? locals.pool.holdingCount - 1 : 0;
        locals.pool.depositCount = (locals.pool.depositCount > 0) ? locals.pool.depositCount - 1 : 0;
        state.mut().pools.set(locals.h.poolId, locals.pool);
        if (input.holdingId < state.get().holdingFreeHint) state.mut().holdingFreeHint = input.holdingId;
        state.mut().holdings.set(input.holdingId, locals.blank);
        output.sharesReturned = locals.h.shares;
        output.returnCode = QPOOL_OK;
    }

    // ======================== Position market (buy-outs) ========================

    // Offer part or all of a position for sale. Not vote-gated: a member's
    // position is their own property, and gating exit behind a vote would let a
    // majority trap them.
    //
    // Only a seat that is worth something can be offered (audit C100, C055):
    //   * a RETIRED holding (sold out, or an IPO that won nothing: active 0) has kept its cost units only so that the claims on what it earned
    //     can be paid, and nothing can ever accrue on them again -- a buyer would pay real QU for a seat that never earns;
    //   * the seats of an IPO holding that has NOT been settled yet are in limbo: SettleIpo hands part of the budget back and shrinks every
    //     seat's cost units pro rata, so an offer written in cost units before it would sell a larger fraction of the holding afterwards, and a
    //     buyer would pay for a parcel nobody can price. They can be offered once SettleIpo has run (SETTLEMENT_PENDING until then).
    // Offering the very offer the seller has already writes nothing.
    PUBLIC_PROCEDURE_WITH_LOCALS(OfferPosition)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.holdingId >= QPOOL_MAX_HOLDINGS || input.costUnits == 0 || input.askPrice == 0)
        {
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.costBasis == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }
        if (locals.h.active == 0) { output.returnCode = QPOOL_ERR_WRONG_STATUS; return; }
        if (locals.h.ipoContractIndex != 0 && locals.h.ipoSettled == 0) { output.returnCode = QPOOL_ERR_SETTLEMENT_PENDING; return; }
        locals.ok.member = qpi.invocator();
        locals.ok.holdingId = input.holdingId;
        if (!state.get().ownership.get(locals.ok, locals.own) || locals.own.cost < input.costUnits)
        {
            output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
        }
        // A seat that has voted on a SELL or ALLOCATE proposal stays with its owner until the vote can no longer be
        // cast, or its units could be sold on and vote again (audit C003). Refused before anything is written.
        if ((uint64)qpi.tick() <= locals.own.voteLockUntilTick)
        {
            output.returnCode = QPOOL_ERR_SEAT_LOCKED; return;
        }
        locals.fk.seller = qpi.invocator();
        locals.fk.holdingId = input.holdingId;
        // A NEW offer counts against the pool's own quota (C013): a pool has QPOOL_MAX_OFFERS_PER_POOL offers at a time, so the table all
        // pools share can never be filled by one. Changing an offer that is there already takes no new place.
        locals.pool = state.get().pools.get(locals.h.poolId);
        locals.isNew = 1;
        if (state.get().offers.get(locals.fk, locals.f))
        {
            locals.isNew = 0;
            if (locals.f.costUnits == input.costUnits && locals.f.askPrice == input.askPrice)
            {
                output.returnCode = QPOOL_OK; return;            // the offer is already exactly this: nothing to write
            }
        }
        if (locals.isNew != 0
            && (locals.pool.offerCount >= QPOOL_MAX_OFFERS_PER_POOL || state.get().offers.population() >= QPOOL_MAX_OFFERS))
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        locals.f.costUnits = input.costUnits;
        locals.f.askPrice = input.askPrice;
        locals.f.poolId = locals.h.poolId;
        if (state.mut().offers.set(locals.fk, locals.f) == NULL_INDEX)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        if (locals.isNew != 0)
        {
            locals.pool.offerCount = locals.pool.offerCount + 1;
            state.mut().pools.set(locals.h.poolId, locals.pool);
        }
        output.returnCode = QPOOL_OK;
    }

    PUBLIC_PROCEDURE_WITH_LOCALS(CancelOffer)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        locals.fk.seller = qpi.invocator();
        locals.fk.holdingId = input.holdingId;
        if (!state.get().offers.get(locals.fk, locals.f)) { output.returnCode = QPOOL_ERR_NO_OFFER; return; }
        state.mut().offers.removeByKey(locals.fk);
        // The place the offer took in its pool's quota is free again (the pool is named by the offer itself, not read from the holding).
        locals.pool = state.get().pools.get(locals.f.poolId);
        locals.pool.offerCount = (locals.pool.offerCount > 0) ? locals.pool.offerCount - 1 : 0;
        state.mut().pools.set(locals.f.poolId, locals.pool);
        output.returnCode = QPOOL_OK;
    }

    // Fill an offer: QU goes to the seller, cost units transfer to the buyer.
    // The asset never moves, so the pool's holdings are unchanged — only the
    // seat changes hands. A buyer who was not previously a member joins (roster
    // slot and Contribution record, see EnsureMember) and pays QPOOL_JOIN_FEE out
    // of the QU attached BEYOND the ask price; attach at least ask + fee.
    //
    // THE BUYER NAMES THE PARCEL (audit C033). The seller can replace his offer at any moment, so the input carries expectedCostUnits, the
    // cost units of the offer the buyer read; the call is refused with OFFER_CHANGED unless it is exactly the live offer's. The QU attached
    // is the buyer's cap on the PRICE (the ask is taken from it, the rest comes back), so together the two bind price and parcel.
    //
    // EVERY REFUSAL COMES BEFORE THE FIRST WRITE, and refunds everything (D23: a refused call must not make the node re-hash the whole state for
    // nothing). The order of the checks is the order of their cost: the input, the offer, the parcel, the price, the holding (it must still be a
    // live one: audit C100, C055), the escrow of its ledger, admission and room on the roster, the fee, the seat row the buyer would need, the seller's
    // seat (still there, not vote-locked); only then does the sync of the holding, and with it the first write, begin.
    PUBLIC_PROCEDURE_WITH_LOCALS(BuyPosition)
    {
        if (input.holdingId >= QPOOL_MAX_HOLDINGS || input.seller == NULL_ID
            || input.seller == qpi.invocator() || input.expectedCostUnits == 0)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.fk.seller = input.seller;
        locals.fk.holdingId = input.holdingId;
        if (!state.get().offers.get(locals.fk, locals.f))
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_NO_OFFER; return;
        }
        if (locals.f.costUnits != input.expectedCostUnits)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_OFFER_CHANGED; return;
        }
        if ((uint64)qpi.invocationReward() < locals.f.askPrice)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_PRICE_TOO_HIGH; return;
        }
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.costBasis == 0)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_NOT_FOUND; return;
        }
        // A seat of a retired holding earns nothing ever again, and the seats of an IPO holding that is not settled have no price yet (see
        // OfferPosition): an offer that was made before the holding came to that state (or that a hand-built state holds) is not filled. The seller
        // can still cancel it.
        if (locals.h.active == 0)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_WRONG_STATUS; return;
        }
        if (locals.h.ipoContractIndex != 0 && locals.h.ipoSettled == 0)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_SETTLEMENT_PENDING; return;
        }
        // Every refusal that needs only the offer, the holding, its pool and the attachment comes BEFORE
        // SyncHolding: SyncHolding writes the state (a whole-state re-hash for the owner) whenever the
        // holding's source has accrued something new, and a refused call must not do that.
        //
        // Dividends parked in escrow are owed to the CURRENT seat owners. Letting a seat change hands
        // before they are released would hand them to the buyer. A plain read (HoldingEscrow): it is the
        // same figure SyncHolding reports, because nothing is written between here and the sync.
        locals.escIn.asset = locals.h.asset;
        locals.escIn.ipoContractIndex = locals.h.ipoContractIndex;
        locals.escIn.payerIndex = locals.h.payerIndex;
        CALL(HoldingEscrow, locals.escIn, locals.escOut);
        if (locals.escOut.escrow != 0)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_SETTLEMENT_PENDING; return;
        }
        // Without the admission check, buying a seat would be a back door into a permissioned
        // pool: the buyer would gain ownership and voting weight having never
        // been admitted. A member of the pool is admitted whatever the allow-list says, and so is its creator (owner decision D7), so
        // the check concerns only a buyer who is not on the roster yet (JoinCheck's isNew): switching a pool to private never blocks a seat
        // trade between its members.
        locals.pool = state.get().pools.get(locals.h.poolId);
        locals.jcIn.poolId = locals.h.poolId;
        locals.jcIn.member = qpi.invocator();
        CALL(JoinCheck, locals.jcIn, locals.jcOut);
        if (locals.pool.permissioned != 0 && locals.jcOut.isNew != 0 && qpi.invocator() != locals.pool.creator)
        {
            locals.admitKey.member = qpi.invocator();
            locals.admitKey.poolId = locals.h.poolId;
            if (!state.get().admitted.contains(locals.admitKey))
            {
                qpi.transfer(qpi.invocator(), qpi.invocationReward());
                output.returnCode = QPOOL_ERR_NOT_ADMITTED; return;
            }
        }
        // Every seat owner must be on the pool roster WITH a Contribution record: SettleIpo and
        // Execute walk the roster, so an owner they cannot see would keep a full cost while costBasis
        // shrinks and sum(cost) would exceed costBasis. A buyer who is new to the pool joins (below),
        // so refuse before anything moves if there is no room for him, and charge him the join fee on
        // top of the ask price: the QU attached beyond the ask must cover it.
        if (locals.jcOut.full != 0)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        locals.joinFee = (locals.jcOut.isNew != 0) ? QPOOL_JOIN_FEE : 0;
        if ((uint64)qpi.invocationReward() < locals.f.askPrice + locals.joinFee)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_INSUFFICIENT_FEE; return;
        }
        // The seat row the buyer would need, if he has none yet, must fit in the pool's own row budget and in the table all pools share. A plain
        // read, made HERE and not after SyncHolding: anybody can attach the ask price (it comes back) and aim this call at a pool whose budget
        // is used up, and a refusal that has already written the state would re-hash the whole of it for nothing (D23).
        locals.buyerKey.member = qpi.invocator();
        locals.buyerKey.holdingId = input.holdingId;
        locals.buyerExisted = state.get().ownership.contains(locals.buyerKey) ? 1 : 0;
        if (locals.buyerExisted == 0
            && (locals.pool.ownershipRows >= QPOOL_MAX_OWNERSHIP_PER_POOL || state.get().ownership.population() >= QPOOL_MAX_OWNERSHIP))
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        // The seller's seat, read before anything is written (SyncHolding writes): it may no longer hold what it offered
        // (the stale offer is dropped), and it may have VOTED since (see Ownership::voteLockUntilTick): a seat that has
        // voted on a SELL or ALLOCATE proposal cannot change hands until that vote can no longer be cast, or its units
        // would vote a second time under their new owner (audit C003). A locked seat's offer is left in place: it can be
        // filled once the lock has lifted, or cancelled.
        locals.sellerKey.member = input.seller;
        locals.sellerKey.holdingId = input.holdingId;
        if (!state.get().ownership.get(locals.sellerKey, locals.sellerOwn)
            || locals.sellerOwn.cost < locals.f.costUnits)
        {
            // Seller no longer holds what they offered; drop the stale offer (and give its place back to the pool's quota).
            state.mut().offers.removeByKey(locals.fk);
            locals.pool.offerCount = (locals.pool.offerCount > 0) ? locals.pool.offerCount - 1 : 0;
            state.mut().pools.set(locals.h.poolId, locals.pool);
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_NO_OFFER; return;
        }
        if ((uint64)qpi.tick() <= locals.sellerOwn.voteLockUntilTick)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_SEAT_LOCKED; return;
        }
        // Pull the holding up to date BEFORE reading its accumulator, so both sides
        // crystallise against everything earned so far.
        locals.syIn.holdingId = input.holdingId;
        locals.syIn.h = locals.h;
        CALL(SyncHolding, locals.syIn, locals.syOut);
        locals.h = locals.syOut.h;

        locals.buyerOwn.cost = 0;
        locals.buyerOwn.payoutDebtX64 = locals.h.payoutPerCostX64;
        locals.buyerOwn.claimable = 0;
        locals.buyerOwn.voteLockUntilTick = 0;
        locals.buyerExisted = state.get().ownership.get(locals.buyerKey, locals.buyerOwn) ? 1 : 0;      // (his row, if he has one: SyncHolding writes no seat row)

        // Crystallise BOTH sides against the current accumulator before moving
        // cost units, so the transfer cannot shift payouts already earned.
        locals.pending = (uint128)locals.sellerOwn.cost
            * (locals.h.payoutPerCostX64 - locals.sellerOwn.payoutDebtX64);
        locals.sellerOwn.claimable = sadd(locals.sellerOwn.claimable, locals.pending.high);
        locals.sellerOwn.payoutDebtX64 = locals.h.payoutPerCostX64;

        if (locals.buyerExisted != 0)
        {
            locals.pending = (uint128)locals.buyerOwn.cost
                * (locals.h.payoutPerCostX64 - locals.buyerOwn.payoutDebtX64);
            locals.buyerOwn.claimable = sadd(locals.buyerOwn.claimable, locals.pending.high);
        }
        locals.buyerOwn.payoutDebtX64 = locals.h.payoutPerCostX64;

        locals.sellerOwn.cost = locals.sellerOwn.cost - locals.f.costUnits;
        locals.buyerOwn.cost = sadd(locals.buyerOwn.cost, locals.f.costUnits);

        // Effects, then the payment to the seller.
        if (state.mut().ownership.set(locals.buyerKey, locals.buyerOwn) == NULL_INDEX)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        // A seller who has nothing left of the seat and nothing owed on it has no row any more.
        if (locals.sellerOwn.cost == 0 && locals.sellerOwn.claimable == 0)
        {
            state.mut().ownership.removeByKey(locals.sellerKey);
            locals.sellerDropped = 1;
        }
        else
        {
            state.mut().ownership.set(locals.sellerKey, locals.sellerOwn);
        }
        state.mut().offers.removeByKey(locals.fk);

        // A buyer new to this pool becomes a member (roster slot and Contribution record together), so
        // he can propose and vote, and pays the join fee. Room was checked above; nothing can refuse now.
        if (locals.jcOut.isNew != 0)
        {
            locals.emIn.poolId = locals.h.poolId;
            locals.emIn.member = qpi.invocator();
            locals.emIn.fee = locals.joinFee;
            CALL(EnsureMember, locals.emIn, locals.emOut);
        }
        // The pool's books and the holding's: the offer is gone, a new buyer has a row, a seller who sold everything has none. (A fresh
        // read of the pool: EnsureMember wrote it.)
        locals.pool = state.get().pools.get(locals.h.poolId);
        locals.pool.offerCount = (locals.pool.offerCount > 0) ? locals.pool.offerCount - 1 : 0;
        locals.pool.ownershipRows = locals.pool.ownershipRows + (1 - locals.buyerExisted);
        locals.pool.ownershipRows = (locals.pool.ownershipRows > locals.sellerDropped) ? locals.pool.ownershipRows - locals.sellerDropped : 0;
        state.mut().pools.set(locals.h.poolId, locals.pool);
        if (locals.buyerExisted == 0 || locals.sellerDropped != 0)
        {
            locals.h.rows = (uint32)(locals.h.rows + (1 - locals.buyerExisted));
            locals.h.rows = (locals.h.rows > locals.sellerDropped) ? (uint32)(locals.h.rows - locals.sellerDropped) : 0;
            state.mut().holdings.set(input.holdingId, locals.h);
        }

        if ((uint64)qpi.invocationReward() > locals.f.askPrice + locals.joinFee)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward() - (sint64)(locals.f.askPrice + locals.joinFee));
        }
        // Management fee off the ask price, same rate and bucket as a
        // dividend or a sale back to the market -- a seat changing hands is
        // revenue-generating activity too. Taken from the seller's proceeds,
        // never added on top of what the buyer pays.
        locals.fee = div((uint128)locals.f.askPrice * (uint128)QPOOL_MANAGEMENT_PERMILLE,
            (uint128)1000ULL).low;
        if (locals.fee > 0)
        {
            state.mut().pendingRevenue = sadd(state.get().pendingRevenue, locals.fee);
            state.mut().totalRevenue = sadd(state.get().totalRevenue, locals.fee);
            state.mut().totalManagementFees = sadd(state.get().totalManagementFees, locals.fee);
        }
        qpi.transfer(input.seller, (sint64)(locals.f.askPrice - locals.fee));

        output.costUnitsBought = locals.f.costUnits;
        output.returnCode = QPOOL_OK;
    }

    // ======================== Registration ========================

    REGISTER_USER_FUNCTIONS_AND_PROCEDURES()
    {
        REGISTER_USER_FUNCTION(GetPool, 1);
        REGISTER_USER_FUNCTION(GetMember, 2);
        REGISTER_USER_FUNCTION(GetHolding, 3);
        REGISTER_USER_FUNCTION(GetPosition, 4);
        REGISTER_USER_FUNCTION(GetProposal, 5);
        REGISTER_USER_FUNCTION(GetStats, 6);
        REGISTER_USER_FUNCTION(GetSource, 7);
        REGISTER_USER_FUNCTION(GetIpoTarget, 8);
        REGISTER_USER_FUNCTION(GetIpoBids, 9);

        REGISTER_USER_PROCEDURE(CreatePool, 1);
        REGISTER_USER_PROCEDURE(Contribute, 2);
        REGISTER_USER_PROCEDURE(WithdrawUncommitted, 3);
        REGISTER_USER_PROCEDURE(Propose, 4);
        REGISTER_USER_PROCEDURE(Vote, 5);
        REGISTER_USER_PROCEDURE(Tally, 6);
        REGISTER_USER_PROCEDURE(Execute, 7);
        REGISTER_USER_PROCEDURE(ReconcileHolding, 8);
        REGISTER_USER_PROCEDURE(ClaimPayout, 9);
        REGISTER_USER_PROCEDURE(OfferPosition, 10);
        REGISTER_USER_PROCEDURE(CancelOffer, 11);
        REGISTER_USER_PROCEDURE(BuyPosition, 12);
        REGISTER_USER_PROCEDURE(RebidIpo, 13);
        REGISTER_USER_PROCEDURE(SettleIpo, 14);
        // 15 was ReconcileIpo (deleted, see the note above SettleIpo's structs): the id stays reserved, nothing is registered under it.
        REGISTER_USER_PROCEDURE(DepositHoldingAsset, 16);
        REGISTER_USER_PROCEDURE(SetAdmission, 17);
        REGISTER_USER_PROCEDURE(SetPermissioned, 18);
        REGISTER_USER_PROCEDURE(ReleaseManagedShares, 19);
        REGISTER_USER_PROCEDURE(CloseEmptyPool, 20);
        REGISTER_USER_PROCEDURE(EvictSquat, 21);
    }

    INITIALIZE()
    {
        // Nothing to seed: there is no admin, and every pool is created by a
        // user. Deliberately so — no key in this contract can move member funds.
    }

    // Route incoming QU. The hook sees only {sourceId, amount, type}: it cannot see
    // which asset a payment is for or which pool owns it.
    //
    //   qpiDistributeDividends from id(idx,0,0,0) -> the SourceKey (that id, CONTRACT)
    //   any other type from a non-zero sender      -> the SourceKey (sender, TOKEN)
    //
    // A payment that matches no ledger is parked in globalUnattributed, as before. That includes
    // what QX and QSWAP pay QPOOL in the middle of one of its own calls (sale proceeds, a refunded
    // fee, the refund of a BUY): a sale in Execute credits those from its balance difference and
    // hands the counter back afterwards (UnparkArrivals), and a BUY takes the refund back out of it
    // (BuyRefund / TakeBackRefund), so only genuinely unattributed money stays in it.
    // One that matches a ledger is split by the ledger's RECOGNISED shares: the
    // per-share accumulator advances and each holding pulls its slice lazily, so the
    // cost does not depend on how many holdings share the payer.
    //
    // WHAT IS SET ASIDE (escrow) instead of credited:
    //   - an IPO source with a holding that has not been settled yet: nobody knows
    //     yet how many of the seats each pool won, so the whole net waits and is
    //     released per recognised share when the last holding settles;
    //   - a source (TOKEN or CONTRACT kind) with an unreconciled QX bid (pendingQx > 0): the shares that
    //     bid has already won sit in the universe but are not recognised yet. Their
    //     slice of the payment is set aside and paid to that bid's holding when it
    //     reconciles. This is the ONLY place the hook reads the universe, and only
    //     for such a source.
    //
    // UNIVERSE LOCK. When type == qpiDistributeDividends the core calls this hook
    // from inside qpi.distributeDividends() while it HOLDS universeLock, a
    // non-reentrant spinlock that qpi.numberOfShares / numberOfPossessedShares also
    // take: calling either here would hang the contract processor. The universe is
    // therefore read only with AssetPossessionIterator, which takes no lock (core's
    // own distributeDividends iterates with it under that same lock).
    struct POST_INCOMING_TRANSFER_locals
    {
        SourceKey key; SourceLedger led; Asset asset; AssetPossessionIterator it;
        uint128 scaled; uint64 fee; uint64 net; uint64 held; uint64 unrec; uint64 att;
    };
    POST_INCOMING_TRANSFER_WITH_LOCALS()
    {
        // Procedure transactions account for themselves inside the procedure.
        if (input.type == TransferType::procedureTransaction
            || input.type == TransferType::procedureInvocationByOtherContract)
        {
            return;
        }
        if (input.amount <= 0) return;

        // Outbid in an IPO. The refund carries no IPO identity -- only this
        // type, with sourceId NULL_ID -- so it is pooled, and each holding draws
        // from the pool exactly what the END_EPOCH snapshot says it is owed
        // (SettleIpo). It is returned capital, not a distribution, so the payout
        // accumulator is untouched.
        if (input.type == TransferType::ipoBidRefund)
        {
            state.mut().ipoRefundEscrow = sadd(state.get().ipoRefundEscrow, (uint64)input.amount);
            state.mut().totalIpoRefunded = sadd(state.get().totalIpoRefunded, (uint64)input.amount);
            return;
        }

        // revenueDonation (and any other zero-sender arrival) is never a dividend.
        if (input.sourceId != NULL_ID)
        {
            locals.key.source = input.sourceId;
            locals.key.kind = (input.type == TransferType::qpiDistributeDividends)
                ? QPOOL_SOURCE_CONTRACT : QPOOL_SOURCE_TOKEN;
            if (state.get().sources.get(locals.key, locals.led))
            {
                // Shares that already sit in the universe for an unreconciled QX bid
                // but are not recognised yet. Capped at what that bid asked for, so
                // stray donated shares never hold back anyone's dividend.
                locals.unrec = 0;
                if (locals.led.unsettled == 0 && locals.led.pendingQx > 0 && locals.led.assetName != 0)
                {
                    // The asset a ledger earns on: issued by the payer for a TOKEN ledger, by
                    // the protocol (NULL_ID) for a CONTRACT ledger, whose payer is the contract.
                    locals.asset.issuer = (locals.key.kind == QPOOL_SOURCE_CONTRACT) ? NULL_ID : locals.key.source;
                    locals.asset.assetName = locals.led.assetName;
                    locals.held = 0;
                    for (locals.it.begin(locals.asset, AssetOwnershipSelect::byOwner(SELF),
                             AssetPossessionSelect::byPossessor(SELF));
                         !locals.it.reachedEnd(); locals.it.next())
                    {
                        locals.held = sadd(locals.held, (uint64)locals.it.numberOfPossessedShares());
                    }
                    if (locals.held > locals.led.recognized)
                    {
                        locals.unrec = locals.held - locals.led.recognized;
                        if (locals.unrec > locals.led.pendingQx) locals.unrec = locals.led.pendingQx;
                    }
                }
                // Nothing recognised and nothing waiting to be: this payment is not on
                // shares we hold, so it is not a dividend.
                if (locals.led.unsettled > 0 || sadd(locals.led.recognized, locals.unrec) > 0)
                {
                    // Management fee: skimmed BEFORE the accumulator advances, so
                    // members' entitlements are computed on the net amount. Per call:
                    // a distribution that reaches us as several possession records
                    // is several calls, each rounded down on its own.
                    locals.fee = div((uint64)input.amount * QPOOL_MANAGEMENT_PERMILLE, 1000ULL);
                    locals.net = (uint64)input.amount - locals.fee;
                    if (locals.fee > 0)
                    {
                        state.mut().pendingRevenue = sadd(state.get().pendingRevenue, locals.fee);
                        state.mut().totalRevenue = sadd(state.get().totalRevenue, locals.fee);
                        state.mut().totalManagementFees = sadd(state.get().totalManagementFees, locals.fee);
                    }
                    // att: the part credited through the per-share accumulator now.
                    if (locals.led.unsettled > 0)
                    {
                        locals.att = 0;
                    }
                    else if (locals.unrec == 0)
                    {
                        locals.att = locals.net;
                    }
                    else
                    {
                        locals.att = div((uint128)locals.net * (uint128)locals.led.recognized,
                            (uint128)(locals.led.recognized + locals.unrec)).low;
                    }
                    if (locals.att > 0)
                    {
                        locals.scaled.high = locals.att;
                        locals.scaled.low = 0;
                        locals.led.accX64 = locals.led.accX64 + div(locals.scaled, (uint128)locals.led.recognized);
                        locals.led.accruedX64 = locals.led.accruedX64 + locals.scaled;
                    }
                    if (locals.net > locals.att)
                    {
                        locals.led.escrow = sadd(locals.led.escrow, locals.net - locals.att);
                        state.mut().dividendEscrowTotal =
                            sadd(state.get().dividendEscrowTotal, locals.net - locals.att);
                    }
                    state.mut().sources.set(locals.key, locals.led);
                    state.mut().totalPayoutsDistributed =
                        sadd(state.get().totalPayoutsDistributed, locals.net);
                    return;
                }
            }
        }
        state.mut().globalUnattributed = sadd(state.get().globalUnattributed, (uint64)input.amount);
    }

    // Split collected revenue to QPOOL's own 676 IPO shares and burn the rest.
    // Reserve / distribute / reconcile: only what actually leaves is deducted,
    // so integer remainders carry to the next epoch instead of being lost.
    struct END_EPOCH_locals
    {
        Entity ent; uint64 contractBalance; uint64 amount;
        uint64 shareholderCut; uint64 burnCut; uint64 perShare; uint64 paid; uint64 distributed;
        // the IPO snapshot
        IpoTarget row; uint64 t; uint64 i; sint64 price; uint64 px; uint64 finalPrice;
        uint64 runPrice; uint64 runCount; uint64 runKnown; IpoPriceKey key; IpoSeat seat;
    };
    END_EPOCH_WITH_LOCALS()
    {
        state.mut().contributions.cleanupIfNeeded();
        state.mut().ownership.cleanupIfNeeded();
        state.mut().votes.cleanupIfNeeded();
        state.mut().offers.cleanupIfNeeded();
        state.mut().sources.cleanupIfNeeded();
        state.mut().contractNames.cleanupIfNeeded();
        state.mut().admitted.cleanupIfNeeded();
        state.mut().ipoPrices.cleanupIfNeeded();
        state.mut().ipoBids.cleanupIfNeeded();
        if (state.get().ipoSeats.needsCleanup()) state.mut().ipoSeats.cleanupIfNeeded();   // (a write only when there is something to compact)

        // ---- The IPO snapshot. Runs first: it must not depend on the revenue gate below. ----
        // The node runs this hook BEFORE finishIPOs(), with system.epoch still the IPO epoch, so qpi.ipoBidPrice / ipoBidId still show
        // the final book of every target whose IPO ends now: the last moment it can be read. For each target that has unsettled
        // holdings and no snapshot yet (and whose book can be read: a row only exists after a bid, which is only possible in
        // the target's IPO epoch): the clearing price is the price of the last slot (0 when the book is not full: the IPO fails and
        // every bid is refunded in full), and every slot QPOOL holds is counted under its bid price. The book is sorted by
        // price, so equal prices are adjacent; a price belongs to ONE holding (ipoPrices), so the count of a price is that
        // holding's seats and the slots of other bidders at the same price are told apart by their bidder. State is written
        // only for what there is to store: a row per price QPOOL holds slots at, and the target's row. At most
        // QPOOL_MAX_IPO_TARGETS x 676 slots are read; there is no loop over pools, holdings or proposals.
        for (locals.t = 0; locals.t < QPOOL_MAX_IPO_TARGETS; locals.t++)
        {
            locals.row = state.get().ipoTargets.get(locals.t);
            if (locals.row.ipoContractIndex == 0 || locals.row.snapshotEpoch != 0) continue;
            if (qpi.ipoBidPrice((uint32)locals.row.ipoContractIndex, 0) < 0) continue;
            locals.price = qpi.ipoBidPrice((uint32)locals.row.ipoContractIndex, (uint32)NUMBER_OF_COMPUTORS - 1);
            locals.finalPrice = (locals.price > 0) ? (uint64)locals.price : 0;
            if (locals.finalPrice != 0)
            {
                locals.key.ipoContractIndex = locals.row.ipoContractIndex;
                locals.runPrice = 0;
                locals.runCount = 0;
                locals.runKnown = 0;
                // one extra round (i == 676) closes the last run
                for (locals.i = 0; locals.i <= (uint64)NUMBER_OF_COMPUTORS; locals.i++)
                {
                    locals.px = 0;
                    if (locals.i < (uint64)NUMBER_OF_COMPUTORS)
                    {
                        locals.price = qpi.ipoBidPrice((uint32)locals.row.ipoContractIndex, (uint32)locals.i);
                        if (locals.price > 0) locals.px = (uint64)locals.price;
                    }
                    if (locals.px != locals.runPrice)
                    {
                        if (locals.runCount != 0)
                        {
                            locals.key.price = locals.runPrice;
                            locals.seat.shares = locals.runCount;
                            locals.seat.spent = smul(locals.runCount, locals.finalPrice);
                            state.mut().ipoSeats.set(locals.key, locals.seat);
                        }
                        locals.runPrice = locals.px;
                        locals.runCount = 0;
                        locals.key.price = locals.px;
                        locals.runKnown = (locals.px != 0 && state.get().ipoPrices.contains(locals.key)) ? 1 : 0;
                    }
                    if (locals.runKnown != 0
                        && qpi.ipoBidId((uint32)locals.row.ipoContractIndex, (uint32)locals.i) == SELF)
                    {
                        locals.runCount = locals.runCount + 1;
                    }
                }
            }
            locals.row.finalPrice = locals.finalPrice;
            locals.row.snapshotEpoch = qpi.epoch();
            state.mut().ipoTargets.set(locals.t, locals.row);
        }

        if (state.get().pendingRevenue < QPOOL_MIN_DISTRIBUTION) return;

        qpi.getEntity(SELF, locals.ent);
        locals.contractBalance = locals.ent.incomingAmount - locals.ent.outgoingAmount;
        locals.amount = state.get().pendingRevenue;
        if (locals.amount > locals.contractBalance) locals.amount = locals.contractBalance;
        if (locals.amount == 0) return;

        locals.shareholderCut = div((uint128)locals.amount * (uint128)QPOOL_REVENUE_SHAREHOLDER_PERMILLE,
            (uint128)1000ULL).low;
        locals.burnCut = div((uint128)locals.amount * (uint128)QPOOL_REVENUE_BURN_PERMILLE,
            (uint128)1000ULL).low;
        locals.distributed = 0;

        locals.perShare = div(locals.shareholderCut, (uint64)NUMBER_OF_COMPUTORS);
        if (locals.perShare > 0
            && locals.perShare * (uint64)NUMBER_OF_COMPUTORS <= locals.contractBalance
            && qpi.distributeDividends((sint64)locals.perShare))
        {
            locals.paid = locals.perShare * (uint64)NUMBER_OF_COMPUTORS;
            locals.distributed = sadd(locals.distributed, locals.paid);
            locals.contractBalance -= locals.paid;
            state.mut().totalDistributedToShareholders =
                sadd(state.get().totalDistributedToShareholders, locals.paid);
        }

        if (locals.burnCut > 0 && locals.burnCut <= locals.contractBalance
            && qpi.burn((sint64)locals.burnCut) >= 0)
        {
            locals.distributed = sadd(locals.distributed, locals.burnCut);
            locals.contractBalance -= locals.burnCut;
            state.mut().totalBurned = sadd(state.get().totalBurned, locals.burnCut);
        }

        state.mut().pendingRevenue = state.get().pendingRevenue - locals.distributed;
    }

    // Shares come under THIS contract's management only when their owner hands them over (QX TransferShareManagementRights with this
    // contract as the new manager): the first step of a deposit. Shares that QPOOL buys (QX, QSWAP) stay under the venue's management and
    // never pass through here. The guide asks a PRE hook to keep the bad case out, so that the transfer fails cleanly and the shares stay
    // tradable where they are, instead of being locked under management by a deposit that can then never be made (audit C032): a TOKEN whose
    // issuer's ledger is bound to ANOTHER asset name (every deposit of it would be SOURCE_CONFLICT) is refused, and so is a token of an issuer that
    // has no ledger when the table of payers is full (CAPACITY). It is a plain read: nothing is written, nothing is called, no share moves
    // here. Everything else is accepted for free; contract shares (issuer NULL_ID) cannot be judged without the paying contract's index,
    // which only the deposit knows, and are taken back with ReleaseManagedShares if the deposit is refused. There is no POST hook: nothing
    // is accounted when shares arrive, a deposit records what it finds in the universe.
    struct PRE_ACQUIRE_SHARES_locals { SourceKey key; SourceLedger led; };
    PRE_ACQUIRE_SHARES_WITH_LOCALS()
    {
        output.allowTransfer = true;
        output.requestedFee = 0;
        if (input.asset.issuer != NULL_ID)
        {
            locals.key.source = input.asset.issuer;
            locals.key.kind = QPOOL_SOURCE_TOKEN;
            if (state.get().sources.get(locals.key, locals.led))
            {
                if (locals.led.assetName != input.asset.assetName) output.allowTransfer = false;
            }
            else if (state.get().sources.population() >= QPOOL_SOURCE_SOFT_CAP)
            {
                output.allowTransfer = false;
            }
        }
    }
};
