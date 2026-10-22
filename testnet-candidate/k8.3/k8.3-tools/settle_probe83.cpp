// settle_probe.cpp -- scratch test of the N2 automatic settlement at BEGIN_EPOCH (SettleHolding / BEGIN_EPOCH / GetStats) against the CORE'S OWN
// finishIPOs() and assetsEndEpoch() and a real qpi.bidInIPO. Not part of the contract and not a replacement for the owner's testnet.
#define NO_UEFI

#include "contract_testing.h"

#include <vector>
#include <cstdio>
#include <cstring>

namespace
{
class LogEnv : public ::testing::Environment
{
public:
    void SetUp() override { __pauseLogMessage(); }
};
::testing::Environment* const logEnv = ::testing::AddGlobalTestEnvironment(new LogEnv);

unsigned long long mix64(unsigned long long x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

template <typename T> struct Z
{
    alignas(T) unsigned char b[sizeof(T)];
    Z() { memset(b, 0, sizeof(b)); }
    T& get() { return *reinterpret_cast<T*>(b); }
};

unsigned long long nameVal(const char* s)
{
    unsigned long long v = 0;
    for (int i = 0; i < 7 && s[i]; ++i) v |= ((unsigned long long)(unsigned char)s[i]) << (8 * i);
    return v;
}
}

class NameFx : protected ContractTesting
{
public:
    NameFx()
    {
        initEmptySpectrum();
        initEmptyUniverse();
        system.epoch = 100;
        INIT_CONTRACT(QPOOL);
        callSystemProcedure(QPOOL_CONTRACT_INDEX, INITIALIZE);
        for (unsigned idx = 31; idx <= 34; ++idx) contractStates[idx] = (unsigned char*)calloc(1, sizeof(IPO));
        __pauseLogMessage();
    }

    QPOOL::StateData* st() { return (QPOOL::StateData*)contractStates[QPOOL_CONTRACT_INDEX]; }

    // a full book (676 slots, prices falling from 1000 by one per 8 slots, the last slot above 0) with nBidders bidders, or an empty one
    void fillBook(unsigned idx, unsigned long long baseSeed, unsigned nBidders, bool full = true)
    {
        IPO* ipo = (IPO*)contractStates[idx];
        memset(ipo, 0, sizeof(IPO));
        if (!full) return;
        for (unsigned i = 0; i < NUMBER_OF_COMPUTORS; ++i)
        {
            const unsigned long long k = baseSeed + (i % nBidders);
            ipo->publicKeys[i] = m256i(mix64(k * 4), mix64(k * 4 + 1), mix64(k * 4 + 2), mix64(k * 4 + 3));   // keys like real ones: no small, ordered numbers
            ipo->prices[i] = 1000 - (long long)(i / 8);
        }
    }

    void issueZero(const char* name7)
    {
        int a, b, c;
        ASSERT_GT(issueAsset(m256i::zero(), name7, 0, CONTRACT_ASSET_UNIT_OF_MEASUREMENT, NUMBER_OF_COMPUTORS, 1, &a, &b, &c), 0);
    }

    std::vector<unsigned long long> walk()
    {
        std::vector<unsigned long long> v;
        for (QPI::AssetIssuanceIterator it(QPI::AssetIssuanceSelect::byIssuer(QPI::NULL_ID)); !it.reachedEnd(); it.next()) v.push_back(it.assetName());
        return v;
    }

    QPOOL::GetNameRegistry_output view(unsigned long long index, unsigned long long name)
    {
        QPOOL::GetNameRegistry_input in{};
        in.index = index; in.assetName = name;
        QPOOL::GetNameRegistry_output out;
        memset(&out, 0, sizeof(out));
        callFunction(QPOOL_CONTRACT_INDEX, 10, in, out);
        return out;
    }

    template <typename I, typename O>
    bool callFn(unsigned short fnType, const I& in, O& out) { callFunction(QPOOL_CONTRACT_INDEX, fnType, in, out); return true; }

    void endEpochQpool() { callSystemProcedure(QPOOL_CONTRACT_INDEX, END_EPOCH); }
    void beginEpochQpool() { callSystemProcedure(QPOOL_CONTRACT_INDEX, BEGIN_EPOCH); }
    // the node's epoch change in the order endEpoch() does it: END_EPOCH of the contract, finishIPOs(), the rebuild of the asset table, the epoch counter
    void epochChange()
    {
        endEpochQpool();
        finishIPOs();
        assetsEndEpoch();
        system.epoch++;
    }

    template <typename I, typename O>
    bool invoke(unsigned short procType, const I& in, O& out, const m256i& user)
    {
        return invokeUserProcedure(QPOOL_CONTRACT_INDEX, procType, in, out, user, 0);
    }

    template <typename I, typename O>
    bool invokeAmt(unsigned short procType, const I& in, O& out, const m256i& user, long long amount)
    {
        return invokeUserProcedure(QPOOL_CONTRACT_INDEX, procType, in, out, user, amount);
    }


    // ---- K8.3: QX ----
    void initQx()
    {
        INIT_CONTRACT(QX);
        callSystemProcedure(QX_CONTRACT_INDEX, INITIALIZE);
        __pauseLogMessage();
    }
    long long qxBidAs(const m256i& from, const m256i& issuer, unsigned long long name, long long price, long long n)
    {
        increaseEnergy(from, price * n);
        QX::AddToBidOrder_input in{ issuer, name, price, n };
        QX::AddToBidOrder_output out;
        invokeUserProcedure(QX_CONTRACT_INDEX, 6, in, out, from, price * n);
        return out.addedNumberOfShares;
    }
    long long qxAsksOfQpool(const m256i& issuer, unsigned long long name)
    {
        QX::EntityAskOrders_input in; in.entity = m256i(QPOOL_CONTRACT_INDEX, 0, 0, 0); in.offset = 0;
        QX::EntityAskOrders_output out;
        callFunction(QX_CONTRACT_INDEX, 4, in, out);
        long long sum = 0;
        for (unsigned i = 0; i < 256; i++)
        {
            auto o = out.orders.get(i);
            if (o.numberOfShares <= 0) break;
            if (o.issuer == issuer && o.assetName == name) sum += o.numberOfShares;
        }
        return sum;
    }

    void clearUniverse()
    {
        memset(assets, 0, universeSizeInBytes);
        as.indexLists.reset();
    }
};

static const char* nm(unsigned long long v, char* buf)
{
    for (int i = 0; i < 7; ++i) buf[i] = (char)((v >> (8 * i)) & 0xff);
    buf[7] = 0;
    return buf;
}



// ---------------------------------------------------------------------------------------------------------------------------------------------
// helpers: pools, holdings, views
static m256i userK(unsigned k) { return m256i(1000 + k, 7, 7, 7); }

struct Spec { unsigned target; unsigned long long price; unsigned long long budget; };

// one pool per spec (creator = userK(k)), one BUY_IPO proposal each, all voted, tallied and executed in spec order: holding k gets slot k
static std::vector<unsigned long long> makeHoldings(NameFx& fx, const std::vector<Spec>& specs, unsigned extra = 0)
{
    std::vector<unsigned long long> pools, props, hids;
    system.tick = 1000;
    for (unsigned k = 0; k < specs.size(); ++k)
    {
        increaseEnergy(userK(k), 100000000);
        QPOOL::CreatePool_input in; in.poolName = 100 + k; in.permissioned = 0;
        QPOOL::CreatePool_output out; memset(&out, 0, sizeof(out));
        EXPECT_TRUE(fx.invokeAmt(1, in, out, userK(k), 10000000)); EXPECT_EQ(out.returnCode, 0u);
        pools.push_back(out.poolId);
        QPOOL::Contribute_input ci; ci.poolId = out.poolId;
        QPOOL::Contribute_output co; memset(&co, 0, sizeof(co));
        EXPECT_TRUE(fx.invokeAmt(2, ci, co, userK(k), 1000000)); EXPECT_EQ(co.returnCode, 0u);
        for (unsigned j = 1; j <= extra; ++j)
        {
            const m256i u = userK(200 + 10 * k + j);
            increaseEnergy(u, 100000000);
            QPOOL::Contribute_output co2; memset(&co2, 0, sizeof(co2));
            EXPECT_TRUE(fx.invokeAmt(2, ci, co2, u, 200000 + 77777 * j)); EXPECT_EQ(co2.returnCode, 0u);
        }
    }
    system.tick = 2000;
    for (unsigned k = 0; k < specs.size(); ++k)
    {
        QPOOL::Propose_input pi; memset(&pi, 0, sizeof(pi));
        pi.poolId = pools[k]; pi.kind = 3; pi.quAmount = specs[k].budget; pi.price = specs[k].price; pi.holdingId = specs[k].target;
        QPOOL::Propose_output po; memset(&po, 0, sizeof(po));
        EXPECT_TRUE(fx.invokeAmt(4, pi, po, userK(k), 0)); EXPECT_EQ(po.returnCode, 0u);
        props.push_back(po.proposalId);
    }
    system.tick = 2001;
    for (unsigned k = 0; k < specs.size(); ++k)
    {
        QPOOL::Vote_input vi; vi.proposalId = props[k]; vi.approve = 1;
        QPOOL::Vote_output vo; memset(&vo, 0, sizeof(vo));
        EXPECT_TRUE(fx.invokeAmt(5, vi, vo, userK(k), 0)); EXPECT_EQ(vo.returnCode, 0u);
        for (unsigned j = 1; j <= extra; ++j)
        {
            QPOOL::Vote_output vo2; memset(&vo2, 0, sizeof(vo2));
            EXPECT_TRUE(fx.invokeAmt(5, vi, vo2, userK(200 + 10 * k + j), 0)); EXPECT_EQ(vo2.returnCode, 0u);
        }
    }
    system.tick = 2000 + 86400 + 1;
    for (unsigned k = 0; k < specs.size(); ++k)
    {
        QPOOL::Tally_input ti; ti.proposalId = props[k];
        QPOOL::Tally_output to; memset(&to, 0, sizeof(to));
        EXPECT_TRUE(fx.invokeAmt(6, ti, to, userK(k), 0)); EXPECT_EQ(to.returnCode, 0u); EXPECT_EQ(to.status, 1u);
    }
    for (unsigned k = 0; k < specs.size(); ++k)
    {
        QPOOL::Execute_input ei; ei.proposalId = props[k];
        QPOOL::Execute_output eo; memset(&eo, 0, sizeof(eo));
        EXPECT_TRUE(fx.invokeAmt(7, ei, eo, userK(k), 0)); EXPECT_EQ(eo.returnCode, 0u);
        hids.push_back(eo.holdingId);
    }
    return hids;
}

static QPOOL::GetStats_output stats(NameFx& fx)
{
    QPOOL::GetStats_input in;
    QPOOL::GetStats_output out; memset(&out, 0, sizeof(out));
    EXPECT_TRUE(fx.callFn(6, in, out));
    return out;
}

static unsigned settleManual(NameFx& fx, unsigned long long hid, unsigned long long* shares = nullptr)
{
    increaseEnergy(userK(99), 1000);
    QPOOL::SettleIpo_input in; in.holdingId = hid;
    QPOOL::SettleIpo_output out; memset(&out, 0, sizeof(out));
    EXPECT_TRUE(fx.invokeAmt(14, in, out, userK(99), 0));
    if (shares) *shares = out.shares;
    return out.returnCode;
}

static QPOOL::SourceLedger ledgerOf(NameFx& fx, unsigned contractIndex)
{
    Z<QPOOL::SourceKey> kz; QPOOL::SourceKey& k = kz.get(); k.source = m256i(contractIndex, 0, 0, 0); k.kind = 1;
    Z<QPOOL::SourceLedger> lz; QPOOL::SourceLedger& l = lz.get();
    fx.st()->sources.get(k, l);
    return l;
}

static void dividend(unsigned contractIndex, long long amount)
{
    notifyContractOfIncomingTransfer(m256i(contractIndex, 0, 0, 0), m256i(QPOOL_CONTRACT_INDEX, 0, 0, 0), amount, QPI::TransferType::qpiDistributeDividends);
}

// ---- T1: three pools on a full book and one on a failed IPO: BEGIN_EPOCH settles all four; a manual SettleIpo then says 'already settled'; a repeat of BEGIN_EPOCH does nothing; dividends flow ----
TEST(QPoolSettleProbe, T1_all_ready_holdings_settle_at_begin_epoch)
{
    NameFx fx;
    fx.beginEpochQpool();                                    // baseline (epoch 100)
    system.epoch = 137;
    fx.fillBook(31, 0, 0, false);                            // an empty book: its IPO fails
    fx.fillBook(32, 5000, 9);                                // a full book: prices 1000 down to 916
    fx.fillBook(33, 0, 0, false);
    auto hids = makeHoldings(fx, { {32, 2000, 10000}, {32, 2001, 10000}, {32, 2002, 10000}, {31, 2000, 10000} });
    ASSERT_EQ(hids.size(), 4u);
    for (auto h : hids) { EXPECT_EQ(fx.st()->holdings.get(h).ipoSettled, 0u); EXPECT_EQ(fx.st()->holdings.get(h).costBasis, 10000u); }
    fx.epochChange();                                        // END_EPOCH snapshot + queue, finishIPOs, rebuild, epoch 138
    EXPECT_EQ(stats(fx).readyUnsettled, 4u);                 // ready before BEGIN_EPOCH: snapshot taken, epoch over
    EXPECT_EQ(stats(fx).autoSettleEpoch, 100u);   // the baseline pass of epoch 100
    fx.beginEpochQpool();
    auto s = stats(fx);
    std::printf("QPOOL_SETTLE_PROBE T1 stats: ready=%llu epoch=%llu done=%llu skipped=%llu total=%llu\n", (unsigned long long)s.readyUnsettled, (unsigned long long)s.autoSettleEpoch, (unsigned long long)s.autoSettleDone, (unsigned long long)s.autoSettleSkipped, (unsigned long long)s.autoSettleTotal);
    EXPECT_EQ(s.readyUnsettled, 0u);
    EXPECT_EQ(s.autoSettleEpoch, 138u);
    EXPECT_EQ(s.autoSettleDone, 4u);
    EXPECT_EQ(s.autoSettleSkipped, 0u);
    EXPECT_EQ(s.autoSettleTotal, 4u);
    unsigned long long seatsSum = 0;
    for (unsigned k = 0; k < 3; ++k)
    {
        const QPOOL::Holding& h = fx.st()->holdings.get(hids[k]);
        std::printf("QPOOL_SETTLE_PROBE T1 holding %llu: settled=%llu shares=%llu costBasis=%llu name=%llx\n", (unsigned long long)hids[k], (unsigned long long)h.ipoSettled, (unsigned long long)h.shares, (unsigned long long)h.costBasis, (unsigned long long)h.asset.assetName);
        EXPECT_EQ(h.ipoSettled, 1u);
        EXPECT_GT(h.shares, 0u);
        EXPECT_EQ(h.asset.assetName, nameVal("TESTEXB"));
        seatsSum += h.shares;
        EXPECT_EQ(fx.st()->pools.get(k).uncommittedTotal + fx.st()->pools.get(k).committedTotal, 990000u);   // nothing lost: the contribution (less the 1% join fee) is on the books, spent on seats or returned
        EXPECT_EQ(fx.st()->pools.get(k).committedTotal, h.costBasis);
    }
    {   // the failed IPO: the holding won nothing, got its whole budget back and is gone
        const QPOOL::Holding& h = fx.st()->holdings.get(hids[3]);
        EXPECT_EQ(h.costBasis, 0u);
        EXPECT_EQ(h.active, 0u);
        EXPECT_EQ(fx.st()->pools.get(3).uncommittedTotal, 990000u);
        EXPECT_EQ(fx.st()->pools.get(3).committedTotal, 0u);
    }
    {
        auto l = ledgerOf(fx, 32);
        EXPECT_EQ(l.unsettled, 0u);
        EXPECT_EQ(l.recognized, seatsSum);
        EXPECT_EQ(l.assetName, nameVal("TESTEXB"));
        EXPECT_EQ(l.holdings, 3u);
    }
    EXPECT_EQ(fx.st()->ipoBids.population(), 0u);
    EXPECT_EQ(fx.st()->ipoSeats.population(), 0u);
    EXPECT_EQ(fx.st()->ipoPrices.population(), 0u);
    for (unsigned k = 0; k < 8; ++k) EXPECT_EQ(fx.st()->ipoTargets.get(k).ipoContractIndex, 0u);   // every row released
    // a keeper calling SettleIpo afterwards gets the usual 'already settled' answer (IPO_NOT_ACTIVE = 18) and nothing changes
    const unsigned long long recBefore = ledgerOf(fx, 32).recognized;
    for (unsigned k = 0; k < 4; ++k) EXPECT_EQ(settleManual(fx, hids[k]), 18u);
    EXPECT_EQ(ledgerOf(fx, 32).recognized, recBefore);
    EXPECT_EQ(stats(fx).autoSettleTotal, 4u);
    // a second BEGIN_EPOCH of the same epoch (a restart) finds nothing to do: the pass of this epoch has run
    const unsigned long long cursorBefore = fx.st()->autoSettleCursor;
    fx.beginEpochQpool();
    s = stats(fx);
    EXPECT_EQ(s.autoSettleEpoch, 138u); EXPECT_EQ(s.autoSettleDone, 4u); EXPECT_EQ(s.autoSettleTotal, 4u); EXPECT_EQ(fx.st()->autoSettleCursor, cursorBefore);
    EXPECT_EQ(ledgerOf(fx, 32).recognized, recBefore);
    // the dividends of the new contract now flow to the settled holdings: credited through the accumulator, nothing parked
    const uint128 accBefore = ledgerOf(fx, 32).accX64;
    dividend(32, 10000);
    {
        auto l = ledgerOf(fx, 32);
        EXPECT_TRUE(l.accX64 > accBefore);
        EXPECT_EQ(l.escrow, 0u);
        EXPECT_EQ(l.unsettled, 0u);
    }
}

// ---- T2: more ready holdings than the bound: the pass stops at the bound, the rest wait for the next epochs, dividends are parked until the last one settles ----
TEST(QPoolSettleProbe, T2_more_ready_than_the_bound)
{
    const unsigned B = (unsigned)QPOOL_AUTO_SETTLE_MAX;
    if (B > 6) GTEST_SKIP() << "built with the production bound (" << B << "): the backlog path needs a small bound";
    NameFx fx;
    fx.beginEpochQpool();
    system.epoch = 137;
    fx.fillBook(31, 0, 0, false);
    fx.fillBook(32, 5000, 9);
    fx.fillBook(33, 0, 0, false);
    std::vector<Spec> specs;
    for (unsigned k = 0; k < B + 3; ++k) specs.push_back({32, 2000 + k, 10000});
    auto hids = makeHoldings(fx, specs);
    const unsigned N = (unsigned)hids.size();
    fx.epochChange();                                        // epoch 138
    EXPECT_EQ(stats(fx).readyUnsettled, (unsigned long long)N);
    fx.beginEpochQpool();
    auto s = stats(fx);
    std::printf("QPOOL_SETTLE_PROBE T2 pass 1 (bound %u, %u ready): done=%llu skipped=%llu ready left=%llu cursor=%llu\n", B, N, (unsigned long long)s.autoSettleDone, (unsigned long long)s.autoSettleSkipped, (unsigned long long)s.readyUnsettled, (unsigned long long)fx.st()->autoSettleCursor);
    EXPECT_EQ(s.autoSettleDone, (unsigned long long)B);
    EXPECT_EQ(s.autoSettleSkipped, 0u);
    EXPECT_EQ(s.readyUnsettled, (unsigned long long)(N - B));
    EXPECT_EQ(ledgerOf(fx, 32).unsettled, (unsigned long long)(N - B));
    unsigned settled = 0;
    for (auto h : hids) settled += (fx.st()->holdings.get(h).ipoSettled != 0) ? 1 : 0;
    EXPECT_EQ(settled, B);
    // the same epoch again (a restart): nothing more is settled
    fx.beginEpochQpool();
    EXPECT_EQ(stats(fx).autoSettleDone, (unsigned long long)B); EXPECT_EQ(stats(fx).autoSettleTotal, (unsigned long long)B);
    EXPECT_EQ(stats(fx).readyUnsettled, (unsigned long long)(N - B));
    // a dividend while some are unsettled is parked, not credited
    const uint128 acc0 = ledgerOf(fx, 32).accX64;
    dividend(32, 10000);
    EXPECT_TRUE(ledgerOf(fx, 32).accX64 == acc0);
    const unsigned long long parked = ledgerOf(fx, 32).escrow;
    EXPECT_GT(parked, 0u);
    // epochs 139, 140 ...: B more each time, none twice
    unsigned long long total = B;
    for (unsigned e = 0; e < 3 && stats(fx).readyUnsettled > 0; ++e)
    {
        fx.epochChange();
        fx.beginEpochQpool();
        s = stats(fx);
        total += s.autoSettleDone;
        std::printf("QPOOL_SETTLE_PROBE T2 epoch %llu: done=%llu skipped=%llu ready left=%llu total=%llu cursor=%llu\n", (unsigned long long)s.autoSettleEpoch, (unsigned long long)s.autoSettleDone, (unsigned long long)s.autoSettleSkipped, (unsigned long long)s.readyUnsettled, (unsigned long long)s.autoSettleTotal, (unsigned long long)fx.st()->autoSettleCursor);
        EXPECT_EQ(s.autoSettleTotal, total);
        EXPECT_EQ(s.autoSettleSkipped, 0u);
    }
    EXPECT_EQ(stats(fx).readyUnsettled, 0u);
    EXPECT_EQ(total, (unsigned long long)N);
    unsigned long long seatsSum = 0;
    for (auto h : hids) { EXPECT_EQ(fx.st()->holdings.get(h).ipoSettled, 1u); seatsSum += fx.st()->holdings.get(h).shares; }
    auto l = ledgerOf(fx, 32);
    EXPECT_EQ(l.unsettled, 0u);
    EXPECT_EQ(l.recognized, seatsSum);
    // the parked dividend was released by the settlement of the last holding: nothing left parked, the accumulator moved
    EXPECT_EQ(l.escrow, 0u);
    EXPECT_TRUE(l.accX64 > acc0);
    for (auto h : hids) EXPECT_EQ(settleManual(fx, h), 18u);
}

// ---- T3: holdings that are refused must not keep the ones behind them waiting: two holdings on quarantined names (NAME_PENDING) are the FIRST in the table, two on a failed IPO follow ----
TEST(QPoolSettleProbe, T3_refused_holdings_do_not_starve_the_rest)
{
    const unsigned B = (unsigned)QPOOL_AUTO_SETTLE_MAX;
    if (B != 2) GTEST_SKIP() << "built with the production bound (" << B << "): this test needs a bound of 2";
    NameFx fx;
    fx.beginEpochQpool();
    system.epoch = 137;
    fx.fillBook(31, 0, 0, false);
    fx.fillBook(32, 7000, 9);
    fx.fillBook(33, 7000, 9);                                // the same bidders as 32: the two names cannot be told apart, the epoch is quarantined
    // which holding slots come first in ipoBids: the table's slot of key k is hash(k) & 2047; the two lowest of the keys 0..3 get the stuck targets
    unsigned long long slot[4];
    for (unsigned k = 0; k < 4; ++k) slot[k] = QPI::HashFunction<unsigned long long>::hash((unsigned long long)k) & (QPOOL_MAX_IPO_HOLDINGS - 1);
    unsigned first = 0, second = 1;
    for (unsigned k = 0; k < 4; ++k) { if (slot[k] < slot[first]) first = k; }
    second = (first == 0) ? 1 : 0;
    for (unsigned k = 0; k < 4; ++k) { if (k != first && slot[k] < slot[second]) second = k; }
    std::printf("QPOOL_SETTLE_PROBE T3 slots of holdings 0..3: %llu %llu %llu %llu  (stuck ones: %u and %u)\n", slot[0], slot[1], slot[2], slot[3], first, second);
    std::vector<Spec> specs(4);
    unsigned nextStuck = 32;
    for (unsigned k = 0; k < 4; ++k)
    {
        if (k == first || k == second) { specs[k] = { nextStuck, 2000, 10000 }; nextStuck++; }   // 5 seats each: the two books stay identical
        else specs[k] = { 31, 2000 + k, 10000 };
    }
    auto hids = makeHoldings(fx, specs);
    ASSERT_EQ(hids.size(), 4u);
    for (unsigned k = 0; k < 4; ++k) EXPECT_EQ(hids[k], (unsigned long long)k);
    fx.epochChange();                                        // epoch 138: the pairing is quarantined (two names, two identical books)
    fx.beginEpochQpool();
    EXPECT_EQ(fx.view(0, 0).mismatches, 1u);
    EXPECT_EQ(fx.view(32, 0).name, 0u);
    auto s = stats(fx);
    std::printf("QPOOL_SETTLE_PROBE T3 pass 1: done=%llu skipped=%llu ready left=%llu cursor=%llu\n", (unsigned long long)s.autoSettleDone, (unsigned long long)s.autoSettleSkipped, (unsigned long long)s.readyUnsettled, (unsigned long long)fx.st()->autoSettleCursor);
    EXPECT_EQ(s.autoSettleDone, 0u);                         // the bound was used up by the two refused holdings
    EXPECT_EQ(s.autoSettleSkipped, 2u);
    EXPECT_EQ(s.readyUnsettled, 4u);
    // a refusal wrote nothing: the stuck holdings are as they were
    EXPECT_EQ(fx.st()->holdings.get(hids[first]).ipoSettled, 0u); EXPECT_EQ(fx.st()->holdings.get(hids[first]).costBasis, 10000u);
    EXPECT_EQ(fx.st()->holdings.get(hids[second]).ipoSettled, 0u); EXPECT_EQ(fx.st()->holdings.get(hids[second]).costBasis, 10000u);
    EXPECT_EQ(settleManual(fx, hids[first]), 40u);           // NAME_PENDING, the manual routine says the same
    fx.epochChange();                                        // epoch 139
    fx.beginEpochQpool();
    s = stats(fx);
    std::printf("QPOOL_SETTLE_PROBE T3 pass 2: done=%llu skipped=%llu ready left=%llu cursor=%llu\n", (unsigned long long)s.autoSettleDone, (unsigned long long)s.autoSettleSkipped, (unsigned long long)s.readyUnsettled, (unsigned long long)fx.st()->autoSettleCursor);
    EXPECT_EQ(s.autoSettleDone, 2u);                         // the two failed-IPO holdings are reached behind the cursor
    EXPECT_EQ(s.readyUnsettled, 2u);
    for (unsigned k = 0; k < 4; ++k)
    {
        if (k == first || k == second) EXPECT_EQ(fx.st()->holdings.get(hids[k]).ipoSettled, 0u);
        else { EXPECT_EQ(fx.st()->holdings.get(hids[k]).costBasis, 0u); EXPECT_EQ(fx.st()->pools.get(k).uncommittedTotal, 990000u); }
    }
    fx.epochChange();                                        // epoch 140: only the two refused ones are left; they are tried again and refused again
    fx.beginEpochQpool();
    s = stats(fx);
    EXPECT_EQ(s.autoSettleDone, 0u); EXPECT_EQ(s.autoSettleSkipped, 2u); EXPECT_EQ(s.readyUnsettled, 2u);
}


// ---- T4: the automatic settlement and the manual one give the SAME books. Prints a digest; run it on this header and on a copy with the automatic pass
// switched off (the test then settles by hand, in holding order) and diff the two outputs. Three members per pool, three pools on a full book, one on a failed IPO. ----
static unsigned long long lo(const uint128& v) { return v.low; }
static unsigned long long hi(const uint128& v) { return v.high; }

TEST(QPoolSettleProbe, T4_digest_of_the_books_after_settlement)
{
    NameFx fx;
    fx.beginEpochQpool();
    system.epoch = 137;
    fx.fillBook(31, 0, 0, false);
    fx.fillBook(32, 5000, 9);
    fx.fillBook(33, 0, 0, false);
    auto hids = makeHoldings(fx, { {32, 2000, 10000}, {32, 2001, 9999}, {32, 2003, 9000}, {31, 2000, 10000} }, 2);
    ASSERT_EQ(hids.size(), 4u);
    fx.epochChange();
    fx.beginEpochQpool();
    unsigned manual = 0;
    for (auto h : hids)
    {
        const QPOOL::Holding& hh = fx.st()->holdings.get(h);
        if (hh.ipoSettled == 0 && hh.costBasis > 0) { EXPECT_EQ(settleManual(fx, h), 0u); manual++; }
    }
    std::printf("DIGEST manualCalls=%u\n", manual);
    for (unsigned k = 0; k < 4; ++k)
    {
        const QPOOL::Holding& h = fx.st()->holdings.get(hids[k]);
        std::printf("DIGEST holding %u: shares=%llu cost=%llu settled=%llu active=%llu rows=%u incarnation=%u ppc=%llu:%llu payouts=%llu avail=%llu atRisk=%llu pending=%llu name=%llx\n", k,
            (unsigned long long)h.shares, (unsigned long long)h.costBasis, (unsigned long long)h.ipoSettled, (unsigned long long)h.active, h.rows, h.incarnation,
            hi(h.payoutPerCostX64), lo(h.payoutPerCostX64), (unsigned long long)h.totalPayouts, (unsigned long long)h.ipoAvailable, (unsigned long long)h.ipoAtRisk, (unsigned long long)h.pendingShares, (unsigned long long)h.asset.assetName);
        const QPOOL::Pool& p = fx.st()->pools.get(k);
        std::printf("DIGEST pool %u: uncommitted=%llu committed=%llu holdings=%llu ipoHoldings=%llu ownershipRows=%llu offers=%llu\n", k, (unsigned long long)p.uncommittedTotal, (unsigned long long)p.committedTotal, (unsigned long long)p.holdingCount, (unsigned long long)p.ipoHoldings, (unsigned long long)p.ownershipRows, (unsigned long long)p.offerCount);
        for (unsigned j = 0; j <= 2; ++j)
        {
            const m256i u = (j == 0) ? userK(k) : userK(200 + 10 * k + j);
            Z<QPOOL::ContribKey> ck; ck.get().member = u; ck.get().poolId = k;
            Z<QPOOL::Contribution> cz; QPOOL::Contribution& c = cz.get();
            const bool hasC = fx.st()->contributions.get(ck.get(), c);
            Z<QPOOL::OwnKey> ok; ok.get().member = u; ok.get().holdingId = hids[k];
            Z<QPOOL::Ownership> oz; QPOOL::Ownership& o = oz.get();
            const bool hasO = fx.st()->ownership.get(ok.get(), o);
            std::printf("DIGEST   member %u.%u: contribution=%d uncommitted=%llu committed=%llu lastDeposit=%llu  seat=%d cost=%llu claimable=%llu debt=%llu:%llu\n", k, j, hasC ? 1 : 0,
                (unsigned long long)c.uncommitted, (unsigned long long)c.committed, (unsigned long long)c.lastDepositTick, hasO ? 1 : 0, (unsigned long long)o.cost, (unsigned long long)o.claimable, hi(o.payoutDebtX64), lo(o.payoutDebtX64));
        }
    }
    {
        auto l = ledgerOf(fx, 32);
        std::printf("DIGEST ledger 32: acc=%llu:%llu accrued=%llu:%llu credited=%llu:%llu name=%llx holdings=%llu recognized=%llu unsettled=%llu escrow=%llu\n", hi(l.accX64), lo(l.accX64), hi(l.accruedX64), lo(l.accruedX64), hi(l.creditedX64), lo(l.creditedX64),
            (unsigned long long)l.assetName, (unsigned long long)l.holdings, (unsigned long long)l.recognized, (unsigned long long)l.unsettled, (unsigned long long)l.escrow);
    }
    std::printf("DIGEST globals: refundEscrow=%llu totalRefunded=%llu escrowTotal=%llu holdingFreeHint=%llu pendingRevenue=%llu ipoBids=%llu ipoSeats=%llu ipoPrices=%llu\n", (unsigned long long)fx.st()->ipoRefundEscrow, (unsigned long long)fx.st()->totalIpoRefunded,
        (unsigned long long)fx.st()->dividendEscrowTotal, (unsigned long long)fx.st()->holdingFreeHint, (unsigned long long)fx.st()->pendingRevenue, (unsigned long long)fx.st()->ipoBids.population(), (unsigned long long)fx.st()->ipoSeats.population(), (unsigned long long)fx.st()->ipoPrices.population());
}

// ---- T5 (K8.3): a SETTLED IPO holding rests a sell on QX; the dividend on its unsold shares and the proceeds of what a buyer takes reach the holding; sold out, it is retired ----
TEST(QPoolSettleProbe, T5_a_settled_ipo_holding_rests_a_sell_and_sells_out)
{
    NameFx fx;
    fx.beginEpochQpool();
    system.epoch = 137;
    fx.fillBook(31, 0, 0, false);
    fx.fillBook(32, 5000, 9);
    fx.fillBook(33, 0, 0, false);
    auto hids = makeHoldings(fx, { {32, 2000, 10000} });
    ASSERT_EQ(hids.size(), 1u);
    fx.epochChange();
    fx.beginEpochQpool();
    // QX pays QPOOL from inside its own call, and the core only delivers that to a contract that has been constructed: move the clock to QPOOL's construction epoch
    system.epoch = contractDescriptions[QPOOL_CONTRACT_INDEX].constructionEpoch;
    fx.initQx();
    const unsigned long long hid = hids[0];
    const QPOOL::Holding h0 = fx.st()->holdings.get(hid);
    ASSERT_EQ(h0.ipoSettled, 1u);
    const unsigned long long N = h0.shares;
    std::printf("STEP settled N=%llu\n", N); std::fflush(stdout);
    ASSERT_GE(N, 4u);
    const unsigned long long name = h0.asset.assetName;
    // vote and execute a SELL_QX_LIMIT of every share at 1,500 (the creator is the pool's only member)
    system.tick += 5;
    QPOOL::Propose_input pi; memset(&pi, 0, sizeof(pi));
    pi.poolId = h0.poolId; pi.kind = QPOOL_KIND_SELL_QX_LIMIT; pi.asset.issuer = NULL_ID; pi.asset.assetName = name; pi.shares = N; pi.price = 1500; pi.holdingId = hid;
    QPOOL::Propose_output po; memset(&po, 0, sizeof(po));
    ASSERT_TRUE(fx.invokeAmt(4, pi, po, userK(0), 0)); ASSERT_EQ(po.returnCode, 0u);
    QPOOL::Vote_input vi; vi.proposalId = po.proposalId; vi.approve = 1;
    QPOOL::Vote_output vo; memset(&vo, 0, sizeof(vo));
    ASSERT_TRUE(fx.invokeAmt(5, vi, vo, userK(0), 0)); ASSERT_EQ(vo.returnCode, 0u);
    system.tick += QPOOL_VOTE_WINDOW + 1;
    QPOOL::Tally_input ti; ti.proposalId = po.proposalId;
    QPOOL::Tally_output to; memset(&to, 0, sizeof(to));
    ASSERT_TRUE(fx.invokeAmt(6, ti, to, userK(0), 0)); ASSERT_EQ(to.status, 1u);
    QPOOL::Execute_input ei; ei.proposalId = po.proposalId;
    QPOOL::Execute_output eo; memset(&eo, 0, sizeof(eo));
    ASSERT_TRUE(fx.invokeAmt(7, ei, eo, userK(0), 0));
    ASSERT_EQ(eo.returnCode, 0u);
    std::printf("STEP executed ret=%u\n", eo.returnCode); std::fflush(stdout);
    EXPECT_EQ(eo.sold, 0u);
    EXPECT_EQ(fx.st()->holdings.get(hid).shares, 0u); EXPECT_EQ(fx.st()->holdings.get(hid).askShares, N);
    EXPECT_EQ(fx.qxAsksOfQpool(NULL_ID, name), (long long)N);
    { auto l = ledgerOf(fx, 32); EXPECT_EQ(l.qxSide, 2u); EXPECT_EQ(l.askTotal, N); EXPECT_EQ(l.qxCount, 1u); EXPECT_EQ(l.recognized, 0u); }
    // a dividend of 10,000 on the shares that all still rest: 100 management fee, all 9,900 set aside for the holding
    std::printf("STEP before dividend\n"); std::fflush(stdout);
    dividend(32, 10000);
    std::printf("STEP after dividend escrow=%llu\n", (unsigned long long)fx.st()->holdings.get(hid).qxEscrow); std::fflush(stdout);
    EXPECT_EQ(fx.st()->holdings.get(hid).qxEscrow, 9900u);
    EXPECT_EQ(ledgerOf(fx, 32).escrow, 9900u);
    // a buyer takes half
    const unsigned long long half = N / 2;
    std::printf("STEP before the buyer\n"); std::fflush(stdout);
    ASSERT_EQ(fx.qxBidAs(userK(500), NULL_ID, name, 1500, (long long)half), (long long)half);
    // QX pays gross - (floor(gross x 0.3%) + 1); QPOOL credits gross - (floor(gross x 0.3%) + shares sold) = its upper bound of the fee, less its 1%
    const unsigned long long gross = 1500 * half, qxFee = gross * 3 / 1000 + 1, arrived = gross - qxFee, netC = gross - (gross * 3 / 1000 + half), credit = netC - netC * 10 / 1000;
    EXPECT_EQ(stats(fx).globalUnattributed, arrived);
    increaseEnergy(userK(99), 1000);
    QPOOL::ReconcileHolding_input ri; ri.holdingId = hid;
    QPOOL::ReconcileHolding_output ro; memset(&ro, 0, sizeof(ro));
    ASSERT_TRUE(fx.invokeAmt(8, ri, ro, userK(99), 0)); ASSERT_EQ(ro.returnCode, 0u);
    EXPECT_EQ(ro.askShares, N - half);
    EXPECT_EQ(stats(fx).globalUnattributed, arrived - netC);
    EXPECT_EQ(fx.st()->holdings.get(hid).qxEscrow, 0u);                                        // the set-aside dividend was paid to the holding
    auto owedOf = [&](unsigned long long h) {
        QPOOL::GetPosition_input gi; gi.holdingId = h; gi.member = userK(0);
        QPOOL::GetPosition_output go; memset(&go, 0, sizeof(go));
        fx.callFn(4, gi, go);
        return (unsigned long long)go.owed;
    };
    EXPECT_GE(owedOf(hid), 9900u + credit - 3); EXPECT_LE(owedOf(hid), 9900u + credit);
    // the buyer takes the rest: the holding is sold out, retired, and its ledger goes
    const unsigned long long restN = N - half;
    ASSERT_EQ(fx.qxBidAs(userK(501), NULL_ID, name, 1500, (long long)restN), (long long)restN);
    ASSERT_TRUE(fx.invokeAmt(8, ri, ro, userK(99), 0)); ASSERT_EQ(ro.returnCode, 0u);
    EXPECT_EQ(fx.st()->holdings.get(hid).active, 0u);
    EXPECT_EQ(ledgerOf(fx, 32).holdings, 0u);
    EXPECT_EQ(stats(fx).globalUnattributed, 0u);
    const unsigned long long g2 = 1500 * restN, n2 = g2 - (g2 * 3 / 1000 + restN), c2 = n2 - n2 * 10 / 1000;
    EXPECT_GE(owedOf(hid), 9900u + credit + c2 - 6); EXPECT_LE(owedOf(hid), 9900u + credit + c2);
    EXPECT_EQ(fx.st()->restingAsks, 0u);
}
