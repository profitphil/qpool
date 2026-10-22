# QPOOL testnet testing guide (K8.3)

Audience: the person or agent running QPOOL on a Qubic testnet with real transactions and real epoch changes.
Build under test: `QPoolK8.3-devkit.h` (K8.3 with the devkit wiring: `QPOOLK6_TICKS_PER_HOUR = 2`, QPAYHUB stub at index 50, test QPAY issuer); the production-valued source is `QPool-K8.3.h`.
For the season test use the season build `QPoolK8.3-season-devkit.h`: the same file with `QPOOL_AUTO_SETTLE_MAX = 3`, so that part of each IPO settles at the epoch start and the rest by hand.
Read `README-K8.3.md`, `../k8.2/README-K8.2.md`, `../k8.1/README-K8.1.md`, `../k8/README-K8.md` and `../k7-final/README-K7.md` first. Everything marked **NEW-K7**, **NEW-K8**, **NEW-K8.1**, **NEW-K8.2** or **NEW-K8.3** has never run on a real node.

**K8.1 rule (the owner's): the pool's OPERATORS are the creator and up to three ADMINS the creator names with `SetPoolAdmins`. Only operators place orders for the pool: they alone carry out passed proposals (`Execute`: IPO bids, QX and QSWAP buys and sells), rebid in an IPO (`RebidIpo`) and cancel a resting QX bid (`CancelQxBid`, with one public exception: anybody may cancel a bid older than 72 hours). Nobody else acts for the pool.**

**K8.2 rule: several pools may rest a QX bid on the same token at once** (up to 8 per payer's ledger, any prices, the same price too). The ledger lists its bids in priority order: the highest limit price first and, at one price, the bid placed first. QX fills bids from the highest price down, and QPOOL's bids at one price are one merged QX order that QPOOL serves earliest first, so the shares that arrive silently are handed to the bids in that order and the dividends set aside for them follow the same order. Anyone's `ReconcileHolding` credits all the ledger's bids. A ninth bid on a full ledger is refused (`QX_BID_PENDING`). A cancel takes the bid out of the list.

**K8.3 rule: a pool can rest a SELL limit order on QX** (proposal kind SELL_QX_LIMIT = 7: limit price, size, no minimum; voted like every sale, carried out by an operator). What the visible bids pay at the limit price fills at once and is credited from the balance, as in a SELL_QX; the rest RESTS on QX's book and the shares leave the holding's `shares` into `askShares` (they are not in the weight any more and still sit with QPOOL). A buyer who later hits the order pays QPOOL silently: the shares leave and the QU arrives with no call into QPOOL (the QU is filed under `globalUnattributed` by the hook). The ledger lists its resting sells in the order QX sells them (**lowest price first** and, at one price, the order placed first); anyone's `ReconcileHolding` hands the sold shares to the sells in that order and credits each its proceeds (shares sold x price, less an UPPER BOUND of QX's fee, less QPOOL's 1% management fee) taken from the `globalUnattributed` counter, never more than it holds. Dividends on the unsold shares are set aside for the sells that still hold them (from the back of the list). `CancelQxAsk{poolId, holdingId}` (procedure 25) takes the unsold part off QX's book and puts the shares back into the holding: operators at any time, anybody once the order is older than 72 hours. **A ledger lists resting bids OR resting sells, never both**: while sells rest on a token a BUY_QX of that token is refused (`QX_SIDE_BUSY`, 42), and while bids rest a SELL_QX_LIMIT is refused at Execute. At most 8 resting sells per ledger (the ninth: `QX_BID_PENDING`, 25) and 128 in all (`CAPACITY`). A holding rests one sell at a time. Tally, ReconcileHolding, SettleIpo and the epoch-start settlement stay open to anyone: they only count votes or credit what already happened and choose nothing.

What changed since the K6 guide (K8.1 adds suite S and the operator rule above, K8.2 suite T, K8.3 suite U): there is no typed IPO share name any more (the name is read from the chain); IPO holdings settle themselves at the epoch start;
`CancelQxBid` exists; the proposal scan, the QPAYHUB leg, `IPO_PRICE_TAKEN`, the QPAY hand-over fee and `GetPosition` changed. New suites: **O** (CancelQxBid),
**P** (names from the chain), **Q** (settlement at epoch start), **R** (the K7 small fixes). Suites G, H, I, J and the appendix were edited; the rest is unchanged.

## 0. Brief to paste to the testing agent

> You are testing the QPOOL smart contract on a testnet. Work through TESTING_GUIDE_K8.3.md in order. For every case record: the
> case id, the transactions sent (tick, sender, procedure, input, attached QU), the return code, the read-back values named
> in the case, expected vs actual, and PASS or FAIL. After EVERY transaction that changes state, run the invariant checks of
> section 3. Never mark a case PASS from a return code alone: always read the state back. Repeat each case with the variations
> of section 4. When something fails, stop that suite, keep the chain state, and report using section 9. Do not change the
> contract to make a test pass. Items in section 8 are known gaps: record them, do not report them as new bugs.
> The IPO flow is different from earlier builds: a BUY_IPO carries NO share name (send asset name 0), the name is paired at the next epoch start
> (read it with GetNameRegistry), and the holdings settle themselves at the epoch start (SettleIpo is only a backup).

**After the single-feature suites, run `REALISTIC_SEASON_TEST_K8.3.md`:** 40 users, 8 pools, 7 epochs, IPO rounds with one, two and three simultaneous IPOs, every procedure and read function many times, and an explicit battery for who may and may not receive dividends. Its plan is generated by `season_plan_k83.py` into `season-plan-k83/` (and the 100-wallet version `season_plan_k83_100.py`).

## 1. Setup

- Contract index: whatever your node assigns. Constants in the source that name OTHER contracts: QX = 1, QSWAP = 13, `QPOOL_QPAYHUB_CONTRACT_INDEX` = 29 in the
  production source and 50 in the devkit file (the QPayhubStub). Point it at your QPAYHUB stand-in (a contract that books plain QU transfers into a counter) and say which index you used.
- QPAY token: the real one is mainnet-only. The devkit file already uses a test issuer; issue the test token on QX with that issuer and the name `QPAY`. Keep the ceiling `QPOOL_QPAY_MAX_PRICE` = 10,000.
- Windows on the devkit build (2 ticks per hour): vote 144 ticks, execute 96, IPO vote 48, a resting QX bid can be cancelled by anyone after 144 ticks (`QPOOL_QX_BID_MAX_AGE_TICKS` = 72 hours). A normal proposal lives 240 ticks, a BUY_IPO 144. Poll the tick; never guess with wall-clock sleeps.
- **IPO targets (new rule).** Each IPO target must be a contract ABOVE index 30 whose IPO ENDS AFTER QPOOL's first epoch has begun. QPOOL takes a baseline of the zero-issuer names at its first `BEGIN_EPOCH`; a contract
  whose shares already exist then is recorded as "known, never paired" and its shares can never be bought, deposited or settled through QPOOL. Check `GetNameRegistry` `baselineDone` = 1 before the first IPO epoch.
- `QPOOL_AUTO_SETTLE_MAX`: 32 in the production source, 3 in the season build. The single-feature suites run on the default; suite Q needs a build with 2 so a backlog can be made with five holdings.
- You need: IPO-phase target contracts (construction epoch = current epoch + 1) for suites G, P, Q; a second token issuer that can pay dividends; the ability to change epoch on demand; the explorer's list of assets of the zero issuer
  (AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAFXIB) or the node's issued-assets reply (each record carries its slot number); and the ability to read every `Get*` function.
- Fund every test wallet generously. The pool fee alone is 10,000,000 QU.
- Fee reserve: record `qpi.queryFeeReserve` for the contract before and after each suite.

## 2. Actors (create at least these, keep them for the whole run)

| Name | Role |
|---|---|
| CREATOR_A, CREATOR_B | create pools A and B |
| M1..M6 | members with different sizes (see 4) |
| OUT1, OUT2 | non-members; also IPO outsider bidders |
| ISSUER1, ISSUER2 | token issuers who pay dividends |
| KEEPER | calls permissionless procedures (Tally, ClaimPayout, BuyAndBurnQpay, ReconcileHolding, SettleIpo as a backup, CancelQxBid after the age limit). **K8.1: a KEEPER can no longer Execute a proposal or rebid**; its attempts must be refused |
| ADMIN1..ADMIN3 | per pool, named by the creator with SetPoolAdmins: carry out proposals, rebid and cancel QX bids when the creator is not there; no other power |
| ATTACKER | tries the cases in sections 7 and 10 |
| SHAREHOLDER1..2 | hold some of QPOOL's own 676 shares (for revenue payout checks) |

Make sure QPOOL's own 676 shares exist and are held by known wallets, otherwise the 40 percent leg cannot be paid.

## 3. Invariants: run after every state-changing transaction

Read `GetStats`, `GetPool` for every pool, `GetMember` for every actor, `GetHolding` for every holding, and the contract's QU balance.

| # | Invariant | How |
|---|---|---|
| I1 | Per pool: sum of members' `uncommitted` = `uncommittedTotal`; sum of `committed` = `committedTotal` | GetPool, GetMember |
| I2 | Roster has no duplicate and every roster member has a record | GetPool, GetMember |
| I3 | Per active holding: sum of members' seat `cost` (GetPosition) = `costBasis` | GetHolding, GetPosition |
| I4 | IPO holding before settlement: `ipoAtRisk + ipoAvailable = costBasis` | GetHolding |
| I5 | Solvency: contract QU balance >= sum(uncommittedTotal) + pendingRevenue + qpayBuyPot + ipoRefundEscrow + dividendEscrowTotal + sum(ipoAvailable) + sum(claimable) | GetStats, GetPool, GetHolding, GetPosition |
| I6 | With no dividends and no IPO live, the balance equals sum(uncommittedTotal) + pendingRevenue + qpayBuyPot + globalUnattributed + ipoRefundEscrow exactly | same |
| I7 | `globalUnattributed` is 0 whenever no QPOOL call is in progress, **except (K8.3) the proceeds of a resting sell that has filled and that no ReconcileHolding has credited yet**: it equals the sum over the ledgers with resting sells of (shares sold and not credited x price, less QX's fees) and drops to 0 at the next reconcile | GetStats |
| I8 | A member cannot withdraw more than `uncommitted` minus what open votes lock | WithdrawUncommitted, GetMember |
| I9 | No counter wraps: any value above 2^63 is a failure | all reads |
| I10 | Each live IPO price belongs to exactly one holding; at most 16 live prices per holding | GetIpoBids |
| I11 | `qpayBuyPot` only increases at END_EPOCH and only decreases through BuyAndBurnQpay | GetStats |
| I12 | Dividend credits: nobody can claim more than was paid in, within 1 QU per claimant of floor rounding | GetHolding, ClaimPayout |
| I13 | **NEW-K8** Name registry: an entry is never removed or changed; `queueLength` is 0 after every BEGIN_EPOCH; `paired` never decreases | GetNameRegistry |
| I14 | **NEW-K7/K8.2** For every source ledger `pendingQx` equals the sum of its holdings' `pendingShares`; a holding with `pendingShares` 0 shows `qxBidPrice` 0 and is not in the ledger's list; the list (`qxHold`, at most 8) holds exactly the holdings with `pendingShares` above 0, highest price first and, at one price, the earlier bid first (`qxSeq`) | GetSource, GetHolding |
| I15 | **NEW-K7** Around every CancelQxBid: balance delta = sum of `uncommittedTotal` delta = `refunded`; `globalUnattributed` unchanged | balance, GetPool, GetStats |
| I17 | **NEW-K8.1** Only operators move a pool's orders: every Execute of a live mandate, every RebidIpo and every CancelQxBid before the age limit by a non-operator returns NOT_AUTHORIZED, returns the reward and writes nothing; closing an EXPIRED mandate and cancelling a stale (older than 72 hours) bid are the only open exceptions | any procedure, GetPool |
| I16 | **NEW-K8** `autoSettleDone + autoSettleSkipped` never exceeds `QPOOL_AUTO_SETTLE_MAX` for one epoch; `readyUnsettled` equals the unsettled holdings of ready targets | GetStats, GetIpoTarget |
| I18 | **NEW-K8.3** For every source ledger with `qxSide` 2: `askTotal` = the sum of the `askShares` of the holdings in its list; the list is ordered by `qxBidPrice` ascending and, at one price, by `qxSeq`; a ledger lists bids or sells, never both (`qxSide` 0 when the list is empty); `GetStats.restingAsks` = the number of holdings with `askShares` above 0, over all ledgers, and is at most 128; `GetStats.restingBids` = the number of holdings listed as resting bids (K8.2 lists), at most 128 | GetSource, GetHolding |
| I19 | **NEW-K8.3** QX side: for every (asset, price) the shares QX lists for QPOOL's ask (`EntityAskOrders`) = the sum of `askShares` of the holdings resting there, less the shares sold and not yet credited; after a ReconcileHolding the two are equal. The universe's shares of the asset held by QPOOL = `recognized` + the unsold part of `askTotal` | QX EntityAskOrders, GetSource |

Rounding tolerance: floor rounding and fixed-point accumulators can leave up to 1 QU of difference per claimant. Anything
larger is a failure. Report the observed difference, not just pass or fail.

## 4. Variation dimensions (apply to every suite, not only once)

Run each case with several values of each dimension; combine them pairwise at least.

- Members in the pool: 1, 2, 3, 10, 50, 256 (roster full).
- Contribution split: 50/50, 60/40, 99/1, 1/1/1 equal, one whale and many dust members, minimum amounts (10,000).
- Amounts: minimum, typical, large, near a boundary (one below, exact, one above), and values near 2^63 for rejected inputs.
- Timing: first tick the action is allowed, last allowed tick, one tick too early, one tick too late, same tick as another action.
- Actor: creator, member, admitted non-member, non-member, issuer, keeper, attacker.
- Pool kind: public, private, switched from one to the other with members inside.
- Asset kind: user-issued token, contract share. Venue: QX, QSWAP, IPO.
- Order of operations: every permutation for 2 to 3 pools or holdings (settle order, claim order, vote order).
- Epoch position: early in the epoch, just before END_EPOCH, just after BEGIN_EPOCH, across several epochs.
- Money state: reserve high, reserve near 0, reserve at 0; contract balance exactly equal to a bid, one QU short.
- Failure injection: attach too little or too much QU, wrong sender, stale input, callee unavailable, repeated submission of the
  same transaction, two conflicting transactions in the same tick.
- Settlement mode: holdings settled by BEGIN_EPOCH, by a manual SettleIpo, or split (a build with a small `QPOOL_AUTO_SETTLE_MAX`); the result must not depend on the mix.
- Operator rights: creator, admin 1 to 3, a removed admin, a funder who is not an operator, a member without a seat, a member of another pool, a stranger; before, at and after the 72 hour age limit for a QX bid.

Every refusing case must also check: the attached reward came back exactly once, and no state changed (compare reads before and after).

## 5. Suites

Notation: `X -> Y` means expected result. Fees: pool 10,000,000; join 10,000; minimum contribution 10,000; dividend
management fee 1 percent; seat sale fee 1 percent of the ask; QX hand-over fee 100; SELL_QSWAP flat fee 100,000.

### A. Pools and membership
| Id | Case | Expected |
|---|---|---|
| A1 | CreatePool with exactly 10,000,000 / less / more | exact: pool created; less: refused; more: excess returned |
| A2 | Create pools until a few exist; read counts | `poolCount` and ids consecutive |
| A3 | Contribute as first-time member with join fee + minimum / below minimum / large | accepted only when it covers fee and minimum; the join fee is revenue, the rest is `uncommitted` |
| A4 | Second Contribute by same member | no second join fee, no second roster entry (I2) |
| A5 | Private pool: non-admitted outsider contributes; creator admits; retry | refused, then accepted; admission persists |
| A6 | Switch public to private and back with members inside | members stay admitted, can contribute and trade seats, newcomers refused when private |
| A7 | Roster to 256, then one more joiner via Contribute and via BuyPosition and via DepositHoldingAsset | refused with a capacity code before any state write |
| A8 | Non-creator calls SetPermissioned / SetAdmission / DepositHoldingAsset | refused, reward returned |
| A9 | CloseEmptyPool on empty and non-empty pool | only an empty pool |
| A10 | Every procedure with attached QU it does not need | the QU comes back exactly once on every path (build a table of all procedures x a refusing input) |

### B. Money in and out
| Id | Case | Expected |
|---|---|---|
| B1 | Withdraw part / all / more than balance / zero | exact, refused beyond balance |
| B2 | Withdraw while a BUY vote cast by this member is open | locked part refused until the vote ends |
| B3 | Withdraw between Tally and Execute for a yes-voter on a passed BUY | still locked until Execute or expiry |
| B4 | Contribute after a BUY proposal opened, then vote | that money carries no weight and is not charged (code DEPOSIT_AFTER_OPEN or zero weight) |
| B5 | Many members contribute and withdraw in the same tick | I1 holds; order of execution does not lose QU |
| B6 | Withdraw when the QU transfer is impossible | refused with a clear code, records restored (ClaimPayout pattern) |

### C. Governance (all proposal kinds)
Kinds: BUY_QSWAP, BUY_QX, BUY_IPO, SELL_QSWAP, SELL_QX, ALLOCATE, SELL_QX_LIMIT (7, K8.3: suite U). Read `GetProposal` after each step.
| Id | Case | Expected |
|---|---|---|
| C1 | Propose each kind with valid input | accepted; fields read back correctly; BUY_IPO shows the derived opening quantity |
| C2 | Propose with invalid inputs: zero, price/quantity overflow, unknown contract index, asset name 0 (every kind except BUY_IPO), a NON-ZERO asset name on a BUY_IPO, amount above `uncommitted`, proposer not a member, second live proposal by the same proposer | each refused before any state write (dirty check); the BUY_IPO name case returns INVALID_INPUT |
| C3 | Vote YES / NO / abstain / twice / after window / by non-member / on a seat sold after voting | first vote counts once; repeats refused |
| C4 | Quorum edges with 250,000 / 250,000 splits: cast one below quorum, exactly quorum, tie yes = no | below: rejected; exact quorum with yes > no passes; a true tie rejects |
| C5 | Tally one tick before the vote window ends / first allowed tick | WINDOW_OPEN then OK |
| C6 | Execute (by the creator or an admin) on the last tick of the execute window / one later by anybody | works, then WINDOW_CLOSED for the first call after the window and WRONG_STATUS after that (anybody may close a dead mandate) |
| C13 | **NEW-K8.1** Execute of a live mandate by a stranger, by a member who voted YES, by the creator of another pool | NOT_AUTHORIZED each time, the reward returns, nothing is written; the same call by the creator or an admin succeeds |
| C7 | Tally late (after execute window) | proposal expires, locks still release |
| C8 | BUY passes with a yes minority paying: 60 percent holder yes, 40 percent abstains | only recorded yes-voters are charged; the abstainer is never charged |
| C9 | Execute asks more than the yes money can pay | clamped or refused per the contract; never charges a non-voter (I1, I5) |
| C10 | Several proposals open at once in one pool and across pools; slot reuse after expiry | no vote bleed between proposals (a recycled slot must not carry old votes) |
| C11 | Seat locked by a SELL/ALLOCATE vote: OfferPosition and BuyPosition from it | refused with SEAT_LOCKED until the vote window ends, then allowed |
| C12 | Proposer's money moves between Propose and Execute | see B3; Execute refuses cleanly if money is not there |

### D. QX trading
Run section D+/E+/F+ (multi-pool battery) first; the cases below are the single-feature checks.
Use a real token on QX with real asks and bids.
| Id | Case | Expected |
|---|---|---|
| D1 | BUY_QX where asks exist at or below the voted price: full fill | holding created with the shares; refund of price improvement returned to the buyers, not left as unattributed money |
| D2 | BUY_QX with partial fill | filled part in the holding, rest rests on QX as a pending bid (`pendingShares`, `qxBidPrice`, `qxBidTick`); ReconcileHolding later recognises fills; CancelQxBid takes the rest back (suite O) |
| D3 | BUY_QX with no asks | whole order rests; nothing lost |
| D4 | A resting bid that never fills | **NEW-K7** CancelQxBid takes it back: see suite O. Until then its QU stays locked on QX (K8.2: it no longer blocks other pools' bids) |
| D5 | SELL_QX of a QX-managed holding at full / partial / no matching bid | proceeds net of fixed costs (2 x 100) credited to the holding; unsold part kept |
| D6 | SELL_QX with more than 256 bids on the book | sale reaches only the first window; remaining shares stay |
| D7 | Shares sold on QX by a member outside QPOOL (donated shares) | never counted as recognised shares |
| D8 | Contract shares (issuer NULL_ID) via QSWAP | rejected; contract shares trade on QX only |

### E. QSWAP trading
| Id | Case | Expected |
|---|---|---|
| E1 | BUY_QSWAP with min-out satisfied / not satisfied | refund of the unspent QU, holding only for what arrived |
| E2 | SELL_QSWAP with min proceeds just above 100,100 / below | below is refused at Propose |
| E3 | SELL_QSWAP success: net proceeds = QSWAP proceeds minus the 100,000 flat fee | holding credited the net; contract balance beyond obligations unchanged (I5, I6) |
| E4 | SELL_QSWAP where QSWAP sells nothing and returns the fee | nothing lost, nothing credited, I7 holds |
| E5 | SELL_QSWAP of shares under QX management and of deposited shares | hand-over works, QX fee paid |
| E6 | Manipulated pool: AMM price moved between Propose and Execute | only the vote-time min-out protects; record the loss bound |
| E7 | Two pools doing swaps in the same tick | each pool's books independent |

### F. Dividends
| Id | Case | Expected |
|---|---|---|
| F1 | Token issuer pays a dividend by plain transfer to QPOOL; one pool holds the token | 1 percent management fee to revenue; the rest split by shares then by cost, +/- 1 QU |
| F2 | Same asset held by TWO pools (different shares and costs) | each pool gets its share of shares; inside each pool members by cost |
| F3 | Contract (distributeDividends) pays a contract share held by a pool | credited the same way |
| F4 | Payment from a sender that matches no ledger | goes to `pendingRevenue` (not `globalUnattributed`), I7 |
| F5 | ClaimPayout twice; by a member with nothing owed; by a non-member | second claim and empty claim refused without writing state |
| F6 | Dividend arrives while an IPO holding of that contract is unsettled | parked in escrow, released per recognised share after the last settlement |
| F7 | Dividend arrives while a BUY_QX fill is unreconciled | the unrecognised part is set aside and paid at reconcile |
| F8 | A seat is bought mid-dividend cycle | the buyer receives only dividends earned after the purchase |
| F9 | 1 QU dividends in many small payments | each rounded down, none created; sum of claims never exceeds what came in |

### G. IPO (most important; needs a contract in its IPO epoch)
Run section G+ (competitive battery with 3 to 4 pools) first; the cases below are the single-feature checks.
Standard book: OUT1 bids 576 slots at 2,500. Pool A: budget 200,000 at opening price 2,000 gives 100 seats all-in, which fills the 676-slot book.
There is NO ceiling price and NO voted share count: the budget is the only limit; seats = floor(money / price).
| Id | Case | Expected |
|---|---|---|
| G1 | Propose BUY_IPO (asset name 0), vote 48 ticks, Tally, Execute | holding with `ipoAtRisk` = 100 x 2,000, `ipoAvailable` = 0; opening quantity = min(676, floor(assigned / price)); `GetHolding.assetName` is 0 until the holding settles |
| G2 | Execute when assigned < opening price | refused, nothing written |
| G3 | OUT2 bids 5 slots at 2,100 | 5 of A's lowest slots replaced; the refund (5 x 2,000) reaches QPOOL during OUT2's own bid transaction and is pooled (`ipoRefundEscrow`) |
| G4 | The creator (or an admin) calls RebidIpo at 2,101 after OUT2's bid | the call first reconciles against the live book and frees the 10,000 that returned; quantity floor(10,000/2,101) = 4 (cost 8,404); a rebid is funded only by money already returned, never by refunds its own bid causes |
| G5 | Repeat RebidIpo reusing price 2,101 until convergence | seats stay 95; all 95 end at 2,101; free funds 405; a price already owned by the same holding is reusable; never CAPACITY while reusing |
| G6 | RebidIpo at a price owned by ANOTHER holding | IPO_PRICE_TAKEN |
| G7 | RebidIpo at or below the book minimum on a full book | PRICE_TOO_LOW, no state write |
| G8 | RebidIpo by a stranger / a member without a seat / a seat owner who is not an operator / a removed admin | NOT_AUTHORIZED each time (K8.1), reward returned, nothing written; an admin succeeds |
| G9 | RebidIpo needing more QU than free funds / than the contract holds | INSUFFICIENT_BALANCE / BOOKS_INCONSISTENT, nothing written |
| G10 | Rebid at a very high price (for example 40x) | accepted if the money covers at least one seat; buys correspondingly few seats |
| G11 | Five rebids in the last stretch of the IPO epoch, with outsiders outbidding between them | after each call: books vs `GetIpoBids` vs the core book agree on seats; money conserved to the unit |
| G12 | Seats the bid pushed out of the pool's OWN book | their refund arrives after the bid; the books free it only at the next rebid or settlement |
| G13 | Two pools bid in the same IPO at different prices; one is outbid | refunds land in the shared pot; neither pool draws the other's money |
| G14 | Epoch change: END_EPOCH reads the book BEFORE the core closes the IPO; BEGIN_EPOCH then pairs the name and settles | `GetIpoTarget` shows a snapshot with the final price; a manual SettleIpo before the snapshot exists is refused (IPO_NO_SNAPSHOT); after the epoch change `GetStats.autoSettleDone` counts the settled holdings |
| G15 | Settlement of every holding in EVERY mix of automatic and manual (a build with `QPOOL_AUTO_SETTLE_MAX` 1 or 2 leaves holdings for manual calls; 2 pools, then 3) | identical per-member results whatever settles at the epoch start and in whatever order the rest is settled by hand |
| G16 | Final price below the pool's price: refund of (price - final) per seat | returned pro rata by cost; sum of members = budget |
| G17 | Failed IPO (book never fills) | everything refunded; BEGIN_EPOCH retires the holding with no shares (no name is paired for that index) |
| G18 | Pool outbid to the end with zero seats | whole budget returned |
| G19 | Two IPOs in the same epoch, one failing and one winning | each settles exactly; one pool cannot take the other's refund |
| G20 | More than 8 pools or targets: ninth IPO target / ninth IPO holding of one pool | refused with CAPACITY before any state write |
| G21 | **NEW-K8** A wrong name can no longer be declared | Propose BUY_IPO with a non-zero asset name returns INVALID_INPUT and writes nothing; with name 0 it is accepted |
| G22 | Dividend of the new contract before and after settlement | parked until all holdings of that ledger settle (with the default bound that is the epoch start; with a small bound it is the last manual call) |
| G23 | RebidIpo after the IPO epoch ended / before the opening bid executed | refused |
| G24 | Ties: a bid equal to the lowest price | does not register; refunded at once |
| G25 | **NEW-K7** A BUY_IPO whose opening price was taken by another holding before its Execute | Execute returns IPO_PRICE_TAKEN, the proposal becomes FAILED (5), the YES voters' lock is released at once, the proposer can Propose again (before K7: PASSED, locked, PROPOSAL_PENDING) |
| G26 | **NEW-K8** Holding of a target whose name is not paired yet (before the first BEGIN_EPOCH after the IPO, or a quarantined epoch) | a manual SettleIpo of a holding that won seats returns NAME_PENDING (41) and writes nothing; a holding that won no seat settles without a name |
| G27 | **NEW-K8** `GetHolding.assetName` after settlement | equals the name `GetNameRegistry` shows for the target; `GetSource` of the target's ledger carries the same name |

### H. Seat market
| Id | Case | Expected |
|---|---|---|
| H1 | OfferPosition of part / all / more than owned / on a retired holding | accepted for valid; refused otherwise |
| H2 | BuyPosition with exact price, over-payment, under-payment | over-payment refunded; under refused; fee 1 percent of the ask taken from the seller's proceeds |
| H3 | Seller replaces the offer between the buyer's read and buy | OFFER_CHANGED when the buyer passed `expectedCostUnits` that no longer matches; refunded, nothing written |
| H8 | **NEW-K7** A sale or a reconcile changes the holding's shares between the buyer's `GetPosition` and `BuyPosition` | `GetPosition.offeredShares` returns the LIVE shares of the holding; a `BuyPosition` with the old `expectedShares` returns OFFER_CHANGED (refunded, nothing written); the same call with the new live value succeeds without a re-offer |
| H4 | Buyer already owns seats; buyer is a non-member of a public / private pool | joins the roster once (join fee paid once) / refused when private and not admitted |
| H5 | CancelOffer; offer after SettleIpo shrank the cost | stale offer must not sell a larger fraction at the old ask |
| H6 | 32 offers per pool, then one more | refused |
| H7 | Seat of a holding whose ledger is unsettled / frozen | SETTLEMENT_PENDING |

### I. Revenue and END_EPOCH (NEW)
Revenue sources to mix: pool creation fee, join fees, 1 percent dividend fee, 1 percent seat fee, a stray wallet transfer,
a revenue donation, a contract dividend with no ledger. Split: 40 shares / 30 QPAYHUB / 20 pot / 10 plus leftovers burned.
| Id | Revenue in one epoch | Expected (per share, paid, to hub, to pot, burned) |
|---|---|---|
| I1 | 1,000,000 | 591, 399,516, 300,000, 200,000, 100,484 |
| I2 | 1,690 | 1, 676, 507, 338, 169 |
| I3 | 1,689 | 0, 0, 506, 337, 846 |
| I4 | 100 | 0, 0, 30, 20, 50 |
| I5 | 1 | 0, 0, 0, 0, 1 |
| I6 | 10,080,000 (a live run already gave 5,964; 4,031,664; 3,024,000; 2,016,000; 1,008,336) | same |
Also check: `pendingRevenue` is 0 afterwards (unless the balance clamped it); `totalDistributedToShareholders`,
`totalQpayhubDeposited`, `qpayBuyPot`, `totalBurned` each rose by exactly the expected amount; each SHAREHOLDER wallet received
`perShare x shares held`; the fee reserve rose by the burned amount; the QPAYHUB stand-in's counter rose by the hub leg.
| Id | Case | Expected |
|---|---|---|
| I7 | Epoch with zero revenue | END_EPOCH writes nothing |
| I8 | Three epochs in a row with different revenue | each epoch independent; pot accumulates |
| I9 | QPAYHUB stand-in refusing the transfer | that leg falls into the burn; nothing lost or double counted |
| I12 | **NEW-K7** No contract at `QPOOL_QPAYHUB_CONTRACT_INDEX` (an index at or above the node's contract count) | END_EPOCH finds it with `qpi.isContractId`, does not transfer, does not book the leg as deposited; `totalQpayhubDeposited` stays 0 and `totalBurned` rises by the 10 percent leg plus the whole 30 percent leg plus rounding. The probe proves only that some contract is there, not that it is QPAYHUB |
| I10 | Contract balance lower than the books (poke if you can) | payout clamps to the balance minus the pot |
| I11 | The pot is never paid out or burned by END_EPOCH | `qpayBuyPot` unchanged by a revenue-less epoch |

### J. BuyAndBurnQpay (NEW)
Anyone may call. Needs a pot (run suite I first). Uses the test token you issued on QX.
| Id | Case | Expected |
|---|---|---|
| J1 | Empty pot and nothing unburned | NOTHING_TO_BUY, no state write, reward returned |
| J2 | Pot but no asks | NOTHING_TO_BUY, pot unchanged |
| J3 | One ask level at 50 for 5,000 tokens | bought 5,000, spent 250,000, burned 5,000, `qpayUnburned` 0. **NEW-K7:** the hand-over fee booked is the amount the balance really fell by during the hand-over (0 if QX refunds the attached reward, 100 if it keeps it): read the balance before and after and report which; pot left = pot - 250,000 - that measured amount, and no QU is counted by no bucket (balance = sum of buckets) |
| J4 | Several ask levels, for example 40, 50, 60 with different sizes | lowest first, each filled at its own price; totals add up |
| J5 | More than 8 levels | 8 bought, the rest of the pot waits; a second call continues |
| J6 | Asks only above 10,000 | untouched; NOTHING_TO_BUY |
| J7 | Ask exactly 10,000 | bought |
| J8 | Budget smaller than the next level's price | stops; the leftover is below one token's price |
| J9 | Partial fill, or the ask changes between the read and the bid | remainder taken back from QX; nothing rests on QX |
| J10 | The hand-over to QPOOL management fails (inject) | tokens counted in `qpayUnburned`, retried and burned by the next call |
| J11 | Supply check | the token's total supply dropped by exactly `burned`; the zero key holds nothing |
| J12 | A lone ask at 9,999 when the market is far lower | it is bought in full (accepted risk: the ceiling is the only protection); record the loss |
| J13 | Two callers in the same tick | no double spend; pot arithmetic exact |
| J14 | Reward attached | returned |
| J15 | Pot untouched across an epoch change with no calls | unchanged |
| J16 | `maxLevels` input 0, 1, 8, 9 | 0 and above 8 mean 8 |

### K. The incoming-transfer hook (NEW)
| Id | Source of QU | Expected |
|---|---|---|
| K1 | Plain wallet transfer, no ledger | `pendingRevenue` and `totalRevenue` up |
| K2 | Revenue donation | same |
| K3 | Contract dividend with no ledger | same |
| K4 | QX / QSWAP payment during a QPOOL call (sale proceeds, refunds) | parked inside the call and handed back, never revenue; I7 holds after every call |
| K5 | IPO refund | `ipoRefundEscrow`, not revenue |
| K6 | Procedure invocation reward | accounted by the procedure, not by the hook |
| K7 | Dividend from a matched ledger | credited as in suite F |

### L. Capacity and limits
| Id | Case | Expected |
|---|---|---|
| L1 | Per pool: 128 holdings, 32 deposits, 8 IPO holdings, 32 offers, 256 admitted, 512 ownership rows | the next one refused with CAPACITY before any state write; other pools unaffected |
| L2 | Global tables (1,024 pools, 262,144 proposals, and so on) | impractical at full size: build a test copy with the constants shrunk by 100x and run the exhaustion cases there |
| L3 | Proposal slot reuse after many proposals | old votes never leak into a reused slot |
| L4 | Hash-map cleanup at END_EPOCH with many removed entries | all reads still correct after the epoch change |

### M. Fee reserve and performance
| Id | Case | Expected |
|---|---|---|
| M1 | Reserve drained per call type | record microseconds-equivalent drain for each procedure |
| M2 | Reserve at or below zero | the node refuses all user procedures and returns the QU; hooks and END_EPOCH still run; money not lost |
| M3 | 1 QU stray transfer every tick for 100 ticks | KNOWN: each writes state; measure node load and reserve; this is the cost vector the build has not fixed |
| M4 | Dust rebids in a loop by a small seat owner | KNOWN: measure cost per call (section 8) |
| M5 | Digest time with the 190 MB state, dirty tick vs clean tick | record both |
| M6 | A call that is refused or does nothing | state NOT dirtied |
| M7 | **NEW-K7** 50 invalid Propose calls by a member with no live proposal while about 1,000 proposal slots are in use | each returns INVALID_INPUT without a state write; record the reserve drain per call (the scan now stops at the first unused slot) |
| M8 | **NEW-K8** BEGIN_EPOCH with the bound's worth of settlements (32 holdings in the production build; build with `QPOOL_AUTO_SETTLE_MAX` as high as your node allows) | record the time of the epoch start and the reserve drain; the agent's estimate of 20 to 35 ms is a guess |

### N. Epoch transitions and persistence
| Id | Case | Expected |
|---|---|---|
| N1 | Read all state, change epoch, read again | only the expected fields changed |
| N2 | Run every `Get*` function before and after a node restart in mid-epoch | identical |
| N3 | END_EPOCH with the reserve at 0 | runs; revenue split still happens |
| N4 | Several epochs with no activity | no drift in any counter |
| N5 | Proposal that spans an epoch change | windows count in ticks, not epochs |
| N6 | **NEW-K8** Restart the node exactly at an epoch start, after a pairing and an automatic settlement | `GetNameRegistry` and the `autoSettle*` fields of `GetStats` are identical before and after; no holding settled twice; no pairing repeated |

### S. Operators: the creator and up to three admins (NEW-K8.1)
`SetPoolAdmins{poolId, admin0, admin1, admin2}` (procedure 24; creator only) replaces the pool's admin list; NULL_ID leaves a slot empty. The creator and the admins are the pool's operators. `GetPool` shows `admin0..2`.
Operators alone: `Execute` (every proposal kind: BUY_IPO opening bids, BUY_QX, SELL_QX, BUY_QSWAP, SELL_QSWAP), `RebidIpo`, `CancelQxBid` and `CancelQxAsk` (before the 72 hour age limit).
Open to everyone (they choose nothing): `Tally`, `ReconcileHolding`, `SettleIpo`, `ClaimPayout` (own money only), the closing of an EXPIRED mandate, and `CancelQxBid` on a bid older than 72 hours.
Run I17 after every case.
| Id | Case | Expected |
|---|---|---|
| S1 | SetPoolAdmins by the creator with 2 admins (one a member, one a wallet that is on no roster) and a reward attached | OK, reward returned; GetPool shows the two ids and an empty third (NULL_ID) |
| S2 | SetPoolAdmins refusals: by an admin, by a member, by the creator of another pool; the creator naming himself; the contract's id; the same id twice; a pool id out of range; an inactive pool | NOT_AUTHORIZED for the first three; INVALID_INPUT for the next four; NOT_FOUND for the inactive pool; nothing changes (GetPool before and after) |
| S3 | The same list again; a list of three; the empty list | OK each time; the identical list writes nothing; three admins and then none are shown by GetPool |
| S4 | Execute of a passed mandate by a stranger, a YES voter who is not an operator, the creator of another pool | NOT_AUTHORIZED, reward returned, balance and books unchanged |
| S5 | Execute by an admin (including an admin on no roster) | OK, same result as the creator would get; then the creator replaces the list without that admin and the same admin's Execute on the next mandate returns NOT_AUTHORIZED |
| S6 | Nobody executes: advance past the execute window, then a stranger calls Execute | the first call returns WINDOW_CLOSED (the mandate is closed and its votes released), later calls WRONG_STATUS; nothing was spent; the proposer may Propose again |
| S7 | RebidIpo by a seat owner who is not an operator, by a stranger, by a removed admin; then by the creator and by an admin | NOT_AUTHORIZED for the first three (reward back, nothing written); OK for the creator and the admin |
| S8 | CancelQxBid before the age limit by an admin, the creator, a funder who is not an operator, a member without a seat | OK for the admin and the creator; NOT_AUTHORIZED for the others; after 72 hours (strictly more) anybody succeeds; the refund goes to the seat owners, never to the caller |
| S9 | The creator is away: an admin runs a whole IPO war (opening Execute, 5 rebids in the last stretch, the cancel of a resting QX bid) | everything succeeds as for the creator; compare with the model of `ipo_reference_model.py`; I1 to I17 hold |
| S10 | Admin list replaced mid-IPO: remove admin 1, add admin 3 while a war is running | the removed admin's next RebidIpo is NOT_AUTHORIZED at once, the new admin's succeeds; no state of the holding changes by the replacement itself |
| S11 | Everything that stays open: Tally, ReconcileHolding, SettleIpo, BuyAndBurnQpay by a stranger | works as before (they choose nothing); the epoch-start settlement is unchanged |
| S12 | A malicious admin (KNOWN, record it): rebid at a very high price with quantity 0 | succeeds (operators are trusted); record the loss bound (review item R5, the burn variant); the creator removes the admin with SetPoolAdmins |

### O. CancelQxBid (NEW-K7)
A BUY_QX whose remainder does not fill rests on QX. `CancelQxBid{poolId, holdingId}` takes it back. Callers: the pool's operators (the creator or an admin; K8.1) at any time; anyone once the bid is older than
`QPOOL_QX_BID_MAX_AGE_TICKS` (strictly older: age = limit is still refused). The QU comes back from QX and is credited to the seat owners by cost; the ledger's QX_BID_PENDING block lifts.
Setup: asset X = a token issued on QX; pool A (creator CREATOR, ALICE 600,000, BOB 400,000 uncommitted) and pool B (creator DAVE, CAROL 300,000, DAVE 200,000). Wait one tick between a contribution and a Propose.
Run invariants I14 and I15 around every case. Read the contract balance, `GetPool.uncommittedTotal`, `GetStats.globalUnattributed`, `GetHolding`, `GetSource` before and after each call.
| Id | Case | Expected |
|---|---|---|
| O1 | Resting bid with an empty ask book: Propose BUY_QX (quAmount 150,000, shares 100, limit 1,000), both vote, Tally, Execute | Execute OK, acquired 0, refunded 50,000; holding `shares` 0, `pendingShares` 100, `costBasis` 100,000, `qxBidPrice` 1,000, `qxBidTick` = the Execute tick; `GetSource.pendingQx` 100; ALICE cost 60,000, BOB 40,000; QX shows 100 @ 1,000 owned by QPOOL |
| O2 | **K8.2** A second pool's BUY_QX on the same asset while the bid rests | accepted: both bids rest, the ledger lists two bids (suite T); only a ninth bid is refused (QX_BID_PENDING) |
| O3 | CREATOR calls CancelQxBid(A, H), no QU attached | OK, cancelledShares 100, refunded 100,000, pendingLeft 0; balance +100,000; pool A `uncommittedTotal` +100,000; `globalUnattributed` unchanged; ALICE 600,000, BOB 400,000, committed 0; holding and ledger gone; no QPOOL bid on QX. Pool B's own bid (placed before or after) is unaffected; the freed holding slot can be taken again |
| O4 | OUTSIDER (no seat), CAROL (member of pool B only), DAVE (creator of pool B) call CancelQxBid(A, H) before the age limit, attaching 777 QU once | all three: NOT_AUTHORIZED (22), the 777 returns, nothing changes. At age = limit still 22. At age = limit + 1 OUTSIDER succeeds: cancelledShares 100, refunded 100,000, OUTSIDER's own balance unchanged, the refund lands on ALICE (+60,000) and BOB (+40,000) |
| O5 | Wrong pool for the holding; holdingId out of range; the same cancel twice | NOT_FOUND (3); INVALID_INPUT (1); NOT_FOUND after the first success |
| O6 | **K8.1** A funder who is not an operator, a member who joined after the bid, and an admin who owns no seat | the funder and the late member are refused (NOT_AUTHORIZED) until the age limit; the admin cancels at once; whoever owns the seats receives the refund share, the caller never does |
| O7 | Partial fill then cancel: OUTSIDER lists 60 @ 1,000; pool A bids quAmount 200,000, shares 100, limit 1,500 (acquired 60, refunded 80,000, pending 40); CAROL lists 10 @ 1,400 (fills 10 of the resting bid); BOB cancels | cancelledShares 30, refunded 45,000; the 10 filled shares are credited by the same call: shares 70, `pendingShares` 0, `costBasis` 75,000; ALICE cost 45,000, BOB 30,000; any OfferPosition on a shrunk seat is withdrawn (offer count -1) |
| O8 | The bid filled completely and nobody reconciled: CancelQxBid | NO_PENDING_BID (40), cancelledShares 0, attached QU returned, state byte for byte unchanged (the holding still shows `pendingShares` 100). ReconcileHolding then credits the fill; a later CancelQxBid returns 40 again |
| O9 | QX holds fewer shares at that price than the books say (hand-corrupted or removed by other means) | EXTERNAL_CALL (11), nothing written, no QU moved (optional: needs a controlled state) |
| O10 | Sold-out holding with a resting bid: after O7, sell the 60 shares through SELL_QX, then cancel | the holding retires (not freed), claims are paid 60:40 within 1 QU, the second ClaimPayout frees the slot and the pool's committed capital |
| O11 | CancelQxBid on an IPO holding | WRONG_STATUS (7) |
| O12 | Contract-share ledger (a dividend-paying contract above index 30 whose name is paired) with a resting bid and a dividend in escrow | cancel ends in the same books as ReconcileHolding followed by cancel; a later dividend splits by the shrunk costs. SETTLEMENT_PENDING (24) if an unsettled IPO holding exists on that ledger |
| O13 | Re-placing a bid right after a cancel | KNOWN: a pool with a PASSED BUY_QX proposal can Execute in the next transaction; record how long a hostile pool can keep a ledger blocked (each block lasts at most the age limit, each proposal needs its own vote) |
| O14 | CancelQxBid when BuyAndBurnQpay has bid on the same asset and price | KNOWN: QX keeps one order per (QPOOL, asset, price); record what the cancel removes and what BuyAndBurnQpay then finds |
| O15 | Someone donates shares of the asset while a bid rests | KNOWN: ReconcileHolding counts them as fills; CancelQxBid may answer NO_PENDING_BID while the order still rests on QX; record it |

### P. IPO share names read from the chain (NEW-K8)
No name is typed. END_EPOCH (before the core finishes the IPOs) queues every contract index from 31 up whose book is full; BEGIN_EPOCH reads the new zero-issuer names, pairs one name with one index (by count when there is one,
by a fingerprint of the bidders when there are several), and quarantines the epoch if nothing matches exactly. Read the result with `GetNameRegistry{index, assetName}`:
`nameState` 0 unknown, 1 table, 2 paired, 3 known and never paired; `nameSource` 0 none, 1 table, 2 registry. Run I13 after every case.
| Id | Case | Expected |
|---|---|---|
| P1 | Baseline: before QPOOL's first epoch change, at least one zero-issuer name outside the embedded table already exists. After QPOOL's first BEGIN_EPOCH read `GetNameRegistry{0, 0}` and `{0, that name}`; Propose BUY_QX and DepositHoldingAsset for (its index, that name) | `baselineDone` 1, that name `nameState` 3, `mismatches` 0, `paired` 0; both procedures return NAME_PENDING (41) with nothing written. A table contract (QX at index 1) shows `nameState` 1 |
| P2 | Propose BUY_IPO with a non-zero asset name, then with name 0 | first INVALID_INPUT (no proposal, QU back); second accepted; after Execute `GetHolding.assetName` is 0 and `ipoContractIndex` = the target |
| P3 | One IPO with a full book. Note the explorer's list of zero-issuer assets before and after the epoch change | exactly one new name; `GetNameRegistry{T, that name}`: `name` = it, `nameSource` 2, `nameState` 2, `nameIndex` = T, `paired` +1, `pairEpoch` = the new epoch, `queueLength` 0, `overflow` 0 |
| P4 | Two IPOs finishing in one epoch with DIFFERENT bidder sets (for example 31 and 32: wallet A takes 400 slots of 31 and 276 of 32, wallet B the rest of each; QPOOL's own seats count) | each index gets its own name, `paired` +2, `mismatches` unchanged, whatever order the explorer or the node's reply lists the two names in. Record the slot numbers of the two issuance records before and after the epoch change (the core rebuilds its asset table at the epoch end; the order may be reversed) |
| P5 | A failed IPO and a winning IPO in one epoch (31's book not full, 32's full) | only 32 is queued and paired: `paired` +1, the name for 31 is 0. A holding that bid in 31 settles with its whole budget back and no name; a holding with seats in 32 settles and `GetHolding.assetName` = 32's name |
| P6 | Quarantine: two IPOs in one epoch with IDENTICAL bidder sets (QPOOL bids the same budget in both and gets the same seats, one other wallet takes all remaining slots of both) | `mismatches` +1, `lastMismatchEpoch` = the new epoch, `lastMismatchNew` 2, `lastMismatchQueued` 2, `paired` unchanged, both names `nameState` 3. SettleIpo on a holding with seats returns NAME_PENDING (41) and writes nothing; capital stays on the books; holdings that won no seat settle |
| P7 | Recovery: a later single IPO (contract 33) finishes in the next epoch | its name is paired; `mismatches` unchanged; the quarantined names stay "known, never paired" for ever |
| P8 | Purchases after pairing: Propose BUY_QX and DepositHoldingAsset for (T, its paired name); then for T with ANOTHER paired contract's name; then for a name the registry never saw | first accepted; second CONTRACT_MISMATCH (28); third NAME_PENDING (41) |
| P9 | Table contracts (1..30) | unchanged: pair checked against the embedded table; a wrong name at a table index returns CONTRACT_MISMATCH (28) |
| P10 | Restart idempotency (see N6) | registry and counters identical before and after |
| P11 | KNOWN, not testable on a small testnet | the queue overflows past 16 IPOs in one epoch; the walk bound of 1,024 zero-issuer issuances. Record that they were not run |

### Q. Settlement at the epoch start (NEW-K8)
`BEGIN_EPOCH` first pairs names, then settles holdings of ready targets (snapshot taken, epoch over), at most `QPOOL_AUTO_SETTLE_MAX` per epoch, from a persistent cursor over the table of unsettled IPO holdings.
A refused holding writes nothing and does not stop the loop. `GetStats` shows `readyUnsettled`, `autoSettleEpoch`, `autoSettleDone`, `autoSettleSkipped`, `autoSettleTotal`. Run I16 after every case.
| Id | Case | Expected |
|---|---|---|
| Q1 | Three pools bid on one target (distinct creators, different opening prices); epoch change | after the epoch change `autoSettleEpoch` = the new epoch, `autoSettleDone` 3, `autoSettleSkipped` 0, `readyUnsettled` 0; each holding `ipoSettled` 1, shares > 0, `assetName` = the paired name; `GetSource.unsettled` 0 |
| Q2 | More ready holdings than the bound (build with `QPOOL_AUTO_SETTLE_MAX` = 2 and five holdings on one target) | first epoch start: `autoSettleDone` 2, `readyUnsettled` 3, the other three untouched; a dividend now is parked (`GetSource.escrow` > 0); a manual SettleIpo of an unsettled holding returns OK and `readyUnsettled` drops by 1; the next epoch starts with `autoSettleDone` 2 (or the rest); every unsettled holding is reached within ceil(ready / bound) epochs |
| Q3 | A failed IPO | the holding is retired automatically: `GetHolding.found` 0 or `active` 0, `uncommittedTotal` back to its value before Execute, `committedTotal` 0, no ledger left, no name paired |
| Q4 | Manual SettleIpo after the automatic one (a different wallet, 0 QU attached, then 1 QU attached) | IPO_NOT_ACTIVE (18) each time, shares 0, returnedToMembers 0; the 1 QU is returned; GetStats, GetPool and GetSource unchanged |
| Q5 | Dividend of the new contract after the last holding settled; ClaimPayout by members who paid | `accHigh/accLow` rise, `escrow` 0, `unsettled` 0; `GetPosition.owed` > 0 for members with cost units; ClaimPayout pays less the 1 percent fee; voters who rejected or abstained get NOT_MEMBER |
| Q6 | A holding that is refused (NAME_PENDING after a quarantine) sits in front of ready holdings | it counts in `autoSettleSkipped`, the others settle in the same pass (the cursor moves past it); it is retried once per rotation and never blocks the rest |
| Q7 | Equivalence: the same scenario settled automatically and by hand (two runs, or the model of `ipo_reference_model.py`) | per-member shares, costs and returned QU identical (within 1 QU per member) |
| Q8 | A restart at the epoch start | see N6 |

### R. The K7 small fixes (NEW-K7)
| Id | Case | Expected |
|---|---|---|
| R1 | Proposal scan: M1, M2, M3 each Propose in order; M3 and M1 Propose again; a member with no live proposal Proposes invalid input; after the vote window Tally with no votes, then M1 Proposes again | ids 0, 1, 2 (lowest free slot); M3 and M1 get PROPOSAL_PENDING (20); the invalid call gets INVALID_INPUT and `proposalCount` is unchanged; M1 reuses slot 0 after the proposal ended |
| R2 | QPAYHUB leg with a contract at the index, and without one | see I9 and I12 |
| R3 | IPO_PRICE_TAKEN: proposal FAILED, votes released | see G25 |
| R4 | BuyAndBurnQpay hand-over fee measured | see J3: `potLeft = pot - spent - measured hand-over`, balance = sum of buckets |
| R5 | `GetPosition.offeredShares` is live | see H8; layouts of `GetPosition_output` and `BuyPosition_input` are unchanged from K6 |
| R6 | State layout | `Offer` 24 bytes, `SourceLedger` 120 bytes; a fresh deployment is needed (about +8,568 bytes against K7 for the K8 registry and settle fields) |

### T. Several resting QX bids on one token (NEW-K8.2)
A ledger lists up to 8 resting bids, highest price first and, at one price, the earlier bid first. The silently arrived shares go to the bids in that order (QX fills the highest bid first); `ReconcileHolding` by anyone credits all of them; before a new bid is placed the fills not yet credited are credited to the bids that already rest. The dividends the hook sets aside for those shares are split in the same order (`Holding.qxEscrow`). Run I14 and I15 around every case.
| Id | Case | Expected |
|---|---|---|
| T1 | Pool A bids 100 @ 1,000, pool B bids 100 @ 1,100, both rest | both accepted; ledger lists B then A; `GetSource.pendingQx` 200; QU conserved |
| T2 | A seller sells 60 into the book, then anyone calls ReconcileHolding on either holding | the 60 go to B (higher price): B shares 60 pending 40, A shares 0 pending 100 |
| T3 | The seller sells 100 more | B's last 40, then 60 of A's: B shares 100 pending 0 (leaves the list), A shares 60 pending 40 |
| T4 | Two pools bid at the SAME price (A first); the seller sells 150 | QX shows one merged order of 200; A gets 100 (leaves the list), B 50 pending 50; B's creator cancels exactly the 50 that still rest |
| T5 | A bid is partly filled and unreconciled, then a pool places a HIGHER bid | Execute credits the earlier bid first; the new higher bid takes none of the earlier fills |
| T6 | Dividends: A bids 100 @ 1,000, B bids 100 @ 1,100 on a dividend-paying contract's shares; 60 fill; the contract pays 1,000 per share (60,000 gross) | all 59,400 net are set aside for B's holding (A's `qxEscrow` 0); after the reconcile B is owed 59,400 (within 3), A 0 |
| T7 | 100 more shares fill, then the contract pays 500 per share before anybody reconciles | 29,700 is credited to B's recognised 60 shares; the 49,500 set aside splits B 19,800 (its last 40) and A 29,700 (its 60); after the reconcile A is owed 29,700 |
| T8 | Capacity: 8 pools rest bids on one token, a ninth pool's Propose and Execute | the ninth is refused with QX_BID_PENDING before any state write; a cancel or a full fill makes room |
| T9 | **K8.3 (was KNOWN in K8.2)** someone donates a share of the token while two bids rest | the donated share is NOT credited as a fill: what filled is what QX's bid book no longer lists (and never more than the universe holds beyond the recognised shares); after the real fills no order is left on QX and CancelQxBid says NO_PENDING_BID |
| T10 | Cancel the first-ranked bid while the second has fills | the cancel credits its own fills first (waterfall), QX removes only what still rests, the other bid keeps its rank and its fills |
| T11 | A SELL_QX of recognised shares while bids rest | unaffected; `recognized` and the unclaimed shares are what the waterfall uses |
| T12 | ReconcileHolding by a stranger on a holding of another pool | credits every bid of the ledger, never more than each one's pendingShares; idempotent |

### U. Resting SELL limit orders on QX (NEW-K8.3)
Proposal kind SELL_QX_LIMIT (7): `price` is the limit, `shares` the size (at most the holding's free `shares`), `quAmount` must be 0, `holdingId` the holding. Vote and tally as for any sale; an operator executes. Whatever the visible bids at or above the limit pay fills at once (credited from the balance difference, 1% to revenue, exactly as a SELL_QX); the remainder rests on QX at the limit price. Run I7, I14, I18, I19 around every case, and compare QX's `EntityAskOrders` for QPOOL with `GetHolding.askShares` / `GetSource.askTotal`.
Setup: token X on QX; pool A (creator CREATOR, ALICE 600,000, BOB 400,000) and pool B (creator DAVE, CAROL 300,000, DAVE 200,000); OUTSIDER (and a second outsider) with QU and X. Each pool first buys a holding of X at once (an outsider sells into the book at 900, the pool bids 1,000), so the shares are QX-managed. QX's trade fee is 0.3% (floor(price x shares x 0.003) + 1 per fill).
| Id | Case | Expected |
|---|---|---|
| U1 | Pool A holds 100 X. SELL_QX_LIMIT 100 @ 2,000 with no bid at or above 2,000 | Execute OK, `sold` 0, `proceeds` 0; GetHolding: `shares` 0, `askShares` 100, `qxBidPrice` 2,000; GetSource: `qxSide` 2, `askTotal` 100, `qxCount` 1, `recognized` without these shares; QX lists 100 @ 2,000 for QPOOL; no QU moved; I18, I19 |
| U2 | A bid of 40 @ 2,500 rests; SELL_QX_LIMIT 100 @ 2,000 | 40 fill at once at 2,500: `sold` 40, `proceeds` 98,703 (100,000 - QX fee 301 - 1% = 996); `askShares` 60; QX lists 60 @ 2,000; `globalUnattributed` unchanged |
| U3 | After U1 a buyer bids 2,000 for 30 | QPOOL's balance rises by 59,819 (60,000 - 181), `globalUnattributed` rises by the same; the holding is unchanged until someone reconciles. A stranger's ReconcileHolding: `askShares` 70, `GetPosition.owed` (all seats) within 3 QU of 59,193 (= 59,790 - 597; the fee bound is 180 + 30 shares = 210, QX really took 181), `globalUnattributed` 29 (the difference); a second ReconcileHolding writes nothing (byte for byte) |
| U4 | The buyer takes the last 70 and anybody reconciles | the holding is retired (`active` 0, claims payable), its ledger is gone, `restingAsks` falls to 0, `globalUnattributed` 0 (the remainder of the fee bounds went to revenue with the last ask); the total owed within 6 QU of 59,193 + 138,115 |
| U5 | Pools A and B each hold 100 X. A sells 100 @ 2,000, B sells 100 @ 1,800; a buyer bids 2,100 for 150 | ledger lists B first then A; QX fills B's 100 at 1,800 and 50 of A's at 2,000; QPOOL receives 179,459 + 99,699; one ReconcileHolding credits both: B retired, A `askShares` 50; owed B 177,567, A 98,654 (each within 3); `globalUnattributed` 148 (waits for the last ask) |
| U6 | The same pools both sell @ 2,000 (A first) | QX shows ONE order of 200; a buyer taking 150 in one fill: A's 100 are sold first (A retired), B keeps 50; QPOOL receives 299,099 (one fee); credits 298,950 split A 199,300 / B 99,650 (owed A 197,307, B 98,654); counter 149 |
| U7 | As U6 but two buyers take 75 each in two separate transactions before anybody reconciles | QPOOL receives two fees; the credit uses the fee BOUND (floor(gross x 0.3%) + shares sold) which is never below QX's real fees, so the credits are below the arrivals (the counter keeps the difference, it never goes negative); each seller is credited at most (shares - fills) QU less than QX paid. Record the counter |
| U8 | Partial fill, then CancelQxAsk by the creator | the sold part is credited first; the unsold part (70) is taken off QX and returns to `shares`; `askShares` 0, `qxBidPrice` 0, ledger `qxSide` 0; QX lists nothing; a stranger before 72 hours (strictly more) and a member without a role: NOT_AUTHORIZED; an admin at once; anybody after the age limit; a second cancel: NO_PENDING_ASK (43). A new SELL_QX_LIMIT on the freed shares works |
| U9 | Exclusivity: with a sell resting, pool B's Propose of a BUY_QX of X | Propose returns QX_SIDE_BUSY (42), nothing written. With a bid resting, a passed SELL_QX_LIMIT: Execute returns QX_SIDE_BUSY, the whole state is byte for byte unchanged |
| U10 | A holding with a sell resting gets a second SELL_QX_LIMIT passed | Execute returns QX_BID_PENDING (25), state unchanged. Propose with `quAmount` not 0, price 0, or shares above the free shares: INVALID_INPUT |
| U11 | Pool A rests a sell of 100 @ 2,000; pool B (holding of 50, QX-managed) sells at once (SELL_QX) 50 @ 1,500 into a bid of 20 @ 1,700 | B sells 20, cancels the other 30 and keeps them; A's order on QX (100 @ 2,000) is untouched; `globalUnattributed` 0 |
| U12 | Capacity: eight pools (one member each) rest sells of 10 @ 2,000 .. 2,007 on one token (placed in any order); a ninth | the list is ordered by price ascending whatever the order of placing; the ninth Execute returns QX_BID_PENDING; a buyer taking all 80 and ONE reconcile credits all eight (each retired), the counter exactly used up (one fill per price) |
| U13 | Dividends on the unsold shares (contract shares of a paying contract): A sells 100 @ 1,800, B sells 100 @ 2,000, the contract pays 1,000 per share, then a buyer takes 120, then it pays 500 per share | first payment (198,000 net): 99,000 set aside for each sell (`qxEscrow`); after the sale the 80 shares left are B's, so the second payment (39,600 net) is all B's; after a reconcile A is owed 99,000 + 177,567, B 138,600 + 39,462 (each within 4); A retired, B `askShares` 80; counter 118 |
| U14 | A SETTLED IPO holding (contract shares won in an IPO) rests a sell | same as U1 to U4 and U13: the dividend on the unsold shares is set aside for the holding, a fill is credited, a sold-out holding retires and its ledger goes (a ledger with an unsettled IPO holding refuses the sell with SETTLEMENT_PENDING) |
| U15 | A holding whose shares are QPOOL-managed (a deposit) or QSWAP-managed rests a sell | QPOOL pays the QX management hand-over fee (100 per source) out of its OWN revenue (`pendingRevenue` falls by it); no member is charged; MEASURE the balance change. When `pendingRevenue` is below the fee and the immediate fills do not pay it, Execute returns INSUFFICIENT_FEE (5) and writes nothing. NOT run in the local tests |
| U16 | Someone donates a share of the token to QPOOL while a sell rests, then a buyer takes the whole sell | the sold shares are credited all the same (QX's book, not the share count, says what sold), the holding retires and the ledger goes; no phantom sell stays booked |
| U17 | Seat market while a sell rests | GetPosition shows `holdingAskShares`; `offeredShares` is the free `shares` only; a seat can be OFFERED, but BuyPosition on a holding with `askShares` above 0 returns SETTLEMENT_PENDING (24) with the reward back and nothing written (a buyer cannot bind askShares, and anyone's ReconcileHolding turns a filled sell into proceeds of the seller before the purchase lands); after the sell is sold out or taken back the purchase works; a BuyPosition whose expectedShares was read before the sell was placed or cancelled returns OFFER_CHANGED |
| U18 | An older, dearer sell has unreconciled silent sales, then another pool places a CHEAPER sell | Execute credits the older sell first (it reconciles before listing the new one), so the cheaper sell is not handed the sales: check both holdings' `askShares` and owed |
| U19 | Two tokens each with a filled, unreconciled sell, reconcile one then the other | each is credited from `globalUnattributed`, the counter never wraps or goes negative, both end at 0 |
| U20 | Operators gone for 72 hours: a stranger cancels a stale sell | OK (strictly more than the limit); the shares are back in the holding |
| U21 | Many zero-fee fills: pool V's sell of 100 KOIN @ 1,000 has filled (99,699 in `globalUnattributed`), nobody reconciled; pool X sells 200 contract shares @ 1 and a buyer takes them one share at a time (each fill pays QPOOL 0) | after ReconcileHolding on X: X credited 0, `globalUnattributed` still 99,699; then V's reconcile credits 98,604 (within 3) and the counter is 0 |
| U22 | EvictSquat on a deposited holding that has free shares AND a sell resting | WRONG_STATUS, state byte for byte unchanged (without the sell the same call gets as far as the share transfer) |
| U23 | A pool holds the QPAY token (name 1497452625) and proposes SELL_QX_LIMIT on it | Propose returns INVALID_INPUT (BuyAndBurnQpay would trade against the pool's own ask) |
| U24 | Dividends are set aside (`qxEscrow` above 0) for the unsold shares of pool A's sell; pool B has a holding on the same ledger that rests nothing | a seat of A's holding cannot be bought (SETTLEMENT_PENDING); a seat of B's holding CAN be bought (A's earmarked dividends are not B's business; a seat trade of a listed holding counts its own earmark only) |
| U25 | A hostile pool keeps a sell resting on token X for ever (cancel after 72 hours, place again) | KNOWN: every BUY_QX of X by another pool is refused with QX_SIDE_BUSY meanwhile; record how long a competing mandate (48 hour execute window) can be blocked |

## 6. Multiples: randomized runs

After the scripted suites, run a seeded random campaign. Log the seed and every action so a failure can be replayed.

1. 3 pools x 3 to 6 members each, one IPO target live, one token on QX, one dividend payer.
2. Each step picks an action with these weights: Contribute 15, Withdraw 8, Propose 8 (any kind; BUY_IPO with asset name 0), Vote 12, Tally 6, Execute 6, ClaimPayout 8,
   OfferPosition 5, BuyPosition 5, CancelOffer 2, RebidIpo 8 (when an IPO is live), SettleIpo 2 (a backup: the epoch change settles), CancelQxBid 3 (valid and invalid callers, before and after the age limit),
   a dividend payment 6, a stray transfer 3, BuyAndBurnQpay 3, a deliberately invalid call 8, advance ticks 10, change epoch 2 (BEGIN_EPOCH pairs and settles).
3. Run invariants I1 to I12 after EVERY step. Stop at the first violation and keep the chain.
4. Run at least: 10 seeds x 400 steps, and one long soak over 5 or more epochs. Include seeds that force the same-tick ordering of
   two conflicting transactions.
5. Repeat the campaign once with a shrunken-capacity copy (L2) so the capacity paths are hit.

## 7. Audit exploit regression list (all must now be refused or harmless)

| Finding | Re-run on chain |
|---|---|
| C001 IPO bid size wrap | BUY_IPO and RebidIpo with huge prices, prices that wrap when multiplied by quantities up to 677; the core and the books must agree on what was spent |
| C002 duplicate roster | BuyPosition as a non-member, then Contribute: one roster entry; a BUY must not charge anyone twice |
| C003 vote recycling | vote on a SELL, move the seat to a sockpuppet, vote again: refused (SEAT_LOCKED) |
| C004, C005 settle order | G15 with outbids after the last rebid |
| C006 BUY vote binding | C8 and B4 |
| C007 dust deposits | non-creator DepositHoldingAsset refused; the creator is limited to 32 deposits |
| C010 resting bid never cancellable | suite O (CancelQxBid, NEW-K7) |
| C012 duplicate IPO price | G6 |
| C013 table exhaustion | L1 with a hostile pool trying to fill shared tables |
| C015 dead mandate | C9 with rounding at the edges |
| C017 join fee bypass | every entry point (Contribute, BuyPosition, deposit) charges it once |
| C018 creator closes exits | switching to private never blocks members' withdrawals or seat trades |
| C019 stale mandate | Execute window starts where the vote ends |
| C020 early settlement | SettleIpo before the snapshot / before the window closes (IPO_NO_SNAPSHOT, WINDOW_OPEN) |
| C033 offer replaced | H3 |
| C034 AMM sandwich | E6 |
| C038 ghost shares | a holding that retires must leave no recognised shares |
| C100 retired holding offers | offers and purchases on a retired holding refused |

### Findings of the review of the automated report (REVIEW_OF_REPORT_K6.md): re-run on chain
| Finding | Re-run on chain |
|---|---|
| R1 devkit constants must not ship | run `release_gate.py` on the production file before any real deployment; it must pass and the devkit file must never be deployed |
| R2 payer-index squatting | P1, P8: a contract above index 30 is trusted only through the registry, so a dust purchase can no longer bind a false (index, name) pair; the registry never changes |
| R3 IPO names wedge and poison | G21, G26, P2, P6: no typed name exists; a quarantine is the only way to get a holding that cannot settle (known gap) |
| R4 uncancellable QX bids; one bid blocks every other pool | suites O and T |
| R5 one-unit owner directs the rebid budget | G8, S3 (K8.1: only the creator and admins rebid; record the loss bound with an admin and a huge price) |
| R6 proposal scan | R1, M7 |
| R7 BUY vote snapshot (a stranger settling an IPO refund stamps `lastDepositTick`) | B4 and C8 with a settled IPO refund between Propose and Vote; with automatic settlement this happens at the epoch start, so run it once across an epoch change |
| A1 IPO name cross-claim | P2, P6, P8 |
| A2 BuyPosition stale backing | H3, H8 (the holding's identity is still not bound: a seller who retires the holding and re-deposits another asset with the same share count and re-offers can substitute; record it) |
| A3 reserve-drain levers | M1 to M4, M7, M8 |
| A5 roster fill, A6 retired-holding slots, A7 vote table fill, A8 SELL_QSWAP split managers | L1, A7, C10, E5 (accepted or documented gaps) |
| A9 hand-over fee booked as spent | J3, R4 |

## 8. Known gaps in this build: record, do not report as new

- Stray transfers write state (suite M3). Not fixed.
- External fees (SELL_QSWAP 100,000, QX 100) are fronted from the pooled balance and netted from the sale's proceeds; they are not debited from the pool's members first.
- **Resting QX bids (K8.2):** several pools rest bids on one token; QX merges QPOOL's bids at one price into one order, which QPOOL serves earliest first (an internal rule, QX cannot tell the pools apart); BuyAndBurnQpay bids at ask levels and could merge with a pool's bid at the same price; (K8.3: what filled is read from QX's bid book, so a donated share is no longer credited as a fill). Resting SELL limit orders exist since K8.3 (suite U); the QX bids and sells of one token cannot rest at the same time (QX_SIDE_BUSY).
- **A holding that won seats on a quarantined index cannot settle** (NAME_PENDING for ever); its capital stays on the books and that contract's dividends stay parked. There is no write-off path (the K4b idea). Only two IPOs in one epoch with identical bidder sets cause it.
- **Contracts above index 30 whose shares existed before QPOOL's first epoch** (and any contract whose IPO ends in the epoch change of QPOOL's own IPO) are "known, never paired": their shares cannot be bought, deposited or settled through QPOOL. QLOAN (index 31) is one if it IPO'd before QPOOL.
- Settlement at the epoch start is bounded by `QPOOL_AUTO_SETTLE_MAX`; a larger backlog takes ceil(ready / bound) epochs unless someone calls SettleIpo. Its cost is a guess (20 to 35 ms for 32 holdings), not a measurement.
- The core's epoch-end order (END_EPOCH, finishIPOs, assetsEndEpoch, BEGIN_EPOCH) and the order of the zero-issuer names were read in a local core, not on a node. The pairing does not depend on the order of the names.
- Operators are trusted: a creator or admin can rebid at any price with all of the holding's free funds (the burn variant of review item R5), and an admin acts for the pool until the creator replaces the list. A rebid can still be spammed in small amounts, now only by an operator; no fee yet (M4).
- If the creator and every admin are unavailable, a passed mandate lapses unexecuted (nothing is spent) and an IPO rebid cannot happen; a resting QX bid can still be cancelled by anyone after 72 hours.
- A pool whose seats are the lowest in the book cannot grow its seat count by rebidding; the count converges to what its money buys at the new price (G5).
- Windows are counted in ticks and assume about one tick per second.
- The proposal kind ALLOCATE is still accepted but its balance has no source: executing it returns NOTHING_OWED; voting on it locks the voter's seat for the window.

## 9. Reporting

For each failure send: case id, build (testnet or production header), chain tick and epoch, the exact transactions with inputs,
return codes, the invariants broken, and the full read-backs (`GetStats`, `GetPool`, `GetMember`, `GetHolding`, `GetPosition`,
`GetIpoTarget`, `GetIpoBids`, `GetProposal`, `GetSource`) taken before and after. Add the node log lines around the failing
transaction. Keep the chain state available.

A results table per suite is enough for passes:

| Case | Variation | Result | Notes |
|---|---|---|---|

## 10. Negative and attacker cases (send each with ATTACKER and OUT1)

- Call every procedure with: zero amounts, maximum values, a price or quantity that overflows when multiplied, an id that does not
  exist, an input struct of the wrong size, a closed or expired proposal, a retired holding, a pool that does not exist.
- Call every procedure from a non-member and from a member of a different pool.
- Submit the same transaction twice in one tick and in consecutive ticks.
- Try to make one pool spend another pool's money: through a rebid, a settlement, a claim, a sale.
- Try to move the QPAY pot: via END_EPOCH, via a stray transfer, via BuyAndBurnQpay with a crafted order book.
- Try to make `globalUnattributed` rise permanently.
- Try to claim from a ledger before it is credited, and after retirement.
- For every refusal, repeat the reward-refund and no-state-change checks of section 4.

## G+. Competitive IPO battery: 3 to 4 pools plus outsiders (run this BEFORE suite G)

One pool filling the book alone proves very little. Real IPOs are decided by several pools and outsiders outbidding each other
in the last stretch. This battery runs 4 pools and 2 or 3 outsiders in the same IPO epoch, with rebids, collisions, refusals,
pools that are wiped out, a failed IPO, and a last-minute snipe.

**Tools in this folder.** `ipo_reference_model.py` is a small model of the core's bid rules and QPOOL's IPO accounting. It prints,
step by step, the EXPECTED refusal code or result, the expected core book, each pool's books (free funds, at-risk, seats listed vs
seats really in the book) and the final settlement. `ipo-war-expected/` holds its output for scripted wars W1 to W6 and for random
wars (seeds 1 to 8, 80 steps). Regenerate any random war with `python3 ipo_reference_model.py random SEED STEPS`.
The model is an aid, not an authority: the node's real book is the truth. A mismatch means "investigate", and the report should
say whether the model or the contract was wrong.

**K8 differences for the wars.** Every BUY_IPO proposal carries asset name 0. After the epoch change the holdings settle themselves (up to `QPOOL_AUTO_SETTLE_MAX`); a manual SettleIpo of a settled holding returns IPO_NOT_ACTIVE (18). The expected per-pool results of the wars below do not change. Targets must be contracts above index 30 whose IPO ends after QPOOL's first epoch (section 1).

**Setup for one IPO epoch (reuse it for several wars, an IPO epoch costs real time).**
- Two IPO targets T1 and T2 in the SAME IPO epoch (T2 is for the failed-IPO war and for settling in the opposite order).
- Pools A, B, C, D, each with at least 3 members and uneven costs (for example 60/30/10), each with the war's budget as
  `uncommitted` BEFORE its BUY_IPO proposal. All members vote yes. Schedule the five proposals so every Execute lands before the
  outsiders fill the book. The IPO vote is 48 ticks and the execute window 96: write a tick table first.
- Outsider wallets OUT1, OUT2, OUT3 with at least 5,000,000 QU each. A wallet IPO bid transaction must be the exact 16 byte
  `ContractIPOBid` layout (price 8 bytes, quantity 2 bytes, padded to 16); a 10 byte payload is silently ignored as a plain transfer.
- Pool wallets that call RebidIpo must be the pool's creator or one of its admins (K8.1). Alternate between them, and include seat owners who are not operators (refused).
- Leave at least 5 to 10 ticks between steps so each effect can be read. Never send two steps of a war in the same tick unless the
  case says so.

**Read after EVERY step.** The core book (the node's contract-IPO request: 676 prices and bidder keys); for every pool `GetIpoBids`
and `GetHolding`; `GetStats` for `ipoRefundEscrow`; the contract balance. Then check:
1. the return code equals the model's (a refusal must leave the state clean: compare reads before and after, reward returned);
2. per pool, `free + atRisk = costBasis` (I4);
3. seats listed by the contract versus seats really in the book: listed may exceed the real count only for seats pushed out since the
   pool's last reconcile (outsider bids, or the pool's own last rebid); after a successful rebid, everything pushed out earlier is
   freed and the listed counts equal the real counts except for the seats THAT rebid pushed out of the pool's own book;
4. `ipoRefundEscrow` rose by exactly the refunds the model shows for pools, and fell by exactly what pools drew;
5. no pool ever drew another pool's money (compare each pool's `free` with the model).

### W1: four pools, three outsiders, escalating war (the flagship; every step with its expected result)
Budgets and opening prices: A 200,000 at 2,000; B 150,000 at 1,500; C 120,000 at 1,200; D 90,000 at 900 (100 seats each, all-in).
```
  1. A Execute: budget 200,000 at opening 2,000 -> 100 seats, free 0
  2. B Execute: budget 150,000 at opening 1,500 -> 100 seats, free 0
  3. C Execute: budget 120,000 at opening 1,200 -> 100 seats, free 0
  4. D Execute: budget 90,000 at opening 900 -> 100 seats, free 0
  5. OUT1 bids 276 @ 1,800 -> refunds in the same transaction: no seat pushed out
  6. OUT2 bids 30 @ 1,000 -> refunds in the same transaction: D +27,000
  7. D RebidIpo @ 1,001 -> OK, 26 seats (cost 26,026); pushed out: none to others; own seats pushed out refund 23,400 (freed at the NEXT reconcile); free now 974
  8. D RebidIpo @ 1,001 -> OK, 24 seats (cost 24,024); pushed out: none to others; own seats pushed out refund 21,600 (freed at the NEXT reconcile); free now 350
  9. C RebidIpo @ 1,001 -> REFUSED IPO_PRICE_TAKEN; free 0
 10. B RebidIpo @ 1,550 -> REFUSED INSUFFICIENT_BALANCE; free 0
 11. OUT3 bids 60 @ 1,300 -> refunds in the same transaction: D +28,010, OUT2 +30,000
 12. D RebidIpo @ 1,001 -> REFUSED PRICE_TOO_LOW; free 350
 13. C RebidIpo @ 1,301 -> REFUSED INSUFFICIENT_BALANCE; free 0
 14. C RebidIpo @ 1,301 -> REFUSED INSUFFICIENT_BALANCE; free 0
 15. OUT2 bids 40 @ 1,600 -> refunds in the same transaction: D +40,040
 16. D RebidIpo @ 1,700 -> OK, 52 seats (cost 88,400); pushed out: C +62,400; own seats pushed out refund 0 (freed at the NEXT reconcile); free now 1,600
 17. C RebidIpo @ 1,650 -> OK, 37 seats (cost 61,050); pushed out: none to others; own seats pushed out refund 44,400 (freed at the NEXT reconcile); free now 1,350
 18. B RebidIpo @ 1,620 -> REFUSED INSUFFICIENT_BALANCE; free 0
 19. OUT1 bids 50 @ 1,900 -> refunds in the same transaction: C +13,200, OUT3 +50,700
 20. B RebidIpo @ 1,901 -> REFUSED INSUFFICIENT_BALANCE; free 0
 21. C RebidIpo @ 1,651 -> OK, 35 seats (cost 57,785); pushed out: B +21,000, OUT3 +27,300; own seats pushed out refund 0 (freed at the NEXT reconcile); free now 1,165
 22. D RebidIpo @ 1,701 -> REFUSED INSUFFICIENT_BALANCE; free 1,600
```
End of epoch: final price 1,500. A 100 seats, cost basis 150,000, returned 50,000. B 86, 129,000, 21,000. C 72, 108,000, 12,000.
D 52, 78,000, 12,000. OUT1 326 seats, OUT2 40 seats, OUT3 none. Pools' cost basis plus returned = 560,000 = their budgets.
Things this war proves: refunds for seats pushed out arrive in the outsider's own transaction; a pool pushes its OWN seats out when it
rebids from the bottom; a price owned by another holding is refused even when the caller has no money; a tie with the book minimum
is refused; a pool with no free funds is refused; seats shrink as prices rise (D ends with 52 seats for 90,000).

### The other scripted wars (steps and expected books in `ipo-war-expected/`)
| War | Idea | Final price | Expected outcome |
|---|---|---|---|
| W2 | W1's end state settled by a mix of automatic and manual settlement (a build with `QPOOL_AUTO_SETTLE_MAX` 1 or 2) | 1,500 | every mix gives W1's per-pool results (the result depends only on the final book). Use T2 with a mirrored war to settle the rest in the opposite order |
| W3 | three pools, book never fills (625 seats) | 0 (the IPO FAILS) | everything returned: A 300,000, B 200,000, C 100,000; holdings retire with no shares |
| W4 | a pool is wiped out and comes back | 1,100 | A 100 seats (110,000 / 90,000 returned), B 100 (110,000 / 40,000), C 100 (110,000 / 10,000), D 81 (89,100 / 900) |
| W5 | one outsider bid hurts B, C and D together; three pools race for the same price; the top pool has no free funds and is wiped out | 1,050 | A 0 seats, 100,000 returned; B, C, D 90 seats each (94,500 / 5,500). Also opening at a price another holding owns is refused. Run the race twice, with the three rebids in opposite orders |
| W6 | an outsider snipes 250 slots at 3,000 at the end; the pools rebid at about 3,001 | 1,800 | A 100 seats (180,000 / 20,000), B 49 (88,200 / 61,800), C 39 (70,200 / 49,800), D 29 (52,200 / 37,800); OUT2 pays only the final price on its 250 seats |

### W7: random wars (the "multiples and varieties" part)
Take `ipo-war-expected/random-seedN-80steps.txt` (N = 1..8) or generate more seeds. Replay every step on the chain in order, one step
per few ticks, and compare each step's accept or refuse code and the books, then the settlement. Run at least 8 seeds on at least two
IPO epochs. Keep the seed and the full action list in the report. Each seed contains many refusals by design (price taken, too low,
no money); a refusal that the chain accepts, or an acceptance that it refuses, is a finding.

### Variations to apply to every war
- Members inside each pool: 3 with costs 60/30/10, one whale with 5 dust members, equal thirds. After settlement each member's
  returned amount must equal the pool's returned amount times the member's cost share, floor, within 1 QU per member, and the
  members must sum to the pool total.
- Who calls RebidIpo: the creator and each admin; once a seat owner who is not an operator and once a stranger (both must be refused, NOT_AUTHORIZED).
- Timing: rebids in the last 20 ticks of the epoch, an outsider bid in the same tick as a rebid (the order of the node decides:
  record it and compare with the model for that order), a rebid on the very last tick.
- A second war on T2 with the same pools: a pool may hold one holding per target; check the 8 IPO holdings per pool limit and that
  prices are unique per target, not across targets.
- Reserve: repeat one war with the contract's fee reserve low but positive; record the drain of each rebid.
- Settle the holdings the epoch start leaves over in different orders, by a keeper, by a member, before and after the other pools settle (needs a build with a small `QPOOL_AUTO_SETTLE_MAX`).
- Dividends of the new contract while holdings are unsettled: parked until the last holding settles.

### Pass criteria for the battery
Every step matches the model (or the difference is explained and filed), every invariant holds after every step, settlement results
match for every war and every settle order, no pool is ever charged for another pool's seats, the sum of every pool's cost basis and
returned amount equals its budget, and no refusal changes state. Record any mismatch between the model and the chain with both books.

### What the model does not cover
Tick timing, the fee reserve, the refund pot clamp, the physical balance refusal (BOOKS_INCONSISTENT), per-member splits, and
dividends. It also assumes the registry frees a price when a pool's reconcile finds no seats left at it.

## D+/E+/F+. Trading and dividends battery: several pools, real counterparties (run BEFORE suites D, E and F)

Single-pool, single-trade checks are not enough. Here 3 to 4 pools trade the SAME assets against the SAME books and pools, with
market makers on the other side, and dividends from several payers reach several pools with several members each.

**Tools.** `trading_reference_model.py` models QX matching, QSWAP's swap formula and QPOOL's dividend and proceeds accounting;
`trading-expected/t1.txt` to `t5.txt` hold its output. Fees: QX charges 0.3 percent (`tradeFee` 3,000,000 billionths, taken from the SELLER's proceeds as floor(value x tradeFee / 1e9) + 1; confirm with
`QX.Fees`); QSWAP's flat sell fee is 100,000 QU. Parameters still to be READ from the live contracts and fed to the model before comparing:
QSWAP `swapFeeRate` (basis points; the examples use 30 as an illustration only) and the hand-over cost of a QX sale (0 when the shares already
sit under QX, 100 or 200 when QPOOL must hand them over: MEASURE it and report it). For QSWAP, call its own `QuoteExactQuInput`, `QuoteExactQuOutput`,
`QuoteExactAssetInput`, `QuoteExactAssetOutput` as the oracle; the model is a cross-check. As before, a mismatch means "investigate".

**Setup.** Tokens TOK1 (issuer ISSUER1) and TOK2 (issuer ISSUER2) issued on QX; a market maker MM1 and MM2 holding plenty of both
and QU; a QSWAP pool for TOK1 with liquidity from an LP wallet; a contract that pays dividends with `distributeDividends` and whose
shares are tradable on QX (for contract shares); pools A, B, C, D each with 2 to 3 members and uneven balances; a KEEPER; the
IPO battery's wallets can be reused. Read the QX order book (`AssetAskOrders`, `AssetBidOrders`, `EntityAskOrders`) and the QSWAP
reserves after every step.

**Read after EVERY step.** Per pool `GetPool`, `GetMember`, `GetHolding` (shares, pendingShares, costBasis), `GetPosition` for every
member, `GetSource` for the payer's ledger (recognised shares, escrow, unsettled), `GetStats` (`globalUnattributed` must be 0 after
every call: QX and QSWAP payments made inside a call are handed back, never revenue), the contract balance, and the venue's own
state. Check invariants I1 to I7 and I12. Every refusal must return the reward and write nothing.

### D+1. Four pools buy the same token on QX (T1)
QX asks from MM: 100@1,000, 100@1,010, 100@1,020, 200@1,050. Pools: A members 60,000 / 40,000; B 70,000 / 30,000; C 90,000 / 60,000;
D 150,000. All members vote yes and each proposal passes before its Execute. Run Executes A, B, C, D in separate ticks.
The rule: quantity = min(voted shares, floor(budget / voted price)); the bid fills lowest ask first at each ask's own price; QX returns the
excess and the price improvement; the refund goes back to the buyers pro rata to what each was charged.
| Pool | Limit x shares, budget | Quantity | Fills | Spent | Refunds | Cost basis |
|---|---|---|---|---|---|---|
| A | 1,000 x 100, 100,000 | 100 | 100@1,000 | 100,000 | 0 | 100,000 |
| B | 1,015 x 100, 100,000 | 98 | 98@1,010 | 98,980 | 1,020 (530 excess + 490 improvement) | 98,980 |
| C | 1,050 x 150, 150,000 | 142 | 2@1,010, 100@1,020, 40@1,050 | 146,020 | 3,980 (900 + 3,080) | 146,020 |
| D | 1,050 x 150, 150,000 | 142 | 142@1,050 | 149,100 | 900 | 149,100 |
Asks left: 18@1,050. Each member's cost = assigned minus refund share, within 1 QU; refund and cost never go to a non-voter.
Second round (asks gone; members top up first): A bids 990 x 50 (budget 49,500) and C bids 995 x 40 (39,800). Nothing fills: both rest on QX
(A: 100 shares + 50 pending, cost basis 149,500; C: 142 + 40 pending, 185,820). MM then sells 90 at limit 990: C's 40@995 (fee 120) and A's
50@990 (fee 149) fill at the pool's own bid price, so no refund. The fills are invisible to QPOOL until `ReconcileHolding` recognises
them; check shares and pendingShares before and after.
Variations: limit below every ask; limit above every ask; budget larger than price x shares (excess refunded); budget smaller than one share;
a pool whose members do not all vote (only yes-voters charged); two pools executing in the SAME tick (record who got which asks);
a pool buying an asset another pool already holds; 256 bids ahead on the book; the order A,B,C,D reversed.

### D+2. Three pools sell into the same bids (T2)
QX bids from MM: 150@1,100, 100@1,090, 100@1,070. Holdings (QX-managed shares, so hand-over cost 0): A 120 shares, members cost 72,000 / 48,000;
B 90 shares, cost 90,000; C 140 shares, costs 70,000 / 30,000 / 20,000. Proposals SELL_QX A at 1,090, B at 1,085, C at 1,080.
| Pool | Fills | Gross | QX fee | Received | Mgmt fee 1% | Credited to members |
|---|---|---|---|---|---|---|
| A | 120@1,100 | 132,000 | 397 | 131,603 | 1,316 | 130,287 (A1 78,172, A2 52,114) |
| B | 30@1,100, 60@1,090 | 98,400 | 297 | 98,103 | 981 | 97,122 |
| C | 40@1,090 | 43,600 | 131 | 43,469 | 434 | 43,035 (C1 25,103, C2 10,758, C3 7,172); 100 unsold shares kept |
Bids left: 100@1,070 (below every limit). Credits are by COST (costBasis does not change); shares fall by the amount sold.
Variations: deposited shares (hand-over 100 or 200: measure), a limit above every bid (nothing sells, shares kept), a sale larger
than the holding, more than 256 bids on the book, two pools selling in the same tick, selling a holding that is part of an unsettled
IPO ledger (SETTLEMENT_PENDING), SELL_QX of a contract share, proposals with min proceeds at or below the fixed costs (refused at Propose).

### E+1. Four pools trade against ONE QSWAP pool in sequence (T4)
Illustration: swapFeeRate 30, reserves 1,000,000 QU / 10,000 TOK1. A buys with 100,000 QU and gets 906 TOK1 (average 110.375), B gets 755,
C gets 639; the pool price moves from 100 to about 168.8. D then sells 800 TOK1 and QSWAP pays 122,020 QU; QPOOL credits 122,020 minus the
flat 100,000 fee = 22,020, management fee 220. Check on the chain with QSWAP's own quote functions BEFORE each Execute and compare.
Cases: min-out set to the first quote makes B and C fail their bound (QU returned, nothing written); min-out below the real output
passes; SELL with minimum proceeds at or below 100,100 is refused at Propose; a SELL where QSWAP sells nothing returns the fee;
an attacker trade in the same pool between Propose and Execute (only the vote-time min-out protects: record the loss); two pools
swapping in the same tick; a pool that does not exist; an empty pool; liquidity too small for the amount; shares under QX management
being handed to QSWAP (hand-over cost 100); contract shares must be refused for QSWAP; assets deposited by the creator then sold.
Cross-venue: buy on QX and sell on QSWAP, buy on QSWAP and sell on QX, the same holding sold in two steps on two venues.

### F+. Dividends reaching several pools and members
Split rule: management fee 1 percent (floor) to revenue; the net splits by recognised shares between holdings, then by cost between members;
rounding is floor, at most 1 QU per member. The numbers below come from `trading-expected/t5.txt`.
| Step | Event | Expected |
|---|---|---|
| 1 | ISSUER1 pays 100,000. Holding A 100 shares (costs 60/30/10), holding B 300 shares | fee 1,000; net 99,000; A 24,750 (A1 14,850, A2 7,425, A3 2,475); B 74,250 |
| 2 | A1 sells half of his seat (30 cost units) to A4; ISSUER1 pays 10,001 | fee 100; net 9,901; A gets 2,475.25 (members 742 / 742 / 247 / 742); B 7,425.75 |
| 3 | B sells 100 of its 300 shares on QX; ISSUER1 pays 90,000 | recognised shares fall to 300; fee 900; net 89,100; shares recognised change the split |
| 4 | Pool C buys 200 shares on QX; ISSUER1 pays 50,000 | C earns only payments made AFTER its shares are recognised |
| 5 | 1,000 payments of 1 QU | no value created: the sum of all claims never exceeds 1,000 QU, each member's error below 1 QU |
| 6 | A contract pays with `distributeDividends` (perShare 150, QPOOL holds 300 shares of it) | 45,000 arrives; fee 450; net 44,550 |
| 3a | A dividend arrives while a BUY_QX fill is unreconciled (T3: bought 100, only 40 recognised; pays 10,000) | fee 100; net 9,900; credited now 3,960; escrow 5,940 released at ReconcileHolding |
Variations: payers of every shape (a token issuer paying by plain transfer, a contract using `distributeDividends`, a contract paying by
`qpi.transfer` from its own id: this last shape is NOT matched to pools and becomes QPOOL revenue by the owner's decision; record it as an
expected behaviour and flag it); the same asset in 4 pools with different shares; members joining and leaving between payments; claiming
before and after each payment, in different orders, by the member and by a keeper; a pool that sold everything; a payment while the IPO
ledger of that contract is unsettled (parked); a payer binding squatted by a dust holding and recovered with EvictSquat; two payers for
one pool; a payment of 1 QU, 99 QU, exactly 100 QU, a huge amount; 256 members in one holding; dividends in kind (not supported: record).

### Pass criteria for D+, E+ and F+
Every fill, refund, fee, credit and claim equals the model or the venue's own quote within 1 QU per member; every pool's books and the
contract balance reconcile after every step (I1 to I7, I12); `globalUnattributed` is 0 after every call; QX and QSWAP payments never
appear as revenue; refusals leave state untouched; no pool is charged for another pool's trade.

---

# Appendix: reference generated from the K8.3 contract source (QPool-K8.3.h)

## A1. Procedures (user procedures, id)

| Id | Name |
|---|---|
| 1 | CreatePool |
| 2 | Contribute |
| 3 | WithdrawUncommitted |
| 4 | Propose |
| 5 | Vote |
| 6 | Tally |
| 7 | Execute |
| 8 | ReconcileHolding |
| 9 | ClaimPayout |
| 10 | OfferPosition |
| 11 | CancelOffer |
| 12 | BuyPosition |
| 13 | RebidIpo |
| 14 | SettleIpo |
| 16 | DepositHoldingAsset |
| 17 | SetAdmission |
| 18 | SetPermissioned |
| 19 | ReleaseManagedShares |
| 20 | CloseEmptyPool |
| 21 | EvictSquat |
| 22 | BuyAndBurnQpay |
| 23 | CancelQxBid |
| 24 | SetPoolAdmins |
| 25 | CancelQxAsk |

Id 15 is reserved (the deleted ReconcileIpo). Never call it. System procedures: INITIALIZE, BEGIN_EPOCH (pairs IPO names, then settles ready IPO holdings), END_EPOCH, POST_INCOMING_TRANSFER, PRE_ACQUIRE_SHARES.

## A2. Functions (read-only, id)

| Id | Name |
|---|---|
| 1 | GetPool |
| 2 | GetMember |
| 3 | GetHolding |
| 4 | GetPosition |
| 5 | GetProposal |
| 6 | GetStats |
| 7 | GetSource |
| 8 | GetIpoTarget |
| 9 | GetIpoBids |
| 10 | GetNameRegistry |

`GetPool` ends with admin0, admin1, admin2 (K8.1). `GetStats` returns 26 values (28 since K8.3); the last five of the 26 are readyUnsettled, autoSettleEpoch, autoSettleDone, autoSettleSkipped, autoSettleTotal, and K8.3 appends **restingAsks, restingBids**. K8.3 appends: `GetHolding` ... qxBidPrice, qxBidTick (the resting order of either side), **askShares**; `GetPosition` ... **holdingAskShares**; `GetSource` ... **qxCount, qxSide, askTotal**; `ReconcileHolding` returns **askShares** as well. `GetNameRegistry{index, assetName}` is unchanged (14 values).

## A3. Return codes

| Code | Name | Meaning |
|---|---|---|
| 1 | INVALID_INPUT |  |
| 2 | CAPACITY |  |
| 3 | NOT_FOUND |  |
| 4 | NOT_MEMBER |  |
| 5 | INSUFFICIENT_FEE |  |
| 6 | INSUFFICIENT_BALANCE |  |
| 7 | WRONG_STATUS |  |
| 8 | WINDOW_OPEN |  |
| 9 | WINDOW_CLOSED |  |
| 10 | ALREADY_VOTED |  |
| 11 | EXTERNAL_CALL |  |
| 12 | TRANSFER_FAILED |  |
| 13 | NOTHING_OWED |  |
| 14 | PRICE_TOO_HIGH |  |
| 15 | NO_OFFER |  |
| 16 | IPO_REJECTED |  |
| 17 | IPO_PRICE_TAKEN |  |
| 18 | IPO_NOT_ACTIVE |  |
| 19 | ABOVE_CEILING | unused: a BUY_IPO has no ceiling price (owner decision D1, final: the budget is the only limit); kept so numbering stays append-only |
| 20 | PROPOSAL_PENDING |  |
| 21 | NOT_ADMITTED |  |
| 22 | NOT_AUTHORIZED |  |
| 23 | SOURCE_CONFLICT | this payer is already bound to a different asset |
| 24 | SETTLEMENT_PENDING | dividends for this asset are parked awaiting settlement; also: the seats of an IPO holding that has not been settled yet cannot be offered or bought |
| 25 | QX_BID_PENDING | BUY_QX: this payer's ledger already lists QPOOL_MAX_QX_BIDS_PER_LEDGER resting bids (CancelQxBid takes one back; ReconcileHolding credits the fills) |
| 26 | DISABLED | unused since KIND_SELL_QX was repaired; kept so numbering stays append-only |
| 27 | QX_ONLY | contract shares can only be sold through QX, never swapped on QSWAP |
| 28 | CONTRACT_MISMATCH | the asset name and the paying contract's index do not belong together |
| 29 | CONTRACT_NOT_LIVE | the paying contract does not exist, or is still in its IPO |
| 30 | NO_FILL | SELL_QX: the visible bid book cannot pay the minimum at the limit price now |
| 31 | BOOKS_INCONSISTENT | defence in depth: a pool's records do not add up (a roster or balance defect); refused before anything moved |
| 32 | SEAT_LOCKED | the seat took part in a vote on a SELL or ALLOCATE proposal that is still open: it cannot be offered or sold until that vote ends |
| 33 | DEPOSIT_AFTER_OPEN | QU was added to the member's balance after this BUY proposal opened: that money has no vote on it |
| 34 | NAME_UNPROVEN | SettleIpo: the holding won seats and the registry knows the name of the target's shares, but QPOOL does not hold enough of them to cover the seats; nothing was settled |
| 35 | IPO_NO_SNAPSHOT | SettleIpo: the target's IPO is over but QPOOL's END_EPOCH left no snapshot of its book; nothing can be settled |
| 36 | PRICE_TOO_LOW | RebidIpo: the book is full and the price does not exceed its minimum, so the bid could not win a seat |
| 37 | SEATS_FULL | unused: a BUY_IPO has no voted quantity (seats are what the money buys); kept so numbering stays append-only |
| 38 | OFFER_CHANGED | BuyPosition: what the buyer expected is not what is live now -- the offer's cost units are not expectedCostUnits (the seller replaced it), or the holding's recorded shares are not expectedShares (a sale or a reconcile changed its backing since the buyer read it); refunded, nothing written |
| 39 | NOTHING_TO_BUY | BuyAndBurnQpay: no QPAY is on offer at or below the ceiling (or the pot is empty); nothing was written |
| 40 | NO_PENDING_BID | CancelQxBid: the holding has no QX bid resting (none was placed, or every share of it has filled); nothing was written |
| 41 | NAME_PENDING | the share name of this contract index is not verified: the registry has no (index, name) pair for it (SettleIpo: BEGIN_EPOCH has not paired the IPO's name yet, retry after the next epoch start; a purchase or deposit above the embedded table: the name is not paired to this index, or never could be); nothing was written |
| 42 | QX_SIDE_BUSY | K8.3: this ledger lists resting orders of the other side (a BUY_QX cannot join resting asks, a resting sell cannot join resting bids) |
| 43 | NO_PENDING_ASK | CancelQxAsk: the holding has no sell resting on QX (none was placed, or every share of it has sold); nothing was written |

Return code 0 is OK. K8.3 adds QX_SIDE_BUSY (42) and NO_PENDING_ASK (43); QX_BID_PENDING (25) now also means that the ledger already lists eight orders or that the holding already rests a sell.

## A4. Constants that matter for testing (production values; the devkit file changes the tick constant to 2 and the QPAYHUB index to 50)

| Name | Value |
|---|---|
| QPOOL_MAX_POOLS | 1024 |
| QPOOL_MAX_CONTRIBUTORS_PER_POOL | 256 |
| QPOOL_MAX_HOLDINGS_PER_POOL | 128 |
| QPOOL_MAX_DEPOSITS_PER_POOL | 32 |
| QPOOL_MAX_IPO_HOLDINGS_PER_POOL | 8 |
| QPOOL_MAX_OFFERS_PER_POOL | 32 |
| QPOOL_MAX_ADMITTED_PER_POOL | 256 |
| QPOOL_MAX_OWNERSHIP_PER_POOL | 512 |
| QPOOL_MAX_IPO_BIDS | 16 |
| QPOOL_MAX_IPO_HOLDINGS | 2048 |
| QPOOL_MAX_IPO_TARGETS | 8 |
| QPOOL_TICKS_PER_HOUR | 3600 |
| QPOOL_VOTE_WINDOW | 72 * QPOOL_TICKS_PER_HOUR |
| QPOOL_EXECUTE_WINDOW | 48 * QPOOL_TICKS_PER_HOUR |
| QPOOL_IPO_VOTE_WINDOW | 24 * QPOOL_TICKS_PER_HOUR |
| QPOOL_QX_BID_MAX_AGE_TICKS | 72 * QPOOL_TICKS_PER_HOUR |
| QPOOL_QUORUM_PERMILLE | 500 |
| QPOOL_CREATE_POOL_FEE | 10000000ULL |
| QPOOL_JOIN_FEE | 10000ULL |
| QPOOL_MIN_CONTRIBUTION | 10000ULL |
| QPOOL_MANAGEMENT_PERMILLE | 10 |
| QPOOL_QX_TRANSFER_FEE | 100LL |
| QPOOL_QSWAP_SELL_FEE | 100000LL |
| QPOOL_REVENUE_SHAREHOLDER_PERMILLE | 400 |
| QPOOL_REVENUE_QPAYHUB_PERMILLE | 300 |
| QPOOL_REVENUE_QPAYBUY_PERMILLE | 200 |
| QPOOL_QPAYHUB_CONTRACT_INDEX | 29 |
| QPOOL_QPAY_MAX_PRICE | 10000ULL |
| QPOOL_QPAY_MAX_LEVELS | 8 |
| QPOOL_QPAY_ASSET_NAME | 1497452625ULL |
| QPOOL_KNOWN_CONTRACTS | 30 |
| QPOOL_MAX_CONTRACT_INDEX | 1023 |
| QPOOL_MAX_CONTRACT_NAMES | 2048 |
| QPOOL_NAME_QUEUE_CAP | 16 |
| QPOOL_NAME_WALK_MAX | QPOOL_MAX_CONTRACT_INDEX + 1 |
| QPOOL_NAME_HOLDERS_MAX | 1024 |
| QPOOL_AUTO_SETTLE_MAX | 32 |
| QPOOL_MAX_QX_BIDS_PER_LEDGER | 8 |
| QPOOL_MAX_RESTING_ASKS | 128 |
| QPOOL_MAX_RESTING_BIDS | 128 |

The pool has up to 3 admins (three `id` fields in `Pool`). Derived on the devkit file (`QPOOL_TICKS_PER_HOUR = 2`): vote window 144 ticks, execute window 96, IPO vote window 48, QX order age limit 144. The season build sets `QPOOL_AUTO_SETTLE_MAX = 3` (the release gate fails it on purpose).
