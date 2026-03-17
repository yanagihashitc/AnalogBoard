#include <cstdio>

#include "../AnalogBoard_TestApp/FileIoLoggingPolicy.h"

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

WaveDataFileIO::RenameAttemptResult MakeRenameResult(bool retried)
{
    WaveDataFileIO::RenameAttemptResult result = {};
    result.success = true;
    result.retried = retried;
    return result;
}

void Test_TC_N_01_OpenSuccess_IsSuppressed()
{
    // Given: Per-pair tmp open succeeded on the hot path.
    // When: The logging policy is queried.
    // Then: Ordinary open success should not be logged.
    TEST_ASSERT(!FileIoLoggingPolicy::ShouldLogOpenSuccess(), "TC-N-01 open success should be suppressed");
}

void Test_TC_N_02_WriteSuccess_IsSuppressed()
{
    // Given: Per-pair write succeeded on the hot path.
    // When: The logging policy is queried.
    // Then: Ordinary write success should not be logged.
    TEST_ASSERT(!FileIoLoggingPolicy::ShouldLogWriteSuccess(), "TC-N-02 write success should be suppressed");
}

void Test_TC_N_03_CloseSuccess_IsSuppressed()
{
    // Given: Per-pair close succeeded on the hot path.
    // When: The logging policy is queried.
    // Then: Ordinary close success should not be logged.
    TEST_ASSERT(!FileIoLoggingPolicy::ShouldLogCloseSuccess(), "TC-N-03 close success should be suppressed");
}

void Test_TC_B_01_PublishSuccessWithoutRetry_IsSuppressed()
{
    // Given: Publish succeeded without any rename retry.
    const WaveDataFileIO::RenameAttemptResult renameResult = MakeRenameResult(false);

    // When: The publish success logging policy is queried.
    // Then: Ordinary publish success should not be logged.
    TEST_ASSERT(!FileIoLoggingPolicy::ShouldLogPublishSuccess(renameResult), "TC-B-01 non-retried publish success should be suppressed");
}

void Test_TC_B_02_PublishSuccessAfterLowRetry_IsLogged()
{
    // Given: Publish succeeded after a rename retry.
    const WaveDataFileIO::RenameAttemptResult renameResult = MakeRenameResult(true);

    // When: The publish success logging policy is queried.
    // Then: Retried publish success should remain observable.
    TEST_ASSERT(FileIoLoggingPolicy::ShouldLogPublishSuccess(renameResult), "TC-B-02 retried publish success should be logged");
}

void Test_TC_B_03_PublishSuccessAfterHighRetry_IsLogged()
{
    // Given: High-side publish also uses the same retry-based policy.
    const WaveDataFileIO::RenameAttemptResult renameResult = MakeRenameResult(true);

    // When: The publish success logging policy is queried.
    // Then: High-side retried publish success should remain observable.
    TEST_ASSERT(FileIoLoggingPolicy::ShouldLogPublishSuccess(renameResult), "TC-B-03 retried publish success should be logged");
}

int main()
{
    std::printf("=== FileIoLoggingPolicy Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_OpenSuccess_IsSuppressed);
    RUN_TEST(Test_TC_N_02_WriteSuccess_IsSuppressed);
    RUN_TEST(Test_TC_N_03_CloseSuccess_IsSuppressed);
    RUN_TEST(Test_TC_B_01_PublishSuccessWithoutRetry_IsSuppressed);
    RUN_TEST(Test_TC_B_02_PublishSuccessAfterLowRetry_IsLogged);
    RUN_TEST(Test_TC_B_03_PublishSuccessAfterHighRetry_IsLogged);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount > 0 ? 1 : 0;
}
