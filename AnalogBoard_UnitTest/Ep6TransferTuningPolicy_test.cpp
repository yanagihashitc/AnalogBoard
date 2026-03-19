#include <cstdio>

#include "../AnalogBoard_Dll/Ep6TransferRetryPolicy.h"
#include "../AnalogBoard_Dll/Ep6TransferTuningPolicy.h"

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

void Test_TC_N_01_Defaults_ExposeExpectedTimeoutAndRetryContract()
{
    // Given: The EP6 transfer tuning policy defaults.
    // When: The timeout and retry defaults are inspected.
    // Then: The DLL timeout and retry helper share the same contract.
    const ULONG timeoutMs = Ep6TransferTuningPolicy::kEp6BulkEndpointTimeoutMs;
    const int maxRetries = Ep6TransferTuningPolicy::kEp6TimeoutRetryMaxRetries;
    const DWORD backoffMs = Ep6TransferTuningPolicy::kEp6TimeoutRetryBackoffMs;
    const DWORD retryPolicyBackoffMs = Ep6TransferRetryPolicy::kEp6TimeoutRetryBackoffMs;
    const bool timeoutMatches = (timeoutMs == 30000u);
    const bool retryMatches = (maxRetries == 1);
    const bool backoffMatches = (backoffMs == 5u);
    const bool policyBackoffMatches = (retryPolicyBackoffMs == backoffMs);
    TEST_ASSERT(timeoutMatches, "TC-N-01 timeout should be 30000ms");
    TEST_ASSERT(retryMatches, "TC-N-01 retry max should remain 1");
    TEST_ASSERT(backoffMatches, "TC-N-01 retry backoff should be 5ms");
    TEST_ASSERT(policyBackoffMatches, "TC-N-01 retry helper should use tuning backoff");
}

void Test_TC_N_02_ApplyBulkInDefaults_WritesTimeout()
{
    // Given: An unset endpoint timeout field.
    ULONG timeoutMs = 0u;

    // When: The EP6 bulk-in defaults are applied.
    Ep6TransferTuningPolicy::ApplyBulkInDefaults(&timeoutMs);

    // Then: The timeout field is set to the tuned timeout value.
    TEST_ASSERT(timeoutMs == Ep6TransferTuningPolicy::kEp6BulkEndpointTimeoutMs, "TC-N-02 timeout should be written");
}

void Test_TC_B_01_ApplyBulkInDefaults_AllowsNullPointer()
{
    // Given: No timeout field is available.
    // When: The EP6 bulk-in defaults are applied to null.
    Ep6TransferTuningPolicy::ApplyBulkInDefaults(nullptr);

    // Then: The helper safely no-ops.
    TEST_ASSERT(true, "TC-B-01 null timeout pointer should be allowed");
}

int main()
{
    std::printf("=== Ep6TransferTuningPolicy Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_Defaults_ExposeExpectedTimeoutAndRetryContract);
    RUN_TEST(Test_TC_N_02_ApplyBulkInDefaults_WritesTimeout);
    RUN_TEST(Test_TC_B_01_ApplyBulkInDefaults_AllowsNullPointer);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount > 0 ? 1 : 0;
}
