#include <windows.h>

#include <cstdio>

#include "../AnalogBoard_Dll/UsbTransferHelpers.h"

static int g_TestCount = 0;
static int g_PassCount = 0;
static int g_FailCount = 0;
static constexpr size_t kEp6OneTimeMaxSize = static_cast<size_t>(1024) * 1024 * 4;

#define TEST_ASSERT(cond, msg) do { \
    g_TestCount++; \
    if (cond) { g_PassCount++; } \
    else { g_FailCount++; std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define RUN_TEST(func) do { \
    std::printf("[TEST] %s\n", #func); \
    func(); \
} while(0)

struct CountingCloser
{
    int* closeCount = nullptr;

    void operator()(HANDLE handle) const
    {
        if (handle != nullptr && closeCount != nullptr)
        {
            ++(*closeCount);
        }
    }
};

struct CountingReleaser
{
    int* releaseCount = nullptr;

    BOOL operator()(HANDLE handle) const
    {
        if (handle != nullptr && releaseCount != nullptr)
        {
            ++(*releaseCount);
        }
        return TRUE;
    }
};

void Test_TC_N_01_Ep2RequiresSharedMutex()
{
    // Given: EP2 command transfer uses the shared EP2/EP4 path.
    // When: The transfer helper is asked whether EP2 requires the shared mutex.
    // Then: The helper should require the mutex.
    TEST_ASSERT(
        UsbTransferHelpers::RequiresEp2Ep4Mutex(UsbTransferHelpers::TransferEndpoint::Ep2),
        "TC-N-01 EP2 must require the shared mutex");
}

void Test_TC_N_02_Ep4RequiresSharedMutex()
{
    // Given: EP4 register read uses the shared EP2/EP4 path.
    // When: The transfer helper is asked whether EP4 requires the shared mutex.
    // Then: The helper should require the mutex.
    TEST_ASSERT(
        UsbTransferHelpers::RequiresEp2Ep4Mutex(UsbTransferHelpers::TransferEndpoint::Ep4),
        "TC-N-02 EP4 must require the shared mutex");
}

void Test_TC_N_03_Ep6RequiresSharedMutex()
{
    // Given: EP6 bulk read shares the same USB session as EP2/EP4 commands.
    // When: The transfer helper is asked whether EP6 requires the shared mutex.
    // Then: The helper should require the mutex.
    TEST_ASSERT(
        UsbTransferHelpers::RequiresEp2Ep4Mutex(UsbTransferHelpers::TransferEndpoint::Ep6),
        "TC-N-03 EP6 must require the shared mutex");
}

void Test_TC_B_01_MutexWaitTimeoutIsFixedToFiveSeconds()
{
    // Given: PR-02 defines a fixed mutex wait timeout.
    // When: The timeout constant is read from the helper.
    // Then: The timeout should be exactly 5000ms.
    DWORD timeoutMs = UsbTransferHelpers::kEp2Ep4MutexWaitTimeoutMs;
    TEST_ASSERT(
        timeoutMs == 5000,
        "TC-B-01 mutex wait timeout must be 5000ms");
}

void Test_TC_N_04_ResetOverlappedPreservesOnlyEventHandle()
{
    OVERLAPPED overlapped = {};
    overlapped.Internal = 1;
    overlapped.InternalHigh = 2;
    overlapped.Offset = 3;
    overlapped.OffsetHigh = 4;
    const HANDLE expectedEvent = reinterpret_cast<HANDLE>(0x1234);

    // Given: OVERLAPPED contains stale values before a new transfer starts.
    // When: The helper resets the structure for a specific event handle.
    // Then: Only the event handle remains and every other field is cleared.
    UsbTransferHelpers::ResetOverlappedWithEvent(&overlapped, expectedEvent);

    TEST_ASSERT(overlapped.Internal == 0, "TC-N-04 Internal must be zero");
    TEST_ASSERT(overlapped.InternalHigh == 0, "TC-N-04 InternalHigh must be zero");
    TEST_ASSERT(overlapped.Offset == 0, "TC-N-04 Offset must be zero");
    TEST_ASSERT(overlapped.OffsetHigh == 0, "TC-N-04 OffsetHigh must be zero");
    TEST_ASSERT(overlapped.hEvent == expectedEvent, "TC-N-04 hEvent must match input");
}

void Test_TC_N_05_ScopedHandleClosesExactlyOnce()
{
    int closeCount = 0;
    const HANDLE dummyHandle = reinterpret_cast<HANDLE>(0x1);

    // Given: A scoped handle owns a valid event handle.
    // When: The scoped handle goes out of scope.
    // Then: The configured closer must run exactly once.
    {
        UsbTransferHelpers::ScopedHandle<CountingCloser> handle(dummyHandle, CountingCloser{ &closeCount });
    }

    TEST_ASSERT(closeCount == 1, "TC-N-05 closer must run exactly once");
}

void Test_TC_N_06_ReusableEp6BufferReusesAllocation()
{
    UsbTransferHelpers::ReusableTransferBuffer buffer;

    // Given: A reusable transfer buffer is allocated for EP6 transfers.
    // When: The same size is requested twice.
    // Then: The existing allocation should be reused.
    TEST_ASSERT(buffer.EnsureSize(kEp6OneTimeMaxSize), "TC-N-06 initial EnsureSize should succeed");
    BYTE* firstPtr = buffer.Data();
    const size_t firstCapacity = buffer.Capacity();

    TEST_ASSERT(buffer.EnsureSize(kEp6OneTimeMaxSize), "TC-N-06 second EnsureSize should succeed");
    TEST_ASSERT(buffer.Data() == firstPtr, "TC-N-06 buffer pointer should be reused");
    TEST_ASSERT(buffer.Capacity() == firstCapacity, "TC-N-06 capacity should remain unchanged");
}

void Test_TC_B_02_ReusableEp6BufferGrowsWhenNeeded()
{
    UsbTransferHelpers::ReusableTransferBuffer buffer;

    // Given: A reusable transfer buffer already has a smaller allocation.
    // When: A larger size is requested.
    // Then: The capacity should grow to satisfy the request.
    TEST_ASSERT(buffer.EnsureSize(1024), "TC-B-02 initial small allocation should succeed");
    TEST_ASSERT(buffer.EnsureSize(kEp6OneTimeMaxSize), "TC-B-02 larger allocation should succeed");
    TEST_ASSERT(buffer.Capacity() >= kEp6OneTimeMaxSize, "TC-B-02 capacity must satisfy requested size");
}

void Test_TC_N_07_ReleaseMutexIfOwned_ReleasesExactlyOnce()
{
    int releaseCount = 0;
    const HANDLE dummyHandle = reinterpret_cast<HANDLE>(0x2);

    // Given: The caller owns a valid mutex and needs to release it.
    // When: The helper releases the mutex through the configured releaser.
    // Then: The releaser should run exactly once and report success.
    const bool released = UsbTransferHelpers::ReleaseMutexIfOwned(
        true,
        dummyHandle,
        CountingReleaser{ &releaseCount });

    TEST_ASSERT(released, "TC-N-07 release should report success");
    TEST_ASSERT(releaseCount == 1, "TC-N-07 releaser must run exactly once");
}

void Test_TC_N_08_ReleaseMutexIfOwned_SkipsWhenNotOwned()
{
    int releaseCount = 0;
    const HANDLE dummyHandle = reinterpret_cast<HANDLE>(0x3);

    // Given: The caller does not own the shared mutex.
    // When: The helper is asked to release it.
    // Then: No release should occur.
    const bool released = UsbTransferHelpers::ReleaseMutexIfOwned(
        false,
        dummyHandle,
        CountingReleaser{ &releaseCount });

    TEST_ASSERT(!released, "TC-N-08 release should be skipped");
    TEST_ASSERT(releaseCount == 0, "TC-N-08 releaser must not run");
}

void Test_TC_B_03_ReleaseMutexIfOwned_NullHandleSkipsRelease()
{
    int releaseCount = 0;

    // Given: The caller believes it owns the mutex but the handle is null.
    // When: The helper validates the handle before releasing.
    // Then: No release should occur and the helper should report false.
    const bool released = UsbTransferHelpers::ReleaseMutexIfOwned(
        true,
        nullptr,
        CountingReleaser{ &releaseCount });

    TEST_ASSERT(!released, "TC-B-03 null handle should not be released");
    TEST_ASSERT(releaseCount == 0, "TC-B-03 releaser must not run");
}

void Test_TC_N_09_ReusableEp6BufferZeroFillClearsExistingBytes()
{
    UsbTransferHelpers::ReusableTransferBuffer buffer;

    // Given: A reused EP6 buffer still contains bytes from the previous transfer.
    // When: The helper zero-fills the active range.
    // Then: The specified bytes should be cleared to zero.
    TEST_ASSERT(buffer.EnsureSize(16), "TC-N-09 EnsureSize should succeed");
    BYTE* data = buffer.Data();
    TEST_ASSERT(data != nullptr, "TC-N-09 Data should not be null");
    memset(data, 0xAB, 16);

    TEST_ASSERT(buffer.ZeroFill(16), "TC-N-09 ZeroFill should succeed");
    TEST_ASSERT(data[0] == 0, "TC-N-09 first byte should be zero");
    TEST_ASSERT(data[15] == 0, "TC-N-09 last byte should be zero");
}

void Test_TC_B_04_ReusableEp6BufferZeroFill_ZeroBytesIsNoOp()
{
    UsbTransferHelpers::ReusableTransferBuffer buffer;

    // Given: A reusable buffer has existing data and zero bytes are requested.
    // When: ZeroFill is called with a zero length.
    // Then: The call should succeed without modifying the buffer.
    TEST_ASSERT(buffer.EnsureSize(4), "TC-B-04 EnsureSize should succeed");
    BYTE* data = buffer.Data();
    TEST_ASSERT(data != nullptr, "TC-B-04 Data should not be null");
    data[0] = 0x5A;

    TEST_ASSERT(buffer.ZeroFill(0), "TC-B-04 ZeroFill(0) should succeed");
    TEST_ASSERT(data[0] == 0x5A, "TC-B-04 existing data should remain unchanged");
}

void Test_TC_B_05_ReusableEp6BufferZeroFill_RejectsOutOfRangeSize()
{
    UsbTransferHelpers::ReusableTransferBuffer buffer;

    // Given: A reusable buffer has less capacity than the requested clear size.
    // When: ZeroFill is called beyond the allocated capacity.
    // Then: The helper should reject the request.
    TEST_ASSERT(buffer.EnsureSize(8), "TC-B-05 EnsureSize should succeed");
    TEST_ASSERT(!buffer.ZeroFill(9), "TC-B-05 ZeroFill beyond capacity should fail");
}

int main()
{
    std::printf("=== UsbTransferHelpers Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_Ep2RequiresSharedMutex);
    RUN_TEST(Test_TC_N_02_Ep4RequiresSharedMutex);
    RUN_TEST(Test_TC_N_03_Ep6RequiresSharedMutex);
    RUN_TEST(Test_TC_B_01_MutexWaitTimeoutIsFixedToFiveSeconds);
    RUN_TEST(Test_TC_N_04_ResetOverlappedPreservesOnlyEventHandle);
    RUN_TEST(Test_TC_N_05_ScopedHandleClosesExactlyOnce);
    RUN_TEST(Test_TC_N_06_ReusableEp6BufferReusesAllocation);
    RUN_TEST(Test_TC_B_02_ReusableEp6BufferGrowsWhenNeeded);
    RUN_TEST(Test_TC_N_07_ReleaseMutexIfOwned_ReleasesExactlyOnce);
    RUN_TEST(Test_TC_N_08_ReleaseMutexIfOwned_SkipsWhenNotOwned);
    RUN_TEST(Test_TC_B_03_ReleaseMutexIfOwned_NullHandleSkipsRelease);
    RUN_TEST(Test_TC_N_09_ReusableEp6BufferZeroFillClearsExistingBytes);
    RUN_TEST(Test_TC_B_04_ReusableEp6BufferZeroFill_ZeroBytesIsNoOp);
    RUN_TEST(Test_TC_B_05_ReusableEp6BufferZeroFill_RejectsOutOfRangeSize);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount > 0 ? 1 : 0;
}
