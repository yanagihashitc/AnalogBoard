#include <cstdio>

#include "../AnalogBoard_TestApp/AcquisitionCompletionLogic.h"

static int g_TestCount = 0;
static int g_PassCount = 0;
static int g_FailCount = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_TestCount++; \
    if (cond) { g_PassCount++; } \
    else { g_FailCount++; std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define RUN_TEST(func) do { \
    std::printf("[TEST] %s\n", #func); \
    func(); \
} while(0)

void Test_TC_R_01_RdWait_DoesNotCompleteAcquisition()
{
    AcquisitionCompletionLogic::Ep4CompletionState state;

    const auto measuring = AcquisitionCompletionLogic::ObserveEp4Completion(
        &state,
        { 256u, 0u, 0, 0 },
        0u);
    TEST_ASSERT(measuring.shouldRead, "TC-R-01 measuring snapshot should expose readable bytes");

    const auto rdWait = AcquisitionCompletionLogic::ObserveEp4Completion(
        &state,
        { 256u, 256u, 1, 0 },
        288u);

    TEST_ASSERT(rdWait.drainingHintSeen, "TC-R-01 RD_WAIT should be treated as draining");
    TEST_ASSERT(!rdWait.acquisitionComplete, "TC-R-01 RD_WAIT must not complete acquisition before DDR_RD_END");
}

void Test_TC_R_02_StartupStaleDdrWrEnd_DoesNotCompleteImmediately()
{
    AcquisitionCompletionLogic::Ep4CompletionState state;

    const auto startup = AcquisitionCompletionLogic::ObserveEp4Completion(
        &state,
        { 0u, 0u, 1, 1 },
        0u);

    TEST_ASSERT(!startup.shouldRead, "TC-R-02 startup stale snapshot should not expose readable bytes");
    TEST_ASSERT(!startup.acquisitionComplete, "TC-R-02 startup stale DDR_WR_END must not complete acquisition");
}

void Test_TC_R_03_DdrRdEnd_IsFinalCompletionSignal()
{
    AcquisitionCompletionLogic::Ep4CompletionState state;

    const auto draining = AcquisitionCompletionLogic::ObserveEp4Completion(
        &state,
        { 16384u, 8192u, 1, 0 },
        0u);
    TEST_ASSERT(draining.drainingHintSeen, "TC-R-03 DDR_WR_END should mark draining");
    TEST_ASSERT(!draining.acquisitionComplete, "TC-R-03 DDR_WR_END without DDR_RD_END must not complete");

    const auto finalComplete = AcquisitionCompletionLogic::ObserveEp4Completion(
        &state,
        { 16384u, 16384u, 1, 1 },
        AcquisitionCompletionLogic::ToReadableUpperBoundBytes(16384u));

    TEST_ASSERT(finalComplete.acquisitionComplete, "TC-R-03 DDR_RD_END with no unread bytes must complete");
}

void Test_TC_R_04_ReadableUpperBound_CanGrowAfterDdrWrEnd()
{
    AcquisitionCompletionLogic::Ep4CompletionState state;

    const auto firstDraining = AcquisitionCompletionLogic::ObserveEp4Completion(
        &state,
        { 16384u, 0u, 1, 0 },
        0u);
    TEST_ASSERT(firstDraining.readableUpperBoundBytes == AcquisitionCompletionLogic::ToReadableUpperBoundBytes(16384u),
        "TC-R-04 first draining poll must expose first readable upper bound");

    const auto secondDraining = AcquisitionCompletionLogic::ObserveEp4Completion(
        &state,
        { 32768u, 8192u, 1, 0 },
        AcquisitionCompletionLogic::ToReadableUpperBoundBytes(16384u));

    TEST_ASSERT(secondDraining.readableUpperBoundBytes == AcquisitionCompletionLogic::ToReadableUpperBoundBytes(32768u),
        "TC-R-04 readable upper bound must keep growing after DDR_WR_END");
}

int main()
{
    std::printf("=== AcquisitionCompletionLogic Unit Tests ===\n\n");

    RUN_TEST(Test_TC_R_01_RdWait_DoesNotCompleteAcquisition);
    RUN_TEST(Test_TC_R_02_StartupStaleDdrWrEnd_DoesNotCompleteImmediately);
    RUN_TEST(Test_TC_R_03_DdrRdEnd_IsFinalCompletionSignal);
    RUN_TEST(Test_TC_R_04_ReadableUpperBound_CanGrowAfterDdrWrEnd);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
