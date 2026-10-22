# QPOOL realistic season test (K8): 40 users, 8 pools, 7 epochs, 6 IPO targets

This is the end-to-end test: a whole "season" of a real QPOOL economy. 40 users, 8 working pools (plus one scratch pool for
`CloseEmptyPool`), 7 epochs, and IPO rounds with competition: epoch 2 has one IPO, epoch 4 has TWO IPOs at once (one wins, one fails),
epoch 6 has THREE at once (a big war, a failure, and a second war). Pools bid in several targets at the
same time, outbid each other, rebid, collide on prices, get wiped out, and settle in different orders. Every procedure and every read
function is exercised many times, successes and refusals, across several pools. It does not replace the single-feature suites; it
runs after them and catches what only a realistic mix reveals.

**What is different in K8.** No BUY_IPO carries a share name (asset name 0). At every epoch start after an IPO round, `BEGIN_EPOCH` pairs the new contract names with the
IPOs that succeeded and settles up to `QPOOL_AUTO_SETTLE_MAX` ready holdings; the plan settles the rest by hand (a manual call on a holding that already settled returns `IPO_NOT_ACTIVE`).
The season build sets the bound to **3**, so every IPO epoch change leaves some holdings for manual calls (4 holdings in epoch 3, 8 in epoch 5, 12 in epoch 7). Epoch 7 pairs TWO names in one epoch change
(T4 and T6 succeeded, T5 failed), which exercises the book-fingerprint pairing. `CancelQxBid` appears in three places: epoch 3 (resting bids on TOK2: a creator cancel with the authority checks and the blocked pool, a funder cancel after a partial fill, and the 72 hour age limit for a stranger),
epoch 4 (a refusal on an IPO holding) and epoch 5 (a bid that filled completely before anyone reconciled). `BuyPosition` carries the live `expectedShares`; a BUY_IPO whose price was taken fails its proposal and unlocks the voters.
The wrong-name scene of the K6 plan is gone (a name can no longer be declared); its place is a refusal of a BUY_IPO with a non-zero name.

The plan is GENERATED (`python3 season_plan_k8.py`) and the generator ASSERTS its own coverage, so the plan cannot silently skip a function.
Files in `season-plan-k8/`: `actions.csv` (the ordered action list, 1,970 rows), `cast.csv`, `pools.csv`, `ipo-rounds.txt` (expected step-by-step IPO wars),
`checkpoints.md` (expected totals per epoch), `coverage.md`. Expected values come from `ipo_reference_model.py`, `trading_reference_model.py`
and the generator's ledger. They are aids, not authorities: the chain is the truth, and a mismatch means "investigate" (say whether the plan or the contract was wrong).
Where the plan cannot know which holdings `BEGIN_EPOCH` took (that follows the contract's table order), the row says `OK` or `IPO_NOT_ACTIVE` and the end result must be the same either way.

## 1. How to run it
1. Do the setup of TESTING_GUIDE_K8.md section 1 and run the single-feature suites first. Deploy the SEASON build (`QPoolK8-season-devkit.h`, `QPOOL_AUTO_SETTLE_MAX = 3`). Confirm the PARAMs of section 9, edit them at the top of `season_plan_k8.py` if they differ, and regenerate.
   The six targets T1 to T6 must be contracts above index 30 whose IPO ends AFTER QPOOL's first epoch has begun (otherwise their names are never paired).
2. Create the 40 wallets of `cast.csv` (fund them as in section 2) and the targets T1 to T6 (section 3). At the start of every epoch from 3 on, first read `GetStats` and `GetNameRegistry` as the rows say.
3. Walk `actions.csv` in order of (epoch, tick). `tick` is an offset inside the epoch, in the testnet header's units (vote 144, execute 96, IPO vote 48).
   An epoch must be long enough for its longest pipeline (a proposal needs vote window + execute window, about 250 ticks; IPO proposals about 150).
   If your epochs are shorter, start the scene earlier or split it across epochs, but keep the order of its steps.
4. `expect` is `OK`, `REFUSED:CODE`, or alternatives joined by `|` (the code names are in the appendix of TESTING_GUIDE_K8.md). A refusal must return
   the attached reward exactly once and write nothing. Actors named `EXT:` are not QPOOL calls: they are the outside world (issuers, market makers, outsiders, the epoch change).
5. After EVERY row run invariants I1 to I16 of TESTING_GUIDE_K8.md. At every `end-of-epoch reads` row compare with `checkpoints.md`.
6. Record per row: actual result, the read-backs named in `note`, PASS or FAIL. Stop a scene at its first failure, keep the chain, and report (TESTING_GUIDE_K8.md section 9).

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
| U09 | member (in 1 to 3 pools) | P1,P2 |
| U10 | member (in 1 to 3 pools) | P1,P2 |
| U11 | member (in 1 to 3 pools) | P1,P3 |
| U12 | member (in 1 to 3 pools) | P1,P4 |
| U13 | member (in 1 to 3 pools) | P1,P5 |
| U14 | member (in 1 to 3 pools) | P1,P6 |
| U15 | member (in 1 to 3 pools) | P1,P6 |
| U16 | member (in 1 to 3 pools) | P2,P6 |
| U17 | member (in 1 to 3 pools) | P2,P7 |
| U18 | member (in 1 to 3 pools) | P2,P8 |
| U19 | member (in 1 to 3 pools) | P2 |
| U20 | member (in 1 to 3 pools) | P2 |
| U21 | member (in 1 to 3 pools) | P3,P5 |
| U22 | member (in 1 to 3 pools) | P3,P6 |
| U23 | member (in 1 to 3 pools) | P3,P7 |
| U24 | member (in 1 to 3 pools) | P3,P8 |
| U25 | member (in 1 to 3 pools) | P4,P6 |
| U26 | member (in 1 to 3 pools) | P4,P7 |
| U27 | member (in 1 to 3 pools) | P4,P8 |
| U28 | member (in 1 to 3 pools) | P4,P8 |
| U29 | member (in 1 to 3 pools) | P5,P6 |
| U30 | member (in 1 to 3 pools) | P5,P6,P7 |
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

Funding guide: creators 25,000,000 QU, members 10,000,000, market makers 500,000,000, issuers 200,000,000, outsiders 300,000,000, KEEPER 150,000,000.
Leave a margin on every wallet: a wallet funded with exactly its fee can be zeroed and pruned by the core, and then its next call never reaches the contract.
QPOOL's own 676 shares must exist and be held by known wallets (for example U09 to U16) so the 40 percent shareholder leg can be checked.

## 3. The pools
| Pool | Name | Kind | Creator | Members | Persona |
|---|---|---|---|---|---|
| P1 | Alpha | public | U01 | U09 U10 U11 U12 U13 U14 U15 | QX trader + IPO regular |
| P2 | Beta | public | U02 | U16 U17 U18 U19 U20 U09 U10 | aggressive IPO hunter |
| P3 | Gamma | private | U03 | U21 U22 U23 U24 U11 | dividend-yield pool, private, creator deposits TOK2 |
| P4 | Delta | public | U04 | U25 U26 U27 U28 U12 | QSWAP trader |
| P5 | Epsilon | private | U05 | U29 U30 U13 U21 | conservative private IPO pool |
| P6 | Zeta | public | U06 | U14 U15 U16 U22 U25 U29 U30 | mixed: QX, QSWAP and IPO |
| P7 | Eta | public | U07 | U17 U23 U26 U30 | small pool, seat-market heavy |
| P8 | Theta | private | U08 | U18 U24 U27 U28 | private pool that toggles public, deposits TOK1 |

Users U09 to U30 belong to one to three pools each, so the same person earns, votes and pays separately in each pool. Contributions are uneven on purpose.
Targets: T1 (epoch 2), T2 and T3 (epoch 4, same IPO epoch), T4, T5 and T6 (epoch 6, same IPO epoch). A target is a test contract with construction epoch = IPO epoch + 1
whose shares can pay `distributeDividends`.

## 4. The season at a glance
| Epoch | Theme | IPOs | Actions |
|---|---|---|---|
| 1 | Launch: baseline of the name registry, create pools, admissions, onboarding, deposits, first governance wave, dividends, seat market, closing, squatting | none | 273 |
| 2 | IPO round 1 with 4 pools (one proposal fails quorum, one price collision that now FAILS the proposal), a BUY_IPO with a non-zero name is refused, trading continues in the other pools | T1 | 244 |
| 3 | BEGIN_EPOCH pairs T1's name and settles 3 of 4 holdings, the last by hand with a dividend arriving mid-way; contract dividends; QPAY buy-and-burn; governance variety; seat-market stress; **CancelQxBid on resting bids** (creator, funder after a partial fill, the age limit) | none | 206 |
| 4 | IPO round 2: two simultaneous IPOs, 8 pools, one fails; trading and the seat market in all pools; CancelQxBid refused on an IPO holding | T2, T3 | 425 |
| 5 | BEGIN_EPOCH pairs T2's name (T3 failed: no name), settles 3 of 8 holdings, the rest by hand in two orders; dividends, a spam burst, squat eviction, offers to capacity; BuyPosition with live shares; **CancelQxBid on a bid that already filled** | none | 204 |
| 6 | IPO round 3: three simultaneous IPOs, one pool in three targets; a BUY_IPO naming another contract is refused | T4, T5, T6 | 482 |
| 7 | BEGIN_EPOCH pairs T4 and T6 by book fingerprint (T5 failed) and settles 3 of 12 holdings, the rest by hand; final dividends, every member claims, shutdown reads | none | 136 |

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
Where to find them: epoch 1 scene S7 (TOK1 to P1, P2, P8 and nobody else; the voters who said no or abstained are listed), epoch 2 S3 (TOK2 only to P3's deposited holdings, creator only),
epoch 3 S2 (T1 shares to four pools, one dividend arriving while two holdings are unsettled), epoch 4 S2, epoch 5 S2 (T2 shares), epoch 7 S2 (every member claims).

## 6. IPO rounds (expected results of the war model, end of epoch)
Full step lists are in `season-plan-k8/ipo-rounds.txt`; the same steps appear as `Execute`, `RebidIpo` and `EXT:IPO bid` rows. Each war has pools funded by their yes-voters only.
| Target | Epoch | Pools | Final price | Expected settlement per pool |
|---|---|---|---|---|
| T1 | 2 | 4 | 1,969 | P1 100 seats, cost 196,900, back 3,100; P2 123 seats, cost 242,187, back 17,813; P5 69 seats, cost 135,861, back 14,139; P6 109 seats, cost 214,621, back 25,379 |
| T2 | 4 | 5 | 1,619 | P2 150 seats, cost 242,850, back 57,150; P3 73 seats, cost 118,187, back 31,813; P4 111 seats, cost 179,709, back 20,291; P6 105 seats, cost 169,995, back 10,005; P7 0 seats, cost 0, back 80,000 |
| T3 | 4 | 3 | 0 (FAILS) | P1 0 seats, cost 0, back 240,000; P5 0 seats, cost 0, back 120,000; P8 0 seats, cost 0, back 100,000 |
| T4 | 6 | 6 | 2,442 | P1 100 seats, cost 244,200, back 5,800; P2 134 seats, cost 327,228, back 22,772; P4 62 seats, cost 151,404, back 48,596; P6 117 seats, cost 285,714, back 14,286; P7 31 seats, cost 75,702, back 24,298; P8 60 seats, cost 146,520, back 3,480 |
| T5 | 6 | 2 | 0 (FAILS) | P3 0 seats, cost 0, back 80,000; P5 0 seats, cost 0, back 60,000 |
| T6 | 6 | 4 | 2,700 | P2 66 seats, cost 178,200, back 21,800; P6 51 seats, cost 137,700, back 12,300; P1 35 seats, cost 94,500, back 5,500; P8 21 seats, cost 56,700, back 3,300 |

Checks specific to the rounds: refunds land inside the outsider's transaction; a rebid is funded only by money already returned; a price owned by another holding is refused;
two targets in one epoch never mix refunds; settle the holdings the epoch start left over in the order given in the plan and then verify the result equals the model (it must not depend on the mix);
T3 and T5 are failed IPOs (book never fills): every pool gets its whole budget back and its holding retires, and no name is paired for them (`GetNameRegistry` shows name 0);
the holdings of T6, including P8's, settle like any other (no wrong name can be declared). Which holdings `BEGIN_EPOCH` settles and which are left for manual calls does not change any result.

## 7. Revenue, END_EPOCH and the QPAY burn per epoch (expected)
| Epoch | Revenue | Per share | Shareholders paid | To QPAYHUB | To pot | Burned | Pot after |
|---|---|---|---|---|---|---|---|
| 1 | 90,582,734 | 53,599 | 36,232,924 | 27,174,820 | 18,116,546 | 9,058,444 | 18,116,546 |
| 2 | 18,695 | 11 | 7,436 | 5,608 | 3,739 | 1,912 | 18,120,285 |
| 3 | 1,403 | 0 | 0 | 420 | 280 | 703 | 7,750,565 |
| 4 | 9,700 | 5 | 3,380 | 2,910 | 1,940 | 1,470 | 7,752,505 |
| 5 | 99,863 | 59 | 39,884 | 29,958 | 19,972 | 10,049 | 7,132,487 |
| 6 | 0 | 0 | 0 | 0 | 0 | 0 | 7,132,487 |
| 7 | 5,000 | 2 | 1,352 | 1,500 | 1,000 | 1,148 | 6,133,487 |

Revenue sources in the plan: pool creation fees, join fees, 1 percent dividend fees, 1 percent seat-sale fees, stray transfers and donations. Replay `BuyAndBurnQpay` rows against
the QPAY asks listed in their notes (levels at or below 10,000, at most 8 levels per call). The pot falls by the amount spent plus the hand-over fee the balance REALLY fell by during the hand-over (K7 measures it; the plan assumes 0, as the local Qx.h refunds the attached reward: measure it, report 0 or 100, and change `QPAY_HANDOVER` in the generator if it is 100).

## 8. Coverage (generated and asserted)
| Function | Calls | OK | Refusals | Distinct variants | Pools |
|---|---|---|---|---|---|
| CreatePool | 10 | 9 | 1 | 3 | 9 |
| Contribute | 66 | 63 | 3 | 7 | 8 |
| WithdrawUncommitted | 19 | 15 | 4 | 9 | 9 |
| Propose | 66 | 52 | 14 | 28 | 8 |
| Vote | 386 | 259 | 127 | 6 | 8 |
| Tally | 83 | 42 | 41 | 2 | 8 |
| Execute | 86 | 38 | 48 | 13 | 8 |
| ReconcileHolding | 23 | 20 | 3 | 6 | 8 |
| ClaimPayout | 106 | 74 | 32 | 10 | 8 |
| OfferPosition | 55 | 53 | 2 | 21 | 8 |
| CancelOffer | 35 | 18 | 17 | 5 | 8 |
| BuyPosition | 47 | 37 | 10 | 12 | 8 |
| RebidIpo | 134 | 19 | 115 | 17 | 8 |
| SettleIpo | 27 | 24 | 3 | 27 | 8 |
| DepositHoldingAsset | 32 | 15 | 17 | 6 | 8 |
| SetAdmission | 14 | 13 | 1 | 2 | 3 |
| SetPermissioned | 6 | 5 | 1 | 4 | 3 |
| ReleaseManagedShares | 6 | 3 | 3 | 6 | 0 |
| CloseEmptyPool | 4 | 1 | 3 | 3 | 3 |
| EvictSquat | 5 | 2 | 3 | 5 | 3 |
| BuyAndBurnQpay | 5 | 3 | 2 | 5 | 0 |
| CancelQxBid | 12 | 3 | 9 | 12 | 3 |
| GetPool | 58 | 58 | 0 | 3 | 8 |
| GetMember | 76 | 76 | 0 | 7 | 8 |
| GetHolding | 179 | 179 | 0 | 31 | 8 |
| GetPosition | 59 | 59 | 0 | 4 | 8 |
| GetProposal | 101 | 101 | 0 | 14 | 8 |
| GetStats | 14 | 14 | 0 | 6 | 0 |
| GetSource | 15 | 15 | 0 | 7 | 0 |
| GetIpoTarget | 8 | 8 | 0 | 8 | 0 |
| GetIpoBids | 120 | 120 | 0 | 27 | 8 |
| GetNameRegistry | 9 | 9 | 0 | 9 | 0 |

Total actions: 1970 in 7 epochs; users 40; working pools 8 (+1 scratch).

## 9. PARAMs to confirm on the first use (then edit `season_plan_k8.py` and regenerate)
- Whether the 10,000 join fee is taken OUT of the attached amount of a first `Contribute` (`JOIN_FEE_FROM_ATTACHED = True`) or added on top.
- The hand-over cost of a QX sale for deposited shares (`HANDOVER`: 0, 100 or 200).
- QX trade fee (0.3 percent, confirm with `QX.Fees`), QSWAP swap fee rate (read it; the plan uses 30 basis points as an illustration), QSWAP flat sell fee 100,000.
- Where the money returned by `SettleIpo` lands for members (the plan assumes their `uncommitted`, pro rata to what each paid).
- The exact refusal code for a below-minimum contribution and for `EvictSquat` before its predicate holds (the plan lists alternatives).
- `AUTO_SETTLE_MAX` = 3 (the season build's `QPOOL_AUTO_SETTLE_MAX`; production 32). If you build with another value, change it at the top of `season_plan_k8.py` and regenerate: the counts of automatic and manual settlements in epochs 3, 5 and 7 follow it.
- `QPAY_HANDOVER` = 0: QU the balance falls by during the QPAY hand-over (0 if QX refunds the attached reward, 100 if it keeps it). It moves the pot figures of section 7.
- The Execute tick of the three CancelQxBid bids in epoch 3 may differ by a tick or two from the plan; the age limit is strictly greater than 144 devkit ticks (72 hours), so compute the boundary from `GetHolding.qxBidTick`.
- Whether the order in which `BEGIN_EPOCH` takes holdings matters: it should not; record which ones it took (`GetHolding.ipoSettled` after the epoch change).

## 10. Pass criteria
Every row matches or its difference is explained and filed; invariants I1 to I12 hold after every row; every dividend reaches exactly the holders listed and nobody else;
settlement matches the war model for every target and every settle order; `globalUnattributed` is 0 after every call; nothing is lost between the contract balance and the books at
the end of epoch 7 beyond the documented dust (at most 1 QU per claimant). Report using TESTING_GUIDE_K8.md section 9 and attach the rows of `actions.csv` that failed.
