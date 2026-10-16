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
//   them. At that moment each contributor's share of the deployed QU is written
//   as their permanent `cost` in that holding. Later contributions therefore
//   fund only later acquisitions and never dilute an existing holding's
//   payouts. This sidesteps NAV valuation entirely: nothing has to be priced to
//   decide who is owed what.
//
//   `Holding.costBasis` is the ownership DENOMINATOR, never a valuation. It is
//   fixed for the life of the holding. Shares may fall to zero through partial
//   sales without disturbing anyone's fraction.
//
// PAYOUTS — one accumulator for dividends and sale proceeds
//   Both are QU arriving against a holding, so both advance the same
//   `payoutPerCostX64` accumulator. A member's entitlement is
//   cost * (payoutPerCostX64 - payoutDebtX64), claimed per holding. No loop
//   over members is ever needed to distribute.
//
// SECONDARY MARKET — buy-outs
//   A member may offer all or part of a position in one holding at a price they
//   set; anyone may fill it. QU goes to the seller and the cost units transfer
//   to the buyer, so the asset itself is untouched — the seat changes hands.
//   Exits are deliberately NOT vote-gated: requiring pool approval to leave
//   would let a majority trap a member.
//
// INTEGRATION CONSTRAINTS (verified against qpi_ipo_impl.h)
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
constexpr uint64 QPOOL_MAX_SOURCES               = 16384;  // payout source -> holding
// A holding may hold slots at several prices at once: a re-bid does not remove
// the earlier bid, it sits alongside it until outbid. Every live price must be
// tracked or reconciliation miscounts what is still at risk.
constexpr uint64 QPOOL_MAX_IPO_BIDS              = 8;
static_assert((QPOOL_MAX_POOLS & (QPOOL_MAX_POOLS - 1)) == 0);
static_assert((QPOOL_MAX_HOLDINGS & (QPOOL_MAX_HOLDINGS - 1)) == 0);
static_assert((QPOOL_MAX_PROPOSALS & (QPOOL_MAX_PROPOSALS - 1)) == 0);
static_assert((QPOOL_MAX_CONTRIBUTIONS & (QPOOL_MAX_CONTRIBUTIONS - 1)) == 0);
static_assert((QPOOL_MAX_OWNERSHIP & (QPOOL_MAX_OWNERSHIP - 1)) == 0);
static_assert((QPOOL_MAX_OFFERS & (QPOOL_MAX_OFFERS - 1)) == 0);
static_assert((QPOOL_MAX_SOURCES & (QPOOL_MAX_SOURCES - 1)) == 0);
static_assert((QPOOL_MAX_CONTRIBUTORS_PER_POOL & (QPOOL_MAX_CONTRIBUTORS_PER_POOL - 1)) == 0);

// ---- Timing. TARGET_TICK_DURATION is 1000 ms, so 1 tick ~ 1 second.
constexpr uint64 QPOOL_TICKS_PER_HOUR   = 3600;
constexpr uint64 QPOOL_VOTE_WINDOW      = 72 * QPOOL_TICKS_PER_HOUR; // 3 days
// A passed proposal must be executed within this window or it expires, so a
// stale mandate cannot be fired months later at a very different price.
constexpr uint64 QPOOL_EXECUTE_WINDOW   = 48 * QPOOL_TICKS_PER_HOUR; // 2 days

// ---- Vote thresholds (permille of eligible weight) ----
constexpr uint64 QPOOL_QUORUM_PERMILLE  = 500; // 50% of eligible weight must vote
static_assert(QPOOL_QUORUM_PERMILLE <= 1000);

// ---- Fees ---------------------------------------------------------------
// Registration matches MSVAULT's 5M vault-registration fee: enough to price
// spam, and the corpus norm for "create a container that holds funds".
constexpr uint64 QPOOL_CREATE_POOL_FEE  = 50000000ULL;
// Charged once, the first time an address ever contributes to a GIVEN pool
// (becomes a new roster slot/voting member). A cheap top-up afterward is
// free -- this only prices becoming a NEW member, the same way
// QPOOL_CREATE_POOL_FEE prices becoming a new pool, so spinning up many
// identities just to flood a pool's roster isn't free.
constexpr uint64 QPOOL_JOIN_FEE         = 100000ULL;
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
constexpr uint64 QPOOL_KIND_SELL_QX     = 5; // place QX ask order
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
constexpr uint32 QPOOL_ERR_ABOVE_CEILING = 19;
constexpr uint32 QPOOL_ERR_PROPOSAL_PENDING = 20;
constexpr uint32 QPOOL_ERR_NOT_ADMITTED = 21;
constexpr uint32 QPOOL_ERR_NOT_AUTHORIZED = 22;

struct QPOOL2 {};

struct QPOOL : public ContractBase
{
    struct Pool
    {
        id creator;
        uint64 poolName;
        uint64 uncommittedTotal; // QU contributed and not yet deployed
        uint64 committedTotal;   // lifetime QU deployed into holdings
        uint64 contributorCount;
        uint64 holdingCount;
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
        // QU currently backing this member's cast-but-unresolved BUY-kind
        // votes in this pool. WithdrawUncommitted may never take uncommitted
        // below this, so a member cannot vote with capital and then reclaim
        // it before the vote resolves -- a free, riskless veto that would
        // otherwise let one wallet serially block every proposal in a pool
        // by recycling the same QU. Released by Tally() once the proposal
        // it backs leaves OPEN.
        uint64 lockedForVotes;
    };

    struct Holding
    {
        uint64 poolId;
        Asset asset;
        uint64 shares;            // shares currently held
        uint64 costBasis;         // permanent ownership denominator
        uint128 payoutPerCostX64; // dividends + sale proceeds per QU of cost
        uint64 totalPayouts;
        // 1 = the asset was DEPOSITED by a member rather than bought with
        // pooled QU. costBasis then means "seat units" declared by the
        // depositor, not QU spent, so it is excluded from pool.committedTotal.
        uint64 deposited;
        uint64 ipoContractIndex;  // non-zero for an IPO-acquired holding
        uint64 pendingShares;     // ordered but not yet confirmed (QX / IPO)
        // IPO auctions are competitive: a bid that is outbid is refunded
        // immediately (TransferType::ipoBidRefund) and must be re-placed
        // higher. The budget is committed once; these track how much of it is
        // currently locked in the standing bid versus free to re-bid.
        uint64 ipoMaxPrice;       // highest price this holding has bid so far
        // Every price this holding has live in the book. Slots at price P belong
        // to this holding iff P appears here, so all of them must be summed.
        Array<uint64, QPOOL_MAX_IPO_BIDS> ipoBidPrices;
        uint64 ipoBidCount;
        uint64 ipoQuantity;       // shares sought
        uint64 ipoAtRisk;         // QU locked in the standing bid
        uint64 ipoAvailable;      // QU free to re-bid (refunded or unspent)
        uint64 active;
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
    };

    struct Proposal
    {
        uint64 poolId;
        uint64 kind;
        id proposer;
        Asset asset;
        uint64 quAmount;    // QU to deploy (buys) or min proceeds (sells)
        uint64 shares;      // shares to sell, IPO quantity, or QX share count
        uint64 price;       // QX limit price or IPO price per share
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

    // BUGFIX: qpi.numberOfShares(asset, byOwner(SELF), byPossessor(SELF)) --
    // used by both SettleIpo and ReconcileHolding to learn how many shares a
    // holding actually won/holds -- reports QPOOL's TOTAL holdings of that
    // asset across the WHOLE contract, not the amount specific to any one
    // holding. Confirmed live: when two different pools' holdings both
    // target the same IPO contract (a supported, expected scenario -- any
    // number of pools can bid on the same target), and BOTH win seats,
    // qpi.numberOfShares() returns the SAME combined total to both holdings'
    // SettleIpo calls (e.g. pool A won 300, pool B won 200, and BOTH
    // holdings ended up recorded with shares=500). assetSharesAttributed
    // tracks a running "already claimed by an earlier-settled holding" ledger
    // so each holding's settlement gets only the remainder, keyed by
    // (issuer, assetName) since Asset itself has no operator== for HashMap use.
    struct AssetKey
    {
        id issuer;
        uint64 assetName;
        bool operator==(const AssetKey& o) const { return issuer == o.issuer && assetName == o.assetName; }
        bool operator!=(const AssetKey& o) const { return issuer != o.issuer || assetName != o.assetName; }
    };

    struct Offer
    {
        uint64 costUnits; // cost basis offered for sale
        uint64 askPrice;  // QU demanded for the whole parcel
    };

    struct VoteKey
    {
        id voter;
        uint64 proposalId;
        bool operator==(const VoteKey& o) const { return voter == o.voter && proposalId == o.proposalId; }
        bool operator!=(const VoteKey& o) const { return voter != o.voter || proposalId != o.proposalId; }
    };

    struct VoteChoice
    {
        uint64 weight;   // recorded at vote time; used to release Contribution::lockedForVotes in Tally
        uint64 approve;  // 1 = yes, 0 = no -- lets Execute skip a no-voter's pro-rata buy-in
    };

    struct StateData
    {
        Array<Pool, QPOOL_MAX_POOLS> pools;
        uint64 poolCount;

        // Flattened member roster: pool p owns [p*256, p*256+256).
        Array<id, QPOOL_MAX_POOLS * QPOOL_MAX_CONTRIBUTORS_PER_POOL> members;
        // Flattened holding index: pool p owns [p*16, p*16+16).
        Array<uint64, QPOOL_MAX_POOLS * QPOOL_MAX_HOLDINGS_PER_POOL> poolHoldings;

        HashMap<ContribKey, Contribution, QPOOL_MAX_CONTRIBUTIONS> contributions;
        // (member, poolId) -> 1 for permissioned pools. Unused for open pools.
        HashMap<ContribKey, uint64, QPOOL_MAX_CONTRIBUTIONS> admitted;
        Array<Holding, QPOOL_MAX_HOLDINGS> holdings;
        uint64 holdingCount;
        HashMap<OwnKey, Ownership, QPOOL_MAX_OWNERSHIP> ownership;
        Array<Proposal, QPOOL_MAX_PROPOSALS> proposals;
        uint64 proposalCount;
        HashMap<VoteKey, VoteChoice, QPOOL_MAX_OWNERSHIP> votes;
        HashMap<OfferKey, Offer, QPOOL_MAX_OFFERS> offers;
        // Maps an incoming payer to the holding it pays, so dividends and fills
        // are attributed without a vote.
        HashMap<id, uint64, QPOOL_MAX_SOURCES> payoutSources;
        // How many shares of a given asset have already been claimed by an
        // earlier-settled holding -- see the AssetKey comment above.
        HashMap<AssetKey, uint64, QPOOL_MAX_HOLDINGS> assetSharesAttributed;

        uint64 totalContributed;
        uint64 totalDeployed;
        uint64 totalPayoutsDistributed;
        uint64 globalUnattributed; // arrivals that match no pool or holding
        // Refunds carry no IPO identity, so they are NOT attributed on arrival.
        // They pool here, and ReconcileIpo moves them to the right holding by
        // reading the live IPO order book — which supports any number of
        // concurrent pools and concurrent IPOs.
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

    struct CreatePool_input { uint64 poolName; uint64 permissioned; };
    struct CreatePool_output { uint32 returnCode; uint64 poolId; };
    struct CreatePool_locals { Pool pool; uint64 i; };

    struct Contribute_input { uint64 poolId; };
    struct Contribute_output { uint32 returnCode; uint64 uncommitted; };
    struct Contribute_locals
    {
        Pool pool; ContribKey key; Contribution c; uint64 existed; uint64 amount; uint64 base;
        uint64 joinFee;
    };

    struct WithdrawUncommitted_input { uint64 poolId; uint64 amount; };
    struct WithdrawUncommitted_output { uint32 returnCode; uint64 withdrawn; };
    struct WithdrawUncommitted_locals { Pool pool; ContribKey key; Contribution c; uint64 available; };

    struct SetAdmission_input { uint64 poolId; id member; uint64 allowed; };
    struct SetAdmission_output { uint32 returnCode; };
    struct SetAdmission_locals { Pool pool; ContribKey key; };

    struct SetPermissioned_input { uint64 poolId; uint64 permissioned; };
    struct SetPermissioned_output { uint32 returnCode; };
    struct SetPermissioned_locals { Pool pool; };

    struct Propose_input
    {
        uint64 poolId; uint64 kind; Asset asset;
        uint64 quAmount; uint64 shares; uint64 price; uint64 holdingId;
    };
    struct Propose_output { uint32 returnCode; uint64 proposalId; };
    struct Propose_locals
    {
        Pool pool; ContribKey key; Contribution c; Proposal p; Holding h;
        uint64 i; uint64 j; uint64 base; uint64 weight; uint64 slotStatus;
        VoteKey vk;
    };

    struct Vote_input { uint64 proposalId; uint64 approve; };
    struct Vote_output { uint32 returnCode; uint64 weight; };
    struct Vote_locals
    {
        Proposal p; VoteKey vk; ContribKey ck; Contribution c;
        OwnKey ok; Ownership own; uint64 weight;
    };

    struct Tally_input { uint64 proposalId; };
    struct Tally_output { uint32 returnCode; uint64 status; };
    struct Tally_locals
    {
        Proposal p; uint64 quorum; uint64 cast;
        // Used to release each voter's Contribution::lockedForVotes once this
        // proposal leaves OPEN (see the release loop at the end of Tally).
        Pool pool; uint64 base; uint64 j; VoteKey vk; VoteChoice voteChoice;
        ContribKey ck; Contribution c;
    };

    struct Execute_input { uint64 proposalId; };
    struct Execute_output { uint32 returnCode; uint64 holdingId; uint64 acquired; uint64 proceeds; };
    struct Execute_locals
    {
        Proposal p; Pool pool; Holding h; ContribKey ck; Contribution c;
        OwnKey ok; Ownership own; uint128 scaled;
        VoteKey vk; VoteChoice voteChoice;
        uint64 i; uint64 base; uint64 slot; uint64 share; uint64 assigned; uint64 remaining;
        uint64 newHoldingId; uint64 hbase;
        Entity ent; uint64 balBefore; uint64 balAfter; uint64 received; uint64 fee;
        sint64 rel; sint64 heldBefore; sint64 heldAfter; sint64 ipoResult; sint64 alreadyManaged;
        sint64 managedBySelf;
        id sourceId;
        QSWAP::SwapExactQuForAsset_input swapBuyIn; QSWAP::SwapExactQuForAsset_output swapBuyOut;
        QSWAP::SwapExactAssetForQu_input swapSellIn; QSWAP::SwapExactAssetForQu_output swapSellOut;
        QX::AddToBidOrder_input qxBidIn; QX::AddToBidOrder_output qxBidOut;
        QX::AddToAskOrder_input qxAskIn; QX::AddToAskOrder_output qxAskOut;
        // Named by which contract is CALLED (asked to hand shares over),
        // not by the destination -- e.g. xferToQswapIn is passed to QX's
        // own TransferShareManagementRights (QX currently holds the shares
        // and is asked to move them to QSWAP), so its type is QX's input.
        QX::TransferShareManagementRights_input xferToQswapIn;
        QX::TransferShareManagementRights_output xferToQswapOut;
        QSWAP::TransferShareManagementRights_input xferToQxIn;
        QSWAP::TransferShareManagementRights_output xferToQxOut;
    };

    struct ClaimPayout_input { uint64 holdingId; };
    struct ClaimPayout_output { uint32 returnCode; uint64 paid; };
    struct ClaimPayout_locals
    {
        Holding h; OwnKey ok; Ownership own; uint128 pending; uint64 owed;
    };

    struct OfferPosition_input { uint64 holdingId; uint64 costUnits; uint64 askPrice; };
    struct OfferPosition_output { uint32 returnCode; };
    struct OfferPosition_locals { OwnKey ok; Ownership own; OfferKey fk; Offer f; Holding h; };

    struct CancelOffer_input { uint64 holdingId; };
    struct CancelOffer_output { uint32 returnCode; };
    struct CancelOffer_locals { OfferKey fk; };

    struct BuyPosition_input { uint64 holdingId; id seller; };
    struct BuyPosition_output { uint32 returnCode; uint64 costUnitsBought; };
    struct BuyPosition_locals
    {
        Holding h; Pool pool; OfferKey fk; Offer f;
        OwnKey sellerKey; Ownership sellerOwn; OwnKey buyerKey; Ownership buyerOwn;
        uint128 pending; uint64 buyerExisted; uint64 i; uint64 base; uint64 slot;
        ContribKey admitKey; uint64 fee;
    };

    struct ReconcileHolding_input { uint64 holdingId; };
    struct ReconcileHolding_output { uint32 returnCode; uint64 shares; uint64 pendingShares; };
    struct ReconcileHolding_locals
    {
        Holding h; sint64 held;
        AssetKey ak; uint64 alreadyAttributed; uint64 unclaimed; uint64 newClaim;
    };

    struct RebidIpo_input { uint64 holdingId; uint64 price; uint64 quantity; };
    struct RebidIpo_output { uint32 returnCode; uint64 bidsRegistered; uint64 atRisk; uint64 available; };
    struct RebidIpo_locals
    {
        Holding h; uint64 cost; uint64 j; sint64 ipoResult;
        uint64 i; uint64 seated; uint64 shouldBeAtRisk; sint64 price; uint64 freed;
    };

    struct DepositHoldingAsset_input
    {
        uint64 poolId; Asset asset; uint64 amount; uint64 seatUnits;
    };
    struct DepositHoldingAsset_output { uint32 returnCode; uint64 holdingId; };
    struct DepositHoldingAsset_locals
    {
        Pool pool; Holding h; OwnKey ok; Ownership own; ContribKey ck; Contribution c;
        uint64 i; uint64 base; uint64 hbase; uint64 newHoldingId; uint64 onRoster;
        sint64 managed; sint64 xfer;
    };

    struct ReconcileIpo_input { uint64 holdingId; };
    struct ReconcileIpo_output
    {
        uint32 returnCode; uint64 seatedShares; uint64 atRisk; uint64 available; uint64 recovered;
    };
    struct ReconcileIpo_locals
    {
        Holding h; uint64 i; uint64 j; uint64 seated; uint64 shouldBeAtRisk; uint64 freed;
        sint64 price;
    };

    struct SettleIpo_input { uint64 holdingId; };
    struct SettleIpo_output { uint32 returnCode; uint64 shares; uint64 returnedToMembers; };
    struct SettleIpo_locals
    {
        Holding h; Pool pool; ContribKey ck; Contribution c; OwnKey ok; Ownership own;
        uint64 i; uint64 base; uint64 refundEach; uint64 returned; uint64 leftover;
        uint64 freed;
        sint64 held;
        AssetKey ak; uint64 alreadyAttributed; uint64 myShare;
    };

    struct GetPool_input { uint64 poolId; };
    struct GetPool_output
    {
        uint32 found; id creator; uint64 poolName; uint64 uncommittedTotal;
        uint64 committedTotal; uint64 contributorCount; uint64 holdingCount;
        uint64 unattributedQu; uint64 permissioned;
    };
    struct GetPool_locals { Pool pool; };

    struct GetMember_input { uint64 poolId; id member; };
    struct GetMember_output { uint32 found; uint64 uncommitted; uint64 committed; };
    struct GetMember_locals { ContribKey key; Contribution c; };

    struct GetHolding_input { uint64 holdingId; };
    struct GetHolding_output
    {
        uint32 found; uint64 poolId; id issuer; uint64 assetName;
        uint64 shares; uint64 costBasis; uint64 totalPayouts;
        uint64 pendingShares; uint64 ipoContractIndex; uint64 active;
        uint64 ipoMaxPrice; uint64 ipoQuantity; uint64 ipoAtRisk; uint64 ipoAvailable;
        uint64 deposited;
    };
    struct GetHolding_locals { Holding h; };

    struct GetPosition_input { uint64 holdingId; id member; };
    struct GetPosition_output
    {
        uint32 found; uint64 cost; uint64 costBasis; uint64 owed;
        uint64 offeredCostUnits; uint64 askPrice;
    };
    struct GetPosition_locals
    {
        Holding h; OwnKey ok; Ownership own; OfferKey fk; Offer f; uint128 pending;
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
    };

    // ======================== Functions ========================

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
    }

    PUBLIC_FUNCTION_WITH_LOCALS(GetMember)
    {
        locals.key.member = input.member;
        locals.key.poolId = input.poolId;
        if (!state.get().contributions.get(locals.key, locals.c)) return;
        output.found = 1;
        output.uncommitted = locals.c.uncommitted;
        output.committed = locals.c.committed;
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
        output.ipoQuantity = locals.h.ipoQuantity;
        output.ipoAtRisk = locals.h.ipoAtRisk;
        output.ipoAvailable = locals.h.ipoAvailable;
        output.deposited = locals.h.deposited;
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
    }

    // ======================== Pool membership ========================

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
        for (locals.i = 0; locals.i < QPOOL_MAX_POOLS; locals.i++)
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
        state.mut().pools.set(locals.i, locals.pool);
        state.mut().poolCount = state.get().poolCount + 1;

        // Fees accrue as revenue and are split to shareholders and burn at
        // END_EPOCH, rather than being burned outright here.
        state.mut().pendingRevenue = sadd(state.get().pendingRevenue, QPOOL_CREATE_POOL_FEE);
        state.mut().totalRevenue = sadd(state.get().totalRevenue, QPOOL_CREATE_POOL_FEE);

        output.poolId = locals.i;
        output.returnCode = QPOOL_OK;
    }

    // Contribute QU. It stays withdrawable until a passed proposal deploys it.
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
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_NOT_FOUND; return;
        }
        // Permissioned pool: only an admitted address may contribute.
        if (locals.pool.permissioned != 0)
        {
            locals.key.member = qpi.invocator();
            locals.key.poolId = input.poolId;
            if (!state.get().admitted.contains(locals.key))
            {
                qpi.transfer(qpi.invocator(), qpi.invocationReward());
                output.returnCode = QPOOL_ERR_NOT_ADMITTED; return;
            }
        }

        locals.key.member = qpi.invocator();
        locals.key.poolId = input.poolId;
        locals.c.uncommitted = 0; locals.c.committed = 0; locals.c.lockedForVotes = 0;
        locals.existed = state.get().contributions.get(locals.key, locals.c) ? 1 : 0;

        // First-time joiners also pay a flat, one-time join fee (see
        // QPOOL_JOIN_FEE) -- collected as pool revenue, same bucket as
        // QPOOL_CREATE_POOL_FEE. A repeat top-up from an existing member
        // pays none of this.
        locals.joinFee = (locals.existed == 0) ? QPOOL_JOIN_FEE : 0;
        if ((uint64)qpi.invocationReward() < locals.joinFee + QPOOL_MIN_CONTRIBUTION)
        {
            if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.amount = (uint64)qpi.invocationReward() - locals.joinFee;

        if (locals.existed == 0)
        {
            // Capacity checks BEFORE the QU is retained, so a full roster
            // refunds rather than swallowing the contribution.
            if (locals.pool.contributorCount >= QPOOL_MAX_CONTRIBUTORS_PER_POOL
                || state.get().contributions.population() >= QPOOL_MAX_CONTRIBUTIONS)
            {
                qpi.transfer(qpi.invocator(), qpi.invocationReward());
                output.returnCode = QPOOL_ERR_CAPACITY; return;
            }
        }
        locals.c.uncommitted = sadd(locals.c.uncommitted, locals.amount);
        if (state.mut().contributions.set(locals.key, locals.c) == NULL_INDEX)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        if (locals.existed == 0)
        {
            locals.base = input.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL;
            state.mut().members.set(locals.base + locals.pool.contributorCount, qpi.invocator());
            locals.pool.contributorCount = locals.pool.contributorCount + 1;
            state.mut().pendingRevenue = sadd(state.get().pendingRevenue, locals.joinFee);
            state.mut().totalRevenue = sadd(state.get().totalRevenue, locals.joinFee);
        }
        locals.pool.uncommittedTotal = sadd(locals.pool.uncommittedTotal, locals.amount);
        state.mut().pools.set(input.poolId, locals.pool);
        state.mut().totalContributed = sadd(state.get().totalContributed, locals.amount);

        output.uncommitted = locals.c.uncommitted;
        output.returnCode = QPOOL_OK;
    }

    // Pull back QU that has not been deployed. Committed capital is not
    // withdrawable — exit an active holding through the position market.
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
        // Capital currently backing an open vote cannot be withdrawn until
        // that vote resolves via Tally -- see Contribution::lockedForVotes.
        locals.available = (locals.c.uncommitted > locals.c.lockedForVotes)
            ? locals.c.uncommitted - locals.c.lockedForVotes : 0;
        if (locals.available < input.amount)
        {
            output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
        }

        // Effects before the transfer.
        locals.c.uncommitted = locals.c.uncommitted - input.amount;
        state.mut().contributions.set(locals.key, locals.c);
        locals.pool.uncommittedTotal = (locals.pool.uncommittedTotal > input.amount)
            ? locals.pool.uncommittedTotal - input.amount : 0;
        state.mut().pools.set(input.poolId, locals.pool);

        qpi.transfer(qpi.invocator(), (sint64)input.amount);
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
        if (locals.pool.permissioned == 0) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        if (qpi.invocator() != locals.pool.creator) { output.returnCode = QPOOL_ERR_NOT_AUTHORIZED; return; }

        locals.key.member = input.member;
        locals.key.poolId = input.poolId;
        if (input.allowed == 0)
        {
            // Revoking blocks FUTURE joining only. It cannot claw back seats
            // already owned — those are the member's property and leave only
            // through the position market.
            state.mut().admitted.removeByKey(locals.key);
            output.returnCode = QPOOL_OK; return;
        }
        if (!state.get().admitted.contains(locals.key)
            && state.get().admitted.population() >= QPOOL_MAX_CONTRIBUTIONS)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        if (state.mut().admitted.set(locals.key, 1) == NULL_INDEX)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        output.returnCode = QPOOL_OK;
    }

    // Toggle a pool between open and permissioned after creation. Creator only.
    //
    // Safe for the same reason SetAdmission is: the per-acquisition ownership
    // model means neither direction can touch an existing holding's cost
    // basis or claw back a seat -- this only ever gates FUTURE Contribute/
    // BuyPosition/DepositHoldingAsset calls, exactly like SetAdmission's own
    // admit/revoke. Existing seats and voting rights are untouched either way.
    //
    // Switching open -> permissioned does NOT retroactively admit existing
    // contributors -- none of them were ever added to `admitted` while the
    // pool was open, so every one of them is locked out of further
    // Contribute/BuyPosition/DepositHoldingAsset calls until the creator
    // explicitly SetAdmission's them, same as any other permissioned pool.
    //
    // Switching permissioned -> open does not clear `admitted` -- it simply
    // stops being consulted while permissioned == 0. If the pool is later
    // switched back to permissioned, that old list becomes active again as-is.
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

        locals.pool.permissioned = input.permissioned;
        state.mut().pools.set(input.poolId, locals.pool);
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
        // as the spam guard now that proposing is free.
        for (locals.i = 0; locals.i < QPOOL_MAX_PROPOSALS; locals.i++)
        {
            if (state.get().proposals.get(locals.i).kind == 0) continue;
            if (state.get().proposals.get(locals.i).poolId != input.poolId) continue;
            if (state.get().proposals.get(locals.i).proposer != qpi.invocator()) continue;
            locals.slotStatus = state.get().proposals.get(locals.i).status;
            if (locals.slotStatus == QPOOL_PROP_OPEN || locals.slotStatus == QPOOL_PROP_PASSED)
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
            if (input.quAmount == 0 || input.quAmount > locals.pool.uncommittedTotal
                || input.asset.issuer == NULL_ID || input.asset.assetName == 0
                || input.shares == 0)
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
            if (input.kind == QPOOL_KIND_BUY_QX && input.price == 0)
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
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
            // price is the MAXIMUM per share; re-bids may go up to it without a
            // fresh vote, because an IPO auction moves faster than a vote can.
            if (input.shares == 0 || input.price == 0
                || input.holdingId <= (uint64)SELF_INDEX
                || input.asset.assetName == 0 || input.asset.issuer != NULL_ID
                || input.quAmount == 0 || input.quAmount > locals.pool.uncommittedTotal
                || input.price * input.shares > input.quAmount)
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
            // Attribution is by PRICE: the order book shows every slot this
            // contract holds, and slots at price P belong to the bid made at
            // price P. So a new bid must not collide with a live bid on the
            // same IPO, otherwise two pools' slots would be indistinguishable.
            for (locals.i = 0; locals.i < QPOOL_MAX_HOLDINGS; locals.i++)
            {
                locals.h = state.get().holdings.get(locals.i);
                if (locals.h.active == 0) continue;
                if (locals.h.ipoContractIndex != input.holdingId) continue;
                for (locals.j = 0; locals.j < locals.h.ipoBidCount; locals.j++)
                {
                    if (locals.h.ipoBidPrices.get(locals.j) == input.price)
                    {
                        output.returnCode = QPOOL_ERR_IPO_PRICE_TAKEN; return;
                    }
                }
            }
        }
        else
        {
            // Sell or allocate: the holding must exist and belong to this pool.
            if (input.holdingId >= QPOOL_MAX_HOLDINGS)
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
            locals.h = state.get().holdings.get(input.holdingId);
            if (locals.h.active == 0 || locals.h.poolId != input.poolId)
            {
                qpi.transfer(qpi.invocator(), qpi.invocationReward());
                output.returnCode = QPOOL_ERR_NOT_FOUND; return;
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
            if (input.kind == QPOOL_KIND_SELL_QX && input.price == 0)
            {
                output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
            }
        }

        // Find a reusable slot: never touched, or finished. OPEN and PASSED
        // proposals are still live and must be skipped.
        for (locals.i = 0; locals.i < QPOOL_MAX_PROPOSALS; locals.i++)
        {
            if (state.get().proposals.get(locals.i).kind == 0) break;
            locals.slotStatus = state.get().proposals.get(locals.i).status;
            if (locals.slotStatus == QPOOL_PROP_EXECUTED
                || locals.slotStatus == QPOOL_PROP_REJECTED
                || locals.slotStatus == QPOOL_PROP_EXPIRED
                || locals.slotStatus == QPOOL_PROP_FAILED) break;
        }
        if (locals.i >= QPOOL_MAX_PROPOSALS)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        // BUGFIX: slot `i` may be REUSED from a finished proposal
        // (EXECUTED/REJECTED/EXPIRED/FAILED, per the slot-search loop just
        // above). `votes` keys entries by (voter, proposalId), and
        // proposalId is only this array index -- not a globally-unique
        // proposal identity. Without clearing stale entries here, any
        // member who voted on the PREVIOUS occupant of this slot collides
        // with the same (voter, i) key on the NEW proposal and is wrongly
        // rejected with ERR_ALREADY_VOTED on what is, for them, a first
        // vote on this proposal. Confirmed via TallyEnforcesQuorumAndMajority
        // (contract_qpool.cpp): a second, independent proposal in the same
        // pool reuses slot 0, and a member's earlier vote on the first
        // (already-rejected) proposal there silently blocked their vote on
        // the second. Bounded by contributorCount (<=256), so this is cheap
        // on every Propose call.
        if (state.get().proposals.get(locals.i).kind != 0)
        {
            locals.base = input.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL;
            for (locals.j = 0; locals.j < locals.pool.contributorCount; locals.j++)
            {
                locals.vk.voter = state.get().members.get(locals.base + locals.j);
                locals.vk.proposalId = locals.i;
                if (state.get().votes.contains(locals.vk))
                {
                    state.mut().votes.removeByKey(locals.vk);
                }
            }
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
        locals.p.kind = input.kind;
        locals.p.proposer = qpi.invocator();
        locals.p.asset = input.asset;
        locals.p.quAmount = input.quAmount;
        locals.p.shares = input.shares;
        locals.p.price = input.price;
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
        if ((uint64)qpi.tick() - locals.p.openTick > QPOOL_VOTE_WINDOW)
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
            locals.weight = locals.c.uncommitted;
        }
        else
        {
            locals.ok.member = qpi.invocator();
            locals.ok.holdingId = locals.p.holdingId;
            if (!state.get().ownership.get(locals.ok, locals.own))
            {
                output.returnCode = QPOOL_ERR_NOT_MEMBER; return;
            }
            locals.weight = locals.own.cost;
        }
        if (locals.weight == 0) { output.returnCode = QPOOL_ERR_NOT_MEMBER; return; }

        locals.vk.voter = qpi.invocator();
        locals.vk.proposalId = input.proposalId;
        if (state.get().votes.contains(locals.vk))
        {
            output.returnCode = QPOOL_ERR_ALREADY_VOTED; return;
        }
        // Record approve/reject alongside the weight -- Execute uses this to
        // exclude a no-voter from being pro-rata'd into a buy they voted
        // against (see Execute's BUY-kind assignment loop).
        if (state.mut().votes.set(locals.vk, VoteChoice{ locals.weight, input.approve }) == NULL_INDEX)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }

        // Lock the capital backing this vote until the proposal resolves via
        // Tally (see Contribution::lockedForVotes / WithdrawUncommitted).
        // Conservative by design: voting on several simultaneously-open
        // proposals with the same balance sums the locks rather than
        // deduplicating them, so the same QU can look locked more than once
        // until each proposal it backed resolves. That only ever over-blocks
        // a withdrawal early, never lets one through it shouldn't -- it
        // never reopens the vote-then-withdraw hole this exists to close.
        if (locals.p.kind <= QPOOL_KIND_BUY_IPO)
        {
            locals.c.lockedForVotes = sadd(locals.c.lockedForVotes, locals.weight);
            state.mut().contributions.set(locals.ck, locals.c);
        }

        if (input.approve == 1) locals.p.yesWeight = sadd(locals.p.yesWeight, locals.weight);
        else locals.p.noWeight = sadd(locals.p.noWeight, locals.weight);
        state.mut().proposals.set(input.proposalId, locals.p);
        output.weight = locals.weight;
        output.returnCode = QPOOL_OK;
    }

    // Close the vote once its window expires. Permissionless.
    PUBLIC_PROCEDURE_WITH_LOCALS(Tally)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.proposalId >= QPOOL_MAX_PROPOSALS) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.p = state.get().proposals.get(input.proposalId);
        if (locals.p.kind == 0 || locals.p.status != QPOOL_PROP_OPEN)
        {
            output.returnCode = QPOOL_ERR_WRONG_STATUS; return;
        }
        if ((uint64)qpi.tick() - locals.p.openTick <= QPOOL_VOTE_WINDOW)
        {
            output.returnCode = QPOOL_ERR_WINDOW_OPEN; return;
        }
        locals.cast = sadd(locals.p.yesWeight, locals.p.noWeight);
        locals.quorum = div((uint128)locals.p.eligibleWeight * (uint128)QPOOL_QUORUM_PERMILLE,
            (uint128)1000ULL).low;
        if (locals.cast < locals.quorum || locals.p.yesWeight <= locals.p.noWeight)
        {
            locals.p.status = QPOOL_PROP_REJECTED;
        }
        else
        {
            locals.p.status = QPOOL_PROP_PASSED;
            locals.p.passedTick = qpi.tick();
        }
        state.mut().proposals.set(input.proposalId, locals.p);

        // This proposal can no longer be voted on -- release every voter's
        // Contribution::lockedForVotes for it (see Vote). Bounded by
        // contributorCount (<=256), same cost model as Propose's own
        // stale-vote cleanup loop.
        if (locals.p.kind <= QPOOL_KIND_BUY_IPO)
        {
            locals.pool = state.get().pools.get(locals.p.poolId);
            locals.base = locals.p.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL;
            for (locals.j = 0; locals.j < locals.pool.contributorCount; locals.j++)
            {
                locals.vk.voter = state.get().members.get(locals.base + locals.j);
                locals.vk.proposalId = input.proposalId;
                if (state.get().votes.get(locals.vk, locals.voteChoice))
                {
                    locals.ck.member = locals.vk.voter;
                    locals.ck.poolId = locals.p.poolId;
                    if (state.get().contributions.get(locals.ck, locals.c))
                    {
                        locals.c.lockedForVotes = (locals.c.lockedForVotes > locals.voteChoice.weight)
                            ? locals.c.lockedForVotes - locals.voteChoice.weight : 0;
                        state.mut().contributions.set(locals.ck, locals.c);
                    }
                }
            }
        }

        output.status = locals.p.status;
        output.returnCode = QPOOL_OK;
    }

    // ======================== Execution ========================

    // Carry out a passed proposal. Permissionless — anyone may fire it, because
    // the mandate came from the vote, not the caller. Must run inside the
    // execute window so a stale mandate cannot be fired at a new price.
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
            output.returnCode = QPOOL_ERR_WINDOW_CLOSED; return;
        }
        locals.pool = state.get().pools.get(locals.p.poolId);
        if (locals.pool.active == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }

        // ---------------- BUY paths ----------------
        if (locals.p.kind <= QPOOL_KIND_BUY_IPO)
        {
            if (locals.p.quAmount > locals.pool.uncommittedTotal)
            {
                output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
            }
            qpi.getEntity(SELF, locals.ent);
            locals.balBefore = locals.ent.incomingAmount - locals.ent.outgoingAmount;
            if (locals.p.quAmount > locals.balBefore)
            {
                output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
            }

            // Allocate a holding slot first, so ownership can be written.
            for (locals.newHoldingId = 0; locals.newHoldingId < QPOOL_MAX_HOLDINGS; locals.newHoldingId++)
            {
                if (state.get().holdings.get(locals.newHoldingId).costBasis == 0
                    && state.get().holdings.get(locals.newHoldingId).active == 0) break;
            }
            if (locals.newHoldingId >= QPOOL_MAX_HOLDINGS
                || locals.pool.holdingCount >= QPOOL_MAX_HOLDINGS_PER_POOL)
            {
                output.returnCode = QPOOL_ERR_CAPACITY; return;
            }

            // Write per-member cost basis pro-rata to uncommitted balances.
            // This is the one bounded loop in the contract; it runs once per
            // acquisition and fixes ownership of this holding forever.
            locals.base = locals.p.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL;
            locals.assigned = 0;
            for (locals.i = 0; locals.i < locals.pool.contributorCount; locals.i++)
            {
                locals.ck.member = state.get().members.get(locals.base + locals.i);
                locals.ck.poolId = locals.p.poolId;
                if (!state.get().contributions.get(locals.ck, locals.c)) continue;
                if (locals.c.uncommitted == 0) continue;
                // A member who voted NO is never forced into this buy -- their
                // money sits out of this specific acquisition (still fully
                // theirs, still withdrawable/usable for a future proposal they
                // DO support). Abstainers are unaffected: only an explicit NO
                // vote on THIS proposalId exempts you.
                locals.vk.voter = locals.ck.member;
                locals.vk.proposalId = input.proposalId;
                if (state.get().votes.get(locals.vk, locals.voteChoice) && locals.voteChoice.approve == 0) continue;
                locals.share = div((uint128)locals.c.uncommitted * (uint128)locals.p.quAmount,
                    (uint128)locals.pool.uncommittedTotal).low;
                if (locals.share == 0) continue;
                if (locals.share > locals.c.uncommitted) locals.share = locals.c.uncommitted;

                locals.c.uncommitted = locals.c.uncommitted - locals.share;
                locals.c.committed = sadd(locals.c.committed, locals.share);
                state.mut().contributions.set(locals.ck, locals.c);

                locals.ok.member = locals.ck.member;
                locals.ok.holdingId = locals.newHoldingId;
                locals.own.cost = locals.share;
                locals.own.payoutDebtX64 = uint128(0, 0);
                locals.own.claimable = 0;
                state.mut().ownership.set(locals.ok, locals.own);
                locals.assigned = sadd(locals.assigned, locals.share);
            }
            if (locals.assigned == 0) { output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return; }

            // Create the holding on the ACTUAL assigned total, so integer
            // remainders never leave costBasis overstated.
            locals.h.poolId = locals.p.poolId;
            locals.h.asset = locals.p.asset;
            locals.h.shares = 0;
            locals.h.costBasis = locals.assigned;
            locals.h.payoutPerCostX64 = uint128(0, 0);
            locals.h.totalPayouts = 0;
            locals.h.deposited = 0;
            locals.h.ipoContractIndex = 0;
            locals.h.pendingShares = 0;
            locals.h.active = 1;

            locals.pool.uncommittedTotal = (locals.pool.uncommittedTotal > locals.assigned)
                ? locals.pool.uncommittedTotal - locals.assigned : 0;
            locals.pool.committedTotal = sadd(locals.pool.committedTotal, locals.assigned);

            if (locals.p.kind == QPOOL_KIND_BUY_QSWAP)
            {
                locals.heldBefore = qpi.numberOfShares(locals.p.asset,
                    AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
                locals.swapBuyIn.assetIssuer = locals.p.asset.issuer;
                locals.swapBuyIn.assetName = locals.p.asset.assetName;
                locals.swapBuyIn.assetAmountOutMin = (sint64)locals.p.shares; // slippage bound
                INVOKE_OTHER_CONTRACT_PROCEDURE_E(QSWAP, SwapExactQuForAsset,
                    locals.swapBuyIn, locals.swapBuyOut, (sint64)locals.assigned, swapErr);
                if (swapErr != NoCallError) { output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return; }
                locals.heldAfter = qpi.numberOfShares(locals.p.asset,
                    AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
                locals.h.shares = (locals.heldAfter > locals.heldBefore)
                    ? (uint64)(locals.heldAfter - locals.heldBefore) : 0;
                output.acquired = locals.h.shares;
            }
            else if (locals.p.kind == QPOOL_KIND_BUY_QX)
            {
                // Order-book buy: fills asynchronously, reconciled later.
                locals.qxBidIn.issuer = locals.p.asset.issuer;
                locals.qxBidIn.assetName = locals.p.asset.assetName;
                locals.qxBidIn.price = (sint64)locals.p.price;
                locals.qxBidIn.numberOfShares = (sint64)locals.p.shares;
                INVOKE_OTHER_CONTRACT_PROCEDURE_E(QX, AddToBidOrder,
                    locals.qxBidIn, locals.qxBidOut, (sint64)locals.assigned, qxErr);
                if (qxErr != NoCallError) { output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return; }
                locals.h.pendingShares = (locals.qxBidOut.addedNumberOfShares > 0)
                    ? (uint64)locals.qxBidOut.addedNumberOfShares : 0;
            }
            else
            {
                // IPO bid. holdingId carries the target contract index, and the
                // asset is {NULL_ID, shareAssetName} from the proposal.
                locals.h.ipoContractIndex = locals.p.holdingId;
                locals.h.ipoMaxPrice = locals.p.price;
                locals.h.ipoQuantity = locals.p.shares;
                locals.h.ipoAvailable = locals.assigned;

                locals.share = locals.p.price * locals.p.shares;
                if (locals.share > locals.h.ipoAvailable)
                {
                    output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
                }
                locals.ipoResult = qpi.bidInIPO((uint32)locals.p.holdingId,
                    (sint64)locals.p.price, (uint32)locals.p.shares);
                if (locals.ipoResult < 0) { output.returnCode = QPOOL_ERR_IPO_REJECTED; return; }
                // The full amount is deducted even on a partial fill; anything
                // not seated is refunded straight back as ipoBidRefund.
                locals.h.ipoAtRisk = locals.share;
                locals.h.ipoAvailable = locals.h.ipoAvailable - locals.share;
                locals.h.pendingShares = (uint64)locals.ipoResult;
                locals.h.ipoBidPrices.set(0, locals.p.price);
                locals.h.ipoBidCount = 1;
            }

            state.mut().holdings.set(locals.newHoldingId, locals.h);
            if (locals.newHoldingId >= state.get().holdingCount)
            {
                state.mut().holdingCount = locals.newHoldingId + 1;
            }
            locals.hbase = locals.p.poolId * QPOOL_MAX_HOLDINGS_PER_POOL;
            state.mut().poolHoldings.set(locals.hbase + locals.pool.holdingCount, locals.newHoldingId);
            locals.pool.holdingCount = locals.pool.holdingCount + 1;
            state.mut().pools.set(locals.p.poolId, locals.pool);
            state.mut().totalDeployed = sadd(state.get().totalDeployed, locals.assigned);

            // Register the payer that will pay this holding, so dividends and
            // fills are attributed without a vote. First writer wins.
            if (locals.h.ipoContractIndex != 0)
            {
                locals.sourceId = id(locals.h.ipoContractIndex, 0, 0, 0);
            }
            else
            {
                locals.sourceId = locals.p.asset.issuer;
            }
            if (!state.get().payoutSources.contains(locals.sourceId)
                && state.get().payoutSources.population() < QPOOL_MAX_SOURCES)
            {
                state.mut().payoutSources.set(locals.sourceId, locals.newHoldingId);
            }

            output.holdingId = locals.newHoldingId;
        }
        // ---------------- SELL paths ----------------
        else if (locals.p.kind == QPOOL_KIND_SELL_QSWAP || locals.p.kind == QPOOL_KIND_SELL_QX)
        {
            locals.h = state.get().holdings.get(locals.p.holdingId);
            if (locals.h.active == 0 || locals.h.shares < locals.p.shares)
            {
                output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
            }
            qpi.getEntity(SELF, locals.ent);
            locals.balBefore = locals.ent.incomingAmount - locals.ent.outgoingAmount;

            if (locals.p.kind == QPOOL_KIND_SELL_QSWAP)
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

                qpi.getEntity(SELF, locals.ent);
                locals.balAfter = locals.ent.incomingAmount - locals.ent.outgoingAmount;
                locals.received = (locals.balAfter > locals.balBefore)
                    ? locals.balAfter - locals.balBefore : 0;
                if (locals.received == 0) { output.returnCode = QPOOL_ERR_TRANSFER_FAILED; return; }

                locals.h.shares = locals.h.shares - locals.p.shares;
                // Management fee first, then proceeds ride the SAME accumulator
                // as dividends. costBasis is untouched, so every member's
                // fraction is preserved.
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
                if (locals.h.shares == 0 && locals.h.pendingShares == 0) locals.h.active = 0;
                state.mut().holdings.set(locals.p.holdingId, locals.h);
                state.mut().totalPayoutsDistributed =
                    sadd(state.get().totalPayoutsDistributed, locals.received);
                output.proceeds = locals.received;
            }
            else
            {
                // QX ask order: proceeds arrive asynchronously and are credited
                // by POST_INCOMING_TRANSFER via the payoutSources mapping.
                // BUGFIX: same as SELL_QSWAP above -- shares bought via
                // BUY_QX (AddToBidOrder) are already managed by QX from the
                // moment of purchase, so releaseShares() (which requires
                // QPOOL to currently hold management rights) would fail here
                // too for the common case. Only release if not already
                // under QX's management.
                //
                // BUGFIX #8: mirrors SELL_QSWAP's fix above -- a holding
                // bought via BUY_QSWAP whose members later vote to sell
                // through QX has its shares managed by QSWAP, not QPOOL
                // itself, so releaseShares() alone can't reach them either.
                // Check all three possible current managers (QX already,
                // QPOOL itself, or QSWAP) and, if it's QSWAP, ask QSWAP's
                // own TransferShareManagementRights to hand it to QX first.
                locals.alreadyManaged = qpi.numberOfShares(locals.h.asset,
                    AssetOwnershipSelect{ SELF, QPOOL_QX_CONTRACT_INDEX, false, false },
                    AssetPossessionSelect{ SELF, QPOOL_QX_CONTRACT_INDEX, false, false });
                if (locals.alreadyManaged < (sint64)locals.p.shares)
                {
                    locals.managedBySelf = qpi.numberOfShares(locals.h.asset,
                        AssetOwnershipSelect{ SELF, SELF_INDEX, false, false },
                        AssetPossessionSelect{ SELF, SELF_INDEX, false, false });
                    if (locals.managedBySelf >= (sint64)locals.p.shares)
                    {
                        locals.rel = qpi.releaseShares(locals.h.asset, SELF, SELF, (sint64)locals.p.shares,
                            QPOOL_QX_CONTRACT_INDEX, QPOOL_QX_CONTRACT_INDEX, QPOOL_QX_TRANSFER_FEE);
                        if (locals.rel < 0) { output.returnCode = QPOOL_ERR_TRANSFER_FAILED; return; }
                    }
                    else
                    {
                        // Must be under QSWAP's management (bought via
                        // BUY_QSWAP, now being sold via SELL_QX) -- ask
                        // QSWAP itself to hand it to QX.
                        locals.xferToQxIn.asset = locals.h.asset;
                        locals.xferToQxIn.numberOfShares = (sint64)locals.p.shares;
                        locals.xferToQxIn.newManagingContractIndex = QPOOL_QX_CONTRACT_INDEX;
                        INVOKE_OTHER_CONTRACT_PROCEDURE_E(QSWAP, TransferShareManagementRights,
                            locals.xferToQxIn, locals.xferToQxOut, QPOOL_QX_TRANSFER_FEE, xferQxErr);
                        if (xferQxErr != NoCallError
                            || locals.xferToQxOut.transferredNumberOfShares < (sint64)locals.p.shares)
                        {
                            output.returnCode = QPOOL_ERR_TRANSFER_FAILED; return;
                        }
                    }
                }
                locals.qxAskIn.issuer = locals.h.asset.issuer;
                locals.qxAskIn.assetName = locals.h.asset.assetName;
                locals.qxAskIn.price = (sint64)locals.p.price;
                locals.qxAskIn.numberOfShares = (sint64)locals.p.shares;
                INVOKE_OTHER_CONTRACT_PROCEDURE_E(QX, AddToAskOrder,
                    locals.qxAskIn, locals.qxAskOut, 0, askErr);
                if (askErr != NoCallError) { output.returnCode = QPOOL_ERR_EXTERNAL_CALL; return; }
                locals.h.shares = (locals.h.shares > locals.p.shares)
                    ? locals.h.shares - locals.p.shares : 0;
                state.mut().holdings.set(locals.p.holdingId, locals.h);
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
        output.returnCode = QPOOL_OK;
    }

    // Refresh a holding's share count against on-chain reality. Needed after an
    // async QX fill or an IPO settlement, where shares arrive without any call
    // into this contract. Permissionless.
    PUBLIC_PROCEDURE_WITH_LOCALS(ReconcileHolding)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.holdingId >= QPOOL_MAX_HOLDINGS) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.costBasis == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }

        // BUGFIX: qpi.numberOfShares() reports QPOOL's TOTAL holdings of this
        // asset, shared across every holding that targets it -- see the
        // AssetKey comment above SettleIpo's equivalent fix. Draw only the
        // unclaimed remainder against the running per-asset ledger, capped
        // at this holding's own pendingShares (its expected fill ceiling),
        // so repeated calls are idempotent and safe when another holding of
        // the same asset is reconciled/settled in between.
        locals.held = qpi.numberOfShares(locals.h.asset,
            AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
        if (locals.held < 0) locals.held = 0;
        locals.ak.issuer = locals.h.asset.issuer;
        locals.ak.assetName = locals.h.asset.assetName;
        locals.alreadyAttributed = 0;
        state.get().assetSharesAttributed.get(locals.ak, locals.alreadyAttributed);
        locals.unclaimed = ((uint64)locals.held > locals.alreadyAttributed)
            ? (uint64)locals.held - locals.alreadyAttributed : 0;
        locals.newClaim = (locals.unclaimed < locals.h.pendingShares) ? locals.unclaimed : locals.h.pendingShares;
        if (locals.newClaim > 0)
        {
            locals.h.shares = locals.h.shares + locals.newClaim;
            locals.h.pendingShares = locals.h.pendingShares - locals.newClaim;
            state.mut().assetSharesAttributed.set(locals.ak, locals.alreadyAttributed + locals.newClaim);
            state.mut().holdings.set(input.holdingId, locals.h);
        }
        output.shares = locals.h.shares;
        output.pendingShares = locals.h.pendingShares;
        output.returnCode = QPOOL_OK;
    }

    // ======================== Payouts ========================

    // Claim this member's share of one holding's dividends and sale proceeds.
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
        locals.pending = (uint128)locals.own.cost
            * (locals.h.payoutPerCostX64 - locals.own.payoutDebtX64);
        locals.owed = sadd(locals.own.claimable, locals.pending.high);
        if (locals.owed == 0) { output.returnCode = QPOOL_ERR_NOTHING_OWED; return; }

        // Effects before the transfer.
        locals.own.payoutDebtX64 = locals.h.payoutPerCostX64;
        locals.own.claimable = 0;
        state.mut().ownership.set(locals.ok, locals.own);

        qpi.transfer(qpi.invocator(), (sint64)locals.owed);
        output.paid = locals.owed;
        output.returnCode = QPOOL_OK;
    }

    // Raise the standing IPO bid. Permissionless and needs NO new vote, up to
    // the ceiling the vote already approved — an IPO auction moves far faster
    // than a 3-day vote, so members approve a maximum price once and anyone may
    // defend the position within it. Funds refunded by being outbid land back
    // in ipoAvailable and are reusable here.
    //
    // bidInIPO enforces the IPO epoch window itself, returning -1 outside it,
    // so no epoch check is needed (and none is possible: the contract cannot
    // read constructionEpoch).
    PUBLIC_PROCEDURE_WITH_LOCALS(RebidIpo)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.holdingId >= QPOOL_MAX_HOLDINGS || input.price == 0 || input.quantity == 0)
        {
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.active == 0 || locals.h.ipoContractIndex == 0)
        {
            output.returnCode = QPOOL_ERR_IPO_NOT_ACTIVE; return;
        }
        // BUGFIX: there is no vote-approved ceiling -- a real auction has no
        // upper bound, only a current high price that a competing bid can
        // always exceed. The previous check here rejected any price above
        // ipoMaxPrice, but ipoMaxPrice is set by Execute to exactly the
        // first bid's own price (see BUY_IPO below), so it was never
        // possible to raise a bid at all. The only real limit is budget,
        // enforced by the ipoAvailable check just below. ipoMaxPrice is
        // updated after a successful bid instead, purely as a running
        // high-water mark for members to read via GetHolding.
        locals.cost = input.price * input.quantity;
        if (locals.cost == 0 || locals.cost > locals.h.ipoAvailable)
        {
            output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
        }

        // The earlier bid is NOT replaced — it stays in the book until outbid —
        // so the new price must be distinct from every price already live, or
        // the two bids' slots become indistinguishable.
        if (locals.h.ipoBidCount >= QPOOL_MAX_IPO_BIDS)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        for (locals.j = 0; locals.j < locals.h.ipoBidCount; locals.j++)
        {
            if (locals.h.ipoBidPrices.get(locals.j) == input.price)
            {
                output.returnCode = QPOOL_ERR_IPO_PRICE_TAKEN; return;
            }
        }
        locals.ipoResult = qpi.bidInIPO((uint32)locals.h.ipoContractIndex,
            (sint64)input.price, (uint32)input.quantity);
        if (locals.ipoResult < 0) { output.returnCode = QPOOL_ERR_IPO_REJECTED; return; }

        locals.h.ipoAvailable = locals.h.ipoAvailable - locals.cost;
        locals.h.ipoAtRisk = sadd(locals.h.ipoAtRisk, locals.cost);
        locals.h.ipoBidPrices.set(locals.h.ipoBidCount, input.price);
        locals.h.ipoBidCount = locals.h.ipoBidCount + 1;
        locals.h.pendingShares = sadd(locals.h.pendingShares, (uint64)locals.ipoResult);
        if (input.price > locals.h.ipoMaxPrice) locals.h.ipoMaxPrice = input.price;

        // BUGFIX #7: a higher-priced rebid can evict units of this SAME
        // holding's own earlier (lower) tranche -- not just other holdings'
        // bids -- and unlike an eviction by someone else, this self-eviction
        // is invisible to a LATER, separate ReconcileIpo call by anyone else,
        // since nobody but this holding's own owner has a reason to call it
        // again right now. If it's never reconciled before the window closes,
        // ipoAtRisk keeps carrying that old tranche's FULL value bundled
        // together with the new tranche's real, still-seated value -- and
        // SettleIpo's window-closed handling (which trusts ipoAtRisk as a
        // single, uniform "may be refundable" number, see its own comment)
        // has no way to separate the two after the fact. Confirmed live
        // (cascading multi-pool IPO competition test): this let a holding
        // that won 300 real shares get its ENTIRE holding wiped out at
        // settlement (shares zeroed, costBasis driven to 0) while members
        // received a phantom full refund of money that was actually spent on
        // those now-unaccounted-for real shares. Fix: reconcile against the
        // LIVE book immediately, right here, the same way a standalone
        // ReconcileIpo call would -- the window is definitely still open
        // (the bid above just succeeded), so the book is still readable, and
        // any of THIS holding's own tranches that the new bid just evicted
        // get moved from ipoAtRisk to ipoAvailable right away, before it's
        // ever possible for the window to close on a stale number.
        locals.seated = 0;
        locals.shouldBeAtRisk = 0;
        for (locals.i = 0; locals.i < (uint64)NUMBER_OF_COMPUTORS; locals.i++)
        {
            if (qpi.ipoBidId((uint32)locals.h.ipoContractIndex, (uint32)locals.i) != SELF) continue;
            locals.price = qpi.ipoBidPrice((uint32)locals.h.ipoContractIndex, (uint32)locals.i);
            if (locals.price < 0) continue;
            for (locals.j = 0; locals.j < locals.h.ipoBidCount; locals.j++)
            {
                if ((uint64)locals.price != locals.h.ipoBidPrices.get(locals.j)) continue;
                locals.seated = locals.seated + 1;
                locals.shouldBeAtRisk = sadd(locals.shouldBeAtRisk, (uint64)locals.price);
                break;
            }
        }
        if (locals.shouldBeAtRisk <= locals.h.ipoAtRisk)
        {
            // Never invent money: only ever move DOWN to match reality,
            // exactly like ReconcileIpo's own equivalent guard.
            locals.freed = locals.h.ipoAtRisk - locals.shouldBeAtRisk;
            if (locals.freed > 0)
            {
                if (locals.freed > state.get().ipoRefundEscrow) locals.freed = state.get().ipoRefundEscrow;
                if (locals.freed > 0)
                {
                    state.mut().ipoRefundEscrow = state.get().ipoRefundEscrow - locals.freed;
                    locals.h.ipoAtRisk = locals.h.ipoAtRisk - locals.freed;
                    locals.h.ipoAvailable = sadd(locals.h.ipoAvailable, locals.freed);
                }
            }
            locals.h.pendingShares = locals.seated;
        }
        state.mut().holdings.set(input.holdingId, locals.h);

        output.bidsRegistered = (uint64)locals.ipoResult;
        output.atRisk = locals.h.ipoAtRisk;
        output.available = locals.h.ipoAvailable;
        output.returnCode = QPOOL_OK;
    }

    // Reconcile one IPO bid against the LIVE order book. Permissionless.
    //
    // This is what makes concurrent IPO bids possible. Refunds arrive with no
    // IPO identity, so instead of guessing an owner on arrival they pool in
    // ipoRefundEscrow, and ground truth is read back here: qpi.ipoBidId and
    // qpi.ipoBidPrice expose all 676 slots, so the contract can count exactly
    // how many slots it still holds at THIS bid's price. Slots at price P
    // belong to the bid placed at price P, which is why Propose refuses a price
    // already live on the same IPO.
    //
    // Anything no longer seated has been refunded, so it moves from at-risk to
    // available and can be re-bid straight away.
    PUBLIC_PROCEDURE_WITH_LOCALS(ReconcileIpo)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.holdingId >= QPOOL_MAX_HOLDINGS) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.active == 0 || locals.h.ipoContractIndex == 0 || locals.h.ipoBidCount == 0)
        {
            output.returnCode = QPOOL_ERR_IPO_NOT_ACTIVE; return;
        }
        // BUGFIX: once the IPO window closes, qpi.ipoBidId() stops returning
        // SELF for ANY slot (some other sentinel instead, for every holding,
        // not just this one) -- so the id-matching loop below would find
        // "seated=0" for a holding checked after its window closes, EVEN ONE
        // THAT ACTUALLY WON REAL SHARES, because the mismatch makes it
        // `continue` past every slot before ever reaching the `price < 0`
        // check a few lines down -- that check can only ever fire for a slot
        // where the id DID match, which can no longer happen once the window
        // is closed, making it dead code. Confirmed live (comprehensive
        // multi-pool cascade test): this let a real winning holding's entire
        // ipoAtRisk get treated as evicted, refunding members their FULL
        // original contribution via SettleIpo's leftover-return path while
        // the real shares it won became orphaned -- still owned by SELF on
        // the real asset ledger, but unclaimed by any holding, since
        // assetSharesAttributed's pendingShares safety ceiling (0, from this
        // same corruption) capped its settlement at zero. Check the book's
        // readability directly and unconditionally first, exactly the same
        // way SettleIpo already does for the identical purpose.
        if (qpi.ipoBidPrice((uint32)locals.h.ipoContractIndex, 0) < 0)
        {
            output.returnCode = QPOOL_ERR_IPO_NOT_ACTIVE; return;
        }

        // Sum slots still held by SELF across EVERY price this holding has live.
        // Valuing each slot at its own price is what keeps at-risk correct when
        // an older bid is still seated alongside a newer one.
        locals.seated = 0;
        locals.shouldBeAtRisk = 0;
        for (locals.i = 0; locals.i < (uint64)NUMBER_OF_COMPUTORS; locals.i++)
        {
            if (qpi.ipoBidId((uint32)locals.h.ipoContractIndex, (uint32)locals.i) != SELF) continue;
            locals.price = qpi.ipoBidPrice((uint32)locals.h.ipoContractIndex, (uint32)locals.i);
            if (locals.price < 0)
            {
                // Negative means the contract is no longer in its IPO epoch, so
                // the book cannot be read. SettleIpo handles it from here.
                output.returnCode = QPOOL_ERR_IPO_NOT_ACTIVE; return;
            }
            for (locals.j = 0; locals.j < locals.h.ipoBidCount; locals.j++)
            {
                if ((uint64)locals.price != locals.h.ipoBidPrices.get(locals.j)) continue;
                locals.seated = locals.seated + 1;
                locals.shouldBeAtRisk = sadd(locals.shouldBeAtRisk, (uint64)locals.price);
                break;
            }
        }
        if (locals.shouldBeAtRisk > locals.h.ipoAtRisk)
        {
            // More seated than we believe we funded: never invent money.
            output.seatedShares = locals.seated;
            output.atRisk = locals.h.ipoAtRisk;
            output.available = locals.h.ipoAvailable;
            output.returnCode = QPOOL_OK; return;
        }

        locals.freed = locals.h.ipoAtRisk - locals.shouldBeAtRisk;
        if (locals.freed > 0)
        {
            // Draw the freed amount out of the shared refund escrow. Clamped, so
            // a holding can never claim refunds that did not arrive.
            if (locals.freed > state.get().ipoRefundEscrow) locals.freed = state.get().ipoRefundEscrow;
            if (locals.freed > 0)
            {
                state.mut().ipoRefundEscrow = state.get().ipoRefundEscrow - locals.freed;
                locals.h.ipoAtRisk = locals.h.ipoAtRisk - locals.freed;
                locals.h.ipoAvailable = sadd(locals.h.ipoAvailable, locals.freed);
            }
        }
        locals.h.pendingShares = locals.seated;
        state.mut().holdings.set(input.holdingId, locals.h);

        output.seatedShares = locals.seated;
        output.atRisk = locals.h.ipoAtRisk;
        output.available = locals.h.ipoAvailable;
        output.recovered = locals.freed;
        output.returnCode = QPOOL_OK;
    }

    // Close out an IPO position once the auction has resolved. Reconciles the
    // shares actually won and hands any unspent or refunded QU back to the
    // members who funded the attempt, shrinking their cost basis in step so
    // costBasis always equals QU actually spent. If nothing was won the holding
    // is retired entirely and the members simply get their money back, leaving
    // them free to fund the next attempt.
    PUBLIC_PROCEDURE_WITH_LOCALS(SettleIpo)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.holdingId >= QPOOL_MAX_HOLDINGS) { output.returnCode = QPOOL_ERR_INVALID_INPUT; return; }
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.ipoContractIndex == 0 || locals.h.costBasis == 0)
        {
            output.returnCode = QPOOL_ERR_IPO_NOT_ACTIVE; return;
        }
        // Everything must be off the table: either refunded or converted to
        // shares. While QU is still at risk the auction is still running.
        if (locals.h.ipoAtRisk != 0)
        {
            // BUGFIX: ReconcileIpo can only ever detect capital that was
            // refunded (evicted from the book) -- a WON slot stays "seated"
            // forever, since finishIPOs() (ipo.h) never clears the book, so
            // ipoAtRisk never reaches 0 through ReconcileIpo alone for a
            // holding that wins any shares. The instant system.epoch moves
            // past the IPO window, qpi.ipoBidId/ipoBidPrice start returning
            // NULL_ID/negative for every slot, so ReconcileIpo can't act on
            // it either (see its own comment: "SettleIpo handles it from
            // here" -- that handoff was never actually implemented). Once
            // the window is confirmed closed, whatever ipoAtRisk still shows
            // is necessarily resolved one way or another: either it became
            // real shares (qpi.numberOfShares below reflects that) or it was
            // already refunded via ipoBidRefund into ipoRefundEscrow, so it
            // is safe to treat it as settled here.
            if (qpi.ipoBidPrice((uint32)locals.h.ipoContractIndex, 0) >= 0)
            {
                // Window still open: the auction could still turn against
                // this holding, so refuse exactly as before.
                output.returnCode = QPOOL_ERR_WINDOW_OPEN; return;
            }
            // BUGFIX: a uniform-clearing-price auction can refund part of
            // ipoAtRisk even for a WON slot (finishIPOs() pays back the gap
            // between what was bid and the real clearing price), and a
            // totally failed auction refunds all of it -- but the book
            // never reflects either: ipo->prices[i] stays at the original
            // bid, finishIPOs() never rewrites it down, so this money was
            // never visible to ReconcileIpo's book-comparison at any point,
            // not even in the instant before the window closed. It is real
            // and safe (it already landed in the shared ipoRefundEscrow via
            // POST_INCOMING_TRANSFER's ipoBidRefund handling), just never
            // credited back to the holding that funded it. Refunds carry no
            // identity, so this can only be a best-effort draw against the
            // shared pot -- correct whenever this is the only holding with
            // an outstanding refund there (the common case), potentially
            // short if another holding's settlement is drawing on the same
            // pot in the same tick (bounded by whatever it actually
            // contains, per the sadd/clamp below, exactly like
            // ReconcileIpo's own equivalent draw). Crediting it to
            // ipoAvailable here (before leftover is computed just below)
            // means the existing pro-rata return-to-members logic picks it
            // up with no further changes needed.
            //
            // RELIES ON: ipoAtRisk staying an accurate reflection of only
            // currently-seated (or already-explicitly-reconciled) value at
            // all times -- see RebidIpo's own self-reconciliation, added for
            // exactly this reason (BUGFIX #7 below).
            locals.freed = locals.h.ipoAtRisk;
            if (locals.freed > state.get().ipoRefundEscrow) locals.freed = state.get().ipoRefundEscrow;
            if (locals.freed > 0)
            {
                state.mut().ipoRefundEscrow = state.get().ipoRefundEscrow - locals.freed;
                locals.h.ipoAvailable = sadd(locals.h.ipoAvailable, locals.freed);
            }
            locals.h.ipoAtRisk = 0;
        }

        // BUGFIX: qpi.numberOfShares() reports QPOOL's TOTAL holdings of this
        // asset, shared across every holding that targets it (see the
        // AssetKey comment above) -- draw only this holding's own remainder
        // against the running per-asset attribution ledger, capped at what
        // this holding could itself have won (pendingShares is a safe
        // ceiling even if stale, since it can only ever have been reduced by
        // an eviction, never inflated beyond what was actually bid).
        locals.held = qpi.numberOfShares(locals.h.asset,
            AssetOwnershipSelect::byOwner(SELF), AssetPossessionSelect::byPossessor(SELF));
        if (locals.held < 0) locals.held = 0;
        locals.ak.issuer = locals.h.asset.issuer;
        locals.ak.assetName = locals.h.asset.assetName;
        locals.alreadyAttributed = 0;
        state.get().assetSharesAttributed.get(locals.ak, locals.alreadyAttributed);
        locals.myShare = ((uint64)locals.held > locals.alreadyAttributed)
            ? (uint64)locals.held - locals.alreadyAttributed : 0;
        if (locals.myShare > locals.h.pendingShares) locals.myShare = locals.h.pendingShares;
        locals.h.shares = locals.myShare;
        state.mut().assetSharesAttributed.set(locals.ak, locals.alreadyAttributed + locals.myShare);
        locals.h.pendingShares = 0;
        locals.leftover = locals.h.ipoAvailable;

        locals.returned = 0;
        if (locals.leftover > 0)
        {
            // Hand back pro-rata and shrink each member's cost by the same
            // fraction, so the ownership denominator keeps matching real spend.
            locals.pool = state.get().pools.get(locals.h.poolId);
            locals.base = locals.h.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL;
            for (locals.i = 0; locals.i < locals.pool.contributorCount; locals.i++)
            {
                locals.ok.member = state.get().members.get(locals.base + locals.i);
                locals.ok.holdingId = input.holdingId;
                if (!state.get().ownership.get(locals.ok, locals.own)) continue;
                if (locals.own.cost == 0) continue;
                locals.refundEach = div((uint128)locals.own.cost * (uint128)locals.leftover,
                    (uint128)locals.h.costBasis).low;
                if (locals.refundEach == 0) continue;
                if (locals.refundEach > locals.own.cost) locals.refundEach = locals.own.cost;

                locals.own.cost = locals.own.cost - locals.refundEach;
                state.mut().ownership.set(locals.ok, locals.own);

                locals.ck.member = locals.ok.member;
                locals.ck.poolId = locals.h.poolId;
                locals.c.uncommitted = 0; locals.c.committed = 0; locals.c.lockedForVotes = 0;
                state.get().contributions.get(locals.ck, locals.c);
                locals.c.uncommitted = sadd(locals.c.uncommitted, locals.refundEach);
                locals.c.committed = (locals.c.committed > locals.refundEach)
                    ? locals.c.committed - locals.refundEach : 0;
                state.mut().contributions.set(locals.ck, locals.c);
                locals.returned = sadd(locals.returned, locals.refundEach);
            }
            locals.pool.uncommittedTotal = sadd(locals.pool.uncommittedTotal, locals.returned);
            locals.pool.committedTotal = (locals.pool.committedTotal > locals.returned)
                ? locals.pool.committedTotal - locals.returned : 0;
            state.mut().pools.set(locals.h.poolId, locals.pool);

            locals.h.ipoAvailable = (locals.leftover > locals.returned)
                ? locals.leftover - locals.returned : 0;
            locals.h.costBasis = (locals.h.costBasis > locals.returned)
                ? locals.h.costBasis - locals.returned : 0;
        }

        // Nothing won, or nothing left funding it: retire the holding so the
        // pool may vote a fresh attempt.
        if (locals.h.shares == 0 || locals.h.costBasis == 0)
        {
            locals.h.active = 0;
        }
        state.mut().holdings.set(input.holdingId, locals.h);

        output.shares = locals.h.shares;
        output.returnedToMembers = locals.returned;
        output.returnCode = QPOOL_OK;
    }

    // ======================== Syndication ========================

    // Deposit an asset you already own into a pool and receive 100% of the
    // resulting holding's seats. You may then sell any fraction of those seats
    // for QU via OfferPosition, and buyers share every future dividend on the
    // asset pro-rata — without the pool ever having to buy anything.
    //
    // The shares must already sit under this contract's management (transfer
    // them with QX TransferShareManagementRights first), same as QTREAT and
    // IEscrow's general-asset deposits.
    //
    // `seatUnits` is the ownership denominator you are carving the holding into.
    // It is a free choice of granularity, not a valuation: pick 1,000,000 and
    // you can sell seats in millionths. No QU is committed, so this holding is
    // deliberately kept out of pool.committedTotal.
    PUBLIC_PROCEDURE_WITH_LOCALS(DepositHoldingAsset)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.poolId >= QPOOL_MAX_POOLS || input.amount == 0 || input.seatUnits == 0
            || input.asset.issuer == NULL_ID || input.asset.assetName == 0)
        {
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.pool = state.get().pools.get(input.poolId);
        if (locals.pool.active == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }
        if (locals.pool.permissioned != 0 && qpi.invocator() != locals.pool.creator)
        {
            locals.ck.member = qpi.invocator();
            locals.ck.poolId = input.poolId;
            if (!state.get().admitted.contains(locals.ck))
            {
                output.returnCode = QPOOL_ERR_NOT_ADMITTED; return;
            }
        }
        if (locals.pool.holdingCount >= QPOOL_MAX_HOLDINGS_PER_POOL)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        if (state.get().ownership.population() >= QPOOL_MAX_OWNERSHIP)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        for (locals.newHoldingId = 0; locals.newHoldingId < QPOOL_MAX_HOLDINGS; locals.newHoldingId++)
        {
            if (state.get().holdings.get(locals.newHoldingId).costBasis == 0
                && state.get().holdings.get(locals.newHoldingId).active == 0) break;
        }
        if (locals.newHoldingId >= QPOOL_MAX_HOLDINGS) { output.returnCode = QPOOL_ERR_CAPACITY; return; }

        // The depositor must genuinely hold the shares under SC management.
        locals.managed = qpi.numberOfShares(input.asset,
            AssetOwnershipSelect::byOwner(qpi.invocator()),
            AssetPossessionSelect::byPossessor(qpi.invocator()));
        if (locals.managed < (sint64)input.amount)
        {
            output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
        }
        locals.xfer = qpi.transferShareOwnershipAndPossession(
            input.asset.assetName, input.asset.issuer,
            qpi.invocator(), qpi.invocator(), (sint64)input.amount, SELF);
        if (locals.xfer < 0) { output.returnCode = QPOOL_ERR_TRANSFER_FAILED; return; }

        locals.h.poolId = input.poolId;
        locals.h.asset = input.asset;
        locals.h.shares = input.amount;
        locals.h.costBasis = input.seatUnits;
        locals.h.payoutPerCostX64 = uint128(0, 0);
        locals.h.totalPayouts = 0;
        locals.h.deposited = 1;
        locals.h.ipoContractIndex = 0;
        locals.h.ipoMaxPrice = 0;
        locals.h.ipoBidCount = 0;
        locals.h.ipoQuantity = 0;
        locals.h.ipoAtRisk = 0;
        locals.h.ipoAvailable = 0;
        locals.h.pendingShares = 0;
        locals.h.active = 1;
        state.mut().holdings.set(locals.newHoldingId, locals.h);
        if (locals.newHoldingId >= state.get().holdingCount)
        {
            state.mut().holdingCount = locals.newHoldingId + 1;
        }

        // Depositor starts owning every seat.
        locals.ok.member = qpi.invocator();
        locals.ok.holdingId = locals.newHoldingId;
        locals.own.cost = input.seatUnits;
        locals.own.payoutDebtX64 = uint128(0, 0);
        locals.own.claimable = 0;
        if (state.mut().ownership.set(locals.ok, locals.own) == NULL_INDEX)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }

        locals.hbase = input.poolId * QPOOL_MAX_HOLDINGS_PER_POOL;
        state.mut().poolHoldings.set(locals.hbase + locals.pool.holdingCount, locals.newHoldingId);
        locals.pool.holdingCount = locals.pool.holdingCount + 1;

        // Put the depositor on the roster if they are not already, so they can
        // vote on proposals about this pool.
        locals.base = input.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL;
        locals.onRoster = 0;
        for (locals.i = 0; locals.i < locals.pool.contributorCount; locals.i++)
        {
            if (state.get().members.get(locals.base + locals.i) == qpi.invocator()) locals.onRoster = 1;
        }
        if (locals.onRoster == 0 && locals.pool.contributorCount < QPOOL_MAX_CONTRIBUTORS_PER_POOL)
        {
            state.mut().members.set(locals.base + locals.pool.contributorCount, qpi.invocator());
            locals.pool.contributorCount = locals.pool.contributorCount + 1;
            // A member record is needed so Propose recognises them.
            locals.ck.member = qpi.invocator();
            locals.ck.poolId = input.poolId;
            locals.c.uncommitted = 0; locals.c.committed = 0; locals.c.lockedForVotes = 0;
            if (!state.get().contributions.contains(locals.ck))
            {
                state.mut().contributions.set(locals.ck, locals.c);
            }
        }
        state.mut().pools.set(input.poolId, locals.pool);

        // Register the dividend payer for this asset so payouts attribute here.
        if (!state.get().payoutSources.contains(input.asset.issuer)
            && state.get().payoutSources.population() < QPOOL_MAX_SOURCES)
        {
            state.mut().payoutSources.set(input.asset.issuer, locals.newHoldingId);
        }

        output.holdingId = locals.newHoldingId;
        output.returnCode = QPOOL_OK;
    }

    // ======================== Position market (buy-outs) ========================

    // Offer part or all of a position for sale. Not vote-gated: a member's
    // position is their own property, and gating exit behind a vote would let a
    // majority trap them.
    PUBLIC_PROCEDURE_WITH_LOCALS(OfferPosition)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        if (input.holdingId >= QPOOL_MAX_HOLDINGS || input.costUnits == 0 || input.askPrice == 0)
        {
            output.returnCode = QPOOL_ERR_INVALID_INPUT; return;
        }
        locals.h = state.get().holdings.get(input.holdingId);
        if (locals.h.costBasis == 0) { output.returnCode = QPOOL_ERR_NOT_FOUND; return; }
        locals.ok.member = qpi.invocator();
        locals.ok.holdingId = input.holdingId;
        if (!state.get().ownership.get(locals.ok, locals.own) || locals.own.cost < input.costUnits)
        {
            output.returnCode = QPOOL_ERR_INSUFFICIENT_BALANCE; return;
        }
        locals.fk.seller = qpi.invocator();
        locals.fk.holdingId = input.holdingId;
        if (!state.get().offers.contains(locals.fk)
            && state.get().offers.population() >= QPOOL_MAX_OFFERS)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        locals.f.costUnits = input.costUnits;
        locals.f.askPrice = input.askPrice;
        if (state.mut().offers.set(locals.fk, locals.f) == NULL_INDEX)
        {
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }
        output.returnCode = QPOOL_OK;
    }

    PUBLIC_PROCEDURE_WITH_LOCALS(CancelOffer)
    {
        if (qpi.invocationReward() > 0) qpi.transfer(qpi.invocator(), qpi.invocationReward());
        locals.fk.seller = qpi.invocator();
        locals.fk.holdingId = input.holdingId;
        if (!state.get().offers.contains(locals.fk)) { output.returnCode = QPOOL_ERR_NO_OFFER; return; }
        state.mut().offers.removeByKey(locals.fk);
        output.returnCode = QPOOL_OK;
    }

    // Fill an offer: QU goes to the seller, cost units transfer to the buyer.
    // The asset never moves, so the pool's holdings are unchanged — only the
    // seat changes hands. A buyer who was not previously a member joins.
    PUBLIC_PROCEDURE_WITH_LOCALS(BuyPosition)
    {
        if (input.holdingId >= QPOOL_MAX_HOLDINGS || input.seller == NULL_ID
            || input.seller == qpi.invocator())
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
        // Without this, buying a seat would be a back door into a permissioned
        // pool: the buyer would gain ownership and voting weight having never
        // been admitted.
        locals.pool = state.get().pools.get(locals.h.poolId);
        if (locals.pool.permissioned != 0)
        {
            locals.admitKey.member = qpi.invocator();
            locals.admitKey.poolId = locals.h.poolId;
            if (!state.get().admitted.contains(locals.admitKey))
            {
                qpi.transfer(qpi.invocator(), qpi.invocationReward());
                output.returnCode = QPOOL_ERR_NOT_ADMITTED; return;
            }
        }
        locals.sellerKey.member = input.seller;
        locals.sellerKey.holdingId = input.holdingId;
        if (!state.get().ownership.get(locals.sellerKey, locals.sellerOwn)
            || locals.sellerOwn.cost < locals.f.costUnits)
        {
            // Seller no longer holds what they offered; drop the stale offer.
            state.mut().offers.removeByKey(locals.fk);
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_NO_OFFER; return;
        }

        locals.buyerKey.member = qpi.invocator();
        locals.buyerKey.holdingId = input.holdingId;
        locals.buyerOwn.cost = 0;
        locals.buyerOwn.payoutDebtX64 = locals.h.payoutPerCostX64;
        locals.buyerOwn.claimable = 0;
        locals.buyerExisted = state.get().ownership.get(locals.buyerKey, locals.buyerOwn) ? 1 : 0;
        if (locals.buyerExisted == 0 && state.get().ownership.population() >= QPOOL_MAX_OWNERSHIP)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
            output.returnCode = QPOOL_ERR_CAPACITY; return;
        }

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
        state.mut().ownership.set(locals.sellerKey, locals.sellerOwn);
        state.mut().offers.removeByKey(locals.fk);

        // A buyer new to this pool joins the roster so buy votes can weight them.
        locals.pool = state.get().pools.get(locals.h.poolId);
        locals.base = locals.h.poolId * QPOOL_MAX_CONTRIBUTORS_PER_POOL;
        locals.slot = 0;
        for (locals.i = 0; locals.i < locals.pool.contributorCount; locals.i++)
        {
            if (state.get().members.get(locals.base + locals.i) == qpi.invocator()) locals.slot = 1;
        }
        if (locals.slot == 0 && locals.pool.contributorCount < QPOOL_MAX_CONTRIBUTORS_PER_POOL)
        {
            state.mut().members.set(locals.base + locals.pool.contributorCount, qpi.invocator());
            locals.pool.contributorCount = locals.pool.contributorCount + 1;
            state.mut().pools.set(locals.h.poolId, locals.pool);
        }

        if ((uint64)qpi.invocationReward() > locals.f.askPrice)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward() - (sint64)locals.f.askPrice);
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
        REGISTER_USER_PROCEDURE(ReconcileIpo, 15);
        REGISTER_USER_PROCEDURE(DepositHoldingAsset, 16);
        REGISTER_USER_PROCEDURE(SetAdmission, 17);
        REGISTER_USER_PROCEDURE(SetPermissioned, 18);
    }

    INITIALIZE()
    {
        // Nothing to seed: there is no admin, and every pool is created by a
        // user. Deliberately so — no key in this contract can move member funds.
    }

    // Route incoming QU. Dividends and QX fills are attributed to a holding via
    // the payoutSources map; anything unrecognised is parked in the pool's
    // unattributed bucket and needs a vote (KIND_ALLOCATE) to assign, so no QU
    // is ever silently lost or credited to the wrong members.
    struct POST_INCOMING_TRANSFER_locals
    {
        uint64 holdingId; Holding h; Pool pool; uint128 scaled; uint64 back;
        uint64 fee; uint64 net;
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

        // Outbid in an IPO. The refund carries no IPO identity — only this
        // type, with sourceId NULL_ID — which is why only one IPO bid may be
        // outstanding contract-wide. The money returns to ipoAvailable so it
        // can be bid again immediately, WITHOUT touching the payout accumulator:
        // this is returned capital, not a distribution.
        if (input.type == TransferType::ipoBidRefund)
        {
            state.mut().ipoRefundEscrow = sadd(state.get().ipoRefundEscrow, (uint64)input.amount);
            state.mut().totalIpoRefunded = sadd(state.get().totalIpoRefunded, (uint64)input.amount);
            return;
        }

        locals.holdingId = 0;
        if (state.get().payoutSources.get(input.sourceId, locals.holdingId))
        {
            locals.h = state.get().holdings.get(locals.holdingId);
            if (locals.h.costBasis > 0)
            {
                // Management fee: skimmed BEFORE the accumulator advances, so
                // members' entitlements are computed on the net amount.
                locals.fee = div((uint128)(uint64)input.amount * (uint128)QPOOL_MANAGEMENT_PERMILLE,
                    (uint128)1000ULL).low;
                locals.net = (uint64)input.amount - locals.fee;
                if (locals.fee > 0)
                {
                    state.mut().pendingRevenue = sadd(state.get().pendingRevenue, locals.fee);
                    state.mut().totalRevenue = sadd(state.get().totalRevenue, locals.fee);
                    state.mut().totalManagementFees = sadd(state.get().totalManagementFees, locals.fee);
                }
                locals.scaled.high = locals.net;
                locals.scaled.low = 0;
                locals.h.payoutPerCostX64 = locals.h.payoutPerCostX64
                    + div(locals.scaled, (uint128)locals.h.costBasis);
                locals.h.totalPayouts = sadd(locals.h.totalPayouts, locals.net);
                state.mut().holdings.set(locals.holdingId, locals.h);
                state.mut().totalPayoutsDistributed =
                    sadd(state.get().totalPayoutsDistributed, locals.net);

                locals.pool = state.get().pools.get(locals.h.poolId);
                if (locals.pool.active != 0) return;
            }
            else
            {
                locals.pool = state.get().pools.get(locals.h.poolId);
                if (locals.pool.active != 0)
                {
                    locals.pool.unattributedQu = sadd(locals.pool.unattributedQu, (uint64)input.amount);
                    state.mut().pools.set(locals.h.poolId, locals.pool);
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
    };
    END_EPOCH_WITH_LOCALS()
    {
        state.mut().contributions.cleanupIfNeeded();
        state.mut().ownership.cleanupIfNeeded();
        state.mut().votes.cleanupIfNeeded();
        state.mut().offers.cleanupIfNeeded();
        state.mut().payoutSources.cleanupIfNeeded();
        state.mut().admitted.cleanupIfNeeded();
        state.mut().assetSharesAttributed.cleanupIfNeeded();

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

    // Assets bought on QX arrive under this contract's management.
    PRE_ACQUIRE_SHARES()
    {
        output.allowTransfer = true;
        output.requestedFee = 0;
    }
};
