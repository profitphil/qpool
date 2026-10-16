# Qubic Smart Contract Builder's Guide

The playbook for writing a production-quality Qubic C++ contract file. Built from (1) the
line-by-line delta between QTREAT-1.h (draft) and QTREAT-2.h (core-dev-corrected version), and
(2) analysis of the full contract corpus in `core/src/contracts/` (35 contracts + `qpi.h` +
`README.md`). Companion file: `NOTES-qtreat-diff.md` (the raw diff study).

---

## 1. The environment: what a contract IS

- One `.h` file per contract in `core/src/contracts/`, filename starts with a capital letter.
  `qpi.h` is auto-available; **no `#include` allowed** — everything lives in the single file.
- The file opens with `using namespace QPI;` then file-scope `constexpr` constants, then a
  secondary empty struct `NAME2 {}` (reserved for EXPAND/migration), then
  `struct NAME : public ContractBase { ... };`.
- The node build defines `CONTRACT_INDEX`, `CONTRACT_STATE_TYPE` before including the file
  (see `contract_core/contract_def.h`). New contracts get the next index; description entry
  `{"NAME", constructionEpoch, destructionEpoch, sizeof(NAME::StateData)}` with the comment
  convention "proposal in epoch N, IPO in N+1, construction and first use in N+2".
- State is capped at 1 GiB. All persistent data goes in nested `struct StateData`.
- Functions = read-only, callable via network message. Procedures = state-mutating, invoked by
  transactions (carry an `invocationReward`). System hooks (INITIALIZE, BEGIN/END_EPOCH,
  BEGIN/END_TICK, PRE/POST_ACQUIRE_SHARES, PRE/POST_RELEASE_SHARES, POST_INCOMING_TRANSFER,
  EXPAND) are called by the node.
- Procedures may call procedures/functions of contracts with a **lower** contract index only.
- **Time.** There is no wall clock you should trust for logic. `qpi.epoch()` advances ~weekly;
  `qpi.tick()` advances ~1/second (`TARGET_TICK_DURATION` is 1000 ms in `public_settings.h`), so
  sub-epoch windows (an hour, 48 hours, 7 days) are expressed in ticks and epoch-scale ones in
  epochs. `qpi.year()/month()/day()/hour()/...` exist but tie behaviour to real dates — avoid
  them for anything consensus-visible.

## 2. Hard language rules (build fails or consensus breaks if violated)

Prohibited (from `README.md` + `qpi.h` character scan):
- `[` `]` — no C arrays, no indexing. Use `Array<T,N>`, `HashMap`, `HashSet`, `Collection`,
  `BitArray`.
- Pointers, casts of pointers, `typedef`, `union`, `double`, `float`, `#`(no includes/defines),
  `"` and `'` (no string/char literals), `%` and `/` as operators (use `mod()` / `div()` from
  qpi.h — both return 0 when the divisor is 0), `__` (double underscore), `const_cast`,
  `QpiContext`.
- Native types (`int`, `char`, `long`) — use `uint8..uint64`, `sint8..sint64`, `bit`, `uint128`,
  `id` (= m256i).
- **No local variable declarations anywhere** — not even loop counters. Every scratch variable
  lives in a `<Name>_locals` struct used with the `_WITH_LOCALS` macro variants, or in StateData.
- Allowed operators: `+ - *`, bitwise, comparisons. Safe helpers: `sadd(a,b)`, `smul(a,b)`
  (clamping), `div(a,b)`, `mod(a,b)`. For pro-rata math use `uint128` intermediates:
  `div((uint128)amount * (uint128)weight, (uint128)totalWeight).low`.
- Strings are built from `Ch::` enums or the `ID(_A,_B,...)` 56-letter constructor
  (last 4 chars of a 60-char identity are checksum — omit them). Asset names are uint64
  little-endian packed ASCII (e.g. `1347897159ULL` == "GGWP").
- `STATIC_ASSERT` / `static_assert` freely for constant relationships (corpus does this a lot).
- **Hard size limits** (framework static_asserts fire at compile time):
  `sizeof(<proc>_input) <= MAX_INPUT_SIZE` (**1024 bytes** — a transaction payload cap from
  `network_messages/common_def.h`), `sizeof(<name>_locals) <= MAX_SIZE_OF_CONTRACT_LOCALS`
  (32 KiB), state <= 1 GiB. An `Array<id, 32>` (1024 B) plus anything else already breaks the
  input cap — for multi-recipient inputs use the QUTIL SendToManyV1 idiom: N **named** `id`
  fields in the input (25 ids = 800 B), copied once into a locals `Array` for looping.

## 3. Canonical file layout (order matters)

```
using namespace QPI;
// 1. file-scope constexpr constants (capacities MUST be powers of two for HashMap/HashSet)
// 2. return-code constants (NAME_OK = 0, NAME_ERR_* = 1..n — append-only, never renumber)
// 3. struct NAME2 {};
// 4. struct NAME : public ContractBase {
//    a. nested plain structs (records, keys — give map-key structs operator== and !=)
//    b. struct StateData { ... };          // ALL persistent fields
//    c. ALL _input/_output/_locals structs // BEFORE any procedure body that uses them
//    d. PUBLIC_FUNCTION[_WITH_LOCALS] bodies (read-only getters)
//    e. PUBLIC_PROCEDURE[_WITH_LOCALS] bodies
//    f. REGISTER_USER_FUNCTIONS_AND_PROCEDURES { REGISTER_USER_FUNCTION(Foo, 1); ... }
//    g. INITIALIZE
//    h. POST_INCOMING_TRANSFER, BEGIN_EPOCH, END_EPOCH, PRE/POST_ACQUIRE_SHARES, ...
// };
```
The core dev explicitly moved procedure bodies that had been interleaved into the struct block
down into section (e). Registration ids start at 1 and are stable ABI — clients depend on them.

State access: `state.get().field` for reads, `state.mut().field` for writes (`mut()` marks the
state dirty for digest recomputation). Never hold references across calls; read → modify local →
`state.mut().map.set(key, value)` write-back.

Comment style: the corpus is mixed (GGWP is heavily commented; the core dev stripped QTREAT-2
nearly bare). Safe rule: keep comments short and load-bearing only — external-call hazards,
accepted trade-offs ("Fallback seed is public and grindable without RANDOM"), deploy-time markers
("DEV TESTNET"), and asset-name value annotations. No banner headers or per-field narration in
the reviewed file.

## 4. QPI API cheat sheet

Context (functions + procedures): `qpi.invocator()`, `qpi.originator()`,
`qpi.invocationReward()`, `qpi.epoch()`, `qpi.tick()`, `qpi.getEntity(id, Entity&)`,
`qpi.numberOfShares(asset, ownershipSelect, possessionSelect)`,
`qpi.numberOfPossessedShares(assetName, issuer, owner, possessor, mgmtIdx, mgmtIdx)`,
`qpi.isAssetIssued(...)`, `qpi.K12(struct)`, `qpi.year/month/day/hour/...`, `qpi.now()`,
`qpi.getPrevSpectrumDigest() / getPrevUniverseDigest() / getPrevComputerDigest()`,
`qpi.queryFeeReserve(contractIndex)`, `qpi.signatureValidity(...)`.

Procedures only: `qpi.transfer(dest, amount)` (QU; range 0..1e15),
`qpi.transferShareOwnershipAndPossession(assetName, issuer, owner, possessor, n, newOwner)`,
`qpi.acquireShares(...)` / `qpi.releaseShares(asset, owner, possessor, n, ownMgmtIdx,
destMgmtIdx, offeredFee)`, `qpi.distributeDividends(amountPerShare)` (pays 676×amountPerShare to
IPO shareholders; returns bool), `qpi.issueAsset(...)`, `qpi.burn(amount)`, `qpi.bidInIPO(...)`.

Cross-contract: `CALL_OTHER_CONTRACT_FUNCTION(STATE_TYPE, func, in, out)` and
`INVOKE_OTHER_CONTRACT_PROCEDURE(STATE_TYPE, proc, in, out, rewardQU)`; both set the implicit
`interContractCallError` (enum: NoCallError, CallErrorContractInErrorState,
CallErrorInsufficientFees, CallErrorAllocationFailed, CallErrorContractInactive).
**You can only call contracts with a LOWER index than your own** (compile-time static_assert),
so check the callee's index in contract_def.h before designing around it.
**Two calls in one scope will not compile:** the plain macros declare
`InterContractCallError interContractCallError`, so a second use in the same scope is a
redefinition. Use the `_E` variants with distinct names —
`CALL_OTHER_CONTRACT_FUNCTION_E(..., myErrA)` /
`INVOKE_OTHER_CONTRACT_PROCEDURE_E(..., rewardQU, myErrB)` — and test each error var.

`qpi.issueAsset(name, issuer, decimals, shares, unitOfMeasurement)` charges **no QU fee**, and
`issuer` must be `SELF` or `qpi.invocator()` (anything else returns 0). The name is validated
strictly: first byte `A`–`Z`, later bytes `A`–`Z`/`0`–`9` or the null terminator, no non-null
byte after a null, max 7 chars (`name <= 0xFFFFFFFFFFFFFF`). Returns shares issued, **0 on
error**. Validate the name when it is first accepted from a user, not at issuance time — a bad
name discovered later can strand a flow that has already taken money.

Contract IPO shares are the asset `{NULL_ID, <contract name packed as uint64>}` (the name is
`contractDescriptions[].assetName`, max 7 chars). Use
`qpi.numberOfShares({NULL_ID, NAME}, AssetOwnershipSelect::byOwner(x), AssetPossessionSelect::byPossessor(x))`
for share-weighted shareholder voting, or the native
`DEFINE_SHAREHOLDER_PROPOSAL_TYPES(slots, assetName)` + `IMPLEMENT_SetShareholderProposal(...)`
framework (see QUtil.h) when the standard proposal semantics fit.

Oracles are real and usable: `QUERY_ORACLE(OI::Price, query, NotifyProc, timeoutMs)` (one-shot,
10 QU) and `SUBSCRIBE_ORACLE(OI::Price, query, NotifyProc, periodMs, notifyPrev)` (subscriptions
expire each epoch, so re-subscribe in BEGIN_EPOCH). The callback is a `PRIVATE_PROCEDURE` taking
`OracleNotificationInput<OI::Price>` and must be registered with
`REGISTER_USER_PROCEDURE_NOTIFICATION(NotifyProc)`. Interfaces live in `src/oracle_interfaces/`
(Price, Mock, DogeShareValidation); `OI::Price` returns an exchange-rate numerator/denominator
for a currency pair at a timestamp, so **a TWAP must be accumulated by your contract** — the
oracle only ever returns a point reading. See QUtil.h and TestExampleC.h.

Identity: `SELF` (= id of this contract), `SELF_INDEX` (= CONTRACT_INDEX), `NULL_ID`,
`NULL_INDEX` (= -1), `INVALID_AMOUNT`, `NUMBER_OF_COMPUTORS` (676).

HashMap<K,V,CAP> (CAP power of two): `population()`, `capacity()`, `contains(k)`,
`get(k, V&)`, `set(k,v)` → element index or **NULL_INDEX when full**, `removeByKey(k)`
(mark-for-removal), `nextElementIndex(idx)` iteration starting from NULL_INDEX, `key(idx)`,
`value(idx)`, `reset()`, `cleanupIfNeeded()` — call cleanup at the top of END_EPOCH for every
map that sees removals (removals only mark; cleanup compacts).

Asset iteration: `AssetPossessionIterator it; it.begin(asset); !it.reachedEnd(); it.next()` with
`it.possessor()`, `it.numberOfPossessedShares()` — this is how epoch holder snapshots are built.

## 5. Money-safety rules (each one paid for in the QTREAT review)

These are the generalized fixes the core dev made; treat every one as mandatory.

1. **Refund `invocationReward` on every early-error return.** The framework never auto-refunds.
   Pattern at the top of any error path:
   `if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());`
   For fee-charging procedures: reject if reward < fee (refund all), refund the excess above the
   exact fee, retain only the fee.
2. **Checks → effects → interactions.** Validate everything first; commit ALL state bookkeeping
   BEFORE the final outbound call; treat a trailing `releaseShares` failure as non-fatal (the
   recipient already owns the asset and can self-release). Never `return error` after an
   irreversible external effect without recording the effect — that's a double-claim bug.
3. **Check `interContractCallError != NoCallError` after EVERY cross-contract call/invoke**
   before touching the output struct — a failed call leaves the output stale/garbage. Per-site
   policy: user procedure → return a dedicated `ERR_EXTERNAL_CALL` code; epoch-loop
   verification → skip the item and never destroy user state on a transient failure; entropy
   purchase → refund the fee explicitly at the failure site and zero the stale output.
4. **Reserve → distribute → reconcile.** In payout loops: move the budget out of the fund first,
   track `distributed` as you pay, use `break` (not `return`) on balance exhaustion, then return
   `reserved - distributed` to the fund and add only `distributed` to lifetime totals.
5. **Validate post-transfer bookkeeping BEFORE the transfer.** If crediting requires a map
   insert, check `!map.contains(key) && map.population() >= CAP → reject` before moving assets.
   Same idea in PRE_ACQUIRE_SHARES: reject new stakers when the stakers map is full so
   POST_ACQUIRE can never fail to credit.
6. **Keep PRE/POST_ACQUIRE_SHARES callbacks minimal.** PRE = validate + `allowTransfer`/fee;
   POST = account exactly what arrived. **No outbound share operations (releaseShares etc.)
   inside these callbacks** — the core dev deleted exactly such a call. Prevent the bad case in
   PRE instead.
7. **Escape hatches must be invariant-scoped, not asset-banned.** Let users release any managed
   surplus above what the contract's own accounting covers
   (`managed - accountedStake >= requested`); blanket bans freeze tokens that reach the contract
   through edge paths.
8. **Update dependent state at the mutation site.** If a full unstake must reset streak/bonus
   counters, do it inside the unstake procedure — not "later in END_EPOCH" (the gap is a window).
9. **Uniform record cleanup.** Every path that can empty a record applies the same
   remove-or-keep criteria (map capacity is a hard resource; leaked empty records are a DoS).
10. **Apply eligibility/exclusion filters at every accrual site** (token snapshot AND NFT tally
    AND any other reward source), not just the primary one.
11. **Re-read the contract balance (`qpi.getEntity(SELF, ent)`;
    `balance = ent.incomingAmount - ent.outgoingAmount`) immediately before each independent
    spending phase**, and clamp every payment to the live balance.
12. **Never bootstrap admin as NULL_ID** (first caller seizes it). Hardcode the admin identity in
    INITIALIZE with a clearly-marked deploy-time `ID(...)` constant; keep testnet vs mainnet
    identity swaps loud (`// DEV TESTNET:`).
13. **Keep an execution-fee reserve.** Contracts pay execution fees from their own balance
    (`CallErrorInsufficientFees` exists; GGWP retains 1% of revenue as an exec reserve; QX fees
    prepaid into the balance are consumed by later `releaseShares`). Don't drain the balance to
    exactly zero in payouts.

## 6. Standard mechanics and where to crib them

- **Epoch dividend snapshot (anti-gaming):** BEGIN_EPOCH snapshot holders via
  AssetPossessionIterator into `beginBalances`; END_EPOCH snapshot again; weight =
  `min(begin, end)`; dust filter (`MIN_ELIGIBLE_BAL`) and admin-settable exclusion slots
  (exchange wallets). — QTREAT, GGWP, qRWA.
- **Shareholder cut:** `perShare = div(cut, NUMBER_OF_COMPUTORS)`;
  `if (perShare > 0 && qpi.distributeDividends((sint64)perShare))` then account
  `perShare * 676`; integer remainder stays in the holder pool. — QTREAT, GGWP, QVAULT, Qx.
- **Token staking custody via QX management rights:** user calls
  QX.TransferShareManagementRights(asset, amount, newManagingContractIndex = this contract);
  PRE_ACQUIRE_SHARES enforces min-stake / no-pending-unstake / capacity / program-active and
  rejects otherwise (the QX call fails, tokens stay tradable); POST_ACQUIRE_SHARES credits
  exactly `input.numberOfShares`. Unstake = request (charge the 100 QU QX fee up front) →
  N-epoch delay → auto-release in END_EPOCH with free manual finalize fallback. Staked ==
  managed at all times. — QTREAT (validated), GGWP.
- **Incoming QU routing:** POST_INCOMING_TRANSFER switches on `input.type`
  (TransferType::standardTransaction / qpiTransfer / qpiDistributeDividends / revenueDonation
  route to funds; procedureTransaction / procedureInvocationByOtherContract are accounted inside
  the invoked procedure — do NOT double count). — QTREAT, qRWA, QVAULT, Qx.
- **Randomness:** buy entropy from RANDOM (`RANDOM_BITFEE * bits`); on
  `interContractCallError` refund the fee to the paying fund and zero the entropy; ALWAYS
  complete the draw by hashing a seed struct containing entropy + prevSpectrumDigest +
  prevUniverseDigest + prevComputerDigest + epoch + tick + participant count with `qpi.K12`;
  index = `mod((uint64)digest.u64._0, count)`. Grindable-fallback trade-off is accepted so a
  RANDOM outage can't permanently block the draw. — QTREAT-2, RandomLottery, QRaffle, Quottery.
- **Cross-contract reads for ownership proofs:** call the authoritative contract (e.g.
  QBAY.getInfoOfNFTById) live, check the error flag, compare `possessor` to
  `qpi.invocator()`; re-verify recurring claims each END_EPOCH and auto-revoke only on a
  *successful* verification that fails ownership. — QTREAT-2.
- **Fees model:** define `NAME_X_FEE` constexprs; causer-pays for user-triggered QX operations;
  contract-pays from a named fund for automated deliveries; every fee subtraction is explicit at
  the site where the cost occurs.

## 7. Testing (the definition of "perfect" is: passes the harness)

- Tests live in `core/test/contract_<name>.cpp` (GoogleTest, `#define NO_UEFI`, includes
  `contract_testing.h`). Model: `core/test/contract_qtreat.cpp` (1,413 lines), plus
  `contract_ggwp.cpp`, `contract_rl.cpp`.
- Harness pattern: subclass `ContractTesting`; `initEmptySpectrum(); initEmptyUniverse();`
  set `system.epoch = contractDescriptions[IDX].constructionEpoch`; `INIT_CONTRACT(...)` +
  `callSystemProcedure(IDX, INITIALIZE)` for the contract AND every dependency (QX, RANDOM,
  QBAY...); drive epochs with BEGIN_EPOCH/END_EPOCH system calls; invoke procedures by their
  registered ids; assert on funds, balances, and share ownership.
- Cover at minimum: every error path refunds the reward; fee under/over-payment; capacity
  exhaustion; external-call failure injection; epoch-boundary behavior (begin/end snapshots,
  auto-release, retry-next-epoch paths); accounting invariants (fund totals == transfers made).
- **Fast standalone syntax check** (no full node build needed; catches size-limit and macro
  errors in seconds — this caught a real MAX_INPUT_SIZE overflow in QMail.h): compile a scratch
  TU that mirrors the contract_def.h include order with `-fsyntax-only`:
  ```cpp
  #include "contract_core/pre_qpi_def.h"
  #include "contracts/qpi.h"
  #define <NAME>_CONTRACT_INDEX <n>
  #define CONTRACT_INDEX <NAME>_CONTRACT_INDEX
  #define CONTRACT_STATE_TYPE <NAME>
  #define CONTRACT_STATE2_TYPE <NAME>2
  #include "contracts/<Name>.h"
  static_assert(sizeof(<NAME>::StateData) > 0);
  ```
  `clang++ -fsyntax-only -std=c++20 --target=x86_64-<os> -mavx -mavx2 -fshort-wchar -I src
  -I lib/platform_common -I lib/platform_os -I lib/platform_efi -I . check.cpp` — expect only
  undefined-inline warnings (implementations live in contract_core impl headers), zero errors.
- Note: the full test build is x86-only (AVX2 minimum, `-mrdrnd`); on arm64 machines run the
  syntax check locally and the test suite on an x86 box (`cmake -B build -DENABLE_AVX512=OFF`,
  `make qubic_core_tests`, `./test/qubic_core_tests --gtest_filter=Test<Name>.*`).

## 8. Pre-submission checklist

- [ ] No prohibited tokens: `[ ] % / " ' # __ typedef union float double int char long`
      (grep the file), no locals outside `_locals` structs.
- [ ] All capacities powers of two; static_asserts on constant relationships.
- [ ] Every `_input` struct <= 1024 bytes (MAX_INPUT_SIZE); run the standalone syntax check
      (section 7) — it enforces this and every other framework static_assert.
- [ ] Multiple inter-contract calls in one scope use the `_E` macro variants with distinct
      error-variable names, and every one of them is tested.
- [ ] Anything validated by a *later* framework call (asset names for `issueAsset`, capacities,
      contract indices) is also validated at the point the value first enters the contract, so a
      bad value cannot strand a flow that has already accepted money.
- [ ] Layout: constants → NAME2 → records → StateData → I/O structs → functions → procedures →
      REGISTER → INITIALIZE → hooks. I/O structs precede their bodies.
- [ ] Every early return in every procedure refunds `invocationReward`.
- [ ] Every cross-contract call followed by an `interContractCallError` check.
- [ ] Every payout loop: reserve/distribute/reconcile, `break` not `return`, balance-clamped,
      balance re-read per phase.
- [ ] State committed before final external interaction in every claim/withdraw path.
- [ ] Capacity checked before any irreversible asset movement.
- [ ] No share operations inside PRE/POST_ACQUIRE/RELEASE hooks.
- [ ] `cleanupIfNeeded()` on all removal-seeing HashMaps at END_EPOCH top.
- [ ] Record-removal criteria identical on all emptying paths.
- [ ] Exclusion/eligibility filters at every accrual site.
- [ ] Admin hardcoded, deploy-time identities marked; error codes append-only; registration ids
      stable.
- [ ] Test file in `core/test/` passing; contract wired into `contract_def.h` (index defines,
      description entry, REGISTER line) and a `contract_<name>.cpp` TU exists.
