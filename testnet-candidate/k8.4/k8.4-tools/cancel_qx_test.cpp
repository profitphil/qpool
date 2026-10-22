// cancel_qx_test.cpp -- an end-to-end test of CancelQxBid against the REAL QX contract of the scratch core.
//
// Built by build_run.sh with the header under test overlaid as src/contracts/QPool.h.
//   -DQPOOL_HAS_CANCEL=1   the cancel tests (only the CancelQxBid header has the procedure and the GetHolding fields)
//   (not defined)          the base subset: the behaviours of a resting BUY_QX that must be the same in the base and in the CancelQxBid header
//
// The fixture is a trimmed copy of the repo's contract_qpool.cpp fixture (same helper names).
#define NO_UEFI

#include "contract_testing.h"

#include <vector>
#include <cstring>

namespace
{
class QPoolLoggingEnv : public ::testing::Environment
{
public:
    void SetUp() override { __pauseLogMessage(); }
};
::testing::Environment* const qpoolLoggingEnv =
    ::testing::AddGlobalTestEnvironment(new QPoolLoggingEnv);
}

static const id CREATOR = id::randomValue();
static const id ALICE = id::randomValue();
static const id BOB = id::randomValue();
static const id CAROL = id::randomValue();
static const id DAVE = id::randomValue();
static const id OUTSIDER = id::randomValue();
static const id TOKEN_ISSUER = id::randomValue();
static const id QPOOL_ID = id(QPOOL_CONTRACT_INDEX, 0, 0, 0);

static constexpr uint64 POOL_NAME = 1;
static constexpr uint64 TOKEN_NAME = 1413629259ULL; // "KOIN"
static const char KOIN7_BYTES[8] = { 'K', 'E', 'B', 'T', 0, 0, 0, 0 }; // the bytes of TOKEN_NAME

enum QPoolProcIds
{
    QP_CREATE_POOL = 1, QP_CONTRIBUTE = 2, QP_WITHDRAW = 3, QP_PROPOSE = 4, QP_VOTE = 5, QP_TALLY = 6, QP_EXECUTE = 7,
    QP_RECONCILE_HOLDING = 8, QP_CLAIM_PAYOUT = 9, QP_OFFER_POSITION = 10, QP_CANCEL_OFFER = 11, QP_BUY_POSITION = 12,
    QP_CANCEL_QX_BID = 23, QP_SET_POOL_ADMINS = 24, QP_CANCEL_QX_ASK = 25,
};
enum QPoolFuncIds { QP_GET_POOL = 1, QP_GET_MEMBER = 2, QP_GET_HOLDING = 3, QP_GET_POSITION = 4, QP_GET_PROPOSAL = 5, QP_GET_STATS = 6, QP_GET_SOURCE = 7 };

// QPI's uint128 has no default constructor, so a struct that holds one is built zeroed in a buffer.
template <typename X> struct Zeroed { alignas(16) unsigned char b[sizeof(X)]; Zeroed() { memset(b, 0, sizeof(b)); } X& get() { return *(X*)b; } };

class T : protected ContractTesting
{
public:
    T()
    {
        initEmptySpectrum();
        initEmptyUniverse();
        system.epoch = contractDescriptions[QPOOL_CONTRACT_INDEX].constructionEpoch;
        system.tick = 1000000;
        INIT_CONTRACT(QPOOL);
        callSystemProcedure(QPOOL_CONTRACT_INDEX, INITIALIZE);
        __pauseLogMessage();
        INIT_CONTRACT(QX);
        callSystemProcedure(QX_CONTRACT_INDEX, INITIALIZE);
        increaseEnergy(CREATOR, 0); increaseEnergy(ALICE, 0); increaseEnergy(BOB, 0); increaseEnergy(CAROL, 0);
        increaseEnergy(DAVE, 0); increaseEnergy(OUTSIDER, 0); increaseEnergy(TOKEN_ISSUER, 0);
        // the asset: KOIN, issued by TOKEN_ISSUER, QX-managed like a QX fill; 1,000,000 shares in the issuer's own record
        int iss;
        EXPECT_EQ(issueAsset(TOKEN_ISSUER, KOIN7_BYTES, 0, "TEST", 1000000, QX_CONTRACT_INDEX, &iss, &koinOwn_, &koinPos_), 1000000);
    }

    QPOOL::StateData* getState() { return (QPOOL::StateData*)contractStates[QPOOL_CONTRACT_INDEX]; }
    void advanceTicks(uint64 n) { system.tick += (unsigned int)n; }
    Asset token() const { return Asset{ TOKEN_ISSUER, TOKEN_NAME }; }

    // ---- procedures ----
    QPOOL::CreatePool_output createPool(const id& from, uint64 name, uint64 permissioned, uint64 fee = QPOOL_CREATE_POOL_FEE)
    {
        QPOOL::CreatePool_input input{ name, permissioned };
        QPOOL::CreatePool_output output;
        increaseEnergy(from, fee);
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_CREATE_POOL, input, output, from, fee);
        return output;
    }
    QPOOL::Contribute_output contribute(const id& from, uint64 poolId, uint64 amount)
    {
        QPOOL::Contribute_input input{ poolId };
        QPOOL::Contribute_output output;
        increaseEnergy(from, amount);
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_CONTRIBUTE, input, output, from, amount);
        return output;
    }
    QPOOL::Propose_output propose(const id& from, uint64 poolId, uint64 kind, const Asset& asset,
        uint64 quAmount, uint64 shares, uint64 price, uint64 holdingId)
    {
        QPOOL::Propose_input input{ poolId, kind, asset, quAmount, shares, price, holdingId };
        QPOOL::Propose_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_PROPOSE, input, output, from, 0);
        return output;
    }
    QPOOL::Vote_output vote(const id& from, uint64 proposalId, uint64 approve)
    {
        QPOOL::Vote_input input{ proposalId, approve };
        QPOOL::Vote_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_VOTE, input, output, from, 0);
        return output;
    }
    QPOOL::Tally_output tally(const id& from, uint64 proposalId)
    {
        QPOOL::Tally_input input{ proposalId };
        QPOOL::Tally_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_TALLY, input, output, from, 0);
        return output;
    }
    QPOOL::Execute_output execute(const id& from, uint64 proposalId)
    {
        QPOOL::Execute_input input{ proposalId };
        QPOOL::Execute_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_EXECUTE, input, output, from, 0);
        return output;
    }
    uint32 reconcileHolding(const id& from, uint64 holdingId)
    {
        QPOOL::ReconcileHolding_input input{ holdingId };
        QPOOL::ReconcileHolding_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_RECONCILE_HOLDING, input, output, from, 0);
        return output.returnCode;
    }
    uint32 offerPosition(const id& from, uint64 holdingId, uint64 costUnits, uint64 askPrice)
    {
        QPOOL::OfferPosition_input input{ holdingId, costUnits, askPrice };
        QPOOL::OfferPosition_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_OFFER_POSITION, input, output, from, 0);
        return output.returnCode;
    }
#ifdef QPOOL_HAS_CANCEL
    QPOOL::SetPoolAdmins_output setPoolAdmins(const id& from, uint64 poolId, const id& a0, const id& a1, const id& a2, sint64 reward = 0)
    {
        QPOOL::SetPoolAdmins_input input{ poolId, a0, a1, a2 };
        QPOOL::SetPoolAdmins_output output;
        if (reward > 0) increaseEnergy(from, reward);
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_SET_POOL_ADMINS, input, output, from, reward);
        return output;
    }
    QPOOL::CancelQxBid_output cancelQxBid(const id& from, uint64 poolId, uint64 holdingId, sint64 reward = 0)
    {
        QPOOL::CancelQxBid_input input{ poolId, holdingId };
        QPOOL::CancelQxBid_output output;
        if (reward > 0) increaseEnergy(from, reward);
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_CANCEL_QX_BID, input, output, from, reward);
        return output;
    }
#endif

    // ---- functions ----
    QPOOL::GetPool_output getPool(uint64 poolId)
    {
        QPOOL::GetPool_input input{ poolId };
        QPOOL::GetPool_output output;
        callFunction(QPOOL_CONTRACT_INDEX, QP_GET_POOL, input, output);
        return output;
    }
    QPOOL::GetMember_output getMember(uint64 poolId, const id& member)
    {
        QPOOL::GetMember_input input{ poolId, member };
        QPOOL::GetMember_output output;
        callFunction(QPOOL_CONTRACT_INDEX, QP_GET_MEMBER, input, output);
        return output;
    }
    QPOOL::GetHolding_output getHolding(uint64 holdingId)
    {
        QPOOL::GetHolding_input input{ holdingId };
        QPOOL::GetHolding_output output;
        callFunction(QPOOL_CONTRACT_INDEX, QP_GET_HOLDING, input, output);
        return output;
    }
    QPOOL::GetPosition_output getPosition(uint64 holdingId, const id& member)
    {
        QPOOL::GetPosition_input input{ holdingId, member };
        QPOOL::GetPosition_output output;
        callFunction(QPOOL_CONTRACT_INDEX, QP_GET_POSITION, input, output);
        return output;
    }
    QPOOL::GetStats_output getStats()
    {
        QPOOL::GetStats_input input;
        QPOOL::GetStats_output output;
        callFunction(QPOOL_CONTRACT_INDEX, QP_GET_STATS, input, output);
        return output;
    }
    QPOOL::GetSource_output getSource(const id& source, uint64 kind)
    {
        QPOOL::GetSource_input input{ source, kind };
        QPOOL::GetSource_output output;
        callFunction(QPOOL_CONTRACT_INDEX, QP_GET_SOURCE, input, output);
        return output;
    }

    // ---- QX ----
    int koinOwn_ = -1, koinPos_ = -1;
    void giveKoin(const id& to, uint64 n)
    {
        int dOwn, dPos;
        ASSERT_TRUE(transferShareOwnershipAndPossession(koinOwn_, koinPos_, to, (long long)n, &dOwn, &dPos, true));
    }
    sint64 qxAsk(const id& from, const Asset& a, sint64 price, sint64 n)
    {
        QX::AddToAskOrder_input in{ a.issuer, a.assetName, price, n };
        QX::AddToAskOrder_output out;
        invokeUserProcedure(QX_CONTRACT_INDEX, 5, in, out, from, 0);
        return out.addedNumberOfShares;
    }
    // Shares resting on QX bids of `a` at exactly `price`, whoever placed them.
    sint64 qxBidsAt(const Asset& a, sint64 price)
    {
        QX::AssetBidOrders_input in;
        in.issuer = a.issuer; in.assetName = a.assetName; in.offset = 0;
        QX::AssetBidOrders_output out;
        callFunction(QX_CONTRACT_INDEX, 3, in, out);
        sint64 sum = 0;
        for (uint64 i = 0; i < 256; i++)
        {
            auto o = out.orders.get(i);
            if (o.numberOfShares <= 0) break;
            if (o.price == price) sum += o.numberOfShares;
        }
        return sum;
    }
    // All shares resting on QX bids of QPOOL (any price), for any asset.
    sint64 qxBidsOfQpool(const Asset& a)
    {
        QX::AssetBidOrders_input in;
        in.issuer = a.issuer; in.assetName = a.assetName; in.offset = 0;
        QX::AssetBidOrders_output out;
        callFunction(QX_CONTRACT_INDEX, 3, in, out);
        sint64 sum = 0;
        for (uint64 i = 0; i < 256; i++)
        {
            auto o = out.orders.get(i);
            if (o.numberOfShares <= 0) break;
            if (o.entity == QPOOL_ID) sum += o.numberOfShares;
        }
        return sum;
    }
    sint64 qpoolHolds(const Asset& a)
    {
        return numberOfShares(a, AssetOwnershipSelect::byOwner(QPOOL_ID), AssetPossessionSelect::byPossessor(QPOOL_ID));
    }

    // ---- pools ----
    // Pool 6040: ALICE 600,000 and BOB 400,000 uncommitted; the creator is CREATOR.
    uint64 poolWith6040()
    {
        auto c = createPool(CREATOR, POOL_NAME, 0);
        EXPECT_EQ(c.returnCode, QPOOL_OK);
        EXPECT_EQ(contribute(ALICE, c.poolId, 600000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
        EXPECT_EQ(contribute(BOB, c.poolId, 400000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
        return c.poolId;
    }
    // A second pool created by DAVE: CAROL 300,000, DAVE 200,000.
    uint64 secondPool()
    {
        auto c = createPool(DAVE, POOL_NAME + 1, 0);
        EXPECT_EQ(c.returnCode, QPOOL_OK);
        EXPECT_EQ(contribute(CAROL, c.poolId, 300000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
        EXPECT_EQ(contribute(DAVE, c.poolId, 200000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
        return c.poolId;
    }
    void passFor(uint64 proposalId, const id& m1, const id& m2)
    {
        EXPECT_EQ(vote(m1, proposalId, 1).returnCode, QPOOL_OK);
        EXPECT_EQ(vote(m2, proposalId, 1).returnCode, QPOOL_OK);
        advanceTicks(QPOOL_VOTE_WINDOW + 1);
        auto tl = tally(CREATOR, proposalId);
        EXPECT_EQ(tl.status, QPOOL_PROP_PASSED);
    }
    // Propose a BUY_QX of KOIN from `proposer`, pass it with m1 and m2, execute it. Returns the Execute output.
    QPOOL::Execute_output buyQx(uint64 poolId, const id& proposer, const id& m1, const id& m2,
        uint64 quAmount, uint64 shares, uint64 price, const id& executor = CREATOR)
    {
        advanceTicks(1);        // money added at the tick a proposal opens has no vote on it (lastDepositTick >= openTick)
        auto p = propose(proposer, poolId, QPOOL_KIND_BUY_QX, token(), quAmount, shares, price, 0);
        EXPECT_EQ(p.returnCode, QPOOL_OK);
        passFor(p.proposalId, m1, m2);
        return execute(executor, p.proposalId);        // K8.1: only the pool's creator or an admin
    }

    // ---- more procedures ----
    QPOOL::ClaimPayout_output claimPayout(const id& from, uint64 holdingId)
    {
        QPOOL::ClaimPayout_input input{ holdingId };
        QPOOL::ClaimPayout_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_CLAIM_PAYOUT, input, output, from, 0);
        return output;
    }
    // The buyer names the parcel (cost units and the holding's shares as the offer last saw them); `pay` is attached.
    QPOOL::BuyPosition_output buyPosition(const id& from, uint64 holdingId, const id& seller, uint64 expectedCostUnits,
        uint64 expectedShares, uint64 pay)
    {
        QPOOL::BuyPosition_input input{ holdingId, seller, expectedCostUnits, expectedShares };
        QPOOL::BuyPosition_output output;
        increaseEnergy(from, pay);
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_BUY_POSITION, input, output, from, pay);
        return output;
    }
    sint64 qxBid(const id& from, const Asset& a, sint64 price, sint64 n)
    {
        increaseEnergy(from, price * n);
        QX::AddToBidOrder_input in{ a.issuer, a.assetName, price, n };
        QX::AddToBidOrder_output out;
        invokeUserProcedure(QX_CONTRACT_INDEX, 6, in, out, from, price * n);
        return out.addedNumberOfShares;
    }
    // ---- TESTEXB plays a dividend-paying contract whose shares QPOOL can buy on QX (as in the repo's suite) ----
    static id payerId() { return id(TESTEXB_CONTRACT_INDEX, 0, 0, 0); }
    static uint64 payerAssetName() { return *(uint64*)contractDescriptions[TESTEXB_CONTRACT_INDEX].assetName; }
    static Asset payerShares() { return Asset{ NULL_ID, payerAssetName() }; }
    void initPayerWithHolders(std::vector<std::pair<m256i, unsigned int>> holders)
    {
        INIT_CONTRACT(TESTEXB);
        callSystemProcedure(TESTEXB_CONTRACT_INDEX, INITIALIZE);
        // K8: a contract above the embedded table is trusted only once the REGISTRY has paired its name; seed that pair as BEGIN_EPOCH would have.
        getState()->contractNames.set(payerAssetName(), (uint64)TESTEXB_CONTRACT_INDEX);
        getState()->indexNames.set((uint64)TESTEXB_CONTRACT_INDEX, payerAssetName());
        issueContractShares(TESTEXB_CONTRACT_INDEX, holders, false);
    }
    bool payerDistributes(sint64 perShare, const id& from)
    {
        TESTEXB::QpiDistributeDividends_input input{ perShare };
        TESTEXB::QpiDistributeDividends_output output;
        sint64 fee = perShare * NUMBER_OF_COMPUTORS;
        increaseEnergy(from, fee);
        return invokeUserProcedure(TESTEXB_CONTRACT_INDEX, 21, input, output, from, fee);
    }
    // A BUY_QX of any asset (contract shares carry the paying contract's index in holdingId).
    QPOOL::Execute_output buyQxOf(const Asset& a, uint64 holdingIdField, uint64 poolId, const id& proposer, const id& m1, const id& m2,
        uint64 quAmount, uint64 shares, uint64 price, const id& executor = CREATOR)
    {
        advanceTicks(1);
        auto p = propose(proposer, poolId, QPOOL_KIND_BUY_QX, a, quAmount, shares, price, holdingIdField);
        EXPECT_EQ(p.returnCode, QPOOL_OK);
        passFor(p.proposalId, m1, m2);
        return execute(executor, p.proposalId);
    }

    // The record of the resting bid as the LEDGER holds it (GetHolding shows it only for a holding that has pendingShares).
#ifdef QPOOL_HAS_CANCEL
    // K8.2: the ledger's list of resting bids (priority order). Returns false when the ledger is gone.
    bool ledgerBids(const id& source, uint64 kind, uint64& count, std::vector<uint64>& ids)
    {
        QPOOL::SourceKey k; k.source = source; k.kind = kind;
        Zeroed<QPOOL::SourceLedger> ledZ; QPOOL::SourceLedger& led = ledZ.get();
        if (!getState()->sources.get(k, led)) return false;
        count = led.qxCount;
        ids.clear();
        for (uint64 i = 0; i < led.qxCount && i < QPOOL_MAX_QX_BIDS_PER_LEDGER; i++) ids.push_back(led.qxHold.get(i));
        return true;
    }
#endif

#ifdef QPOOL_HAS_K83
    // ---- K8.3: resting sells ----
    QPOOL::CancelQxAsk_output cancelQxAsk(const id& from, uint64 poolId, uint64 holdingId, sint64 reward = 0)
    {
        QPOOL::CancelQxAsk_input input{ poolId, holdingId };
        QPOOL::CancelQxAsk_output output;
        if (reward > 0) increaseEnergy(from, reward);
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_CANCEL_QX_ASK, input, output, from, reward);
        return output;
    }
    // Propose a SELL_QX_LIMIT of KOIN on holding `holdingId`, pass it with m1 and m2, execute it.
    QPOOL::Execute_output sellLimitOf(const Asset& a, uint64 poolId, const id& proposer, const id& m1, const id& m2,
        uint64 holdingId, uint64 shares, uint64 price, const id& executor = CREATOR)
    {
        advanceTicks(1);
        auto p = propose(proposer, poolId, QPOOL_KIND_SELL_QX_LIMIT, a, 0, shares, price, holdingId);
        EXPECT_EQ(p.returnCode, QPOOL_OK);
        passFor(p.proposalId, m1, m2);
        return execute(executor, p.proposalId);
    }
    QPOOL::Execute_output sellLimit(uint64 poolId, const id& proposer, const id& m1, const id& m2,
        uint64 holdingId, uint64 shares, uint64 price, const id& executor = CREATOR)
    {
        return sellLimitOf(token(), poolId, proposer, m1, m2, holdingId, shares, price, executor);
    }
    // An immediate SELL_QX (min proceeds `minQu`) on holding `holdingId`.
    QPOOL::Execute_output sellNowOf(const Asset& a, uint64 poolId, const id& proposer, const id& m1, const id& m2,
        uint64 holdingId, uint64 shares, uint64 price, uint64 minQu, const id& executor = CREATOR)
    {
        advanceTicks(1);
        auto p = propose(proposer, poolId, QPOOL_KIND_SELL_QX, a, minQu, shares, price, holdingId);
        EXPECT_EQ(p.returnCode, QPOOL_OK);
        passFor(p.proposalId, m1, m2);
        return execute(executor, p.proposalId);
    }
    // Shares QPOOL has resting on QX asks of `a` (all prices) and at exactly `price` (-1: all prices).
    sint64 qxAsksOfQpool(const Asset& a, sint64 price = -1)
    {
        QX::EntityAskOrders_input in; in.entity = QPOOL_ID; in.offset = 0;
        QX::EntityAskOrders_output out;
        callFunction(QX_CONTRACT_INDEX, 4, in, out);
        sint64 sum = 0;
        for (uint64 i = 0; i < 256; i++)
        {
            auto o = out.orders.get(i);
            if (o.numberOfShares <= 0) break;
            if (o.issuer == a.issuer && o.assetName == a.assetName && (price < 0 || o.price == price)) sum += o.numberOfShares;
        }
        return sum;
    }
    // Owed to a pool's seat owners on a holding (ALICE+BOB for pool A, CAROL+DAVE for pool B).
    uint64 owedA(uint64 hx) { return getPosition(hx, ALICE).owed + getPosition(hx, BOB).owed; }
    uint64 owedB(uint64 hx) { return getPosition(hx, CAROL).owed + getPosition(hx, DAVE).owed; }
#endif

#ifdef QPOOL_HAS_K83
    // A second asset of TOKEN_ISSUER (QX-managed, 1,000 shares in the issuer's record) with the given 4-letter name; returns its name value.
    uint64 issueSecond(const char* four)
    {
        char bytes[8] = { four[0], four[1], four[2], four[3], 0, 0, 0, 0 };
        int iss, o2, p2;
        EXPECT_EQ(issueAsset(TOKEN_ISSUER, bytes, 0, "TEST", 1000, QX_CONTRACT_INDEX, &iss, &o2, &p2), 1000);
        uint64 name = 0; memcpy(&name, bytes, 8);
        second_[0] = o2; second_[1] = p2;
        return name;
    }
    int second_[2] = { -1, -1 };
    void giveSecond(const id& to, uint64 n)
    {
        int dOwn, dPos;
        ASSERT_TRUE(transferShareOwnershipAndPossession(second_[0], second_[1], to, (long long)n, &dOwn, &dPos, true));
    }
    void advanceEpochs(unsigned n) { system.epoch += n; }
    uint32 evictSquat(const id& from, uint64 holdingId, uint64 rivalName)
    {
        QPOOL::EvictSquat_input in{ holdingId, rivalName };
        QPOOL::EvictSquat_output out;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, 21, in, out, from, 0);
        return out.returnCode;
    }
    void markDeposited(uint64 hx) { auto h = getState()->holdings.get(hx); h.deposited = 1; getState()->holdings.set(hx, h); }
#endif

#ifdef QPOOL_HAS_K83
    // K8.4: the whole ledger of a payer (for fields the Get functions do not show).
    QPOOL::SourceLedger ledgerRaw(const id& source, uint64 kind)
    {
        QPOOL::SourceKey k; k.source = source; k.kind = kind;
        Zeroed<QPOOL::SourceLedger> ledZ; QPOOL::SourceLedger& led = ledZ.get();
        getState()->sources.get(k, led);
        return led;
    }
    // A stranger gives shares to QPOOL through QX (a donation: no order of QPOOL stands behind them).
    void qxDonate(const id& from, const Asset& a, sint64 n)
    {
        const sint64 reward = 10000000;                                                   // QX keeps its transfer fee and hands the rest back
        increaseEnergy(from, reward);
        QX::TransferShareOwnershipAndPossession_input in{ a.issuer, QPOOL_ID, a.assetName, n };
        QX::TransferShareOwnershipAndPossession_output out;
        invokeUserProcedure(QX_CONTRACT_INDEX, 2, in, out, from, reward);
        EXPECT_EQ(out.transferredNumberOfShares, n);
    }
    // K8.4: the ledger's list of resting ASKS (lowest price first). Returns false when the ledger is gone.
    bool ledgerAsks(const id& source, uint64 kind, uint64& count, std::vector<uint64>& ids)
    {
        QPOOL::SourceKey k; k.source = source; k.kind = kind;
        Zeroed<QPOOL::SourceLedger> ledZ; QPOOL::SourceLedger& led = ledZ.get();
        if (!getState()->sources.get(k, led)) return false;
        count = led.askCount;
        ids.clear();
        for (uint64 i = 0; i < led.askCount && i < QPOOL_MAX_QX_BIDS_PER_LEDGER; i++) ids.push_back(led.askHold.get(i));
        return true;
    }
#endif

    // ---- conservation ----
    // What QPOOL's balance must be, from its own buckets: members' uncommitted QU, QPOOL revenue not yet paid out, the counter of arrivals that
    // belong to nobody, the escrows and the pots. (No payouts are claimable in these tests.)
    sint64 bucketsSum()
    {
        auto s = getStats();
        sint64 sum = 0;
        for (uint64 p = 0; p < s.poolCount; p++) sum += (sint64)getPool(p).uncommittedTotal;
        sum += (sint64)s.pendingRevenue + (sint64)s.globalUnattributed + (sint64)s.dividendEscrowTotal
            + (sint64)s.ipoRefundEscrow + (sint64)s.qpayBuyPot;
        return sum;
    }
    // QPOOL's balance minus its buckets: 0 when every QU is accounted for (QU sitting on QX in a resting bid is not in the balance).
    sint64 unaccounted() { return getBalance(QPOOL_ID) - bucketsSum(); }

    // ---- whole-state snapshots (a refused call must leave every byte of the state as it was) ----
    std::vector<unsigned char> snapshot()
    {
        const size_t n = sizeof(QPOOL::StateData);
        std::vector<unsigned char> v(n);
        memcpy(v.data(), contractStates[QPOOL_CONTRACT_INDEX], n);
        return v;
    }
    bool sameAs(const std::vector<unsigned char>& v)
    {
        return memcmp(v.data(), contractStates[QPOOL_CONTRACT_INDEX], v.size()) == 0;
    }
};

// ======================== the base subset: a resting BUY_QX before any cancel ========================

// An empty ask book: the whole bid rests. The ledger counts it as pending and blocks a second BUY_QX on the asset; ReconcileHolding credits a fill.
TEST(CancelQxBidBase, RestingBidBlocksTheLedgerUntilItFills)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.giveKoin(OUTSIDER, 300);
    sint64 unacc0 = t.unaccounted();
    EXPECT_EQ(unacc0, 0) << "the buckets do not add up to QPOOL's balance before any bid (test formula)";

    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    EXPECT_EQ(e.acquired, 0ULL);
    EXPECT_EQ(e.refunded, 50000ULL);                 // the excess over 100 x 1,000
    uint64 hx = e.holdingId;
    auto h = t.getHolding(hx);
    EXPECT_EQ(h.shares, 0ULL);
    EXPECT_EQ(h.pendingShares, 100ULL);
    EXPECT_EQ(h.costBasis, 100000ULL);
    auto src = t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN);
    EXPECT_EQ(src.found, 1u);
    EXPECT_EQ(src.pendingQx, 100ULL);
    EXPECT_EQ(t.qxBidsAt(t.token(), 1000), 100);     // the bid rests on QX
    EXPECT_EQ(t.unaccounted(), 0);                   // the 100,000 locked on QX left the balance and the uncommitted total together

    // K8.2: a second pool's BUY_QX on the same asset is NOT blocked any more.
    EXPECT_EQ(t.propose(CAROL, pb, QPOOL_KIND_BUY_QX, t.token(), 100000, 50, 1500, 0).returnCode, QPOOL_OK);

    // OUTSIDER sells all 100 into the resting bid (at the bid's own price, 1,000): QPOOL notices at ReconcileHolding.
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 100), 100);
    EXPECT_EQ(t.qpoolHolds(t.token()), 100);
    EXPECT_EQ(t.getHolding(hx).shares, 0ULL);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);
    h = t.getHolding(hx);
    EXPECT_EQ(h.shares, 100ULL);
    EXPECT_EQ(h.pendingShares, 0ULL);
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).pendingQx, 0ULL);
    EXPECT_EQ(t.propose(CAROL, pb, QPOOL_KIND_BUY_QX, t.token(), 100000, 50, 1500, 0).returnCode, QPOOL_ERR_PROPOSAL_PENDING);   // the one above is still live
    EXPECT_EQ(t.unaccounted(), 0);
}

// ======================== the cancel tests ========================
#ifdef QPOOL_HAS_CANCEL

static void expectBookRecord(T& t, uint64 hx, uint64 price, uint64 tick, const id& source = TOKEN_ISSUER, uint64 kind = QPOOL_SOURCE_TOKEN)
{
    auto h = t.getHolding(hx);
    if (price != 0)
    {
        EXPECT_EQ(h.qxBidPrice, price);
        EXPECT_EQ(h.qxBidTick, tick);
    }
    // the ledger itself (a ledger that is gone lists no bid): the holding is in its list exactly when a bid rests
    uint64 cnt = 99; std::vector<uint64> ids;
    if (t.ledgerBids(source, kind, cnt, ids))
    {
        bool in = false;
        for (auto x : ids) if (x == hx) in = true;
        EXPECT_EQ(in, price != 0);
    }
}

// 1. A resting bid with an empty ask book records its price and tick; a second pool's BUY_QX is refused; the creator cancels; the funders are
//    refunded to the cent; the holding and the ledger are gone; the second pool can now buy; QU is conserved throughout.
TEST(CancelQxBid, CreatorCancelsRestingBidAndTheLedgerIsFree)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    ASSERT_EQ(t.unaccounted(), 0);
    sint64 balBefore = getBalance(QPOOL_ID);
    auto poolBefore = t.getPool(pa);
    EXPECT_EQ(poolBefore.uncommittedTotal, 1000000ULL);

    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    unsigned int tickAtExecute = system.tick;
    uint64 hx = e.holdingId;
    EXPECT_EQ(t.getHolding(hx).pendingShares, 100ULL);
    EXPECT_EQ(t.getHolding(hx).costBasis, 100000ULL);
    expectBookRecord(t, hx, 1000, tickAtExecute);
    EXPECT_EQ(t.getPosition(hx, ALICE).cost, 60000ULL);
    EXPECT_EQ(t.getPosition(hx, BOB).cost, 40000ULL);
    EXPECT_EQ(t.getMember(pa, ALICE).uncommitted, 540000ULL);
    EXPECT_EQ(t.getMember(pa, BOB).uncommitted, 360000ULL);
    EXPECT_EQ(t.getPool(pa).uncommittedTotal, 900000ULL);
    EXPECT_EQ(t.getPool(pa).committedTotal, 100000ULL);
    EXPECT_EQ(t.unaccounted(), 0);
    EXPECT_EQ(t.propose(DAVE, pb, QPOOL_KIND_BUY_QX, t.token(), 100000, 50, 1500, 0).returnCode, QPOOL_OK);       // K8.2: the second pool is not blocked

    // The creator of the pool cancels (not a member with a seat: he never paid anything).
    auto c = t.cancelQxBid(CREATOR, pa, hx);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.cancelledShares, 100ULL);
    EXPECT_EQ(c.refunded, 100000ULL);
    EXPECT_EQ(c.pendingLeft, 0ULL);
    EXPECT_EQ(t.qxBidsOfQpool(t.token()), 0);        // nothing rests on QX any more
    // The funders have their QU back, exactly as before the bid.
    EXPECT_EQ(t.getMember(pa, ALICE).uncommitted, 600000ULL);
    EXPECT_EQ(t.getMember(pa, BOB).uncommitted, 400000ULL);
    EXPECT_EQ(t.getMember(pa, ALICE).committed, 0ULL);
    EXPECT_EQ(t.getMember(pa, BOB).committed, 0ULL);
    auto poolAfter = t.getPool(pa);
    EXPECT_EQ(poolAfter.uncommittedTotal, 1000000ULL);
    EXPECT_EQ(poolAfter.committedTotal, 0ULL);
    EXPECT_EQ(poolAfter.holdingCount, 0ULL);
    EXPECT_EQ(poolAfter.ownershipRows, 0ULL);
    // The holding's slot is free, its ledger (and with it the record of the bid) is gone.
    EXPECT_EQ(t.getHolding(hx).found, 0u);
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).found, 0u);
    // Conservation: the balance is what it was before the bid, and every QU is in a bucket.
    EXPECT_EQ(getBalance(QPOOL_ID), balBefore);
    EXPECT_EQ(t.unaccounted(), 0);
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);

    // The second pool can now buy (and the buy rests in its turn: record set again).
    auto e2 = t.buyQx(pb, CAROL, CAROL, DAVE, 100000, 50, 1500, DAVE);   // DAVE created pool B
    ASSERT_EQ(e2.returnCode, QPOOL_OK);
    EXPECT_EQ(e2.holdingId, hx);                     // the freed slot is taken again
    EXPECT_EQ(t.getHolding(e2.holdingId).pendingShares, 50ULL);
    expectBookRecord(t, e2.holdingId, 1500, system.tick);
    EXPECT_EQ(t.unaccounted(), 0);
    // K8.2: and the first pool is not blocked either
    EXPECT_EQ(t.propose(ALICE, pa, QPOOL_KIND_BUY_QX, t.token(), 100000, 50, 1500, 0).returnCode, QPOOL_OK);
}

// 2. Who may cancel, and when: a funder at once; the creator of ANOTHER pool, a member of another pool and a stranger are refused until the bid is
//    older than QPOOL_QX_BID_MAX_AGE_TICKS (strictly), and then anybody may; the refund still goes to the funders; a refusal writes nothing.
TEST(CancelQxBid, AuthorisationAndTheAgeLimit)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    EXPECT_EQ(t.unaccounted(), 0);

    auto snap = t.snapshot();
    sint64 outsiderBal = getBalance(OUTSIDER);
    EXPECT_EQ(t.cancelQxBid(OUTSIDER, pa, hx).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    EXPECT_EQ(t.cancelQxBid(CAROL, pa, hx).returnCode, QPOOL_ERR_NOT_AUTHORIZED);   // a member of the other pool
    EXPECT_EQ(t.cancelQxBid(DAVE, pa, hx).returnCode, QPOOL_ERR_NOT_AUTHORIZED);    // the creator of the OTHER pool
    EXPECT_TRUE(t.sameAs(snap)) << "a refused cancel wrote to the state";
    // Attached QU comes back on a refusal (the fixture hands the caller the 777 before the call, so he ends up with exactly them).
    sint64 before = getBalance(OUTSIDER);
    EXPECT_EQ(t.cancelQxBid(OUTSIDER, pa, hx, 777).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    EXPECT_EQ(getBalance(OUTSIDER), before + 777);
    outsiderBal = getBalance(OUTSIDER);
    EXPECT_TRUE(t.sameAs(snap));
    // Wrong pool for the holding, bad numbers.
    EXPECT_EQ(t.cancelQxBid(CREATOR, pb, hx).returnCode, QPOOL_ERR_NOT_FOUND);
    EXPECT_EQ(t.cancelQxBid(CREATOR, QPOOL_MAX_POOLS, hx).returnCode, QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.cancelQxBid(CREATOR, pa, QPOOL_MAX_HOLDINGS).returnCode, QPOOL_ERR_INVALID_INPUT);
    EXPECT_TRUE(t.sameAs(snap));

    // Exactly the limit: still refused. One tick more: anybody may.
    unsigned int placed = (unsigned int)t.getHolding(hx).qxBidTick;
    ASSERT_EQ(placed, system.tick);
    t.advanceTicks(QPOOL_QX_BID_MAX_AGE_TICKS);
    ASSERT_EQ((uint64)(system.tick - placed), QPOOL_QX_BID_MAX_AGE_TICKS);
    EXPECT_EQ(t.cancelQxBid(OUTSIDER, pa, hx).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    EXPECT_TRUE(t.sameAs(snap));
    t.advanceTicks(1);
    auto c = t.cancelQxBid(OUTSIDER, pa, hx);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.cancelledShares, 100ULL);
    EXPECT_EQ(c.refunded, 100000ULL);
    EXPECT_EQ(getBalance(OUTSIDER), outsiderBal);    // the caller got nothing
    EXPECT_EQ(t.getMember(pa, ALICE).uncommitted, 600000ULL);
    EXPECT_EQ(t.getMember(pa, BOB).uncommitted, 400000ULL);
    EXPECT_EQ(t.unaccounted(), 0);
    EXPECT_EQ(t.getHolding(hx).found, 0u);
    // a cancel of what is gone
    EXPECT_EQ(t.cancelQxBid(CREATOR, pa, hx).returnCode, QPOOL_ERR_NOT_FOUND);
}

// 2b. A funder (BOB, who voted yes and was charged) cancels at once; a member of the pool who was NOT charged (a late joiner) may not.
TEST(CancelQxBid, OnlyOperatorsCancelAtOnceAnAdminNeedsNoSeat)
{
    T t;
    uint64 pa = t.poolWith6040();
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    ASSERT_EQ(t.contribute(CAROL, pa, 50000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);   // joins the pool after the bid: no seat in it
    EXPECT_EQ(t.cancelQxBid(CAROL, pa, hx).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    EXPECT_EQ(t.cancelQxBid(BOB, pa, hx).returnCode, QPOOL_ERR_NOT_AUTHORIZED);     // K8.1: a funder is not an operator
    ASSERT_EQ(t.setPoolAdmins(CREATOR, pa, CAROL, NULL_ID, NULL_ID).returnCode, QPOOL_OK);   // an admin needs no seat
    auto c = t.cancelQxBid(CAROL, pa, hx);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.refunded, 100000ULL);
    EXPECT_EQ(t.getMember(pa, ALICE).uncommitted, 600000ULL);
    EXPECT_EQ(t.getMember(pa, BOB).uncommitted, 400000ULL);
    EXPECT_EQ(t.getMember(pa, CAROL).uncommitted, 50000ULL);
    EXPECT_EQ(t.unaccounted(), 0);
}

// 3. A partial fill before the cancel: some shares filled at once (at a better price than the limit), some filled silently later, the rest is
//    cancelled. The fills are credited by the same call, the refund is exactly price x shares cancelled, the costs shrink, the holding lives on.
TEST(CancelQxBid, PartialFillThenCancelCreditsTheFillsAndRefundsTheRest)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 300);
    t.giveKoin(CAROL, 300);
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 1000, 60), 60);               // 60 for sale at 1,000
    // 100 at up to 1,500: 60 fill at once (at 1,000), 40 rest at 1,500.
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 200000, 100, 1500);
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    EXPECT_EQ(e.acquired, 60ULL);
    EXPECT_EQ(e.refunded, 80000ULL);
    EXPECT_EQ(t.getHolding(hx).shares, 60ULL);
    EXPECT_EQ(t.getHolding(hx).pendingShares, 40ULL);
    EXPECT_EQ(t.getHolding(hx).costBasis, 120000ULL);
    EXPECT_EQ(t.getPosition(hx, ALICE).cost, 72000ULL);
    EXPECT_EQ(t.getPosition(hx, BOB).cost, 48000ULL);
    EXPECT_EQ(t.unaccounted(), 0);
    unsigned int placed = system.tick;
    expectBookRecord(t, hx, 1500, placed);

    // CAROL sells 10 into the resting bid: 10 more filled, silently.
    ASSERT_EQ(t.qxAsk(CAROL, t.token(), 1400, 10), 10);
    EXPECT_EQ(t.qpoolHolds(t.token()), 70);
    EXPECT_EQ(t.getHolding(hx).shares, 60ULL);                           // not noticed yet
    EXPECT_EQ(t.qxBidsAt(t.token(), 1500), 30);
    // ALICE offers part of her seat: the cancel shrinks every seat, so the offer must not survive it.
    ASSERT_EQ(t.offerPosition(ALICE, hx, 10000, 5000), QPOOL_OK);
    EXPECT_EQ(t.getPosition(hx, ALICE).offeredCostUnits, 10000ULL);
    EXPECT_EQ(t.getPool(pa).offerCount, 1ULL);

    sint64 balBefore = getBalance(QPOOL_ID);
    auto c = t.cancelQxBid(CREATOR, pa, hx);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.cancelledShares, 30ULL);                                  // 40 - the 10 that filled
    EXPECT_EQ(c.refunded, 45000ULL);                                      // 30 x 1,500
    EXPECT_EQ(c.pendingLeft, 0ULL);
    EXPECT_EQ(getBalance(QPOOL_ID), balBefore + 45000);
    EXPECT_EQ(t.qxBidsOfQpool(t.token()), 0);
    auto h = t.getHolding(hx);
    EXPECT_EQ(h.found, 1u);
    EXPECT_EQ(h.shares, 70ULL);                                           // 60 + the 10 credited by the cancel's own reconcile
    EXPECT_EQ(h.pendingShares, 0ULL);
    EXPECT_EQ(h.costBasis, 75000ULL);                                     // 120,000 - 45,000
    EXPECT_EQ(h.active, 1ULL);
    expectBookRecord(t, hx, 0, 0);                                        // the record is cleared
    auto src = t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN);
    EXPECT_EQ(src.pendingQx, 0ULL);
    EXPECT_EQ(src.recognized, 70ULL);
    // 45,000 split 72,000 : 48,000 by cost = 27,000 : 18,000; costs shrink by the same
    EXPECT_EQ(t.getPosition(hx, ALICE).cost, 45000ULL);
    EXPECT_EQ(t.getPosition(hx, BOB).cost, 30000ULL);
    EXPECT_EQ(t.getMember(pa, ALICE).uncommitted, 600000ULL - 72000ULL + 27000ULL);
    EXPECT_EQ(t.getMember(pa, BOB).uncommitted, 400000ULL - 48000ULL + 18000ULL);
    EXPECT_EQ(t.getMember(pa, ALICE).committed, 45000ULL);
    EXPECT_EQ(t.getMember(pa, BOB).committed, 30000ULL);
    EXPECT_EQ(t.getPool(pa).committedTotal, 75000ULL);
    EXPECT_EQ(t.getPosition(hx, ALICE).offeredCostUnits, 0ULL);          // the open offer was withdrawn
    EXPECT_EQ(t.getPool(pa).offerCount, 0ULL);
    EXPECT_EQ(t.unaccounted(), 0);
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);
    // The ledger is free: another BUY_QX can be proposed.
    EXPECT_EQ(t.propose(BOB, pa, QPOOL_KIND_BUY_QX, t.token(), 100000, 50, 1500, 0).returnCode, QPOOL_OK);
}

// 4. The bid has already filled completely when the cancel arrives: nothing rests, NO_PENDING_BID, NOTHING is written (not even the fills);
//    ReconcileHolding then credits them and clears the record.
TEST(CancelQxBid, FullyFilledBidIsNothingToCancel)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 300);
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 100), 100);              // the whole bid fills, silently
    EXPECT_EQ(t.qxBidsOfQpool(t.token()), 0);
    auto snap = t.snapshot();
    sint64 bal = getBalance(QPOOL_ID);
    auto c = t.cancelQxBid(CREATOR, pa, hx, 5000);
    EXPECT_EQ(c.returnCode, QPOOL_ERR_NO_PENDING_BID);
    EXPECT_EQ(c.cancelledShares, 0ULL);
    EXPECT_EQ(c.refunded, 0ULL);
    EXPECT_TRUE(t.sameAs(snap)) << "NO_PENDING_BID wrote to the state";
    EXPECT_EQ(getBalance(QPOOL_ID), bal);                                 // the 5,000 attached came back
    // A call that follows the fill: a cancel after the reconcile finds no pending bid either.
    ASSERT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).shares, 100ULL);
    EXPECT_EQ(t.getHolding(hx).pendingShares, 0ULL);
    expectBookRecord(t, hx, 0, 0);
    EXPECT_EQ(t.cancelQxBid(CREATOR, pa, hx).returnCode, QPOOL_ERR_NO_PENDING_BID);
    // A holding that never had a bid: NO_PENDING_BID as well.
    EXPECT_EQ(t.unaccounted(), 0);
}

// 5. QX holds no such order (the books say 50 rest, QX has 40): the call fails with EXTERNAL_CALL and writes NOTHING, no QU moves.
TEST(CancelQxBid, WhenQxRemovesNothingNothingIsWritten)
{
    T t;
    uint64 pa = t.poolWith6040();
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 40, 1000);
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    ASSERT_EQ(t.getHolding(hx).pendingShares, 40ULL);
    // Corrupt the books on purpose: 50 pending in the holding and in the ledger, 40 on QX.
    auto h = t.getState()->holdings.get(hx);
    h.pendingShares = 50;
    t.getState()->holdings.set(hx, h);
    QPOOL::SourceKey k; k.source = TOKEN_ISSUER; k.kind = QPOOL_SOURCE_TOKEN;
    Zeroed<QPOOL::SourceLedger> ledZ; QPOOL::SourceLedger& led = ledZ.get();
    ASSERT_TRUE(t.getState()->sources.get(k, led));
    led.pendingQx = 50;
    t.getState()->sources.set(k, led);
    auto snap = t.snapshot();
    sint64 bal = getBalance(QPOOL_ID);
    auto c = t.cancelQxBid(CREATOR, pa, hx, 321);
    EXPECT_EQ(c.returnCode, QPOOL_ERR_EXTERNAL_CALL);
    EXPECT_EQ(c.cancelledShares, 0ULL);
    EXPECT_EQ(c.refunded, 0ULL);
    EXPECT_TRUE(t.sameAs(snap)) << "a failed cancel wrote to the state";
    EXPECT_EQ(getBalance(QPOOL_ID), bal);
    EXPECT_EQ(t.qxBidsAt(t.token(), 1000), 40);                           // QX untouched
}

// 6. Two owners with uneven seats and a rounding remainder: the refund is split by cost units with a running total, adds up exactly, nobody is
//    refunded more than his cost; a seat bought on the position market gets the refund (the current owner of the seat).
TEST(CancelQxBid, RefundSplitIsExactAndFollowsTheSeats)
{
    T t;
    uint64 pa = t.poolWith6040();
    // 7 shares at 1,001: 7,007 locked; costs 60:40 of 7,007 = 4,204 / 2,803 (running-total rounding)
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 20000, 7, 1001);
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    EXPECT_EQ(t.getHolding(hx).costBasis, 7007ULL);
    uint64 ca = t.getPosition(hx, ALICE).cost, cb = t.getPosition(hx, BOB).cost;
    EXPECT_EQ(ca + cb, 7007ULL);
    uint64 ua = t.getMember(pa, ALICE).uncommitted, ub = t.getMember(pa, BOB).uncommitted;
    auto c = t.cancelQxBid(CREATOR, pa, hx);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.refunded, 7007ULL);
    // the whole cost goes back: each owner gets exactly his cost
    EXPECT_EQ(t.getMember(pa, ALICE).uncommitted, ua + ca);
    EXPECT_EQ(t.getMember(pa, BOB).uncommitted, ub + cb);
    EXPECT_EQ(t.unaccounted(), 0);
}

// 7. The CONTRACT ledger kind is not tested here (the base subset has no contract-shares fixture); see the report.

// 8. The record cycles: bid, cancel, bid again at another price (the record follows), partial fill keeps the original tick.
TEST(CancelQxBid, TheRecordFollowsTheBidAndSurvivesAPartialFill)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 300);
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    unsigned int placed = system.tick;
    expectBookRecord(t, hx, 1000, placed);
    t.advanceTicks(5000);
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 25), 25);                 // 25 of 100 fill
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);                // ReconcileHolding credits them; the bid still rests
    EXPECT_EQ(t.getHolding(hx).shares, 25ULL);
    EXPECT_EQ(t.getHolding(hx).pendingShares, 75ULL);
    expectBookRecord(t, hx, 1000, placed);                                // price and ORIGINAL tick kept
    auto c = t.cancelQxBid(CREATOR, pa, hx);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.cancelledShares, 75ULL);
    EXPECT_EQ(c.refunded, 75000ULL);
    expectBookRecord(t, hx, 0, 0);
    EXPECT_EQ(t.getHolding(hx).shares, 25ULL);
    EXPECT_EQ(t.getHolding(hx).costBasis, 25000ULL);
    // new bid at another price: the record follows
    auto e2 = t.buyQx(pa, BOB, ALICE, BOB, 100000, 30, 2000);
    ASSERT_EQ(e2.returnCode, QPOOL_OK);
    expectBookRecord(t, e2.holdingId, 2000, system.tick);
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).pendingQx, 30ULL);
    EXPECT_EQ(t.unaccounted(), 0);
}

// 9. A holding that has shares and a resting remainder, where everything that rests is cancelled and the holding is sold out of its shares
//    meanwhile is not constructed here; the sold-out retire path is exercised by SELL tests of the repo suite, not by this file.


// 10. A CONTRACT ledger (contract shares bought on QX), with a partial silent fill and dividends that the hook set aside for it: the cancel does
//     what ReconcileHolding does (credits the fills and the escrow) and then shrinks the costs. Twin run: the same events with a ReconcileHolding
//     before the cancel must end in exactly the same books; and the dividends that follow split 60:40 on the smaller costs.
struct ContractRunResult
{
    uint64 aliceOwed, bobOwed, costBasis, aliceCost, bobCost, shares, pending, escrow, recognized, pendingQx, pendingRevenue, escrowTotal;
    uint64 aliceUncommitted, bobUncommitted, committedTotal, uncommittedTotal;
    uint64 cancelled, refunded;
    uint64 bound;
    sint64 unacc;
    uint64 aliceOwed2, bobOwed2;
};
static void runContractScenario(bool reconcileFirst, ContractRunResult& r)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.initPayerWithHolders({ { OUTSIDER, 300 }, { CAROL, 376 } });
    const Asset shares = T::payerShares();
    const id payer = T::payerId();
    const uint64 CTR = TESTEXB_CONTRACT_INDEX;
    ASSERT_EQ(t.qxAsk(OUTSIDER, shares, 1000, 60), 60);
    auto e = t.buyQxOf(shares, CTR, pa, ALICE, ALICE, BOB, 200000, 100, 1500);       // 60 fill at 1,000, 40 rest at 1,500
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    ASSERT_EQ(t.getHolding(hx).pendingShares, 40ULL);
    ASSERT_EQ(t.getHolding(hx).costBasis, 120000ULL);
    expectBookRecord(t, hx, 1500, system.tick, payer, QPOOL_SOURCE_CONTRACT);
    auto src = t.getSource(payer, QPOOL_SOURCE_CONTRACT);
    EXPECT_EQ(src.pendingQx, 40ULL);
    ASSERT_EQ(t.qxAsk(CAROL, shares, 1400, 15), 15);                                  // 15 more fill, silently
    ASSERT_TRUE(t.payerDistributes(1800, OUTSIDER));                                  // QPOOL holds 75 of the 676: 135,000 arrives
    src = t.getSource(payer, QPOOL_SOURCE_CONTRACT);
    EXPECT_GT(src.escrow, 0ULL);                                                       // set aside for the 15 not yet recognised
    if (reconcileFirst) EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);

    auto c = t.cancelQxBid(CREATOR, pa, hx);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    r.cancelled = c.cancelledShares; r.refunded = c.refunded;
    EXPECT_EQ(c.cancelledShares, 25ULL);
    EXPECT_EQ(c.refunded, 37500ULL);                                                   // 25 x 1,500
    auto h = t.getHolding(hx);
    r.shares = h.shares; r.pending = h.pendingShares; r.costBasis = h.costBasis;
    r.aliceCost = t.getPosition(hx, ALICE).cost; r.bobCost = t.getPosition(hx, BOB).cost;
    r.aliceOwed = t.getPosition(hx, ALICE).owed; r.bobOwed = t.getPosition(hx, BOB).owed;
    src = t.getSource(payer, QPOOL_SOURCE_CONTRACT);
    r.escrow = src.escrow; r.recognized = src.recognized; r.pendingQx = src.pendingQx;
    auto st = t.getStats();
    r.pendingRevenue = st.pendingRevenue; r.escrowTotal = st.dividendEscrowTotal;
    r.aliceUncommitted = t.getMember(pa, ALICE).uncommitted; r.bobUncommitted = t.getMember(pa, BOB).uncommitted;
    r.committedTotal = t.getPool(pa).committedTotal; r.uncommittedTotal = t.getPool(pa).uncommittedTotal;
    uint64 bound = 0;
    t.getState()->contractNames.get(shares.assetName, bound);
    r.bound = bound;
    r.unacc = t.unaccounted() - (sint64)(r.aliceOwed + r.bobOwed);
    expectBookRecord(t, hx, 0, 0, payer, QPOOL_SOURCE_CONTRACT);
    EXPECT_EQ(t.qxBidsOfQpool(shares), 0);
    // The dividends that follow split on the smaller costs.
    ASSERT_TRUE(t.payerDistributes(1000, OUTSIDER));                                  // 75,000 to QPOOL
    r.aliceOwed2 = t.getPosition(hx, ALICE).owed; r.bobOwed2 = t.getPosition(hx, BOB).owed;
}

TEST(CancelQxBid, ContractLedgerWithEscrowMatchesReconcileThenCancel)
{
    ContractRunResult a, b;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    runContractScenario(false, a);        // cancel alone: it reconciles by itself
    runContractScenario(true, b);         // ReconcileHolding first, then the cancel
    EXPECT_EQ(a.shares, 75ULL); EXPECT_EQ(a.pending, 0ULL);
    EXPECT_EQ(a.costBasis, 120000ULL - 37500ULL);
    EXPECT_EQ(a.recognized, 75ULL); EXPECT_EQ(a.pendingQx, 0ULL); EXPECT_EQ(a.escrow, 0ULL); EXPECT_EQ(a.escrowTotal, 0ULL);
    EXPECT_EQ(a.bound, (uint64)TESTEXB_CONTRACT_INDEX);                // the ledger and its name binding live on
    // 37,500 split 72,000 : 48,000 by cost = 22,500 : 15,000 (exact here); the costs shrink by the same
    EXPECT_EQ(a.aliceCost, 72000ULL - 22500ULL);
    EXPECT_EQ(a.bobCost, 48000ULL - 15000ULL);
    EXPECT_EQ(a.aliceUncommitted, 600000ULL - 72000ULL + 22500ULL);
    EXPECT_EQ(a.bobUncommitted, 400000ULL - 48000ULL + 15000ULL);
    EXPECT_EQ(a.committedTotal, 82500ULL);
    EXPECT_EQ(a.uncommittedTotal, 1000000ULL - 82500ULL);
    EXPECT_GE(a.unacc, 0); EXPECT_LE(a.unacc, 3);                      // payout floor dust only
    // twin
    EXPECT_EQ(a.shares, b.shares); EXPECT_EQ(a.pending, b.pending); EXPECT_EQ(a.costBasis, b.costBasis);
    EXPECT_EQ(a.aliceCost, b.aliceCost); EXPECT_EQ(a.bobCost, b.bobCost);
    EXPECT_EQ(a.aliceOwed, b.aliceOwed); EXPECT_EQ(a.bobOwed, b.bobOwed);
    EXPECT_EQ(a.pendingRevenue, b.pendingRevenue);
    EXPECT_EQ(a.aliceUncommitted, b.aliceUncommitted); EXPECT_EQ(a.bobUncommitted, b.bobUncommitted);
    EXPECT_EQ(a.committedTotal, b.committedTotal);
    EXPECT_EQ(a.aliceOwed2, b.aliceOwed2); EXPECT_EQ(a.bobOwed2, b.bobOwed2);
    // the second dividend (75,000 gross, 74,250 net on 75 recognised shares, all of them on this holding) goes 60:40 on the costs left
    uint64 d1a = a.aliceOwed2 - a.aliceOwed, d1b = a.bobOwed2 - a.bobOwed;
    EXPECT_GE(d1a + d1b, 74250ULL - 3); EXPECT_LE(d1a + d1b, 74250ULL);
    EXPECT_NEAR((double)d1a, 74250.0 * 0.6, 3.0);
    EXPECT_NEAR((double)d1b, 74250.0 * 0.4, 3.0);
}

// 11. The holding sold out of its shares while its bid still rests (a SELL_QX), then the bid is cancelled: nothing is left to hold, so the
//     holding is RETIRED (not freed: the sale's proceeds are still owed), its costs shrink by the refund after what the seats earned has been
//     crystallised, the claims pay out the proceeds in full, and the last claim frees the slot and the pool's committed capital.
TEST(CancelQxBid, SoldOutHoldingWithARestingBidIsRetiredAndKeepsItsClaims)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 300);
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 1000, 60), 60);
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 200000, 100, 1500);       // 60 fill at 1,000; 40 rest at 1,500
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    ASSERT_EQ(t.getHolding(hx).shares, 60ULL);
    ASSERT_EQ(t.getHolding(hx).pendingShares, 40ULL);
    // somebody bids 60 at 1,600 (above the pool's own bid), and the pool sells its 60 shares into it
    ASSERT_EQ(t.qxBid(CAROL, t.token(), 1600, 60), 60);
    t.advanceTicks(1);
    auto sp = t.propose(ALICE, pa, QPOOL_KIND_SELL_QX, t.token(), 90000, 60, 1500, hx);
    ASSERT_EQ(sp.returnCode, QPOOL_OK);
    t.passFor(sp.proposalId, ALICE, BOB);
    auto se = t.execute(CREATOR, sp.proposalId);
    ASSERT_EQ(se.returnCode, QPOOL_OK);
    EXPECT_EQ(se.sold, 60ULL);
    auto h = t.getHolding(hx);
    EXPECT_EQ(h.shares, 0ULL);
    EXPECT_EQ(h.pendingShares, 40ULL);
    EXPECT_EQ(h.costBasis, 120000ULL);
    uint64 proceeds = h.totalPayouts;
    EXPECT_GT(proceeds, 90000ULL);
    EXPECT_EQ(t.qxBidsAt(t.token(), 1500), 40);                       // the pool's bid still rests
    EXPECT_EQ(t.getHolding(hx).qxBidPrice, 1500ULL);
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).recognized, 0ULL);

    auto c = t.cancelQxBid(CREATOR, pa, hx);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.cancelledShares, 40ULL);
    EXPECT_EQ(c.refunded, 60000ULL);
    h = t.getHolding(hx);
    EXPECT_EQ(h.found, 1u);                                           // retired, not freed
    EXPECT_EQ(h.active, 0ULL);
    EXPECT_EQ(h.shares, 0ULL);
    EXPECT_EQ(h.pendingShares, 0ULL);
    EXPECT_EQ(h.costBasis, 60000ULL);
    EXPECT_EQ(h.rows, 2ULL);
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).found, 0u);   // its ledger went with its last holding
    EXPECT_EQ(t.getPool(pa).committedTotal, 60000ULL);
    EXPECT_EQ(t.getPool(pa).holdingCount, 1ULL);
    EXPECT_EQ(t.getMember(pa, ALICE).uncommitted, 600000ULL - 72000ULL + 36000ULL);
    EXPECT_EQ(t.getMember(pa, BOB).uncommitted, 400000ULL - 48000ULL + 24000ULL);
    EXPECT_EQ(t.qxBidsOfQpool(t.token()), 0);
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);
    // The proceeds are still owed in full, 60:40 as the seats were when they were earned (crystallised before the costs shrank).
    auto ca = t.claimPayout(ALICE, hx);
    ASSERT_EQ(ca.returnCode, QPOOL_OK);
    auto cb = t.claimPayout(BOB, hx);
    ASSERT_EQ(cb.returnCode, QPOOL_OK);
    EXPECT_GE(ca.paid + cb.paid, proceeds - 2);
    EXPECT_LE(ca.paid + cb.paid, proceeds);
    EXPECT_NEAR((double)ca.paid, (double)proceeds * 0.6, 2.0);
    EXPECT_NEAR((double)cb.paid, (double)proceeds * 0.4, 2.0);
    // the last claim frees the slot and the committed capital
    EXPECT_EQ(t.getHolding(hx).found, 0u);
    EXPECT_EQ(t.getPool(pa).committedTotal, 0ULL);
    EXPECT_EQ(t.getPool(pa).holdingCount, 0ULL);
    EXPECT_EQ(t.getPool(pa).ownershipRows, 0ULL);
    sint64 un = t.unaccounted();
    EXPECT_GE(un, 0); EXPECT_LE(un, 2);
}

// 12. A seat bought on the position market before the cancel is a seat like any other: its owner gets his part of the refund and may cancel.
TEST(CancelQxBid, ASeatBoughtBeforeTheCancelGetsTheRefund)
{
    T t;
    uint64 pa = t.poolWith6040();
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);       // 100 rest at 1,000: costs 60,000 : 40,000
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    ASSERT_EQ(t.offerPosition(ALICE, hx, 20000, 5000), QPOOL_OK);
    auto pos = t.getPosition(hx, ALICE);
    auto bp = t.buyPosition(OUTSIDER, hx, ALICE, pos.offeredCostUnits, pos.offeredShares, 5000 + QPOOL_JOIN_FEE);
    ASSERT_EQ(bp.returnCode, QPOOL_OK);
    EXPECT_EQ(t.getPosition(hx, OUTSIDER).cost, 20000ULL);
    EXPECT_EQ(t.getPosition(hx, ALICE).cost, 40000ULL);
    sint64 balBefore = getBalance(QPOOL_ID);
    // K8.1: the new owner of a seat is not an operator; the creator cancels and the refund still follows the seats
    EXPECT_EQ(t.cancelQxBid(OUTSIDER, pa, hx).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    auto c = t.cancelQxBid(CREATOR, pa, hx);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.refunded, 100000ULL);
    EXPECT_EQ(getBalance(QPOOL_ID), balBefore + 100000);
    EXPECT_EQ(t.getMember(pa, OUTSIDER).uncommitted, 20000ULL);        // 20,000 of the 100,000 went to the seat he bought
    EXPECT_EQ(t.getMember(pa, BOB).uncommitted, 400000ULL);
    EXPECT_EQ(t.getMember(pa, ALICE).uncommitted, 600000ULL - 60000ULL + 40000ULL);   // the 5,000 she was paid for the seat is hers, in her wallet
    EXPECT_EQ(t.getHolding(hx).found, 0u);
    EXPECT_EQ(t.unaccounted(), 0);
}


// 13. The R2 squat: a 1-share, 1-QU bid on contract shares binds the payer's ledger and the share name to a contract index for ever (EvictSquat
//     refuses purchase-created holdings). A stranger can now cancel it once it is older than the limit: the holding is freed, the ledger and the
//     name binding are released, and the honest pool can use the pair.
TEST(CancelQxBid, DISABLED_ASquattingBidIsReleasedByAnyoneAfterTheAgeLimit)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.initPayerWithHolders({ { OUTSIDER, 300 }, { CAROL, 376 } });
    const Asset shares = T::payerShares();
    const id payer = T::payerId();
    const uint64 CTR = TESTEXB_CONTRACT_INDEX;
    auto e = t.buyQxOf(shares, CTR, pa, ALICE, ALICE, BOB, 1, 1, 1);       // the cheapest bid there is: price 1, one share, 1 QU
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    EXPECT_EQ(t.getHolding(hx).pendingShares, 1ULL);
    uint64 bound = 0;
    EXPECT_TRUE(t.getState()->contractNames.get(shares.assetName, bound));
    EXPECT_EQ(bound, (uint64)CTR);
    EXPECT_EQ(t.getSource(payer, QPOOL_SOURCE_CONTRACT).found, 1u);
    EXPECT_EQ(t.cancelQxBid(OUTSIDER, pa, hx).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    t.advanceTicks(QPOOL_QX_BID_MAX_AGE_TICKS + 1);
    auto c = t.cancelQxBid(OUTSIDER, pa, hx);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.cancelledShares, 1ULL);
    EXPECT_EQ(c.refunded, 1ULL);
    EXPECT_EQ(t.getHolding(hx).found, 0u);
    EXPECT_EQ(t.getSource(payer, QPOOL_SOURCE_CONTRACT).found, 0u);
    bound = 0;
    EXPECT_FALSE(t.getState()->contractNames.get(shares.assetName, bound));      // the name is free again
    EXPECT_EQ(t.qxBidsOfQpool(shares), 0);
    EXPECT_EQ(t.unaccounted(), 0);
}

// 14. A bid that fills completely inside Execute leaves nothing resting and no record on the ledger.
TEST(CancelQxBid, ABidThatFillsAtOnceLeavesNoRecord)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 300);
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 1000, 100), 100);
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1500);
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    EXPECT_EQ(e.acquired, 100ULL);
    EXPECT_EQ(t.getHolding(e.holdingId).pendingShares, 0ULL);
    expectBookRecord(t, e.holdingId, 0, 0);
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).pendingQx, 0ULL);
    EXPECT_EQ(t.cancelQxBid(CREATOR, pa, e.holdingId).returnCode, QPOOL_ERR_NO_PENDING_BID);
    EXPECT_EQ(t.unaccounted(), 0);
}


// ---- K8.1: operators (the creator and up to three admins) ----

TEST(Operators, SetPoolAdminsRules)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    // the creator names two admins; GetPool shows them
    auto r = t.setPoolAdmins(CREATOR, pa, BOB, OUTSIDER, NULL_ID, 999);
    EXPECT_EQ(r.returnCode, QPOOL_OK);
    auto gp = t.getPool(pa);
    EXPECT_TRUE(gp.admin0 == BOB); EXPECT_TRUE(gp.admin1 == OUTSIDER); EXPECT_TRUE(gp.admin2 == NULL_ID);
    // an admin cannot name admins; neither can a member or a stranger; another pool's creator cannot
    EXPECT_EQ(t.setPoolAdmins(BOB, pa, CAROL, NULL_ID, NULL_ID).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    EXPECT_EQ(t.setPoolAdmins(ALICE, pa, CAROL, NULL_ID, NULL_ID).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    EXPECT_EQ(t.setPoolAdmins(DAVE, pa, CAROL, NULL_ID, NULL_ID).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    // refused inputs: the creator himself, the contract, the same id twice, a bad pool
    EXPECT_EQ(t.setPoolAdmins(CREATOR, pa, CREATOR, NULL_ID, NULL_ID).returnCode, QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.setPoolAdmins(CREATOR, pa, QPOOL_ID, NULL_ID, NULL_ID).returnCode, QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.setPoolAdmins(CREATOR, pa, ALICE, ALICE, NULL_ID).returnCode, QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.setPoolAdmins(CREATOR, pa, NULL_ID, ALICE, ALICE).returnCode, QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.setPoolAdmins(CREATOR, QPOOL_MAX_POOLS, ALICE, NULL_ID, NULL_ID).returnCode, QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.setPoolAdmins(CREATOR, 900, ALICE, NULL_ID, NULL_ID).returnCode, QPOOL_ERR_NOT_FOUND);
    gp = t.getPool(pa);                                                   // none of the refusals changed the list
    EXPECT_TRUE(gp.admin0 == BOB); EXPECT_TRUE(gp.admin1 == OUTSIDER); EXPECT_TRUE(gp.admin2 == NULL_ID);
    // the same list again writes nothing; three admins, then the list is cleared
    EXPECT_EQ(t.setPoolAdmins(CREATOR, pa, BOB, OUTSIDER, NULL_ID).returnCode, QPOOL_OK);
    EXPECT_EQ(t.setPoolAdmins(CREATOR, pa, CAROL, DAVE, ALICE).returnCode, QPOOL_OK);
    gp = t.getPool(pa);
    EXPECT_TRUE(gp.admin0 == CAROL); EXPECT_TRUE(gp.admin1 == DAVE); EXPECT_TRUE(gp.admin2 == ALICE);
    EXPECT_EQ(t.setPoolAdmins(CREATOR, pa, NULL_ID, NULL_ID, NULL_ID).returnCode, QPOOL_OK);
    gp = t.getPool(pa);
    EXPECT_TRUE(gp.admin0 == NULL_ID); EXPECT_TRUE(gp.admin1 == NULL_ID); EXPECT_TRUE(gp.admin2 == NULL_ID);
    // the pools are independent
    EXPECT_TRUE(t.getPool(pb).admin0 == NULL_ID);
    EXPECT_EQ(t.unaccounted(), 0);                                        // the attached 999 QU came back
}

TEST(Operators, OnlyOperatorsExecuteAndTheExpiryStaysOpen)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.advanceTicks(1);
    auto p = t.propose(ALICE, pa, QPOOL_KIND_BUY_QX, t.token(), 150000, 100, 1000, 0);
    ASSERT_EQ(p.returnCode, QPOOL_OK);
    t.passFor(p.proposalId, ALICE, BOB);
    // members and strangers are refused, state untouched; the reward would come back (execute() attaches none)
    sint64 bal = getBalance(QPOOL_ID);
    EXPECT_EQ(t.execute(ALICE, p.proposalId).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    EXPECT_EQ(t.execute(BOB, p.proposalId).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    EXPECT_EQ(t.execute(OUTSIDER, p.proposalId).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    EXPECT_EQ(t.execute(DAVE, p.proposalId).returnCode, QPOOL_ERR_NOT_AUTHORIZED);        // the creator of the OTHER pool
    EXPECT_EQ(getBalance(QPOOL_ID), bal);
    // an admin (a stranger to the roster) places the order
    ASSERT_EQ(t.setPoolAdmins(CREATOR, pa, OUTSIDER, NULL_ID, NULL_ID).returnCode, QPOOL_OK);
    auto e = t.execute(OUTSIDER, p.proposalId);
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    EXPECT_EQ(t.getHolding(e.holdingId).pendingShares, 100ULL);
    // a second mandate: the creator executes; a removed admin is refused
    t.advanceTicks(QPOOL_QX_BID_MAX_AGE_TICKS + 5);
    ASSERT_EQ(t.cancelQxBid(OUTSIDER, pa, e.holdingId).returnCode, QPOOL_OK);              // the admin cancels
    t.advanceTicks(1);
    auto p2 = t.propose(ALICE, pa, QPOOL_KIND_BUY_QX, t.token(), 150000, 100, 1000, 0);
    ASSERT_EQ(p2.returnCode, QPOOL_OK);
    t.passFor(p2.proposalId, ALICE, BOB);
    ASSERT_EQ(t.setPoolAdmins(CREATOR, pa, NULL_ID, NULL_ID, NULL_ID).returnCode, QPOOL_OK);
    EXPECT_EQ(t.execute(OUTSIDER, p2.proposalId).returnCode, QPOOL_ERR_NOT_AUTHORIZED);   // no longer an admin
    // nobody carries it out: after the execute window anybody may close the dead mandate (it is not an operator-only act)
    t.advanceTicks(QPOOL_EXECUTE_WINDOW + 2);
    EXPECT_EQ(t.execute(OUTSIDER, p2.proposalId).returnCode, QPOOL_ERR_WINDOW_CLOSED);
    EXPECT_EQ(t.execute(CREATOR, p2.proposalId).returnCode, QPOOL_ERR_WRONG_STATUS);
    EXPECT_EQ(t.unaccounted(), 0);
}


// ---- K8.2: several pools rest bids on the same token at once ----

static uint64 ledgerCount(T& t, std::vector<uint64>& ids)
{
    uint64 cnt = 0;
    t.ledgerBids(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN, cnt, ids);
    return cnt;
}

TEST(MultiBid, TwoPoolsRestAtOnceAndFillsFollowPricePriority)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.giveKoin(OUTSIDER, 300);
    auto a = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);                       // A: 100 @ 1,000 rests
    auto b = t.buyQx(pb, CAROL, CAROL, DAVE, 120000, 100, 1100, DAVE);                // B: 100 @ 1,100 rests as well
    ASSERT_EQ(a.returnCode, QPOOL_OK); ASSERT_EQ(b.returnCode, QPOOL_OK);
    uint64 hxA = a.holdingId, hxB = b.holdingId;
    std::vector<uint64> ids;
    EXPECT_EQ(ledgerCount(t, ids), 2ULL);
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], hxB); EXPECT_EQ(ids[1], hxA);                                   // the higher price is first
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).pendingQx, 200ULL);
    EXPECT_EQ(t.unaccounted(), 0);

    // 60 shares are sold into the book: QX serves the highest bid first, so they are B's.
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 60), 60);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hxA), QPOOL_OK);                           // anyone, on either holding, credits both
    EXPECT_EQ(t.getHolding(hxB).shares, 60ULL);  EXPECT_EQ(t.getHolding(hxB).pendingShares, 40ULL);
    EXPECT_EQ(t.getHolding(hxA).shares, 0ULL);   EXPECT_EQ(t.getHolding(hxA).pendingShares, 100ULL);
    // 100 more: B's last 40 at 1,100, then 60 of A's at 1,000.
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 100), 100);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hxB), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hxB).shares, 100ULL); EXPECT_EQ(t.getHolding(hxB).pendingShares, 0ULL);
    EXPECT_EQ(t.getHolding(hxA).shares, 60ULL);  EXPECT_EQ(t.getHolding(hxA).pendingShares, 40ULL);
    EXPECT_EQ(ledgerCount(t, ids), 1ULL);                                              // B left the list, A stays
    ASSERT_EQ(ids.size(), 1u); EXPECT_EQ(ids[0], hxA);
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).pendingQx, 40ULL);
    EXPECT_EQ(t.unaccounted(), 0);
}

TEST(MultiBid, SamePriceServesTheEarlierBidFirst)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.giveKoin(OUTSIDER, 300);
    auto a = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);                       // A first
    auto b = t.buyQx(pb, CAROL, CAROL, DAVE, 150000, 100, 1000, DAVE);                // B at the SAME price, later
    ASSERT_EQ(a.returnCode, QPOOL_OK); ASSERT_EQ(b.returnCode, QPOOL_OK);
    uint64 hxA = a.holdingId, hxB = b.holdingId;
    std::vector<uint64> ids;
    EXPECT_EQ(ledgerCount(t, ids), 2ULL);
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], hxA); EXPECT_EQ(ids[1], hxB);
    EXPECT_EQ(t.qxBidsAt(t.token(), 1000), 200);                                       // QX holds ONE merged order of 200
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 150), 150);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hxB), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hxA).shares, 100ULL); EXPECT_EQ(t.getHolding(hxA).pendingShares, 0ULL);
    EXPECT_EQ(t.getHolding(hxB).shares, 50ULL);  EXPECT_EQ(t.getHolding(hxB).pendingShares, 50ULL);
    // B's creator takes the rest back: exactly the 50 that still rest on the merged order
    auto c = t.cancelQxBid(DAVE, pb, hxB);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.cancelledShares, 50ULL);
    EXPECT_EQ(c.refunded, 50000ULL);
    EXPECT_EQ(t.qxBidsAt(t.token(), 1000), 0);
    EXPECT_EQ(t.getHolding(hxB).shares, 50ULL);
    EXPECT_EQ(ledgerCount(t, ids), 0ULL);
    EXPECT_EQ(t.unaccounted(), 0);
}

TEST(MultiBid, ANewHigherBidCannotTakeEarlierFills)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.giveKoin(OUTSIDER, 300);
    auto a = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);
    ASSERT_EQ(a.returnCode, QPOOL_OK);
    uint64 hxA = a.holdingId;
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 30), 30);                              // 30 of A's fill, nobody has reconciled
    auto b = t.buyQx(pb, CAROL, CAROL, DAVE, 120000, 100, 1100, DAVE);                 // a HIGHER bid joins
    ASSERT_EQ(b.returnCode, QPOOL_OK);
    uint64 hxB = b.holdingId;
    EXPECT_EQ(t.getHolding(hxA).shares, 30ULL);  EXPECT_EQ(t.getHolding(hxA).pendingShares, 70ULL);   // A was credited when B joined
    EXPECT_EQ(t.getHolding(hxB).shares, 0ULL);   EXPECT_EQ(t.getHolding(hxB).pendingShares, 100ULL);
    std::vector<uint64> ids;
    EXPECT_EQ(ledgerCount(t, ids), 2ULL);
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], hxB); EXPECT_EQ(ids[1], hxA);
    EXPECT_EQ(t.unaccounted(), 0);
}

TEST(MultiBid, DividendsAreSetAsideForTheBidThatWonTheShares)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.initPayerWithHolders({ { OUTSIDER, 300 }, { CAROL, 376 } });
    const Asset shares = T::payerShares();
    const uint64 CTR = TESTEXB_CONTRACT_INDEX;
    auto a = t.buyQxOf(shares, CTR, pa, ALICE, ALICE, BOB, 150000, 100, 1000);                     // A: 100 @ 1,000
    auto b = t.buyQxOf(shares, CTR, pb, CAROL, CAROL, DAVE, 120000, 100, 1100, DAVE);              // B: 100 @ 1,100
    ASSERT_EQ(a.returnCode, QPOOL_OK); ASSERT_EQ(b.returnCode, QPOOL_OK);
    uint64 hxA = a.holdingId, hxB = b.holdingId;
    ASSERT_EQ(t.qxAsk(OUTSIDER, shares, 900, 60), 60);                                              // 60 shares: B's
    ASSERT_TRUE(t.payerDistributes(1000, OUTSIDER));                                                // QPOOL holds 60: 60,000 arrives, 59,400 net
    EXPECT_EQ(t.getState()->holdings.get(hxB).qxEscrow, 59400ULL);                                       // all of it is B's
    EXPECT_EQ(t.getState()->holdings.get(hxA).qxEscrow, 0ULL);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hxA), QPOOL_OK);
    auto owedB = [&]() { return t.getPosition(hxB, CAROL).owed + t.getPosition(hxB, DAVE).owed; };
    auto owedA = [&]() { return t.getPosition(hxA, ALICE).owed + t.getPosition(hxA, BOB).owed; };
    EXPECT_GE(owedB(), 59400ULL - 3); EXPECT_LE(owedB(), 59400ULL);
    EXPECT_EQ(owedA(), 0ULL);
    // 100 more shares: B's last 40, then 60 of A's. A second dividend of 500 per share arrives on the 160 QPOOL holds before anybody reconciles.
    ASSERT_EQ(t.qxAsk(OUTSIDER, shares, 900, 100), 100);
    ASSERT_TRUE(t.payerDistributes(500, OUTSIDER));                                                 // 80,000 gross, 79,200 net
    // 60 shares are recognised (B's): 79,200 x 60/160 = 29,700 through the accumulator; 49,500 is set aside: B's 40 shares 19,800, A's 60 shares 29,700
    EXPECT_EQ(t.getState()->holdings.get(hxB).qxEscrow, 19800ULL);
    EXPECT_EQ(t.getState()->holdings.get(hxA).qxEscrow, 29700ULL);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hxB), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hxB).shares, 100ULL); EXPECT_EQ(t.getHolding(hxA).shares, 60ULL);
    EXPECT_GE(owedA(), 29700ULL - 3); EXPECT_LE(owedA(), 29700ULL);
    EXPECT_GE(owedB(), 59400ULL + 29700ULL + 19800ULL - 6); EXPECT_LE(owedB(), 59400ULL + 29700ULL + 19800ULL);
    sint64 slack = t.unaccounted() - (sint64)(owedA() + owedB());
    EXPECT_GE(slack, 0); EXPECT_LE(slack, 6);                                                       // payout floor dust only
}


#ifdef QPOOL_HAS_K83
// ---- K8.3: resting SELL limit orders ----

// Pool A (ALICE/BOB, creator CREATOR) buys `n` KOIN into a holding at once (OUTSIDER sold them into the book at 900). Returns the holding id.
static uint64 holdKoin(T& t, uint64 pool, const id& proposer, const id& m1, const id& m2, const id& executor, uint64 n)
{
    EXPECT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, (sint64)n), (sint64)n);
    auto b = t.buyQx(pool, proposer, m1, m2, 150000, n, 1000, executor);
    EXPECT_EQ(b.returnCode, QPOOL_OK);
    EXPECT_EQ(b.acquired, n);
    return b.holdingId;
}
static uint64 askLedgerCount(T& t, std::vector<uint64>& ids)
{
    uint64 cnt = 0;
    t.ledgerAsks(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN, cnt, ids);
    return cnt;
}
static QPOOL::SourceLedger tokenLedger(T& t)
{
    QPOOL::SourceKey k; k.source = TOKEN_ISSUER; k.kind = QPOOL_SOURCE_TOKEN;
    Zeroed<QPOOL::SourceLedger> ledZ; QPOOL::SourceLedger& led = ledZ.get();
    t.getState()->sources.get(k, led);
    return led;
}

// A share donated to QPOOL is not a fill of a resting BID: QX's bid book, not the share count, says what filled.
TEST(RestingBid, ADonatedShareIsNotCreditedAsAFill)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 300);
    auto a = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);                        // 100 @ 1,000 rest
    ASSERT_EQ(a.returnCode, QPOOL_OK);
    uint64 hx = a.holdingId;
    EXPECT_EQ(t.getStats().restingBids, 1ULL);
    t.giveKoin(QPOOL_ID, 1);                                                            // a stranger donates one share
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).shares, 0ULL) << "the donated share must not be booked as a fill";
    EXPECT_EQ(t.getHolding(hx).pendingShares, 100ULL);
    // the real fill arrives
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 100), 100);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).shares, 100ULL);
    EXPECT_EQ(t.getHolding(hx).pendingShares, 0ULL);
    EXPECT_EQ(t.qxBidsOfQpool(t.token()), 0);                                           // nothing is left resting on QX
    EXPECT_EQ(t.getStats().restingBids, 0ULL);
    EXPECT_EQ(t.cancelQxBid(CREATOR, pa, hx).returnCode, QPOOL_ERR_NO_PENDING_BID);
}

TEST(RestingSell, RestsOnQxAndAFillIsCreditedWithItsProceeds)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hx = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    EXPECT_EQ(t.unaccounted(), 0);
    auto s = t.sellLimit(pa, ALICE, ALICE, BOB, hx, 100, 2000);
    ASSERT_EQ(s.returnCode, QPOOL_OK);
    EXPECT_EQ(s.sold, 0ULL); EXPECT_EQ(s.proceeds, 0ULL);
    EXPECT_EQ(t.getHolding(hx).shares, 0ULL); EXPECT_EQ(t.getHolding(hx).askShares, 100ULL);
    EXPECT_EQ(t.getHolding(hx).askPrice, 2000ULL);
    EXPECT_EQ(t.getHolding(hx).active, 1ULL);
    auto src = t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN);
    EXPECT_EQ(src.askCount, 1ULL); EXPECT_EQ(src.qxCount, 0ULL); EXPECT_EQ(src.askTotal, 100ULL);
    EXPECT_EQ(src.recognized, 0ULL); EXPECT_EQ(src.pendingQx, 0ULL);
    EXPECT_EQ(t.qxAsksOfQpool(t.token()), 100); EXPECT_EQ(t.qxAsksOfQpool(t.token(), 2000), 100);
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);
    EXPECT_EQ(t.unaccounted(), 0);

    // a buyer takes 30 at 2,000: QX pays 60,000 less its fee (floor(60,000 x 0.3%) + 1 = 181) to QPOOL, outside any call
    ASSERT_EQ(t.qxBid(OUTSIDER, t.token(), 2000, 30), 30);
    EXPECT_EQ(t.getStats().globalUnattributed, 59819ULL);
    EXPECT_EQ(t.getHolding(hx).askShares, 100ULL);                                     // nobody has told QPOOL yet
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);                              // anyone may
    EXPECT_EQ(t.getHolding(hx).askShares, 70ULL);
    EXPECT_EQ(t.getStats().globalUnattributed, 29ULL);                                  // 59,819 arrived, 59,790 was credited (fee bound 180 + 30 shares = 210): 29 stay in the counter
    EXPECT_GE(t.owedA(hx), 59193ULL - 3); EXPECT_LE(t.owedA(hx), 59193ULL);            // 59,790 less the 1% management fee (597)
    EXPECT_EQ(t.qxAsksOfQpool(t.token()), 70);
    auto snap = t.snapshot();
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);
    EXPECT_TRUE(t.sameAs(snap)) << "a second reconcile with nothing new must write nothing";

    // the last 70 sell: the holding is sold out, retired, and its ledger goes
    ASSERT_EQ(t.qxBid(OUTSIDER, t.token(), 2000, 70), 70);
    EXPECT_EQ(t.reconcileHolding(CAROL, hx), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).active, 0ULL); EXPECT_EQ(t.getHolding(hx).askShares, 0ULL);
    EXPECT_FALSE(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).found);
    EXPECT_EQ(t.getState()->restingAsks, 0ULL);
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);
    uint64 owed = t.owedA(hx);
    EXPECT_GE(owed, 59193ULL + 138115ULL - 6); EXPECT_LE(owed, 59193ULL + 138115ULL);   // + (140,000 - 420 - 70 shares = 139,510) less its 1% (1,395); the remainder of the counter went to revenue
    sint64 slack = t.unaccounted() - (sint64)owed;
    EXPECT_GE(slack, 0); EXPECT_LE(slack, 6);
}

TEST(RestingSell, WhatTheBidsPayFillsAtOnceAndTheRestRests)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hx = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    ASSERT_EQ(t.qxBid(OUTSIDER, t.token(), 2500, 40), 40);                            // a resting bid of 40 at 2,500
    auto s = t.sellLimit(pa, ALICE, ALICE, BOB, hx, 100, 2000);
    ASSERT_EQ(s.returnCode, QPOOL_OK);
    EXPECT_EQ(s.sold, 40ULL);
    EXPECT_EQ(s.proceeds, 98703ULL);                                                   // 100,000 less QX's 301, less the 1% (996)
    EXPECT_EQ(t.getHolding(hx).shares, 0ULL); EXPECT_EQ(t.getHolding(hx).askShares, 60ULL);
    EXPECT_EQ(t.qxAsksOfQpool(t.token(), 2000), 60);
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);
    EXPECT_GE(t.owedA(hx), 98703ULL - 3); EXPECT_LE(t.owedA(hx), 98703ULL);
}

TEST(RestingSell, TwoPoolsSellTheCheapestFirst)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hxA = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    uint64 hxB = holdKoin(t, pb, CAROL, CAROL, DAVE, DAVE, 100);
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hxA, 100, 2000).returnCode, QPOOL_OK);
    ASSERT_EQ(t.sellLimit(pb, CAROL, CAROL, DAVE, hxB, 100, 1800, DAVE).returnCode, QPOOL_OK);   // the cheaper ask joins second but is listed first
    std::vector<uint64> ids;
    EXPECT_EQ(askLedgerCount(t, ids), 2ULL);
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], hxB); EXPECT_EQ(ids[1], hxA);
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).askTotal, 200ULL);
    EXPECT_EQ(t.qxAsksOfQpool(t.token()), 200);
    // a buyer takes 150 at up to 2,100: QX serves the cheapest first, B's 100 at 1,800, then 50 of A's at 2,000
    ASSERT_EQ(t.qxBid(OUTSIDER, t.token(), 2100, 150), 150);
    EXPECT_EQ(t.getStats().globalUnattributed, 179459ULL + 99699ULL);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hxA), QPOOL_OK);                            // one call credits both
    EXPECT_EQ(t.getHolding(hxB).active, 0ULL);                                          // B sold out
    EXPECT_EQ(t.getHolding(hxA).askShares, 50ULL); EXPECT_EQ(t.getHolding(hxA).active, 1ULL);
    EXPECT_EQ(t.getStats().globalUnattributed, 148ULL);                                 // 279,158 arrived, 279,010 credited (fee bounds 540+100 and 300+50): the rest waits for the last ask
    EXPECT_GE(t.owedB(hxB), 177567ULL - 3); EXPECT_LE(t.owedB(hxB), 177567ULL);        // 179,360 less 1,793
    EXPECT_GE(t.owedA(hxA), 98654ULL - 3);  EXPECT_LE(t.owedA(hxA), 98654ULL);         // 99,650 less 996
    EXPECT_EQ(askLedgerCount(t, ids), 1ULL);
    ASSERT_EQ(ids.size(), 1u); EXPECT_EQ(ids[0], hxA);
    sint64 slack = t.unaccounted() - (sint64)(t.owedA(hxA) + t.owedB(hxB));
    EXPECT_GE(slack, 0); EXPECT_LE(slack, 8);
}

TEST(RestingSell, AtOnePriceTheEarlierOrderSellsFirst)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hxA = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    uint64 hxB = holdKoin(t, pb, CAROL, CAROL, DAVE, DAVE, 100);
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hxA, 100, 2000).returnCode, QPOOL_OK);
    ASSERT_EQ(t.sellLimit(pb, CAROL, CAROL, DAVE, hxB, 100, 2000, DAVE).returnCode, QPOOL_OK);   // same price, later
    std::vector<uint64> ids;
    EXPECT_EQ(askLedgerCount(t, ids), 2ULL);
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], hxA); EXPECT_EQ(ids[1], hxB);
    EXPECT_EQ(t.qxAsksOfQpool(t.token(), 2000), 200);                                   // ONE merged order at QX
    ASSERT_EQ(t.qxBid(OUTSIDER, t.token(), 2000, 150), 150);                           // one fill of 150 against the merged order
    EXPECT_EQ(t.getStats().globalUnattributed, 299099ULL);                             // 300,000 less ONE fee (901)
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hxB), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hxA).active, 0ULL);                                          // A's 100 sold first
    EXPECT_EQ(t.getHolding(hxB).askShares, 50ULL);
    EXPECT_EQ(t.getStats().globalUnattributed, 149ULL);                                 // 299,099 arrived, 298,950 credited (fee bound 900 + 150 shares)
    EXPECT_GE(t.owedA(hxA), 197307ULL - 3); EXPECT_LE(t.owedA(hxA), 197307ULL);        // (199,300 less 1%)
    EXPECT_GE(t.owedB(hxB), 98654ULL - 3);  EXPECT_LE(t.owedB(hxB), 98654ULL);         // (99,650 less 1%)
}

TEST(RestingSell, CancelTakesBackWhatIsLeftAndCreditsWhatSold)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hx = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hx, 100, 2000).returnCode, QPOOL_OK);
    ASSERT_EQ(t.qxBid(OUTSIDER, t.token(), 2000, 30), 30);
    // a stranger may not (the order is young)
    EXPECT_EQ(t.cancelQxAsk(OUTSIDER, pa, hx).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    EXPECT_EQ(t.cancelQxAsk(CAROL, pa, hx).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    auto c = t.cancelQxAsk(CREATOR, pa, hx);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.cancelledShares, 70ULL); EXPECT_EQ(c.pendingLeft, 0ULL);
    EXPECT_EQ(t.qxAsksOfQpool(t.token()), 0);
    EXPECT_EQ(t.getHolding(hx).shares, 70ULL); EXPECT_EQ(t.getHolding(hx).askShares, 0ULL); EXPECT_EQ(t.getHolding(hx).active, 1ULL);
    EXPECT_EQ(t.getHolding(hx).askPrice, 0ULL);
    EXPECT_GE(t.owedA(hx), 59193ULL - 3); EXPECT_LE(t.owedA(hx), 59193ULL);            // the 30 that sold were credited first
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);                                   // the cancel left no sell resting anywhere: the 29 left over went to revenue
    auto src = t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN);
    EXPECT_EQ(src.qxCount, 0ULL); EXPECT_EQ(src.askCount, 0ULL); EXPECT_EQ(src.askTotal, 0ULL); EXPECT_EQ(src.recognized, 70ULL);
    EXPECT_EQ(t.getState()->restingAsks, 0ULL);
    EXPECT_EQ(t.cancelQxAsk(CREATOR, pa, hx).returnCode, QPOOL_ERR_NO_PENDING_ASK);
    // the shares are free again: a new sell can be voted
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hx, 70, 2500).returnCode, QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).askShares, 70ULL);
}

TEST(RestingSell, AnyoneCancelsAStaleSellAndAnAdminMayAtOnce)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hx = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    ASSERT_EQ(t.setPoolAdmins(CREATOR, pa, CAROL, NULL_ID, NULL_ID).returnCode, QPOOL_OK);
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hx, 100, 2000).returnCode, QPOOL_OK);
    t.advanceTicks(QPOOL_QX_BID_MAX_AGE_TICKS - 5);
    EXPECT_EQ(t.cancelQxAsk(OUTSIDER, pa, hx).returnCode, QPOOL_ERR_NOT_AUTHORIZED);   // not yet older than the limit
    t.advanceTicks(10);
    auto c = t.cancelQxAsk(OUTSIDER, pa, hx);                                          // now anybody may; the shares go back to the holding
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.cancelledShares, 100ULL);
    EXPECT_EQ(t.getHolding(hx).shares, 100ULL);
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hx, 100, 2000).returnCode, QPOOL_OK);
    EXPECT_EQ(t.cancelQxAsk(CAROL, pa, hx).returnCode, QPOOL_OK);                      // an admin at once
}

// K8.4: resting bids and resting sells on ONE token at the same time, from different pools: both fill, one reconcile credits both.
TEST(RestingMixed, BidsAndSellsRestTogetherAndBothFill)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hxB = holdKoin(t, pb, CAROL, CAROL, DAVE, DAVE, 100);                       // B holds 100 (OUTSIDER sold them at 900)
    ASSERT_EQ(t.sellLimit(pb, CAROL, CAROL, DAVE, hxB, 100, 2000, DAVE).returnCode, QPOOL_OK);       // B rests a sell of all 100 @ 2,000
    auto a = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);                         // A rests a bid of 100 @ 1,000 on the SAME token: accepted
    ASSERT_EQ(a.returnCode, QPOOL_OK);
    uint64 hxA = a.holdingId;
    auto src = t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN);
    EXPECT_EQ(src.qxCount, 1ULL); EXPECT_EQ(src.askCount, 1ULL);
    EXPECT_EQ(src.pendingQx, 100ULL); EXPECT_EQ(src.askTotal, 100ULL);
    EXPECT_EQ(t.qxAsksOfQpool(t.token()), 100); EXPECT_EQ(t.qxBidsOfQpool(t.token()), 100);
    EXPECT_EQ(t.getState()->restingAsks, 1ULL); EXPECT_EQ(t.getState()->restingBids, 1ULL);
    EXPECT_EQ(t.unaccounted(), 0);
    // OUTSIDER sells 60 into A's bid (filled at A's price 1,000); BOB buys 30 from B's ask at 2,000. Nobody reconciles yet.
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 60), 60);
    ASSERT_EQ(t.qxBid(BOB, t.token(), 2000, 30), 30);
    EXPECT_EQ(t.getStats().globalUnattributed, 59819ULL);
    // ONE reconcile, on either holding, credits both sides
    EXPECT_EQ(t.reconcileHolding(DAVE, hxA), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hxA).shares, 60ULL); EXPECT_EQ(t.getHolding(hxA).pendingShares, 40ULL);
    EXPECT_EQ(t.getHolding(hxB).askShares, 70ULL); EXPECT_EQ(t.getHolding(hxB).shares, 0ULL);
    EXPECT_GE(t.owedB(hxB), 59193ULL - 3); EXPECT_LE(t.owedB(hxB), 59193ULL);
    EXPECT_EQ(t.getStats().globalUnattributed, 29ULL);
    src = t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN);
    EXPECT_EQ(src.recognized, 60ULL); EXPECT_EQ(src.pendingQx, 40ULL); EXPECT_EQ(src.askTotal, 70ULL);
    EXPECT_EQ(t.qxAsksOfQpool(t.token()), 70); EXPECT_EQ(t.qxBidsOfQpool(t.token()), 40);
    sint64 slack = t.unaccounted() - (sint64)t.owedB(hxB);
    EXPECT_GE(slack, 0); EXPECT_LE(slack, 60);
    // both are cancelled by their creators: the shares come back, the QU of the bid is refunded, the ledger lists nothing
    EXPECT_EQ(t.cancelQxBid(CREATOR, pa, hxA).returnCode, QPOOL_OK);
    EXPECT_EQ(t.cancelQxAsk(DAVE, pb, hxB).returnCode, QPOOL_OK);
    EXPECT_EQ(t.getHolding(hxB).shares, 70ULL);
    EXPECT_EQ(t.qxAsksOfQpool(t.token()), 0); EXPECT_EQ(t.qxBidsOfQpool(t.token()), 0);
    src = t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN);
    EXPECT_EQ(src.qxCount, 0ULL); EXPECT_EQ(src.askCount, 0ULL);
    EXPECT_EQ(t.getState()->restingAsks, 0ULL); EXPECT_EQ(t.getState()->restingBids, 0ULL);
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);
}

// The dividend of a ledger with a recognised holding, a resting BID and a resting SELL is split with QX's books: the fills of the bid, the unsold shares of the sell and
// the recognised shares each get their part.
TEST(RestingMixed, DividendsAreSplitBetweenTheRecognisedTheBidsAndTheSells)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.initPayerWithHolders({ { OUTSIDER, 300 }, { CAROL, 376 } });
    const Asset shares = T::payerShares();
    const uint64 CTR = TESTEXB_CONTRACT_INDEX;
    ASSERT_EQ(t.qxAsk(OUTSIDER, shares, 900, 200), 200);
    auto a1 = t.buyQxOf(shares, CTR, pa, ALICE, ALICE, BOB, 150000, 100, 1000);                     // A holds 100 (recognised)
    auto b1 = t.buyQxOf(shares, CTR, pb, CAROL, CAROL, DAVE, 150000, 100, 1000, DAVE);              // B holds 100
    ASSERT_EQ(a1.returnCode, QPOOL_OK); ASSERT_EQ(b1.returnCode, QPOOL_OK);
    uint64 hxA1 = a1.holdingId, hxB = b1.holdingId;
    ASSERT_EQ(t.sellLimitOf(shares, pb, CAROL, CAROL, DAVE, hxB, 100, 2000, DAVE).returnCode, QPOOL_OK);   // B rests a sell of all 100 @ 2,000
    auto a2 = t.buyQxOf(shares, CTR, pa, ALICE, ALICE, BOB, 40000, 50, 800);                        // A rests a bid of 50 @ 800
    ASSERT_EQ(a2.returnCode, QPOOL_OK); ASSERT_EQ(a2.acquired, 0ULL);
    uint64 hxA2 = a2.holdingId;
    // dividend 1: QPOOL holds 200 shares: 100 recognised (A1), 100 unsold (B's sell). Net 198,000: half to the recognised, half set aside for the sell.
    ASSERT_TRUE(t.payerDistributes(1000, OUTSIDER));
    EXPECT_EQ(t.getState()->holdings.get(hxB).askEscrow, 99000ULL);
    EXPECT_EQ(t.getState()->holdings.get(hxA2).qxEscrow, 0ULL);
    // A's bid fills: OUTSIDER sells 50 into it at 800 (the 50 stay unreconciled), then dividend 2 of 500 a share on 250 shares: net 123,750
    ASSERT_EQ(t.qxAsk(OUTSIDER, shares, 700, 50), 50);
    ASSERT_TRUE(t.payerDistributes(500, OUTSIDER));
    //   recognised 100/250 -> 49,500 (accumulator); filled bid 50/250 -> 24,750; unsold sell 100/250 -> 49,500
    EXPECT_EQ(t.getState()->holdings.get(hxA2).qxEscrow, 24750ULL);
    EXPECT_EQ(t.getState()->holdings.get(hxB).askEscrow, 99000ULL + 49500ULL);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hxA2), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hxA2).shares, 50ULL); EXPECT_EQ(t.getHolding(hxA2).pendingShares, 0ULL);
    auto owedA = [&](uint64 hx) { return t.getPosition(hx, ALICE).owed + t.getPosition(hx, BOB).owed; };
    EXPECT_GE(owedA(hxA1), 99000ULL + 49500ULL - 3); EXPECT_LE(owedA(hxA1), 99000ULL + 49500ULL);   // 99,000 + 49,500 through the accumulator
    EXPECT_GE(owedA(hxA2), 24750ULL - 3); EXPECT_LE(owedA(hxA2), 24750ULL);
}

// ---- QX cannot be read while a dividend arrives (QX in an error state): the hook does not guess, it parks the slice of the unrecognised shares, bound to the number of shares it
// was computed for; the next ReconcileHolding / CancelQxBid / CancelQxAsk splits it with QX's books as they are then, never more shares than were parked. ----
// Scene: A holds 100 (recognised) and rests a bid; B holds 100 and rests a sell of them @ 2,000. `fillBefore`: 50 of A's bid fill before the dividend.
struct ParkScene
{
    T t;
    uint64 pa = 0, pb = 0, hxA1 = 0, hxB = 0, hxA2 = 0;
    Asset shares;
    uint64 owedA(uint64 hx) { return t.getPosition(hx, ALICE).owed + t.getPosition(hx, BOB).owed; }
    void build(uint64 bidShares, bool fillBefore)
    {
        pa = t.poolWith6040();
        pb = t.secondPool();
        t.initPayerWithHolders({ { OUTSIDER, 300 }, { CAROL, 376 } });
        shares = T::payerShares();
        const uint64 CTR = TESTEXB_CONTRACT_INDEX;
        ASSERT_EQ(t.qxAsk(OUTSIDER, shares, 900, 200), 200);
        auto a1 = t.buyQxOf(shares, CTR, pa, ALICE, ALICE, BOB, 150000, 100, 1000);
        auto b1 = t.buyQxOf(shares, CTR, pb, CAROL, CAROL, DAVE, 150000, 100, 1000, DAVE);
        ASSERT_EQ(a1.returnCode, QPOOL_OK); ASSERT_EQ(b1.returnCode, QPOOL_OK);
        hxA1 = a1.holdingId; hxB = b1.holdingId;
        ASSERT_EQ(t.sellLimitOf(shares, pb, CAROL, CAROL, DAVE, hxB, 100, 2000, DAVE).returnCode, QPOOL_OK);
        auto a2 = t.buyQxOf(shares, CTR, pa, ALICE, ALICE, BOB, 800 * bidShares, bidShares, 800);
        ASSERT_EQ(a2.returnCode, QPOOL_OK); ASSERT_EQ(a2.acquired, 0ULL);
        hxA2 = a2.holdingId;
        if (fillBefore) ASSERT_EQ(t.qxAsk(OUTSIDER, shares, 700, 50), 50);
    }
    // a dividend of `perShare` while QX cannot be called
    void distributeWithQxDown(sint64 perShare)
    {
        contractError[QX_CONTRACT_INDEX] = ContractErrorTimeout;
        ASSERT_TRUE(t.payerDistributes(perShare, OUTSIDER));
        contractError[QX_CONTRACT_INDEX] = NoContractError;
    }
};

TEST(RestingMixed, AFailedQxReadParksTheDividendAndTheReconcileSplitsItWithQxBooks)
{
    ParkScene s;
    s.build(50, false);
    T& t = s.t;
    s.distributeWithQxDown(1000);                                                       // QPOOL holds 200: net 198,000; 100 of the shares are unrecognised
    auto src = t.getSource(T::payerId(), QPOOL_SOURCE_CONTRACT);
    EXPECT_EQ(src.unresolved, 99000ULL);                                                // parked, not guessed
    EXPECT_EQ(t.ledgerRaw(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolvedRateX64.high, 990ULL);                   // 99,000 for 100 shares: 990 QU a share
    EXPECT_EQ(t.ledgerRaw(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolvedRateX64.low, 0ULL);
    EXPECT_EQ(t.getState()->holdings.get(s.hxB).askEscrow, 0ULL);
    EXPECT_EQ(t.getState()->holdings.get(s.hxA2).qxEscrow, 0ULL);
    EXPECT_GE(s.owedA(s.hxA1), 99000ULL - 3); EXPECT_LE(s.owedA(s.hxA1), 99000ULL);     // the recognised half went through the accumulator
    // A's bid fills 50 after the dividend, before anyone resolves it: the slice was parked for 100 shares, which were B's unsold sell; the sell is served first, so the fills
    // that came later take nothing of it
    ASSERT_EQ(t.qxAsk(OUTSIDER, s.shares, 700, 50), 50);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, s.hxB), QPOOL_OK);
    src = t.getSource(T::payerId(), QPOOL_SOURCE_CONTRACT);
    EXPECT_EQ(src.unresolved, 0ULL);
    EXPECT_EQ(t.ledgerRaw(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolvedRateX64.high, 0ULL);
    EXPECT_EQ(t.ledgerRaw(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolvedRateX64.low, 0ULL);
    EXPECT_EQ(t.getState()->holdings.get(s.hxB).askEscrow, 99000ULL);
    EXPECT_EQ(t.getState()->holdings.get(s.hxA2).qxEscrow, 0ULL);
    EXPECT_EQ(t.getHolding(s.hxA2).shares, 50ULL);
    EXPECT_LE(s.owedA(s.hxA2), 3ULL);
}

// The fills of a bid that came BEFORE the dividend are paid their worth, the sell its worth, nothing is left over.
TEST(RestingMixed, AParkedSliceIsPaidForTheSharesItWasParkedFor)
{
    ParkScene s;
    s.build(50, true);
    T& t = s.t;
    s.distributeWithQxDown(1000);                                                       // QPOOL holds 250: 100 recognised, 50 filled (not credited), 100 unsold; net 247,500
    EXPECT_EQ(t.getSource(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolved, 148500ULL);  // 150 of 250 shares
    EXPECT_EQ(t.ledgerRaw(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolvedRateX64.high, 990ULL);                   // 148,500 for 150 shares
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, s.hxA1), QPOOL_OK);                          // any holding of the ledger resolves it
    EXPECT_EQ(t.getSource(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolved, 0ULL);
    EXPECT_EQ(t.getState()->holdings.get(s.hxB).askEscrow, 99000ULL);                   // 100 of 150
    EXPECT_EQ(t.getState()->holdings.get(s.hxA2).qxEscrow, 49500ULL);                   // 50 of 150
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, s.hxA2), QPOOL_OK);
    EXPECT_EQ(t.getHolding(s.hxA2).shares, 50ULL);
    EXPECT_GE(s.owedA(s.hxA2), 49500ULL - 3); EXPECT_LE(s.owedA(s.hxA2), 49500ULL);
    EXPECT_GE(s.owedA(s.hxA1), 99000ULL - 3); EXPECT_LE(s.owedA(s.hxA1), 99000ULL);
}

// Shares that belong to no order (a donation) have a share of the parked slice too, and it goes to the recognised holders, as the hook sends it: the orders do not take it.
TEST(RestingMixed, ADonatedShareDoesNotTakeTheOrdersPartOfAParkedSlice)
{
    ParkScene s;
    s.build(50, true);
    T& t = s.t;
    t.qxDonate(CAROL, s.shares, 100);                                                   // 100 shares given to QPOOL
    s.distributeWithQxDown(1000);                                                       // QPOOL holds 350: 100 recognised, 100 donated, 50 filled, 100 unsold; net 346,500
    EXPECT_EQ(t.getSource(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolved, 247500ULL);  // 250 of 350 shares
    EXPECT_GE(s.owedA(s.hxA1), 99000ULL - 3); EXPECT_LE(s.owedA(s.hxA1), 99000ULL);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, s.hxA1), QPOOL_OK);
    EXPECT_EQ(t.getState()->holdings.get(s.hxB).askEscrow, 99000ULL);                   // 100 of 250
    EXPECT_EQ(t.getState()->holdings.get(s.hxA2).qxEscrow, 49500ULL);                   // 50 of 250
    EXPECT_GE(s.owedA(s.hxA1), 198000ULL - 3); EXPECT_LE(s.owedA(s.hxA1), 198000ULL);   // + the 100 donated shares' 99,000
}

// Two dividends are parked with QX recovering in between and A's bid filling meanwhile: the slice of each is worth what ITS shares were worth (990 QU a share both times); the sell's
// 100 shares exist for both dividends, the 50 fills only for the second. (One count for both parks would have paid the fills part of the first dividend's slice.)
TEST(RestingMixed, TwoParkedDividendsAreEachPaidForTheirOwnShares)
{
    ParkScene s;
    s.build(50, false);
    T& t = s.t;
    s.distributeWithQxDown(1000);                                                       // D1: 100 unsold shares, slice 99,000 (990 a share)
    ASSERT_EQ(t.qxAsk(OUTSIDER, s.shares, 700, 50), 50);                                // QX is back: A's bid fills 50
    s.distributeWithQxDown(1000);                                                       // D2: QPOOL holds 250, 150 unrecognised, slice 148,500 (990 a share)
    EXPECT_EQ(t.getSource(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolved, 99000ULL + 148500ULL);
    EXPECT_EQ(t.ledgerRaw(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolvedRateX64.high, 1980ULL);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, s.hxA1), QPOOL_OK);
    EXPECT_EQ(t.getSource(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolved, 0ULL);
    EXPECT_EQ(t.getState()->holdings.get(s.hxB).askEscrow, 198000ULL);                  // 100 shares x 1,980
    EXPECT_EQ(t.getState()->holdings.get(s.hxA2).qxEscrow, 49500ULL);                   // 50 fills, only for D2
}

// CancelQxBid (and CancelQxAsk) resolve the parked slice BEFORE they credit fills and take the order back, so the evidence is still there.
TEST(RestingMixed, CancelQxBidResolvesAParkedSliceFirst)
{
    ParkScene s;
    s.build(80, true);                                                                  // 50 of the 80 filled, 30 rest
    T& t = s.t;
    s.distributeWithQxDown(1000);
    EXPECT_EQ(t.getSource(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolved, 148500ULL);
    ASSERT_EQ(t.cancelQxBid(CREATOR, s.pa, s.hxA2).returnCode, QPOOL_OK);
    EXPECT_EQ(t.getSource(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolved, 0ULL);
    EXPECT_EQ(t.getHolding(s.hxA2).shares, 50ULL); EXPECT_EQ(t.getHolding(s.hxA2).pendingShares, 0ULL);
    EXPECT_GE(s.owedA(s.hxA2), 49500ULL - 3); EXPECT_LE(s.owedA(s.hxA2), 49500ULL);     // the fills' part reached the bid that won them
    EXPECT_EQ(t.getState()->holdings.get(s.hxB).askEscrow, 99000ULL);
}

TEST(RestingMixed, CancelQxAskResolvesAParkedSliceFirst)
{
    ParkScene s;
    s.build(50, true);
    T& t = s.t;
    s.distributeWithQxDown(1000);
    ASSERT_EQ(t.cancelQxAsk(CREATOR, s.pb, s.hxB).returnCode, QPOOL_OK);                // B's creator takes the unsold shares back
    EXPECT_EQ(t.getSource(T::payerId(), QPOOL_SOURCE_CONTRACT).unresolved, 0ULL);
    EXPECT_EQ(t.getHolding(s.hxB).askShares, 0ULL); EXPECT_EQ(t.getHolding(s.hxB).shares, 100ULL);
    EXPECT_GE(t.owedB(s.hxB), 99000ULL - 3); EXPECT_LE(t.owedB(s.hxB), 99000ULL);       // the unsold shares' part reached the sell that held them
}

// No holding may join a ledger while a slice is parked (it would be paid part of a dividend that arrived before it existed); sales and seat trades wait too.
TEST(RestingMixed, ANewHoldingCannotJoinWhileASliceIsParked)
{
    ParkScene s;
    s.build(50, true);
    T& t = s.t;
    s.distributeWithQxDown(1000);
    t.advanceTicks(1);
    EXPECT_EQ(t.propose(ALICE, s.pa, QPOOL_KIND_BUY_QX, s.shares, 10000, 10, 500, TESTEXB_CONTRACT_INDEX).returnCode, QPOOL_ERR_SETTLEMENT_PENDING);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, s.hxA1), QPOOL_OK);
    t.advanceTicks(1);
    EXPECT_EQ(t.propose(ALICE, s.pa, QPOOL_KIND_BUY_QX, s.shares, 10000, 10, 500, TESTEXB_CONTRACT_INDEX).returnCode, QPOOL_OK);
}

// A bid must be priced below QPOOL's own lowest resting sell of the token (QX sees one entity); a sell into QPOOL's own bid is an internal trade and is credited.
TEST(RestingMixed, SelfTradeRules)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hxB = holdKoin(t, pb, CAROL, CAROL, DAVE, DAVE, 100);
    ASSERT_EQ(t.sellLimit(pb, CAROL, CAROL, DAVE, hxB, 100, 2000, DAVE).returnCode, QPOOL_OK);      // the lowest sell of the token: 2,000
    t.advanceTicks(1);
    EXPECT_EQ(t.propose(ALICE, pa, QPOOL_KIND_BUY_QX, t.token(), 150000, 50, 2000, 0).returnCode, QPOOL_ERR_SELF_CROSS);    // at the sell's price: refused
    EXPECT_EQ(t.propose(ALICE, pa, QPOOL_KIND_BUY_QX, t.token(), 150000, 50, 2500, 0).returnCode, QPOOL_ERR_SELF_CROSS);    // above it: refused
    auto ok = t.propose(ALICE, pa, QPOOL_KIND_BUY_QX, t.token(), 100000, 50, 1999, 0);                                    // just below: accepted
    EXPECT_EQ(ok.returnCode, QPOOL_OK);
    // the other way: A rests a bid, B sells INTO it (an internal trade between two pools)
    ASSERT_EQ(t.cancelQxAsk(DAVE, pb, hxB).returnCode, QPOOL_OK);
    t.advanceTicks(QPOOL_PROPOSAL_LIFETIME + 5);                                                                         // A's accepted proposal above is dead
    auto bid = t.buyQx(pa, ALICE, ALICE, BOB, 75000, 50, 1500);                                                          // A: 50 @ 1,500 rest
    ASSERT_EQ(bid.returnCode, QPOOL_OK);
    auto sell = t.sellLimit(pb, CAROL, CAROL, DAVE, hxB, 50, 1400, DAVE);                                                // B: limit 1,400 <= A's 1,500: fills at A's price at once
    ASSERT_EQ(sell.returnCode, QPOOL_OK);
    EXPECT_EQ(sell.sold, 50ULL);
    EXPECT_EQ(sell.proceeds, 74027ULL);                                                                                  // 75,000 - 226 (QX) = 74,774, less 1% (747)
    EXPECT_EQ(t.getHolding(hxB).askShares, 0ULL);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, bid.holdingId), QPOOL_OK);                                                    // A's bid learns it was filled: the shares stayed in QPOOL
    EXPECT_EQ(t.getHolding(bid.holdingId).shares, 50ULL); EXPECT_EQ(t.getHolding(bid.holdingId).pendingShares, 0ULL);
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);
    sint64 slack = t.unaccounted() - (sint64)t.owedB(hxB);
    EXPECT_GE(slack, 0); EXPECT_LE(slack, 10);
}

// One holding can rest the part of its purchase that has not filled AND a sell of the shares that have.
TEST(RestingMixed, OneHoldingRestsABidAndASell)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 1000);
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 40), 40);
    auto b = t.buyQx(pa, ALICE, ALICE, BOB, 100000, 100, 1000);                          // 40 fill at once, 60 rest
    ASSERT_EQ(b.returnCode, QPOOL_OK); ASSERT_EQ(b.acquired, 40ULL);
    uint64 hx = b.holdingId;
    EXPECT_EQ(t.getHolding(hx).pendingShares, 60ULL);
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hx, 20, 2000).returnCode, QPOOL_OK);      // 20 of the 40 filled shares rest as a sell
    auto h = t.getHolding(hx);
    EXPECT_EQ(h.shares, 20ULL); EXPECT_EQ(h.pendingShares, 60ULL); EXPECT_EQ(h.askShares, 20ULL);
    EXPECT_EQ(h.qxBidPrice, 1000ULL); EXPECT_EQ(h.askPrice, 2000ULL);                     // two separate records
    EXPECT_EQ(t.qxBidsOfQpool(t.token()), 60); EXPECT_EQ(t.qxAsksOfQpool(t.token()), 20);
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 30), 30);                                  // 30 of the bid fill
    ASSERT_EQ(t.qxBid(BOB, t.token(), 2000, 5), 5);                                        // 5 of the sell sell
    EXPECT_EQ(t.reconcileHolding(DAVE, hx), QPOOL_OK);
    h = t.getHolding(hx);
    EXPECT_EQ(h.shares, 50ULL); EXPECT_EQ(h.pendingShares, 30ULL); EXPECT_EQ(h.askShares, 15ULL);
    EXPECT_EQ(t.cancelQxBid(CREATOR, pa, hx).returnCode, QPOOL_OK);
    EXPECT_EQ(t.cancelQxAsk(CREATOR, pa, hx).returnCode, QPOOL_OK);
    h = t.getHolding(hx);
    EXPECT_EQ(h.shares, 65ULL); EXPECT_EQ(h.pendingShares, 0ULL); EXPECT_EQ(h.askShares, 0ULL);   // 40 + 30 - 5, all free again
    EXPECT_EQ(t.qxBidsOfQpool(t.token()), 0); EXPECT_EQ(t.qxAsksOfQpool(t.token()), 0);
}

// Cancelling a bid must not retire (or free the slot of) a holding that still rests a sell of the shares the bid won.
TEST(RestingMixed, ACancelledBidDoesNotRetireAHoldingThatRestsASell)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 1000);
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 40), 40);
    auto b = t.buyQx(pa, ALICE, ALICE, BOB, 100000, 100, 1000);                          // 40 fill, 60 rest
    ASSERT_EQ(b.returnCode, QPOOL_OK); ASSERT_EQ(b.acquired, 40ULL);
    uint64 hx = b.holdingId;
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hx, 40, 2000).returnCode, QPOOL_OK);      // all 40 filled shares rest as a sell: shares 0
    EXPECT_EQ(t.getHolding(hx).shares, 0ULL); EXPECT_EQ(t.getHolding(hx).pendingShares, 60ULL); EXPECT_EQ(t.getHolding(hx).askShares, 40ULL);
    ASSERT_EQ(t.cancelQxBid(CREATOR, pa, hx).returnCode, QPOOL_OK);                        // the bid goes back: the holding has no shares and nothing pending
    auto h = t.getHolding(hx);
    EXPECT_EQ(h.found, 1u); EXPECT_EQ(h.active, 1ULL) << "the holding still rests a sell";
    EXPECT_EQ(h.askShares, 40ULL); EXPECT_EQ(h.pendingShares, 0ULL);
    std::vector<uint64> ids;
    EXPECT_EQ(askLedgerCount(t, ids), 1ULL);
    ASSERT_EQ(t.qxBid(BOB, t.token(), 2000, 10), 10);                                      // 10 of the sell sell and are credited
    EXPECT_EQ(t.reconcileHolding(DAVE, hx), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).askShares, 30ULL);
    EXPECT_GT(t.owedA(hx), 0ULL);
    ASSERT_EQ(t.cancelQxAsk(CREATOR, pa, hx).returnCode, QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).shares, 30ULL); EXPECT_EQ(t.getHolding(hx).active, 1ULL);
}

// The global bound on resting bids also holds for the FIRST bid of a token that has no ledger yet (else the window of 256 QX orders could be filled with one-QU bids).
TEST(RestingMixed, TheGlobalBidCapAlsoBindsTheFirstBidOfANewLedger)
{
    T t;
    uint64 pa = t.poolWith6040();
    EXPECT_FALSE(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).found);
    t.getState()->restingBids = QPOOL_MAX_RESTING_BIDS;                                  // QPOOL already rests the most it may
    t.advanceTicks(1);
    EXPECT_EQ(t.propose(ALICE, pa, QPOOL_KIND_BUY_QX, t.token(), 10000, 10, 1000, 0).returnCode, QPOOL_ERR_CAPACITY);
    t.getState()->restingBids = QPOOL_MAX_RESTING_BIDS - 1;
    EXPECT_EQ(t.propose(ALICE, pa, QPOOL_KIND_BUY_QX, t.token(), 10000, 10, 1000, 0).returnCode, QPOOL_OK);
}

// One pool cannot take a whole side of a token: at most QPOOL_MAX_QX_ORDERS_PER_POOL_PER_SIDE (12) of the 32 places.
TEST(RestingMixed, APoolHoldsAtMostItsPlacesOfASide)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    const uint64 K = QPOOL_MAX_QX_ORDERS_PER_POOL_PER_SIDE;
    for (uint64 i = 0; i < K; i++)
    {
        auto e = t.buyQx(pa, ALICE, ALICE, BOB, 10000, 10, 1000 - i * 10);
        ASSERT_EQ(e.returnCode, QPOOL_OK) << i;
    }
    t.advanceTicks(1);
    EXPECT_EQ(t.propose(ALICE, pa, QPOOL_KIND_BUY_QX, t.token(), 10000, 10, 1000 - K * 10, 0).returnCode, QPOOL_ERR_POOL_SLOTS);     // one more than pool A may hold
    auto other = t.buyQx(pb, CAROL, CAROL, DAVE, 10000, 10, 1000 - K * 10 - 10, DAVE);                                              // another pool still can
    EXPECT_EQ(other.returnCode, QPOOL_OK);
}

// QX lists an owner's orders 256 at a time. QPOOL reads its books page by page: 300 bids of QPOOL on another asset (priced above the pool's own) push the pool's KOIN bid
// onto the second page, where it is still seen (a single window would have been full, and every read refused as unreadable).
TEST(RestingMixed, ABidBeyondTheFirstPageOfQxBooksIsStillRead)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 zoin = t.issueSecond("ZOIN");
    Asset zoinAsset{ TOKEN_ISSUER, zoin };
    for (sint64 p = 0; p < 300; p++) ASSERT_EQ(t.qxBid(QPOOL_ID, zoinAsset, 2000 + p, 1), 1) << p;
    t.giveKoin(OUTSIDER, 1000);
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);                         // nobody sells: the whole bid rests, as the 301st order of QPOOL
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    EXPECT_EQ(t.getHolding(hx).pendingShares, 100ULL); EXPECT_EQ(t.getHolding(hx).shares, 0ULL);
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 40), 40);                               // 40 of the bid fill silently
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).shares, 40ULL); EXPECT_EQ(t.getHolding(hx).pendingShares, 60ULL);
    EXPECT_EQ(t.cancelQxBid(CREATOR, pa, hx).returnCode, QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).pendingShares, 0ULL); EXPECT_EQ(t.getHolding(hx).shares, 40ULL);
    EXPECT_EQ(t.qxBidsOfQpool(t.token()), 0);
}

// The same for the asks: 300 asks of QPOOL on another asset (priced below the pool's own) put its KOIN sell on the second page of QX's ask book.
TEST(RestingMixed, ASellBeyondTheFirstPageOfQxBooksIsStillRead)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 zoin = t.issueSecond("ZOIN");
    Asset zoinAsset{ TOKEN_ISSUER, zoin };
    t.giveKoin(OUTSIDER, 1000);
    uint64 hx = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    t.giveSecond(QPOOL_ID, 300);
    for (sint64 p = 0; p < 300; p++) ASSERT_EQ(t.qxAsk(QPOOL_ID, zoinAsset, 100 + p, 1), 1) << p;
    auto s = t.sellLimit(pa, ALICE, ALICE, BOB, hx, 100, 5000);                         // the 301st order of QPOOL
    ASSERT_EQ(s.returnCode, QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).askShares, 100ULL);
    ASSERT_EQ(t.qxBid(OUTSIDER, t.token(), 5000, 30), 30);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).askShares, 70ULL);
    EXPECT_EQ(t.getStats().globalUnattributed, 29ULL);                                  // 149,549 arrived, 149,520 was credited (fee bound 450 + 30 shares)
    EXPECT_EQ(t.cancelQxAsk(CREATOR, pa, hx).returnCode, QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).askShares, 0ULL); EXPECT_EQ(t.getHolding(hx).shares, 70ULL);
}

// The deepest page: 1,100 bids of QPOOL on another asset (priced above the pool's own) put the pool's KOIN bid on the FIFTH page of QX's book (QPOOL_QX_READ_PAGES = 5).
TEST(RestingMixed, ABidOnTheFifthPageOfQxBooksIsStillRead)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 zoin = t.issueSecond("ZOIN");
    Asset zoinAsset{ TOKEN_ISSUER, zoin };
    for (sint64 p = 0; p < 1100; p++) ASSERT_EQ(t.qxBid(QPOOL_ID, zoinAsset, 2000 + p, 1), 1) << p;
    t.giveKoin(OUTSIDER, 1000);
    auto e = t.buyQx(pa, ALICE, ALICE, BOB, 150000, 100, 1000);                         // the 1,101st order of QPOOL
    ASSERT_EQ(e.returnCode, QPOOL_OK);
    uint64 hx = e.holdingId;
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 40), 40);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).shares, 40ULL); EXPECT_EQ(t.getHolding(hx).pendingShares, 60ULL);
    EXPECT_EQ(t.cancelQxBid(CREATOR, pa, hx).returnCode, QPOOL_OK);
    EXPECT_EQ(t.qxBidsOfQpool(t.token()), 0);
}

TEST(RestingSell, AHoldingRestsOneSellAtATimeAndARefusalWritesNothing)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hx = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hx, 60, 2000).returnCode, QPOOL_OK);
    t.advanceTicks(1);
    auto p = t.propose(ALICE, pa, QPOOL_KIND_SELL_QX_LIMIT, t.token(), 0, 40, 2100, hx);
    ASSERT_EQ(p.returnCode, QPOOL_OK);
    t.passFor(p.proposalId, ALICE, BOB);
    auto snap = t.snapshot();
    EXPECT_EQ(t.execute(CREATOR, p.proposalId).returnCode, QPOOL_ERR_QX_BID_PENDING);
    EXPECT_TRUE(t.sameAs(snap));
    // a non-operator cannot place it either
    EXPECT_EQ(t.execute(OUTSIDER, p.proposalId).returnCode, QPOOL_ERR_NOT_AUTHORIZED);
    // quAmount must be 0, price non-zero
    EXPECT_EQ(t.propose(BOB, pa, QPOOL_KIND_SELL_QX_LIMIT, t.token(), 5, 10, 2100, hx).returnCode, QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.propose(BOB, pa, QPOOL_KIND_SELL_QX_LIMIT, t.token(), 0, 10, 0, hx).returnCode, QPOOL_ERR_INVALID_INPUT);
}

TEST(RestingSell, AnotherHoldingSellsAtOnceWhileAnAskRests)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hxA = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    uint64 hxB = holdKoin(t, pb, CAROL, CAROL, DAVE, DAVE, 50);
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hxA, 100, 2000).returnCode, QPOOL_OK);          // A's 100 are reserved on QX
    ASSERT_EQ(t.qxBid(OUTSIDER, t.token(), 1700, 20), 20);                                        // a bid of 20 at 1,700 (below A's ask: it rests)
    auto s = t.sellNowOf(t.token(), pb, CAROL, CAROL, DAVE, hxB, 50, 1500, 10000, DAVE);          // B sells at once: 20 fill, the other 30 are cancelled again
    ASSERT_EQ(s.returnCode, QPOOL_OK);
    EXPECT_EQ(s.sold, 20ULL);
    EXPECT_EQ(t.getHolding(hxB).shares, 30ULL);
    EXPECT_EQ(t.qxAsksOfQpool(t.token()), 100);                                                   // only A's order is left on the book
    EXPECT_EQ(t.qxAsksOfQpool(t.token(), 2000), 100);
    EXPECT_EQ(t.getHolding(hxA).askShares, 100ULL);
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);
}

// QPOOL_MAX_QX_BIDS_PER_LEDGER (32) pools of one member each rest a sell on the same token at different prices (listed lowest first whatever the order they came in); one more is
// refused; one buyer takes everything and one reconcile credits all of them.
TEST(RestingSell, AFullLedgerOfPoolsRestsSellsAndTheNextIsRefused)
{
    T t;
    const int CAP = (int)QPOOL_MAX_QX_BIDS_PER_LEDGER;
    const int N = CAP + 1;
    id creator[N]; uint64 pool[N]; uint64 hx[N];
    t.giveKoin(OUTSIDER, 1000);
    for (int i = 0; i < N; i++)
    {
        creator[i] = id::randomValue();
        auto c = t.createPool(creator[i], POOL_NAME + 10 + i, 0);
        ASSERT_EQ(c.returnCode, QPOOL_OK);
        pool[i] = c.poolId;
        ASSERT_EQ(t.contribute(creator[i], pool[i], 100000).returnCode, QPOOL_OK);
        ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 10), 10);
        t.advanceTicks(1);
        auto p = t.propose(creator[i], pool[i], QPOOL_KIND_BUY_QX, t.token(), 50000, 10, 1000, 0);
        ASSERT_EQ(p.returnCode, QPOOL_OK);
        ASSERT_EQ(t.vote(creator[i], p.proposalId, 1).returnCode, QPOOL_OK);
        t.advanceTicks(QPOOL_VOTE_WINDOW + 1);
        ASSERT_EQ(t.tally(creator[i], p.proposalId).status, QPOOL_PROP_PASSED);
        auto e = t.execute(creator[i], p.proposalId);
        ASSERT_EQ(e.returnCode, QPOOL_OK);
        ASSERT_EQ(e.acquired, 10ULL);
        hx[i] = e.holdingId;
    }
    auto sell = [&](int i, uint64 price)
    {
        t.advanceTicks(1);
        auto p = t.propose(creator[i], pool[i], QPOOL_KIND_SELL_QX_LIMIT, t.token(), 0, 10, price, hx[i]);
        EXPECT_EQ(p.returnCode, QPOOL_OK);
        EXPECT_EQ(t.vote(creator[i], p.proposalId, 1).returnCode, QPOOL_OK);
        t.advanceTicks(QPOOL_VOTE_WINDOW + 1);
        EXPECT_EQ(t.tally(creator[i], p.proposalId).status, QPOOL_PROP_PASSED);
        return t.execute(creator[i], p.proposalId);
    };
    // prices 2000 + CAP - 1 .. 2000 for pools 0 .. CAP-1 (the later the pool, the cheaper)
    for (int i = 0; i < CAP; i++) ASSERT_EQ(sell(i, 2000 + CAP - 1 - i).returnCode, QPOOL_OK) << i;
    std::vector<uint64> ids;
    EXPECT_EQ(askLedgerCount(t, ids), (uint64)CAP);
    ASSERT_EQ(ids.size(), (size_t)CAP);
    for (int i = 0; i < CAP; i++) EXPECT_EQ(ids[i], hx[CAP - 1 - i]) << "position " << i;      // lowest price first
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).askTotal, (uint64)CAP * 10);
    EXPECT_EQ(t.getState()->restingAsks, (uint64)CAP);
    auto snap = t.snapshot();
    EXPECT_EQ(sell(CAP, 1999).returnCode, QPOOL_ERR_QX_BID_PENDING);                    // the ledger is full (the snapshot below is taken after the vote)
    (void)snap;
    // a buyer takes all the shares at once: one fill per price
    ASSERT_EQ(t.qxBid(OUTSIDER, t.token(), 2100, (sint64)CAP * 10), (sint64)CAP * 10);
    uint64 pot = t.getStats().globalUnattributed;
    EXPECT_GT(pot, 0ULL);
    EXPECT_EQ(t.reconcileHolding(CAROL, hx[3]), QPOOL_OK);
    for (int i = 0; i < CAP; i++) EXPECT_EQ(t.getHolding(hx[i]).active, 0ULL) << "holding " << i;
    EXPECT_FALSE(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).found == 1 && t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).holdings == (uint64)CAP);
    EXPECT_EQ(t.getState()->restingAsks, 0ULL);
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);                                   // all of them sold out: what the fee bounds left over went to revenue
    // the extra pool's holding is the only one left on the ledger and can rest a sell now
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).holdings, 1ULL);
    t.advanceTicks(QPOOL_PROPOSAL_LIFETIME + 5);                                        // the refused mandate of the last pool is dead; he may propose again
    EXPECT_EQ(sell(CAP, 1999).returnCode, QPOOL_OK);
}

// A buyer's zero-fee fills (price 1, one share: QX's fee takes the whole 1 QU, nothing reaches QPOOL) cannot make one ledger draw another ledger's proceeds out of the shared counter.
TEST(RestingSell, ManyZeroFeeFillsCannotDrawAnotherLedgersProceeds)
{
    T t;
    uint64 pa = t.poolWith6040();                     // pool V sells KOIN
    uint64 pb = t.secondPool();                       // pool X sells contract shares at price 1
    t.initPayerWithHolders({ { OUTSIDER, 300 }, { CAROL, 376 } });
    t.giveKoin(OUTSIDER, 1000);
    const Asset shares = T::payerShares();
    const uint64 CTR = TESTEXB_CONTRACT_INDEX;
    uint64 hxV = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    ASSERT_EQ(t.qxAsk(OUTSIDER, shares, 900, 200), 200);
    auto b = t.buyQxOf(shares, CTR, pb, CAROL, CAROL, DAVE, 250000, 200, 1000, DAVE);
    ASSERT_EQ(b.returnCode, QPOOL_OK); ASSERT_EQ(b.acquired, 200ULL);
    uint64 hxX = b.holdingId;
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hxV, 100, 1000).returnCode, QPOOL_OK);
    ASSERT_EQ(t.sellLimitOf(shares, pb, CAROL, CAROL, DAVE, hxX, 200, 1, DAVE).returnCode, QPOOL_OK);
    ASSERT_EQ(t.qxBid(OUTSIDER, t.token(), 1000, 100), 100);                           // V's sell fills: 99,699 reach the counter
    EXPECT_EQ(t.getStats().globalUnattributed, 99699ULL);
    for (int i = 0; i < 200; i++) ASSERT_EQ(t.qxBid(OUTSIDER, shares, 1, 1), 1) << i;  // X's 200 shares sell one by one: each fill pays QPOOL 0
    EXPECT_EQ(t.getStats().globalUnattributed, 99699ULL);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hxX), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hxX).active, 0ULL);                                          // sold out
    EXPECT_EQ(t.owedB(hxX), 0ULL);                                                      // and credited nothing: nothing arrived for it
    EXPECT_EQ(t.getStats().globalUnattributed, 99699ULL) << "V's proceeds must still be all there";
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hxV), QPOOL_OK);
    EXPECT_GE(t.owedA(hxV), 98604ULL - 3); EXPECT_LE(t.owedA(hxV), 98604ULL);          // (100,000 - 300 - 100 shares = 99,600) less 1%
    EXPECT_EQ(t.getStats().globalUnattributed, 0ULL);                                   // the remainder of V's fee bound went to revenue with the last ask
}

// A single share donated to QPOOL must not leave a sold ask booked for ever: QX's book, not the share count, says what sold.
TEST(RestingSell, ADonatedShareCannotLeaveASoldAskStuck)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hx = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hx, 100, 2000).returnCode, QPOOL_OK);
    t.giveKoin(QPOOL_ID, 1);                                                            // a stranger donates one share
    ASSERT_EQ(t.qxBid(OUTSIDER, t.token(), 2000, 100), 100);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hx).active, 0ULL) << "the sold ask must be credited and retired";
    EXPECT_EQ(t.getHolding(hx).askShares, 0ULL);
    EXPECT_FALSE(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).found);
    EXPECT_EQ(t.getState()->restingAsks, 0ULL);
    // and the token can be bid for again
    uint64 pb = t.secondPool();
    EXPECT_EQ(t.propose(CAROL, pb, QPOOL_KIND_BUY_QX, t.token(), 100000, 50, 800, 0).returnCode, QPOOL_OK);
}

// EvictSquat took a deposited holding's free shares, retired the holding and dropped the ledger even while a sell of it rested on QX.
TEST(RestingSell, EvictSquatRefusesAHoldingThatRestsASell)
{
    T t;
    uint64 rival = t.issueSecond("ZOIN");
    t.giveSecond(OUTSIDER, 5);
    t.giveKoin(OUTSIDER, 1000);
    auto c = t.createPool(CREATOR, POOL_NAME + 40, 0);
    ASSERT_EQ(c.returnCode, QPOOL_OK);
    uint64 pool = c.poolId;
    ASSERT_EQ(t.contribute(CREATOR, pool, 300000).returnCode, QPOOL_OK);
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 100), 100);
    t.advanceTicks(1);
    auto p = t.propose(CREATOR, pool, QPOOL_KIND_BUY_QX, t.token(), 150000, 100, 1000, 0);
    ASSERT_EQ(p.returnCode, QPOOL_OK);
    ASSERT_EQ(t.vote(CREATOR, p.proposalId, 1).returnCode, QPOOL_OK);
    t.advanceTicks(QPOOL_VOTE_WINDOW + 1);
    ASSERT_EQ(t.tally(CREATOR, p.proposalId).status, QPOOL_PROP_PASSED);
    auto e = t.execute(CREATOR, p.proposalId);
    ASSERT_EQ(e.returnCode, QPOOL_OK); ASSERT_EQ(e.acquired, 100ULL);
    uint64 hx = e.holdingId;
    t.markDeposited(hx);                                                                // (stands in for a deposit: the creator is the only seat owner)
    t.advanceEpochs(QPOOL_SQUAT_EPOCHS + 1);
    // control: nothing rests - every guard of EvictSquat passes and the call reaches the transfer, which fails here only because the shares are QX-managed
    EXPECT_EQ(t.evictSquat(OUTSIDER, hx, rival), QPOOL_ERR_TRANSFER_FAILED);
    // 60 shares rest on QX
    t.advanceTicks(1);
    auto q = t.propose(CREATOR, pool, QPOOL_KIND_SELL_QX_LIMIT, t.token(), 0, 60, 2000, hx);
    ASSERT_EQ(q.returnCode, QPOOL_OK);
    ASSERT_EQ(t.vote(CREATOR, q.proposalId, 1).returnCode, QPOOL_OK);
    t.advanceTicks(QPOOL_VOTE_WINDOW + 1);
    ASSERT_EQ(t.tally(CREATOR, q.proposalId).status, QPOOL_PROP_PASSED);
    ASSERT_EQ(t.execute(CREATOR, q.proposalId).returnCode, QPOOL_OK);
    ASSERT_EQ(t.getHolding(hx).askShares, 60ULL); ASSERT_EQ(t.getHolding(hx).shares, 40ULL);
    auto snap = t.snapshot();
    EXPECT_EQ(t.evictSquat(OUTSIDER, hx, rival), QPOOL_ERR_WRONG_STATUS);
    EXPECT_TRUE(t.sameAs(snap));
    EXPECT_EQ(t.getHolding(hx).active, 1ULL);
}

// A BuyAndBurnQpay bid would trade against QPOOL's own resting ask of the QPAY token: no resting sell of it.
TEST(RestingSell, NoRestingSellOfTheQpayToken)
{
    T t;
    uint64 qpay = t.issueSecond("QPAY");
    EXPECT_EQ(qpay, QPOOL_QPAY_ASSET_NAME);
    t.giveSecond(OUTSIDER, 100);
    uint64 pa = t.poolWith6040();
    const Asset a{ TOKEN_ISSUER, qpay };
    ASSERT_EQ(t.qxAsk(OUTSIDER, a, 900, 100), 100);
    auto b = t.buyQxOf(a, 0, pa, ALICE, ALICE, BOB, 150000, 100, 1000);
    ASSERT_EQ(b.returnCode, QPOOL_OK); ASSERT_EQ(b.acquired, 100ULL);
    t.advanceTicks(1);
    EXPECT_EQ(t.propose(ALICE, pa, QPOOL_KIND_SELL_QX_LIMIT, a, 0, 100, 2000, b.holdingId).returnCode, QPOOL_ERR_INVALID_INPUT);
}

// A seat of a holding that rests a sell cannot be bought (a buyer cannot bind askShares, and a silent sale credited by anyone would strip the backing he read);
// with dividends set aside for its unsold shares the same holds. It can be bought again once the sell is taken back.
TEST(RestingSell, ASeatOfAHoldingThatRestsASellCannotBeBought)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.initPayerWithHolders({ { OUTSIDER, 300 }, { CAROL, 376 } });
    const Asset shares = T::payerShares();
    const uint64 CTR = TESTEXB_CONTRACT_INDEX;
    ASSERT_EQ(t.qxAsk(OUTSIDER, shares, 900, 100), 100);
    auto a = t.buyQxOf(shares, CTR, pa, ALICE, ALICE, BOB, 150000, 100, 1000);
    ASSERT_EQ(a.returnCode, QPOOL_OK); ASSERT_EQ(a.acquired, 100ULL);
    uint64 hx = a.holdingId;
    ASSERT_EQ(t.sellLimitOf(shares, pa, ALICE, ALICE, BOB, hx, 100, 2000).returnCode, QPOOL_OK);
    ASSERT_TRUE(t.payerDistributes(1000, OUTSIDER));                                    // 99,000 net set aside for the holding
    ASSERT_EQ(t.getState()->holdings.get(hx).askEscrow, 99000ULL);
    uint64 cost = t.getPosition(hx, ALICE).cost;
    ASSERT_GT(cost, 0ULL);
    EXPECT_EQ(t.offerPosition(ALICE, hx, cost, 50000), QPOOL_OK);
    auto snap = t.snapshot();
    auto r = t.buyPosition(DAVE, hx, ALICE, cost, t.getPosition(hx, ALICE).offeredShares, 60000 + QPOOL_JOIN_FEE);
    EXPECT_EQ(r.returnCode, QPOOL_ERR_SETTLEMENT_PENDING);
    EXPECT_TRUE(t.sameAs(snap));
}

// The same rule on a token without dividends: refusal while the sell rests, purchase once it is taken back.
TEST(RestingSell, ASeatCanBeBoughtOnceTheSellIsTakenBack)
{
    T t;
    uint64 pa = t.poolWith6040();
    t.giveKoin(OUTSIDER, 1000);
    uint64 hx = holdKoin(t, pa, ALICE, ALICE, BOB, CREATOR, 100);
    ASSERT_EQ(t.sellLimit(pa, ALICE, ALICE, BOB, hx, 60, 2000).returnCode, QPOOL_OK);      // 60 rest, 40 stay free
    ASSERT_EQ(t.offerPosition(BOB, hx, 20000, 5000), QPOOL_OK);
    auto pos = t.getPosition(hx, BOB);
    ASSERT_EQ(pos.offeredShares, 40ULL);
    auto snap = t.snapshot();
    auto r = t.buyPosition(DAVE, hx, BOB, pos.offeredCostUnits, pos.offeredShares, 5000 + QPOOL_JOIN_FEE);
    EXPECT_EQ(r.returnCode, QPOOL_ERR_SETTLEMENT_PENDING);
    EXPECT_TRUE(t.sameAs(snap));
    ASSERT_EQ(t.cancelQxAsk(CREATOR, pa, hx).returnCode, QPOOL_OK);
    pos = t.getPosition(hx, BOB);
    auto ok = t.buyPosition(DAVE, hx, BOB, pos.offeredCostUnits, pos.offeredShares, 5000 + QPOOL_JOIN_FEE);
    EXPECT_EQ(ok.returnCode, QPOOL_OK);
}

// Dividends set aside for ONE holding's unsold shares do not hold back the seat trades of the other holdings of the ledger.
TEST(RestingSell, EarmarkedDividendsDoNotBlockAnotherHoldingsSeatTrade)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.initPayerWithHolders({ { OUTSIDER, 300 }, { CAROL, 376 } });
    const Asset shares = T::payerShares();
    const uint64 CTR = TESTEXB_CONTRACT_INDEX;
    ASSERT_EQ(t.qxAsk(OUTSIDER, shares, 900, 200), 200);
    auto a = t.buyQxOf(shares, CTR, pa, ALICE, ALICE, BOB, 150000, 100, 1000);
    auto b = t.buyQxOf(shares, CTR, pb, CAROL, CAROL, DAVE, 150000, 100, 1000, DAVE);
    ASSERT_EQ(a.returnCode, QPOOL_OK); ASSERT_EQ(b.returnCode, QPOOL_OK);
    uint64 hxA = a.holdingId, hxB = b.holdingId;
    ASSERT_EQ(t.sellLimitOf(shares, pa, ALICE, ALICE, BOB, hxA, 100, 2000).returnCode, QPOOL_OK);   // only A rests a sell
    ASSERT_TRUE(t.payerDistributes(1000, OUTSIDER));                                               // half of the net is set aside for A's unsold 100
    ASSERT_GT(t.getState()->holdings.get(hxA).askEscrow, 0ULL);
    ASSERT_EQ(t.offerPosition(DAVE, hxB, t.getPosition(hxB, DAVE).cost, 50000), QPOOL_OK);
    auto pos = t.getPosition(hxB, DAVE);
    auto r = t.buyPosition(ALICE, hxB, DAVE, pos.offeredCostUnits, pos.offeredShares, 50000 + QPOOL_JOIN_FEE);
    EXPECT_EQ(r.returnCode, QPOOL_OK) << "B's seat is not A's business";
}

TEST(RestingSell, DividendsOnUnsoldSharesGoToTheAsksThatStillHoldThem)
{
    T t;
    uint64 pa = t.poolWith6040();
    uint64 pb = t.secondPool();
    t.initPayerWithHolders({ { OUTSIDER, 300 }, { CAROL, 376 } });
    const Asset shares = T::payerShares();
    const uint64 CTR = TESTEXB_CONTRACT_INDEX;
    ASSERT_EQ(t.qxAsk(OUTSIDER, shares, 900, 200), 200);
    auto a = t.buyQxOf(shares, CTR, pa, ALICE, ALICE, BOB, 150000, 100, 1000);                      // A: 100 shares
    auto b = t.buyQxOf(shares, CTR, pb, CAROL, CAROL, DAVE, 150000, 100, 1000, DAVE);               // B: 100 shares
    ASSERT_EQ(a.returnCode, QPOOL_OK); ASSERT_EQ(b.returnCode, QPOOL_OK);
    ASSERT_EQ(a.acquired, 100ULL); ASSERT_EQ(b.acquired, 100ULL);
    uint64 hxA = a.holdingId, hxB = b.holdingId;
    ASSERT_EQ(t.sellLimitOf(shares, pa, ALICE, ALICE, BOB, hxA, 100, 1800).returnCode, QPOOL_OK);              // A: 100 @ 1,800
    ASSERT_EQ(t.sellLimitOf(shares, pb, CAROL, CAROL, DAVE, hxB, 100, 2000, DAVE).returnCode, QPOOL_OK);      // B: 100 @ 2,000
    EXPECT_EQ(t.getHolding(hxA).shares, 0ULL); EXPECT_EQ(t.getHolding(hxB).shares, 0ULL);
    // a dividend of 1,000 a share on the 200 QPOOL still holds: all of it is set aside for the asks, half each
    ASSERT_TRUE(t.payerDistributes(1000, OUTSIDER));
    EXPECT_EQ(t.getState()->holdings.get(hxA).askEscrow, 99000ULL);
    EXPECT_EQ(t.getState()->holdings.get(hxB).askEscrow, 99000ULL);
    // a buyer takes 120: A's 100 at 1,800 and 20 of B's at 2,000
    ASSERT_EQ(t.qxBid(OUTSIDER, shares, 2000, 120), 120);
    // a second dividend of 500 on the 80 that are left: they are B's (the cheap ones sold first), so it is all B's
    ASSERT_TRUE(t.payerDistributes(500, OUTSIDER));
    EXPECT_EQ(t.getState()->holdings.get(hxA).askEscrow, 99000ULL);
    EXPECT_EQ(t.getState()->holdings.get(hxB).askEscrow, 99000ULL + 39600ULL);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hxA), QPOOL_OK);
    EXPECT_EQ(t.getHolding(hxA).active, 0ULL);                                                   // A sold out
    EXPECT_EQ(t.getHolding(hxB).askShares, 80ULL);
    EXPECT_EQ(t.getStats().globalUnattributed, 118ULL);                                 // 219,338 arrived, 219,220 credited
    // A: 99,000 dividend + (180,000 - 640) less 1%;  B: 138,600 dividend + (40,000 - 140) less 1%
    EXPECT_GE(t.owedA(hxA), 99000ULL + 177567ULL - 4); EXPECT_LE(t.owedA(hxA), 99000ULL + 177567ULL);
    EXPECT_GE(t.owedB(hxB), 138600ULL + 39462ULL - 4);  EXPECT_LE(t.owedB(hxB), 138600ULL + 39462ULL);
    sint64 slack = t.unaccounted() - (sint64)(t.owedA(hxA) + t.owedB(hxB));
    EXPECT_GE(slack, 0); EXPECT_LE(slack, 10);
}
#endif // QPOOL_HAS_K83


TEST(Size, PrintStateSize)
{
    printf("QPOOL_SIZEOF StateData=%zu Holding=%zu SourceLedger=%zu\n", sizeof(QPOOL::StateData), sizeof(QPOOL::Holding), sizeof(QPOOL::SourceLedger));
}
#endif // QPOOL_HAS_CANCEL
