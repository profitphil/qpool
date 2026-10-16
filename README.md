# QPOOL — testnet package v1

Vote-governed asset pooling for Qubic. Members contribute QU, the pool acquires
dividend-paying assets by vote (QSWAP swap, QX order, or contract IPO bid),
disposes of them by vote, and distributes dividends and sale proceeds pro-rata.
There is no admin key anywhere in the contract.

| File | Goes to | What it is |
|---|---|---|
| `contracts/QPool.h` | `src/contracts/` | The contract — 2,117 lines, 17 procedures, 6 functions |
| `test/contract_qpool.cpp` | `test/` | 22 gtest cases |

State size: **6,515,664 bytes** (~6.2 MB), well inside the 1 GiB cap.
Verified: compiles with zero errors; the test file compiles clean via two
independent flag routes (see section 5).

---

## 1. The ownership model (read this first — everything follows from it)

Contributions sit as **uncommitted** QU until a passed proposal deploys them. At
that moment each contributor's share of the deployed QU is written as their
permanent **cost** in that specific holding.

`Holding.costBasis` is an **ownership denominator, never a valuation.** It is
fixed for the holding's life.

Three consequences worth internalising:

- **Later contributions cannot dilute an existing holding.** They fund only
  later acquisitions. No NAV, no oracle, no asset pricing is ever needed to
  decide who is owed what.
- **Dividends and sale proceeds use one mechanism.** Both are QU arriving
  against a holding, so both advance the same `payoutPerCostX64` accumulator.
  Distribution never loops over members.
- **Partial sales disturb nobody's fraction,** because shares can fall while
  costBasis stays put.

For a **deposited** asset (syndication, section 6) `costBasis` instead means
"seat units" declared by the depositor, and the holding carries `deposited = 1`
and is excluded from `pool.committedTotal`.

---

## 2. Fees

| Fee | Amount | Notes |
|---|---|---|
| Pool registration | 5,000,000 QU | Matches MSVAULT's vault-registration fee |
| Proposing | **free** | See below |
| Management (running) | **2% of every payout** | Dividends, sale proceeds, allocations |
| Revenue split | 80% shareholders / 20% burn | At END_EPOCH via `distributeDividends` |

**Proposing is free** because the 2% already earns from pools that perform, and
taxing governance is self-defeating when governance is the mechanism. Spam is
held off structurally instead — **one live proposal per member per pool**, the
same rule QPI's own `ProposalVoting` uses. Without it, one member could fill all
256 proposal slots and freeze governance for every pool.

**There is deliberately no flat per-epoch fee.** MSVAULT charges one, and when a
vault cannot pay, its code wipes the vault — balance seized, owners cleared —
and its own comments admit assets get stuck needing "manual patches". Applied
here that would destroy member-owned holdings over an unpaid fee. It would also
break the invariant `sum(member.uncommitted) == pool.uncommittedTotal` unless
every member were debited pro-rata each epoch. A percentage of payouts has none
of these failure modes and only earns when a pool earns.

The burn share is not only tokenomics: `qpi.burn` credits the contract's
execution fee reserve, which QPOOL needs to keep running.

---

## 3. Integration

Two edits in `src/contract_core/contract_def.h`, one in `test/CMakeLists.txt`.

**Pick the index first.** Take the next free index. On a tree topping out at
`QTREAT = 29` that is `QPOOL = 30`; on `upstream/develop`, where QTREAT is 30,
it would be 31. Substitute your number throughout.

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

Set the real construction epoch before mainnet — see section 7a.

### 3c. Test wiring

Add `contract_qpool.cpp` to the `add_executable(qubic_core_tests ...)` list.

---

## 4. Entry points

**Procedures** — `CreatePool`(1), `Contribute`(2), `WithdrawUncommitted`(3),
`Propose`(4), `Vote`(5), `Tally`(6), `Execute`(7), `ReconcileHolding`(8),
`ClaimPayout`(9), `OfferPosition`(10), `CancelOffer`(11), `BuyPosition`(12),
`RebidIpo`(13), `SettleIpo`(14), `ReconcileIpo`(15), `DepositHoldingAsset`(16),
`SetAdmission`(17).

**Functions** — `GetPool`(1), `GetMember`(2), `GetHolding`(3), `GetPosition`(4),
`GetProposal`(5), `GetStats`(6).

`Tally`, `Execute`, `ReconcileHolding`, `ReconcileIpo` and `SettleIpo` are all
**permissionless** — the mandate comes from the vote, not the caller.

Proposal kinds: `1` BUY_QSWAP, `2` BUY_QX, `3` BUY_IPO, `4` SELL_QSWAP,
`5` SELL_QX, `6` ALLOCATE (assign unattributed QU to a holding).

Vote weight is **kind-dependent**: buys weight by *uncommitted QU* (the money
being spent), sells and allocations by *cost basis in the target holding* (the
people who own the thing being sold). A fully-committed member has no vote on
new buys, which is correct — they are not funding it.

---

## 5. Stage 1 — unit tests

```bash
cd core
cmake -B build -DENABLE_AVX512=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target qubic_core_tests -j
./build/test/qubic_core_tests --gtest_filter='TestQPool.*'
```

Linux is the supported platform. **macOS cannot build the test binary** — the
platform layer assumes Windows' 4-byte `long`
(`static_assert(sizeof(long) == 4)` in `platform/concurrency.h`) plus MSVC
intrinsics. The contract itself syntax-checks on macOS; the harness does not.

> **A verification note worth repeating.** Compiling the test with
> `-fms-compatibility` reported zero errors in my code — but that flag also
> breaks the macOS SDK's `char16_t`, and 139 lines of resulting SDK noise were
> *masking six real errors of mine*. Always count errors per file rather than
> trusting an empty grep. The two routes now agree at zero.

The two invariants that matter most each have a dedicated test:

- `LaterContributorDoesNotDiluteExistingHolding` — a new member joins with 5x
  the existing holding's entire cost basis; existing payouts are unchanged.
- `SeatSaleDoesNotStealAlreadyEarnedPayouts` — a seat sold *after* a dividend
  accrues leaves the earned amount with the seller and starts the buyer at zero.

**Three tests build state directly** (`fabricateHolding` and the two IPO cases)
because the real acquisition paths need a live QSWAP pool or an actual IPO epoch
in the harness. Payout maths, the seat market and IPO settlement are genuinely
tested; execution against QSWAP is not. That gap needs an integration test.

Expect some arithmetic assertions to need nudging on the first run — the
rounding figures are hand-computed, not machine-verified.

---

## 6. Stage 2 — testnet

Qubic testnet is EFI bare metal with multiple nodes, supported by the test team
(lead: kavatak) via the **#test-net channel on Discord**. Coordinate there
first. Branch convention per `doc/contributing.md`:
`testnet/YYYY-MM-DD-QPOOL` off `develop`.

Watch these on the nodes (from `test/README.md`):

- **[High]** No misalignment on `computerDigest` / `spectrumDigest` /
  `universeDigest` — `md5sum` the end-of-epoch files across nodes.
- **[High]** Stack usage — press **F2**. `ReconcileIpo` scans all 676 IPO slots
  with two cross-boundary reads each and is the heaviest call in the contract.
  `Execute` also loops the pool's members (up to 256).
- **[Medium]** `#define ADDON_TX_STATUS_REQUEST 1` to verify tx processing;
  read transfer/burn logs with [qlogging](https://github.com/qubic/qlogging).

### 6a. Core pooling flow

1. `CreatePool(name, permissioned=0)` — 5,000,000 QU.
2. `Contribute` from 3+ wallets. Check `GetMember` and `GetPool.uncommittedTotal`.
3. `WithdrawUncommitted` part of one — confirm undeployed capital is returnable.
4. `Propose(BUY_QSWAP, asset, quAmount, shares=minSharesOut, 0, 0)`.
   **`shares` is mandatory** — it is the slippage bound, and a zero min-out is
   sandwichable, so the contract rejects it.
5. `Vote` from members, wait out the 3-day window, `Tally`, then `Execute`
   (any wallet). Confirm `GetHolding` cost basis equals QU actually deployed and
   each member's `GetPosition.cost` is their pro-rata share.
6. Let a dividend arrive, then `ClaimPayout` per member. Verify the 2% skim in
   `GetStats.totalManagementFees`.
7. `Propose(SELL_QSWAP, ..., shares, quAmount=minProceeds)` — **`quAmount` is
   mandatory** for the same slippage reason. Confirm proceeds distribute through
   the same accumulator and cost basis is untouched.

### 6b. Seat market / buy-outs

8. `OfferPosition(holdingId, costUnits, askPrice)`, then `BuyPosition` from
   another wallet. QU goes to the **seller**, not the pool; the asset never
   moves. Confirm both sides' earned payouts are preserved across the transfer.

### 6c. Syndication

9. Move an asset you own under QPOOL's management (QX
   `TransferShareManagementRights`), then
   `DepositHoldingAsset(poolId, asset, amount, seatUnits)`. You receive **every
   seat**; sell fractions via `OfferPosition`. `seatUnits` is a granularity
   choice, not a valuation.

### 6d. Permissioned pools

10. `CreatePool(name, permissioned=1)`, then `SetAdmission` (creator only).
    Verify all three gates: `Contribute`, `BuyPosition` and
    `DepositHoldingAsset` all refuse unadmitted addresses. Revoking blocks new
    capital but **cannot** claw back seats already owned.

> **Permissioned is access control, NOT confidentiality.** Every byte of
> contract state is replicated to every node, so member addresses,
> contributions and holdings are public regardless. A private pool controls who
> may *join*; it cannot hide who has joined. `Pool.admissionRoot` is reserved
> for a future ZK membership set (see section 8) and is unused today.

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

**2. IPO bids cannot fire from a callback.** `bidInIPO` returns -1 when
`contractCallbacksRunning != NoContractCallback`, so END_EPOCH is out. All
execution is by permissionless user procedure.

**3. Bids are only valid during `constructionEpoch - 1`** and settle at epoch
end, so shares arrive asynchronously.

### 7a. The re-bid cycle

IPO auctions are competitive and a 3-day vote cannot keep up, so **the vote
approves a price ceiling, not a single bid**:

- `Propose(BUY_IPO)` commits the whole budget once, fixes ownership, and sets
  `ipoMaxPrice`. `holdingId` carries the **target contract index**; `asset` must
  be `{NULL_ID, shareAssetName}` because contract shares are protocol-issued and
  contracts cannot read `contractDescriptions` (it is defined after the contract
  includes, specifically to keep it out of reach).
- `RebidIpo(holdingId, price, quantity)` — **permissionless, no new vote**, up
  to the ceiling. Anyone may defend the position as the auction moves.
- Being outbid fires `TransferType::ipoBidRefund` (source `NULL_ID`). Refunds
  pool in `ipoRefundEscrow` and are **not** attributed on arrival.
- `ReconcileIpo(holdingId)` reads the live book via `qpi.ipoBidId` /
  `qpi.ipoBidPrice`, counts slots still held by SELF **at each of this
  holding's live prices**, and moves anything no longer seated back to
  `ipoAvailable` — ready to re-bid.
- `SettleIpo(holdingId)` closes out after the auction: reconciles shares won and
  returns unspent QU pro-rata, shrinking each member's cost in step so
  `costBasis` always equals real spend. Won nothing → the holding retires and
  everyone's capital returns to `uncommitted`, free for the next attempt.

**Distinct prices are mandatory, not cosmetic.** Attribution works because slots
at price P belong to the bid placed at price P. `Propose` rejects a price already
live on the same IPO and `RebidIpo` requires a strictly higher one. Up to **8
live prices per holding**; beyond that `RebidIpo` returns `ERR_CAPACITY`.

**The book is only readable during the IPO epoch** (`ipoBidId` returns `NULL_ID`,
`ipoBidPrice` returns `-2` outside it). Use `ReconcileIpo` during the auction and
`SettleIpo` afterwards; `ReconcileIpo` detects the negative return and tells you
to switch.

---

## 8. Before mainnet

1. **Set a real construction epoch** (currently placeholder `236`).
2. **Choose the index deliberately** — section 7, constraint 1.
3. **Unattributed QU.** `POST_INCOMING_TRANSFER` sees only `{sourceId, amount}`,
   so payers are mapped to holdings at acquisition time. Anything unrecognised
   parks in `pool.unattributedQu` and needs a `KIND_ALLOCATE` vote. Nothing is
   lost, but if two holdings share an issuer the first registered wins and the
   rest need manual allocation.
4. **Integration test against QSWAP** for `Execute`'s buy and sell paths — the
   only procedures making real external calls, and the ones the unit tests
   cannot reach.
5. **Audit.** No admin key is a real strength here; the areas to focus review on
   are the ownership loop in `Execute`, the accumulator arithmetic, and IPO
   price attribution.
6. **On the planned BN254+Groth16 verifier:** the realistic win is unlinkable
   *membership* — prove inclusion in an admitted set without revealing which
   member. Amounts will not hide for free (pro-rata needs the aggregate cost
   basis public; hiding individual amounts needs Pedersen commitments plus range
   proofs), and `qpi.transfer` always writes a public record, so you get
   unlinkability rather than invisible amounts. The primitive to ask the core
   team for is **Merkle inclusion against a contract-held root plus a nullifier
   set** — that one pattern covers private membership and private claims.
   `Pool.admissionRoot` is reserved now because changing `StateData`'s size
   later forces a state migration.
