# QPOOL — testnet package

Vote-governed asset pooling for Qubic. Members contribute QU, the pool acquires
dividend-paying assets by vote (QSWAP swap, QX order, or contract IPO bid),
disposes of them by vote (QSWAP swap, or an immediate sale on QX), and
distributes dividends and sale proceeds pro-rata. There is no admin key
anywhere in the contract.

| File | Goes to | What it is |
|---|---|---|
| `QPool.h` | `src/contracts/` | The contract — 6,016 lines, 20 procedures (ids 1 to 14 and 16 to 21; 15 is reserved), 9 public functions, plus private helpers |
| `contract_qpool.cpp` | `test/` | 263 gtest cases (16,655 lines) |
| `add_ipo_test_contracts.sh` | — | Harness only: adds seven IPO-book test contracts (`TESTEXE` to `TESTEXK`) to a COPY of the core, which the tests of several simultaneous IPOs need (section 5, step KX) |
| `CHANGELOG.md` | — | What changed against the previous revision of this package, in plain words |
| `docs/QUBIC-CONTRACT-GUIDE.md` | — | The contract-writing playbook this file follows |
| `reports/` | — | Two live devnet test reports of the **previous** revision — see the warning below |

State size: **190,201,464 bytes** (~181.4 MiB), inside the 1 GiB cap but not
small — see the capacities table in section 4b and the cost model in section 2a.
Verified: the contract syntax-checks with zero errors attributed to `QPool.h`
under the standalone clang check described in `docs/QUBIC-CONTRACT-GUIDE.md`
section 7 (143 errors come from the macOS platform headers and are expected;
`QPool.h` itself carries 20 warnings, all of the `-Wdeprecated-anon-enum-enum-conversion`
class raised by the contract-call macros). A comment-stripped scan for the
tokens the contract language forbids finds none, and `qpi.numberOfShares` /
`qpi.numberOfPossessedShares` appear in no public function and not in the
hook (the reason is in section 4c).

> **Status of this revision.** What is claimed here was checked by that syntax
> check and by the 263-test unit suite, which ran in a harness that is *not* the
> supported build (section 5). This revision has **not** been run on a devnet,
> on a testnet node, or under the MSVC build. The two reports under `reports/`
> describe the previous revision: their findings were fixed, but they are not
> evidence about this code, and one statement in them — that the first-registered
> pool taking a whole shared dividend is "intentional design" — is wrong and
> superseded by section 4c. Re-run the live tests (section 6) before relying
> on any of it.

---

## 1. The ownership model (read this first — everything follows from it)

Contributions sit as **uncommitted** QU until a passed proposal deploys them. At
that moment each YES voter's share of the deployed QU is written as their
permanent **cost** in that specific holding.

`Holding.costBasis` is an **ownership denominator, never a valuation.** It is set
when the holding is created — to the QU a buy really spent (section 4f), or to the seat
units of a deposit — and afterwards it only shrinks when an IPO returns unspent QU
(section 7).

Three consequences worth internalising:

- **Later contributions cannot dilute an existing holding.** They fund only
  later acquisitions. No NAV, no oracle, no asset pricing is ever needed to
  decide who is owed what.
- **Dividends and sale proceeds end in one mechanism.** Both are QU credited to a
  holding's `payoutPerCostX64` accumulator, and a member's claim is
  `cost × (accumulator − debt)`. Sale proceeds are credited straight to the one
  holding that sold. Dividends reach a holding through its payer's *source
  ledger* (section 4c): the hook advances one per-share accumulator per payer, in
  constant time, and each holding pulls its slice lazily, in proportion to the
  shares it has recognised. Distribution never loops over members, holdings or
  pools.
- **Partial sales disturb nobody's fraction,** because shares can fall while
  costBasis stays put.

The consequence for shared assets: every pool that holds the paying asset
receives its due, split by shares among all holdings of that asset — whichever
way they were acquired — with the two qualifications in section 4c (IPO shares
wait for settlement, and unreconciled QX fills are set aside until reconciled).

For a **deposited** asset (syndication, section 6c) `costBasis` instead means
"seat units" declared by the depositor, and the holding carries `deposited = 1`
and is excluded from `pool.committedTotal`.

---

## 2. Fees

| Fee | Amount | Notes |
|---|---|---|
| Pool registration | 10,000,000 QU | `QPOOL_CREATE_POOL_FEE` (owner decision D8); excess over the fee is refunded. Not given back when a pool is closed |
| Join | 10,000 QU | `QPOOL_JOIN_FEE` (owner decision D8); once per (address, pool), whichever way a NEWCOMER first becomes a member: `Contribute` (out of the attached QU) or `BuyPosition` (out of the QU attached beyond the ask price). The pool creator's `DepositHoldingAsset` (the only deposit there is, step K5) puts him on the roster for nothing |
| Minimum contribution | 10,000 QU | `QPOOL_MIN_CONTRIBUTION`; net of the join fee on a first join, so a first `Contribute` attaches at least 20,000 QU |
| Proposing | **free** | See below |
| Management (running) | **1% of every payout** | `QPOOL_MANAGEMENT_PERMILLE = 10`; see the list below |
| QSWAP buy fee | 100,000 QU | `QSWAP_ADDITIONAL_FEE`; QSWAP keeps this much of the budget attached to every `BUY_QSWAP` swap and swaps the rest. It is part of the holding's cost, and the budget must exceed it |
| QSWAP sell fee | 100,000 QU | `QPOOL_QSWAP_SELL_FEE`; advanced from QPOOL's balance on every `SELL_QSWAP` execute and **recovered from the selling holding's proceeds** — see below |
| QX management-rights fee | 100 QU per move | QX's own transfer fee, assumed to stay 100 (`QPOOL_QX_TRANSFER_FEE`). Paid whenever shares move *into* QX's management (`SELL_QX`: one fee for QPOOL-managed deposited shares and one for QSWAP-managed shares, at most two) and when QX hands shares to QSWAP (`SELL_QSWAP` of a QX-bought holding). Moving QPOOL-managed shares to QSWAP is not charged (QSWAP requests no fee). Charged to the selling holding the same way as the QSWAP sell fee |
| QX trade fee | 0.3% (read live) | Taken by QX from the seller on every `SELL_QX` fill; it is already out of the proceeds QPOOL receives |
| Revenue split | 80% shareholders / 20% burn | At END_EPOCH via `distributeDividends`, once `pendingRevenue` reaches 676,000 QU |

**The join fee** prices becoming a *new voting member* of a pool, the same way
the registration fee prices becoming a new pool, so flooding a roster with
fresh identities is not free **by any route**. A member is on a pool's roster
exactly when he has a Contribution record for it (one fact, created in one
place, `EnsureMember`), and every way in charges the fee once:

- `Contribute`: it is attached on top of the contribution: a first-time
  `Contribute` of 600,000 QU to a pool sends 700,000. Top-ups from a member pay
  nothing extra, however he became one.
- `BuyPosition`: a buyer who is not a member attaches the ask price **plus** the
  fee (`INSUFFICIENT_FEE` and a full refund when the QU beyond the ask do not
  cover it). The fee is not taken from the seller's proceeds. A member pays the
  ask only.
- `DepositHoldingAsset`: **no fee.** Only the pool's creator can deposit (owner decision D16, step K5); he is
  put on the roster as the owner of every seat for nothing, and whatever he attaches comes back in full.

A newcomer is refused with `CAPACITY` on a full roster (256 members, lifetime:
there is no leave-pool call) before any QU or share moves, by every route. Both
fees accrue to `pendingRevenue` rather than being burned outright.

**What the two fees cost an attacker (owner decision D8, said plainly).** The fees are the only price of the capacity attacks that the
per-pool quotas (section 4b) do not close, so here are the figures. Exhausting the 1,024-pool table costs 1,024 x 10,000,000 =
**10.24 billion QU** of registration fees (before D8: 51.2 billion). Filling one open pool's 256-member roster with throw-away identities costs
256 x 10,000 = **2.56 million QU** of join fees (before: 25.6 million) and closes that pool to newcomers for good (there is no leave-pool call), whichever route
the identities take. Filling the table of unsettled IPO holdings (2,048) takes 256 pools of 8 holdings each, **2.56 billion QU** of registration
fees plus the budget of every holding, which comes back at settlement. A pool nobody ever used can be closed by its creator (`CloseEmptyPool`) and its
slot is the next one `CreatePool` takes, but the fee is not given back.

**The management fee** is skimmed before the payout accumulator advances, so
members' entitlements are always computed net. Floor rounding throughout. It
applies at four sites:

- dividends arriving through `POST_INCOMING_TRANSFER`, taken **once, on
  arrival**, and rounded down per hook call (a distribution can reach QPOOL as
  several calls, one per possession record — see 4c). Escrow releases and syncs
  later are fee-free, and a QX *fill* moves shares, not QU, so nothing is taken
  at fill time;
- `SELL_QSWAP` and `SELL_QX` proceeds inside `Execute`, taken on the QU that
  actually reached the contract;
- `ALLOCATE` inside `Execute` — kept for ABI, but it can no longer pay anything
  (section 10);
- **seat sales** in `BuyPosition` — taken off the ask price before the seller
  is paid, never added on top of what the buyer pays.

**Proposing is free** because the 1% already earns from pools that perform, and
taxing governance is self-defeating when governance is the mechanism. Spam is
held off structurally instead — **one live proposal per member per pool**, the
same rule QPI's own `ProposalVoting` uses — and the proposal array is sized so
that cap can never be the thing that runs out (section 4b). Section 2a says what
this costs the contract.

**There is deliberately no flat per-epoch fee.** MSVAULT charges one, and when a
vault cannot pay, its code wipes the vault — balance seized, owners cleared —
and its own comments admit assets get stuck needing "manual patches". Applied
here that would destroy member-owned holdings over an unpaid fee. It would also
break the invariant `sum(member.uncommitted) == pool.uncommittedTotal` unless
every member were debited pro-rata each epoch. A percentage of payouts has none
of these failure modes and only earns when a pool earns.

**The QSWAP sell fee is an external requirement, and the selling holding bears
it.** QSWAP's `SwapExactAssetForQu` does nothing unless the caller attaches at
least `QSWAP_ADDITIONAL_FEE` (100,000 QU), and it hands the fee back when it
refuses the swap. A buy clears that floor naturally because the swap amount
itself is attached; a sell attaches the *asset* side, so QPOOL has to advance
the fee out of its own balance at the moment of the call. The advance is
recovered inside the same call: `Execute` credits the holding with the *balance
difference* — what QSWAP paid, minus the 100,000 fee, minus the 100 QU QX fee if
QX had to hand the shares over — less the 1% management fee. That is
why `Propose` requires minimum proceeds above 100,100 QU, so a sale can never
net the holding nothing while costing the other members money. The contract
must hold at least 100,000 QU when the call is made. A live pool's undeployed
contributions provide that, but **a freshly deployed contract with an empty
balance fails its first `SELL_QSWAP` with `ERR_EXTERNAL_CALL`** — see section 9.

The burn share is not only tokenomics: `qpi.burn` credits the contract's
execution fee reserve, which QPOOL needs to keep running (next section).

### 2a. Execution fees: what running QPOOL costs (and who pays)

Qubic contracts do not pay per call from a wallet. Each contract has an
*execution fee reserve*, kept in Contract 0's state, funded when the contract's
own IPO completes (`finalPrice × 676`) and refilled by `qpi.burn` from the
contract or by anyone through QUtil's `BurnQubicForContract`. The rules that
matter here, from the core's `doc/execution_fees.md`:

- A user procedure call is refused by the node (and the attached QU returned)
  while the reserve is not positive. User *functions* are never checked.
  `POST_INCOMING_TRANSFER` still runs with an empty reserve, so dividends are
  still received, and `END_EPOCH` runs anyway.
- The fees deducted are **proportional to the actual execution time** of the
  procedures.
- On top of that, **every tick in which any code calls `state.mut()` makes the
  node recompute the digest of the whole contract state.** QPOOL's state is
  ~181.4 MiB, and the core's own document warns that the digest can cost much
  more than the procedure that caused it.

**The owner pays all of this.** QPOOL takes money from a caller in only four
procedures: `CreatePool` (10,000,000 QU), `Contribute` (the join fee, first time
only) and `BuyPosition` (1% of the ask price comes out of the seller's proceeds, and
the join fee of a buyer who is new to the pool). Every other procedure returns
any attached QU at once (`DepositHoldingAsset` and `ReleaseManagedShares` accept some only to
refund it, or to pay the venue's transfer fee out of it), so a caller
pays QPOOL nothing for the execution time they use. The reserve is refilled by
the 20% burn at END_EPOCH, which only runs when fees have accrued. QPOOL itself
exposes no reserve function; watch it with the external tools listed in
`doc/execution_fees.md` (qfront.org, qubic.li, contracts.qubic.tools, `qubic-cli
-qutilqueryfeereserve`) and top it up with `BurnQubicForContract` if it runs low.

What the contract does about cost, and where it does not:

- **Validate before the first `state.mut()`.** Every input check, every
  existence check and every external *read* comes before the first write.
  A refused `Propose`, `Vote` or `Tally`, and an `Execute` refused before its first
  money-moving call — a wrong status, a buy of an unissued asset, a sale refused
  with `SETTLEMENT_PENDING`, `NO_FILL` or `INSUFFICIENT_BALANCE`, a vote on a full
  vote table — write nothing and so cost no re-hash. (A buy that QX or QSWAP refuses
  after the call has run is different: the refund passes through the hook, which
  writes.) The harness checks this
  through the core's own state-changed flag (`RefusalsInProposeVoteTallyAndExecuteDoNotDirtyTheState`,
  `VoteOnAFullVoteTableIsRefusedBeforeAnythingIsWritten`).
- **The seat market and the payout claim write nothing when they refuse or have nothing to do (step K6).** `ClaimPayout` works out what the claim
  would pay from a read-only preview of the sync (`SyncPreview`) and returns `NOTHING_OWED` without having written anything -- a seat owner used to be
  able to make the node re-hash the whole state every tick for nothing by claiming on a holding with nothing owed (audit C022, owner decision D23).
  Every refusal of `BuyPosition` (the parcel, the price, the holding, the escrow, admission, room, the fee, the buyer's seat row, the seller's seat and its vote lock)
  comes before its first write; `OfferPosition` of the very offer that stands, `SetPermissioned` to the mode the pool has, `SetAdmission` that changes nothing,
  a withdrawal that is refused, and a claim that finds nothing owed also write nothing; `SyncHolding` itself writes only when the ledger has accrued something since
  the holding last looked. The test `K6CallsThatChangeNothingAndCallsThatAreRefusedDoNotMarkTheStateDirty` checks all of it with the core's state-changed flag.
  The refusals that still write are listed in section 10. (`SetAdmission` writes nothing when it
  changes nothing -- admitting an admitted address, revoking one that is not listed -- since K5.) (Every refusal of a `BUY_IPO` `Propose` or `Execute` and of a `SettleIpo` — the auction is
  still running, no snapshot, no row, a name nothing proves, books that do not add up — comes before the first
  write; `RefusedIpoCallsDoNotDirtyTheState` and `AHoldingThatWonSeatsUnderAnUnprovenNameIsNeitherSettledNorRetired`
  check it with the state-changed flag, and so does every refusal of a `RebidIpo`: `Ki...` tests assert that none of them marks the state dirty. END_EPOCH writes the IPO snapshot only for a target that has none yet.)
- **No new unbounded loop.** The ledger helpers are O(1) (one hash-map lookup,
  one write, at most two 128-bit divisions). The loops that exist are listed in
  section 6 with their bounds; the heavy ones (`Propose` scans all 262,144
  proposal slots to enforce one-live-proposal-per-member) predate this revision and were
  *not* removed. (`Propose(BUY_IPO)` used to scan all 131,072 holdings for a price
  collision as well; it asks the registry of IPO bid prices now, one hash lookup, and
  `Execute` of a `BUY_IPO` refuses a closed IPO window before it searches for a free holding
  slot.)
- **The hook cannot be metered or refused.** It runs on every non-procedure
  arrival, and an arrival from an unregistered sender writes
  `globalUnattributed`, which dirties the state. Anyone can therefore force a
  re-hash in a tick by sending QU to the contract's address (they lose the QU:
  it is parked with no exit). That is a cost an attacker pays in QU and QPOOL
  pays in reserve — listed in section 10.

---

## 3. Integration

Two edits in `src/contract_core/contract_def.h`, one in `test/CMakeLists.txt`.

**Pick the index first.** Take the next free index on the network you deploy
to. The mainnet table embedded in the contract (section 4d) has indices 1 to 30
taken (29 QPAYHUB, 30 QTREAT), so on that network QPOOL is 31 or higher. The
local core checkout this package was built against lists QTREAT at 29 and QPOOL
at 30, which is why the snippets below say 30 — substitute your number
throughout, and check the embedded table against the `contractDescriptions[]`
of the tree you deploy (section 4d says what a mismatch does).

> **The index decides which IPOs QPOOL can ever bid on.** See section 7 — this
> is the single most consequential deployment choice in the package.

### 3a. Include block

Above `// new contracts should be added above this line` in the include section:

```cpp
#undef CONTRACT_INDEX
#undef CONTRACT_STATE_TYPE
#undef CONTRACT_STATE2_TYPE

#define QPOOL_CONTRACT_INDEX 30
#define CONTRACT_INDEX QPOOL_CONTRACT_INDEX
#define CONTRACT_STATE_TYPE QPOOL
#define CONTRACT_STATE2_TYPE QPOOL2
#include "contracts/QPool.h"
```

QPOOL must be included **after** QX (1) and QSWAP (13), which it calls.

### 3b. Description + registration

In `contractDescriptions[]` (asset name must be <= 7 chars):

```cpp
    {"QPOOL", 236, 10000, sizeof(QPOOL::StateData)},
```

and in `REGISTER_CONTRACT_FUNCTIONS_AND_PROCEDURES`:

```cpp
    REGISTER_CONTRACT_FUNCTIONS_AND_PROCEDURES(QPOOL);
```

Set the real construction epoch before mainnet — see section 8.

### 3c. Test wiring

Add `contract_qpool.cpp` to the `add_executable(qubic_core_tests ...)` list. The
suite relies on two of the core's test-only example contracts, `TESTEXB` (plays a
dividend-paying contract) and `TESTEXD` (plays an IPO target), which
`test/contract_testing.h` switches on with `INCLUDE_CONTRACT_TEST_EXAMPLES`; they
follow the last real contract in `contract_def.h` (QPOOL, in the checkout this was
built against). A few tests name indices relative to
`QPOOL_CONTRACT_INDEX`, and the table test knows that QPOOL's own index is not a
valid paying contract.

The tests of several IPOs that end in the same epoch (step KX, section 5) need more IPO-book contracts than the core's examples have: the stock
`TESTEXC` (IPO epoch 137) and `TESTEXD` (154) are two books in two epochs. `add_ipo_test_contracts.sh <copy of the core> [<build dir>]` adds seven more,
`TESTEXE` to `TESTEXK`, all with `TESTEXD`'s epoch, at the end of the contract table (so no existing index moves). Without them `contract_qpool.cpp` does not
compile. Run it on a COPY of the core, never on the checkout you keep (it refuses the owner's paths).

### 3d. Checks to run on the file before anything else

1. The standalone clang syntax check (`docs/QUBIC-CONTRACT-GUIDE.md` section 7).
   Count errors **per file**: expect none attributed to `QPool.h`.
2. The forbidden-token scan on the comment-stripped file: none of `[ ] % / " ' #
   __ typedef union float double int char long`.
3. A CI grep that `qpi.numberOfShares` and `qpi.numberOfPossessedShares` appear
   in **no `PUBLIC_FUNCTION` and not in `POST_INCOMING_TRANSFER`**. Today they
   appear in neither; the deadlock this prevents is explained in section 4c, and a
   future edit would reintroduce it silently.

---

## 4. Entry points, governance rules and capacities

### Procedures and functions

Ids are stable ABI. Procedure 15 (`ReconcileIpo`, deleted in K4) is gone: its id stays reserved and nothing is registered under it. Procedure 13 (`RebidIpo`, deleted in K2) is back with a new meaning (step KI, owner decision D1, section 7a). `GetIpoTarget` (function 8, step K4) and `GetIpoBids` (function 9, step KI) are the functions added since the first revision of this series.

| Id | Procedure | Input, in order | Output, in order | QU attached |
|---|---|---|---|---|
| 1 | `CreatePool` | poolName, permissioned | returnCode, poolId | >= 10,000,000 (excess refunded) |
| 2 | `Contribute` | poolId | returnCode, uncommitted | the contribution (+ 10,000 on a first join) |
| 3 | `WithdrawUncommitted` | poolId, amount | returnCode, withdrawn | none |
| 4 | `Propose` | poolId, kind, asset {issuer, assetName}, quAmount, shares, price, holdingId | returnCode, proposalId | none |
| 5 | `Vote` | proposalId, approve | returnCode, weight | none |
| 6 | `Tally` | proposalId | returnCode, status | none |
| 7 | `Execute` | proposalId | returnCode, holdingId, acquired, proceeds, **sold**, **refunded** | none |
| 8 | `ReconcileHolding` | holdingId | returnCode, shares, pendingShares | none |
| 9 | `ClaimPayout` | holdingId | returnCode, paid | none |
| 10 | `OfferPosition` | holdingId, costUnits, askPrice | returnCode | none |
| 11 | `CancelOffer` | holdingId | returnCode | none |
| 12 | `BuyPosition` | holdingId, seller, **expectedCostUnits** (step K6: the cost units of the offer the buyer read; refused with `OFFER_CHANGED` unless it is exactly the live offer's) | returnCode, costUnitsBought | >= the ask price, **+ 10,000 when the buyer is not a member of the pool yet** (excess refunded) |
| 13 | `RebidIpo` | holdingId, price, quantity | returnCode, bidsRegistered, seatsBefore, freed, atRisk, available, livePrices, **bidQuantity** | none |
| 14 | `SettleIpo` | holdingId | returnCode, shares, returnedToMembers | none |
| 15 | *(reserved)* | `ReconcileIpo` was deleted: the END_EPOCH snapshot reads the IPO book exactly, once, and `SettleIpo` works from it (section 7a). Nothing is registered under id 15 and no later procedure may take it | | |
| 16 | `DepositHoldingAsset` | poolId, asset {issuer, assetName}, amount, seatUnits, contractIndex | returnCode, holdingId | **the pool's creator only** (anybody else: `NOT_AUTHORIZED`, everything refunded, nothing read or written); no fee, whatever is attached comes back |
| 17 | `SetAdmission` | poolId, member, allowed | returnCode | none |
| 18 | `SetPermissioned` | poolId, permissioned | returnCode | none |
| 19 | `ReleaseManagedShares` | asset {issuer, assetName}, amount, destinationIndex | returnCode, released, feePaid, requiredFee | the transfer fee the destination asks for (QX: 100 QU); the rest comes back |
| 20 | `CloseEmptyPool` | poolId | returnCode | none |
| 21 | `EvictSquat` | holdingId, rivalName | returnCode, sharesReturned | none |

| Id | Function | Input | Output |
|---|---|---|---|
| 1 | `GetPool` | poolId | found, creator, poolName, uncommittedTotal, committedTotal, contributorCount, holdingCount, unattributedQu, permissioned, **offerCount, admittedCount, ownershipRows, depositCount, ipoHoldings** (what the pool uses of each shared table; the quotas are in 4b). `holdingCount` is the number of holding slots the pool occupies NOW (it was a lifetime counter before K5) |
| 2 | `GetMember` | poolId, member | found, uncommitted, committed, **locked** (the part of `uncommitted` that backs votes right now; 0 once its expiry has passed) |
| 3 | `GetHolding` | holdingId | found, poolId, issuer, assetName, shares, costBasis, totalPayouts, pendingShares, ipoContractIndex, active, ipoMaxPrice, ipoAtRisk, ipoAvailable, deposited, **ipoSettled, sourceEscrow, payerIndex**, **rows** (the seat rows that exist for the holding) |
| 4 | `GetPosition` | holdingId, member | found, cost, costBasis, owed, offeredCostUnits, askPrice, **voteLockUntilTick** (the seat cannot be offered or sold while the tick is at or below it) |
| 5 | `GetProposal` | proposalId | found, poolId, kind, proposer, issuer, assetName, quAmount, shares, price, holdingId, yesWeight, noWeight, eligibleWeight, openTick, status. For a `BUY_IPO`, `shares` is the opening quantity `Propose` derived, `min(676, quAmount / price)` (not voted) |
| 6 | `GetStats` | — | poolCount, holdingCount, proposalCount, totalContributed, totalDeployed, totalPayoutsDistributed, globalUnattributed, ipoRefundEscrow, totalIpoRefunded, pendingRevenue, totalRevenue, totalManagementFees, totalDistributedToShareholders, totalBurned, **dividendEscrowTotal, totalUnownedSwept** |
| 7 | `GetSource` | source (id), kind (0 TOKEN, 1 CONTRACT) | found, assetName, holdings, recognized, unsettled, escrow, pendingQx, accHigh, accLow |
| 8 | `GetIpoTarget` | ipoContractIndex, price (0 = the row only) | found, unsettled, bidEpoch, snapshotEpoch, finalPrice, seats, spent. `found` = 0 when no unsettled QPOOL holding bids in that target; `snapshotEpoch` = 0 until END_EPOCH has read the book; `seats` and `spent` are what QPOOL's slots at that bid price came to (0 for a holding that won nothing and before the snapshot) |
| 9 | `GetIpoBids` | holdingId | found, ipoContractIndex, **budget**, maxPrice, seats, freeFunds, atRisk, refundPot, count, prices[16], seatsAt[16]. What a rebid bot reads (section 7a): `found` = 0 unless the holding is an unsettled IPO holding; `budget` = `costBasis` = `freeFunds + atRisk`; `seats` and `seatsAt` are the seats QPOOL held when the holding was last reconciled (by a rebid) plus what its last bid seated, a number for the eye (the next rebid and the settlement read the book again); `freeFunds` = `ipoAvailable`; `refundPot` = `ipoRefundEscrow` |

Bold names are new or appended in this revision; nothing was reordered or
removed (checked against the previous revision's structs), so a client built for it
still decodes the output fields it knew. The *inputs* that changed are
`DepositHoldingAsset`, which grew by one trailing field (64 to 72 bytes: send the
new size; it did not change in K5, but who may call it did), and `BuyPosition`, which grew by the trailing `expectedCostUnits` in step K6 (40 to 48 bytes: an input that is
too short is padded with zeros by the node, so a client of the old layout reads 0 and is refused with `INVALID_INPUT` and a full refund, never filled blind); `Propose` also validates more strictly (step KI added a trailing `ceiling` to its input and step KI2 removed it again: 88 bytes, as before KI), and `RebidIpo`'s output has one more trailing field, `bidQuantity` (step KI2).

**Byte layout.** Fields are in the order shown, each starting on a multiple of 8
bytes. The `uint32` (`returnCode`, or `found` in a function) is always first and
takes the first 4 bytes of its 8-byte slot; an `id` is 32 bytes; an `Asset` is
`{id issuer; uint64 assetName}` = 40 bytes; everything else is `uint64`. Measured
by compiling the struct sizes (input / output bytes): CreatePool 16/16,
Contribute 8/16, WithdrawUncommitted 16/16, Propose **88**/16, Vote 16/16, Tally 8/16,
Execute 8/48, ReconcileHolding 8/24, ClaimPayout 8/16, OfferPosition 24/4,
CancelOffer 8/4, BuyPosition **48**/16, **RebidIpo 24/64**, SettleIpo 8/24,
DepositHoldingAsset **72**/16, SetAdmission 48/4, SetPermissioned 16/4, **ReleaseManagedShares 56/32, CloseEmptyPool 8/4, EvictSquat 16/16**;
GetPool 8/**136**, GetMember 40/32, GetHolding 8/**168**, GetPosition 40/56, GetProposal
8/**168**, GetStats 1/128, GetSource 40/72, GetIpoTarget 16/56, **GetIpoBids 8/328**. For example `Propose_input` is poolId@0,
kind@8, asset@16, quAmount@56, shares@64, price@72, holdingId@80, and
`Execute_output` is returnCode@0, holdingId@8, acquired@16, proceeds@24, sold@32,
refunded@40. A contract's address is `id(index, 0, 0, 0)` (the index in the first
8 bytes, the other 24 zero), which is the `source` to pass to `GetSource` for a
CONTRACT ledger; for a TOKEN ledger it is the asset's issuer.

**Only `CreatePool`, `Contribute` and `BuyPosition` accept QU** (and `ReleaseManagedShares`,
for the venue's transfer fee). Every other procedure refunds whatever is attached, first thing,
whatever happens next — so attaching QU there is pointless, and an early error
return never needs to refund a second time (a double refund of exactly this kind
was a drain-the-contract bug in the previous revision's `Propose`, now fixed and
covered by `ProposeRefundsAttachedRewardExactlyOnce`). The four that do take QU
refund the whole attachment on every refusal, and the excess over what they keep
on success. (`DepositHoldingAsset` refunds everything on every path, the success included: a deposit costs nothing.)

`Tally`, `Execute`, `ReconcileHolding` and `SettleIpo` are
**permissionless** — the mandate comes from the vote, or the effect only moves
money to its rightful owners, not from the caller's standing. `ClaimPayout` needs
a seat in the holding. `Propose` and `Vote` need pool membership.
`SetAdmission` and `SetPermissioned` are creator-only.

### Proposal kinds

| Kind | Name | `asset` | `quAmount` | `shares` | `price` | `holdingId` |
|---|---|---|---|---|---|---|
| 1 | `BUY_QSWAP` | the token (issuer is not NULL_ID) | budget, <= the pool's uncommitted QU; **must exceed 100,000** (QSWAP's flat fee comes out of it) | **minimum shares out for the whole budget** (mandatory, > 0); scaled down, rounded up, when `Execute` spends less (4a) | unused | must be 0 |
| 2 | `BUY_QX` | a token, **or contract shares** `{NULL_ID, name}` | budget, <= uncommitted; the order is sized to what the YES voters can pay (4a) | order size at most (> 0) | limit price per share (> 0) | 0 for a token; for contract shares the **paying contract's index** (1 to 1023, not QPOOL's own) |
| 3 | `BUY_IPO` | `{NULL_ID, share name of the target}` | the **budget**, <= uncommitted and **>= `price`** (at least one seat at the opening price); the only limit of the bidding | **ignored** (any value is accepted); `Propose` stores the opening quantity it derives, `min(676, quAmount / price)`, for `GetProposal` to show | the **opening** price per share, the price of the first bid (1 to 1,479,289,940,828): a starting point, not a limit on later bids | the **target contract's index** (above QPOOL's own, at most 1023) |
| 4 | `SELL_QSWAP` | ignored — the holding's asset is stored instead | **minimum proceeds, must exceed 100,100** | shares to sell (<= the holding's) | unused | the holding |
| 5 | `SELL_QX` | ignored — the holding's asset is stored instead | **minimum QU the visible QX bid book must pay now**, after QX's fee; must exceed 200 | shares to sell (<= the holding's) | **lowest price per share** (> 0); `price x shares` below 1e15 | the holding |
| 6 | `ALLOCATE` | — (the holding's asset is stored) | amount | — | — | the holding — kept for ABI; `Execute` can only return `NOTHING_OWED` (section 10) |

Validation at `Propose` is deliberately the same set of checks `Execute` will
make, so a vote is not spent on something that cannot run: a BUY kind runs
`CheckSource` (sections 4c and 4d — payer binding, capacity, pending QX bid,
contract-share pairing); a sell kind needs the holding to exist, be active and
belong to the pool; `SELL_QSWAP` of contract shares is refused with `QX_ONLY`.
`Execute` repeats the checks that can change between vote and execution.

Proposal fields are not kind-checked beyond the table: a field marked "unused" is
stored and ignored.

### 4a. Voting

Vote weight is **kind-dependent**: buys weight by *uncommitted QU* (the money
being spent), sells and allocations by *seat units in the target holding* (the
people who own the thing being sold). A fully-committed member has no vote on
new buys, which is correct — they are not funding it. **A vote binds the people who
cast it** (step K3 of the audit fixes: C003, C006, C015, C019, C051, C076; owner
decisions D3 and D18). The rules below replace the earlier ones, where abstainers were drafted
into a buy, a vote's lock ended at `Tally`, and the execute window started when somebody called
`Tally`.

**Windows.** The vote stays open for **3 days** (`QPOOL_VOTE_WINDOW`) for every kind **except `BUY_IPO`, which
is voted on for 24 hours** (`QPOOL_IPO_VOTE_WINDOW` = 86,400 ticks: an IPO is won and lost in the last hours of its epoch, owner decision D1),
then anyone may `Tally`. `Vote`, `Tally`, the execute window and the vote locks all take the window from
one private function of the proposal's kind (`VoteWindowOf`), so they cannot disagree (`Propose`'s two scans over the proposal slots use the matching lifetime constants inline: a call per slot would cost more than the scan). Passing needs **50% of eligible weight cast** and more yes than no. A passed
proposal must be `Execute`d within **2 days** (`QPOOL_EXECUTE_WINDOW`, the same for every kind) **of the end of the vote**
(`openTick + the vote window of its kind`), whenever `Tally` was called: waiting to tally buys no time, and
`Proposal.passedTick` is always `openTick + the vote window`. The whole life of a proposal is its vote window plus the execute window:
`QPOOL_PROPOSAL_LIFETIME` = 5 days, or 3 days for a `BUY_IPO` (`QPOOL_IPO_PROPOSAL_LIFETIME`). **Windows are counted in ticks and a tick is
nominally one second** (`TARGET_TICK_DURATION` is 1,000 ms in the core), so "24 hours" is 86,400 ticks only at that speed; a network that runs
slower or faster stretches or shrinks every window in real time. After it nothing can be done with the proposal: a `Tally` that would
have passed it makes it EXPIRED instead (and the locks of its voters go), `Execute` flips a passed
one to EXPIRED, and if nobody calls either, `Propose` treats an OPEN or PASSED proposal that old as
finished: its proposer may propose again, its slot is reused and the vote rows it left are deleted.
Note what the window does *not* protect: `Execute` is permissionless, so **the caller chooses the
moment** inside the window; the per-share limit (`price` on `BUY_QX`/`SELL_QX`, the min-out on
`BUY_QSWAP`, the minimum proceeds on `SELL_QSWAP`) is what binds the price.

**Which money has a vote on a buy.** Only money that was in the pool when the proposal opened. A
member whose balance grew at the tick the proposal opened or later (a `Contribute`, or capital
handed back by `SettleIpo`; `Contribution.lastDepositTick >= openTick`) is refused with
`DEPOSIT_AFTER_OPEN` (33) on that proposal, whatever he votes. So flash capital cannot carry a
decision or veto one, and a deposit must be at least one tick older than the proposal it is meant to
vote on. It does not matter on proposals that open after the deposit. (`SettleIpo` is permissionless,
so whoever calls it picks the moment: a member whose refund arrives while a buy is open loses his vote
on that buy, but not his balance. This is the price of the rule.)

**A buy charges only the people who voted YES.** `Execute` walks the roster and pays the buy from the
members whose recorded vote is YES, each for at most **the weight he voted with** and at most
what he still has (his balance may have been spent by another mandate he backed). An abstainer or a NO
voter is never drafted, and neither is money added after the vote. The funded part of the mandate is
`min(quAmount, what the YES voters can pay)`; it is split over them by rounding the running total down
(`share(i) = floor(P(i) × funded / yesSum) − floor(P(i−1) × funded / yesSum)`), so the shares add up to it **exactly**
and none is more than the member's weight. **The order is sized to that money**, at the voted limit price: a
`BUY_QX` buys `min(shares, floor(funded / price))` shares, a `BUY_IPO` bids `min(676, floor(funded / price))` slots at its
opening price (all-in: the budget is the only limit of an IPO holding, there is no voted quantity and no ceiling), a `BUY_QSWAP` asks for at least `ceil(shares × funded / quAmount)` shares out, so the
price limit of the vote never widens. A mandate is therefore never dead for a rounding reason, and a
budget of exactly `price × shares` works (the old advice to pad it is gone). If the funded part cannot
buy one share (or nobody who voted YES has any money left) the mandate is closed once with
status FAILED (`INSUFFICIENT_BALANCE`): the proposer may propose again and the voters are released.
A refusal of the market (QX, QSWAP, the IPO) is not closed this way: it leaves the proposal PASSED
for a retry inside its window.

**Voting locks the capital behind the vote.** A buy-kind vote adds its weight to
`Contribution.lockedForVotes` and `WithdrawUncommitted` cannot take `uncommitted` below the lock
(`GetMember.locked` shows it). **A YES vote's lock lasts until the mandate is carried out (`Execute`)
or its life is over; a NO vote's until `Tally`** (a rejected, expired or sell/allocate vote is released
by `Tally` too). The lock needs no loop to lift when nobody does anything: it carries an expiry
(`lockExpiryTick`, the latest end of life of the proposals voted on) and reads as 0 after it. Without this
a member could vote with QU, pull it out and recycle the same money to veto or carry every
proposal in the pool at no risk. The lock is a hold, not a debit: `GetMember.uncommitted` is
unchanged. It is also conservative — votes on several simultaneously-open proposals sum their locks
instead of deduplicating, so the same QU can look locked more than once. That only ever over-blocks
a withdrawal; it never lets one through that should have been held. Each vote is lifted exactly once:
the lock is one number, and `Contribution.lockStartTick` says which votes belong to it (a vote that
nobody ended before its lock expired is not part of the next lock and takes nothing out of it).

**A sell or allocate vote locks the seat.** Its weight is the voter's seat units, and the same units must
not be able to vote twice. So a member who voted keeps his seat until the vote window ends
(`Ownership.voteLockUntilTick = openTick + VOTE_WINDOW`, shown by `GetPosition.voteLockUntilTick`):
`OfferPosition` of that seat and `BuyPosition` against it are refused with `SEAT_LOCKED` (32), the
refusal writes nothing and the buyer's QU comes back; `CancelOffer`, claims and dividends are not
affected, and the holding's other seats are free. A voted seat therefore cannot be sold for up to 3 days.
The units of a seat that was sold BEFORE the vote vote once, with their new owner. `yes + no` can never
exceed the weight the proposal opened with (`Vote` refuses with `BOOKS_INCONSISTENT` if it ever would).
A SELL or ALLOCATE also remembers which holding it was about: its pool, its asset and the holding's **incarnation**
(`Proposal.holdingTag`, a serial number every created holding draws from `StateData.holdingSerial`, kept in `Holding.incarnation`; 0 in a free slot). `Vote` and `Execute` answer
`NOT_FOUND` if the slot holds anything else now. The slot of a holding that lost its IPO, of a sold-out holding whose last member has claimed and of an evicted squat is taken again
(step K5), and the pool and the asset are not enough to tell two holdings of one slot apart: the creator of a pool can deposit the same asset again into the slot a sold-out holding gave back, and
the first version of K5 let a mandate voted by the OLD owners sell that new deposit (or credit the pool's unowned QU to its owners). The incarnation is what tells them apart; a tag of 32 bits is exact
because it only has to separate the holdings one slot can hold during the life of ONE proposal (432,000 ticks, at most 4,096 transactions a tick: fewer than 1.8 billion creations, fewer than 2^32).

**The seat market (step K6).** A seat is a number of cost units in one holding; `OfferPosition(holdingId, costUnits, askPrice)` offers a parcel of it and `BuyPosition` fills it. Four rules:

1. *The buyer names the parcel.* `BuyPosition` carries `expectedCostUnits`, the size of the offer the buyer read (`GetPosition(holdingId, seller).offeredCostUnits`), and is refused with `OFFER_CHANGED`
   (38), refunded in full and writing nothing unless it is exactly the live offer's. A seller can replace his offer at any moment (`OfferPosition` overwrites it) and the QU a buyer attaches only caps the PRICE, so before
   K6 an offer for 300,000 cost units at 50,000 QU that the seller replaced with one unit for the same 50,000 sold the buyer ONE unit (audit C033). The price needs no field of its own: the ask is taken from the attachment, the
   rest comes back, and a higher ask than the attachment is `PRICE_TOO_HIGH`. 0 is never a parcel (`INVALID_INPUT`).
2. *Only a seat that is worth something is for sale.* A RETIRED holding (sold out, or an IPO that won nothing: `active` 0) keeps its cost units only so that the claims on what it earned can be paid, and nothing can accrue on them again; the seats of an
   IPO holding that `SettleIpo` has not run for are in limbo, because `SettleIpo` hands part of the budget back and shrinks every seat's cost units pro rata, which would turn an offer written in cost units into a larger fraction of the
   holding than the seller meant (audit C100, C055). `OfferPosition` refuses both (`WRONG_STATUS`, `SETTLEMENT_PENDING`), `BuyPosition` does not fill an offer that is still there (the seller can cancel it; a claim removes it with the row), and nothing is written. An unsettled IPO
   holding's seats can be offered once `SettleIpo` has run, in the units that are there then.
3. *Admission (owner decision D7).* In a permissioned pool a newcomer needs an entry on the allow-list for `Contribute` and for `BuyPosition`; a MEMBER of the pool, however he joined, always counts as admitted, and so does the pool's creator. So the creator's
   switch to private (`SetPermissioned`, in both directions, at any time) never locks the members out of adding capital and never blocks a seat trade between members: it decides who may ENTER. Revoking an entry stops an address that has not joined yet, not a member.
4. *Refusals write nothing.* Every check of `BuyPosition` (rules 1 to 3, the escrow of the ledger, the roster's room, the join fee, the seat row the buyer would need, the seller's seat and its vote lock) comes before the sync of the holding, which is the first write; an offer that
   is exactly the one that stands is not written again.

**A NO voter is never bought in, and neither is an abstainer.** Their money sits out of that
acquisition and stays withdrawable (a NO voter's after `Tally`) or available for a later proposal they
do support. The holding's `costBasis` is set to what was actually spent (section 4f), so members
who were not charged never leave it overstated.

**Vote rows are deleted when the vote ends, and slot reuse is safe.** Proposal ids are array slots and
finished proposals are reused, so a vote row is keyed by the proposal (`openTick × 262,144 + slot`, which names one
proposal for ever), never by the slot alone. A row lives from `Vote` until the vote is over for good: `Tally`
deletes them all for a rejected, expired or sell/allocate proposal and the NO rows of a passed buy, `Execute` the
YES rows it has used (or the mandate failed or expired), `Propose` those of a slot whose proposal was left alone
past its life. A member's vote on the previous occupant of a slot can therefore never be mistaken for a vote on
the new one, and the vote table does not fill up with dead rows. `Vote`, `Tally` and `Execute` still name a
proposal by its slot (the ABI did not change): a transaction built for one proposal can land on a later one
that took the slot.

**A vote can still be wasted.** `Propose` does not require a `BUY_QX` budget to cover `price x shares`, nor a
`BUY_QSWAP` budget to exceed QSWAP's 100,000 QU flat fee: the order is sized at `Execute` to the money
that really votes YES. A swap whose output misses the (scaled) minimum is refused by QSWAP and `Execute` returns
`EXTERNAL_CALL`, changes nothing and leaves the proposal PASSED; a mandate whose funded part cannot buy one
share is closed (above). Until the life of a dead proposal is over (or it is closed) it counts as its proposer's
one live proposal.

**Membership is one fact (roster = record).** A member is on a pool's roster
(`members`, append-only, 256 slots per pool) exactly when he has a `Contribution`
record for the pool, and the record names his roster position (`memberIdx`).
Both are written by one private helper, `EnsureMember`, which `Contribute`,
`BuyPosition` and `DepositHoldingAsset` call once the call can no longer be
refused. Before it, a read-only `JoinCheck` tells the caller whether the address
is new and whether there is room (the caller then checks that the join fee is
covered), so a refusal costs nothing. No route can therefore put an address on
the roster without a record, or list it twice. The previous revision let
`BuyPosition` do the first: the buyer's later `Contribute` then listed him twice,
`Execute` charged him twice and `SettleIpo` refunded him twice, and a record-less
buyer on a full roster could neither contribute nor propose. (One other place
writes a record: `SettleIpo` gives a roster member who somehow has none a record
at his own position, so that his refund is not lost.)

Three defences stay in the roster loops anyway, each O(1) per member:
`Execute`, `SettleIpo` and the vote release (`Tally`, `Execute` and `Propose` call it) act on a roster slot only when it is the slot
the member's own record names; `Execute` refuses a buy whose payers' buy-ins add
up to more than the mandate or than the pool's uncommitted total (`BOOKS_INCONSISTENT`, before anything moves); and
`SettleIpo` never hands back more than is left to hand back.

### 4b. Capacities

| Constant | Value | Meaning |
|---|---|---|
| `QPOOL_MAX_POOLS` | 1,024 | pools |
| `QPOOL_MAX_CONTRIBUTORS_PER_POOL` | 256 | roster slots per pool |
| `QPOOL_MAX_HOLDINGS_PER_POOL` | 128 | holdings per pool |
| `QPOOL_MAX_HOLDINGS` | 131,072 | = pools × holdings per pool |
| `QPOOL_MAX_CONTRIBUTIONS` | 262,144 | (pool, member) records = pools × roster |
| `QPOOL_MAX_PROPOSALS` | 262,144 | = `QPOOL_MAX_CONTRIBUTIONS` — see below |
| `QPOOL_MAX_OWNERSHIP` | 524,288 | (holding, member) records; also the `votes` map size |
| `QPOOL_MAX_OFFERS` | 32,768 | open seat offers = pools × `QPOOL_MAX_OFFERS_PER_POOL` |
| `QPOOL_MAX_ADMITTED` | 262,144 | allow-list entries = pools × `QPOOL_MAX_ADMITTED_PER_POOL` |
| `QPOOL_MAX_OFFERS_PER_POOL` | 32 | **per-pool quota (K5):** open offers of seats in one pool; the 33rd NEW offer is refused with `CAPACITY` before anything is written. Changing an offer that exists takes no new place; a cancel, a fill and a settlement that removes the seat give the place back |
| `QPOOL_MAX_ADMITTED_PER_POOL` | 256 | **per-pool quota:** allow-list entries of one pool (a roster has 256 places) |
| `QPOOL_MAX_OWNERSHIP_PER_POOL` | 512 | **per-pool quota:** seat rows (member, holding) of one pool = `QPOOL_MAX_OWNERSHIP` / pools. A purchase writes one row for every member who pays, so a 256-member roster that all votes yes uses the budget in **two** purchases and the third is refused with `CAPACITY` before anything moves (a pool of 20 members makes 25). Rows are given back where they empty (a seat sold whole and claimed, a failed IPO's holding, a retired holding whose members have claimed). This is the cost of keeping the table at its size: the owner decides the launch profile (D6) |
| `QPOOL_MAX_DEPOSITS_PER_POOL` | 32 | **per-pool quota:** deposits of one pool (its creator's); less than a quarter of the 128 holding slots, so that acquisitions always keep slots |
| `QPOOL_MAX_IPO_HOLDINGS_PER_POOL` | 8 | **per-pool quota:** unsettled IPO holdings of one pool, one for each of the at most `QPOOL_MAX_IPO_TARGETS` = 8 IPOs that can be live; the 9th is refused with `CAPACITY` at `Propose` (advisory) and at `Execute` (binding, before anything is written) |
| `QPOOL_SQUAT_EPOCHS` | 4 | how old (in epochs) a payer's binding must be before `EvictSquat` can give it up (4d) |
| `QPOOL_MAX_SOURCES` | 16,384 | dividend sources (payers) with a live holding. **No new payer is admitted above `QPOOL_SOURCE_SOFT_CAP` = 12,288 (75%)** |
| `QPOOL_IPO_MAX_QUANTITY` | 676 | the core's limit on the quantity of an IPO bid (`NUMBER_OF_COMPUTORS`, the slots of an IPO book) |
| `QPOOL_IPO_MAX_PRICE` | 1,479,289,940,828 | the core's limit on the price of an IPO bid (`MAX_AMOUNT / 676`); 676 slots at this price cost 999,999,999,999,728 QU |
| `QPOOL_IPO_VOTE_WINDOW` | 86,400 ticks | the vote window of a `BUY_IPO`: 24 hours at the nominal tick of one second (every other kind: `QPOOL_VOTE_WINDOW`, 259,200) |
| `QPOOL_PROPOSAL_LIFETIME` | 432,000 ticks | the whole life of a proposal: `VOTE_WINDOW` (259,200, 3 days) + `EXECUTE_WINDOW` (172,800, 2 days); locks and mandates lapse by themselves after it. A `BUY_IPO` lives `QPOOL_IPO_PROPOSAL_LIFETIME` = 259,200 ticks (1 + 2 days) |
| `QPOOL_MAX_IPO_BIDS` | 16 | live bid prices of one IPO holding (the list in `ipoBids`). The owner's figure is three to five rebids in the last half hour, so 16 is three times that, with room for the prices that stay alive while their seats are upgraded (section 7a); a seventeenth is refused with `CAPACITY`, nothing moves, and room returns when a price dies |
| `QPOOL_MAX_IPO_HOLDINGS` | 2,048 | unsettled IPO holdings at one time, all pools together (the table `ipoBids`: one list of up to 16 prices per holding); `Execute` refuses a further one with `CAPACITY`. **The owner lowered it from 8,192** (state about 4.1 MB smaller): nobody expects that many active IPOs. It is NOT pools × quota (1,024 × 8): the per-pool quota above makes it cost 256 pools (2.56 billion QU of registration fees, plus the budget of every holding) to fill it, not one |
| `QPOOL_MAX_IPO_PRICES` | 32,768 | the registry of IPO bid prices: one entry per live bid price of an unsettled IPO holding = `QPOOL_MAX_IPO_HOLDINGS × QPOOL_MAX_IPO_BIDS` (a `static_assert` ties them), so it cannot be full while a holding has room for one more price. Sized with the table above (it was 131,072 with 8,192 holdings) |
| `QPOOL_MAX_IPO_TARGETS` | 8 | target contracts with unsettled IPO holdings at one time (the table END_EPOCH snapshots); a bid in a ninth is refused with `CAPACITY` |
| `QPOOL_MAX_IPO_SEATS` | 8,192 | snapshot rows, one per (target, bid price) at which QPOOL holds slots: at most 8 × 676 are ever used (a `static_assert` keeps the table under 80% full) |
| `QPOOL_KNOWN_CONTRACTS` | 30 | rows in the embedded contract table (4d) |
| `QPOOL_MAX_CONTRACT_INDEX` | 1,023 | highest possible contract index (the protocol caps contracts at 1,024) |
| `QPOOL_MAX_CONTRACT_NAMES` | 2,048 | name → index bindings for contracts above the table (at most 993 can exist) |

The proposal array equals the number of (pool, member) pairs that can exist.
With one live proposal per member per pool, that is the most proposals that can
ever be open at once, so legitimate load can never exhaust the array — only
reclaimable congestion from finished-but-not-yet-reused slots. The cost of that
sizing is the ~175 MiB state and the fact that `Propose` scans the whole
proposal array (section 6).

The payer table keeps empty slots on purpose. Every transfer to the contract
whose sender is *not* a registered payer is a hash miss, and a miss probes up to
the first empty slot; the core documents constant time below ~80% fill and
near-linear above ~90%, so no new payer is admitted at 75% (`CAPACITY`). Holdings
that retire free their payer slot, and `END_EPOCH` compacts the table.

**State size: where the 190,201,464 bytes go** (measured with `sizeof`):

| Field | Bytes | MiB |
|---|---|---|
| `proposals` (262,144 × 168) | 44,040,192 | 42.0 |
| `ownership` (524,288 slots) | 42,074,128 | 40.1 |
| `votes` (524,288 slots) | 29,491,216 | 28.1 |
| `holdings` (131,072 × 184) | 24,117,248 | 23.0 |
| `contributions` (262,144 slots) | 23,134,224 | 22.1 |
| `admitted` (262,144 slots) | 12,648,464 | 12.1 |
| `members` (1,024 × 256 ids) | 8,388,608 | 8.0 |
| `ipoPrices` (32,768 slots) | 794,640 | 0.76 |
| `ipoBids` (2,048 × 280) | 573,968 | 0.55 |
| `ipoSeats` (8,192 slots) | 264,208 | 0.25 |
| `sources` (16,384 × 144) | 2,363,408 | 2.3 |
| `offers`, `pools`, `contractNames`, `ipoTargets`, scalars | 2,311,152 | 2.2 |

(The last row sums offers 2,105,360, pools 172,032, contractNames 33,296, ipoTargets 320 and 144 bytes of scalar fields.) `Holding` is 184 bytes (it was 176: step K5 added `rows`),
`SourceLedger` 104 (`bindEpoch`), `Pool` 168 (five counters of the shared tables), `Offer` 24 (`poolId`), `IpoBids` 272 (a count, the seat sum and two arrays of 16), `Ownership` 40 (it was 32: the seat lock `voteLockUntilTick`, 4,194,304 bytes in all), `Contribution` 48 (it was 32:
`memberIdx`, the member's position in the roster, came with K1; the lock expiry, the lock start and the last deposit tick came with K3, three 32-bit ticks,
4,194,304 bytes in all). History: K2 took 9,437,184 bytes out of the holdings and put 3,178,512 into the registry of IPO bid prices; K3 added 8,388,608 bytes (192,361,216 then);
K4 added 264,528 (the snapshot rows and the table of IPO targets); KI added 4,458,512 and KI2 took 3,211,264 out again (193,872,992 at KX). **Step K5**: the owner's lower IPO cap took **4,105,728 bytes** out
(`ipoPrices` 3,178,512 to 794,640, `ipoBids` 2,295,824 to 573,968: 189,767,264), and the quotas added **434,192 bytes**: `Holding.rows` +1,048,576, the dead `poolHoldings` table −1,048,576,
`Offer.poolId` +262,144, `SourceLedger.bindEpoch` +131,072, five counters in each of the 1,024 pools +40,960 and two cursors +16 (190,201,456). **The K5 repair added 8 bytes** (`holdingSerial`, the counter of holdings created): the holding's incarnation and the proposal's tag cost nothing, because `Holding.rows` became
a 32-bit number that shares its 8 bytes with the new 32-bit `incarnation` (a roster has 256 members at most), and `Proposal.kind` (a number from 0 to 6) became 32 bits and shares its 8 bytes with the new 32-bit `holdingTag`;
`Holding` is still 184 bytes and `Proposal` 168 (measured with `sizeof`). **190,201,464 now, 3,671,528 bytes (1.9%) less than at KX.**

### 4c. How a dividend is split (source ledgers)

The hook (`POST_INCOMING_TRANSFER`) sees only `{sender, amount, type}`. It cannot
see which asset a payment is for or which pool owns it — and several holdings,
in several pools, can own shares of the **same** asset (two pools winning seats
in one IPO is the normal case). The previous revision pointed each payer at ONE
holding, first registered wins, so the first pool took the whole dividend of a
shared asset. That was a bug, not a design choice. QPOOL now keeps one **source
ledger** per payer and splits every dividend over all holdings of the payer's
asset.

**Sources.** A source is `{payer, kind}`. The key is always *derived* from the
holding (`ipoContractIndex`, `payerIndex` or `asset.issuer`); nothing stores a
holding id, so a reused holding slot can never be a stale target.

| Kind | Payer | A payment belongs to it when | A holding belongs to it when |
|---|---|---|---|
| CONTRACT (1) | `id(index, 0, 0, 0)` | the hook is called with type `qpiDistributeDividends` from that address | it holds contract shares — won in the IPO, bought on QX, or deposited — whose paying contract is `index` |
| TOKEN (0) | the asset's issuer | any other transfer from that non-zero address (a plain transaction or a `qpi.transfer`) | it holds a token issued by that address |

A ledger holds only aggregates (96 bytes): the cumulative net dividend per
recognised share (× 2^64), the total ever moved into it and the total holdings
have pulled out (so a pull can never exceed what arrived), the one asset name the
source is bound to, how many holdings are registered, their **recognised shares**
(`Holding.shares`, summed), how many IPO holdings are unsettled, the escrow, and
the shares of unreconciled QX bids.

**On arrival** the hook does constant work, with no loop over holdings, members
or pools:

1. A sender that is not a registered payer — or a payer with nothing recognised
   and nothing waiting to be — is not paying on shares QPOOL holds: the whole
   amount goes to `globalUnattributed`, with no fee.
2. Otherwise 1% (floored) goes to revenue.
3. The rest advances the accumulator by `net / recognised`, except for the part
   set aside as escrow (below).

**Pulling.** A holding takes `shares × (accumulator − its snapshot)` from the
ledger the next time anything reads or changes its weight (`SyncHolding`), clamped
to what the ledger has really taken in, and adds it to its own
`payoutPerCostX64`. Every code path that changes `shares`, `pendingShares` or
`costBasis`, or reads the accumulator for a payout, syncs first; `Holding.shares`
is written only by two helpers (`RegisterHolding`, `ApplyHolding`). This is the
one discipline a future edit can break silently (section 10).

**Worked example.** Two pools hold shares of the same IPO-won contract, both
already settled: pool A's holding has 300 shares, pool B's 200. A dividend of
1,000,000 QU arrives in one hook call. Fee 10,000; net 990,000; the accumulator
rises by 990,000 / 500 = 1,980 per share. Pool A's holding earns 300 × 1,980 =
594,000 and pool B's 200 × 1,980 = 396,000, and each pool's members then split
their holding's amount by cost. (The previous revision gave A's holding all
990,000 and B's nothing.) When the same distribution reaches QPOOL as several hook
calls — `distributeDividends` calls the hook once per possession record, and
QPOOL's shares of one asset can sit under QX, QSWAP and QPOOL management at once —
the result is the same up to rounding per call
(`DividendSplitIsIdenticalWhenItArrivesAsSeveralHookCalls`, and the same dividend
through the real `distributeDividends` of a test contract in
`TwoPoolsSharingIpoTargetSplitRealDividendProRata`).

**Stray shares.** QPOOL accepts any transfer of management rights to it
(`PRE_ACQUIRE_SHARES` allows everything), so its real possession of an asset can
exceed the recognised shares. The hook divides by the *recognised* shares only, so
shares nobody has registered simply raise the recognised holders' take and
cannot hold a dividend back (`StrayDonatedSharesDoNotHoldBackDividends`); the one
exception is while a QX bid is unreconciled, below.

**What is set aside (escrow) instead of credited.** Two cases, and both are paid
out exactly:

1. **IPO-won shares, until every IPO holding of that contract has been through
   `SettleIpo`.** Shares won in an IPO arrive at the epoch change with no callback,
   so until each holding is settled the ledger does not count them. Every dividend
   in the meantime waits whole; the settlement that clears the *last* unsettled
   holding releases the escrow per recognised share. **A sole IPO holder's first
   dividend waits for `SettleIpo` too.** `SettleIpo` is permissionless and callable
   from the first epoch after the IPO's (the END_EPOCH snapshot exists by then, 7a).
   A holding that cannot be settled (`NAME_UNPROVEN`, 7a) keeps its ledger unsettled
   and so keeps the ledger's dividends parked; they are not lost.
2. **Fills of an unreconciled QX bid.** QX fills silently. While a ledger has an
   unreconciled bid, the hook counts QPOOL's real possession of the asset (it walks
   the possession records with `AssetPossessionIterator`) in excess of the
   recognised shares, caps that at the bid's pending shares, and sets that
   proportion of each dividend aside. `ReconcileHolding` (permissionless)
   recognises the fills and pays the whole escrow to the bid's holding. This is
   exact because only one bid per payer may be unreconciled (below).

**The freeze.** While a holding's source has escrow, `BuyPosition` (the buyer is
refunded), `SELL_QSWAP` and `SELL_QX` (the proposal stays PASSED and can be retried
inside its window) return `SETTLEMENT_PENDING` (24). Escrowed dividends belong to
whoever held the shares when they arrived, and a seat that changes hands, or
shares that leave, before the release would hand them to the wrong people.
`ClaimPayout`, `OfferPosition`, `CancelOffer` are never frozen. Anyone can end a
freeze by calling `SettleIpo` or `ReconcileHolding`; `GetSource(...).escrow` and
`GetHolding.sourceEscrow` show whether one is on.

**Registration rules (`CheckSource`).** Run at `Propose`, again at `Execute`, and
at `DepositHoldingAsset`, always before any capital or asset moves, so a refusal
is free:

- *One payer, one asset* (`SOURCE_CONFLICT`). A transfer does not say which asset
  it is for, so a source is bound to a single asset: a second asset of the same
  issuer is refused while the first is held. The binding frees when the last holding
  of the source retires. (A pool can hold a cheap real sibling asset of an issuer
  to keep that issuer's other assets out of QPOOL — section 10.)
- A TOKEN payer may not be QPOOL itself, QX or QSWAP: they pay QPOOL as a side
  effect of its own calls (refunds, sale proceeds) and those would be mistaken for
  dividends.
- No new payer above 75% fill of the payer table (`CAPACITY`).
- **One unreconciled QX bid per payer** (`QX_BID_PENDING`). QX merges same-price
  bids of one entity and says nothing about whose is whose, so two outstanding bids
  could not be told apart. A bid that never fills (there is no cancel path)
  blocks further `BUY_QX` on that payer until it fills. `BUY_QSWAP` and deposits
  are never blocked by it.
- Contract-share pairing (4d) and, for a CONTRACT ledger that mixes IPO-won,
  bought and deposited shares: bought or deposited shares may only *join* when no IPO
  holding of that contract is unsettled (`SETTLEMENT_PENDING`; with an unsettled
  holding every dividend is parked, and a late joiner would be paid arrears from
  before it existed), and an IPO bid may not join a ledger with a QX bid in
  flight or escrow parked for a QX fill (`SETTLEMENT_PENDING`; the escrow would be
  released to the wrong holders).

**Lifetime.** A holding leaves its ledger (retires) when its `SettleIpo` wins nothing
(a holding that has shares is never retired, K4), or when a sale leaves it with no
shares and no pending shares. When the last holding of a source leaves, the ledger is
removed; any escrow still in it belongs to nobody and is booked as revenue
(`totalUnownedSwept`), and so is what the ledger took in for its recognised shares and
no holding ever credited (rounding, and the slice of a share no holding carried).

**What does not go through a ledger.** Sale proceeds are credited to the one
holding that sold. A payment the hook cannot match — an unregistered sender, a
payer with nothing recognised, a contract that pays its shareholders with
`qpi.transfer` instead of `qpi.distributeDividends`, `revenueDonation` — lands in
`globalUnattributed`, which has no exit (section 10). **Dividends are attributed by
the sender**: only the asset's issuer (TOKEN) or the paying contract through
`qpi.distributeDividends` (CONTRACT) is recognised, so a treasury or distributor
wallet, QUtil `SendToMany`, or a contract that pays by `qpi.transfer` is not.

**Why `qpi.numberOfShares` is banned in the hook and in every public function.**
The core calls the hook from *inside* `qpi.distributeDividends` while it holds the
universe lock, a non-reentrant spinlock that `qpi.numberOfShares` and
`qpi.numberOfPossessedShares` also take: calling either there hangs the contract
processor (reproduced with a negative control in the harness). A public function
holds the contract's state *read* lock while it waits for the universe lock, and
the hook that holds the universe lock waits for the state *write* lock; that
deadlock is read from the core's `contract_exec.h`, not executed. So the hook reads
the universe only with `AssetPossessionIterator`, which takes no lock. Its cost is
constant except for a ledger with an unreconciled QX bid, which pays one pass over
QPOOL's possession records of that asset; the pass grows with the probe cluster in
the core's assets table (measured in the harness on the identical loop: 0.47 µs
with an empty universe, 5 µs with 1,000 unrelated assets owned by QPOOL, 95 µs with
20,000 — not measured on a node). A wallet transaction to the contract blocks the
tick processor until the hook returns, so this is on the critical path.

**Views.** `GetHolding.totalPayouts` and `GetPosition.owed` include what a sync
would credit right now, so they agree with what `ClaimPayout` pays; they exclude
escrow. `totalPayouts` is floored per sync and can read 1 QU low.

### 4d. Contract shares (assets whose issuer is NULL_ID)

Contract shares are the shares a contract issues in its own IPO (`QX`, `QUTIL`,
...). The core issues them with issuer `NULL_ID`; the contract pays its
shareholders through `qpi.distributeDividends`. QPOOL treats them as follows.

**Every pool holding them is paid.** All holdings of one contract's shares —
IPO-won, bought on QX, deposited — earn from the same CONTRACT ledger
`id(index, 0, 0, 0)` and split a dividend by shares, whichever way they came in.

**They can only be sold through QX.** QSWAP swaps issuer-issued tokens only.
`BUY_QSWAP` of contract shares is `INVALID_INPUT` at `Propose`; `SELL_QSWAP` of a
holding of contract shares is `QX_ONLY` (27) at `Propose` and again at `Execute`
(before anything is written or any call is made). The only way out is `SELL_QX`
(4e). Seats in any holding, a deposited one included, can still change hands on the
seat market without the shares moving.

**Three ways in.** Each needs the paying contract's index, because the asset name
alone does not say which contract pays on it and contracts cannot read
`contractDescriptions`:

| Way in | Where the index comes from | Notes |
|---|---|---|
| IPO win | the `BUY_IPO` target (`holdingId`) | The shares arrive at the epoch change **already managed by QX, with no callback to QPOOL** (the core writes the ownership and possession records directly). The holding has `shares = 0` until `SettleIpo` recognises them, from the snapshot END_EPOCH took of the book (section 7a) |
| Buy on QX | `holdingId` of a `BUY_QX` proposal whose asset is `{NULL_ID, name}` | An order may fill at once or rest; resting shares are pending until `ReconcileHolding` |
| Deposit | the new `contractIndex` field of `DepositHoldingAsset` | The pool's CREATOR (nobody else can deposit, step K5) first hands his shares to QPOOL's management with QX `TransferShareManagementRights` (IPO-won shares sit under QX). He can deposit his own contract shares and sell parts of the seats to entrants through `OfferPosition`; shares that a refused deposit leaves under QPOOL's management come back with `ReleaseManagedShares` |

For a token the index fields must be 0 (a non-zero value is `INVALID_INPUT`; the
previous revision silently ignored `holdingId` on token buys).

**Nothing in QPOOL needs updating when a contract is added to Qubic.** Indices
only ever come from proposals and deposits; the embedded table below is a check on
them, not a registry.

**The pair check.** A contract's address is `id(index, 0, 0, 0)`, so the address
carries no extra information; the link that cannot be verified is *asset name ↔
index*. At `Propose`, `Execute` and `DepositHoldingAsset`, before anything moves:

- the index must be 1 to 1,023 and not QPOOL's own, the name 1 to 7 bytes
  (`INVALID_INPUT`);
- **index 1 to 30: exact.** The pair must match the embedded table; any other name
  is `CONTRACT_MISMATCH` (28). A name the table lists is also refused at any other
  index;
- **index above 30: the link cannot be proved on chain.** QPOOL keeps the pairing
  one-to-one instead: a name bound to one index is refused for another, and a ledger
  bound to one name refuses a second, so no two live ledgers count the same share
  asset (first claim wins; the claim is released when the ledger goes). For shares
  that already exist (QX buy, deposit) it also asks the core, with `qpi.ipoBidPrice`,
  that the node build lists a contract at that index and that the current epoch is
  not that contract's IPO epoch (`CONTRACT_NOT_LIVE`, 29). That rules out indices
  nobody has deployed and a contract that is mid-IPO; note it cannot tell a contract
  past its IPO from one whose IPO has not started yet, because the core answers
  "not in its IPO epoch" for both. For an IPO bid it requires that the declared name
  is not already an issued asset (the IPO's own shares do not exist yet, so an
  issued name belongs to some other contract), and `SettleIpo` proves the name before it
  recognises any seat under it: the ledger is not bound to another name, the pairing is possible (table, and
  no other ledger owns the name) and QPOOL holds at least recognised + seats shares of it, otherwise
  `NAME_UNPROVEN` and nothing is written (section 7a).

**The embedded table.** Mainnet contract index → share asset name, copied from
`explorer.qubic.org` (smart contracts page) on 2026-10-17, packed as little-endian
ASCII in a `uint64` (`QX` = 22,609):

| Index | Name | Index | Name | Index | Name |
|---|---|---|---|---|---|
| 1 | QX | 11 | MSVAULT | 21 | QRP |
| 2 | QTRY | 12 | QBAY | 22 | QTF |
| 3 | RANDOM | 13 | QSWAP | 23 | QDUEL |
| 4 | QUTIL | 14 | NOST | 24 | PULSE |
| 5 | MLM | 15 | QDRAW | 25 | VOTTUN |
| 6 | GQMPROP | 16 | RL | 26 | QUSINO |
| 7 | SWATCH | 17 | QBOND | 27 | ESCROW |
| 8 | CCF | 18 | QIP | 28 | GGWP |
| 9 | QEARN | 19 | QRAFFLE | 29 | QPAYHUB |
| 10 | QVAULT | 20 | QRWA | 30 | QTREAT |

The table is the one thing in the contract that is tied to a network. **Rows 1 to
28 match the local core checkout; rows 29 and 30 do not** (the checkout lists QTREAT
at 29 and QPOOL at 30). A row that is wrong for the network you deploy on refuses
the genuine pair at that index and accepts the pair the row names — which still
needs genuine shares of that name — so check it against the tree you deploy, and
re-check it for any network whose contract list differs.

**What this does and does not protect (indices above 30).**

- *Guaranteed:* the index is a contract the node build lists and that is not in its
  IPO epoch right now; one name maps to at most one index among live ledgers; and,
  if the table is right for the network, a listed (index ≤ 30) contract can never be
  mislabelled.
- *Not guaranteed:* that the name really belongs to the index. **The vote is the only
  check, so voters on a `BUY_QX` or a deposit must verify the index of an unlisted
  contract themselves.** A pool that buys or deposits shares under the wrong index
  earns nothing on them in QPOOL: the dividend arrives from the real contract, finds
  no ledger and is filed under `globalUnattributed` (no exit), and the wrong claim
  keeps the real pair out until that holding is sold.
- *Squatting.* A payer's ledger is bound to ONE asset name, and an unlisted contract's name is bound to ONE index, by whoever registers first. Only the
  pool's creator can bind one by a deposit (step K5, D16), and a pool costs 10,000,000 QU, so a squat costs a pool and one genuine share. Effect:
  denial of service only -- QPOOL cannot take the other asset of the same issuer (`SOURCE_CONFLICT`), or X's real shares or a bid in X's IPO (a deposit of a
  genuine share of an unlisted contract Z under X's index claims the pair (X, Z's name); the real pair of X is refused with `CONTRACT_MISMATCH`), while that
  holding exists. Nobody's funds move. **`EvictSquat(holdingId, rivalName)` (anybody, step K5) gives up a binding that never paid**, when ALL of these hold,
  each an O(1) lookup: the holding is a DEPOSIT and the only holding of its ledger; the ledger never took in a dividend, has no escrow and no QX bid; the
  holding was never paid or sold and its depositor, the pool's creator, still owns every seat and has no other row in it (nobody paid for a seat);
  and the binding is *contested* -- TOKEN ledger: the caller owns a share of ANOTHER asset of the same issuer (`rivalName`, issued, held by the caller) and the
  binding is at least `QPOOL_SQUAT_EPOCHS` = 4 epochs old; CONTRACT ledger: the contract at that index is in its IPO epoch NOW (its shares cannot exist before the IPO
  ends, so a ledger that holds shares of it, bound by a deposit made before the IPO began, is wrong beyond doubt). The depositor loses nothing: his shares go back to him in full (still
  managed by QPOOL: `ReleaseManagedShares` takes them to QX) BEFORE any book is touched, and nothing is written if the return fails. An honest early depositor
  is therefore evicted only by somebody who really needs the binding and only after it stood unused for four epochs; never while the asset has paid, while a seat has been
  sold, while another pool holds the same asset, or if the holding was bought by a vote. **What is not covered:** a squat of an unlisted contract's index after that contract's IPO
  is over cannot be proved wrong on chain (the name-to-index link above index 30 cannot be checked, owner decision D9): it stays until its creator releases it (the fix is the embedded table before the IPO).
  **Also not covered:** a binding made by a vote -- a pool of one member that buys a dust share of the issuer's other asset on QX with its own money (a pool, a vote and the share). Those
  shares sit under QX's or QSWAP's management, which QPOOL cannot take back, so nothing can return them; only the squatter's own `SELL_QX` releases the binding.
  Claiming an index no node build lists is blocked.
- *IPO late binding.* The name in a `BUY_IPO` vote cannot be checked until shares
  exist, so the first `SettleIpo` that wins shares binds the ledger to its declared
  name. Names the table lists, names already issued and names another ledger claims
  are refused, and (K4) a holding whose declared name nothing proves is no longer
  retired with its capital stranded: it is left unsettled with `NAME_UNPROVEN`.
  What remains: when two UNLISTED contracts' IPO windows overlap and a pool declares
  the *other* contract's still-unissued name for its own IPO, the first settlement
  that finds enough shares of that name in QPOOL binds the ledger to it, whoever
  declared it for what. (Step KX reproduced this on two real books that end in the same epoch,
  `KxADeclaredNameThatBelongsToTheOtherSimultaneousIpo...`: the honest holdings of BOTH IPOs are then
  `NAME_UNPROVEN` and stay so; the test has a switch, `D9` in `kBugs`.) Only the embedded table is exact, so the real cure is to add
  a contract to the table before its IPO, or to reserve declared names (a denial of
  service instead of a theft). **Owner decision (D9).**
- *A holding that cannot prove its name stalls its ledger.* A holding that won seats
  under a name nothing proves stays unsettled for as long as that is so (there is no
  rename or abandon path; the audit's vote-gated rename was not built). The members'
  capital stays on the books (cost and seats unchanged), but the ledger of that IPO
  stays `unsettled` and so keeps ALL of its dividends parked (every pool's); once a dividend has
  been parked the seat market and the sells of the other holdings of that IPO are frozen
  (`SETTLEMENT_PENDING`), and bought or deposited shares of the same contract cannot join its ledger.
  Another holding of the same IPO that declared the right name settles normally; the
  stuck one then stays stuck, because the ledger is bound to the other name. Nothing
  is lost and nothing can be stolen this way, but one mistaken or deliberate wrong
  name (cost: a pool and at least one won seat) delays the dividends of every pool on
  that IPO until a rename/abandon path exists. **Owner decision: build one.**

### 4e. Selling on QX (`SELL_QX`): an immediate sale

The previous revision's `SELL_QX` placed a resting ask on QX, removed the shares
from the holding at once, and expected the proceeds to arrive later and be routed
by sender — which sent them to whichever holding registered first. It is replaced
by a sale that completes inside one `Execute` call, so the shares that left and the
QU that arrived are both known exactly and credited to the one holding that sold,
the way `SELL_QSWAP` does it. **No ask of QPOOL ever rests on the book.**

**The mandate.** `price` is the lowest price per share QX may fill at — QX itself
enforces it, so it is the binding protection for every share. `quAmount` is a floor
on the *total* QU the visible bid book must pay **now**, after QX's trade fee and
before QPOOL's own 1% fee and any hand-over fees. It is checked before anything
moves (a sale cannot be undone afterwards), and it also stops a griefer from
spending the mandate on a one-share fill. `Propose` requires `price > 0`,
`quAmount > 200` (two possible hand-over fees) and `price × shares` below QX's
1e15 limit, and makes no call into QX.

**What `Execute` does**, in order:

1. *Reads only* — a refusal here writes nothing, pays no fee, returns the reward,
   and leaves the proposal PASSED so it can be retried inside its window: the
   holding is active and holds the shares (`INSUFFICIENT_BALANCE`); its source has no
   escrow (`SETTLEMENT_PENDING`); QPOOL really owns the shares, whoever manages them
   (`INSUFFICIENT_BALANCE`); a plan for putting them under QX's management; and a
   replay of QX's matching against the first 256 bids of its book at the limit price
   — if the book cannot pay `quAmount` (or the hand-over fees) the answer is
   `NO_FILL` (30).
2. *The management plan.* QX only sells shares it manages. Shares already under QX
   (IPO-won contract shares, shares bought on QX) need nothing. Deposited shares are
   under QPOOL's own management and are released to QX (`qpi.releaseShares`, QX fee
   100 QU); shares bought on QSWAP are handed over by QSWAP's
   `TransferShareManagementRights` (100 QU). One fee per source, at most two. If part
   of the shares sits under any other contract's management the sale is refused
   before any fee is paid.
3. *The sale.* One ask at the limit price. QX matches it against resting bids best
   first, each fill at that bid's own price (never below the limit), and pays QPOOL
   for each fill during the call. `Execute` then reads QX's own book for how much of
   the ask did *not* fill and cancels exactly that with `RemoveFromAskOrder`. If the
   sale reaches past the 256 bids that were replayed, only what that window takes is
   offered, so the replay is exact for everything the ask can reach; the remainder
   needs a new proposal.
4. *Recording.* The shares that sold leave the holding through `ApplyHolding`, which
   first credits what the holding earned on them. The QU is the **balance
   difference** across the call, so QX's trade fee (0.3%, charged per fill) and any
   hand-over fee are already out of it; the 1% management fee comes off, and the rest
   rides the holding's accumulator. `Execute_output.sold` is the shares sold,
   `proceeds` the net credited.

**Facts about the result.**

- *What sold is measured from QX's book, not from QPOOL's share count.* A fill
  against QPOOL's own resting bid moves shares from QPOOL to QPOOL and leaves the
  count unchanged (`SellQxCrossingThePoolsOwnRestingBidIsAccountedFromQxsBook`); the
  share count is only an upper bound and a fallback.
- *A partial fill uses the mandate up* (EXECUTED); the unsold remainder needs a new
  proposal and a new vote. Zero fill, or a book below the minimum, is `NO_FILL`
  with nothing changed, and the same mandate can be executed later once the book
  improves.
- The credited amount can be below `quAmount`, which is a floor on the book's
  payment, not on what the holding nets after the 1% and any hand-over fees.
- *Costs the holding bears:* QX's trade fee, the hand-over fees, the 1% management
  fee. A holding whose shares fully sell retires and, if it was its ledger's last,
  releases the ledger and any contract-name claim.
- *If QX refuses the ask after a hand-over* (the pre-checks make this unreachable;
  a test forces it with an ask of QPOOL's own that reserves the shares), nothing
  sells, the proposal stays PASSED, the hand-over fee is lost to QX and deducted from
  `pendingRevenue` so no holding's books are short. *If the cancel ever failed after
  a sale*, the sale is still recorded, the proposal is marked EXECUTED and
  `EXTERNAL_CALL` is returned; that path cannot be reproduced in the harness.
- *Before calling `Execute`, check QX's bid book* for the asset. A refused call costs
  the caller nothing (the reward comes back). Members should choose the price and
  the minimum with the 256-bid window and the 0.3% fee in mind.

`SELL_QSWAP` is unchanged in shape: the holding's source must have no escrow
(`SETTLEMENT_PENDING`), minimum proceeds must exceed 100,100 QU, QPOOL releases or
requests the hand-over of the shares if QSWAP does not already manage them, and the
holding is credited the balance difference after the 1% fee. Once shares have left,
they always leave the holding's weight, whatever the proceeds turn out to be. One
gap remains: a management hand-over fee paid before a swap that then fails is not
recorded against anyone (section 10).

### 4f. Buys: what is charged, and what comes back

A passed buy runs in a fixed order so a failure leaves no trace: every check first,
then a read-only pass that works out each member's buy-in (only the members who voted YES pay, each
for at most the weight he voted with: section 4a), then the one external call that can
fail, and only then the writes. A call that fails before it runs, a refused order,
a swap that bought nothing, or a reported success that spent nothing all end in
`EXTERNAL_CALL` with **no capital moved, no holding created and the proposal still
PASSED**.

The members are charged **what was really spent**, not what was assigned. QX and
QSWAP hand back whatever they did not use — QX: the excess of the budget over
`price × shares` and every price improvement; QSWAP: the whole amount when it
refuses the swap (a successful swap spends everything, its own flat fee included) —
by `qpi.transfer` during the call, which reaches the hook as an ordinary arrival
from their address and is filed under `globalUnattributed`. `Execute` reads that
refund back (the smaller of the contract's balance difference and what the hook
parked, never more than was handed over), and then:

- (`assigned` is the funded part of the mandate: `min(quAmount, what the YES voters can pay)`, section 4a; the order was sized to it.)
- `spent = assigned − refund` becomes the holding's `costBasis`, the pool's
  `committedTotal` increase and `totalDeployed`;
- each member's `cost` and `committed` are their share of `spent`, split by
  **running-total rounding** — `cost(i) = floor(P(i) × spent / assigned) −
  floor(P(i−1) × spent / assigned)` where `P(i)` is the sum of the buy-ins of the
  roster up to member `i` — so the costs add up to `spent` exactly, each is within
  one QU of its exact pro-rata value, and none exceeds the member's buy-in;
- the refund stays in the members' `uncommitted` balances (nothing is paid out) and is
  taken out of `globalUnattributed` again; `Execute_output.refunded` reports it.

Example (`BuyQxRefundSplitsExactlyOverThreeMembers`): a budget of 200,000 is split over three YES voters by running total into buy-ins of 100,000 + 66,666 +
33,334 = 200,000 (since K3 the buy-ins add up to the budget exactly; before, each was floored on its own and 199,999 were assigned). A bid of 57 shares at 1,234 = 70,338. QX returns
129,662. The members' costs are 35,169 / 23,445 / 11,724, which add up to 70,338
exactly (plain per-member floors would give 70,336 and leave 2 QU of real money
unowned). Dividends are unaffected by the cost split: they follow shares.

What is *not* returned: the part of a `BUY_QX` budget that sits in a resting bid
(`price × unfilled shares`) counts as spent, and stays in QX until the bid fills —
there is no cancel path (section 10). `BUY_IPO` is separate: the whole budget is
committed, refunds arrive as `ipoBidRefund` into a shared pot, and `SettleIpo`
returns the unspent part and what the book gave back, worked out from the snapshot
(section 7a).

### 4g. Error codes

Append-only; numbering never changes. 23 to 38 are new in this revision.

| Code | Name | Meaning |
|---|---|---|
| 0 | `OK` | |
| 1 | `INVALID_INPUT` | malformed or out-of-range input; for `BUY_IPO` also a `price` outside 1 to 1,479,289,940,828 (the core's own limit) or a budget below the opening price (not one seat), at `Propose` and again at `Execute` (`shares` is not read); for `RebidIpo` a price of 0 or above the maximum, or a quantity above 676 (the core's); for contract shares also an index outside 1 to 1,023 or QPOOL's own, a name that is not 1 to 7 bytes, a non-zero index on a token, `BUY_QSWAP` of contract shares; at `Execute`, a buy of an asset that is not issued; `BuyPosition` with `expectedCostUnits` 0 (an input of the layout before K6 reads as 0) |
| 2 | `CAPACITY` | a table, roster or the payer table (above 75% fill) is full; a newcomer (by `Contribute` or `BuyPosition`, or the creator's first `DepositHoldingAsset`: see the lock-out in section 10) on a pool whose 256-member roster is full; **a pool at one of its quotas (4b): a 33rd offer, a 257th allow-list entry, a purchase or deposit beyond the pool's 512 seat rows, a 33rd deposit, a 9th unsettled IPO holding, a holding beyond the pool's 128 slots** |
| 3 | `NOT_FOUND` | pool, holding, ledger or proposal missing, or not this pool's; `Vote` or `Execute` of a SELL or ALLOCATE whose holding slot holds another holding now (another pool, another asset, or the same asset created in the slot later: another incarnation) |
| 4 | `NOT_MEMBER` | caller has no seat / no pool membership for this action |
| 5 | `INSUFFICIENT_FEE` | `CreatePool` with less than the fee; `BuyPosition` by a newcomer without the join fee (less than ask + fee attached, but at least the ask) |
| 6 | `INSUFFICIENT_BALANCE` | not enough uncommitted QU, shares, or contract balance; a sale whose shares sit under a contract QPOOL cannot move. `RebidIpo`: the rebid money rule (section 7a): a rebid is funded only by money that has ALREADY come back, so `price × quantity` is more than the holding's free funds plus what the call's pass finds has come back from the refund pot (the refunds of the seats this very bid pushes out are not counted), or the default quantity (`quantity = 0`) buys not one seat. It is refused from the holding's own list, without the pass, when even the most the list lets the holding have cannot pay for it; otherwise it is refused AFTER the pass (for example when the pot holds only another pool's money). Nothing is written either way |
| 7 | `WRONG_STATUS` | proposal not in the required state (also: tallied/executed after it was closed, expired or failed); `ReconcileHolding` on an IPO holding; `OfferPosition` or `BuyPosition` on a RETIRED holding (sold out or an IPO that won nothing: `active` is 0, its cost units can never earn again; step K6); `CloseEmptyPool` of a pool that was used; `EvictSquat` when the binding is not a never-used, contested squat |
| 8 | `WINDOW_OPEN` | `Tally` before the vote window ends; `SettleIpo` while the auction is still running: no snapshot yet and the epoch is the one of the first bid, or the snapshot's own epoch (nobody can end a running auction for a pool) |
| 9 | `WINDOW_CLOSED` | `Vote` after the window; `Execute` after the execute window, which ends `EXECUTE_WINDOW` ticks after the VOTE closed (the proposal flips to EXPIRED and its vote rows go) |
| 10 | `ALREADY_VOTED` | |
| 11 | `EXTERNAL_CALL` | a call into QX or QSWAP failed or did nothing: a buy refused, a swap that bought nothing, a sale that sold nothing |
| 12 | `TRANSFER_FAILED` | a share hand-over or QU transfer failed (`ClaimPayout`, and since step K6 `WithdrawUncommitted`: the contract could not pay, both records are put back exactly as they were); `DepositHoldingAsset` of shares QPOOL does not manage; `ReleaseManagedShares` when the destination refused or asked for more fee than was attached (`requiredFee` says what it asked) |
| 13 | `NOTHING_OWED` | |
| 14 | `PRICE_TOO_HIGH` | `BuyPosition` with less attached than the ask |
| 15 | `NO_OFFER` | |
| 16 | `IPO_REJECTED` | `Execute` of a `BUY_IPO` and `RebidIpo`: the target's IPO is not running (checked before any scan), or its book has been read for the last time (END_EPOCH took the snapshot), or `bidInIPO` refused (no spectrum entry, ...) |
| 17 | `IPO_PRICE_TAKEN` | `Propose`, `Execute` and `RebidIpo`: an unsettled IPO holding already bids at this price on the same IPO. For `RebidIpo`, a price of the SAME holding is its own and is allowed (it adds seats); only another holding's price is taken (the registry of IPO bid prices; `Execute` and `RebidIpo` are the binding checks) |
| 18 | `IPO_NOT_ACTIVE` | `SettleIpo` and `RebidIpo` on a holding that is not an open, unsettled IPO holding (never was one, or already settled) |
| 19 | `ABOVE_CEILING` | **never returned**: an IPO has no ceiling price (step KI2, owner decision D1: the budget is the only limit). The number stays reserved so that numbering is append-only |
| 20 | `PROPOSAL_PENDING` | the member already has a live proposal in this pool: OPEN or PASSED and not yet past its whole life (`QPOOL_PROPOSAL_LIFETIME`) |
| 21 | `NOT_ADMITTED` | `Contribute` or `BuyPosition` by a newcomer to a permissioned pool who is neither on its allow-list nor its creator. A member of the pool is always admitted, whatever the list says (owner decision D7, step K6) |
| 22 | `NOT_AUTHORIZED` | `SetAdmission`, `SetPermissioned` or `CloseEmptyPool` by someone who is not the pool's creator; `DepositHoldingAsset` by anyone but the creator (everything attached is refunded and nothing is read or written) |
| 23 | `SOURCE_CONFLICT` | this payer is already bound to a different asset, or QPOOL/QX/QSWAP offered as a token payer |
| 24 | `SETTLEMENT_PENDING` | dividends for this source are parked awaiting `SettleIpo` / `ReconcileHolding`; also a deposit, QX buy or IPO bid that must wait for a ledger to settle (4c); and (step K6) `OfferPosition` or `BuyPosition` on an IPO holding that `SettleIpo` has not run for yet: its seats have no price until the settlement shrinks their cost units |
| 25 | `QX_BID_PENDING` | a QX bid on this payer has not been reconciled yet |
| 26 | `DISABLED` | never returned (reserved; kept so the numbering stays append-only) |
| 27 | `QX_ONLY` | contract shares can only be sold through QX |
| 28 | `CONTRACT_MISMATCH` | the asset name and the paying contract's index do not belong together |
| 29 | `CONTRACT_NOT_LIVE` | the paying contract is not in the node build, or is in its IPO epoch |
| 30 | `NO_FILL` | `SELL_QX`: the visible bid book cannot pay the minimum at the limit price now |
| 31 | `BOOKS_INCONSISTENT` | defence in depth, never expected. `Vote` found that the votes would add up to more than the weight the proposal opened with. `Execute` of a buy found that the payers' buy-ins add up to more than the mandate or than the pool's uncommitted total (the books of the pool are wrong): refused before anything moved, the proposal stays PASSED. `SettleIpo` found no row for the holding's target, or a snapshot that says the holding won more seats than it may hold or spent more than it bid: nothing written. `RebidIpo` found no list for an unsettled IPO holding, no row for its target, or a book that holds more of the holding's slots than the holding ever put in (the pass finds more seated than the books say the holding put into the book): nothing written; or the cost of the bid is above the contract's own physical balance (the free funds are physically in the contract, so a lower balance means the books and the balance disagree; the core would refuse the bid, the contract does not ask it): nothing written; or the registry and the holding's list disagree about a price (the registry names this holding as the owner of a price its list lacks, or its list has a price the registry has no entry for): nothing written; or, AFTER the bid, the contract's balance fell by more than `price × quantity` (the post-bid overspend report; the bid cannot be undone, so it IS recorded like any other and the caller is told; the core never does this today, see section 5, step KX). `Execute` of a `BUY_IPO` measured that the core took MORE QU out of the contract than `price × quantity` (a change of the core's behaviour; the bid cannot be undone, so it IS recorded and the proposal is used up, and the caller is told) |
| 32 | `SEAT_LOCKED` | `OfferPosition` of a seat, or `BuyPosition` against one, that voted on a SELL or ALLOCATE proposal whose vote window has not ended (section 4a). A refusal writes nothing and refunds everything |
| 33 | `DEPOSIT_AFTER_OPEN` | `Vote` on a BUY by a member whose balance grew at the tick the proposal opened or later (a `Contribute`, or capital handed back by `SettleIpo`): that money has no vote on it (section 4a) |
| 34 | `NAME_UNPROVEN` | `SettleIpo` of a holding that won seats whose declared share name nothing proves: the target's ledger is bound to another name, the pairing is impossible (a name the table lists for another index, or another ledger owns it), or QPOOL does not hold recognised + seats shares of it. Nothing is written, the reward comes back, the holding stays unsettled with its capital (section 7a) |
| 35 | `IPO_NO_SNAPSHOT` | `SettleIpo`: the epoch has moved past the first bid but END_EPOCH left no snapshot of the target's book (QPOOL's END_EPOCH did not run in that epoch, e.g. the contract was in an error state). Nothing can be settled; nothing is written |
| 36 | `PRICE_TOO_LOW` | `RebidIpo`: the book is full and the price does not exceed its minimum (the 676th slot), so the core would refuse the bid and only register a price. Found from two reads of the book; nothing is written |
| 37 | `SEATS_FULL` | **never returned**: an IPO holding has no voted quantity (step KI2: seats are what the money buys). The number stays reserved |
| 38 | `OFFER_CHANGED` | `BuyPosition`: `expectedCostUnits` is not the cost units of the live offer (the seller replaced it after the buyer read it, or the buyer named the wrong parcel). Everything attached is refunded and nothing is written (step K6) |

### 4h. Reading the state

- `GetSource(source, kind)` — one ledger. `recognized` is the sum of the registered
  holdings' `shares`; `unsettled` is the number of IPO holdings still to be
  settled; `escrow` is the QU set aside (non-zero means somebody should call
  `SettleIpo` or `ReconcileHolding`); `pendingQx` is the unreconciled QX bid
  quantity; `accHigh`/`accLow` are the two halves of the accumulator. For a CONTRACT
  source pass `id(index, 0, 0, 0)` and kind 1; for a TOKEN source the issuer and 0.
- `GetHolding` — `shares` is the recognised weight (it is 0 for an IPO holding until
  `SettleIpo` runs), `pendingShares` what is ordered but not yet recognised,
  `ipoSettled`, `sourceEscrow`, `payerIndex` (the paying contract for bought or
  deposited contract shares, 0 otherwise). For an IPO holding `ipoMaxPrice` is the price of
  its highest bid so far (the opening price until a rebid goes higher; every price the holding bids at is a key in the registry of bid prices) (the holding has no voted quantity and no ceiling: the money is its only limit); `ipoAtRisk` is what the holding's bids
  have put into the book and not moved back, `ipoAvailable` its free funds (the budget never bid plus the outbid stake that a rebid has moved back),
  and `ipoAtRisk + ipoAvailable` is `costBasis` until `SettleIpo`, which returns
  `ipoAvailable` plus what the book gave back of `ipoAtRisk` and sets both to 0;
  `pendingShares` of an IPO holding is only the number of seats the holding held when it was last reconciled plus what its last bid seated,
  for the eye: the book moves on, and the snapshot decides.
- `GetIpoBids(holdingId)` — the page a rebid bot reads (section 7a): the budget, the highest price bid, the seats as of the last reconcile, the free funds, the stake still at risk, the refund pot, and the holding's live
  bid prices with their seat counts as of the last reconcile. The order book itself is read from a node.
- `GetIpoTarget(index, price)` — the IPO snapshot of one target contract: `unsettled` holdings of
  QPOOL, the epoch of the first bid, the epoch the snapshot was taken (0 = not yet; a settlement is
  possible from the epoch after it), the clearing price (`finalPrice`, 0 = the book was not full and
  the IPO failed) and, for a bid price, the `seats` QPOOL won there and what they `spent`.
- `GetPool` — besides the totals, the five counters of the shared tables (`offerCount`, `admittedCount`, `ownershipRows`, `depositCount`, `ipoHoldings`) and
  `holdingCount`, the holding slots the pool occupies now; compare them with the quotas of 4b before trying to add a row. `GetHolding.rows` is the number of seat rows of a holding.
- `GetPosition.owed` — what `ClaimPayout` would pay right now.
- `GetStats` — watch `globalUnattributed` (money that matched nothing; no exit),
  `dividendEscrowTotal` (all escrow), `totalUnownedSwept`, `ipoRefundEscrow`. **`poolCount` is the number of ACTIVE pools** (`CreatePool` adds one, `CloseEmptyPool` takes one off) since K5,
  no longer the highest pool id plus one: once a pool has been closed the live pool ids have a gap, so a client that enumerates pools must read `GetPool(id).found` for the ids up to
  the table size (1,023) and not stop at `poolCount - 1`.

---

## 5. Stage 1 — unit tests

### The supported command, and what was actually run

The stock command (Linux is the supported platform) is:

```bash
cd core
cmake -B build -DENABLE_AVX512=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target qubic_core_tests -j
./build/test/qubic_core_tests --gtest_filter='TestQPool.*'
```

**That command was not run for this revision.** The stock `qubic_core_tests` target
does not build on macOS: the platform layer assumes Windows' 4-byte `long`
(`static_assert(sizeof(long) == 4)` in `platform/concurrency.h`) and uses MSVC
intrinsics and other MSVC-only constructs. The contract itself syntax-checks on
macOS; the test harness does not build. The 263 tests were run in a **substitute harness**:

- an x86_64 binary built with Apple clang and executed under Rosetta 2 on an
  Apple-silicon Mac with 8 GB of memory (one suite at a time — two at once crash the
  machine), at `-O1` with warnings silenced;
- against a private scratch copy of the core to which the minimum clang / LP64 port
  edits were applied by script: `CHAR16` as `wchar_t`, 32-bit counters for the
  `_Interlocked*` calls, an MSVC `0i64` literal, `memcmp` in place of a `__m256i`
  `==`, `setjmp`/`longjmp` in place of the edk2 `SetJump`, `typename` where MSVC lets
  it go, and a force-included header for a few MSVC secure-CRT functions; the test
  target was cut down to the QPOOL test file plus the two shared sources it needs;
- the contract under test is this package's `QPool.h`, unmodified; QX and QSWAP are
  the real contracts from the core checkout, `TESTEXB` plays a paying contract and
  `TESTEXD` (and, since step KX, `TESTEXC` and `TESTEXE` to `TESTEXK`) an IPO target.

Those port edits are not part of this package and have not been proposed to core.
The harness is not the MSVC build and not a node: it does not exercise the real
tick processor, locks contended across threads, or real timing. The refunds of an IPO come from the
core's own `bidInContractIPO()` and `finishIPOs()`; only the order of the epochs is wrong (see "The test foundation").

> **A verification note worth repeating.** An earlier attempt that compiled the test
> with `-fms-compatibility` reported zero errors in the contract — but that flag also
> breaks the macOS SDK's `char16_t`, and 139 lines of resulting SDK noise were
> *masking six real errors*. Always count errors per file rather than trusting an
> empty grep.

### Result

The final full run, on the final `QPool.h` and test file: **263 tests, 263 pass** (966 s). The latest step is K6 of the audit fixes (the seat market and payment correctness, "The seat market and payment correctness (step K6)" below): `QPool.h` changed in `BuyPosition` (the buyer names the parcel, a retired or unsettled holding is refused, admission is for newcomers only), `OfferPosition`, `Contribute`, `WithdrawUncommitted` (a failed payment is undone), `SetPermissioned`, `ClaimPayout` (it asks a read-only preview what it would pay before it writes) and `SyncHolding` (plus one new private function, `SyncPreview`); `BuyPosition`'s input grew from 40 to 48 bytes and error code 38 is new; the state did not change (**190,201,464 bytes**); the suite grew from 256 to 263 tests (7 new: six of them fail on the K5 contract and the seventh closes a coverage gap, `ck/K6/proof-new-tests-against-K5.summary.txt`; five existing tests changed for stated reasons; two properties joined the invariant checker); 22 mutants of the K6 rules: 21 are killed, one is equivalent (`ck/K6/mutation`). The step before it was the REPAIR of K5 (its figures: 256 tests, 256 pass, 937 s; two defects that the independent verification of K5 found; "K5 repair" below: a refused seat purchase no longer writes the state, and a mandate is tied to the incarnation of its holding; the state is **190,201,464 bytes**, the suite grew from 253 to 256 tests). Before it came K5 of the audit fixes (capacity quotas, deposit pricing and slot lifecycle, "Capacity quotas, deposit pricing and slot lifecycle (step K5)" below): `QPool.h` changed (the fees of owner decision D8, the lower IPO cap, per-pool quotas, creator-only deposits, recycled holding and pool slots, three new procedures, a narrowed `PRE_ACQUIRE_SHARES`); the state was **190,201,456 bytes** then (3,671,536 less than before); the suite grew from 235 to 253 tests (18 new, every one of them FAILING on the contract of the step before: `ck/K5/proof-new-tests-against-KX.summary.txt`; 17 existing tests changed for stated reasons; the invariant checker has nine new properties). The step before it was KX of the audit fixes (several IPOs at the same time, "Several IPOs at the same time (step KX)" below; its figures: 235 tests, 235 pass, 864 s):
**no behaviour of `QPool.h` changed** (only the header comment that states what a refused `RebidIpo` costs, which was wrong; the state is unchanged, 193,872,992 bytes); the suite grew from 220 to 235 tests (15 new: 14 `Kx...` tests and a kitchen sink with two simultaneous IPOs), and the harness grew
seven IPO-book test contracts (`TESTEXE` to `TESTEXK`, added to the scratch core by `add_ipo_test_contracts.sh`) so that eight IPOs can end in one epoch. Because nothing in the contract changed, the new tests PASS on the KI2 contract as well (`ck/KX/regression`): they prove the multi-IPO logic rather than fix it.
Their teeth are shown by mutants of `QPool.h` (`ck/KX/mutation`): 12 mutants of the multi-IPO logic (the settlement drawing the whole pot; the price registry without its target in `Execute` and in `RebidIpo`; END_EPOCH snapshotting only one target, storing every target's rows under one key, or using one clearing price for all;
the target table returning the first used row or scanning only seven rows; `Execute` not refusing a ninth target; a rebid drawing the whole pot; the first settlement freeing the row; the pass of a rebid counting an outsider's slots at the holding's price) are all killed, one of them (the last) only after a test was added: it survived the first series, because no test had an
outsider bid at a holding's price. The kitchen sink with two simultaneous IPOs ran 10 enforced seeds and 24 explored ones with no invariant violated. The one problem found is the known name-binding limit (owner decision D9), reproduced on the real books and pinned by a switch in `kBugs`.
The step before it was KI2 of the audit fixes (the owner's final IPO rule, sections 4a, 7a, 9 and 10; its figures: 220 tests, 220 pass, 814 s):
**the IPO bidding is governed by the total amount of QU allocated** (no voted quantity, no ceiling price). `QPool.h` changed in `Propose`, `Execute` (BUY_IPO sizing), `RebidIpo` (rewritten: bounded by free funds and the contract's
balance, price reuse, the money-term cheap refusal), `SettleIpo` (the seat bound is the book's 676), `GetHolding`, `GetProposal` and `GetIpoBids`; `Propose_input.ceiling`, `Proposal.ceiling`, `IpoBids.ceiling` and `Holding.ipoQuantity` are gone (the state is 3,211,264
bytes smaller: 193,872,992), `RebidIpo_output` has a trailing `bidQuantity`, and the suite grew from 209 to 220 tests (11 new). The harness now runs a bid in the order of the core and checks the contract's real balance against it after every bid
(`runIpoBidCall`), and three new tests pin the core's own rules (deduct first, walk the units, refund after the loop; `finishIPOs` refunds a second time) against the real `bidInContractIPO` and `finishIPOs`. Regression evidence
(`ck/KI2/regression`): the new and rewritten tests were run against the KI contract: against the KI contract plus only the two new output fields (`kishell1_vs_KI.diff`) 26 of the 32 tests of the filter fail (the other six are the three tests of the core's own rules and the three randomised tests, which pass vacuously there because KI refuses every proposal that sends no ceiling), and against KI with a Propose that reads the new proposal shape the KI way (ceiling = opening price, quantity = what the budget buys) 16 of 33 fail on behaviour and 17, which test what did not change, pass. Mutants of `QPool.h` (`ck/KI2/mutation`): 20 mutants (the money check after the pass, the physical-balance check, the price reuse and its list entry, the room check for a reused price, the belief in the list at the book's minimum, the pot clamp and its debit, the default quantity, the 676 caps of Execute and Propose, the budget boundary, the two lifetime lines of Propose both ways round, the 676 boundary of SettleIpo, the book-minimum boundary, and the forbidden credit of a rebid's own refunds) are all killed; three survived the first series (the check after the pass, the credit in the pass only, the list believed at the minimum) and were killed after one test was added and a filter corrected.
Tests that changed because the behaviour changed on purpose are listed in the CHANGELOG, section 10 (the quantity is derived from the budget, so every IPO test that voted a quantity with a larger budget now votes a budget that buys that quantity).
The step before it was KI of the audit fixes (the owner's earlier IPO logic, with a voted quantity and a ceiling; replaced by KI2 as described above; the sections 4a, 7a and 10 describe the current rules):
`QPool.h` changed in `Propose`, `Vote`, `Tally`, `Execute`, `SettleIpo`, `GetHolding` and `GetProposal` and has the new procedure `RebidIpo` (id 13), the new function `GetIpoBids` (id 9) and two new private helpers
(`VoteWindowOf`, `IpoSeatsNow`), the state grew by 4,458,512 bytes, and the suite grew from 191 to 209 tests (18 new, all named `Ki...`). Every new test fails on the K4 contract: against the K4 `QPool.h` plus only the new interface shapes (the `ceiling` field, `RebidIpo` and `GetIpoBids` as stubs, the new constants; `ck/KI/regression/make_k4_shell.py`) the unchanged test file compiles and all 19 tests of the filter (the 18 `Ki...` tests and the rewritten `NobodyCanBidInAnIpoBeyondWhatTheVoteApprovedAndProcedureFifteenDoesNotExist`) fail, 0 pass (the first failure of nearly every one is the vote window: K4 votes an IPO for 72 hours, so a `Tally` after one day is `WINDOW_OPEN`; the others fail on the missing ceiling checks, the lock that lasts five days and the missing rebid), and against the unmodified K4 header the test file does not compile at all (156 errors: no `ceiling`, no `RebidIpo_input`, no `IpoBids`, ...; logs in `ck/KI/regression`). 26 mutants of `QPool.h` (`ck/KI/mutation`: the ceiling and its boundary, the seat rule, the registry, the book's minimum and its boundary, the quantity bound, the compaction of dead prices and the release of their registry entries, the clamp of the draw by the pot, the use of returned funds in the same call, the pot debit, the tracking of the highest price, the pass's attribution by price, the list's belief in seats at the minimum, the closed window, the funds check, the sum over a holding's prices in `SettleIpo`, the one-day vote in `Vote` and `Tally`, the anchoring of the execute window, the lock of an IPO vote, the sizing at the ceiling and the budget rule of `Propose`) are each killed by at least one test, 25 of 26 (five of them by a refusal test whose call, wrongly accepted, bids and hangs the harness, which cannot deliver the self-refund of a call it did not expect: the watchdog ends the run). The 26th, a rebid that registers a price that seated nothing, survives and is equivalent: a rebid that passes the checks always seats at least one slot, so that line is a defence with no reachable behaviour.
The step before it was K4 of the audit fixes (IPO settlement from an END_EPOCH snapshot, sections 4c, 4d and 7a): `QPool.h` changed in `Propose`, `Execute`, `SettleIpo`, `ApplyHolding` and `END_EPOCH` (plus one new private helper, `IpoRowOf`, and the view `GetIpoTarget`), `ReconcileIpo` is gone, the state grew by 264,528 bytes, and the suite grew from 173 to 191 tests (18 new). Every test of the new behaviour fails on the K3 contract (the proof log is in `ck/K4`: all 21 tests of the filter (the 18 new tests, the replaced snapshot test and the two known-bug tests of C004 and C005, which now run both settle orders) fail there, 19 of them on a behavioural assertion; the other two, the control `TheRefundPotIsASolvencyClamp...` (K3 clamps to the pot too) and the randomised `KitchenSinkWithTwoIposKeepsTheInvariants`, fail only through the end-of-test invariants, because the K3 contract keeps no snapshot rows). 42 mutants of `QPool.h` (`ck/K4/mutation`; the snapshot's slot count, final price, cost and closing run, every `SettleIpo` refusal, the draw from the pot, the exact split, the cap that keeps a unit of cost, the name proof, the row bookkeeping, the ninth-target refusal, the revenue sweep) are each killed by at least one test (four survived the first run: the snapshot taken again, the pot reduced by the whole refund, the bound-name check and `ApplyHolding` treating `retire` as authoritative. Three were killed after a test was sharpened or added (the clearing price now moves before END_EPOCH is replayed, an assertion on the pot, a test of a name the universe has enough of), and the fourth cannot be killed: no procedure passes shares together with a retirement, so that line is a defence in depth with no reachable behaviour). The step before it was K3 of the audit
fixes (governance binding, section 4a): `QPool.h` changed in `Contribute`, `WithdrawUncommitted`, `Propose`, `Vote`, `Tally`, `Execute`,
`OfferPosition`, `BuyPosition`, `SettleIpo`, `GetMember` and `GetPosition` (plus one new private helper, `ReleaseVotes`), the state grew by
8,388,608 bytes, and the suite grew from 149 to 173 tests. Every new test fails on the K2 contract (the proof log is in `ck/K3`: all
37 tests of the filter (the new tests, the changed ones and a few that run governance flows) fail there, 33 of them on a behavioural assertion; the other four (the control
`AMandateThatMatchesTheBookRestsItsBidAndNothingComesBack` and three tests that only run governance flows) fail only through the end-of-test invariants, because vote
rows are keyed differently), the tests whose expectations the intended change of behaviour altered are listed in the CHANGELOG, and the mutants
of `QPool.h` (the deposit rule, each seat-lock check, the lock arithmetic and its expiry, what Tally and Execute release, the YES-only charge, the funded
clamp, the order sizing, the minimum-out scaling and its rounding, the FAILED closure, the exact split, the stale-slot rules, the holding identity)
are each killed by at least one test (38 mutants in all, `ck/K3/mutation`: two of them survived the first run, a mandate funded with the weight voted instead of what the voters still have and a minimum out rounded down instead of up, and two tests were added or sharpened to kill them). The step before it, K2 (IPO bid validation, the registry of IPO bid prices and the deletion of `RebidIpo`, which step KI reinstated in a bounded form, section 7a): `QPool.h` changed in
`Propose`, `Execute`, `ReconcileIpo`, `SettleIpo` and `END_EPOCH`, `RebidIpo` is gone, and the suite grew from 140 to 149 tests. Eight
of the nine new tests fail on the K1 contract (the ninth, `ProposeBuyIpoAsksTheRegistryAndDoesNotScanTheHoldings`, documents the
lookup that replaced a scan and passes on both), so does the rewritten `ReconcileIpoIsExecutedAgainstARealBook` (K4 deleted the procedure and replaced the test by `TheEndOfEpochSnapshotReadsTheIpoBookExactlyAndOnlyAtTheEndOfTheIpoEpoch`), and 14 mutants of
`QPool.h` (each of the limits, the cost check, the registry lookups, insert and release, the window test, the reconcile's price filter,
its report of bad books and its write-on-change) are each killed by at least one test. One branch has no test: the check of
`Execute` that the QU that left the contract in an IPO bid is not more than `price × quantity`, because the harness cannot make
the core take more than it should; it was run against a scratch copy with a transfer injected after the bid, where `Execute`
returned `BOOKS_INCONSISTENT`, recorded the holding, used the proposal up and placed no second bid. The step before it, K1 (roster
and membership integrity, section 4a "Membership is one fact"), changed `QPool.h` in `Contribute`, `BuyPosition`,
`DepositHoldingAsset`, `Execute`, `SettleIpo` and `Tally` (plus the two new private helpers `JoinCheck` and `EnsureMember`), and
the suite grew from 131 to 140 tests. The test foundation (next section) is the one of step K0. The one red test of earlier runs,
`SettleIpoRefusesWhileCapitalStillAtRisk`, was a defect of the TEST, not of the contract: it hand-built an IPO holding aimed
at `QPOOL_CONTRACT_INDEX + 1` (the test contract `TESTEXA`), whose IPO epoch is not the epoch the test runs in, so the
contract correctly treated the window as closed. It is rewritten against `TESTEXD` in its real IPO epoch, passes, and a
mutant of `QPool.h` without the window check fails it. (`SettleIpo`'s `WINDOW_OPEN` refusal has a passing test now.)

Selected tests were also run against deliberately broken copies of the contract (the
hook's issuer fix reverted, a wrong table value, the sale counter restore removed, no
cancel after a sale, the ask at the wrong price, the refund not taken back, plain
per-member floors in the cost split, a write before an escrow check, no vote-table
precheck, ...) and failed there. Two mutants of the refund measure survive, because
the two measures it takes agree in every flow the real QX and QSWAP can produce.
The test foundation was checked the same way: 24 mutants of `QPool.h` (quorum `<=`, a tie that passes,
each of the three window edges, the NO-voter exemption removed or widened to abstainers (K0; since K3 nobody but a YES voter is charged), a reward refund
removed or doubled, the CreatePool over-payment kept, the fee not booked at the hook, the seat sale or ALLOCATE,
no burn, no window check in `SettleIpo`, locks not released, pool totals not
maintained, ...) each fail at least one test: 19 fail one of the new tests, and four bookkeeping mutants (the pool's
committed and uncommitted totals, a seat sale that keeps the seller's cost units) fail EXISTING tests that never asserted those
quantities, through the end-of-test invariants alone.

### The test foundation (step K0)

- **Wrappers.** Every procedure has a fixture wrapper whose last argument is the QU attached to the call; the wrapper funds the
  sender and checks the harness really delivered the call. `ReconcileIpo` (deleted in K4) used to be executed by a test against a real book
  (`ReconcileIpoIsExecutedAgainstARealBook`, which became `TheEndOfEpochSnapshotReadsTheIpoBookExactlyAndOnlyAtTheEndOfTheIpoEpoch`;
  `RebidIpo`, which the earlier version of that test also drove, was deleted in K2 and is driven by the `Ki...` tests since step KI).
- **Global invariants.** `qpoolAudit()` reads the whole state and names every violated property (unique roster, every roster id has
  a Contribution, seats add up to `costBasis`, pool totals add up, ledger counters equal what the live holdings say, `creditedX64
  <= accruedX64`, no empty ledger, vote tallies and locks equal the recorded votes, the books are covered by the contract's balance
  with one QU of dust per claimant, ...). It runs from the fixture destructor, so it checks the final state of EVERY test, and after
  every step of the randomised tests. Actor ids are fixed (not random), so every run replays exactly; the seed is printed.
- **Kitchen-sink randomised test.** 3 pools x 3 members, all 20 procedures (K5 added three; 18 until K4 deleted `ReconcileIpo`; `RebidIpo` was out between K2 and KI), the real QX, QSWAP, an IPO target and a
  dividend-paying contract, in two phases (the IPO epoch, then the live epoch), five seeds, the invariants checked after every
  step; since K4 a second test runs four seeds with TWO IPO epochs in a row (TESTEXC's, then TESTEXD's; the refunds of both share one pot, and
  holdings of the first are settled or refused while the second runs). It finds defects of this revision (ghost recognised shares after a multi-pool
  IPO, fixed in K4; the roster defect C002 was fixed in step K1 and
  its switch is on; the stale vote rows C051 and the BUY-vote binding C006 were fixed in K3 and their switches are on; it also found a defect of
  K3's own first lock arithmetic, a phantom lock, before K3 was done, and, in K4, a defect of the fixture itself: its pre-credit of an IPO
  self-refund assumed the voted quantity was bid, not the quantity Execute sized to the funded money). They are listed per seed in
  `kKnownFuzzBugs`, tied to the audit finding. `kBugs` holds one switch per finding: a cluster that fixes a finding sets
  `fixed = true` in its row, and the tolerance disappears, the invariant is enforced for every seed and the known-bug
  tests (`CheckerSees...`, `SettleOrderDecidesWho...C004`, `StalePendingShares...C005`, ...) assert the correct behaviour.
  `KitchenSinkExploreSeedsFromTheEnvironment` runs other seeds (`QPOOL_FUZZ_SEEDS=...`) and only reports.
- **IPO chronology (the harness runs the epochs in the wrong order).** In this scratch core QPOOL's construction epoch (236) is
  later than the IPO epoch of every test contract (TESTEXD: 154); on the live chain a contract can only bid on contracts deployed
  after it. So at the target's IPO epoch the core thinks QPOOL does not exist yet. (a) a refund the core pays QPOOL from outside
  a QPOOL call (an outsider's bid evicting its slots, `finishIPOs`) makes the harness' notification stub fail an assertion, which
  is a NON-fatal failure in gtest and still runs the hook; the fixture swallows exactly that one. (b) a refund the core pays QPOOL
  from inside a QPOOL call (QPOOL outbids its own slots, or leaves part of its bid unseated) cannot build the hook's call
  context and `__qpiAbort()`s into an endless loop; the fixture switches the nested notification off for that call and delivers the
  refund to the hook right AFTER the call (`runIpoBidCall`), which is the node's order (the core deducts the whole `price × units`, walks the
  units, and pays every refund only after the loop) and its final state. The call itself therefore can neither see nor count the refunds of its own
  bid, and every bid also checks the contract's real balance against the core's accounting (it fell by `price × units`, less the refund). (c) QX and QSWAP pay QPOOL from inside their procedures,
  so a BUY or SELL on them must not run at that epoch; a watchdog turns a hang into a failure with a message. The fixture
  mode is `ScopedIpoEpoch` / `enterIpoEpoch()` / `goLive()`; `outsiderBid()`, `finishIpos()` and `closeIpoEpoch()` (END_EPOCH,
  then the core's own `finishIPOs()`, then the next epoch) drive the real book.
- **Hand-built holdings.** The tests that used `fabricateHolding` and `creditPayout` now buy through the real QX
  (`buyHolding`) and receive the dividend through the hook (`creditPayout` remains in one test, for a credit that predates a
  settlement). `fabricateTokenHolding`, `fabricateContractHolding` and `fabricateIpoHolding` remain where the real
  path needs arbitrary cost splits, an unreconciled QX bid or a contract in its IPO; they now keep the pool's books whole
  (roster, holding count, `deposited`), and `HandBuiltHoldingsMirrorTheRealPaths` compares each with the real path field by field.

### Several IPOs at the same time (step KX)

The owner's question: "if multiple IPOs are active, that must not affect the logic." The core lets any number of contracts share one construction epoch, so
their IPOs run in the same epoch. What matters for QPOOL (read in `contract_core/ipo.h`, `qpi/impl/qpi_ipo_impl.h` and `qubic.cpp`): every IPO has its own 676-slot
book and its own clearing price; the node runs every contract's END_EPOCH and then `finishIPOs()`, which loops over ALL contracts whose IPO epoch is the current
one and pays each bidder ONE refund per IPO; every refund carries only the type `ipoBidRefund`, never an IPO; the hook of each refund is run to completion before
the next refund is paid (`notifyContractOfIncomingTransfer` waits for the contract processor), so a refund is in the pot before any user transaction of the next epoch.

*The harness.* Until KX it could not build that: the core's examples have only two contracts with an IPO book, `TESTEXC` (IPO epoch 137) and `TESTEXD` (154), so K4
tested two IPOs one after the other. `add_ipo_test_contracts.sh` adds seven more, `TESTEXE` to `TESTEXK`, each an exact copy of `TestExampleD` (the state is the
core's `IPO` struct; the only code is the `END_TICK` that pays the contract's balance to its shareholders, which the dividend test uses), all with
`TESTEXD`'s construction epoch 155: **eight books in IPO epoch 154**, and `TESTEXC` as a ninth in an earlier epoch (what the eight-row test needs). The files it creates or edits
in the scratch core, and nothing else: `src/contracts/TestExampleE.h` to `TestExampleK.h` (new); `src/contract_core/contract_def.h` (the include block, the
`contractDescriptions` row and the `REGISTER` line of each, all after `TESTEXD`, so no index moves); `src/qubic.cpp` (`setContractFeeReserve` of each inside
`INCLUDE_CONTRACT_TEST_EXAMPLES`; the file is not compiled by the tests). `test/contract_testex.cpp` is not edited (its fixture starts `TESTEXA` to `TESTEXD` only and
the core's `finishIPOs()` skips a contract whose state is not allocated); its 14 tests were run on the patched core and all pass. One more step is NOT a source edit:
`contractCount` grew, and the global arrays sized by it (`contractUserFunctions[contractCount][65536]` and its siblings in `contract_def.h`) are *defined* in the
prebuilt `common_def.cpp.o` of the test build, which therefore has to be rebuilt (the script does it when it is given the build directory, and `tools/qrun.sh` rebuilds it
whenever it is older than `contract_def.h`; the old object is kept as `common_def.cpp.o.pre-KX`). Nothing in the package's own files depends on any of this except the tests that
name `TESTEXE` to `TESTEXK`; the diff of the scratch-core files is in `ck/KX/harness`.

*What the tests do, on those real books* (all `Kx...`; every figure was worked out with `kx/kxmodel.py`, the model of the core's bid loop and the contract's money rule
extended to several books and one shared pot, before it was pinned; each step runs the invariant checker):

| Test | What it shows |
|---|---|
| `KxTwoPoolsBidInBothIpos...` | pools A and B each bid in D and in E, the same prices cross between the IPOs, the outsider outbids D and E in turns, the pools rebid in turns with one shared pot: every rebid draws exactly its own holding's money and changes nothing in the other IPO; one `END_EPOCH` snapshots both books (two clearing prices); the four holdings settle exactly |
| `KxAPriceBelongsToOneHoldingPerIpo...` | the same price in two IPOs is accepted (`Execute`, `RebidIpo`); the same price in one IPO is `IPO_PRICE_TAKEN` at `Propose`, at `Execute` (a race between two proposals: the first to execute wins) and at `RebidIpo`; a holding's own price is reused |
| `KxOneIpoFailsAndTheOtherWins...` | D is full and wins, E never fills and fails, both end in one epoch: ONE `END_EPOCH` call snapshots both (clearing prices 1,000 and 0), the pot holds the premium of D and the whole bid of E as one anonymous 13,000, and each holding gets exactly its own in either order; a failed IPO's principal goes back to its own pool |
| `KxEveryOrderOfSettlement...` | the four holdings settle in all 24 orders, each ending in the identical state number for number (the state is saved once and restored for each order) |
| `KxAnEvictionAfterTheLastReconcile...` | a holding is outbid after its last reconcile (its list is stale), the other IPO is quiet; the settlement works from the snapshot and the quiet holding is untouched |
| `KxTheSharedPotNeverLetsAPoolDraw...` | with the pot holding only another IPO's refunds a rebid that passes the cheap bound is refused after the pass, and each pool then draws exactly its own money |
| `KxAllEightRowsOfTheTargetTable...` | eight rows filled by eight real `Execute`s (the eighth row, index 7, included), the ninth target refused by `Execute` (`CAPACITY`, nothing moved, the proposal stays PASSED) and by `Propose`, a settled row freed and reused by the ninth, ONE `END_EPOCH` reading eight books, eight settlements freeing eight rows |
| `KxAStuckHoldingInOneIpo...` | a `NAME_UNPROVEN` holding in D parks only D's dividends; E settles and pays normally; D's honest holding settles; the stuck one stays |
| `KxDividendsOfTwoIpos...` | the two books pay their shareholders through the core (`END_TICK` -> `distributeDividends` -> QPOOL's hook): both parked, E's released by E's last settlement alone, D's by D's |
| `KxTheSameMoneyCannotFundTwoIpos...` | two mandates of 400,000 on one 500,000 pool: the second is sized to the 100,000 the voters can still pay |
| `KxARebidIsRefusedWhenTheRegistryAndTheHoldingsList...` | the registry/list disagreement guard of `RebidIpo` (both ways), by planting the state and restoring it |
| `KxTheInvariantChecker...` | the checker's `ipo-funds` property now adds up the money outbid in ALL live books against the one pot (a pot that covers each book alone but not both is found) |
| `KxAnOutsidersSlotsAtAHoldingsPrice...` | outsiders bid at exactly the price a holding opened at, in both IPOs: the rebid pass and the snapshot count only QPOOL's own slots (found by a mutant that survived the first series) |
| `KxADeclaredNameThatBelongsToTheOtherSimultaneousIpo...` | **the one defect found: owner decision D9, below** |
| `KitchenSinkWithTwoSimultaneousIposKeepsTheInvariants` | the kitchen sink with `TESTEXD` and `TESTEXE` live in one epoch: 10 enforced seeds (80 + 200 steps); every step works on one of the books; the books end together, each filled (it wins) or not (it fails); invariants after every step |

*What was found.* No arithmetic or attribution defect: the snapshot, the registry, the pot and the settlement are independent of how many IPOs are live. The one problem is
the known name-binding limit (4d, section 10, owner decision D9), which simultaneity turns from "a stray share of some other contract" into "any other IPO that ends in
the same epoch": `SettleIpo` can only prove that QPOOL holds ENOUGH shares of the declared name, and when two unlisted contracts' IPOs end together BOTH names exist. A holding that
bids in D under E's name (the vote cannot check it, E's shares do not exist yet) and settles first binds D's ledger to E's name, after which the honest holding of E is
`NAME_UNPROVEN` (E's name belongs to D's ledger) and so is every honest holding of D (D's ledger carries another name): their capital stays on the books, their IPOs' dividends stay parked
(and a target that has a stuck holding keeps one of the 8 rows of the table). `KxADeclaredNameThat...` reproduces it on the real books (switch `D9` in `kBugs`; once a cure is built the same test requires the
honest holdings to settle). **It is a stranding (nothing is lost, nothing can be taken), costs the attacker a pool and one bid, and has no cure inside the contract that
needs no decision**: reserve the declared name at `Execute` (a squatter then blocks the honest pool from bidding at all), refuse to bind a contested name (both stay stuck), or
put the contract in the embedded table before its IPO (exact). The vote-gated rename/abandon path (K4b) is what lets a pool out afterwards.

*What the tests could not reach:* the post-bid overspend report of `RebidIpo` (`BOOKS_INCONSISTENT` after the bid when the balance fell by more than `price × quantity`: the real core
takes exactly that, and the wrap that once made it take more is closed at the input, C001), so it is defence in depth that only the invariant checker would see; a ninth book IN THE SAME
epoch beyond the eight (there are eight same-epoch books; in the table test the ninth TARGET is `TESTEXK`, refused while the row of the earlier `TESTEXC` is still in use, and no ninth book exists
to be bid in at the same time); the real tick processor's scheduling.

### Capacity quotas, deposit pricing and slot lifecycle (step K5)

Audit findings C007, C013, C039, C031, C032 and C046, owner decisions D8 (fees), D16 (deposits are the creator's), the owner's lower IPO cap and the per-pool IPO quota. What changed in `QPool.h`:

- **Constants first.** `QPOOL_CREATE_POOL_FEE` is 10,000,000 QU and `QPOOL_JOIN_FEE` 10,000 QU (D8; `QPOOL_MIN_CONTRIBUTION` stays 10,000). `QPOOL_MAX_IPO_HOLDINGS` is 2,048 (it was 8,192) and with it `QPOOL_MAX_IPO_PRICES`
  32,768 (it was 131,072). The state fell by 4,105,728 bytes at that step (from 193,872,992 to 189,767,264); every `static_assert` that names either constant still holds. Only three existing
  tests quoted the fees as numbers (`EndEpochSplitsRevenueEightyTwenty`, `EndEpochRunsAtATargetsIpoEpoch...`, `EndEpochPaysRealShareholders...`: two registrations are 20,000,000 now, so 23,668 a share, 15,999,568 paid,
  4,000,000 burned, 432 carried); every other test uses the constants.
- **Per-pool quotas** for every table that all pools share (4b): offers 32, allow-list entries 256, seat rows 512, deposits 32, unsettled IPO holdings 8, each a counter in the `Pool`, each checked BEFORE the first write of the call that
  would add the row, each given back where the row goes. `static_assert`s tie `QPOOL_MAX_OFFERS`, `QPOOL_MAX_ADMITTED`, `QPOOL_MAX_OWNERSHIP` and `QPOOL_MAX_HOLDINGS` to pools × quota.
- **`DepositHoldingAsset` is the pool creator's alone** (D16), refusing everybody else with `NOT_AUTHORIZED` before anything is read or written; it costs nothing and puts the creator on the roster.
- **The life of a slot.** `Pool.holdingCount` counts the slots a pool occupies NOW. A holding that gets its whole bid back (`SettleIpo`) gives back its slot, rows, offers and IPO quota; a sold-out holding is freed by its last member's `ClaimPayout`, which removes
  each claimant's row; a seat sold whole (`BuyPosition`, `SettleIpo`) removes the row when nothing is owed on it; the free-slot search starts at `holdingFreeHint`; the dead `poolHoldings` table is gone; `CloseEmptyPool` and a pool cursor
  recycle the slot of a pool nobody used.
- **Three new procedures:** `ReleaseManagedShares` (19), `CloseEmptyPool` (20), `EvictSquat` (21), and a narrowed `PRE_ACQUIRE_SHARES` (4d, 6c).

| Test | What it pins |
|---|---|
| `K5OnlyThePoolsCreatorCanDepositAndAnybodyElseIsRefusedBeforeAnythingIsWritten` | six callers (an admitted member, members, strangers, the issuer) in a public and a private pool: `NOT_AUTHORIZED`, 777,777 back, no dirty state, shares unmoved; the creator deposits in both for nothing |
| `K5APoolsDepositsAreCappedSoItsAcquisitionsKeepTheirSlots` | 32 deposits, the 33rd `CAPACITY`, and the pool still buys |
| `K5APoolHasAQuotaOfOffersAndNeverFillsTheTableOtherPoolsUse` | 32 sock-puppet offers, the 33rd refused, an edit takes no place, another pool unaffected, a cancel and a fill give a place back |
| `K5APoolHasAQuotaOfAllowListEntriesAndRevokingGivesOneBack` | 256 entries, the 257th refused; admitting twice / revoking an absent entry writes nothing; revoking on an opened pool |
| `K5APoolHasABudgetOfSeatRowsAndThePurchaseThatWouldExceedItIsRefusedWhileAnotherPoolStillBuys` | 256 members, three real `BUY_QX`: the third (rows 512 + 256) is `CAPACITY` with nothing written, another pool buys |
| `K5APoolHasAQuotaOfUnsettledIpoHoldingsAndAPoolAtItsQuotaCannotStopAnotherFromBidding` | 8 holdings, two proposals open at once with one place left: the second `Execute` is `CAPACITY` before any write, `Propose` says so too, another pool bids, a settlement gives the place back |
| `K5AFailedIposHoldingGivesBackItsSlotItsSeatRowsAndItsQuotaAndTheLowestFreeSlotIsTheNextOnesTaken` | counts and cursor through a failed IPO's settlement, the offer goes with the holding, the freed slot is the next deposit's |
| `K5ASoldOutHoldingIsFreedWhenItsLastMemberHasClaimedAndTheSlotIsTakenAgain` | a real purchase and sale on QX; each claim removes a row (a member with nothing owed gets OK, 0 paid and his row cleaned up), the last frees the slot, `committedTotal` falls with it, the next deposit takes the slot |
| `K5ASeatRowWithNothingLeftAndNothingOwedIsRemovedWhereItEmptiesAndItsOfferGoesWithIt` | a seat sold whole before it earned: no row; after it earned: the row stays until the claim |
| `K5ASquatOfATokenThatNeverPaidIsEvictedWhenSomebodyElseNeedsTheBindingAndOnlyThen` | the audit's squat; too young, no rival, the bound asset as rival, a rival nobody holds: refused and nothing written; then evicted, shares back in full, the honest deposit goes through |
| `K5AnHonestDepositorIsNeverEvictedWhileTheBindingIsUsedPaidOrSharedOrBoughtWithTheMembersMoney` | a seat sold, a second pool holding the asset, a dividend, a bought holding: none is evictable |
| `K5ASquatOnAContractsIndexIsEvictedOnceThatContractIsInItsIpoAndNeverBefore` | the contract-name squat: refused at the live epoch, evicted in the contract's IPO epoch, the honest `BUY_IPO` then proposes |
| `K5ASharesThatCouldNeverBeDepositedAreNotTakenUnderManagementInTheFirstPlace` | QX reports 0 moved for a token the issuer's ledger would refuse |
| `K5SharesOwnedByTheCallerAndManagedByQpoolCanBeTakenBackAndNothingElseCan` | the release: ten refusals refunded to the QU, QX's 100 fee taken, 677 of 777 back, the shares tradable again; a depositor cannot pull a deposit out |
| `K5ASeatBuyerWhoWouldNeedARowBeyondThePoolsBudgetIsRefusedAndRefundedInFull` | a newcomer buying a seat in a pool whose row budget is used: `CAPACITY`, the ask and the fee back, the offer stands, **the state is not marked dirty** (repair); with a place free it goes through; a buyer who already has a row is not stopped by the budget |
| `K5TheOwnersFeesAndTheLowerIpoCapAreInForceAndThePooledIpoTableIsStillAHardLimit` | the constants of D8 and the cap (10,000,000 / 10,000 / 2,048 / 32,768), the anti-spam figures (10.24 billion, 2.56 million, 2.56 billion), a first contribution of 20,000, and a full IPO table refusing a `BUY_IPO` |
| `K5AnUnusedPoolCanBeClosedByItsCreatorAndItsSlotIsTheNextOneCreatePoolTakes` | close, refusals, the fee kept, the lowest slot recycled |
| `K5TheNegativePathsOfDepositHoldingAssetAreRefusedBeforeAnythingMovesAndRefundEverything` | audit C046: shares under another manager, more than owned, a never-issued asset, the row budget, the holding ceiling, a full seat table |

The invariant checker has nine new properties (`pool-deposits`, `pool-ipo-holdings`, `pool-rows`, `pool-offers`, `offer-row`, `pool-admitted`, `own-dead-row`, `retired-rows`, `slot-hint`, and `pool-holding-count` is now exact), each
broken on purpose in `TheInvariantCheckerNamesEveryPropertyItGuards`; the kitchen sink deposits as the creator, takes shares back with `ReleaseManagedShares`, and calls the three new procedures in their refusals.

**Existing tests changed (17), and why.** Fees: the three above. Deposits are the creator's: `TwoPoolsDepositingTheSameTokenShareTheIssuersDividend`, `PayerTableKeepsEmptySlots`,
`DepositOfContractSharesNeedsAMatchingIndexAndMovesNothingOtherwise`, `DepositedContractSharesSplitARealDividendWithAnIpoWonHolding` (a successful deposit costs nothing now), `UnlistedContractNamesStayBoundToOneIndexAndOneLedgerToOneName`,
`GovernanceQuorumTieAndWindowEdgesAreExact`, `HandBuiltHoldingsMirrorTheRealPaths` (each makes the depositor the creator of the pool he deposits into),
`EveryRouteIntoAPoolPaysTheJoinFeeExactlyOnce` (route 1 is now "the creator joins for nothing, a newcomer is refused"), `ASeatBuyerInTheLastRosterSlotIsAFullMember` and `EveryProcedureRefundsAnAttachedRewardExactlyOnceOnEveryPath`
(the full-roster / not-authorised rows), `RevenueIsBookedAtEveryFeeSiteAndNowhereElse` (one join fee fewer). Deposit quota: `HoldingsPerPoolCapacityRaisedTo64AndStillEnforced` (32 deposits, then the holding ceiling by setting the pool's count).
IPO quota: `KxAllEightRowsOfTheTargetTableAreFilledByRealBidsANinthIsRefusedUntilASettledRowFreesUp` (pool P has eight unsettled holdings, the most a pool may have: the "a target with a row is no ninth target" proposal comes from another pool).
Procedure ids: `NobodyCanBidInAnIpoBeyondWhatTheVoteApprovedAndProcedureFifteenDoesNotExist` (19 to 21 are registered). The fixture: `depositAsset` attaches nothing by default, `setOwner`/`adoptHandBuilt`/`fundFromMembers` keep the new counters of a hand-built holding, and the wrapper helpers of the three new procedures.
Two more were EXTENDED, with no assertion removed or weakened: `TheInvariantCheckerNamesEveryPropertyItGuards` (a deliberate break per new property, so the checker is proven to catch each) and the kitchen-sink harness shared by the `KitchenSink...` tests (the creator does the depositing, a refused depositor takes his shares back, and an operation calls the three new procedures in their refusals).

#### K5 repair: two defects of the first K5, found by its independent verification

The verifier of K5 reproduced both with tests of his own (the whole suite passed on the first K5: neither was covered). Both are fixed and covered now.

1. **A refused seat purchase wrote the state (D23).** `BuyPosition` checked the pool's seat-row budget for a buyer who needs a new row only AFTER `SyncHolding`, which writes the holding whenever its source has a ledger. K5 made that
   refusal reachable (before it needed a full global table), so anybody could attach the ask price plus the join fee (it comes back) against a pool whose budget is used up, once a tick, and make the node re-hash the whole 190 MB state for nothing. The check is now a plain
   `contains` lookup made with the other read-only refusals, before the stale-offer drop and the sync: the refusal writes nothing. (The other refusals K5 added were already in front of every write; the verifier confirmed that with a dirty-flag test of nine kinds of outsider call: `EvictSquat`, `CloseEmptyPool`, `ReleaseManagedShares`, a stranger's deposit, a revocation that changes nothing, a stranger's `ClaimPayout`, `CancelOffer`, a stranger's `OfferPosition` and a `BuyPosition` without an offer.)
2. **A slot is not an identity.** K5 frees the slot of a failed IPO's holding, of a sold-out holding whose last member has claimed, and of an evicted squat, and the next holding takes it. `Vote` and `Execute` named the holding a SELL or ALLOCATE
   mandate was about only by (pool, issuer, asset name). The verifier's scenario: ALICE and BOB each get a SELL_QX of the whole holding passed; the first is executed and sells out; both claim, so the slot is free; the pool's creator deposits
   the same asset again and gets the same slot; Execute of the second, old mandate returned OK and sold 1,000 of the creator's 2,000 new shares (proceeds 1,085,634) -- anybody can fire a passed mandate, so a keeper bot would have done it. An ALLOCATE
   would have credited the pool's unowned QU to the new holding's owners (198,000 in the test), and the creator could vote with his new seat on the old owners' open proposals. K5 made this reachable: before it a retired holding kept its slot for ever.
   **Fix:** every holding has an *incarnation*: `Holding.incarnation`, the value of `StateData.holdingSerial` after the increment that its creation (`Execute` of a buy, `DepositHoldingAsset`) made; a free slot has 0 (the three places that free a slot blank it or clear the field). `Propose` of a SELL or ALLOCATE
   records the holding's incarnation in `Proposal.holdingTag`, and `Vote` and `Execute` answer `NOT_FOUND` when the slot's incarnation is not the proposal's, before anything is read from the holding or moved. The pool and asset checks stay. The cost is 8 bytes of state (the serial): the tag
   and the incarnation share the 8 bytes that `Proposal.kind` and `Holding.rows` took alone (both became 32 bits), so `Holding` is 184 bytes and `Proposal` 168 as before. 32 bits are exact: a tag only has to tell apart the holdings a slot can hold during the life of ONE proposal (432,000 ticks), and the core takes at most
   4,096 transactions a tick (`NUMBER_OF_TRANSACTIONS_PER_TICK`, `network_messages/common_def.h`): fewer than 1.8 billion creations in that time, fewer than 2^32.

What else names a holding by its slot number, and why none of it needed the tag (checked, not assumed):

| Keeps a holding's slot number | Why a recycled slot cannot be mistaken for the old holding |
|---|---|
| seat rows (member, holding) and the offers on them | removed with the holding: a failed IPO's `SettleIpo`, an evicted squat and the claims on a retired holding delete every row and offer before the slot is free (invariants `own-orphan`, `offer-row`, `retired-rows`) |
| the IPO bid list, the (target, price) registry, the snapshot rows | belong to UNSETTLED IPO holdings only; `SettleIpo` removes them before the slot can be free (`ipo-registry`, `ipo-seats`) |
| SELL / ALLOCATE proposals | **the incarnation tag** (this repair) |
| BUY proposals | name no slot: a BUY creates a holding (its `holdingId` field is the paying contract's index) |
| vote rows | keyed by (voter, proposal), never by holding (audit C051) |
| pool ids, whose slots `CloseEmptyPool` frees | a pool closes only with an empty roster (nobody ever joined), so no proposal, record, vote, seat or offer can name it |
| calls that pass a holding id (`ClaimPayout`, `BuyPosition`, `OfferPosition`, `SettleIpo`...) | they act on what is in the slot at the moment of the call, and say so in their input; none of them is a mandate that waits |

| Test | What it pins |
|---|---|
| `K5ASellMandateOfAnEarlierHoldingNeverRunsOnTheHoldingThatTookItsSlot` | the verifier's scenario: two passed mandates, one executed, the slot freed and taken by the creator's new deposit of the same asset (incarnation 1 then 2); the other, stale mandate is `NOT_FOUND`: nothing sold, no QU moved, the creator's seat whole, the mandate still PASSED; the creator's own mandate on the new holding sells 1,000 |
| `K5AnAllocateMandateOfAnEarlierHoldingNeverPaysTheHoldingThatTookItsSlot` | the same for ALLOCATE with 200,000 QU unowned in the pool: refused, the pool keeps them, the new holding has paid nothing |
| `K5NobodyCanVoteOnAnOpenProposalAboutAnEarlierHoldingOfTheSlotHeNowOwns` | two proposals (SELL and ALLOCATE) still OPEN when the slot is taken again: the creator's vote on either is `NOT_FOUND`, weight 0, the state not marked, no vote row; a proposal about the new holding is voted on as always |

On the first K5 contract (plus only inert declarations of the three new fields, so that the tests compile) all four tests of the repair FAIL on behaviour: the refused purchase marks the state dirty; the stale `Execute` returns OK, sells 1,000 of the 2,000 new shares and reports proceeds of 1,085,634 QU; the stale ALLOCATE
pays 198,000 QU; the creator's votes return OK with weight 1,000 and put 1,000 into the old proposals' yes weight (`ck/K5/repair-proof-against-first-K5.summary.txt`). Tests extended or changed: `K5ASeatBuyer...` (the dirty-flag assertion and the existing-buyer case), `ContractSharesAreNeverSoldThroughQswap` (its PASSED proposal is written into the state by hand, and now records the holding's incarnation the way `Propose` does: without the tag the mandate is refused as being about another holding, `NOT_FOUND`, before the `QX_ONLY` check it exists to pin; the assertion is unchanged), `K5AFailedIposHolding...` (one assertion: the freed slot has incarnation 0),
`TheInvariantCheckerNamesEveryPropertyItGuards` (a tenth new property, `holding-incarnation`: a free slot with an incarnation, an occupied slot without one or with a duplicate or one above the serial; two deliberate breaks). The fixture: the hand-built holdings draw a serial like the real paths (`adoptHandBuilt`), `expectBooksBalance`
reads pools by `found` (`poolCount` counts ACTIVE pools since K5, see 4h), and **every test now starts at a tick a chain could be at**: `system.tick` lives on from test to test, so a test run ALONE in a fresh process started at tick 0, where every seat reads as vote-locked (`tick <= voteLockUntilTick == 0`) and
`K5ASeatBuyer...` failed (it only passed in suite order); the constructor of the fixture now lifts the tick to 2.

Mutants of the repaired `QPool.h` (`ck/K5/repair-mutation`): seven, each undoing one rule of the repair (Execute without the tag check, Vote without it, the row-budget refusal back after the sync, the serial not kept by `Execute`, a failed IPO's slot keeping its incarnation, a deposit without an incarnation,
a proposal that does not record the tag), are all killed by the repair's tests. The randomised kitchen-sink runs after the repair (8 + 6 + 6 seeds, one IPO / two in a row / two in the same epoch; `ck/K5/exploration`) violated no invariant, the new one included.
The first version of K5 is kept in `ck/K5/first-K5`.

### The seat market and payment correctness (step K6)

Audit findings C033, C100, C018, C028, C022, C045 and (unverified in the audit, found real) C055; owner decisions D7 (admission), D17 (any member can buy a seat) and D23 (nothing may dirty the state for free). What changed in `QPool.h` (the state did not change: **190,201,464 bytes**; no new loop; `SyncPreview` is a lookup and a multiplication):

- **`BuyPosition` names the parcel** (C033). The input has a trailing `expectedCostUnits` (40 to 48 bytes), and the call is `OFFER_CHANGED` (new code 38), refunded and writing nothing unless it equals the live offer's cost units; 0 is `INVALID_INPUT`, which is also what a client of the old layout gets (the node pads its 40 bytes with zeros). The check is the third thing after the offer lookup, before everything that costs.
- **Retired and unsettled holdings are not for sale** (C100, C055). `OfferPosition` and `BuyPosition` refuse `active == 0` with `WRONG_STATUS` and an IPO holding with `ipoSettled == 0` with `SETTLEMENT_PENDING`. I chose to BLOCK the unsettled IPO holding rather than to rebase its offers when `SettleIpo` shrinks the costs
  (the other option the plan names): a rebase needs a loop over the offers or a second number in each offer (`Offer` would grow by 8 bytes x 32,768), a block needs two comparisons. The price is that an IPO seat cannot be sold while the auction runs. A side effect that is good for the fee model: nobody can buy his way into the right to rebid (`RebidIpo` is for seat owners) during the auction, which section 10 had listed as a lever of the rebid loop (a cost for step K9). `shares == 0` is NOT the test (an unsettled IPO holding has no shares and can still earn).
- **Admission (D7)** (C018, C059). In `Contribute` and `BuyPosition` the allow-list is consulted only for a NEWCOMER (`JoinCheck.isNew`) who is not the pool's creator; a member, however he joined, and the creator are admitted. The creator keeps the switch in both directions (`SetPermissioned`); it now decides who may enter. The admission and escrow checks of `BuyPosition` were
  already in front of `SyncHolding` (K5); K6 reads the roster (`JoinCheck`) before the admission check, because the rule needs to know whether the buyer is a member.
- **`WithdrawUncommitted` checks its payment** (C028): the member's balance and the pool's total are put back and the call returns `TRANSFER_FAILED`.
- **Nothing is written for nothing** (C022, C065, D23). `SyncPreview` (a private read-only function) returns the holding as a sync would leave it; `SyncHolding` calls it and writes only when the ledger has accrued something since the holding's snapshot; `ClaimPayout` asks the preview what it would pay and returns `NOTHING_OWED` before any write;
  `OfferPosition` of the very offer that stands and `SetPermissioned` to the current mode return OK and write nothing.

| Test | What it pins |
|---|---|
| `K6TheBuyerNamesTheParcelAndAnOfferThatWasReplacedAfterHeReadItIsRefusedAndRefunded` | the audit's case (300,000 units for 50,000, replaced by one unit): `OFFER_CHANGED`, the 60,000 back, the seller paid nothing, the offer stands, **the state is not marked dirty**; a bigger, a smaller and a huge parcel alike; 0 and an old-layout input are `INVALID_INPUT` and refunded; the parcel that is there is sold (49,500 to the seller); the struct is 48 bytes |
| `K6TheSeatsOfARetiredHoldingCannotBeOfferedAndAnOfferMadeBeforeItWasRetiredIsNotFilled` | a real `SELL_QX` of the last 500 shares retires the holding (cost basis kept); the standing offer is refused `WRONG_STATUS` and refunded, no new offer can be made, the seller cancels, the members claim, and the empty slot is `NOT_FOUND` |
| `K6TheSeatsOfAnIpoHoldingCannotBeOfferedOrBoughtBeforeSettleIpoHasShrunkTheirCostUnits` | the audit's C055 case: refused while unsettled (an offer put in by hand is not filled either); `SettleIpo` returns 40 percent (360,000 / 240,000); half the seat is then offered and bought in the units that are there |
| `K6AMemberIsAdmittedWhateverTheCreatorDoesAndOnlyNewcomersNeedTheAllowList` | the audit's C018 case: a private pool with an empty list; a newcomer is refused (nothing written), a member buys a member's seat and adds capital, the creator joins as a newcomer by buying and by contributing (join fee once), an entry lets a newcomer buy, back to open anyone buys |
| `K6AWithdrawalThatCannotBePaidPutsBothRecordsBackAndSaysSo` | the contract holds 50,000: 100,000 is `TRANSFER_FAILED` with member, pool total and balance unchanged, 40,000 is paid, then the full call works |
| `K6CallsThatChangeNothingAndCallsThatAreRefusedDoNotMarkTheStateDirty` | claims with nothing owed (including a one-QU dividend that floors to nothing for both seats), re-offering the offer that stands, `SetPermissioned` to the current mode, nine `BuyPosition` refusals, refused withdrawals: none marks the state; a claim that pays, a changed offer, a mode switch and a purchase do |
| `K6ABuyerWhoAlreadyOwnsSeatsKeepsWhatHeEarnedAndOverpaymentComesBackAndAStaleOfferIsDropped` | the audit's C045 numbers (ALICE pays 6,000 for BOB's parcel at 5,000: 1,000 back, 4,950 to BOB, 50 revenue, costs 700,000 / 300,000, owed unchanged, then 128,698 / 69,298); a lowered ask; the stale offer (put in by hand) is dropped and the quota returned |

Two properties joined the invariant checker, each broken on purpose in `TheInvariantCheckerNamesEveryPropertyItGuards`: `offer-parcel` (an offer never names more cost units than its seller owns, or none) and `offer-unsettled` (no offer sits on an unsettled IPO holding).

**Existing tests changed (5), and why.** `RevokingAdmissionCannotClawBackOwnedSeats` and `SetPermissionedTogglesGateWithoutTouchingExistingMembers` asserted that a member of a private pool cannot add capital without an allow-list entry (the behaviour D7 reverses: they now assert that he can, and that a revoked NEWCOMER cannot join).
Three tests made an offer on an unsettled IPO holding with `OfferPosition` (refused since K6, audit C055): `LostIpoDropsItsLedgerAndSlotReuseIsSafe` and `K5AFailedIposHoldingGivesBackItsSlotItsSeatRowsAndItsQuotaAndTheLowestFreeSlotIsTheNextOnesTaken` show that `SettleIpo` wipes the offers of a holding that got its whole bid back, so the offer is put into the state by hand now (`k6PlantOffer`: it is the wipe those tests are about; the
K5 test also asserts that `OfferPosition` refuses); `KiOnlyAPersonWithASeatInTheHoldingMayRebidAStrangerCannot` bought a seat of an unsettled IPO holding to show that the right to rebid is read from the seat row, so the purchase is asserted refused and the rows it would have left are built by the new fixture helper `moveSeat`.
The fixture: `buyPosition` takes the expected parcel as an optional last argument and sends the live offer's by default (`LIVE_OFFER`), so no other test needed a change; `buyPositionOldClient` sends the old 40-byte input. No assertion was removed or weakened.

**Regression evidence.** `ck/K6/proof-new-tests-against-K5.summary.txt`: the new test file was run against the K5 contract plus inert declarations of the new input field and error code (`proof-k5-scratch-contract.diff`: the contract ignores both). Six of the seven new tests FAIL there (and the two D7 tests, the KI test and the K5 test above, as changed); the seventh, the `BuyPosition` branches (C045), passes there by nature: C045 is a coverage finding, the K5 code was right and had no test, so the proof that the new test has teeth is
the mutants (below). Four mutants of the K5 contract that break the branches C045 names were run against the WHOLE K5 suite (256 tests, `ck/K6/mutation/results-k5-suite.txt`): three SURVIVE it (`BuyPosition` without the crystallisation of the earnings of a buyer who already owns seats; a stale offer left in the table while the pool's count is lowered; no check that the seller still owns the parcel), which is what C045 said and is no longer true with the K6 test;
the fourth (the excess of the attachment kept) is killed by `BuyPositionJoinsTheBuyerAsAMemberAndALaterContributeDoesNotListHimAgain` and `EveryProcedureRefundsAnAttachedRewardExactlyOnceOnEveryPath`, tests that steps K0 and K1 added after the audit froze its copy (the audit said it survived the 114-test suite).
Mutants of `QPool.h` (`ck/K6/mutation`, 22): the parcel check and the refusal of a parcel of 0; the retired and the unsettled refusals of `OfferPosition` and of `BuyPosition` (four mutants); the re-offer of the very offer that stands; the D7 rule reverted in `Contribute` and in `BuyPosition`, and the creator's exemption removed from each (four); a withdrawal that ignores a failed payment, one that puts back only the pool's total and one only the member's balance;
`ClaimPayout` that syncs before it refuses (the old order) and one whose preview skips the sync; `SetPermissioned` that writes for nothing; `BuyPosition` without the buyer's crystallisation, without the excess refund, with the stale offer left in the table and without the seller-owns-the-parcel check. **21 are killed** (each by at least one test of the K6 filter, `ck/K6/mutation/results.txt`) and **one survives and is equivalent**: `SyncHolding` that writes the holding even when the ledger has accrued nothing.
Every path that reaches that write also writes something else (the ownership row, the pool or the holding itself), and a claim that finds nothing owed no longer reaches it, so neither the dirty flag nor any state tells it from K6's code; the write-on-change is kept as a plain saving, not as a fix a test can pin.

### What the suite covers

The original four invariants keep a dedicated test each:

- `LaterContributorDoesNotDiluteExistingHolding` — a new member joins with 5x the
  existing holding's entire cost basis; existing payouts are unchanged.
- `SeatSaleDoesNotStealAlreadyEarnedPayouts` — a seat sold *after* a dividend accrues
  leaves the earned amount with the seller and starts the buyer at zero.
- `VoteThenWithdrawIsBlockedUntilProposalResolves` — the vote-lock from section 4a:
  not even 1 QU of a voter's backing balance is withdrawable until the vote has done its work
  (a NO vote: `Tally`; a YES vote: until the mandate is carried out or lapses), while a
  non-voter's withdrawal is unaffected.
- `JoinFeeChargedOnceOnFirstContributionOnly` — the join fee hits the first
  `Contribute` to a pool and never a top-up.

Added since:

- **Fair dividends** — `TwoPoolsSharingIpoTargetSplitRealDividendProRata` (a 300 : 200
  split through the real `qpi.distributeDividends`),
  `RealDividendSplitsTheSameInEverySettlementOrder`,
  `DividendBeforeSettlementIsEscrowedThenSplitByFinalShares`,
  `SoleIpoHoldingIsParkedUntilItSettles`,
  `TwoPoolsDepositingTheSameTokenShareTheIssuersDividend`,
  `UnreconciledQxFillIsEscrowedAndPaidOnReconcile`,
  `StrayDonatedSharesDoNotHoldBackDividends`, the freeze tests
  (`SeatSaleIsFrozenWhileDividendsAreParked`, `SellIsRefusedWhileDividendsAreParked`),
  and `HookReadsTheUniverseWithoutTakingTheUniverseLock`.
- **Randomised invariants** — four tests (a plain ledger; a contract ledger mixing
  IPO-won, deposited and QX-bought shares; sales and dividends against real QX;
  buys against real QX) run 80 to 90 random steps and after each one assert that
  recognised shares equal the sum of the live holdings' shares and what QPOOL really
  owns, that nothing was paid out beyond what arrived (short only by floor dust), that
  the contract's balance moved by exactly arrivals minus claims, and that every view
  equals what a claim pays.
- **Contract shares** — the embedded table row by row, malformed proposals and
  deposits (refused with the exact code, the reward returned once, nothing moved),
  mixed ledgers, name binding and release, real QX and real IPO books.
- **`SELL_QX`** — full, partial and refused sales, hand-over from QPOOL and from
  QSWAP, a fill against the pool's own bid, the 256-bid window, last-holding retire.
- **Buys** — refund and exact cost split over QX and QSWAP.
- **Membership (step K1)** — `BuyPositionJoinsTheBuyerAsAMemberAndALaterContributeDoesNotListHimAgain`
  (found, memberIdx, join fee from the QU beyond the ask, no second fee or slot),
  `ASeatBuyerWhoTopsUpIsChargedOnceByExecuteFindingC002` (the headline reproduction through real
  flows), `ASeatBuyerInTheLastRosterSlotIsAFullMember` (propose and contribute on a full roster; newcomers
  refused by every route before anything moves), `EveryRouteIntoAPoolPaysTheJoinFeeExactlyOnce`, and
  the defences shown with a PLANTED duplicate roster entry (a state no procedure can create any more):
  `ExecuteChargesAPlantedDuplicateRosterEntryOnce`, `TallyReleasesAVoteLockOnceEvenForAPlantedDuplicate...`,
  `SettleIpoRefundsAPlantedDuplicateSeatOwnerOnce`, and `ExecuteRefusesToChargeMoreThanTheMandate` (the
  `BOOKS_INCONSISTENT` guard). The checker has a new property, `roster-index`, and the C002 switch is on, so
  the randomised tests enforce roster uniqueness and the record-per-member rule for every seed.
  Each of the refusals K1 added or moved (a missing join fee, a full roster, an unadmitted buyer in
  `BuyPosition`; a full roster in `Contribute` and `DepositHoldingAsset`) is also asserted not to
  set the state-changed flag.
- **Governance binding (step K3)** — `ASeatThatHasVotedCannotBeOfferedOrSoldUntilTheVoteWindowEnds`,
  `AFivePercentHolderCannotPassASaleByRecyclingHisUnits` (the audit's exploit: refused at the first hop),
  `MoneyBroughtInAfterAProposalOpensHasNoVoteOnIt`, `AProposerCannotCarryHisOwnBuyWithMoneyBroughtInAfterProposing`,
  `AYesVoterCannotWithdrawBetweenTallyAndExecuteAndExecuteLiftsTheLock`,
  `ASixtyPercentHolderCannotPassABuyThatOnlyTheFortyPercentAbstainerWouldPay`, `ALateTallyExpiresTheMandate...`,
  `TheExecuteWindowIsMeasuredFromTheEndOfTheVote...`, `AnExactBudgetBuysEverythingDespiteRounding...`,
  `AMandateThatTheYesVotersCannotBuyEvenOneShareWith...`, `ASwapIsSizedToTheFundedPart...`, `ASwapsMinimumOutIsRoundedUp...`,
  `AnIpoBidIsSizedToTheFundedPart...`, `AProposalNobodyTalliedOrExecuted...`, `ExecuteLiftsOnlyTheLockOfItsOwnVote...`,
  `AYesVoterWhoseShareRoundsToNothing...`, `AMandateIsFundedWithWhatItsYesVotersStillHave...`, `AVoteNobodyEndedNeverTakesWeightOutOfALaterLock...`,
  `CapitalHandedBackBySettleIpoHasNoVote...`, `ALateJoinerCanVoteOnARecycledSlot...`,
  `AProposalAboutAHoldingThatLostItsIpo...`, `VoteAndExecuteRefuseBooksThatDoNotAddUp...`,
  `SellVotesAreDeletedAtTally...`. The checker has five new properties (`vote-stale`, `vote-terminal`, `seat-lock`, an exact `lock-sum`,
  and `vote-within-eligible`, which was off until K3).
- **IPO settlement (step K4)** — `ThreePoolsSettleToIdenticalPerMemberResultsInEveryOrder` (three pools, three settle orders, a dividend parked
  until the last settlement: identical per-member results and the pot used up exactly),
  `AFailedIposPrincipalReachesItsOwnPoolEvenWhenAnotherIposWinnerSettlesFirst` (two targets, one fails, one wins, both orders),
  `TheSnapshotCountsOnlyQpoolsSlotsAtAPriceAnOutsiderBidsAtToo` (ties in both directions), `AHoldingOutbidInFullGetsItsWholeBidBackWhateverTheOrder`,
  `TheEndOfEpochSnapshotReadsTheIpoBookExactlyAndOnlyAtTheEndOfTheIpoEpoch`, `TheSnapshotIsTakenOnceAndALaterEndEpochDoesNotTouchIt`,
  `TheSnapshotDoesNotWaitForTheRevenueGateOfEndEpoch`, `NobodyCanSettleAPoolOutOfARunningAuctionNoMatterWhatTheBookShows` (C020, C110),
  `AHoldingThatWonSeatsUnderAnUnprovenNameIsNeitherSettledNorRetired` (C037), `TheSharesOfAHoldingMustBeThereOverAndAboveWhatTheLedgerAlreadyCounts`,
  `AHoldingWithSharesIsNeverRetiredAndKeepsAUnitOfCost...` and `ASettledHoldingWithSharesIsNeverRetiredEvenWhenItsWholeBidCameBack` (C038),
  `TheRefundIsSplitExactlyOverTheSeatsAndNothingIsStranded` (C068), `TheRefundPotIsASolvencyClamp...`,
  `ASnapshotThatSaysMoreWasWonOrSpentThanWasBidIsRefusedWithoutAWrite`, `WhenALedgerEmptiesWhatItAccruedAndNobodyCreditedIsBookedAsRevenue`,
  `ADeclaredNameThatTheUniverseHasEnoughOfStillCannotJoinALedgerBoundToAnotherName`,
  `ABidInAnIpoTargetBeyondTheEighthIsRefusedBeforeAnythingMoves`, `KitchenSinkWithTwoIposKeepsTheInvariants` (which also lets some proposals
  declare a wrong share name, so holdings that cannot be settled stay in the run). The checker has three new properties
  (`ipo-targets`, `ipo-seats`, `ipo-pot`) and the C004, C005 and C038 switches are on, so the randomised tests enforce the ledger counts for every seed.
- **IPO bidding, finalized (step KI)** — `KiTheLastHalfHourOnePoolIsOutbidAndRebidsFiveTimesWithItsMoneyBackEachTimeAndWins` (two pools and an outsider; pool A is outbid and
  rebids five times at rising prices, its money back each time, and wins; pool B is outbid until the end; a dividend parked until the last settlement; settled in both
  orders to identical per-member results), `KiAPoolOutbidBelowItsCeilingKeepsRebidingUpToTheCeilingAndNeverAboveIt`, `KiOnlyAPersonWithASeatInTheHoldingMayRebidAStrangerCannot`,
  `KiARebidAtOrBelowTheMinimumOfAFullBookAndAtALivePriceIsRefusedCheaplyAndADeadPriceIsReleased`, `KiARebidAfterTheWindowHasClosedIsRefused`,
  `KiARebidMayNotAskForMoreSeatsThanTheHoldingIsMissingOrWasVoted`, `KiARebidSpendsOnlyWhatHasComeBackAndAShortPotClampsTheDrawSafely`,
  `KiNineRebidsAcrossTheEpochKeepTheListToTheLivePricesAndTheRegistryInStep`, `KiTheListHoldsSixteenLivePricesAndAFullListIsRefusedUntilAPriceDies`,
  `KiAHoldingThatAlreadyHasEverySeatIsRefusedFromTheListButAStaleListIsNotBelieved`, `KiAPoolOutbidUntilTheEndAndLeftWithNoSeatGetsItsWholeBudgetBack`,
  `KiAnIpoIsVotedOnForOneDayEveryOtherKindStillForThreeDays`, `KiTheExecuteWindowOfAnIpoRunsFromTheEndOfItsOneDayVoteWhenEverTallyIsCalled`,
  `KiTheVoteLockOfAnIpoEndsWithItsLifeOfThreeDaysAndThatOfOtherBuysWithTheirsOfFiveDays`, `KiAnIpoProposalMustCarryACeilingAndABudgetThatCoversItForEverySeat`,
  `KiExecuteRefusesAStoredIpoProposalWhoseCeilingOrBudgetIsWrong`, `KiTheOpeningBidIsSizedToWhatTheYesMoneyBuysAtTheCeiling`,
  `KiTheViewsShowTheCeilingTheLivePricesTheFreeFundsAndTheRemainingQuantity`. Every figure in them was worked out with a model of the core's bid loop before it was pinned. The
  randomised tests now also rebid (an informed outbid-and-rebid macro and a random one). The checker has three new properties (`ipo-bids`, `ipo-list-seats`, `pool-ipo-funds`) and its
  `lock-sum` and `ipo-seats` properties read the per-kind window and the exact clearing price.
- **Money safety** — `ProposeRefundsAttachedRewardExactlyOnce`,
  `FailedIpoBidLeavesNoTrace`, `FailedQswapBuyLeavesNoTrace`,
  `FailedClaimPaymentKeepsTheClaim`, `SettleIpoIsSingleShot`, and
  `RefusalsInProposeVoteTallyAndExecuteDoNotDirtyTheState`.
- **The seat market (step K6)** — `K6TheBuyerNamesTheParcel...` (C033), `K6TheSeatsOfARetiredHolding...` (C100), `K6TheSeatsOfAnIpoHolding...` (C055), `K6AMemberIsAdmittedWhateverTheCreatorDoes...` (C018, D7),
  `K6AWithdrawalThatCannotBePaid...` (C028), `K6CallsThatChangeNothingAndCallsThatAreRefused...` (C022, D23) and `K6ABuyerWhoAlreadyOwnsSeats...` (C045); the table is in "The seat market and payment correctness (step K6)" above.

The figures pinned in the tests added by the last three stages were cross-checked
against an independent floor-arithmetic model before being pinned; the arithmetic in
the original 27 tests was hand-computed and passes.

### What the suite does not reach

- The tick processor's scheduling of the hook after `finishIPOs()` (the harness calls it inline), and the live order of the epochs.
- The moment a refund reaches the hook inside a rebid. On the node the hook runs inside the `bidInIPO` call, right after the bid's loop; the harness
  delivers the self-refund of a rebid right after the call returns (`runIpoBidCall`). The contract reads the pot only before the bid (what it may draw) and relative to what it
  holds when it writes back (`pot − drawn`, and `drawn` is at most what the pot held before), so the final state is the same, and a contract that counted the refund of its own bid would
  fail `Ki2TheRefundsOfTheSeatsARebidPushesOutCannotFundThatRebid` in both.
- Two IPO targets that end in the SAME epoch are covered since step KX (eight books in one epoch, `TESTEXD` to `TESTEXK`, and the earlier `TESTEXC`: "Several IPOs
  at the same time" above). What is still not reached is the real tick processor's order of the refunds' hooks across IPOs (the harness runs each inline, which is
  the order the node's synchronous `notifyContractOfIncomingTransfer` gives) and a ninth book in one epoch (the eight-row table refuses a ninth target before any bid).
- The `issueAsset` failure of `finishIPOs()` (a share name that is already an issued asset) and a QPOOL END_EPOCH that the node skips: planted
  states only (`IPO_NO_SNAPSHOT`), never the real thing.
- `SELL_QSWAP` is exercised end to end only for shares QSWAP already manages. The two
  hand-over branches (`releaseShares` for QPOOL-managed deposits, and QX's
  `TransferShareManagementRights` for a QX-bought token) are not exercised by any test.
- A few defensive guards (a reconcile on a ledger with unsettled IPO holdings, an IPO
  bid into a ledger with a pending bid) are tested only on hand-built states, because
  validation keeps real states from reaching them; two failure paths of `SELL_QX`
  (QX failing right after accepting an order; the cancel failing after a sale) cannot
  be reproduced without fault injection.
- Two defences of the seat market are reached only on states the contract no longer makes: the stale offer that `BuyPosition` drops (the seller owns less than he offered: built by editing his seat) and
  the offer that is there on an unsettled IPO holding (planted); and the failed payment of `WithdrawUncommitted` is reached by taking the QU out of the contract by hand (the books cannot get there).
- Load, timing and the state-hash cost on a real node.

---

## 6. Stage 2 — testnet

Qubic testnet is EFI bare metal with multiple nodes, supported by the test team
(lead: kavatak) via the **#test-net channel on Discord**. Coordinate there
first. Branch convention per `doc/contributing.md`:
`testnet/YYYY-MM-DD-QPOOL` off `develop`.

Watch these on the nodes (from `test/README.md`):

- **[High]** No misalignment on `computerDigest` / `spectrumDigest` /
  `universeDigest` — `md5sum` the end-of-epoch files across nodes.
- **[High]** Stack usage and per-call cost — press **F2**. The loops, with bounds:
  `Propose` scans the full 262,144-slot proposal array for the one-live-proposal
  check (and again to find a free slot, though that loop breaks early), while
  `Propose(BUY_IPO)` checks the price against the registry of live IPO bids, one hash
  lookup (it scanned all 131,072 holding slots before K2); `Execute` and
  `DepositHoldingAsset` look for a free holding slot from a cursor (`holdingFreeHint`: every slot below it is occupied, so the search is O(1) unless the
  first slots after it are in use) and `Execute` of a buy loops the pool's members (up to 256)
  three times (who pays, the exact split over a local array that reads no state, the writes), a fourth time when a refund came back, and once more to delete the vote rows
  (`ReleaseVotes`, which `Tally` also runs, for every kind, when any vote was cast, and a `Propose` that reuses a stale slot); `SettleIpo`
  loops the members (up to 256) (`BuyPosition` and `DepositHoldingAsset` do not: the
  membership question is one hash lookup, `JoinCheck`; `ClaimPayout`, `CloseEmptyPool`, `EvictSquat` and `ReleaseManagedShares` are O(1)); END_EPOCH reads, once per epoch, the book of each IPO target that has unsettled
  holdings (at most 8 targets: 676 prices each, a cross-boundary read each, and the bidder only of the slots at a bid price of a
  holding; eight IPOs that end in the same epoch are read in that one call), and writes only what it stores; `RebidIpo` makes at most ONE pass over the 676 slots of the book (a read of the bidder per slot and of the price for QPOOL's own, each matched against at most 16 listed prices) and only after every refusal that needs a few lookups and two reads of the book -- a call the cheap bound lets through pays for the pass and can still be refused after it (section 7a); `SettleIpo` is O(8) for the target table plus the two member loops and a loop over the holding's at most 16 prices;
  `Execute(SELL_QX)` reads QX's bid book and QPOOL's ask list (256 entries each).
  Locals stay below the 32 KiB limit: `Execute` 7,664 bytes, `RebidIpo` 2,000, `QxBidBook` 12,448,
  `QxAskRest` 14,440 (the largest). The hook is constant-time except for the iterator
  pass described in 4c.
- **[High]** State size and digest cost. `StateData` is ~181.4 MiB and every tick that
  writes it makes the node re-hash all of it (section 2a); confirm the nodes' load,
  save and digest times are acceptable with that footprint, and compare digest time
  in ticks with and without QPOOL traffic.
- **[Medium]** `#define ADDON_TX_STATUS_REQUEST 1` to verify tx processing;
  read transfer/burn logs with [qlogging](https://github.com/qubic/qlogging).

What must be run live because the harness cannot (section 5): contract-share buys and
deposits through the real QX management hand-over, a real `finishIPOs` refund,
`SELL_QX` against real liquidity (hand-over from QPOOL and from QSWAP), buy refunds
against real QX and QSWAP, the `SELL_QSWAP` hand-over branches, an overlapping-IPO
scenario, and the hook and digest timing at full state size.

### 6a. Core pooling flow

1. `CreatePool(name, permissioned=0)` — attach 10,000,000 QU.
2. `Contribute` from 3+ wallets, each attaching **10,000 QU more** than the
   amount they want held (the join fee). Check `GetMember.uncommitted` is the
   net amount and `GetPool.uncommittedTotal` is the sum. Top up one wallet and
   confirm no second fee.
3. `WithdrawUncommitted` part of one — confirm undeployed capital is returnable.
4. `Propose(BUY_QSWAP, asset, quAmount, shares=minSharesOut, price=0, holdingId=0)`.
   **`shares` is mandatory** — it is the slippage bound, and a zero min-out is
   sandwichable, so the contract rejects it.
5. `Vote` from members (deposit at least one tick BEFORE the proposal opens, or the vote is refused with `DEPOSIT_AFTER_OPEN`). While a vote is outstanding, confirm the voter's
   `WithdrawUncommitted` is refused with `ERR_INSUFFICIENT_BALANCE` and a
   non-voter's succeeds. Have one member vote NO and one abstain. Wait out the 3-day window,
   `Tally` (a YES voter stays locked, the NO voter is freed), then `Execute` (any wallet) within 2 days of the END OF THE VOTE. Confirm `GetHolding.costBasis` equals the
   QU actually **spent** (a swap that went through spends the whole budget, QSWAP's
   100,000 QU fee included; if QSWAP refused it, `Execute` returns `EXTERNAL_CALL`
   with `refunded` showing the QU that stayed with the members),
   each YES member's `GetPosition.cost` is their share of what was spent, and the
   NO voter and the abstainer have no position and an untouched `uncommitted`.
6. Let a dividend arrive (the issuer sends QU to the contract's address), then
   `ClaimPayout` per member. Verify the 1% skim in `GetStats.totalManagementFees`.
   **For a shared target run `SettleIpo` for EVERY holding before expecting claims**;
   `GetSource` shows `escrow` and `unsettled`.
7. `Propose(SELL_QSWAP, ..., shares, quAmount=minProceeds)` — **`quAmount` is
   mandatory** for the same slippage reason and **must exceed 100,100 QU**. **Before
   `Execute`, make sure the contract's own balance is at least 100,000 QU** (the QSWAP
   sell fee, advanced and recovered from the proceeds — section 2); on a fresh
   deployment with no fee income yet, send it some. Confirm proceeds distribute through
   the same accumulator and cost basis is untouched.
8. **Shared source.** Have two pools hold the same asset (the same token, or the same
   IPO target) and let a dividend arrive before and after both have settled. Confirm it
   splits by shares — 60 : 40 for 300 : 200 shares — not to whichever pool registered
   first; confirm `GetSource.recognized` is the sum of the holdings' `shares`.
9. **QX bid, partial fill.** `BUY_QX` with a budget above `price × shares`. Confirm the
   excess comes back to the members' `uncommitted` (`Execute_output.refunded`,
   `costBasis` is the spent amount, `globalUnattributed` unchanged). Let part of the
   bid fill, let a dividend arrive, confirm `GetSource.escrow` is non-zero and
   `BuyPosition` / sells on that source return `SETTLEMENT_PENDING`, then call
   `ReconcileHolding` and confirm the set-aside slice is paid. Confirm a second `BUY_QX`
   on that payer is refused with `QX_BID_PENDING` while a bid is unreconciled.
10. **`SELL_QX`.** With resting bids on QX: `Propose(SELL_QX, ..., shares, price=limit,
    quAmount=minimum)`, `Execute`. Confirm `Execute_output.sold` and `proceeds`, that
    QX shows **no ask of QPOOL's** afterwards, that a partial fill marks the proposal
    EXECUTED, and that an empty book returns `NO_FILL` with nothing changed.

### 6b. Seat market / buy-outs

11. `OfferPosition(holdingId, costUnits, askPrice)`, then `BuyPosition` from
    another wallet, **naming the parcel** (`expectedCostUnits` = the `offeredCostUnits` that `GetPosition(holdingId, seller)` shows). QU goes to the **seller**, not the pool; the asset never
    moves. The seller receives the ask price less 1%. Confirm both sides' earned
    payouts are preserved across the transfer. Replace the offer between the read and the buy (`OfferPosition` again with another size): the buy returns
    `OFFER_CHANGED` and everything comes back. `OfferPosition` and `BuyPosition` on a retired holding (`WRONG_STATUS`) and on an unsettled IPO holding (`SETTLEMENT_PENDING`) are refused. A seat sale is refused with
    `SETTLEMENT_PENDING` while the holding's source has escrow, and a newcomer is
    refused with `CAPACITY` when the roster is full. **A buyer who is not a member of
    the pool attaches the ask price plus 10,000 QU** (the join fee; with less
    than that the call returns `INSUFFICIENT_FEE` and refunds everything). Confirm
    `GetMember` then says `found = 1` for him with zero balances, that he can
    `Propose`, that a later `Contribute` of his lists him once and charges no fee,
    and that `GetPool.contributorCount` grew by one.

### 6c. Syndication

12. **Only the pool's creator deposits** (step K5, owner decision D16): a member, an admitted address
    and a stranger all get `NOT_AUTHORIZED` with everything attached refunded and nothing read or
    written, in a public pool and in a private one. Somebody who wants to fractionalise his own
    shares creates a pool of his own. The creator moves an asset he owns under QPOOL's management (QX
    `TransferShareManagementRights`), then
    `DepositHoldingAsset(poolId, asset, amount, seatUnits, contractIndex)`. For a token,
    `contractIndex` is 0. He receives **every seat**; he sells fractions via `OfferPosition`.
    `seatUnits` is a granularity choice, not a valuation. The deposit costs nothing: it puts him on
    the roster if he is not there yet (a roster slot and a zero record, no join fee), and refuses with
    `CAPACITY` before the shares move when the roster is full. A pool holds at most 32 deposits at a time
    (`QPOOL_MAX_DEPOSITS_PER_POOL`), so that deposits can never use the holding slots acquisitions need.
    **Pre-flight, because moving management is the first step and cannot be undone by the deposit:** read
    `GetSource(issuer, 0)` (for contract shares `GetSource(id(index, 0, 0, 0), 1)`): if the issuer's ledger exists and is bound to ANOTHER
    asset name, every deposit of this asset is `SOURCE_CONFLICT`. Since K5 the move itself is refused in that case (QPOOL's
    `PRE_ACQUIRE_SHARES` returns `allowTransfer = false`, QX reports 0 transferred and the shares stay tradable); for contract shares the pairing
    with the index is not known when the shares move, so a deposit that is refused afterwards leaves them under QPOOL's management: take them back with
    `ReleaseManagedShares(asset, amount, 1)` attaching QX's 100 QU transfer fee (what is attached beyond the fee comes back).
12b. `ReleaseManagedShares(asset, amount, destinationIndex)` moves only shares that **the caller owns** and QPOOL manages (a pool's holding is owned by the contract,
    so a depositor cannot pull his deposit out with it). Attach what the destination asks for; with too little the call returns `TRANSFER_FAILED` and
    `requiredFee` says how much. It writes nothing.
12c. `CloseEmptyPool(poolId)` (creator only) closes a pool in which nothing was ever done (no member, no money, no holding, no allow-list entry); the
    registration fee stays, the slot is the next one `CreatePool` takes. `WRONG_STATUS` for any pool that was used.
12d. `EvictSquat(holdingId, rivalName)` (anybody): see 4d, "Squatting".

### 6d. Permissioned pools

13. `CreatePool(name, permissioned=1)`, then `SetAdmission` (creator only; at most 256 entries per pool, `CAPACITY` beyond; admitting an
    admitted address or revoking one that is not listed changes and writes nothing). Verify the gates: `Contribute` and `BuyPosition`
    refuse addresses that are neither on the list, nor members of the pool, nor its creator (a deposit is the creator's alone, so it has no gate). Revoking an
    entry stops an address that has not joined yet; it **cannot** claw back seats already owned and does not stop a member from adding capital (owner decision D7, step K6).
    An entry can be revoked also after the pool was opened again.
14. `SetPermissioned(poolId, 1)` on an open pool (creator only; in both directions, at any time, and a call that sets the mode the pool has writes nothing). Existing
    members keep their seats and votes **and stay admitted**: a member of the pool always counts as admitted for `Contribute` and `BuyPosition` whatever the allow-list says,
    and so does the creator (owner decision D7; before step K6 a switch to private left an empty list and locked every member out, which let the creator buy distressed
    seats while nobody else could). What the switch decides is who may ENTER: a newcomer needs a `SetAdmission`. Flip it back
    with `SetPermissioned(poolId, 0)`; the admitted list is kept, not cleared,
    and simply stops being consulted.

> **Permissioned is access control, NOT confidentiality.** Every byte of
> contract state is replicated to every node, so member addresses,
> contributions and holdings are public regardless. A private pool controls who
> may *join*; it cannot hide who has joined. `Pool.admissionRoot` is reserved
> for a future ZK membership set (see section 8) and is unused today.

### 6e. Contract shares

15. **Buy on QX.** `Propose(BUY_QX, {NULL_ID, name}, quAmount, shares, price,
    holdingId = paying contract's index)`. Confirm a wrong index for a listed contract
    is refused with `CONTRACT_MISMATCH`, index 0 / QPOOL's own / above 1,023 with
    `INVALID_INPUT`, an unlisted index of a contract that does not exist (or is in its
    IPO epoch) with `CONTRACT_NOT_LIVE`, and `BUY_QSWAP` of the same asset with
    `INVALID_INPUT`. After a fill and a dividend (the contract's own
    `qpi.distributeDividends`), confirm the holding earns.
16. **Deposit.** Move contract shares under QPOOL's management, then
    `DepositHoldingAsset(..., contractIndex)`. A wrong or missing index is refused and
    **the shares do not move**. Confirm a deposited holding and an IPO-won or QX-bought
    holding of the same contract split one dividend by shares, and that a deposit while
    an IPO holding of that contract is unsettled is refused with `SETTLEMENT_PENDING`.
17. **Only QX.** `Propose(SELL_QSWAP)` on a contract-share holding returns `QX_ONLY`;
    `SELL_QX` works and, when the last holding of an unlisted contract sells out, its
    name claim is released.

---

## 7. IPO bidding — the constraints that shape deployment

All three verified against `src/qpi/impl/qpi_ipo_impl.h` and
`src/contract_core/ipo.h`.

**1. QPOOL can only bid on contracts with a HIGHER index.** The guard is
`if (_currentContractIndex >= IPOContractIndex) return -1;`. A contract must
already be registered to have an IPO, so any IPO live when QPOOL deploys is at a
lower index and permanently out of reach. QPOOL can bid on **every contract
added after it** — which is every future Qubic contract.

> Note this is the **reverse** of the normal call rule, where a caller needs a
> *higher* index. Same codebase, opposite directions.
>
> **Practical consequence: register QPOOL at the lowest index you can get.**
> Everything after it becomes biddable; nothing before it ever will be.
>
> **What the index does not limit:** shares of the contracts below QPOOL's index
> — all 30 in the embedded table, if QPOOL is 31 or higher — cannot be won in an IPO
> but can still be **bought on QX** and **deposited** (4d), whatever the index.

**2. IPO bids cannot fire from a callback.** `bidInIPO` returns -1 when
`contractCallbacksRunning != NoContractCallback`, so END_EPOCH is out. All
execution is by permissionless user procedure.

**3. Bids are only valid during `constructionEpoch - 1`** and settle at epoch
end, so shares arrive asynchronously — and **without any callback to QPOOL**, which
is why `SettleIpo` has to be run (below). The book itself is only readable until that
epoch ends: the node runs every contract's END_EPOCH first, with `system.epoch` still
the IPO epoch (`qubic.cpp`: `endEpoch()` runs the contracts' END_EPOCH, then
`finishIPOs()`, then `system.epoch++`), so QPOOL reads the final book in its own
END_EPOCH (below).

### 7a. The bid cycle

An IPO is a uniform-price auction: the 676 best bids win a share each, every winner pays the
FINAL price (the lowest winning bid) and the premium over it is refunded when the IPO ends. It is
won and lost in the last hour of the epoch: a pool is typically outbid and re-bids three to five
times in the final half hour, depending on where the price and the book stand, and once it has been
outbid and its money has come back it must be able to bid again. The owner's rule (decision D1, final;
step KI2): **the IPO bidding is governed by the total amount of QU allocated, because the higher the IPO
price becomes the fewer shares a pool can buy.** There is no voted share quantity and no ceiling price:
a pool cannot know how high an auction will go, so **the vote approves a budget and an opening price, and the
holding may then bid again, as often as it needs, with that money and nothing else bounding it.** The seats a
holding holds are whatever its money buys at the prices it bids, and they shrink as the price rises.

- **The vote.** `Propose(BUY_IPO)` commits nothing yet and fixes the terms. `holdingId` is the
  **target contract's index** (above QPOOL's own, at most 1,023); `asset` must be `{NULL_ID, shareAssetName}`
  (contract shares are protocol-issued and contracts cannot read `contractDescriptions`, so the name is
  declared by the vote: 4d says what is and is not checked about it); `quAmount` is the **budget**, the QU the pool
  allocates, at most the pool's uncommitted money; `price` is the **opening price**, the price of the first bid: a starting
  point the voters look at, **not a limit** on later bids. The core's own limits are enforced before anything is multiplied or
  cast (audit C001): `1 <= opening <= 1,479,289,940,828` (MAX_AMOUNT / 676) and the budget must buy at least one seat at the
  opening price (`quAmount >= price`), else `INVALID_INPUT`. **`shares` is ignored for a BUY_IPO** (the field stays in
  `Propose_input` so that clients keep their layout; any value is accepted): `Propose` stores the opening quantity it derives,
  `min(676, floor(budget / opening price))`, in the proposal's `shares`, and `GetProposal` shows it to the voters (what the opening
  bid takes if every voter pays). Because the quantity is derived and bounded by construction, no product the core multiplies can
  wrap or leave the QU range (`676 × 1,479,289,940,828 <= MAX_AMOUNT`). Every other kind reads `shares` as before.
  A BUY_IPO is **voted on for 24 hours** (`QPOOL_IPO_VOTE_WINDOW` = 86,400 ticks; every other kind keeps
  its 72); its mandate then lasts the usual **2 days from the end of the vote** (`Proposal.passedTick = openTick +
  vote window`, whenever `Tally` runs), so a BUY_IPO's whole life is 3 days (`VoteWindowOf` is the one definition that
  `Vote`, `Tally`, the execute window and the vote locks use; the two scans of `Propose` over the proposal slots use the matching lifetime constants inline).
  **Ticks are nominal.** Every window is counted in ticks, and a tick is one second only at the core's target
  speed (`TARGET_TICK_DURATION` is 1,000 ms): 24 hours is 86,400 ticks at that speed and more or less in real time
  if the network runs slower or faster.
- **The opening bid is ALL-IN.** `Execute` checks the proposal against the same limits again, refuses with `IPO_REJECTED`
  when the target's IPO is not running (the first thing it looks at), refuses with `IPO_PRICE_TAKEN` when a live bid
  of the same IPO uses the opening price, takes the budget from the YES voters exactly as for every other buy (K3), and then makes
  ONE `bidInIPO(target, opening, n0)` with **`n0 = min(676, floor(assigned / opening price))`** seats. The full `opening × n0` leaves
  the contract (the part the core does not seat comes straight back as a refund), `ipoAtRisk` is what the holding's bids have put
  into the book and `ipoAvailable` is the rest of the budget, less than one seat at the opening price (the **free funds**);
  `ipoAtRisk + ipoAvailable` equals `costBasis`, the budget, for as long as the holding is unsettled. The measured-spend defence is
  unchanged: right after the call the contract's balance can only have fallen by the cost less what the core handed back; if it fell
  by more, the call records the holding all the same and returns `BOOKS_INCONSISTENT` (a refusal there would leave the bid in the book
  unbooked).
- **`RebidIpo` (procedure 13, reinstated): bounded by money and by nothing else.** `RebidIpo(holdingId, price, quantity)` places a
  further bid for an unsettled IPO holding. There is **no cooldown and no limit on the number of calls**.
  - *Who:* the caller must **own a seat** in the holding (`cost > 0`): only a person with money at stake acts for
    the holding; a stranger, a member of another pool and a member without a seat are refused with `NOT_MEMBER` (4). A seat owner of any size
    may spend the holding's free funds (they are the holding's, not his): the vote approved a budget, not a single bid.
  - *Price:* any price from 1 to 1,479,289,940,828 (`INVALID_INPUT` outside it) that **the money pays for** (below); on a **full**
    book `price >` the book's minimum (the price of the 676th slot), otherwise the core would refuse the bid and only register a price
    (`PRICE_TOO_LOW`, 36). The price of **another** holding of the same IPO is refused (`IPO_PRICE_TAKEN`, 17: the registry below); a price
    **this** holding already has is its own and may be used again: the bid adds seats at that price and takes no entry in the list.
  - *Quantity:* `quantity = 0` means **as many seats as the free funds buy at this price** (at most 676); a number asks for that many
    (`INVALID_INPUT` above the core's 676). There is no voted quantity to run into. A bid evicts the lowest slots priced below it, one
    unit each, and a unit that finds no slot below its price is refunded at once, unseated: the quantity is the caller's choice.
  - ***THE MONEY RULE.*** A rebid at price p for n seats is allowed when **p × n is at most the holding's free funds at the START of
    the call**, after the call's own reconcile pass (below) has turned the refunds of EARLIER evictions into free funds, **and at most the
    contract's physical balance**; otherwise the call is refused and **nothing is written** (`INSUFFICIENT_BALANCE`, 6, when the free funds
    do not cover it; `BOOKS_INCONSISTENT`, 31, when they do but the contract's balance is lower, which can only mean the books and the balance
    disagree). **The refunds of the seats the bid itself pushes out are neither counted nor predicted.** The core (`ipo.h`,
    `bidInContractIPO`) first DEDUCTS the whole `price × quantity` from the bidder's physical balance (and does nothing, returning -1, when
    the balance is too small), then walks the units one at a time, and pays every refund only AFTER the loop, into the hook. So a bid that
    replaces the pool's own lowest seats gets their price back only after it has paid the whole of its cost, and **those refunds can
    fund the next rebid, never this one**. The books after a call may therefore still show the own seats it pushed out as held, until the
    next reconcile (the next rebid, or the settlement, which reads the book itself). A pool that was outbid recovers step by step, each
    step funded only by money that has already come back, and it recovers fewer seats than it lost when the price has risen: the economic
    limit is the budget (the same 200,000 buys 100 seats at 2,000 and 95 at 2,101, never the 100 again).
  - *The window:* the target's IPO must be running and not yet read for the last time (`IPO_REJECTED`).
  - **The reconcile, in the same call.** After the cheap refusals, ONE pass over the 676 slots counts, for each price the
    holding has registered, the slots QPOOL still holds there (a price belongs to exactly one holding, so this is the
    holding's own count whatever the other pools did). Every seat that was lost at price p **before this call** was refunded by the
    core into the pot `ipoRefundEscrow` at the moment it was lost, and now moves exactly p from `ipoAtRisk` to
    `ipoAvailable`, **clamped by what the pot holds, which is debited by the same amount**. Money that was outbid and has come back
    is therefore usable in the same call, no separate reconcile call exists, and because every live price has one owner
    the attribution is exact whatever the order of the calls and however many pools are outbid in the same tick (a pool draws its own
    lost seats' price and never another pool's refund, which sits in the same pot). A refused call writes nothing at all (it does not
    even mark the state dirty).
  - **Dead prices and the list.** A price with no seat left is released from the registry and from the holding's list at the next
    rebid (the one that finds it dead): the list counts LIVE prices only (audit C016) and anyone may then bid at that price.
    **`QPOOL_MAX_IPO_BIDS` = 16** live prices per holding. Because a rebid may reuse a price the holding already has, a pool that keeps
    bidding at one price never grows its list, and it cannot be locked out by it. A list that is full of distinct prices **that all still
    have seats** refuses a further **new** price with `CAPACITY` (after the pass, nothing moves): the pool then bids at a price it already
    has (any of them above the book's minimum), or waits for one to die (the lowest prices go first, since eviction takes the lowest slot).
  - **What a rebid does to the book.** A bid of n slots at price p evicts the n LOWEST slots of a full book that are priced below p (the
    youngest of equal prices first), whoever owns them. A pool that is being outbid is at the bottom, so its own remaining seats are usually
    the lowest slots: **a rebid first upgrades them** (their price goes into the pot after the bid, the same number of slots is seated at
    the new price), until the bid reaches slots of other bidders.
  - *Cost of a failing call.* Everything up to the pass is a handful of lookups and two reads of the book, and one walk of the holding's
    list (at most 16 prices) that bounds the money the holding can have: the seats the list shows at a price ABOVE today's book minimum cannot
    have been lost (a slot is only ever lost by being the lowest of a full book and the minimum never falls), so what they are worth at their
    own prices is still at risk, and `free funds + min(the rest of the at-risk money, the pot)` is the most the holding can have after a
    reconcile. A rebid that bound cannot pay for is refused from the list, without the pass. The list is believed only for those prices; a
    stale count at or below the minimum is not (the pass runs). **So a refusal is cheap only when the bound refuses it.** A call the bound lets through
    (the holding has seats at or below the minimum, or the pot holds money, possibly another pool's) pays for the 676-slot pass and may still be refused
    after it, with nothing written and the reward back: `INSUFFICIENT_BALANCE` (the money has not come back; it is the pass that finds out, for example,
    that the pot holds only another pool's money), `CAPACITY` (a new price at 16 live prices), `BOOKS_INCONSISTENT` (the pass finds more seated than the
    holding put in, or the cost is above the contract's physical balance) and `IPO_REJECTED` (the core refuses the bid). Tested across two IPOs in
    `KxTheSharedPotNeverLetsAPoolDrawTheMoneyOfAnotherPoolInAnotherIpo`. Each such call is executed work paid from the execution fee reserve (section 2a);
    the pass is bounded (676 reads) but not free, and a seat owner can repeat it: see section 10.
  - `RebidIpo_output` is returnCode, bidsRegistered, seatsBefore (the seats the holding held when the book was read), freed
    (the money moved from at-risk to free funds by the reconcile), atRisk, available, livePrices, and bidQuantity (the seats bid for: the
    defaulted quantity when `quantity` was 0).
- **What a bot reads.** `GetIpoBids(holdingId)` (function 9): found, the target, the budget, the highest price bid so
  far, the seats (as of the last reconcile), the free funds, the at-risk money, the refund pot, and the
  list of live prices with their seat counts as of the last reconcile. The seat counts are a number for the eye: the next
  rebid and the settlement read the book again. `GetProposal` shows the derived opening quantity. The book itself is read off-chain
  from the node.
- Being outbid fires `TransferType::ipoBidRefund` (source `NULL_ID`). Refunds pool in `ipoRefundEscrow` and are **not**
  attributed on arrival: the core pays one refund per entity per bid call and it carries no identity. Each holding takes
  what is its own when it reconciles (above) or settles (below), and the pot never pays out more than has arrived.
- **The snapshot (END_EPOCH).** `Execute` keeps a table of at most 8 target contracts that have unsettled holdings
  (`GetIpoTarget`); a bid in a ninth target is refused with `CAPACITY` before anything moves. At the end of the target's IPO
  epoch END_EPOCH reads each such target's book once: the clearing price is the price of the last slot (0 when the book is not
  full: the IPO failed and every bid is refunded in full), and in one pass over the 676 slots it counts, for **every price the
  registry knows** (a holding may have several), the slots whose bidder is QPOOL. For each price it stores the seats and what they
  cost (`seats × finalPrice`). At most 8 × 676 slots are read; there is no loop over pools, holdings or proposals.
- `SettleIpo(holdingId)` closes out after the auction, and **it must be run for every IPO holding.** It is **single-shot** and
  permissionless, and it works from the snapshot only after its epoch (`WINDOW_OPEN` before, `IPO_NO_SNAPSHOT` if the node
  skipped QPOOL's END_EPOCH; a stranger cannot end a running auction). It adds up the rows of the holding's prices. What a
  holding gets does not depend on the order in which holdings settle, on a stale count, on how often the holding
  rebid, on the pot, or on how many shares QPOOL holds in all:
  - **Refund:** `ipoAvailable` (free funds: the budget never bid plus everything a reconcile moved back) plus `ipoAtRisk − spent`
    (what the bids still have in the book less the cost of the seats won: the premium over the clearing price, the outbid stake no
    reconcile moved, the whole bid of a failed IPO). The second part is drawn from the pot and clamped to what the pot holds. It
    is split over the seat owners with a running total of their cost units, so the shares add up exactly (audit C068), and
    `costBasis` becomes `seats × finalPrice`: real spend. In terms of the seats: of the money still at risk the holding is owed Σ(price of each
    seat it won) − finalPrice × seats (the premium over the clearing price) plus the price of every seat it lost that no reconcile has moved back,
    and the full principal when the IPO failed or the holding holds no seat.
  - **Shares:** exactly the seats the snapshot gives it, recognised in the ledger, whatever mix of prices they were won at.
    The universe is consulted only as proof (next bullet), never as the number.
  - **A holding outbid right up to the end** and left with no seat gets everything back and retires. **A holding that has shares
    is never retired**, and the refund never takes its last unit of cost (audit C038).
  - It removes all of the holding's prices from the registry, its rows from the snapshot and its list, and the last settlement
    of a target frees the target's row.
- **The name is proven, not assumed (audit C037, D9)** — unchanged: `SettleIpo` recognises a holding's seats only if the
  universe proves the name; otherwise it returns `NAME_UNPROVEN`, writes nothing and does not retire the holding (4d, section 10).
- **A dividend that arrives before every IPO holding of the contract has settled is held, and released per recognised share
  when the last one settles.** A holding that rebid many times parks exactly the same dividends as one that did not.
- `ReconcileHolding` does not serve IPO holdings (`WRONG_STATUS`): it is for QX-bid holdings only.
- **There is no `ReconcileIpo` (procedure id 15 stays reserved) — decided and documented.** A rebid reconciles inside its own
  call, so an explicit call could only move money the members cannot use before a rebid or the settlement anyway, would leave the seat
  counts stale between its calls, and would cost 676 reads each time as a free lever on the fee reserve for anyone (audit C008, C023).

**Distinct prices are mandatory, not cosmetic.** Attribution works because slots at price P belong to the bid placed at price P,
so two unsettled holdings of one IPO must never use the same price. The registry `ipoPrices` maps (target contract, price) to the
one unsettled IPO holding that bids there, and a holding's list (`ipoBids`, keyed by the holding's slot: up to 16
prices with their seat counts) is its other half: every price in a list is registered to its holding and every registry entry is in
its holding's list. `Propose` looks the opening price up (advisory: two proposals at one price can be open at once, and both
pass); `Execute` and `RebidIpo` check it again right before they bid and refuse the second one with `IPO_PRICE_TAKEN`, bidding
nothing (a `RebidIpo` at a price the registry gives to the SAME holding is that holding's own and is allowed); a proposal that loses this race stays PASSED and expires with its execute window. One hash lookup each. A holding whose
opening bid seated nothing (the book was full above its price) still registers the price, with no seat; its first rebid releases it.

**The book is only readable during the IPO epoch** (`ipoBidId` returns `NULL_ID`, `ipoBidPrice` returns `-2` outside it), which
is why `RebidIpo` is refused outside it and the snapshot is taken in END_EPOCH, the last moment it can be read.

**The refund pot is shared, and the attribution does not depend on call order.** `finishIPOs` sends QPOOL ONE refund per entity
per IPO with no IPO identity, so refunds pool in `ipoRefundEscrow`. A rebid draws only what the book says its own holding lost, a
settlement only what the snapshot says its holding is owed, so the pot is used up exactly and the order of calls changes nothing
(`KiTheLastHalfHourOnePoolIsOutbidAndRebidsFiveTimesWithItsMoneyBackEachTimeAndWins` settles in both orders to identical per-member
results; `ThreePoolsSettleToIdenticalPerMemberResultsInEveryOrder`).

---

## 8. Before mainnet

1. **Set a real construction epoch** (currently placeholder `236`).
2. **Choose the index deliberately** — section 7, constraint 1. On the mainnet table
   embedded in the contract it is 31 or higher.
3. **Check the embedded contract table against the network you deploy to** (4d). It
   is a 2026-10-17 copy of the explorer's list and differs from the local core
   checkout at indices 29 and 30.
4. **How dividends reach pools.** (The previous revision's item here, "Unattributed
   QU", described the first holding registered for an issuer taking all of its
   dividends; that was a bug and is fixed.) `POST_INCOMING_TRANSFER` sees only
   `{sourceId, amount, type}`, so every payer is bound to one asset and one ledger; every holding of a
   shared payer is paid pro rata by recognised shares; shares nobody has claimed yet
   are set aside (4c). What still lands in `globalUnattributed`, which has no exit:
   a payment from a sender that is not a registered issuer or paying contract; a
   contract that pays its shareholders by `qpi.transfer` instead of
   `qpi.distributeDividends`; side payments from QX or QSWAP that no tracked call
   claims; `revenueDonation`; dividends for a source that no longer has a holding.
5. **Seed the contract balance.** The first `SELL_QSWAP` needs 100,000 QU of QPOOL's
   own balance (plus 100 if QX must first hand the shares over) to advance the swap fee
   (section 2). Fee income covers it in steady state; a brand-new deployment has
   none. A plain transfer to the contract's address does it (the hook files it under
   `globalUnattributed`, which is the right place for money that belongs to nobody).
   Also make sure the **execution fee reserve** is positive and has a refill plan
   (section 2a).
6. **Decide whether the ~181 MiB state and its per-tick re-hash cost are acceptable**
   for the node operators and for the owner's reserve, or shrink the capacities in
   section 4b. Shrinking later forces a state migration, so decide before launch.
7. **Integration test against QSWAP and QX** for `Execute`'s buy and sell paths on the
   target network — the only procedures making real external calls, and the ones the
   unit tests cannot reach on a real node. The devnet runs in `reports/` covered the
   *previous* revision once; this revision's external paths (section 6) have not
   been run live at all. Re-run after any change.
8. **Audit.** No admin key is a real strength here. Focus on: the hook; the
   lazy-sync discipline (every writer of `shares`, `pendingShares` or `costBasis`
   syncs first, and only `RegisterHolding` and `ApplyHolding` write `Holding.shares`);
   the universe-lock rule (4c); the mixed-ledger rules and name binding (4c, 4d); the
   `SELL_QX` replay, ask and cancel path; the buy-refund accounting
   (`BuyRefund` / `TakeBackRefund`) and the counter restore in sales
   (`UnparkArrivals`); the ownership loop in `Execute`; the accumulator arithmetic;
   the vote-lock accounting in `Vote` / `ReleaseVotes` / `WithdrawUncommitted`, the YES-only charge and order sizing in `Execute`; and IPO price
   attribution.
9. **Decide the open questions in section 10** — in particular IPO late binding, the
   shared IPO refund pot, the exit for `globalUnattributed`, a cancel path for
   QX bids, and the launch profile of the shared tables (the per-pool quotas of 4b, the 512 seat rows a pool gets, and whether the vote rows need a quota too).
10. **On the planned BN254+Groth16 verifier:** the realistic win is unlinkable
    *membership* — prove inclusion in an admitted set without revealing which
    member. Amounts will not hide for free (pro-rata needs the aggregate cost
    basis public; hiding individual amounts needs Pedersen commitments plus range
    proofs), and `qpi.transfer` always writes a public record, so you get
    unlinkability rather than invisible amounts. The primitive to ask the core
    team for is **Merkle inclusion against a contract-held root plus a nullifier
    set** — that one pattern covers private membership and private claims.
    `Pool.admissionRoot` is reserved now because changing `StateData`'s size
    later forces a state migration.

---

## 9. Operating runbook

All of this is permissionless: anyone can run it, and members have the incentive. A
small keeper script per pool is worth having.

**At deployment, once**

- Set the construction epoch, pick the index, check the contract table (section 8).
- Seed the contract balance (section 8, item 5) and fund the execution fee reserve
  (section 2a); decide who watches it.

**While an IPO is running — the rebid bot (section 7a)**

- The vote is a **budget** and an opening price (no quantity, no ceiling: the money is the only limit). `Execute` bids all-in at the opening
  price, `min(676, floor(assigned / opening))` seats. From then on any person who owns a seat of the holding may call
  `RebidIpo(holdingId, price, quantity)` as often as needed, with no cooldown. A holding is outbid when its lowest slots are taken; the money of a
  lost slot is refunded into the pot at once, and the next rebid turns it into free funds (the call reconciles first) and spends it.
- **Every rebid is funded only by money that has already come back** (the money rule, section 7a): a rebid at price p for n seats needs `p × n` of free funds
  at the start of the call, and the contract's physical balance. The refunds of the seats the rebid itself pushes out are not counted: they fund the next one. A pool
  that was outbid therefore recovers step by step, and recovers fewer seats than it lost when the price has risen (the same budget buys `floor(budget / price)` seats at
  the lowest price it holds). Rebid as soon as the money has come back, with `quantity = 0` (as many seats as the free funds buy at that price); each call's refunds are what the next call can spend.
- Poll `GetIpoBids(holdingId)` for the budget, the free funds, the pot and the seats as of the last reconcile, and read the order book from a node:
  the price must be above the book's minimum (`PRICE_TOO_LOW` otherwise) and not a price of another holding of the IPO (`IPO_PRICE_TAKEN`). **Reuse a price the holding
  already has** when you can (above the minimum): it adds seats without taking an entry in the list, so the list of 16 live prices cannot lock the pool out; a further new price while
  all 16 still have seats is refused with `CAPACITY`. A call refused for its input, the holding, the caller, the price (taken, or not above a full book's minimum), the IPO window or the money bound
  taken from the holding's own list costs a handful of lookups, two reads of the book and one walk of the holding's list. **A call that passes
  that bound pays for the pass over the 676 slots whether or not it then wins anything, and can still be refused after it** (nothing is written, the
  reward comes back): `INSUFFICIENT_BALANCE` when the money has not really come back (the bound is only an upper bound: the pot may hold only another
  pool's money, or the default quantity may work out at 0 seats), `CAPACITY` at 16 live prices for a new price, `BOOKS_INCONSISTENT`
  (more seated than the holding ever put in, or the cost above the contract's balance), `IPO_REJECTED` if the core itself refuses the bid.
  A bot should therefore ask `GetIpoBids` and read the book before it calls, and not use the contract to find out whether a rebid would work.
- Expect the **lowest slots to be your own**: a rebid evicts the lowest slots of the book first, so the first rebid after an outbid usually upgrades
  seats the holding already has (the tests show a 43-call sequence, a five-rebid half hour and a ten-rebid epoch). The books may still show the seats a call just pushed
  out as held until the next call; the settlement reads the book itself.
- Only the people whose money the vote committed can rebid: since step K6 a seat of an unsettled IPO holding can be neither offered nor bought (`SETTLEMENT_PENDING` until `SettleIpo` has run), so a
  stranger cannot buy his way into the right to rebid. Any of those seat owners, whatever the size of his seat, may spend the holding's free funds at any price they pay for: the vote is the budget. A pool that
  does not want a member of any size to spend its free funds should not leave free funds: a budget that is a multiple of the opening price leaves none.
- The window is the target's IPO epoch: `RebidIpo` is refused (`IPO_REJECTED`) once END_EPOCH has read the book. Ticks are nominal (one per second at the
  core's target speed): do not plan the last minute around a wall clock; read the epoch's progress from a node.

**When an IPO window closes — the one duty that cannot wait**

- Call `SettleIpo` for **every** IPO holding of the contract, in every pool, as soon as
  the epoch has moved past the window (the shares arrive at the epoch change with no
  callback; END_EPOCH has taken the snapshot by then). Until the last one is settled, that
  contract's dividends are held, seat sales and sales on the source are refused
  (`SETTLEMENT_PENDING`), bought or deposited shares of the same contract cannot join its
  ledger, and the ledger is not bound to a name. The ORDER no longer matters for the money
  (section 7a); the first settlement that wins shares binds the name (section 4d).
- A `SettleIpo` that returns `NAME_UNPROVEN` means the share name the vote declared is not the
  one the universe shows: nothing was settled or lost. Check `GetHolding.assetName` against the
  contract's real share name; it cannot be changed (section 10).
- `IPO_NO_SNAPSHOT` should never be seen: it means QPOOL's END_EPOCH did not run in the target's
  IPO epoch. Report it; nothing of that IPO can be settled.
- Confirm with `GetSource(id(index,0,0,0), 1)`: `unsettled` is 0, `escrow` is 0,
  `recognized` is the total won.

**After a QX buy that did not fill completely**

- `ReconcileHolding(holdingId)` whenever fills may have arrived (it is idempotent and
  claims no more than the bid asked for). While the bid is unreconciled the source
  holds part of every dividend in escrow and refuses seat sales and sells. Only one
  bid per payer may be unreconciled, and a bid that never fills keeps blocking further
  `BUY_QX` on that payer — there is no cancel path.
- Set the `BUY_QX` budget to at least `price × shares`; a `BUY_QSWAP` budget must
  exceed 100,000 QU.

**Selling**

- `SELL_QSWAP`: minimum proceeds above 100,100 QU; the contract needs 100,000 QU of
  balance at the moment of the call. Contract shares cannot be sold this way.
- `SELL_QX`: look at QX's bid book for the asset first; choose `price` and the minimum
  with the 256-bid window and the 0.3% QX fee in mind; execute while the book is deep
  enough. A partial fill uses the mandate up. A refused call costs the caller nothing.
- Both refuse with `SETTLEMENT_PENDING` while the source has escrow; retry after
  `SettleIpo` / `ReconcileHolding`.

**Housekeeping**

- `Tally` an OPEN proposal once its 3-day window has passed, and `Execute` a PASSED proposal
  within 2 days of the END OF THE VOTE (a late `Tally` does not move that deadline; after 5 days from the opening a vote
  can no longer pass). Nothing has to be called to free anybody afterwards: a proposal past its whole life is finished
  (its proposer may propose again, its slot and vote rows are reused and a YES voter's lock lapses by itself).
- END_EPOCH needs nothing: it compacts the tables, and once `pendingRevenue` reaches
  676,000 QU it pays 80% to QPOOL's 676 IPO shares and burns 20% into the reserve.
- **Slots and quotas (step K5).** After a sale of a whole holding its members should `ClaimPayout`: each claim removes the claimant's seat row and the LAST one frees the holding's slot
  (README 4b); a failed IPO's holding is freed by its `SettleIpo`. A pool that is near a quota (`GetPool`: `offerCount` 32, `admittedCount` 256, `ownershipRows` 512, `depositCount` 32,
  `ipoHoldings` 8, `holdingCount` 128) is refused with `CAPACITY` before anything moves, so a client reads the counters first. A creator whose deposit was refused takes his shares back with
  `ReleaseManagedShares` (attach QX's 100 QU fee). Anyone who is locked out of an issuer's other asset by a deposit that never paid can call `EvictSquat` four epochs after that deposit (4d).

**Monitoring**

- `GetStats.globalUnattributed` should stay at the deliberate seed plus strays; growth
  means payers that are not recognised (an unregistered treasury wallet, a contract
  paying by `qpi.transfer`). `dividendEscrowTotal` above zero means somebody owes a
  `SettleIpo` or a `ReconcileHolding`. `ipoRefundEscrow` above zero outside an IPO is
  refunds of holdings that have not been settled yet (it is zero once every IPO holding of every
  IPO is settled, apart from a refund the books did not expect, which nothing sweeps). `totalUnownedSwept` is escrow that nobody could claim.
- The execution fee reserve (section 2a), and `GetSource` for every payer shared by
  several pools.

**Index choice and IPO reachability**

- QPOOL can bid only in IPOs of contracts with a higher index than its own: take the
  lowest index available (section 7). Shares of lower-indexed contracts can still be
  bought on QX or deposited.
- Voters on a `BUY_QX` or a deposit of an unlisted contract's shares must check the
  index against the real contract list themselves (section 4d).

---

## 10. Known limitations and unfixed risks

Nothing here is hidden elsewhere; where a fix is an owner decision, it says so.

**Money and accounting**

- **`globalUnattributed` has no exit.** Anything the hook cannot match lands there: an
  unregistered sender, a payer with nothing recognised, dividends for a source that no
  longer has a holding, a contract that pays by `qpi.transfer`, `revenueDonation`
  (which arrives with a zero sender and could reasonably have been revenue), strays,
  and the balance seed. There is no sweep or recovery path. *Owner decision.*
- **`ALLOCATE` (kind 6) can no longer pay anything.** Nothing feeds
  `pool.unattributedQu`, so a passed `ALLOCATE` returns `NOTHING_OWED`. It is kept for
  ABI.
- **`SELL_QSWAP` has an unrecorded-fee hole that `SELL_QX` does not.** If QX is paid its
  100 QU fee for handing the shares to QSWAP and the swap then fails (`EXTERNAL_CALL`),
  the fee has left the balance and is charged to nobody: the books are short by up to
  100 QU per failed attempt. `SELL_QX` deducts such a fee from `pendingRevenue`; the
  same fix has not been applied to `SELL_QSWAP`.
- **Rounding.** Floors throughout (accumulator, per cost unit, claim, fee): a member can
  lose up to about 1 to 2 QU against the exact fraction at integer boundaries, never an
  overpay; the dust stays unowned in the balance. Running-total cost rounding can put
  one member's cost 1 QU above its exact value and another's below it, depending on
  roster order; the costs still add up exactly.
- **The resting part of a `BUY_QX`** (`price × unfilled shares`) counts as spent and
  stays locked in QX until the bid fills; there is no cancel path (owner decision D4 asks for a vote-gated one; not built yet).

**Membership**

- **The seats of an IPO holding cannot be traded until it is settled (step K6, audit C055).** `SettleIpo` is permissionless and can be called from the epoch after the auction, so the wait is short, but a member
  cannot sell his IPO seat while the auction runs; the alternative (rebasing every offer when `SettleIpo` shrinks the costs) needs a loop over the offers or a second number in each. **An offer on a holding that retires after it
  was made stays in the table, and counts in its pool's quota of 32, until its seller cancels it or claims his seat** (nobody can fill it).
- **Roster slots are never reclaimed.** A pool has 256 roster slots for its whole life, also for members who have sold
  every seat and withdrawn every QU; there is no leave-pool call. Flooding a pool with identities now costs one join fee
  each, by every route a newcomer can take (256 x 10,000 QU = 2.56 million QU for a full roster, owner decision D8), but it can still be done by someone willing to pay, and
  it then closes the pool to newcomers (`CAPACITY`) for good. A reclaim path needs a per-member seat counter to stay O(1)
  (`memberIdx` already gives the roster position for a swap-remove) and was not built.
- **The join fee is paid by the person who joins, not by the seller:** a seat seller cannot make a buyer a member for free,
  but a pool member cannot be removed either, however little he holds. A deposit joins nobody but the pool's creator (owner decision D16, step K5).

**Shared tables and quotas (step K5; README 4b)**

- **Every shared table has a per-pool quota except the vote rows.** Offers (32 per pool), allow-list entries (256), seat rows (512), deposits (32) and unsettled IPO
  holdings (8) cannot be used up by one pool at the expense of another. The `votes` table (524,288 rows, one per voter and open proposal) has none: a
  pool of 256 members with 256 open proposals that all vote holds 65,536 rows, so about eight such pools fill it, and a full vote table refuses `Vote`
  (`CAPACITY`, nothing written) in EVERY pool until proposals end. At the new fees that costs roughly 8 x 10,000,000 (pools) + 2,048 x 20,000 (identities that join and keep
  the minimum) = about 120 million QU and half a million transactions. A per-pool vote quota would stop it but would cap a large pool's parallel governance (512 rows is two fully voted proposals
  of a 256-member roster); that is the launch-profile decision (D6) and was not taken here. Exits that need no vote (`WithdrawUncommitted`, `ClaimPayout`, `OfferPosition`, `BuyPosition`) keep working.
- **The table of unsettled IPO holdings (2,048) is not pools × quota.** The owner sized it for a handful of active IPOs; the quota of 8 per pool means it takes 256 pools (2.56 billion QU
  of registration fees, plus each holding's budget, which comes back at settlement) to fill it and refuse every other pool's IPO bid with `CAPACITY`.
- **A pool's seat-row budget (512) is the table size divided by the pools, and a large roster uses it fast:** two purchases with a 256-member roster that all votes yes. Rows come back
  as seats empty, as a failed IPO's holding is settled and as the members of a retired holding claim, but a pool that wants many purchases with a big roster needs a bigger table (D6).
- **The creator can be locked out of depositing into his own PUBLIC pool.** `DepositHoldingAsset` puts the creator on the roster if he is not on it, and the roster holds 256 members. If strangers fill it first
  (256 x 20,000 QU: 2.56 million QU of join fees, the rest contributions they can withdraw) his deposit is refused with `CAPACITY`, before the shares move, and `SetPermissioned` does not help (the roster is full, not the allow-list). He can neither deposit nor close the pool.
  Deposit first, or create the pool private from the start (`permissioned = 1`: only the addresses the creator admits can join, so strangers cannot fill the roster). Reserving a roster place for the creator would change the 256 members of every pool and the position
  of the first member (the harness and clients read `members[pool * 256]`), so it was not built; it is the owner's decision (D16).
- **A sold-out holding frees its slot only when its last member has claimed.** A member who never claims (a lost key, a dust claim nobody bothers with) keeps that one slot of the pool's 128
  occupied; his row is his claim and nobody else can remove it. `ClaimPayout` of a retired holding with nothing owed removes the row and returns OK with 0 paid.
- **Offers are not expired.** A pool's 32 offer places can be taken by 32 sock puppets (each pays the join fee and buys a seat) who offer one unit at an unbuyable price; the pool's own members can then not offer
  until a puppet cancels (nobody else can cancel his offer). The damage is that pool's alone; it costs about 32 x 10,000 QU plus the seats. A listing bond or a lazy expiry was not built.

**Dividends**

- **Attribution is by sender.** A treasury or distributor wallet, QUtil `SendToMany`,
  or a contract that pays its shareholders by `qpi.transfer` is not recognised. A plain
  transfer from the issuer of a registered token *is* taken as a dividend whatever it
  was meant for.
- **One payer, one asset.** A second asset of a bound payer is refused while the
  first is held. A pool can hold a cheap real sibling asset of an issuer, or a QX bid
  that never fills, and keep the issuer's other assets out of QPOOL. It blocks, it does
  not steal; it costs a pool (10,000,000 QU) and a stuck order. Since K5 a DEPOSITED sibling that never paid can be given up by
  anybody who owns another asset of the issuer, four epochs after it was deposited (`EvictSquat`, 4d); a binding made by a vote (a bought holding) or one that has paid cannot,
  and a QX bid that never fills still has no cancel path. A binding made by a pool's own `BUY_QX` of a dust share is likewise out of reach of `EvictSquat` (QX manages the shares). An explicit payer field on the proposal would remove the rule.
- **IPO dividends wait for settlement.** If nobody calls `SettleIpo`, that contract's
  dividends and its seat trades stay parked. The escrow is not lost: it is released the
  moment the last IPO holding settles, or swept to revenue if the source empties. A
  holding whose `SettleIpo` returns `NAME_UNPROVEN` cannot be settled by anyone, so it
  keeps its ledger's dividends parked for as long as its name is unproven (section 4d: no
  rename or abandon path was built).
- **The snapshot depends on QPOOL's END_EPOCH running in the target's IPO epoch.** The core
  skips a contract's END_EPOCH while the contract is in an error state; then the epoch's IPO
  holdings cannot be settled (`IPO_NO_SNAPSHOT`) and their capital stays on the books.
  Conversely, if the core's `finishIPOs()` cannot issue the shares (the share name is already
  an issued asset) it fails the IPO although the snapshot saw a full book: the holdings with
  seats then return `NAME_UNPROVEN` (no shares exist) and the refund of the whole bid, which
  the core pays, is larger than the books expect; the difference stays in the pot unowned.
  Neither is reachable with the core as read.
- **Rebidding is bounded by the budget and nothing else (steps KI and KI2).** A seat owner, however small his seat, can spend the holding's free funds on
  rebids at any price they pay for (above the book's minimum, and not another holding's price): the vote approved a budget, not a single bid, and there is
  no ceiling price (the owner's rule: a pool cannot know how high an auction will go). Free funds are the rest of the budget that does not buy a seat at the opening
  price, plus what has been outbid and come back; a budget that is a multiple of the opening price leaves none until something comes back. Under the uniform price every
  winner pays the clearing price, so a high bid costs nothing unless the book itself rises that far; an outbid stake is only locked in the book while the slot is held.
- **A rebid is funded only by money that has already come back, so a pool recovers step by step, and fewer seats than it lost when the price has risen (step KI2).**
  A bid of n slots evicts the n lowest slots of a full book, whoever owns them, and the core pays the refunds only after it has taken the whole cost, so the refunds
  of the pool's own pushed-out seats cannot pay for that bid and are not counted (they fund the next one). Restoring seats that sit at the very bottom of the book
  therefore takes several rebids, each at a price the money that came back can pay for; the seats the budget can hold shrink as the price rises (200,000 holds 100 seats at 2,000
  and 95 at 2,101). Reusing a price the holding already has keeps the list of live prices (16 at most) from filling: a list of 16 distinct prices that all still have seats refuses
  a further NEW price with `CAPACITY`, and the pool bids at a price it has. This is the limit most likely to matter in the last minutes: see the 43-rebid test
  (`Ki2AnOutbidPoolRebidsCallAfterCall...`), the five-rebid half hour and the ten-rebid epoch.
- **The refund pot clamps, it does not invent.** A rebid or a settlement draws no more than the pot holds. In a state where a refund never reached the
  hook (the core's own refund always does) the outbid stake that the pot cannot cover stays booked as cost and the members are credited only what has
  arrived (`KiARebidSpendsOnlyWhatHasComeBack...`).
- **Ticks are nominal.** The 24-hour vote of a `BUY_IPO` is 86,400 ticks; at a network speed other than one tick per second it is another length in real time.
- **At most 8 IPO targets with unsettled holdings at one time.** A bid in a ninth is refused
  with `CAPACITY`; a stuck holding (above) keeps its target's row until it settles. A target whose row still has its
  snapshot (an earlier IPO of the same contract index with unsettled holdings) takes no new bid (`IPO_REJECTED`) until
  those holdings are settled.
- **Arrears follow today's holders.** QPOOL knows who holds shares only when the QU
  arrives. A dividend declared for a period before a sale but paid after it goes to the
  holdings registered at that moment, or to `globalUnattributed` if there is none.
- **Strays versus fills.** While a QX bid is unreconciled, shares donated to QPOOL look
  like its fills and are claimable by that holding up to the quantity it asked for.
- **Payer table soft cap.** No new payer above 12,288 of 16,384 (75%); retired holdings
  free their slot at END_EPOCH.
- **A listed contract's table row is network-specific** (4d); a wrong row refuses the
  genuine pair and accepts the row's pair.

**Contract shares and IPOs** (details in 4d)

- Above index 30 the name ↔ index link cannot be proved on chain. A wrong index
  entered by a proposer earns nothing for the pool and keeps the real pair out until
  that holding is sold; the vote is the only check.
- A squatter (10,000,000 QU plus one genuine share) can deny a deployed contract's
  index and another contract's name while their holding exists. `EvictSquat` ends it only when that contract is in its IPO epoch (4d); after the
  IPO the claim cannot be proved wrong on chain.
- IPO late binding: with overlapping IPO windows of two UNLISTED contracts, a pool that
  declares the other contract's name and settles first can still bind the other contract's
  name when QPOOL holds enough shares of it (4d). K4 stops the old consequence (honest holdings
  retired with their capital stranded) but not the binding. *Owner decision* (add contracts
  to the embedded table before their IPO, or reserve declared names at registration; the
  latter trades the theft for a denial of service at `Propose`). Step KX showed the consequence on two real
  books that end in the same epoch (`KxADeclaredNameThatBelongsToTheOtherSimultaneousIpo...`): the honest holdings of both IPOs are
  `NAME_UNPROVEN` for good, their dividends stay parked, and each of the two targets keeps one of the 8 rows of the table. With the
  rename/abandon path (K4b, not built yet) a pool could get out; until then the only exact cure is the embedded table.
- The "contract exists" check cannot tell a contract past its IPO from one whose IPO
  has not started yet; it only excludes unlisted indices and a contract mid-IPO.

**QX**

- **One unreconciled QX bid per payer, and no cancel path.** A bid that never fills (a
  lowball bid) blocks further `BUY_QX` on that payer until it fills.
- **`SELL_QX`**:
  - `quAmount` is a floor on what the *visible* book pays now, net of QX's fee, before
    QPOOL's 1% and any hand-over fees; the credited amount can be lower. `Execute` is
    permissionless, so the caller picks the moment; the limit price is what protects
    each share.
  - Only the first 256 bids are read. A wallet holding 256 small bids above the real
    market can make the visible book fall short of `quAmount` and delay a sale (it
    cannot take money: any fill is at their high prices, and the proposal can be retried
    or re-proposed with a smaller minimum). Deeper liquidity is not reached in one
    `Execute`; the rest needs a new proposal.
  - A partly filled sale consumes the mandate.
  - The 100 QU management-rights fee is a constant, assumed unchanged since QX epoch 138;
    if QX changes it the hand-over fails safely (`TRANSFER_FAILED`) before anything is
    sold, or the sale returns `NO_FILL`. QX's trade fee is read live.
  - Selling into the pool's own resting bid is allowed and consistent, but costs QX's
    0.3% and moves shares between two holdings at the bid's price.
  - If the hand-over succeeds and QX then refuses the ask (unreachable after the
    pre-checks), the fee is lost to QX and charged to `pendingRevenue`.
  - Two paths cannot be tested without fault injection (QX not answering right after it
    accepted an order; the cancel failing after a sale).

**Buys**

- A payment from QX during a BUY is read as a refund whether it is one or not. One kind
  could be misread: QX paying QPOOL as the *seller* if the bid crosses a resting ask of
  QPOOL's own. `SELL_QX` cancels the unfilled rest of every sale, so no such ask exists
  unless one is planted by hand; a planted one would put the seller's proceeds into
  the buyers' refund (capped at the amount assigned).
- The branch where QX or QSWAP keeps QU but reports zero shares is not handled. The
  real contracts refund the whole reward on every rejection path (read from their code);
  if one ever did not, the pool would be short by the kept amount.
- The two refund measures (balance and hook) are never exercised separately by a test:
  two mutants that ignore one of them survive. Taking the smaller is a defensive choice.

**Cost, operations and the lazy-sync discipline**

- **Execution fees are paid by the owner** and most procedures are free to call
  (section 2a); a flood of permissionless calls (`Execute`, `Tally`, `ReconcileHolding`,
  `SettleIpo`, refused `Propose`...) costs the reserve execution time, unbounded in
  number, and QPOOL charges nothing to stop it.
- **A transfer from any unregistered sender dirties the state** (the hook writes
  `globalUnattributed`), so anyone can force a whole-state re-hash in a tick for the price
  of a few QU, which they lose.
- **A rebid the cheap bound lets through pays for the 676-slot pass and may still be refused after it** (`INSUFFICIENT_BALANCE`, `CAPACITY`,
  `BOOKS_INCONSISTENT`; section 7a). Nothing is written and the reward comes back, but the pass was executed work, paid from the execution fee
  reserve, and a seat owner can repeat it: the same lever as the pass of a successful rebid, which is why the cost of a rebid loop is on the list for
  the fee hardening (step K9). (Before step K6 a seat of an unsettled IPO holding could be bought, so a stranger could repeat it; it cannot now: the callers are the members whose money the vote committed.)
- **Hot loops remain** and dominate execution time: `Propose` scans 262,144 proposal
  slots. A per-pool proposal index would lower the fees the owner pays; it was not built (step K9). The free-slot cursor
  (`holdingFreeHint`, `poolFreeHint`) is: `Execute` and `DepositHoldingAsset` no longer scan the holdings from index 0.
- **Refusals that still dirty the state:** since step K6 only one in the seat market, and it removes something: `BuyPosition` that finds the offer's seller no longer owns the parcel (a state the
  contract no longer makes: a seat shrinks only by a sale, which consumes the offer, or by `SettleIpo`, which the offers of an unsettled holding cannot meet) drops the stale offer and gives its place back,
  and refunds the buyer. Every other refusal of `BuyPosition`, `OfferPosition` and `ClaimPayout` writes nothing (section 2a). (`SetAdmission` no longer writes when it changes nothing, K5; `SetPermissioned` neither, K6.)
  The refusals that remain elsewhere are the ones listed by the cluster that owns them (the hook, step K8; the failing `Execute` retries and the rebid pass, step K9).
- **The hook's universe read** (only for a ledger with an unreconciled QX bid) relies on
  `AssetPossessionIterator` taking no lock, which the core's own `distributeDividends`
  also relies on. The public-function deadlock rule is read from the core's source, not
  executed (the hook rule *was* executed: the negative control hangs). Add the CI grep of
  section 3d.
- **Lazy-sync discipline.** Correctness depends on every writer of `shares`,
  `pendingShares` or `costBasis` syncing first and only two helpers writing
  `Holding.shares`. The solvency clamp bounds a missed sync to a misallocation within one
  source, never to insolvency, and the randomised invariant tests assert it, but a future
  edit can still break fairness silently. Keep the helper-banner comment and those tests.

**Governance and behaviour you may not expect**

- `Execute` is permissionless: the executor picks the moment inside the window.
- A vote can be spent on a mandate that cannot run (4a).
- A PASSED proposal that is never executed blocks its proposer only until its life is over (5 days after it opened);
  a YES voter's money stays locked until the mandate is carried out or that day.
- A deposit counts on a buy only if it is at least a tick older than the proposal; a refund that `SettleIpo` hands back (anyone may call it) counts as a deposit.
- A voted seat of a SELL or ALLOCATE cannot be offered or sold for up to 3 days.
- A NO voter's money is locked until `Tally`; a YES voter's until `Execute` or the 5th day.
- `Vote`, `Tally` and `Execute` name a proposal by its slot: a transaction built for one proposal can land on a later one that took the slot.
- `Permissioned` is access control, not confidentiality (section 6d).

**Verification gaps**

- The 263 tests ran in a substitute harness (section 5), not the MSVC build, a node or a
  devnet; the previous reports do not cover this revision.
- `docs/QUBIC-CONTRACT-GUIDE.md` has **not** been updated with the rules learned here
  (the universe-lock and public-function deadlock rules; callbacks have no execution
  limit, so keep hooks O(1); `CALL` zeroes the callee's locals but not the caller's
  output struct; `PRIVATE_*` macros open a protected section so helper I/O structs must
  precede them; refund the reward exactly once; never mutate state before the last fallible
  external call; soft-cap hash tables probed by untrusted keys).
