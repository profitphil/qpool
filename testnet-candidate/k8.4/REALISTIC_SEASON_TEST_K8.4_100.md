# QPOOL realistic season test (K8.4, large): 100 wallets, 10 pools, 7 epochs, 6 IPO targets

This is the large end-to-end test: a whole "season" of a real QPOOL economy with **100 wallets and 10 working pools** (plus one scratch pool for `CloseEmptyPool`). Pools have 9 to 42 members;
the big ones (P2, P6 with 28, P1 with 24, P9 with 42) vote, trade and bid with dozens of members each. 7 epochs, and IPO rounds with competition: epoch 2 has one IPO (5 pools), epoch 4 has TWO IPOs at once
(T2 wins with 6 pools, T3 fails with 4), epoch 6 has THREE at once (T4 with 8 pools, T5 fails, T6 with 5). Pools bid in several targets at the same time, outbid each other, rebid, collide on prices,
get wiped out, and settle in different orders. Every procedure and every read function is exercised many times, successes and refusals, across several pools. It does not replace the single-feature
suites; it runs after them (and after the 40-wallet season test, which is the smaller version of this plan) and catches what only a realistic mix reveals.

**The K8.1 rule (the owner's).** Only a pool's OPERATORS act for it: the creator and up to three admins the creator names with `SetPoolAdmins` (procedure 24). Operators alone carry out passed proposals (`Execute`: IPO bids, QX and QSWAP buys and sells),
rebid in an IPO (`RebidIpo`) and cancel a resting QX bid (`CancelQxBid`), the last with one public exception: anybody may cancel a bid older than 72 hours (144 devkit ticks). A KEEPER, a member, a seat owner or another pool's creator is refused (`NOT_AUTHORIZED`).
Epoch 1 starts with every creator naming the pool's admins (scene E1.S3b); the plan then alternates the creator and an admin as the executor. The admins in the plan: P1: U09, U10; P2: U16, U17; P3: U21, U22, U23; P4: U25, U26; P5: none (creator only); P6: U14, U15; P7: U17, U23, U26; P8: U18, U24; P9: U19, U20; P10: U28, U15. P5 has none (creator only) on purpose. In epoch 4 P4's creator replaces his list (one admin leaves, one joins).
Rows tagged `a stranger tries to fire the passed mandate` and `a seat owner who is not an operator` are refusals; tally, reconcile, settlement and the closing of an expired mandate stay open to everyone.

**The K8.2 rule.** Several pools may rest a QX bid on the same token at once (up to 8 per ledger in K8.2, 32 since K8.4). The ledger ranks them by price (highest first) and, at one price, by who bid first; the silently arrived shares and the dividends set aside for them go to the bids in that order. Scene E5.S8 has two pools rest bids on TOK2 at 410 and 400 while a seller fills 60 shares: the 410 bid fills first. In scene E3.S7 the second pool's BUY_QX is no longer refused while the first pool's bid rests.

**The K8.3 rule.** A pool can rest a SELL on QX (proposal kind SELL_QX_LIMIT = 7, procedure `CancelQxAsk` = 25): what the visible bids pay fills at once, the rest rests on QX, a buyer pays QPOOL silently and a ReconcileHolding credits it (less QX's fee, bounded, and the 1% management fee).

**The K8.4 rule.** Bids and sells rest on the SAME token at the same time: a ledger keeps two lists and a holding two order records, and what filled or sold is read from QX's two books. A new bid must be priced below QPOOL's own lowest sell of the token (SELF_CROSS), and one pool holds at most 12 of the 32 places of a side. Scene E5.S9 has P6 rest a sell of its 50 TOK2 shares at 520 and P1 rest a bid of 30 @ 400 on the same token; P1's BUY_QX priced at 520 is refused (SELF_CROSS); MM3 takes 20 of the sell, MM2 fills 10 of the bid; ONE reconcile credits both (the sell's proceeds 10,246; the 19 left over go to revenue with the last cancel); an admin cancels the sell and P1's creator the rest of the bid.

**What is different in K8.** No BUY_IPO carries a share name (asset name 0). At every epoch start after an IPO round, `BEGIN_EPOCH` pairs the new contract names with the
IPOs that succeeded and settles up to `QPOOL_AUTO_SETTLE_MAX` ready holdings; the plan settles the rest by hand (a manual call on a holding that already settled returns `IPO_NOT_ACTIVE`).
The season build sets the bound to **3**, so every IPO epoch change leaves holdings for manual calls (5 holdings in epoch 3, 10 in epoch 5, 16 in epoch 7). Epoch 7 pairs TWO names in one epoch change
(T4 and T6 succeeded, T5 failed), which exercises the book-fingerprint pairing. `CancelQxBid` appears in three places: epoch 3 (resting bids on TOK2: a creator cancel with the authority checks and the blocked pool,
an admin cancel after a partial fill, and the 72 hour age limit for a stranger), epoch 4 (a refusal on an IPO holding) and epoch 5 (a bid that filled completely before anyone reconciled).
`BuyPosition` carries the live `expectedShares`; a BUY_IPO whose price was taken fails its proposal and unlocks the voters. A BUY_IPO with a non-zero asset name is refused.

The plan is GENERATED (`python3 season_plan_k8_100.py`) and the generator ASSERTS its own coverage, so the plan cannot silently skip a function.
Files in `season-plan-k84-100/`: `actions.csv` (the ordered action list, 3,662 rows), `cast.csv`, `pools.csv`, `ipo-rounds.txt` (expected step-by-step IPO wars), `ipo-results.csv`,
`checkpoints.md` (expected totals per epoch), `coverage.md`. Expected values come from `ipo_reference_model.py`, `trading_reference_model.py`
and the generator's ledger. They are aids, not authorities: the chain is the truth, and a mismatch means "investigate" (say whether the plan or the contract was wrong).
Where the plan cannot know which holdings `BEGIN_EPOCH` took (that follows the contract's table order), the row says `OK` or `IPO_NOT_ACTIVE` and the end result must be the same either way.

## 1. How to run it
1. Do the setup of TESTING_GUIDE_K8.4.md section 1 and run the single-feature suites first. Deploy the SEASON build (`QPoolK8.4-season-devkit.h`, `QPOOL_AUTO_SETTLE_MAX = 3`). Confirm the PARAMs of section 9, edit them at the top of `season_plan_k84_100.py` if they differ, and regenerate.
   After onboarding the creators name their admins (E1.S3b); from then on every Execute, RebidIpo and early CancelQxBid row names a creator or an admin as the actor. Do not use a KEEPER for them.
   The six targets T1 to T6 must be contracts above index 30 whose IPO ends AFTER QPOOL's first epoch has begun (otherwise their names are never paired).
2. Create the 100 wallets of `cast.csv` (fund them as in section 2) and the targets T1 to T6 (section 3). At the start of every epoch from 3 on, first read `GetStats` and `GetNameRegistry` as the rows say.
3. Walk `actions.csv` in order of (epoch, tick). `tick` is an offset inside the epoch, in the testnet header's units (vote 144, execute 96, IPO vote 48).
   An epoch must be long enough for its longest pipeline (a proposal needs vote window + execute window, about 250 ticks; IPO proposals about 150).
   If your epochs are shorter, start the scene earlier or split it across epochs, but keep the order of its steps.
4. `expect` is `OK`, `REFUSED:CODE`, or alternatives joined by `|` (the code names are in the appendix of TESTING_GUIDE_K8.4.md). A refusal must return
   the attached reward exactly once and write nothing. Actors named `EXT:` are not QPOOL calls: they are the outside world (issuers, market makers, outsiders, the epoch change).
5. After EVERY row run invariants I1 to I19 of TESTING_GUIDE_K8.4.md. At every `end-of-epoch reads` row compare with `checkpoints.md`.
6. Record per row: actual result, the read-backs named in `note`, PASS or FAIL. Stop a scene at its first failure, keep the chain, and report (TESTING_GUIDE_K8.4.md section 9).

## 2. The cast
| User | Role | Pools |
|---|---|---|
| U01 | creator of pool P1 | P1 |
| U02 | creator of pool P2 | P2 |
| U03 | creator of pool P3 | P3 |
| U04 | creator of pool P4 | P4 |
| U05 | creator of pool P5 | P5 |
| U06 | creator of pool P6 | P6 |
| U07 | creator of pool P7 | P7 |
| U08 | creator of pool P8 | P8 |
| U31 | market maker MM1 (sells TOK1 on QX) | - |
| U32 | market maker MM2 (sells TOK2 on QX) | - |
| U33 | market maker MM3 (QX bids, QPAY asks) | - |
| U34 | LP: QSWAP liquidity TOK1/QU | - |
| U35 | ISSUER1: issues TOK1, pays dividends | - |
| U36 | ISSUER2: issues TOK2, pays dividends | - |
| U37 | OUT1: IPO outsider and trader | - |
| U38 | OUT2: IPO outsider and sniper | - |
| U39 | OUT3: IPO outsider | - |
| U40 | KEEPER and ATTACKER (non-member, permissionless calls, hostile inputs) | - |
| U41 | creator of pool P9 | P9 |
| U42 | creator of pool P10 | P10 |
| U43 | OUT4: IPO outsider | - |
| U44 | OUT5: IPO outsider and sniper | - |
| U45 | OUT6: IPO outsider | - |
| U46 | market maker MM4 (TOK1 asks from epoch 4 on) | - |
| U09 to U100 (except the roles above) | 76 members, each in 1 to 3 pools (34 in 2, 42 in 3) | see `cast.csv` for each wallet's pools |

Funding guide: creators 25,000,000 QU, members 10,000,000, market makers 500,000,000, issuers 200,000,000, outsiders 300,000,000, KEEPER 150,000,000.
Leave a margin on every wallet: a wallet funded with exactly its fee can be zeroed and pruned by the core, and then its next call never reaches the contract.
QPOOL's own 676 shares must exist and be held by known wallets (for example U09 to U16) so the 40 percent shareholder leg can be checked.
100 wallets means about 100 funding transactions before the season starts: script them.

## 3. The pools
| Pool | Name | Kind | Creator | Members (with the creator) | Persona |
|---|---|---|---|---|---|
| P1 | Alpha | public | U01 | 24 (see `pools.csv`) | QX trader + IPO regular |
| P2 | Beta | public | U02 | 28 (see `pools.csv`) | aggressive IPO hunter |
| P3 | Gamma | private | U03 | 14 (see `pools.csv`) | dividend-yield pool, private, creator deposits TOK2 |
| P4 | Delta | public | U04 | 18 (see `pools.csv`) | QSWAP trader |
| P5 | Epsilon | private | U05 | 11 (see `pools.csv`) | conservative private IPO pool |
| P6 | Zeta | public | U06 | 28 (see `pools.csv`) | mixed: QX, QSWAP and IPO |
| P7 | Eta | public | U07 | 9 (see `pools.csv`) | small pool, seat-market heavy |
| P8 | Theta | private | U08 | 13 (see `pools.csv`) | private pool that toggles public, deposits TOK1 |
| P9 | Iota | public | U41 | 42 (see `pools.csv`) | large public pool: QX trader and IPO regular, many small members |
| P10 | Kappa | private | U42 | 17 (see `pools.csv`) | private IPO syndicate |

Users U09 to U30 and U47 to U100 belong to one to three pools each, so the same person earns, votes and pays separately in each pool. Contributions are uneven on purpose.
Targets: T1 (epoch 2), T2 and T3 (epoch 4, same IPO epoch), T4, T5 and T6 (epoch 6, same IPO epoch). A target is a test contract ABOVE index 30 with construction epoch = IPO epoch + 1
whose shares can pay `distributeDividends`, and whose IPO ends after QPOOL's first epoch has begun.

## 4. The season at a glance
| Epoch | Theme | IPOs | Actions |
|---|---|---|---|
| 1 | Launch (the creators name their admins): baseline of the name registry, create 10 pools (P10 private), admissions, onboarding of about 150 memberships, deposits, first governance wave (P9 buys on QX, P10 on QSWAP), dividends, seat market, closing, squatting | none | 647 |
| 2 | IPO round 1 with 5 pools (one proposal fails quorum, one price collision that FAILS the proposal), a BUY_IPO with a non-zero name is refused, trading continues in the other pools | T1 | 395 |
| 3 | BEGIN_EPOCH pairs T1's name and settles 3 of 5 holdings, the rest by hand with a dividend arriving mid-way; contract dividends; QPAY buy-and-burn; governance variety; seat-market stress; **CancelQxBid on resting bids** (creator, funder after a partial fill, the age limit) | none | 305 |
| 4 | IPO round 2: two simultaneous IPOs, 10 pools, one fails; trading and the seat market in all pools; CancelQxBid refused on an IPO holding | T2, T3 | 737 |
| 5 | BEGIN_EPOCH pairs T2's name (T3 failed: no name), settles 3 of 10 holdings, the rest by hand in two orders; dividends, a spam burst, squat eviction, offers to capacity; BuyPosition with live shares; **CancelQxBid on a bid that already filled**; **two pools rest QX bids on one token and a seller fills them in price order (K8.2)**; **a pool rests a sell and another pool a bid on the SAME token, both fill in part, one reconcile credits both, both are cancelled (K8.3/K8.4)** | none | 422 |
| 6 | IPO round 3: three simultaneous IPOs (8, 3 and 5 pools), one pool in three targets; a BUY_IPO naming another contract is refused | T4, T5, T6 | 848 |
| 7 | BEGIN_EPOCH pairs T4 and T6 by book fingerprint (T5 failed) and settles 3 of 16 holdings, the rest by hand; final dividends, every member (100 wallets' memberships) claims, shutdown reads | none | 308 |

## 5. Dividend attribution battery (who gets what, and who must get nothing)
A dividend must reach only the people who hold the asset through QPOOL. Every dividend row in `actions.csv` (`EXT:pay dividend`) lists the exact expected amounts AND a
`MUST RECEIVE 0` list, and is followed by negative checks. The rules, each tested several times in the plan:
1. Only the holdings of the paying asset's ledger receive anything. A pool that holds no seat of that asset receives nothing: its pool and member reads must be unchanged by the payment.
2. Between pools that hold the SAME asset the payment is split by recognised shares: each pool receives exactly the dividend on its own shares. It is never spread over all pools, and never by pool size or contribution.
3. Inside a holding the amount is split by cost, and only among members who PAID for the seats. A member who voted NO or abstained on the purchase was not charged, has no seat, and gets 0:
   `ClaimPayout` by such a member must be refused (`NOT_MEMBER`) and their balances must not move. Same for IPO holdings: only the yes-voters who funded the bid earn from the shares it wins.
4. A user who is in two pools earns separately in each pool, from that pool's holdings only.
5. A seat bought after a payment earns only later payments; the seller keeps what accrued before the sale.
6. A pool whose BUY proposal was rejected or expired holds nothing and earns nothing.
7. A creator's deposited holding belongs entirely to the creator until seats are sold; the other members get 0 from it.
8. A holding whose IPO is unsettled: its part is parked in escrow and released only to the holdings of that ledger, by shares, when the LAST holding settles. A BUY_QX fill that has not been reconciled: the unrecognised part is set aside, not given to the others.
9. Shares sold on QX stop earning for the shares sold; the members' cost fractions do not change.
10. A payment from a sender that matches no ledger is not a pool dividend: it becomes QPOOL revenue (the 40/30/20/10 split), whatever pools exist.
11. Tolerance: floor rounding, at most 1 QU per member. The management fee is 1 percent of each payment (floor) and goes to revenue.
Where to find them: epoch 1 scene S7 (TOK1 to P1, P2, P8 and P9 and nobody else; the voters who said no or abstained are listed), epoch 2 S3 (TOK2 only to P3's deposited holdings, creator only),
epoch 3 S2 (T1 shares to five pools, one dividend arriving while two holdings are unsettled), epoch 4 S2, epoch 5 S2 (T2 shares), epoch 7 S2 (every member claims).

## 6. IPO rounds (expected results of the war model, end of epoch)
Full step lists are in `season-plan-k84-100/ipo-rounds.txt`; the same steps appear as `Execute`, `RebidIpo` and `EXT:IPO bid` rows. Each war has pools funded by their yes-voters only.
| Target | Epoch | Pools | Final price | Expected settlement per pool |
|---|---|---|---|---|
| T1 | 2 | 5 | 2,052 | P1 62 seats, cost 127,224, back 72,776; P2 123 seats, cost 252,396, back 7,604; P5 72 seats, cost 147,744, back 2,256; P6 109 seats, cost 223,668, back 16,332; P9 53 seats, cost 108,756, back 71,244 |
| T2 | 4 | 6 | 1,669 | P2 150 seats, cost 250,350, back 49,650; P3 83 seats, cost 138,527, back 11,473; P4 111 seats, cost 185,259, back 14,741; P6 105 seats, cost 175,245, back 4,755; P7 45 seats, cost 75,105, back 4,895; P10 52 seats, cost 86,788, back 13,212 |
| T3 | 4 | 4 | 0 (FAILS) | P1 0 seats, cost 0, back 240,000; P5 0 seats, cost 0, back 120,000; P8 0 seats, cost 0, back 100,000; P9 0 seats, cost 0, back 45,000 |
| T4 | 6 | 8 | 2,550 | P1 56 seats, cost 142,800, back 107,200; P2 134 seats, cost 341,700, back 8,300; P4 74 seats, cost 188,700, back 11,300; P6 50 seats, cost 127,500, back 172,500; P7 31 seats, cost 79,050, back 20,950; P8 46 seats, cost 117,300, back 32,700; P9 46 seats, cost 117,300, back 32,700; P10 30 seats, cost 76,500, back 3,500 |
| T5 | 6 | 3 | 0 (FAILS) | P3 0 seats, cost 0, back 80,000; P5 0 seats, cost 0, back 60,000; P9 0 seats, cost 0, back 50,000 |
| T6 | 6 | 5 | 2,700 | P2 66 seats, cost 178,200, back 21,800; P6 51 seats, cost 137,700, back 12,300; P1 35 seats, cost 94,500, back 5,500; P8 21 seats, cost 56,700, back 3,300; P10 30 seats, cost 81,000, back 9,000 |

Checks specific to the rounds: refunds land inside the outsider's transaction; a rebid is funded only by money already returned; a price owned by another holding is refused;
two targets in one epoch never mix refunds; settle the holdings the epoch start left over in the order given in the plan and then verify the result equals the model (it must not depend on the mix);
T3 and T5 are failed IPOs (book never fills): every pool gets its whole budget back and its holding retires, and no name is paired for them (`GetNameRegistry` shows name 0);
the holdings of T6, including P8's, settle like any other (no wrong name can be declared). Which holdings `BEGIN_EPOCH` settles and which are left for manual calls does not change any result.

## 7. Revenue, END_EPOCH and the QPAY burn per epoch (expected)
| Epoch | Revenue | Per share | Shareholders paid | To QPAYHUB | To pot | Burned | Pot after |
|---|---|---|---|---|---|---|---|
| 1 | 112,112,734 | 66,338 | 44,844,488 | 33,633,820 | 22,422,546 | 11,211,880 | 22,422,546 |
| 2 | 18,844 | 11 | 7,436 | 5,653 | 3,768 | 1,987 | 22,426,314 |
| 3 | 1,466 | 0 | 0 | 439 | 293 | 734 | 12,056,607 |
| 4 | 11,500 | 6 | 4,056 | 3,450 | 2,300 | 1,694 | 12,058,907 |
| 5 | 102,235 | 60 | 40,560 | 30,670 | 20,447 | 10,558 | 11,439,364 |
| 6 | 0 | 0 | 0 | 0 | 0 | 0 | 11,439,364 |
| 7 | 5,000 | 2 | 1,352 | 1,500 | 1,000 | 1,148 | 10,440,364 |

Revenue sources in the plan: pool creation fees, join fees, 1 percent dividend fees, 1 percent seat-sale fees, stray transfers and donations. Replay `BuyAndBurnQpay` rows against
the QPAY asks listed in their notes (levels at or below 10,000, at most 8 levels per call). The pot falls by the amount spent plus the hand-over fee the balance REALLY fell by during the hand-over (K7 measures it; the plan assumes 0, as the local Qx.h refunds the attached reward: measure it, report 0 or 100, and change `QPAY_HANDOVER` in the generator if it is 100).

## 8. Coverage (generated and asserted)
| Function | Calls | OK | Refusals | Distinct variants | Pools |
|---|---|---|---|---|---|
| CreatePool | 12 | 11 | 1 | 3 | 11 |
| Contribute | 219 | 216 | 3 | 7 | 10 |
| WithdrawUncommitted | 21 | 17 | 4 | 9 | 11 |
| Propose | 81 | 66 | 15 | 30 | 10 |
| Vote | 1335 | 1166 | 169 | 6 | 10 |
| Tally | 111 | 56 | 55 | 2 | 10 |
| Execute | 140 | 52 | 88 | 16 | 10 |
| ReconcileHolding | 31 | 28 | 3 | 10 | 10 |
| ClaimPayout | 288 | 236 | 52 | 11 | 10 |
| OfferPosition | 63 | 61 | 2 | 21 | 10 |
| CancelOffer | 43 | 22 | 21 | 5 | 10 |
| BuyPosition | 55 | 43 | 12 | 12 | 10 |
| RebidIpo | 151 | 24 | 127 | 18 | 10 |
| SettleIpo | 34 | 31 | 3 | 34 | 10 |
| DepositHoldingAsset | 36 | 15 | 21 | 6 | 10 |
| SetAdmission | 52 | 51 | 1 | 2 | 4 |
| SetPermissioned | 7 | 6 | 1 | 4 | 4 |
| ReleaseManagedShares | 6 | 3 | 3 | 6 | 0 |
| CloseEmptyPool | 4 | 1 | 3 | 3 | 3 |
| EvictSquat | 5 | 2 | 3 | 5 | 3 |
| BuyAndBurnQpay | 5 | 3 | 2 | 5 | 0 |
| CancelQxBid | 16 | 5 | 11 | 16 | 3 |
| SetPoolAdmins | 18 | 12 | 6 | 10 | 10 |
| CancelQxAsk | 3 | 1 | 2 | 3 | 1 |
| GetPool | 73 | 73 | 0 | 4 | 10 |
| GetMember | 90 | 90 | 0 | 7 | 10 |
| GetHolding | 231 | 231 | 0 | 33 | 10 |
| GetPosition | 73 | 73 | 0 | 4 | 10 |
| GetProposal | 136 | 136 | 0 | 17 | 10 |
| GetStats | 15 | 15 | 0 | 7 | 0 |
| GetSource | 20 | 20 | 0 | 12 | 0 |
| GetIpoTarget | 8 | 8 | 0 | 8 | 0 |
| GetIpoBids | 157 | 157 | 0 | 28 | 10 |
| GetNameRegistry | 9 | 9 | 0 | 9 | 0 |

Total actions: 3662 in 7 epochs; users 100; working pools 10 (+1 scratch).

## 9. PARAMs to confirm on the first use (then edit `season_plan_k84_100.py` and regenerate)
- Whether the 10,000 join fee is taken OUT of the attached amount of a first `Contribute` (`JOIN_FEE_FROM_ATTACHED = True`) or added on top.
- The hand-over cost of a QX sale for deposited shares (`HANDOVER`: 0, 100 or 200).
- QX trade fee (0.3 percent, confirm with `QX.Fees`), QSWAP swap fee rate (read it; the plan uses 30 basis points as an illustration), QSWAP flat sell fee 100,000.
- Where the money returned by `SettleIpo` lands for members (the plan assumes their `uncommitted`, pro rata to what each paid).
- The exact refusal code for a below-minimum contribution and for `EvictSquat` before its predicate holds (the plan lists alternatives).
- `AUTO_SETTLE_MAX` = 3 (the season build's `QPOOL_AUTO_SETTLE_MAX`; production 32). If you build with another value, change it at the top of `season_plan_k84_100.py` and regenerate: the counts of automatic and manual settlements in epochs 3, 5 and 7 follow it.
- `QPAY_HANDOVER` = 0: QU the balance falls by during the QPAY hand-over (0 if QX refunds the attached reward, 100 if it keeps it). It moves the pot figures of section 7.
- The Execute tick of the three CancelQxBid bids in epoch 3 may differ by a tick or two from the plan; the age limit is strictly greater than 144 devkit ticks (72 hours), so compute the boundary from `GetHolding.qxBidTick`.
- Whether the order in which `BEGIN_EPOCH` takes holdings matters: it should not; record which ones it took (`GetHolding.ipoSettled` after the epoch change).

## 10. Pass criteria
Every row matches or its difference is explained and filed; invariants I1 to I12 hold after every row; every dividend reaches exactly the holders listed and nobody else;
settlement matches the war model for every target and every settle order; `globalUnattributed` is 0 after every call; nothing is lost between the contract balance and the books at
the end of epoch 7 beyond the documented dust (at most 1 QU per claimant). Report using TESTING_GUIDE_K8.4.md section 9 and attach the rows of `actions.csv` that failed.
