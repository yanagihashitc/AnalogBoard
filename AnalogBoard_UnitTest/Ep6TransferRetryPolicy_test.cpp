#include <cstdio>
#include <vector>

#include "../AnalogBoard_Dll/Ep6TransferRetryPolicy.h"

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

void Test_TC_N_01_ImmediateSuccess_NoRetry()
{
    // Given: The transfer succeeds on the first attempt.
    int transferCalls = 0;
    std::vector<unsigned long> sleepCalls;
    int attempts = 0;
    int retries = 0;

    // When: The retry helper executes the transfer.
    const bool success = Ep6TransferRetryPolicy::ExecuteWithRetry(
        [&]() -> bool
        {
            ++transferCalls;
            return true;
        },
        [&](unsigned long backoffMs)
        {
            sleepCalls.push_back(backoffMs);
        },
        &attempts,
        &retries);

    // Then: Success is returned without retry or backoff.
    TEST_ASSERT(success, "TC-N-01 should succeed");
    TEST_ASSERT(transferCalls == 1, "TC-N-01 should call transfer once");
    TEST_ASSERT(attempts == 1, "TC-N-01 should report one attempt");
    TEST_ASSERT(retries == 0, "TC-N-01 should report zero retries");
    TEST_ASSERT(sleepCalls.empty(), "TC-N-01 should not sleep");
}

void Test_TC_N_02_TransientFailure_RetriesAndSucceeds()
{
    // Given: The first attempt fails and the second succeeds.
    int transferCalls = 0;
    std::vector<unsigned long> sleepCalls;
    int attempts = 0;
    int retries = 0;

    // When: The retry helper executes with the default retry budget.
    const bool success = Ep6TransferRetryPolicy::ExecuteWithRetry(
        [&]() -> bool
        {
            ++transferCalls;
            return transferCalls >= 2;
        },
        [&](unsigned long backoffMs)
        {
            sleepCalls.push_back(backoffMs);
        },
        &attempts,
        &retries);

    // Then: The transient failure is recovered by one retry.
    TEST_ASSERT(success, "TC-N-02 should succeed after retry");
    TEST_ASSERT(transferCalls == 2, "TC-N-02 should call transfer twice");
    TEST_ASSERT(attempts == 2, "TC-N-02 should report two attempts");
    TEST_ASSERT(retries == 1, "TC-N-02 should report one retry");
    TEST_ASSERT(sleepCalls.size() == 1, "TC-N-02 should sleep once");
    TEST_ASSERT(sleepCalls[0] == Ep6TransferRetryPolicy::kEp6TimeoutRetryBackoffMs, "TC-N-02 should use default backoff");
}

void Test_TC_A_01_ExhaustedRetryBudget_Fails()
{
    // Given: Every transfer attempt fails.
    int transferCalls = 0;
    std::vector<unsigned long> sleepCalls;
    int attempts = 0;
    int retries = 0;

    // When: The retry helper executes with one retry budget.
    const bool success = Ep6TransferRetryPolicy::ExecuteWithRetry(
        [&]() -> bool
        {
            ++transferCalls;
            return false;
        },
        [&](unsigned long backoffMs)
        {
            sleepCalls.push_back(backoffMs);
        },
        &attempts,
        &retries,
        1,
        1);

    // Then: The helper fails after consuming the retry budget.
    TEST_ASSERT(!success, "TC-A-01 should fail after retry budget is exhausted");
    TEST_ASSERT(transferCalls == 2, "TC-A-01 should stop after initial attempt plus one retry");
    TEST_ASSERT(attempts == 2, "TC-A-01 should report two attempts");
    TEST_ASSERT(retries == 1, "TC-A-01 should report one retry");
    TEST_ASSERT(sleepCalls.size() == 1, "TC-A-01 should sleep once");
}

void Test_TC_B_01_ZeroRetryBudget_FailsImmediately()
{
    // Given: Retry budget is disabled.
    int transferCalls = 0;
    std::vector<unsigned long> sleepCalls;
    int attempts = 0;
    int retries = 0;

    // When: The transfer fails under zero retry budget.
    const bool success = Ep6TransferRetryPolicy::ExecuteWithRetry(
        [&]() -> bool
        {
            ++transferCalls;
            return false;
        },
        [&](unsigned long backoffMs)
        {
            sleepCalls.push_back(backoffMs);
        },
        &attempts,
        &retries,
        0,
        1);

    // Then: No retry or backoff occurs.
    TEST_ASSERT(!success, "TC-B-01 should fail immediately");
    TEST_ASSERT(transferCalls == 1, "TC-B-01 should call transfer once");
    TEST_ASSERT(attempts == 1, "TC-B-01 should report one attempt");
    TEST_ASSERT(retries == 0, "TC-B-01 should report zero retries");
    TEST_ASSERT(sleepCalls.empty(), "TC-B-01 should not sleep");
}

void Test_TC_B_02_ZeroBackoff_RetriesWithoutSleep()
{
    // Given: The transfer recovers on retry, but backoff is set to zero.
    int transferCalls = 0;
    std::vector<unsigned long> sleepCalls;
    int attempts = 0;
    int retries = 0;

    // When: The helper executes with zero backoff.
    const bool success = Ep6TransferRetryPolicy::ExecuteWithRetry(
        [&]() -> bool
        {
            ++transferCalls;
            return transferCalls >= 2;
        },
        [&](unsigned long backoffMs)
        {
            sleepCalls.push_back(backoffMs);
        },
        &attempts,
        &retries,
        1,
        0);

    // Then: Retry still occurs, but no sleep call is issued.
    TEST_ASSERT(success, "TC-B-02 should succeed");
    TEST_ASSERT(transferCalls == 2, "TC-B-02 should call transfer twice");
    TEST_ASSERT(attempts == 2, "TC-B-02 should report two attempts");
    TEST_ASSERT(retries == 1, "TC-B-02 should report one retry");
    TEST_ASSERT(sleepCalls.empty(), "TC-B-02 should not sleep when backoff is zero");
}

int main()
{
    std::printf("=== Ep6TransferRetryPolicy Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_ImmediateSuccess_NoRetry);
    RUN_TEST(Test_TC_N_02_TransientFailure_RetriesAndSucceeds);
    RUN_TEST(Test_TC_A_01_ExhaustedRetryBudget_Fails);
    RUN_TEST(Test_TC_B_01_ZeroRetryBudget_FailsImmediately);
    RUN_TEST(Test_TC_B_02_ZeroBackoff_RetriesWithoutSleep);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount > 0 ? 1 : 0;
}
