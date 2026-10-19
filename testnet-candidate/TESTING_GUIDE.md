# QPOOL testnet testing guide

Audience: the person or agent running QPOOL on a Qubic testnet with real transactions and real epoch changes.
Build under test: `QPool_testnet.h` (production `QPool.h` with `QPOOL_TICKS_PER_HOUR = 2`).
Read `CHANGES-SINCE-K6.md` first. Everything marked **NEW** was never run anywhere before this testnet.

## 0. Brief to paste to the testing agent

> You are testing the QPOOL smart contract on a testnet. Work through TESTING_GUIDE.md in order. For every case record: the
> case id, the transactions sent (tick, sender, procedure, input, attached QU), the return code, the read-back values named
> in the case, expected vs actual, and PASS or FAIL. After EVERY transaction that changes state, run the invariant checks of
> section 3. Never mark a case PASS from a return code alone: always read the state back. Repeat each case with the variations
> of section 4. When something fails, stop that suite, keep the chain state, and report using section 9. Do not change the
> contract to make a test pass. Items in section 8 are known gaps: record them, do not report them as new bugs.

## 1. Setup

- Contract index: whatever your node assigns (the real mainnet slot would be 32 or later). Constants in the source that name
  OTHER contracts: QX = 1, QSWAP = 13, `QPOOL_QPAYHUB_CONTRACT_INDEX` = 29. Point the last one at your QPAYHUB stand-in (a
  contract that books plain QU transfers into a counter) and say which index you used.
- QPAY token: the real one is mainnet-only. Issue a test token on QX and replace the issuer block and `QPOOL_QPAY_ASSET_NAME`
  inside `BuyAndBurnQpay`. Keep the ceiling constant `QPOOL_QPAY_MAX_PRICE` = 10,000.
- Windows on this build: vote 144 ticks, execute 96, IPO vote 48. A normal proposal lives 240 ticks, a BUY_IPO 144. Poll the
  tick; never guess with wall-clock sleeps (a proposal expired in an earlier run because of that).
- You need: an IPO-phase target contract (construction epoch = current epoch + 1) for section G; a second token issuer that can
  pay dividends; the ability to change epoch on demand; and the ability to read every `Get*` function.
- Fund every test wallet generously. The pool fee alone is 10,000,000 QU.
- Fee reserve: record `qpi.queryFeeReserve` for the contract before and after each suite.

## 2. Actors (create at least these, keep them for the whole run)

| Name | Role |
|---|---|
| CREATOR_A, CREATOR_B | create pools A and B |
| M1..M6 | members with different sizes (see 4) |
| OUT1, OUT2 | non-members; also IPO outsider bidders |
| ISSUER1, ISSUER2 | token issuers who pay dividends |
| KEEPER | calls permissionless procedures (Tally, Execute, SettleIpo, ClaimPayout, BuyAndBurnQpay) |
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
| I7 | `globalUnattributed` is 0 whenever no QPOOL call is in progress (QX/QSWAP payments are parked only inside a call) | GetStats |
| I8 | A member cannot withdraw more than `uncommitted` minus what open votes lock | WithdrawUncommitted, GetMember |
| I9 | No counter wraps: any value above 2^63 is a failure | all reads |
| I10 | Each live IPO price belongs to exactly one holding; at most 16 live prices per holding | GetIpoBids |
| I11 | `qpayBuyPot` only increases at END_EPOCH and only decreases through BuyAndBurnQpay | GetStats |
| I12 | Dividend credits: nobody can claim more than was paid in, within 1 QU per claimant of floor rounding | GetHolding, ClaimPayout |

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
Kinds: BUY_QSWAP, BUY_QX, BUY_IPO, SELL_QSWAP, SELL_QX, ALLOCATE. Read `GetProposal` after each step.
| Id | Case | Expected |
|---|---|---|
| C1 | Propose each kind with valid input | accepted; fields read back correctly; BUY_IPO shows the derived opening quantity |
| C2 | Propose with invalid inputs: zero, price/quantity overflow, unknown contract index, name 0, amount above `uncommitted`, proposer not a member, second live proposal by the same proposer | each refused before any state write (dirty check) |
| C3 | Vote YES / NO / abstain / twice / after window / by non-member / on a seat sold after voting | first vote counts once; repeats refused |
| C4 | Quorum edges with 250,000 / 250,000 splits: cast one below quorum, exactly quorum, tie yes = no | below: rejected; exact quorum with yes > no passes; a true tie rejects |
| C5 | Tally one tick before the vote window ends / first allowed tick | WINDOW_OPEN then OK |
| C6 | Execute on the last tick of the execute window / one later | works, then EXPIRED |
| C7 | Tally late (after execute window) | proposal expires, locks still release |
| C8 | BUY passes with a yes minority paying: 60 percent holder yes, 40 percent abstains | only recorded yes-voters are charged; the abstainer is never charged |
| C9 | Execute asks more than the yes money can pay | clamped or refused per the contract; never charges a non-voter (I1, I5) |
| C10 | Several proposals open at once in one pool and across pools; slot reuse after expiry | no vote bleed between proposals (a recycled slot must not carry old votes) |
| C11 | Seat locked by a SELL/ALLOCATE vote: OfferPosition and BuyPosition from it | refused with SEAT_LOCKED until the vote window ends, then allowed |
| C12 | Proposer's money moves between Propose and Execute | see B3; Execute refuses cleanly if money is not there |

### D. QX trading
Use a real token on QX with real asks and bids.
| Id | Case | Expected |
|---|---|---|
| D1 | BUY_QX where asks exist at or below the voted price: full fill | holding created with the shares; refund of price improvement returned to the buyers, not left as unattributed money |
| D2 | BUY_QX with partial fill | filled part in the holding, rest rests on QX as a pending bid (`pendingShares`); ReconcileHolding later recognises fills |
| D3 | BUY_QX with no asks | whole order rests; nothing lost |
| D4 | A resting bid that never fills (this build has NO cancel) | capital stays tied; document it, see section 8 |
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
Standard book: OUT1 bids 576 slots at 2,500. Pool A: budget 200,000 at opening price 2,000 gives 100 seats all-in, which fills the 676-slot book.
There is NO ceiling price and NO voted share count: the budget is the only limit; seats = floor(money / price).
| Id | Case | Expected |
|---|---|---|
| G1 | Propose BUY_IPO, vote 48 ticks, Tally, Execute | holding with `ipoAtRisk` = 100 x 2,000, `ipoAvailable` = 0; opening quantity = min(676, floor(assigned / price)) |
| G2 | Execute when assigned < opening price | refused, nothing written |
| G3 | OUT2 bids 5 slots at 2,100 | 5 of A's lowest slots replaced; the refund (5 x 2,000) reaches QPOOL during OUT2's own bid transaction and is pooled (`ipoRefundEscrow`) |
| G4 | A seat owner calls RebidIpo at 2,101 after OUT2's bid | the call first reconciles against the live book and frees the 10,000 that returned; quantity floor(10,000/2,101) = 4 (cost 8,404); a rebid is funded only by money already returned, never by refunds its own bid causes |
| G5 | Repeat RebidIpo reusing price 2,101 until convergence | seats stay 95; all 95 end at 2,101; free funds 405; a price already owned by the same holding is reusable; never CAPACITY while reusing |
| G6 | RebidIpo at a price owned by ANOTHER holding | IPO_PRICE_TAKEN |
| G7 | RebidIpo at or below the book minimum on a full book | PRICE_TOO_LOW, no state write |
| G8 | RebidIpo by a stranger / a member without a seat | refused |
| G9 | RebidIpo needing more QU than free funds / than the contract holds | INSUFFICIENT_BALANCE / BOOKS_INCONSISTENT, nothing written |
| G10 | Rebid at a very high price (for example 40x) | accepted if the money covers at least one seat; buys correspondingly few seats |
| G11 | Five rebids in the last stretch of the IPO epoch, with outsiders outbidding between them | after each call: books vs `GetIpoBids` vs the core book agree on seats; money conserved to the unit |
| G12 | Seats the bid pushed out of the pool's OWN book | their refund arrives after the bid; the books free it only at the next rebid or settlement |
| G13 | Two pools bid in the same IPO at different prices; one is outbid | refunds land in the shared pot; neither pool draws the other's money |
| G14 | Epoch change: END_EPOCH must read the book BEFORE the core closes the IPO; then SettleIpo | `GetIpoTarget` shows a snapshot with the final price; settlement before the snapshot exists is refused |
| G15 | SettleIpo for every holding in EVERY order (2 pools, then 3) | identical per-member results in every order |
| G16 | Final price below the pool's price: refund of (price - final) per seat | returned pro rata by cost; sum of members = budget |
| G17 | Failed IPO (book never fills) | everything refunded; holding retires with no shares |
| G18 | Pool outbid to the end with zero seats | whole budget returned |
| G19 | Two IPOs in the same epoch, one failing and one winning | each settles exactly; one pool cannot take the other's refund |
| G20 | More than 8 pools or targets: ninth IPO target / ninth IPO holding of one pool | refused with CAPACITY before any state write |
| G21 | Wrong share name declared | holding wins seats under the real name but cannot prove its name: NAME_UNPROVEN and it stays unsettled (known gap, section 8) |
| G22 | Dividend of the new contract before and after settlement | parked until all holdings of that ledger settle |
| G23 | RebidIpo after the IPO epoch ended / before the opening bid executed | refused |
| G24 | Ties: a bid equal to the lowest price | does not register; refunded at once |

### H. Seat market
| Id | Case | Expected |
|---|---|---|
| H1 | OfferPosition of part / all / more than owned / on a retired holding | accepted for valid; refused otherwise |
| H2 | BuyPosition with exact price, over-payment, under-payment | over-payment refunded; under refused; fee 1 percent of the ask taken from the seller's proceeds |
| H3 | Seller replaces the offer between the buyer's read and buy | OFFER_CHANGED when the buyer passed `expectedCostUnits` that no longer matches; refunded, nothing written |
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
| I9 | QPAYHUB stand-in missing or refusing the transfer | that leg falls into the burn; nothing lost or double counted |
| I10 | Contract balance lower than the books (poke if you can) | payout clamps to the balance minus the pot |
| I11 | The pot is never paid out or burned by END_EPOCH | `qpayBuyPot` unchanged by a revenue-less epoch |

### J. BuyAndBurnQpay (NEW)
Anyone may call. Needs a pot (run suite I first). Uses the test token you issued on QX.
| Id | Case | Expected |
|---|---|---|
| J1 | Empty pot and nothing unburned | NOTHING_TO_BUY, no state write, reward returned |
| J2 | Pot but no asks | NOTHING_TO_BUY, pot unchanged |
| J3 | One ask level at 50 for 5,000 tokens | bought 5,000, spent 250,000, QX fee 100 from the pot, burned 5,000, `qpayUnburned` 0 (a live run matched this) |
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

### N. Epoch transitions and persistence
| Id | Case | Expected |
|---|---|---|
| N1 | Read all state, change epoch, read again | only the expected fields changed |
| N2 | Run every `Get*` function before and after a node restart in mid-epoch | identical |
| N3 | END_EPOCH with the reserve at 0 | runs; revenue split still happens |
| N4 | Several epochs with no activity | no drift in any counter |
| N5 | Proposal that spans an epoch change | windows count in ticks, not epochs |

## 6. Multiples: randomized runs

After the scripted suites, run a seeded random campaign. Log the seed and every action so a failure can be replayed.

1. 3 pools x 3 to 6 members each, one IPO target live, one token on QX, one dividend payer.
2. Each step picks an action with these weights: Contribute 15, Withdraw 8, Propose 8 (any kind), Vote 12, Tally 6, Execute 6, ClaimPayout 8,
   OfferPosition 5, BuyPosition 5, CancelOffer 2, RebidIpo 8 (when an IPO is live), SettleIpo 4, a dividend payment 6, a stray transfer 3,
   BuyAndBurnQpay 3, a deliberately invalid call 8, advance ticks 10, change epoch 2.
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
| C010 resting bid never cancellable | D4 (still open) |
| C012 duplicate IPO price | G6 |
| C013 table exhaustion | L1 with a hostile pool trying to fill shared tables |
| C015 dead mandate | C9 with rounding at the edges |
| C017 join fee bypass | every entry point (Contribute, BuyPosition, deposit) charges it once |
| C018 creator closes exits | switching to private never blocks members' withdrawals or seat trades |
| C019 stale mandate | Execute window starts where the vote ends |
| C020 early settlement | SettleIpo before the snapshot / before the window closes |
| C033 offer replaced | H3 |
| C034 AMM sandwich | E6 |
| C038 ghost shares | a holding that retires must leave no recognised shares |
| C100 retired holding offers | offers and purchases on a retired holding refused |

## 8. Known gaps in this build: record, do not report as new

- Stray transfers write state (suite M3). Not fixed.
- External fees (SELL_QSWAP 100,000, QX 100) are fronted from the pooled balance and netted from the sale's proceeds; they are not debited from the pool's members first.
- No cancel for resting QX bids (D4).
- An IPO holding that won seats under an unprovable name stays unsettled and parks that ledger's dividends (G21).
- A rebid can be spammed in small amounts by any seat owner; no fee yet (M4).
- QLOAN (index 31 on current mainnet) is not in the table of known dividend payers.
- A pool whose seats are the lowest in the book cannot grow its seat count by rebidding; the count converges to what its money buys at the new price (G5).
- Windows are counted in ticks and assume about one tick per second.

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

---

# Appendix: reference generated from the contract source

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

Id 15 is reserved (the deleted ReconcileIpo). Never call it.

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
| 25 | QX_BID_PENDING | a QX bid on this payer's asset has not been reconciled yet |
| 26 | DISABLED | unused since KIND_SELL_QX was repaired; kept so numbering stays append-only |
| 27 | QX_ONLY | contract shares can only be sold through QX, never swapped on QSWAP |
| 28 | CONTRACT_MISMATCH | the asset name and the paying contract's index do not belong together |
| 29 | CONTRACT_NOT_LIVE | the paying contract does not exist, or is still in its IPO |
| 30 | NO_FILL | SELL_QX: the visible bid book cannot pay the minimum at the limit price now |
| 31 | BOOKS_INCONSISTENT | defence in depth: a pool's records do not add up (a roster or balance defect); refused before anything moved |
| 32 | SEAT_LOCKED | the seat took part in a vote on a SELL or ALLOCATE proposal that is still open: it cannot be offered or sold until that vote ends |
| 33 | DEPOSIT_AFTER_OPEN | QU was added to the member's balance after this BUY proposal opened: that money has no vote on it |
| 34 | NAME_UNPROVEN | SettleIpo: the holding won seats but the shares of its declared name cannot be proven to be them; nothing was settled |
| 35 | IPO_NO_SNAPSHOT | SettleIpo: the target's IPO is over but QPOOL's END_EPOCH left no snapshot of its book; nothing can be settled |
| 36 | PRICE_TOO_LOW | RebidIpo: the book is full and the price does not exceed its minimum, so the bid could not win a seat |
| 37 | SEATS_FULL | unused: a BUY_IPO has no voted quantity (seats are what the money buys); kept so numbering stays append-only |
| 38 | OFFER_CHANGED | BuyPosition: the live offer is not the parcel the buyer expected (the seller replaced it after the buyer read it); refunded, nothing written |
| 39 | NOTHING_TO_BUY | BuyAndBurnQpay: no QPAY is on offer at or below the ceiling (or the pot is empty); nothing was written |
| 0 | OK | success |

## A4. Constants that matter for testing (production values; the testnet header changes only the tick constant)

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

Derived on the testnet header (`QPOOL_TICKS_PER_HOUR = 2`): vote window 144 ticks, execute window 96, IPO vote window 48.

