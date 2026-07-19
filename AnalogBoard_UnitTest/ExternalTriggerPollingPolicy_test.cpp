#include <cstdio>

#include "../AnalogBoard_TestApp/ExternalTriggerPollingPolicy.h"

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

void Test_ETP_N_01_IdlePollContinuesAfterBoundedDelay()
{
    const auto decision = ExternalTriggerPollingPolicy::Evaluate(
        true,
        false,
        false,
        true);

    TEST_ASSERT(
        decision.action == ExternalTriggerPollingPolicy::Action::kContinueAfterDelay,
        "ETP-N-01 idle poll should continue only through the delayed action");
    TEST_ASSERT(
        decision.delayMs == 10u,
        "ETP-N-01 external-trigger idle cadence should be 10 ms");
}

void Test_ETP_N_02_TriggerDoesNotAddDelay()
{
    const auto decision = ExternalTriggerPollingPolicy::Evaluate(
        true,
        true,
        false,
        true);

    TEST_ASSERT(
        decision.action == ExternalTriggerPollingPolicy::Action::kTriggered,
        "ETP-N-02 a detected trigger should start acquisition");
    TEST_ASSERT(decision.delayMs == 0u, "ETP-N-02 trigger path should not sleep");
}

void Test_ETP_A_01_TransferFailureIsFatalWithoutRetryDelay()
{
    const auto decision = ExternalTriggerPollingPolicy::Evaluate(
        false,
        false,
        false,
        true);

    TEST_ASSERT(
        decision.action == ExternalTriggerPollingPolicy::Action::kTransferFailed,
        "ETP-A-01 the first EP4 transfer failure should remain fatal");
    TEST_ASSERT(decision.delayMs == 0u, "ETP-A-01 failure should not be hidden by retry delay");
}

void Test_ETP_A_02_ModeChangeAndCancelStopWithoutDelay()
{
    const auto modeChanged = ExternalTriggerPollingPolicy::Evaluate(
        true,
        false,
        true,
        true);
    const auto cancelled = ExternalTriggerPollingPolicy::Evaluate(
        true,
        false,
        false,
        false);

    TEST_ASSERT(
        modeChanged.action == ExternalTriggerPollingPolicy::Action::kModeChanged,
        "ETP-A-02 a mode change should stop the wait loop");
    TEST_ASSERT(modeChanged.delayMs == 0u, "ETP-A-02 mode change should not sleep");
    TEST_ASSERT(
        cancelled.action == ExternalTriggerPollingPolicy::Action::kCancelled,
        "ETP-A-02 a cancelled sampling request should stop the wait loop");
    TEST_ASSERT(cancelled.delayMs == 0u, "ETP-A-02 cancel should not sleep");
}

void Test_ETP_A_03_TransferFailureTakesPriorityOverAllSignals()
{
    const auto decision = ExternalTriggerPollingPolicy::Evaluate(
        false,
        true,
        true,
        false);

    TEST_ASSERT(
        decision.action == ExternalTriggerPollingPolicy::Action::kTransferFailed,
        "ETP-A-03 transfer failure should take priority over trigger, mode change, and cancel");
}

void Test_ETP_A_04_TriggerTakesPriorityOverModeChange()
{
    const auto samplingRequested = ExternalTriggerPollingPolicy::Evaluate(
        true,
        true,
        true,
        true);
    const auto samplingCancelled = ExternalTriggerPollingPolicy::Evaluate(
        true,
        true,
        true,
        false);

    TEST_ASSERT(
        samplingRequested.action == ExternalTriggerPollingPolicy::Action::kTriggered,
        "ETP-A-04 trigger should take priority over mode change while sampling is requested");
    TEST_ASSERT(
        samplingCancelled.action == ExternalTriggerPollingPolicy::Action::kTriggered,
        "ETP-A-04 trigger should take priority over mode change after sampling is cancelled");
}

void Test_ETP_A_05_TriggerTakesPriorityOverCancellation()
{
    const auto decision = ExternalTriggerPollingPolicy::Evaluate(
        true,
        true,
        false,
        false);

    TEST_ASSERT(
        decision.action == ExternalTriggerPollingPolicy::Action::kTriggered,
        "ETP-A-05 trigger should take priority over cancellation");
}

void Test_ETP_A_06_ModeChangeTakesPriorityOverCancellation()
{
    const auto decision = ExternalTriggerPollingPolicy::Evaluate(
        true,
        false,
        true,
        false);

    TEST_ASSERT(
        decision.action == ExternalTriggerPollingPolicy::Action::kModeChanged,
        "ETP-A-06 mode change should take priority over cancellation");
}

void Test_ETP_A_07_ApplicationShutdownTakesPriorityOverTrigger()
{
    const auto decision = ExternalTriggerPollingPolicy::Evaluate(
        true,
        true,
        false,
        true,
        true);

    TEST_ASSERT(
        decision.action == ExternalTriggerPollingPolicy::Action::kCancelled,
        "ETP-A-07 application shutdown should cancel a simultaneous trigger");
    TEST_ASSERT(decision.delayMs == 0u, "ETP-A-07 shutdown should not sleep");
}

void Test_ETP_A_08_TransferFailureRemainsFatalDuringShutdown()
{
    const auto decision = ExternalTriggerPollingPolicy::Evaluate(
        false,
        true,
        false,
        true,
        true);

    TEST_ASSERT(
        decision.action == ExternalTriggerPollingPolicy::Action::kTransferFailed,
        "ETP-A-08 transfer failure should remain observable during shutdown");
}

int main()
{
    std::printf("=== ExternalTriggerPollingPolicy Unit Tests ===\n\n");

    RUN_TEST(Test_ETP_N_01_IdlePollContinuesAfterBoundedDelay);
    RUN_TEST(Test_ETP_N_02_TriggerDoesNotAddDelay);
    RUN_TEST(Test_ETP_A_01_TransferFailureIsFatalWithoutRetryDelay);
    RUN_TEST(Test_ETP_A_02_ModeChangeAndCancelStopWithoutDelay);
    RUN_TEST(Test_ETP_A_03_TransferFailureTakesPriorityOverAllSignals);
    RUN_TEST(Test_ETP_A_04_TriggerTakesPriorityOverModeChange);
    RUN_TEST(Test_ETP_A_05_TriggerTakesPriorityOverCancellation);
    RUN_TEST(Test_ETP_A_06_ModeChangeTakesPriorityOverCancellation);
    RUN_TEST(Test_ETP_A_07_ApplicationShutdownTakesPriorityOverTrigger);
    RUN_TEST(Test_ETP_A_08_TransferFailureRemainsFatalDuringShutdown);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
