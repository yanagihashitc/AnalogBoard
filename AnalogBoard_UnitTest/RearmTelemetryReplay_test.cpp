#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "../AnalogBoard_TestApp/AcquisitionCompletionLogic.h"
#include "../AnalogBoard_TestApp/RearmTelemetry.h"

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

struct CompletionReplay
{
    AcquisitionCompletionLogic::Ep4CompletionState completionState;
    RearmTelemetry::Buffer<8> telemetry;

    bool Observe(
        std::uint64_t timestampMs,
        std::uint32_t waveWrCnt,
        std::uint32_t waveRdCnt,
        int ddrWrEnd,
        int ddrRdEnd,
        std::size_t savedBytes)
    {
        AcquisitionCompletionLogic::Ep4CompletionSnapshot snapshot;
        snapshot.waveWrCnt = waveWrCnt;
        snapshot.waveRdCnt = waveRdCnt;
        snapshot.ddrWrEnd = ddrWrEnd;
        snapshot.ddrRdEnd = ddrRdEnd;
        const AcquisitionCompletionLogic::Ep4CompletionDecision decision =
            AcquisitionCompletionLogic::ObserveEp4Completion(
                &completionState,
                snapshot,
                savedBytes);
        if (decision.enteredDdrRdEnd)
        {
            telemetry.MarkDdrRdEndConfirmed(timestampMs);
        }
        if (decision.acquisitionComplete)
        {
            telemetry.MarkHostDrainComplete(timestampMs);
        }
        return decision.acquisitionComplete;
    }
};

void Test_RP_N_01_RecordedEp4Replay_ProducesSeparatedDurations()
{
    // Given: A replay containing startup stale status, partial drain, final RD_END, and a later trigger.
    CompletionReplay replay;
    TEST_ASSERT(replay.telemetry.StartCycle(100, true), "RP-N-01 first trigger must start a cycle");

    // When: The recorded-style EP4 sequence reaches true completion and host cleanup finishes.
    TEST_ASSERT(!replay.Observe(110, 0, 0, 1, 1, 0), "RP-N-01 startup stale status must not complete");
    TEST_ASSERT(!replay.Observe(120, 16000, 8000, 0, 0, 8192), "RP-N-01 partial data must remain active");
    TEST_ASSERT(!replay.Observe(180, 16000, 16000, 1, 0, 16032), "RP-N-01 WR_END without RD_END must not complete");
    TEST_ASSERT(replay.Observe(200, 16000, 16000, 1, 1, 16032), "RP-N-01 RD_END with no unread bytes must complete");
    TEST_ASSERT(replay.telemetry.MarkPublishCleanupComplete(220), "RP-N-01 publish/cleanup must complete");
    TEST_ASSERT(replay.telemetry.MarkHostReady(230), "RP-N-01 host must become ready");
    TEST_ASSERT(replay.telemetry.StartCycle(500, true), "RP-N-01 next trigger must start cycle 2");

    // Then: Replay-derived rearm excludes the external-trigger wait.
    std::uint64_t rearmMs = 0;
    std::uint64_t externalWaitMs = 0;
    TEST_ASSERT(RearmTelemetry::TryGetRearmMs(replay.telemetry.At(0), &rearmMs), "RP-N-01 rearm must be available");
    TEST_ASSERT(rearmMs == 30, "RP-N-01 rearm must be 30 ms");
    TEST_ASSERT(RearmTelemetry::TryGetExternalWaitMs(replay.telemetry.At(0), &externalWaitMs), "RP-N-01 external wait must be available");
    TEST_ASSERT(externalWaitMs == 270, "RP-N-01 external wait must be 270 ms");
}

void Test_RP_A_01_MissingRdEndFault_DoesNotFabricateRearm()
{
    // Given: A fault replay that reaches WR_END and drains host bytes but never asserts RD_END.
    CompletionReplay replay;
    TEST_ASSERT(replay.telemetry.StartCycle(1000, true), "RP-A-01 trigger must start");

    // When: Repeated status snapshots remain in RD_WAIT.
    TEST_ASSERT(!replay.Observe(1100, 32000, 32000, 1, 0, 32032), "RP-A-01 first RD_WAIT must not complete");
    TEST_ASSERT(!replay.Observe(1200, 32000, 32000, 1, 0, 32032), "RP-A-01 repeated RD_WAIT must not complete");

    // Then: No drain or rearm duration is invented for the faulted cycle.
    std::uint64_t rearmMs = 0;
    TEST_ASSERT(!replay.telemetry.At(0).hasDdrRdEndConfirmed, "RP-A-01 RD_END marker must remain absent");
    TEST_ASSERT(replay.telemetry.MarkPublishCleanupComplete(1300),
        "RP-A-01 independent publish boundary must remain observable");
    TEST_ASSERT(!replay.telemetry.MarkHostReady(1400), "RP-A-01 host-ready marker without RD_END/drain must fail");
    TEST_ASSERT(!RearmTelemetry::TryGetRearmMs(replay.telemetry.At(0), &rearmMs), "RP-A-01 rearm must be unavailable");
}

void Test_RP_A_02_TransportFaultAndRecovery_KeepOnlyValidBoundaries()
{
    // Given: A cycle where a simulated EP4 read failure produces no completion snapshot.
    CompletionReplay replay;
    TEST_ASSERT(replay.telemetry.StartCycle(2000, true), "RP-A-02 trigger must start");

    // When: Invalid early markers are attempted, followed by a valid recovered completion sequence.
    TEST_ASSERT(!replay.telemetry.MarkHostReady(2010), "RP-A-02 early host-ready must fail");
    TEST_ASSERT(!replay.Observe(2050, 64000, 32000, 0, 0, 32000), "RP-A-02 active replay must not complete");
    TEST_ASSERT(replay.Observe(2100, 64000, 64000, 1, 1, 64032), "RP-A-02 recovered RD_END must complete");
    TEST_ASSERT(replay.telemetry.MarkPublishCleanupComplete(2110), "RP-A-02 recovered publish must complete");
    TEST_ASSERT(replay.telemetry.MarkHostReady(2120), "RP-A-02 recovered host-ready must complete");

    // Then: The summary contains one valid sample and no fault-time timestamp.
    const RearmTelemetry::DurationSummary summary = replay.telemetry.SummarizeRearm();
    TEST_ASSERT(summary.sampleCount == 1, "RP-A-02 exactly one recovered sample must be summarized");
    TEST_ASSERT(summary.p99Ms == 20, "RP-A-02 recovered p99 must be 20 ms");
}

void Test_RP_N_02_RdEndBeforeHostDrain_PreservesFirstBoundary()
{
    // Given: An active replay where DDR_RD_END appears while host bytes remain unread.
    CompletionReplay replay;
    TEST_ASSERT(replay.telemetry.StartCycle(100, true), "RP-N-02 trigger must start");
    TEST_ASSERT(!replay.Observe(110, 16000, 0, 0, 0, 0), "RP-N-02 active cycle must not complete");

    // When: RD_END is first observed at 150 ms and host drain completes at 200 ms.
    TEST_ASSERT(!replay.Observe(150, 16000, 16000, 1, 1, 0),
        "RP-N-02 RD_END with unread bytes must not complete");
    TEST_ASSERT(replay.Observe(200, 16000, 16000, 1, 1, 16032),
        "RP-N-02 drained host bytes must complete");
    TEST_ASSERT(replay.telemetry.MarkPublishCleanupComplete(220), "RP-N-02 publish must complete");
    TEST_ASSERT(replay.telemetry.MarkHostReady(230), "RP-N-02 host must become ready");

    // Then: Rearm starts at the first active RD_END observation, not at host-drain completion.
    const RearmTelemetry::DurationValue rearm = RearmTelemetry::ReadRearmMs(replay.telemetry.At(0));
    TEST_ASSERT(rearm.available, "RP-N-02 rearm must be available");
    TEST_ASSERT(rearm.milliseconds == 80, "RP-N-02 rearm must include the remaining host drain");
}

int main()
{
    std::printf("=== RearmTelemetry Replay/Fault Tests ===\n\n");

    RUN_TEST(Test_RP_N_01_RecordedEp4Replay_ProducesSeparatedDurations);
    RUN_TEST(Test_RP_N_02_RdEndBeforeHostDrain_PreservesFirstBoundary);
    RUN_TEST(Test_RP_A_01_MissingRdEndFault_DoesNotFabricateRearm);
    RUN_TEST(Test_RP_A_02_TransportFaultAndRecovery_KeepOnlyValidBoundaries);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
