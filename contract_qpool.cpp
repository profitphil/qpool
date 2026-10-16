#define NO_UEFI

#include "contract_testing.h"

// Pause contract logging for the whole binary (same rationale as
// contract_qtreat.cpp: the logging path does misaligned stores that abort under
// the clang test build's -fsanitize=alignment).
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

// A contract's own identity is id(contractIndex, 0, 0, 0).
static const id QPOOL_ID = id(QPOOL_CONTRACT_INDEX, 0, 0, 0);

static constexpr uint64 POOL_NAME = 1;
static constexpr uint64 TOKEN_NAME = 1413629259ULL; // "KOIN"

enum QPoolProcIds
{
    QP_CREATE_POOL = 1,
    QP_CONTRIBUTE = 2,
    QP_WITHDRAW = 3,
    QP_PROPOSE = 4,
    QP_VOTE = 5,
    QP_TALLY = 6,
    QP_EXECUTE = 7,
    QP_RECONCILE_HOLDING = 8,
    QP_CLAIM_PAYOUT = 9,
    QP_OFFER_POSITION = 10,
    QP_CANCEL_OFFER = 11,
    QP_BUY_POSITION = 12,
    QP_REBID_IPO = 13,
    QP_SETTLE_IPO = 14,
    QP_RECONCILE_IPO = 15,
    QP_DEPOSIT_ASSET = 16,
    QP_SET_ADMISSION = 17,
    QP_SET_PERMISSIONED = 18,
};

enum QPoolFuncIds
{
    QP_GET_POOL = 1,
    QP_GET_MEMBER = 2,
    QP_GET_HOLDING = 3,
    QP_GET_POSITION = 4,
    QP_GET_PROPOSAL = 5,
    QP_GET_STATS = 6,
};

class ContractTestingQPool : protected ContractTesting
{
public:
    ContractTestingQPool()
    {
        initEmptySpectrum();
        initEmptyUniverse();
        system.epoch = contractDescriptions[QPOOL_CONTRACT_INDEX].constructionEpoch;
        INIT_CONTRACT(QPOOL);
        callSystemProcedure(QPOOL_CONTRACT_INDEX, INITIALIZE);

        __pauseLogMessage();
        increaseEnergy(CREATOR, 0);
        increaseEnergy(ALICE, 0);
        increaseEnergy(BOB, 0);
        increaseEnergy(CAROL, 0);
        increaseEnergy(DAVE, 0);
        increaseEnergy(OUTSIDER, 0);
        increaseEnergy(TOKEN_ISSUER, 0);
    }

    QPOOL::StateData* getState()
    {
        return (QPOOL::StateData*)contractStates[QPOOL_CONTRACT_INDEX];
    }

    void endEpoch() { callSystemProcedure(QPOOL_CONTRACT_INDEX, END_EPOCH); }
    void advanceTicks(uint64 n) { system.tick += (unsigned int)n; }

    // ---- procedures ----

    QPOOL::CreatePool_output createPool(const id& from, uint64 name, uint64 permissioned,
        uint64 fee = QPOOL_CREATE_POOL_FEE)
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

    QPOOL::WithdrawUncommitted_output withdraw(const id& from, uint64 poolId, uint64 amount)
    {
        QPOOL::WithdrawUncommitted_input input{ poolId, amount };
        QPOOL::WithdrawUncommitted_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_WITHDRAW, input, output, from, 0);
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

    QPOOL::ClaimPayout_output claimPayout(const id& from, uint64 holdingId)
    {
        QPOOL::ClaimPayout_input input{ holdingId };
        QPOOL::ClaimPayout_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_CLAIM_PAYOUT, input, output, from, 0);
        return output;
    }

    uint32 offerPosition(const id& from, uint64 holdingId, uint64 costUnits, uint64 askPrice)
    {
        QPOOL::OfferPosition_input input{ holdingId, costUnits, askPrice };
        QPOOL::OfferPosition_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_OFFER_POSITION, input, output, from, 0);
        return output.returnCode;
    }

    uint32 cancelOffer(const id& from, uint64 holdingId)
    {
        QPOOL::CancelOffer_input input{ holdingId };
        QPOOL::CancelOffer_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_CANCEL_OFFER, input, output, from, 0);
        return output.returnCode;
    }

    QPOOL::BuyPosition_output buyPosition(const id& from, uint64 holdingId, const id& seller, uint64 pay)
    {
        QPOOL::BuyPosition_input input{ holdingId, seller };
        QPOOL::BuyPosition_output output;
        increaseEnergy(from, pay);
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_BUY_POSITION, input, output, from, pay);
        return output;
    }

    QPOOL::DepositHoldingAsset_output depositAsset(const id& from, uint64 poolId,
        const Asset& asset, uint64 amount, uint64 seatUnits)
    {
        QPOOL::DepositHoldingAsset_input input{ poolId, asset, amount, seatUnits };
        QPOOL::DepositHoldingAsset_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_DEPOSIT_ASSET, input, output, from, 0);
        return output;
    }

    uint32 setAdmission(const id& from, uint64 poolId, const id& member, uint64 allowed)
    {
        QPOOL::SetAdmission_input input{ poolId, member, allowed };
        QPOOL::SetAdmission_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_SET_ADMISSION, input, output, from, 0);
        return output.returnCode;
    }

    uint32 setPermissioned(const id& from, uint64 poolId, uint64 permissioned)
    {
        QPOOL::SetPermissioned_input input{ poolId, permissioned };
        QPOOL::SetPermissioned_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_SET_PERMISSIONED, input, output, from, 0);
        return output.returnCode;
    }

    QPOOL::SettleIpo_output settleIpo(const id& from, uint64 holdingId)
    {
        QPOOL::SettleIpo_input input{ holdingId };
        QPOOL::SettleIpo_output output;
        invokeUserProcedure(QPOOL_CONTRACT_INDEX, QP_SETTLE_IPO, input, output, from, 0);
        return output;
    }

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

    QPOOL::GetProposal_output getProposal(uint64 proposalId)
    {
        QPOOL::GetProposal_input input{ proposalId };
        QPOOL::GetProposal_output output;
        callFunction(QPOOL_CONTRACT_INDEX, QP_GET_PROPOSAL, input, output);
        return output;
    }

    QPOOL::GetStats_output getStats()
    {
        QPOOL::GetStats_input input;
        QPOOL::GetStats_output output;
        callFunction(QPOOL_CONTRACT_INDEX, QP_GET_STATS, input, output);
        return output;
    }

    // ---- helpers ----

    Asset token() const { return Asset{ TOKEN_ISSUER, TOKEN_NAME }; }

    // Create an open pool with two contributors, 60/40.
    uint64 poolWith6040()
    {
        auto c = createPool(CREATOR, POOL_NAME, 0);
        EXPECT_EQ(c.returnCode, QPOOL_OK);
        // Both are first-time joins, so each attaches QPOOL_JOIN_FEE on top
        // of the 600000/400000 they actually want left as uncommitted.
        EXPECT_EQ(contribute(ALICE, c.poolId, 600000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
        EXPECT_EQ(contribute(BOB, c.poolId, 400000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
        return c.poolId;
    }

    // Drive a proposal to PASSED with unanimous approval from both members.
    void passProposal(uint64 proposalId)
    {
        EXPECT_EQ(vote(ALICE, proposalId, 1).returnCode, QPOOL_OK);
        EXPECT_EQ(vote(BOB, proposalId, 1).returnCode, QPOOL_OK);
        advanceTicks(QPOOL_VOTE_WINDOW + 1);
        auto t = tally(CREATOR, proposalId);
        EXPECT_EQ(t.returnCode, QPOOL_OK);
        EXPECT_EQ(t.status, QPOOL_PROP_PASSED);
    }

    // Build a holding directly, bypassing the acquisition path, so payout and
    // seat-market behaviour can be tested without a live QSWAP pool.
    // cost is split 60/40 between Alice and Bob to mirror poolWith6040().
    uint64 fabricateHolding(uint64 poolId, uint64 costBasis)
    {
        // Holding embeds a uint128, which has no default constructor, so take a
        // copy of an untouched (zero-initialised) slot as the template.
        uint64 hid = getState()->holdingCount;
        QPOOL::Holding h = getState()->holdings.get(hid);
        h.poolId = poolId;
        h.asset = token();
        h.shares = 1000;
        h.costBasis = costBasis;
        h.active = 1;
        getState()->holdings.set(hid, h);
        getState()->holdingCount = hid + 1;

        QPOOL::OwnKey ok;
        QPOOL::Ownership own{ 0, uint128(0, 0), 0 };
        own.cost = (costBasis * 60) / 100;
        ok.member = ALICE; ok.holdingId = hid;
        getState()->ownership.set(ok, own);
        own.cost = costBasis - ((costBasis * 60) / 100);
        ok.member = BOB;
        getState()->ownership.set(ok, own);

        // Mirror the deployment in pool + member accounting.
        auto p = getState()->pools.get(poolId);
        p.uncommittedTotal = (p.uncommittedTotal > costBasis) ? p.uncommittedTotal - costBasis : 0;
        p.committedTotal += costBasis;
        p.holdingCount += 1;
        getState()->pools.set(poolId, p);
        return hid;
    }

    // Credit a payout against a holding the way a dividend would, including the
    // management fee skim, so claim maths can be asserted.
    void creditPayout(uint64 holdingId, uint64 amount)
    {
        auto h = getState()->holdings.get(holdingId);
        uint64 fee = (amount * QPOOL_MANAGEMENT_PERMILLE) / 1000;
        uint64 net = amount - fee;
        getState()->pendingRevenue += fee;
        getState()->totalRevenue += fee;
        getState()->totalManagementFees += fee;
        uint128 scaled(0, 0);
        scaled.high = net;
        scaled.low = 0;
        h.payoutPerCostX64 = h.payoutPerCostX64 + div(scaled, (uint128)h.costBasis);
        h.totalPayouts += net;
        getState()->holdings.set(holdingId, h);
        increaseEnergy(QPOOL_ID, amount);
    }
};

// ======================== Pool creation & membership ========================

TEST(TestQPool, CreatePoolChargesFeeAndValidates)
{
    ContractTestingQPool t;

    // Underpaying the registration fee is refused and refunded.
    sint64 before = getBalance(CREATOR);
    EXPECT_EQ(t.createPool(CREATOR, POOL_NAME, 0, QPOOL_CREATE_POOL_FEE - 1).returnCode,
              QPOOL_ERR_INSUFFICIENT_FEE);
    EXPECT_EQ(getBalance(CREATOR) - before, (sint64)(QPOOL_CREATE_POOL_FEE - 1));

    EXPECT_EQ(t.createPool(CREATOR, 0, 0).returnCode, QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.createPool(CREATOR, POOL_NAME, 2).returnCode, QPOOL_ERR_INVALID_INPUT);

    auto c = t.createPool(CREATOR, POOL_NAME, 0);
    EXPECT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(c.poolId, 0ULL);

    auto p = t.getPool(0);
    EXPECT_EQ(p.found, 1u);
    EXPECT_EQ(p.creator, CREATOR);
    EXPECT_EQ(p.permissioned, 0ULL);

    // The fee becomes revenue rather than being burned outright.
    EXPECT_EQ(t.getStats().pendingRevenue, QPOOL_CREATE_POOL_FEE);
    EXPECT_EQ(t.getStats().totalRevenue, QPOOL_CREATE_POOL_FEE);
}

TEST(TestQPool, ContributeTracksUncommittedAndRoster)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();

    auto p = t.getPool(pid);
    EXPECT_EQ(p.uncommittedTotal, 1000000ULL);
    EXPECT_EQ(p.contributorCount, 2ULL);
    EXPECT_EQ(t.getMember(pid, ALICE).uncommitted, 600000ULL);
    EXPECT_EQ(t.getMember(pid, BOB).uncommitted, 400000ULL);

    // Topping up accumulates without adding a roster slot.
    EXPECT_EQ(t.contribute(ALICE, pid, 100000).returnCode, QPOOL_OK);
    EXPECT_EQ(t.getMember(pid, ALICE).uncommitted, 700000ULL);
    EXPECT_EQ(t.getPool(pid).contributorCount, 2ULL);

    // Dust below the minimum is refused and refunded.
    sint64 before = getBalance(CAROL);
    EXPECT_EQ(t.contribute(CAROL, pid, QPOOL_MIN_CONTRIBUTION - 1).returnCode, QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(getBalance(CAROL) - before, (sint64)(QPOOL_MIN_CONTRIBUTION - 1));
}

// QPOOL_JOIN_FEE prices becoming a NEW member of a pool -- a one-time cost,
// charged only the first time an address ever contributes to THIS pool.
// Repeat top-ups from an already-joined member are free of it, same as the
// create-pool fee only ever applies once per pool, not per contribution.
TEST(TestQPool, JoinFeeChargedOnceOnFirstContributionOnly)
{
    ContractTestingQPool t;
    auto c = t.createPool(CREATOR, POOL_NAME, 0);
    EXPECT_EQ(c.returnCode, QPOOL_OK);
    uint64 revenueBefore = t.getStats().pendingRevenue;

    // Below (join fee + minimum) is refused and fully refunded -- a
    // first-timer cannot sneak in under just the ordinary minimum.
    sint64 before = getBalance(ALICE);
    EXPECT_EQ(t.contribute(ALICE, c.poolId, QPOOL_JOIN_FEE + QPOOL_MIN_CONTRIBUTION - 1).returnCode,
              QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(getBalance(ALICE) - before, (sint64)(QPOOL_JOIN_FEE + QPOOL_MIN_CONTRIBUTION - 1));

    // First real contribution: the join fee is skimmed into pool revenue,
    // only the remainder becomes her uncommitted balance.
    EXPECT_EQ(t.contribute(ALICE, c.poolId, 500000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
    EXPECT_EQ(t.getMember(c.poolId, ALICE).uncommitted, 500000ULL);
    EXPECT_EQ(t.getStats().pendingRevenue, revenueBefore + QPOOL_JOIN_FEE);
    EXPECT_EQ(t.getStats().totalRevenue, revenueBefore + QPOOL_JOIN_FEE);

    // A repeat top-up from the SAME member pays none of it: the ordinary
    // minimum alone is enough, and the full amount lands as uncommitted.
    uint64 revenueAfterJoin = t.getStats().pendingRevenue;
    EXPECT_EQ(t.contribute(ALICE, c.poolId, QPOOL_MIN_CONTRIBUTION).returnCode, QPOOL_OK);
    EXPECT_EQ(t.getMember(c.poolId, ALICE).uncommitted, 500000ULL + QPOOL_MIN_CONTRIBUTION);
    EXPECT_EQ(t.getStats().pendingRevenue, revenueAfterJoin);
}

TEST(TestQPool, UndeployedCapitalIsWithdrawable)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();

    EXPECT_EQ(t.withdraw(ALICE, pid, 700000).returnCode, QPOOL_ERR_INSUFFICIENT_BALANCE);
    EXPECT_EQ(t.withdraw(OUTSIDER, pid, 1).returnCode, QPOOL_ERR_NOT_MEMBER);

    sint64 before = getBalance(ALICE);
    auto w = t.withdraw(ALICE, pid, 200000);
    EXPECT_EQ(w.returnCode, QPOOL_OK);
    EXPECT_EQ(w.withdrawn, 200000ULL);
    EXPECT_EQ(getBalance(ALICE) - before, 200000LL);
    EXPECT_EQ(t.getMember(pid, ALICE).uncommitted, 400000ULL);
    EXPECT_EQ(t.getPool(pid).uncommittedTotal, 800000ULL);
}

// ======================== Governance ========================

TEST(TestQPool, ProposingIsFreeAndLimitedToOneLivePerMember)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();

    // Free: nothing is deducted.
    sint64 before = getBalance(ALICE);
    auto p1 = t.propose(ALICE, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 100000, 50, 0, 0);
    EXPECT_EQ(p1.returnCode, QPOOL_OK);
    EXPECT_EQ(getBalance(ALICE), before);
    // unchanged by proposing -- just the create-pool fee + both members' join fees from poolWith6040()
    EXPECT_EQ(t.getStats().pendingRevenue, QPOOL_CREATE_POOL_FEE + 2 * QPOOL_JOIN_FEE);

    // Second live proposal from the same member in the same pool is refused —
    // this is the spam guard that replaces the old per-proposal fee.
    EXPECT_EQ(t.propose(ALICE, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 100000, 50, 0, 0).returnCode,
              QPOOL_ERR_PROPOSAL_PENDING);

    // A different member is unaffected.
    EXPECT_EQ(t.propose(BOB, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 50000, 20, 0, 0).returnCode,
              QPOOL_OK);

    // Non-members cannot propose.
    EXPECT_EQ(t.propose(OUTSIDER, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 1000, 1, 0, 0).returnCode,
              QPOOL_ERR_NOT_MEMBER);

    // Once the first proposal is resolved, the member may propose again.
    t.advanceTicks(QPOOL_VOTE_WINDOW + 1);
    EXPECT_EQ(t.tally(CREATOR, p1.proposalId).status, QPOOL_PROP_REJECTED); // no quorum
    EXPECT_EQ(t.propose(ALICE, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 100000, 50, 0, 0).returnCode,
              QPOOL_OK);
}

TEST(TestQPool, BuyProposalRequiresSlippageBound)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();

    // shares == 0 means a zero min-shares-out, which would be sandwichable.
    EXPECT_EQ(t.propose(ALICE, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 100000, 0, 0, 0).returnCode,
              QPOOL_ERR_INVALID_INPUT);
    // Asking to deploy more than the pool holds.
    EXPECT_EQ(t.propose(ALICE, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 2000000, 50, 0, 0).returnCode,
              QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.propose(ALICE, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 100000, 50, 0, 0).returnCode,
              QPOOL_OK);
}

TEST(TestQPool, VoteWeightIsUncommittedForBuys)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();
    auto p = t.propose(ALICE, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 100000, 50, 0, 0);

    auto va = t.vote(ALICE, p.proposalId, 1);
    EXPECT_EQ(va.returnCode, QPOOL_OK);
    EXPECT_EQ(va.weight, 600000ULL); // weighted by money at stake

    EXPECT_EQ(t.vote(ALICE, p.proposalId, 1).returnCode, QPOOL_ERR_ALREADY_VOTED);
    EXPECT_EQ(t.vote(OUTSIDER, p.proposalId, 1).returnCode, QPOOL_ERR_NOT_MEMBER);

    auto vb = t.vote(BOB, p.proposalId, 0);
    EXPECT_EQ(vb.weight, 400000ULL);

    auto pr = t.getProposal(p.proposalId);
    EXPECT_EQ(pr.yesWeight, 600000ULL);
    EXPECT_EQ(pr.noWeight, 400000ULL);
    EXPECT_EQ(pr.eligibleWeight, 1000000ULL);
}

TEST(TestQPool, TallyEnforcesQuorumAndMajority)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();

    // Only Bob votes: 400k of 1M cast, below the 50% quorum.
    auto p1 = t.propose(ALICE, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 100000, 50, 0, 0);
    EXPECT_EQ(t.vote(BOB, p1.proposalId, 1).returnCode, QPOOL_OK);
    EXPECT_EQ(t.tally(CREATOR, p1.proposalId).returnCode, QPOOL_ERR_WINDOW_OPEN);
    t.advanceTicks(QPOOL_VOTE_WINDOW + 1);
    EXPECT_EQ(t.tally(CREATOR, p1.proposalId).status, QPOOL_PROP_REJECTED);

    // Quorum met but a tie loses (yes must strictly exceed no).
    auto p2 = t.propose(BOB, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 100000, 50, 0, 0);
    EXPECT_EQ(t.vote(ALICE, p2.proposalId, 0).returnCode, QPOOL_OK);
    EXPECT_EQ(t.vote(BOB, p2.proposalId, 1).returnCode, QPOOL_OK);
    t.advanceTicks(QPOOL_VOTE_WINDOW + 1);
    EXPECT_EQ(t.tally(CREATOR, p2.proposalId).status, QPOOL_PROP_REJECTED);
}

// A capital-weighted vote is only a real commitment if the capital backing
// it can't be pulled back out before the vote resolves. Otherwise one
// wallet could serially veto every proposal in a pool for free: contribute,
// vote no, withdraw, repeat -- with the same QU, at zero cost and zero
// capital ever genuinely at risk.
TEST(TestQPool, VoteThenWithdrawIsBlockedUntilProposalResolves)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();
    auto p = t.propose(BOB, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 100000, 50, 0, 0);

    // Alice votes with her full 600000 uncommitted balance as weight.
    auto va = t.vote(ALICE, p.proposalId, 1);
    EXPECT_EQ(va.returnCode, QPOOL_OK);
    EXPECT_EQ(va.weight, 600000ULL);

    // While her vote is outstanding, none of the QU backing it is
    // withdrawable -- not even a small fraction of it.
    EXPECT_EQ(t.withdraw(ALICE, pid, 1).returnCode, QPOOL_ERR_INSUFFICIENT_BALANCE);
    EXPECT_EQ(t.withdraw(ALICE, pid, 600000).returnCode, QPOOL_ERR_INSUFFICIENT_BALANCE);
    // Her balance itself is untouched -- this is a hold, not a debit.
    EXPECT_EQ(t.getMember(pid, ALICE).uncommitted, 600000ULL);

    // Bob's balance was never locked (he hasn't voted), so his withdrawal is
    // unaffected by Alice's outstanding vote.
    EXPECT_EQ(t.withdraw(BOB, pid, 400000).returnCode, QPOOL_OK);

    // Once the vote resolves (quorum met, Alice's yes clears Bob's absent
    // no), the lock releases and her full balance is withdrawable again.
    t.advanceTicks(QPOOL_VOTE_WINDOW + 1);
    EXPECT_EQ(t.tally(CREATOR, p.proposalId).status, QPOOL_PROP_PASSED);
    EXPECT_EQ(t.withdraw(ALICE, pid, 600000).returnCode, QPOOL_OK);
}

TEST(TestQPool, PassedProposalExpiresOutsideExecuteWindow)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();
    auto p = t.propose(ALICE, pid, QPOOL_KIND_BUY_QSWAP, t.token(), 100000, 50, 0, 0);
    t.passProposal(p.proposalId);

    // A stale mandate must not be firable at a much later price.
    t.advanceTicks(QPOOL_EXECUTE_WINDOW + 1);
    EXPECT_EQ(t.execute(CREATOR, p.proposalId).returnCode, QPOOL_ERR_WINDOW_CLOSED);
    EXPECT_EQ(t.getProposal(p.proposalId).status, QPOOL_PROP_EXPIRED);
}

// ======================== Payouts ========================

TEST(TestQPool, PayoutSplitsProRataByCostBasisAfterManagementFee)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();
    uint64 hid = t.fabricateHolding(pid, 1000000);

    EXPECT_EQ(t.getPosition(hid, ALICE).cost, 600000ULL);
    EXPECT_EQ(t.getPosition(hid, BOB).cost, 400000ULL);

    // 100,000 QU dividend: 1% = 1,000 management fee, 99,000 to members.
    t.creditPayout(hid, 100000);
    EXPECT_EQ(t.getStats().totalManagementFees, 1000ULL);

    // 60/40 of 99,000, less 2 units of Q64.64 fixed-point rounding dust.
    EXPECT_EQ(t.getPosition(hid, ALICE).owed, 59399ULL);
    EXPECT_EQ(t.getPosition(hid, BOB).owed, 39599ULL);

    sint64 before = getBalance(ALICE);
    auto ca = t.claimPayout(ALICE, hid);
    EXPECT_EQ(ca.returnCode, QPOOL_OK);
    EXPECT_EQ(ca.paid, 59399ULL);
    EXPECT_EQ(getBalance(ALICE) - before, 59399LL);

    // Claiming twice pays nothing more.
    EXPECT_EQ(t.claimPayout(ALICE, hid).returnCode, QPOOL_ERR_NOTHING_OWED);
    EXPECT_EQ(t.getPosition(hid, ALICE).owed, 0ULL);
    // Bob is untouched by Alice's claim.
    EXPECT_EQ(t.getPosition(hid, BOB).owed, 39599ULL);

    EXPECT_EQ(t.claimPayout(OUTSIDER, hid).returnCode, QPOOL_ERR_NOT_MEMBER);
}

// A later contributor must not dilute an existing holding's payouts — the
// central promise of the per-acquisition ownership model.
TEST(TestQPool, LaterContributorDoesNotDiluteExistingHolding)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();
    uint64 hid = t.fabricateHolding(pid, 1000000);

    // Carol joins with more capital than the whole existing holding.
    EXPECT_EQ(t.contribute(CAROL, pid, 5000000).returnCode, QPOOL_OK);

    t.creditPayout(hid, 100000);

    // Alice and Bob still split it 60/40; Carol is owed nothing.
    EXPECT_EQ(t.getPosition(hid, ALICE).owed, 59399ULL);
    EXPECT_EQ(t.getPosition(hid, BOB).owed, 39599ULL);
    EXPECT_EQ(t.getPosition(hid, CAROL).found, 0u);
    EXPECT_EQ(t.claimPayout(CAROL, hid).returnCode, QPOOL_ERR_NOT_MEMBER);
}

// ======================== Seat market / buy-outs ========================

TEST(TestQPool, SeatSaleTransfersCostUnitsAndPaysSeller)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();
    uint64 hid = t.fabricateHolding(pid, 1000000);

    EXPECT_EQ(t.offerPosition(ALICE, hid, 700000, 1000), QPOOL_ERR_INSUFFICIENT_BALANCE);
    EXPECT_EQ(t.offerPosition(ALICE, hid, 0, 1000), QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.offerPosition(ALICE, hid, 300000, 50000), QPOOL_OK);

    auto pos = t.getPosition(hid, ALICE);
    EXPECT_EQ(pos.offeredCostUnits, 300000ULL);
    EXPECT_EQ(pos.askPrice, 50000ULL);

    // Underpaying is refused and refunded.
    sint64 before = getBalance(CAROL);
    EXPECT_EQ(t.buyPosition(CAROL, hid, ALICE, 49999).returnCode, QPOOL_ERR_PRICE_TOO_HIGH);
    EXPECT_EQ(getBalance(CAROL) - before, 49999LL);

    sint64 sellerBefore = getBalance(ALICE);
    auto b = t.buyPosition(CAROL, hid, ALICE, 50000);
    EXPECT_EQ(b.returnCode, QPOOL_OK);
    EXPECT_EQ(b.costUnitsBought, 300000ULL);
    // QU goes to the seller, not the pool -- minus the 1% management fee
    // (50000 * 1% = 500), same rate as a dividend or a market sale.
    EXPECT_EQ(getBalance(ALICE) - sellerBefore, 49500LL);

    EXPECT_EQ(t.getPosition(hid, ALICE).cost, 300000ULL);
    EXPECT_EQ(t.getPosition(hid, CAROL).cost, 300000ULL);
    // The asset itself never moved.
    EXPECT_EQ(t.getHolding(hid).shares, 1000ULL);
    EXPECT_EQ(t.getHolding(hid).costBasis, 1000000ULL);

    // The offer is consumed.
    EXPECT_EQ(t.buyPosition(DAVE, hid, ALICE, 50000).returnCode, QPOOL_ERR_NO_OFFER);
}

// A seat transfer must not move payouts that were already earned.
TEST(TestQPool, SeatSaleDoesNotStealAlreadyEarnedPayouts)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();
    uint64 hid = t.fabricateHolding(pid, 1000000);

    t.creditPayout(hid, 100000);
    EXPECT_EQ(t.getPosition(hid, ALICE).owed, 59399ULL);

    // Alice sells her whole seat AFTER the dividend accrued.
    EXPECT_EQ(t.offerPosition(ALICE, hid, 600000, 10000), QPOOL_OK);
    EXPECT_EQ(t.buyPosition(CAROL, hid, ALICE, 10000).returnCode, QPOOL_OK);

    // Alice keeps what she earned while holding the seat.
    EXPECT_EQ(t.getPosition(hid, ALICE).owed, 59399ULL);
    // Carol earned nothing yet, despite now owning the cost units.
    EXPECT_EQ(t.getPosition(hid, CAROL).cost, 600000ULL);
    EXPECT_EQ(t.getPosition(hid, CAROL).owed, 0ULL);

    // A fresh dividend accrues to Carol, not Alice.
    t.creditPayout(hid, 100000);
    EXPECT_EQ(t.getPosition(hid, CAROL).owed, 59399ULL);
    EXPECT_EQ(t.getPosition(hid, ALICE).owed, 59399ULL);

    EXPECT_EQ(t.claimPayout(ALICE, hid).paid, 59399ULL);
    EXPECT_EQ(t.claimPayout(CAROL, hid).paid, 59399ULL);
}

TEST(TestQPool, CancelOfferWorks)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();
    uint64 hid = t.fabricateHolding(pid, 1000000);

    EXPECT_EQ(t.cancelOffer(ALICE, hid), QPOOL_ERR_NO_OFFER);
    EXPECT_EQ(t.offerPosition(ALICE, hid, 100000, 5000), QPOOL_OK);
    EXPECT_EQ(t.cancelOffer(ALICE, hid), QPOOL_OK);
    EXPECT_EQ(t.buyPosition(CAROL, hid, ALICE, 5000).returnCode, QPOOL_ERR_NO_OFFER);
}

// ======================== Permissioned pools ========================

TEST(TestQPool, PermissionedPoolGatesEveryEntryPoint)
{
    ContractTestingQPool t;
    auto c = t.createPool(CREATOR, POOL_NAME, 1);
    EXPECT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(t.getPool(c.poolId).permissioned, 1ULL);

    // Only the creator curates, and only on a permissioned pool.
    EXPECT_EQ(t.setAdmission(ALICE, c.poolId, ALICE, 1), QPOOL_ERR_NOT_AUTHORIZED);

    // Contributing without admission is refused and refunded.
    sint64 before = getBalance(ALICE);
    EXPECT_EQ(t.contribute(ALICE, c.poolId, 500000).returnCode, QPOOL_ERR_NOT_ADMITTED);
    EXPECT_EQ(getBalance(ALICE) - before, 500000LL);

    EXPECT_EQ(t.setAdmission(CREATOR, c.poolId, ALICE, 1), QPOOL_OK);
    EXPECT_EQ(t.setAdmission(CREATOR, c.poolId, BOB, 1), QPOOL_OK);
    EXPECT_EQ(t.contribute(ALICE, c.poolId, 600000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
    EXPECT_EQ(t.contribute(BOB, c.poolId, 400000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);

    // Buying a seat must not be a back door for an unadmitted address.
    uint64 hid = t.fabricateHolding(c.poolId, 1000000);
    EXPECT_EQ(t.offerPosition(ALICE, hid, 100000, 5000), QPOOL_OK);
    before = getBalance(OUTSIDER);
    EXPECT_EQ(t.buyPosition(OUTSIDER, hid, ALICE, 5000).returnCode, QPOOL_ERR_NOT_ADMITTED);
    EXPECT_EQ(getBalance(OUTSIDER) - before, 5000LL);

    // Admitted buyers can still fill it.
    EXPECT_EQ(t.setAdmission(CREATOR, c.poolId, CAROL, 1), QPOOL_OK);
    EXPECT_EQ(t.buyPosition(CAROL, hid, ALICE, 5000).returnCode, QPOOL_OK);
}

TEST(TestQPool, RevokingAdmissionCannotClawBackOwnedSeats)
{
    ContractTestingQPool t;
    auto c = t.createPool(CREATOR, POOL_NAME, 1);
    EXPECT_EQ(t.setAdmission(CREATOR, c.poolId, ALICE, 1), QPOOL_OK);
    EXPECT_EQ(t.setAdmission(CREATOR, c.poolId, BOB, 1), QPOOL_OK);
    EXPECT_EQ(t.contribute(ALICE, c.poolId, 600000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
    EXPECT_EQ(t.contribute(BOB, c.poolId, 400000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
    uint64 hid = t.fabricateHolding(c.poolId, 1000000);

    EXPECT_EQ(t.setAdmission(CREATOR, c.poolId, ALICE, 0), QPOOL_OK);

    // Revocation blocks future joining only: the seat and its payouts remain.
    t.creditPayout(hid, 100000);
    EXPECT_EQ(t.getPosition(hid, ALICE).cost, 600000ULL);
    EXPECT_EQ(t.claimPayout(ALICE, hid).paid, 59399ULL);
    // But she cannot add new capital.
    EXPECT_EQ(t.contribute(ALICE, c.poolId, 100000).returnCode, QPOOL_ERR_NOT_ADMITTED);
}

TEST(TestQPool, SetPermissionedTogglesGateWithoutTouchingExistingMembers)
{
    ContractTestingQPool t;
    auto c = t.createPool(CREATOR, POOL_NAME, 0);  // starts OPEN
    EXPECT_EQ(c.returnCode, QPOOL_OK);
    EXPECT_EQ(t.getPool(c.poolId).permissioned, 0ULL);

    // Anyone can contribute while open.
    EXPECT_EQ(t.contribute(ALICE, c.poolId, 600000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
    EXPECT_EQ(t.contribute(BOB, c.poolId, 400000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);

    // Only the creator may toggle it, value must be 0/1, pool must exist.
    EXPECT_EQ(t.setPermissioned(ALICE, c.poolId, 1), QPOOL_ERR_NOT_AUTHORIZED);
    EXPECT_EQ(t.setPermissioned(CREATOR, c.poolId, 2), QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.setPermissioned(CREATOR, 500, 1), QPOOL_ERR_NOT_FOUND);  // valid range, never created

    // Flip to permissioned.
    EXPECT_EQ(t.setPermissioned(CREATOR, c.poolId, 1), QPOOL_OK);
    EXPECT_EQ(t.getPool(c.poolId).permissioned, 1ULL);

    // Existing members are NOT retroactively admitted -- even ALICE, already
    // a contributor, is locked out of adding MORE capital until explicitly
    // admitted, same as any other permissioned pool.
    sint64 before = getBalance(ALICE);
    EXPECT_EQ(t.contribute(ALICE, c.poolId, 100000).returnCode, QPOOL_ERR_NOT_ADMITTED);
    EXPECT_EQ(getBalance(ALICE) - before, 100000LL);
    // Her EXISTING seat and voting weight are untouched by the toggle.
    EXPECT_EQ(t.getMember(c.poolId, ALICE).uncommitted, 600000ULL);

    // Once admitted, she can add capital again.
    EXPECT_EQ(t.setAdmission(CREATOR, c.poolId, ALICE, 1), QPOOL_OK);
    EXPECT_EQ(t.contribute(ALICE, c.poolId, 100000).returnCode, QPOOL_OK);

    // A brand-new address is refused entirely while permissioned.
    EXPECT_EQ(t.contribute(CAROL, c.poolId, 100000).returnCode, QPOOL_ERR_NOT_ADMITTED);

    // Flip back to open: anyone, admitted or not, can contribute again.
    EXPECT_EQ(t.setPermissioned(CREATOR, c.poolId, 0), QPOOL_OK);
    EXPECT_EQ(t.getPool(c.poolId).permissioned, 0ULL);
    EXPECT_EQ(t.contribute(CAROL, c.poolId, 200000 + QPOOL_JOIN_FEE).returnCode, QPOOL_OK);
}

// ======================== Syndication ========================

TEST(TestQPool, DepositAssetGivesDepositorEverySeat)
{
    ContractTestingQPool t;
    auto c = t.createPool(CREATOR, POOL_NAME, 0);

    // Nothing under SC management yet, so the deposit must fail rather than
    // fabricating a holding out of nothing.
    EXPECT_EQ(t.depositAsset(CREATOR, c.poolId, t.token(), 1000, 1000000).returnCode,
              QPOOL_ERR_INSUFFICIENT_BALANCE);

    EXPECT_EQ(t.depositAsset(CREATOR, c.poolId, t.token(), 0, 1000000).returnCode,
              QPOOL_ERR_INVALID_INPUT);
    EXPECT_EQ(t.depositAsset(CREATOR, c.poolId, t.token(), 1000, 0).returnCode,
              QPOOL_ERR_INVALID_INPUT);
}

// The gap the test above admits: nobody ever exercised the SUCCESS path,
// because nothing in this file grants a wallet real, directly-held
// possession of an asset first. Mirrors contract_testing.h's own
// issueContractShares() pattern to do exactly that.
//
// REAL FINDING while chasing a live "DepositHoldingAsset never succeeds"
// report: DepositHoldingAsset's own precondition check
// (qpi.numberOfShares with byOwner/byPossessor, which use
// anyManagingContract=true per qpi.h's AssetOwnershipSelect/
// AssetPossessionSelect) matches shares under ANY managing contract. But
// the actual transfer it then attempts, qpi.transferShareOwnershipAndPossession,
// is implemented (qpi_asset_impl.h, the ownership/possession
// managingContractIndex checks) to only move shares whose ownership AND
// possession records are ALREADY managed by the calling contract itself
// (QPOOL). So a depositor holding the asset raw, or via QX/QSWAP, passes
// the precondition cleanly and then fails at the transfer with
// QPOOL_ERR_TRANSFER_FAILED. The procedure's own doc comment ("The
// depositor must genuinely hold the shares under SC management") is
// technically correct but easy to misread as "holds it, period" -- this
// test pins down the real requirement: management rights must already be
// with QPOOL (e.g. via QX's TransferShareManagementRights) BEFORE calling
// DepositHoldingAsset. This is a live-usability gap worth documenting
// clearly for integrators, not a QPool.h defect requiring a code change.
TEST(TestQPool, DepositAssetSucceedsWhenAssetIsAlreadyQpoolManaged)
{
    ContractTestingQPool t;
    auto c = t.createPool(CREATOR, POOL_NAME, 0);

    int issuanceIndex, ownershipIndex, possessionIndex;
    long long issued = issueAsset(CREATOR, "DEPTEST", 0, "TEST", 5000000, QPOOL_CONTRACT_INDEX,
        &issuanceIndex, &ownershipIndex, &possessionIndex);
    ASSERT_EQ(issued, 5000000);

    Asset depositToken{ CREATOR, *(uint64*)"DEPTEST" };
    EXPECT_EQ(numberOfShares(depositToken, AssetOwnershipSelect::byOwner(CREATOR),
        AssetPossessionSelect::byPossessor(CREATOR)), 5000000);

    auto out = t.depositAsset(CREATOR, c.poolId, depositToken, 1000000, 1000);
    EXPECT_EQ(out.returnCode, QPOOL_OK);
    // Holding IDs are 0-indexed slot numbers, so 0 is a perfectly valid id
    // for the first holding ever created -- just confirm the position it
    // points to genuinely exists with the right cost.
    if (out.returnCode == QPOOL_OK)
    {
        EXPECT_EQ(t.getPosition(out.holdingId, CREATOR).cost, 1000ULL);
    }
}

// Found live during the mega-comprehensive test: three pools independently
// hit QPOOL_MAX_HOLDINGS_PER_POOL after heavy real use (IPO participation +
// QSWAP/QX buys), which was 16 -- raised to 64 (with QPOOL_MAX_HOLDINGS
// raised in step, 512->4096, so MAX_POOLS*MAX_HOLDINGS_PER_POOL still fits
// exactly in the global array). This pins the new ceiling down: fabricate
// holdings up to one below it, confirm the boundary-th DepositHoldingAsset
// call still succeeds, then confirm the next one correctly refuses with
// QPOOL_ERR_CAPACITY.
TEST(TestQPool, HoldingsPerPoolCapacityRaisedTo64AndStillEnforced)
{
    ContractTestingQPool t;
    auto c = t.createPool(CREATOR, POOL_NAME, 0);

    for (uint64 i = 0; i < QPOOL_MAX_HOLDINGS_PER_POOL - 1; ++i)
    {
        t.fabricateHolding(c.poolId, 1000);
    }
    EXPECT_EQ(t.getPool(c.poolId).holdingCount, QPOOL_MAX_HOLDINGS_PER_POOL - 1);

    int issuanceIndex, ownershipIndex, possessionIndex;
    ASSERT_EQ(issueAsset(CREATOR, "CAPTEST", 0, "TEST", 5000000, QPOOL_CONTRACT_INDEX,
        &issuanceIndex, &ownershipIndex, &possessionIndex), 5000000);
    Asset capToken{ CREATOR, *(uint64*)"CAPTEST" };

    // The 64th holding: should still succeed under the new cap.
    auto lastOk = t.depositAsset(CREATOR, c.poolId, capToken, 1000000, 1000);
    EXPECT_EQ(lastOk.returnCode, QPOOL_OK);
    EXPECT_EQ(t.getPool(c.poolId).holdingCount, QPOOL_MAX_HOLDINGS_PER_POOL);

    // The 65th: must be refused, not silently accepted past the cap.
    auto overCap = t.depositAsset(CREATOR, c.poolId, capToken, 1000000, 1000);
    EXPECT_EQ(overCap.returnCode, QPOOL_ERR_CAPACITY);
}

// ======================== Revenue distribution ========================

TEST(TestQPool, EndEpochSplitsRevenueEightyTwenty)
{
    ContractTestingQPool t;
    // Two pool registrations put 100,000,000 QU of revenue on the books.
    EXPECT_EQ(t.createPool(CREATOR, POOL_NAME, 0).returnCode, QPOOL_OK);
    EXPECT_EQ(t.createPool(ALICE, POOL_NAME + 1, 0).returnCode, QPOOL_OK);
    EXPECT_EQ(t.getStats().pendingRevenue, 100000000ULL);

    t.endEpoch();

    // 80% = 80,000,000 -> 118,343 per share * 676 = 79,999,868 distributed.
    // 20% = 20,000,000 burned. Remainder carries over.
    EXPECT_EQ(t.getStats().totalDistributedToShareholders, 79999868ULL);
    EXPECT_EQ(t.getStats().totalBurned, 20000000ULL);
    EXPECT_EQ(t.getStats().pendingRevenue, 100000000ULL - 79999868ULL - 20000000ULL);
}

TEST(TestQPool, EndEpochCarriesRevenueBelowThreshold)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();
    uint64 hid = t.fabricateHolding(pid, 1000000);

    // Drain the registration fee first so only the small skim remains.
    t.endEpoch();
    uint64 carried = t.getStats().pendingRevenue;

    // A 100,000 dividend yields only a 1,000 fee — below the distribution floor.
    t.creditPayout(hid, 100000);
    uint64 beforeDist = t.getStats().totalDistributedToShareholders;
    t.endEpoch();
    EXPECT_EQ(t.getStats().totalDistributedToShareholders, beforeDist);
    EXPECT_EQ(t.getStats().pendingRevenue, carried + 1000ULL);
}

// ======================== IPO accounting ========================

TEST(TestQPool, IpoProposalValidatesIndexNameAndBudget)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();
    Asset shareAsset{ NULL_ID, 18671117686362193ULL }; // "QPAYHUB"

    // bidInIPO needs SELF_INDEX < target, so a lower index must be refused.
    EXPECT_EQ(t.propose(ALICE, pid, QPOOL_KIND_BUY_IPO, shareAsset,
                        1000000, 10, 1000, QPOOL_CONTRACT_INDEX - 1).returnCode,
              QPOOL_ERR_INVALID_INPUT);
    // Contract shares are protocol-issued: a non-NULL issuer is wrong.
    EXPECT_EQ(t.propose(ALICE, pid, QPOOL_KIND_BUY_IPO, t.token(),
                        1000000, 10, 1000, QPOOL_CONTRACT_INDEX + 1).returnCode,
              QPOOL_ERR_INVALID_INPUT);
    // price * shares must fit inside the approved budget.
    EXPECT_EQ(t.propose(ALICE, pid, QPOOL_KIND_BUY_IPO, shareAsset,
                        1000, 10, 1000, QPOOL_CONTRACT_INDEX + 1).returnCode,
              QPOOL_ERR_INVALID_INPUT);

    EXPECT_EQ(t.propose(ALICE, pid, QPOOL_KIND_BUY_IPO, shareAsset,
                        1000000, 10, 1000, QPOOL_CONTRACT_INDEX + 1).returnCode,
              QPOOL_OK);
}

// An IPO that wins nothing must return every committed QU to its funders so
// they are free to fund the next attempt.
TEST(TestQPool, LostIpoReturnsCapitalAndRetiresHolding)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();

    // Stand in for a failed IPO attempt: budget committed, nothing won, all of
    // it refunded back into ipoAvailable by ReconcileIpo.
    uint64 hid = t.getState()->holdingCount;
    QPOOL::Holding h = t.getState()->holdings.get(hid);
    h.poolId = pid;
    h.asset = Asset{ NULL_ID, 18671117686362193ULL };
    h.shares = 0;
    h.costBasis = 1000000;
    h.ipoContractIndex = QPOOL_CONTRACT_INDEX + 1;
    h.ipoMaxPrice = 2000;
    h.ipoBidCount = 1;
    h.ipoBidPrices.set(0, 1000);
    h.ipoAtRisk = 0;
    h.ipoAvailable = 1000000;
    h.active = 1;
    t.getState()->holdings.set(hid, h);
    t.getState()->holdingCount = hid + 1;

    QPOOL::OwnKey ok;
    QPOOL::Ownership own{ 0, uint128(0, 0), 0 };
    own.cost = 600000; ok.member = ALICE; ok.holdingId = hid;
    t.getState()->ownership.set(ok, own);
    own.cost = 400000; ok.member = BOB;
    t.getState()->ownership.set(ok, own);

    // Mirror the commitment in member accounting.
    QPOOL::ContribKey ck;
    QPOOL::Contribution cc;
    ck.member = ALICE; ck.poolId = pid;
    t.getState()->contributions.get(ck, cc);
    cc.uncommitted = 0; cc.committed = 600000;
    t.getState()->contributions.set(ck, cc);
    ck.member = BOB;
    t.getState()->contributions.get(ck, cc);
    cc.uncommitted = 0; cc.committed = 400000;
    t.getState()->contributions.set(ck, cc);
    auto p = t.getState()->pools.get(pid);
    p.uncommittedTotal = 0;
    p.committedTotal = 1000000;
    t.getState()->pools.set(pid, p);

    auto s = t.settleIpo(CREATOR, hid);
    EXPECT_EQ(s.returnCode, QPOOL_OK);
    EXPECT_EQ(s.shares, 0ULL);
    EXPECT_EQ(s.returnedToMembers, 1000000ULL);

    // Capital is back as uncommitted, ready for a fresh attempt.
    EXPECT_EQ(t.getMember(pid, ALICE).uncommitted, 600000ULL);
    EXPECT_EQ(t.getMember(pid, BOB).uncommitted, 400000ULL);
    EXPECT_EQ(t.getPool(pid).uncommittedTotal, 1000000ULL);
    // The empty holding is retired.
    EXPECT_EQ(t.getHolding(hid).active, 0ULL);
}

TEST(TestQPool, SettleIpoRefusesWhileCapitalStillAtRisk)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();

    uint64 hid = t.getState()->holdingCount;
    QPOOL::Holding h = t.getState()->holdings.get(hid);
    h.poolId = pid;
    h.asset = Asset{ NULL_ID, 18671117686362193ULL };
    h.costBasis = 1000000;
    h.ipoContractIndex = QPOOL_CONTRACT_INDEX + 1;
    h.ipoBidCount = 1;
    h.ipoBidPrices.set(0, 1000);
    h.ipoAtRisk = 500000; // auction still running
    h.ipoAvailable = 500000;
    h.active = 1;
    t.getState()->holdings.set(hid, h);
    t.getState()->holdingCount = hid + 1;

    EXPECT_EQ(t.settleIpo(CREATOR, hid).returnCode, QPOOL_ERR_WINDOW_OPEN);
}

TEST(TestQPool, StatsAggregate)
{
    ContractTestingQPool t;
    uint64 pid = t.poolWith6040();
    EXPECT_EQ(t.getStats().poolCount, 1ULL);
    EXPECT_EQ(t.getStats().totalContributed, 1000000ULL);
    uint64 hid = t.fabricateHolding(pid, 1000000);
    t.creditPayout(hid, 50000);
    EXPECT_EQ(t.getStats().totalManagementFees, 500ULL);
    EXPECT_EQ(t.getHolding(hid).totalPayouts, 49500ULL);
}
