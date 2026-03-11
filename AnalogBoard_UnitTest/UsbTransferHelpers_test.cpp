#include <windows.h>

#include <utility>

#include "TestFramework.h"
#include "../AnalogBoard_Dll/UsbTransferHelpers.h"

static constexpr size_t kEp6OneTimeMaxSize = static_cast<size_t>(1024) * 1024 * 4;

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

void Test_TC_N_10_ScopedHeapBufferAllocatesZeroedMaxTransfer()
{
    UsbTransferHelpers::ScopedHeapBuffer buffer;

    // Given: EP6 comparison fix uses a fresh local heap buffer for the maximum transfer size.
    // When: The helper allocates a 4MB scratch buffer.
    // Then: Allocation succeeds and the range is zero-initialized.
    TEST_ASSERT(buffer.Allocate(kEp6OneTimeMaxSize), "TC-N-10 max allocation should succeed");
    TEST_ASSERT(buffer.Data() != nullptr, "TC-N-10 buffer pointer must be valid");
    TEST_ASSERT(buffer.Capacity() == kEp6OneTimeMaxSize, "TC-N-10 capacity must match requested size");
    TEST_ASSERT(buffer.Data()[0] == 0, "TC-N-10 first byte must be zero");
    TEST_ASSERT(buffer.Data()[kEp6OneTimeMaxSize - 1] == 0, "TC-N-10 last byte must be zero");
}

void Test_TC_N_11_ScopedHeapBufferAllocatesZeroedMinimumPositiveSize()
{
    UsbTransferHelpers::ScopedHeapBuffer buffer;

    // Given: The local scratch buffer helper accepts the smallest meaningful positive size.
    // When: One byte is allocated.
    // Then: Allocation succeeds and the byte is zero-initialized.
    TEST_ASSERT(buffer.Allocate(1), "TC-N-11 min positive allocation should succeed");
    TEST_ASSERT(buffer.Data() != nullptr, "TC-N-11 buffer pointer must be valid");
    TEST_ASSERT(buffer.Capacity() == 1, "TC-N-11 capacity must be one byte");
    TEST_ASSERT(buffer.Data()[0] == 0, "TC-N-11 allocated byte must be zero");
}

void Test_TC_B_06_ScopedHeapBufferRejectsZeroSize()
{
    UsbTransferHelpers::ScopedHeapBuffer buffer;

    // Given: No scratch buffer has been allocated yet.
    // When: The helper is asked to allocate zero bytes.
    // Then: The request is rejected and the helper stays empty.
    TEST_ASSERT(!buffer.Allocate(0), "TC-B-06 zero-size allocation must fail");
    TEST_ASSERT(buffer.Data() == nullptr, "TC-B-06 buffer pointer must stay null");
    TEST_ASSERT(buffer.Capacity() == 0, "TC-B-06 capacity must stay zero");
}

void Test_TC_B_07_ScopedHeapBufferZeroSizeClearsPreviousAllocation()
{
    UsbTransferHelpers::ScopedHeapBuffer buffer;

    // Given: A previous scratch buffer allocation succeeded.
    // When: The helper is asked to allocate zero bytes next.
    // Then: The previous allocation is released and state returns to empty.
    TEST_ASSERT(buffer.Allocate(1024), "TC-B-07 initial allocation should succeed");
    TEST_ASSERT(!buffer.Allocate(0), "TC-B-07 zero-size reallocation must fail");
    TEST_ASSERT(buffer.Data() == nullptr, "TC-B-07 buffer pointer must be cleared");
    TEST_ASSERT(buffer.Capacity() == 0, "TC-B-07 capacity must be cleared");
}

void Test_TC_N_12_ScopedHeapBufferMoveConstructorTransfersOwnership()
{
    UsbTransferHelpers::ScopedHeapBuffer source;

    // Given: A scratch buffer owns a valid allocation before move construction.
    // When: Ownership is transferred into a new helper via move construction.
    // Then: The new helper owns the buffer and the source becomes empty.
    TEST_ASSERT(source.Allocate(1024), "TC-N-12 initial allocation should succeed");
    BYTE* originalData = source.Data();

    UsbTransferHelpers::ScopedHeapBuffer moved(std::move(source));

    TEST_ASSERT(moved.Data() == originalData, "TC-N-12 moved helper must own the original buffer");
    TEST_ASSERT(moved.Capacity() == 1024, "TC-N-12 moved helper must keep capacity");
    TEST_ASSERT(source.Data() == nullptr, "TC-N-12 source buffer must be cleared");
    TEST_ASSERT(source.Capacity() == 0, "TC-N-12 source capacity must be cleared");
}

void Test_TC_N_13_ScopedHeapBufferMoveAssignmentTransfersOwnership()
{
    UsbTransferHelpers::ScopedHeapBuffer source;
    UsbTransferHelpers::ScopedHeapBuffer destination;

    // Given: Both source and destination own independent allocations.
    // When: Destination takes ownership via move assignment.
    // Then: Destination owns the source buffer and the source becomes empty.
    TEST_ASSERT(source.Allocate(2048), "TC-N-13 source allocation should succeed");
    TEST_ASSERT(destination.Allocate(512), "TC-N-13 destination allocation should succeed");
    BYTE* sourceData = source.Data();

    destination = std::move(source);

    TEST_ASSERT(destination.Data() == sourceData, "TC-N-13 destination must own the source buffer");
    TEST_ASSERT(destination.Capacity() == 2048, "TC-N-13 destination must keep source capacity");
    TEST_ASSERT(source.Data() == nullptr, "TC-N-13 source buffer must be cleared");
    TEST_ASSERT(source.Capacity() == 0, "TC-N-13 source capacity must be cleared");
}

void Test_TC_B_08_ScopedHeapBufferMoveConstructorHandlesEmptySource()
{
    UsbTransferHelpers::ScopedHeapBuffer source;

    // Given: An empty scratch buffer has no allocation.
    // When: It is move-constructed into another helper.
    // Then: Both objects remain empty and valid.
    UsbTransferHelpers::ScopedHeapBuffer moved(std::move(source));

    TEST_ASSERT(moved.Data() == nullptr, "TC-B-08 moved helper must remain empty");
    TEST_ASSERT(moved.Capacity() == 0, "TC-B-08 moved helper capacity must remain zero");
    TEST_ASSERT(source.Data() == nullptr, "TC-B-08 source buffer must remain empty");
    TEST_ASSERT(source.Capacity() == 0, "TC-B-08 source capacity must remain zero");
}

void Test_TC_B_09_ScopedHeapBufferMoveAssignmentHandlesEmptySource()
{
    UsbTransferHelpers::ScopedHeapBuffer source;
    UsbTransferHelpers::ScopedHeapBuffer destination;

    // Given: Destination owns an allocation and source is empty.
    // When: Destination takes ownership from the empty source.
    // Then: Destination releases its old buffer and ends in the empty state.
    TEST_ASSERT(destination.Allocate(256), "TC-B-09 destination allocation should succeed");

    destination = std::move(source);

    TEST_ASSERT(destination.Data() == nullptr, "TC-B-09 destination buffer must be cleared");
    TEST_ASSERT(destination.Capacity() == 0, "TC-B-09 destination capacity must be cleared");
    TEST_ASSERT(source.Data() == nullptr, "TC-B-09 source buffer must remain empty");
    TEST_ASSERT(source.Capacity() == 0, "TC-B-09 source capacity must remain zero");
}

void Test_TC_N_14_ScopedHeapBufferUsesCrtMallocBackend()
{
    // Given: Field verification succeeded only with the CRT malloc/free-backed
    // scratch buffer used in the comparison build.
    // When: The helper exposes the active allocator backend contract.
    // Then: The contract must remain CRT malloc/free.
    TEST_ASSERT(
        UsbTransferHelpers::GetScopedHeapBufferBackend() == UsbTransferHelpers::HeapAllocationBackend::CrtMalloc,
        "TC-N-14 ScopedHeapBuffer backend must stay on CRT malloc/free");
}

void Test_TC_B_10_ScopedHeapBufferBackendContractSurvivesReuse()
{
    UsbTransferHelpers::ScopedHeapBuffer buffer;

    // Given: The scratch buffer has already gone through one allocate/reset cycle.
    // When: The allocator backend contract is queried after reuse-related state changes.
    // Then: The helper must still report the CRT malloc/free backend.
    TEST_ASSERT(buffer.Allocate(64), "TC-B-10 initial allocation should succeed");
    buffer.Reset();

    TEST_ASSERT(
        UsbTransferHelpers::GetScopedHeapBufferBackend() == UsbTransferHelpers::HeapAllocationBackend::CrtMalloc,
        "TC-B-10 backend contract must remain CRT malloc/free after reset");
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
    RUN_TEST(Test_TC_N_10_ScopedHeapBufferAllocatesZeroedMaxTransfer);
    RUN_TEST(Test_TC_N_11_ScopedHeapBufferAllocatesZeroedMinimumPositiveSize);
    RUN_TEST(Test_TC_B_06_ScopedHeapBufferRejectsZeroSize);
    RUN_TEST(Test_TC_B_07_ScopedHeapBufferZeroSizeClearsPreviousAllocation);
    RUN_TEST(Test_TC_N_12_ScopedHeapBufferMoveConstructorTransfersOwnership);
    RUN_TEST(Test_TC_N_13_ScopedHeapBufferMoveAssignmentTransfersOwnership);
    RUN_TEST(Test_TC_B_08_ScopedHeapBufferMoveConstructorHandlesEmptySource);
    RUN_TEST(Test_TC_B_09_ScopedHeapBufferMoveAssignmentHandlesEmptySource);
    RUN_TEST(Test_TC_N_14_ScopedHeapBufferUsesCrtMallocBackend);
    RUN_TEST(Test_TC_B_10_ScopedHeapBufferBackendContractSurvivesReuse);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount > 0 ? 1 : 0;
}
