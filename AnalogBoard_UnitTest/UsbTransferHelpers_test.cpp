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

void Test_TC_N_03_Ep6SkipsSharedMutex()
{
    // Given: EP6 bulk read is independent from EP2/EP4.
    // When: The transfer helper is asked whether EP6 requires the shared mutex.
    // Then: The helper should not require the mutex.
    TEST_ASSERT(
        !UsbTransferHelpers::RequiresEp2Ep4Mutex(UsbTransferHelpers::TransferEndpoint::Ep6),
        "TC-N-03 EP6 must not require the shared mutex");
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

int main()
{
    std::printf("=== UsbTransferHelpers Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_Ep2RequiresSharedMutex);
    RUN_TEST(Test_TC_N_02_Ep4RequiresSharedMutex);
    RUN_TEST(Test_TC_N_03_Ep6SkipsSharedMutex);
    RUN_TEST(Test_TC_B_01_MutexWaitTimeoutIsFixedToFiveSeconds);
    RUN_TEST(Test_TC_N_04_ResetOverlappedPreservesOnlyEventHandle);
    RUN_TEST(Test_TC_N_05_ScopedHandleClosesExactlyOnce);
    RUN_TEST(Test_TC_N_06_ReusableEp6BufferReusesAllocation);
    RUN_TEST(Test_TC_B_02_ReusableEp6BufferGrowsWhenNeeded);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount > 0 ? 1 : 0;
}
