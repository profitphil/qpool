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
    QP_CANCEL_QX_BID = 23, QP_SET_POOL_ADMINS = 24,
};
enum QPoolFuncIds { QP_GET_POOL = 1, QP_GET_MEMBER = 2, QP_GET_HOLDING = 3, QP_GET_POSITION = 4, QP_GET_PROPOSAL = 5, QP_GET_STATS = 6, QP_GET_SOURCE = 7 };

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
        uint64 quAmount, uint64 shares, uint64 price)
    {
        advanceTicks(1);
        auto p = propose(proposer, poolId, QPOOL_KIND_BUY_QX, a, quAmount, shares, price, holdingIdField);
        EXPECT_EQ(p.returnCode, QPOOL_OK);
        passFor(p.proposalId, m1, m2);
        return execute(CREATOR, p.proposalId);
    }

    // The record of the resting bid as the LEDGER holds it (GetHolding shows it only for a holding that has pendingShares).
#ifdef QPOOL_HAS_CANCEL
    bool ledgerRecord(const id& source, uint64 kind, uint64& price, uint64& tick)
    {
        QPOOL::SourceKey k; k.source = source; k.kind = kind;
        QPOOL::SourceLedger led{ uint128(0, 0), uint128(0, 0), uint128(0, 0) };
        if (!getState()->sources.get(k, led)) return false;
        price = led.qxBidPrice; tick = led.qxBidTick;
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

    // A second pool's BUY_QX on the same asset is refused at Propose.
    EXPECT_EQ(t.propose(CAROL, pb, QPOOL_KIND_BUY_QX, t.token(), 100000, 50, 1500, 0).returnCode, QPOOL_ERR_QX_BID_PENDING);

    // OUTSIDER sells all 100 into the resting bid (at the bid's own price, 1,000): QPOOL notices at ReconcileHolding.
    ASSERT_EQ(t.qxAsk(OUTSIDER, t.token(), 900, 100), 100);
    EXPECT_EQ(t.qpoolHolds(t.token()), 100);
    EXPECT_EQ(t.getHolding(hx).shares, 0ULL);
    EXPECT_EQ(t.reconcileHolding(OUTSIDER, hx), QPOOL_OK);
    h = t.getHolding(hx);
    EXPECT_EQ(h.shares, 100ULL);
    EXPECT_EQ(h.pendingShares, 0ULL);
    EXPECT_EQ(t.getSource(TOKEN_ISSUER, QPOOL_SOURCE_TOKEN).pendingQx, 0ULL);
    EXPECT_EQ(t.propose(CAROL, pb, QPOOL_KIND_BUY_QX, t.token(), 100000, 50, 1500, 0).returnCode, QPOOL_OK);
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
    // the ledger itself (a ledger that is gone holds no record)
    uint64 lp = 99, lt = 99;
    if (t.ledgerRecord(source, kind, lp, lt))
    {
        EXPECT_EQ(lp, price);
        EXPECT_EQ(lt, tick);
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
    EXPECT_EQ(t.propose(CAROL, pb, QPOOL_KIND_BUY_QX, t.token(), 100000, 50, 1500, 0).returnCode, QPOOL_ERR_QX_BID_PENDING);

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
    // and the first pool is blocked now
    EXPECT_EQ(t.propose(ALICE, pa, QPOOL_KIND_BUY_QX, t.token(), 100000, 50, 1500, 0).returnCode, QPOOL_ERR_QX_BID_PENDING);
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
    QPOOL::SourceLedger led{ uint128(0, 0), uint128(0, 0), uint128(0, 0) };
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

#endif // QPOOL_HAS_CANCEL
