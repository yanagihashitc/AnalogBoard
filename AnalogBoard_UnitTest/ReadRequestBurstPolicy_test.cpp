#include <cstdio>

#include "../AnalogBoard_TestApp/ReadRequestBurstPolicy.h"

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

void Test_TC_N_01_FourFileBurstCap_ForOneMegFile()
{
    // Given: One file is 1 MiB and the legacy burst cap is 256 MiB.
    // When: The burst cap is resolved from the file size.
    // Then: The read request should be limited to four files.
    const unsigned long result =
        ReadRequestBurstPolicy::ResolveReadBurstCapBytes(1024UL * 1024UL, 256UL * 1024UL * 1024UL);
    TEST_ASSERT(result == 4UL * 1024UL * 1024UL, "TC-N-01 should cap to four 1 MiB files");
}

void Test_TC_N_02_FourFileBurstCap_ForTenMegFile()
{
    // Given: One file is 10 MiB and the legacy burst cap is 256 MiB.
    // When: The burst cap is resolved from the file size.
    // Then: The read request should be limited to four files.
    const unsigned long result =
        ReadRequestBurstPolicy::ResolveReadBurstCapBytes(10UL * 1024UL * 1024UL, 256UL * 1024UL * 1024UL);
    TEST_ASSERT(result == 40UL * 1024UL * 1024UL, "TC-N-02 should cap to four 10 MiB files");
}

void Test_TC_B_01_ZeroFileSize_FallsBackToLegacyCap()
{
    // Given: File size inference is unavailable.
    // When: The burst cap is resolved.
    // Then: The legacy burst cap should be preserved.
    const unsigned long result =
        ReadRequestBurstPolicy::ResolveReadBurstCapBytes(0UL, 256UL * 1024UL * 1024UL);
    TEST_ASSERT(result == 256UL * 1024UL * 1024UL, "TC-B-01 should preserve the legacy cap");
}

void Test_TC_B_02_UnalignedBurstCap_IsRoundedUpTo16KiB()
{
    // Given: Four files produce a non-aligned transfer size.
    // When: The burst cap is resolved.
    // Then: The cap should be rounded up to the 16 KiB USB transfer boundary.
    const unsigned long result =
        ReadRequestBurstPolicy::ResolveReadBurstCapBytes(100000UL, 256UL * 1024UL * 1024UL);
    TEST_ASSERT(result == 409600UL, "TC-B-02 should round up to the next 16 KiB boundary");
}

void Test_TC_B_03_BurstCap_DoesNotExceedLegacyCap()
{
    // Given: Four files would exceed the legacy 256 MiB burst cap.
    // When: The burst cap is resolved.
    // Then: The legacy cap should remain the upper bound.
    const unsigned long result =
        ReadRequestBurstPolicy::ResolveReadBurstCapBytes(80UL * 1024UL * 1024UL, 256UL * 1024UL * 1024UL);
    TEST_ASSERT(result == 256UL * 1024UL * 1024UL, "TC-B-03 should clamp to the legacy cap");
}

int main()
{
    std::printf("=== ReadRequestBurstPolicy Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_FourFileBurstCap_ForOneMegFile);
    RUN_TEST(Test_TC_N_02_FourFileBurstCap_ForTenMegFile);
    RUN_TEST(Test_TC_B_01_ZeroFileSize_FallsBackToLegacyCap);
    RUN_TEST(Test_TC_B_02_UnalignedBurstCap_IsRoundedUpTo16KiB);
    RUN_TEST(Test_TC_B_03_BurstCap_DoesNotExceedLegacyCap);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount > 0 ? 1 : 0;
}
