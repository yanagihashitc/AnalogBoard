#include <cstdio>

#include "../AnalogBoard_TestApp/AcquisitionCycleRecoveryPolicy.h"

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

void Test_TC_N_01_AutoSuccess_KeepsRuntimeWithoutRecoveryStop()
{
    // Given: An automatic cycle that completed successfully.
    // When: Post-cycle recovery policy is evaluated.
    const bool shouldContinue =
        AcquisitionCycleRecoveryPolicy::ShouldContinueRuntimeAfterCycle(
            false,
            WaveAcquisition::TerminalStatus::Success);
    const bool shouldStop =
        AcquisitionCycleRecoveryPolicy::ShouldAttemptStopSamplingAfterCycle(
            false,
            WaveAcquisition::TerminalStatus::Success);

    // Then: Runtime continues and no recovery stop is attempted.
    TEST_ASSERT(shouldContinue, "TC-N-01 auto success should continue runtime");
    TEST_ASSERT(!shouldStop, "TC-N-01 auto success should not attempt recovery stop");
}

void Test_TC_N_02_AutoEp6Timeout_StopsRuntimeAndRequestsRecoveryStop()
{
    // Given: An automatic cycle that ended with EP6 timeout.
    // When: Post-cycle recovery policy is evaluated.
    const bool shouldContinue =
        AcquisitionCycleRecoveryPolicy::ShouldContinueRuntimeAfterCycle(
            false,
            WaveAcquisition::TerminalStatus::Ep6Timeout);
    const bool shouldStop =
        AcquisitionCycleRecoveryPolicy::ShouldAttemptStopSamplingAfterCycle(
            false,
            WaveAcquisition::TerminalStatus::Ep6Timeout);

    // Then: Runtime stops and a recovery stop is requested.
    TEST_ASSERT(!shouldContinue, "TC-N-02 auto timeout should stop runtime");
    TEST_ASSERT(shouldStop, "TC-N-02 auto timeout should attempt recovery stop");
}

void Test_TC_N_03_ManualSuccess_StopsRuntimeAndAlwaysStopsSampling()
{
    // Given: A manual cycle that completed successfully.
    // When: Post-cycle recovery policy is evaluated.
    const bool shouldContinue =
        AcquisitionCycleRecoveryPolicy::ShouldContinueRuntimeAfterCycle(
            true,
            WaveAcquisition::TerminalStatus::Success);
    const bool shouldStop =
        AcquisitionCycleRecoveryPolicy::ShouldAttemptStopSamplingAfterCycle(
            true,
            WaveAcquisition::TerminalStatus::Success);

    // Then: Runtime stops and the usual stop command is still required.
    TEST_ASSERT(!shouldContinue, "TC-N-03 manual success should stop runtime");
    TEST_ASSERT(shouldStop, "TC-N-03 manual success should stop sampling");
}

void Test_TC_B_01_AutoStopped_DoesNotAttemptRecoveryStop()
{
    // Given: An automatic cycle stopped by the user.
    // When: Post-cycle recovery policy is evaluated.
    const bool shouldContinue =
        AcquisitionCycleRecoveryPolicy::ShouldContinueRuntimeAfterCycle(
            false,
            WaveAcquisition::TerminalStatus::Stopped);
    const bool shouldStop =
        AcquisitionCycleRecoveryPolicy::ShouldAttemptStopSamplingAfterCycle(
            false,
            WaveAcquisition::TerminalStatus::Stopped);

    // Then: Runtime stops without extra recovery I/O.
    TEST_ASSERT(!shouldContinue, "TC-B-01 auto stopped should stop runtime");
    TEST_ASSERT(!shouldStop, "TC-B-01 auto stopped should not attempt recovery stop");
}

int main()
{
    std::printf("=== AcquisitionCycleRecoveryPolicy Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_AutoSuccess_KeepsRuntimeWithoutRecoveryStop);
    RUN_TEST(Test_TC_N_02_AutoEp6Timeout_StopsRuntimeAndRequestsRecoveryStop);
    RUN_TEST(Test_TC_N_03_ManualSuccess_StopsRuntimeAndAlwaysStopsSampling);
    RUN_TEST(Test_TC_B_01_AutoStopped_DoesNotAttemptRecoveryStop);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
