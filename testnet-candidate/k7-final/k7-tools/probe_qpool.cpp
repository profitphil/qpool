// probe_qpool.cpp -- translation unit compiled by check_contract.sh.
//
// It includes the core's contract testing scaffolding (test/contract_testing.h, which pulls in contract_def.h
// and with it the contract header under test as contract index QPOOL_CONTRACT_INDEX) and instantiates the
// contract the same way the core's own contract tests do: INIT_CONTRACT registers every user function and
// procedure (this instantiates every QPI template the contract uses), callSystemProcedure(INITIALIZE)
// runs the contract's INITIALIZE.
//
// When the binary is also linked and run (check_contract.sh --run) the test below does a smoke run:
// INITIALIZE, then every REGISTERED USER FUNCTION (read-only by construction: the core runs functions on a
// const state) once with an all-zero input of the size the contract registered. It only proves that the
// contract starts and that reading its state with zero input does not crash or abort; it checks no results.
#define NO_UEFI

#include "contract_testing.h"

#include <vector>

namespace
{
// Contract logging does misaligned stores that abort under -fsanitize=alignment; the harness does not use
// the sanitizer, but pausing the log keeps the probe identical to the core's own QPOOL tests.
class QPoolProbeLoggingEnv : public ::testing::Environment
{
public:
    void SetUp() override { __pauseLogMessage(); }
};
::testing::Environment* const qpoolProbeLoggingEnv =
    ::testing::AddGlobalTestEnvironment(new QPoolProbeLoggingEnv);
}

class QPoolHarnessProbe : protected ContractTesting
{
public:
    QPoolHarnessProbe()
    {
        initEmptySpectrum();
        initEmptyUniverse();
        system.epoch = contractDescriptions[QPOOL_CONTRACT_INDEX].constructionEpoch;
        INIT_CONTRACT(QPOOL);
        callSystemProcedure(QPOOL_CONTRACT_INDEX, INITIALIZE);
        __pauseLogMessage();
    }

    // Calls every registered user function once with a zeroed input. Returns how many were called.
    unsigned int smokeAllFunctions(unsigned int& badOutputSize)
    {
        unsigned int called = 0;
        badOutputSize = 0;
        for (unsigned int fn = 0; fn < 65536; ++fn)
        {
            if (!contractUserFunctions[QPOOL_CONTRACT_INDEX][fn])
                continue;
            const unsigned short inSize = contractUserFunctionInputSizes[QPOOL_CONTRACT_INDEX][fn];
            const unsigned short outSize = contractUserFunctionOutputSizes[QPOOL_CONTRACT_INDEX][fn];
            std::vector<unsigned char> input(inSize ? inSize : 1, 0);
            QpiContextUserFunctionCall qpiContext(QPOOL_CONTRACT_INDEX);
            const unsigned int err = qpiContext.call((unsigned short)fn, input.data(), inSize);
            EXPECT_EQ(err, 0u) << "user function " << fn << " returned an error code";
            if (qpiContext.outputSize != outSize)
                ++badOutputSize;
            qpiContext.freeBuffer();
            ++called;
        }
        return called;
    }

    QPOOL::StateData* getState() { return (QPOOL::StateData*)contractStates[QPOOL_CONTRACT_INDEX]; }
};

TEST(QPoolHarness, InitializeAndReadOnlyFunctions)
{
    QPoolHarnessProbe probe;
    unsigned int badOutputSize = 0;
    const unsigned int called = probe.smokeAllFunctions(badOutputSize);
    EXPECT_GT(called, 0u);
    EXPECT_EQ(badOutputSize, 0u);
    std::cout << "QPOOL_HARNESS_SMOKE functions_called=" << called << std::endl;
}
