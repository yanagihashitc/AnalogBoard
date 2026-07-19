#include <cstdio>

#include "../AnalogBoard_TestApp/AcquisitionShutdownCoordinator.h"

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

void Test_ASC_N_01_IdleCloseCanProceedImmediately()
{
    // Given: no acquisition thread has been reserved or started.
    AcquisitionShutdownCoordinator coordinator;

    // When: the application window requests close.
    const auto decision = coordinator.RequestClose();

    // Then: no finalization is pending, so close may proceed immediately.
    TEST_ASSERT(
        decision == AcquisitionShutdownCoordinator::CloseDecision::kCloseNow,
        "ASC-N-01 idle close should proceed immediately");
}

void Test_ASC_N_02_FirstCloseRequestsRunningThreadStop()
{
    // Given: an acquisition thread start has been reserved.
    AcquisitionShutdownCoordinator coordinator;
    TEST_ASSERT(coordinator.TryBeginThread(), "ASC-N-02 thread reservation should succeed");

    // When: the first close request arrives while acquisition owns finalization.
    const auto decision = coordinator.RequestClose();

    // Then: the window stays alive and the worker must observe cancellation.
    TEST_ASSERT(
        decision == AcquisitionShutdownCoordinator::CloseDecision::kRequestStop,
        "ASC-N-02 first close should request stop");
    TEST_ASSERT(
        !coordinator.ShouldRunThread(),
        "ASC-N-02 stop request should prevent acquisition from continuing");
}

void Test_ASC_N_03_RepeatedCloseWaitsForFinalization()
{
    // Given: close has already requested the running thread to stop.
    AcquisitionShutdownCoordinator coordinator;
    TEST_ASSERT(coordinator.TryBeginThread(), "ASC-N-03 thread reservation should succeed");
    TEST_ASSERT(
        coordinator.RequestClose() == AcquisitionShutdownCoordinator::CloseDecision::kRequestStop,
        "ASC-N-03 first close should request stop");

    // When: another close arrives before telemetry and log finalization completes.
    const auto decision = coordinator.RequestClose();

    // Then: destruction remains deferred.
    TEST_ASSERT(
        decision == AcquisitionShutdownCoordinator::CloseDecision::kWaitForFinalization,
        "ASC-N-03 repeated close should wait for finalization");
}

void Test_ASC_N_04_CloseRequestedFinalizationNeedsAutomaticClose()
{
    // Given: the application is waiting for a close-requested thread shutdown.
    AcquisitionShutdownCoordinator coordinator;
    TEST_ASSERT(coordinator.TryBeginThread(), "ASC-N-04 thread reservation should succeed");
    TEST_ASSERT(
        coordinator.RequestClose() == AcquisitionShutdownCoordinator::CloseDecision::kRequestStop,
        "ASC-N-04 close should request stop");

    // When: the worker reports that CSV, summary, and log flush are complete.
    const bool shouldPostClose = coordinator.ThreadFinalized();

    // Then: the main window needs exactly one asynchronous close request.
    TEST_ASSERT(shouldPostClose, "ASC-N-04 close-requested finalization should post close");
    TEST_ASSERT(
        !coordinator.TryBeginThread(),
        "ASC-N-04 a new acquisition must not start while automatic close is pending");
    TEST_ASSERT(
        coordinator.RequestClose() == AcquisitionShutdownCoordinator::CloseDecision::kCloseNow,
        "ASC-N-04 post-finalization close should proceed immediately");
}

void Test_ASC_N_05_NaturalFinalizationDoesNotCloseApplication()
{
    // Given: acquisition is running without an application close request.
    AcquisitionShutdownCoordinator coordinator;
    TEST_ASSERT(coordinator.TryBeginThread(), "ASC-N-05 thread reservation should succeed");

    // When: the acquisition thread finishes naturally.
    const bool shouldPostClose = coordinator.ThreadFinalized();

    // Then: the application stays open for the operator.
    TEST_ASSERT(!shouldPostClose, "ASC-N-05 natural finalization should not post close");
    TEST_ASSERT(
        coordinator.RequestClose() == AcquisitionShutdownCoordinator::CloseDecision::kCloseNow,
        "ASC-N-05 close after natural finalization should proceed");
}

void Test_ASC_A_01_ThreadStartFailureReturnsToIdle()
{
    // Given: a thread start is reserved but native thread creation will fail.
    AcquisitionShutdownCoordinator coordinator;
    TEST_ASSERT(coordinator.TryBeginThread(), "ASC-A-01 thread reservation should succeed");

    // When: the caller reports the creation failure.
    coordinator.ThreadStartFailed();

    // Then: no nonexistent worker blocks application close.
    TEST_ASSERT(
        coordinator.RequestClose() == AcquisitionShutdownCoordinator::CloseDecision::kCloseNow,
        "ASC-A-01 failed thread start should return to idle");
}

void Test_ASC_A_02_ConcurrentStartReservationsAreRefused()
{
    // Given: one acquisition thread already owns the running lifetime.
    AcquisitionShutdownCoordinator coordinator;
    TEST_ASSERT(coordinator.TryBeginThread(), "ASC-A-02 first thread reservation should succeed");

    // When/Then: a second start cannot reserve the same lifetime.
    TEST_ASSERT(
        !coordinator.TryBeginThread(),
        "ASC-A-02 a second reservation must be refused while running");
}

void Test_ASC_A_03_StartIsRefusedAfterCloseRequestsStop()
{
    // Given: application close has requested the current worker to stop.
    AcquisitionShutdownCoordinator coordinator;
    TEST_ASSERT(coordinator.TryBeginThread(), "ASC-A-03 first thread reservation should succeed");
    TEST_ASSERT(
        coordinator.RequestClose() == AcquisitionShutdownCoordinator::CloseDecision::kRequestStop,
        "ASC-A-03 close should request stop");

    // When/Then: another worker cannot start during deferred finalization.
    TEST_ASSERT(
        !coordinator.TryBeginThread(),
        "ASC-A-03 a new reservation must be refused after stop is requested");
}

void Test_ASC_A_04_ClosePendingFinalizationIsIdempotentAndLatched()
{
    // Given: the worker has transitioned a requested close to close-pending.
    AcquisitionShutdownCoordinator coordinator;
    TEST_ASSERT(coordinator.TryBeginThread(), "ASC-A-04 thread reservation should succeed");
    TEST_ASSERT(
        coordinator.RequestClose() == AcquisitionShutdownCoordinator::CloseDecision::kRequestStop,
        "ASC-A-04 close should request stop");
    TEST_ASSERT(
        coordinator.ThreadFinalized(),
        "ASC-A-04 first finalization should request exactly one automatic close");

    // When: cleanup accidentally reports finalization again before WM_CLOSE is consumed.
    const bool shouldPostDuplicateClose = coordinator.ThreadFinalized();

    // Then: no duplicate close is posted and close-pending remains latched.
    TEST_ASSERT(
        !shouldPostDuplicateClose,
        "ASC-A-04 repeated finalization must not post a duplicate close");
    TEST_ASSERT(
        !coordinator.TryBeginThread(),
        "ASC-A-04 repeated finalization must keep close-pending latched");
    TEST_ASSERT(
        coordinator.RequestClose() == AcquisitionShutdownCoordinator::CloseDecision::kCloseNow,
        "ASC-A-04 the posted close should consume the close-pending latch");
}

void Test_ASC_B_01_NaturalFinalizationAllowsNextStart()
{
    // Given: a previous acquisition thread has finished naturally.
    AcquisitionShutdownCoordinator coordinator;
    TEST_ASSERT(coordinator.TryBeginThread(), "ASC-B-01 first thread reservation should succeed");
    TEST_ASSERT(!coordinator.ThreadFinalized(), "ASC-B-01 natural finish should not post close");

    // When: the operator starts a later acquisition session.
    const bool beganNextThread = coordinator.TryBeginThread();

    // Then: the coordinator accepts the new running lifetime.
    TEST_ASSERT(beganNextThread, "ASC-B-01 next thread reservation should succeed");
    TEST_ASSERT(coordinator.ShouldRunThread(), "ASC-B-01 next thread should be allowed to run");
}

void Test_ASC_B_02_NaturalFinalizationIsIdempotent()
{
    // Given: a worker finishes without an application close request.
    AcquisitionShutdownCoordinator coordinator;
    TEST_ASSERT(coordinator.TryBeginThread(), "ASC-B-02 thread reservation should succeed");
    TEST_ASSERT(!coordinator.ThreadFinalized(), "ASC-B-02 natural finish should not post close");

    // When/Then: a duplicate report stays harmless and leaves the coordinator idle.
    TEST_ASSERT(
        !coordinator.ThreadFinalized(),
        "ASC-B-02 repeated natural finalization should remain a no-op");
    TEST_ASSERT(
        coordinator.TryBeginThread(),
        "ASC-B-02 repeated natural finalization should still allow the next session");
}

int main()
{
    std::printf("=== AcquisitionShutdownCoordinator Unit Tests ===\n\n");

    RUN_TEST(Test_ASC_N_01_IdleCloseCanProceedImmediately);
    RUN_TEST(Test_ASC_N_02_FirstCloseRequestsRunningThreadStop);
    RUN_TEST(Test_ASC_N_03_RepeatedCloseWaitsForFinalization);
    RUN_TEST(Test_ASC_N_04_CloseRequestedFinalizationNeedsAutomaticClose);
    RUN_TEST(Test_ASC_N_05_NaturalFinalizationDoesNotCloseApplication);
    RUN_TEST(Test_ASC_A_01_ThreadStartFailureReturnsToIdle);
    RUN_TEST(Test_ASC_A_02_ConcurrentStartReservationsAreRefused);
    RUN_TEST(Test_ASC_A_03_StartIsRefusedAfterCloseRequestsStop);
    RUN_TEST(Test_ASC_A_04_ClosePendingFinalizationIsIdempotentAndLatched);
    RUN_TEST(Test_ASC_B_01_NaturalFinalizationAllowsNextStart);
    RUN_TEST(Test_ASC_B_02_NaturalFinalizationIsIdempotent);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
