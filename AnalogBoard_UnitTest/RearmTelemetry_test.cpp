#include <cstdint>
#include <cstdio>
#include <limits>

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

void Test_RT_N_01_TwoCycles_SeparateRearmFromExternalWait()
{
    // Given: A fixed telemetry buffer waiting for two externally triggered cycles.
    RearmTelemetry::Buffer<4> telemetry;

    // When: Cycle 1 crosses every boundary and cycle 2 begins later.
    TEST_ASSERT(telemetry.StartCycle(100, true), "RT-N-01 cycle 1 must start");
    TEST_ASSERT(telemetry.MarkDdrRdEndConfirmed(200), "RT-N-01 RD_END must be recorded");
    TEST_ASSERT(telemetry.MarkHostDrainComplete(210), "RT-N-01 host drain must be recorded");
    TEST_ASSERT(telemetry.MarkPublishCleanupComplete(220), "RT-N-01 publish/cleanup must be recorded");
    TEST_ASSERT(telemetry.MarkHostReady(230), "RT-N-01 host ready must be recorded");
    TEST_ASSERT(telemetry.StartCycle(500, true), "RT-N-01 cycle 2 must start");

    // Then: IDs are consecutive and the two durations do not include each other.
    const RearmTelemetry::CycleRecord& first = telemetry.At(0);
    TEST_ASSERT(first.cycleId == 1, "RT-N-01 first cycle ID must be 1");
    TEST_ASSERT(telemetry.At(1).cycleId == 2, "RT-N-01 second cycle ID must be 2");
    std::uint64_t rearmMs = 0;
    std::uint64_t externalWaitMs = 0;
    TEST_ASSERT(RearmTelemetry::TryGetRearmMs(first, &rearmMs), "RT-N-01 rearm must be available");
    TEST_ASSERT(rearmMs == 30, "RT-N-01 rearm must be hostReady - confirmed RD_END");
    TEST_ASSERT(RearmTelemetry::TryGetExternalWaitMs(first, &externalWaitMs), "RT-N-01 external wait must be available");
    TEST_ASSERT(externalWaitMs == 270, "RT-N-01 external wait must be nextTrigger - hostReady");
}

void Test_RT_N_02_ZeroAndMaximumTicks_AreValidBoundaries()
{
    // Given: A cycle that starts at tick zero and ends near the uint64 maximum.
    RearmTelemetry::Buffer<1> telemetry;

    // When: Valid monotonic boundary values are recorded.
    TEST_ASSERT(telemetry.StartCycle(0, true), "RT-N-02 tick zero must be accepted");
    const std::uint64_t maxTick = (std::numeric_limits<std::uint64_t>::max)();
    TEST_ASSERT(telemetry.MarkDdrRdEndConfirmed(maxTick - 1), "RT-N-02 max-1 RD_END must be accepted");
    TEST_ASSERT(telemetry.MarkHostDrainComplete(maxTick), "RT-N-02 max host drain must be accepted");
    TEST_ASSERT(telemetry.MarkPublishCleanupComplete(maxTick), "RT-N-02 max publish must be accepted");
    TEST_ASSERT(telemetry.MarkHostReady(maxTick), "RT-N-02 max host ready must be accepted");

    // Then: The valid one-millisecond duration is calculated without a sentinel collision.
    std::uint64_t rearmMs = 0;
    TEST_ASSERT(RearmTelemetry::TryGetRearmMs(telemetry.At(0), &rearmMs), "RT-N-02 rearm must be available");
    TEST_ASSERT(rearmMs == 1, "RT-N-02 max-boundary rearm must be 1");
}

void Test_RT_N_03_DurationValue_CarriesAvailabilityAndMillisecondsTogether()
{
    // Given: A completed telemetry record whose re-arm duration is non-zero.
    RearmTelemetry::Buffer<1> telemetry;
    TEST_ASSERT(telemetry.StartCycle(100, true), "RT-N-03 cycle must start");
    TEST_ASSERT(telemetry.MarkDdrRdEndConfirmed(150), "RT-N-03 RD_END must be recorded");
    TEST_ASSERT(telemetry.MarkHostDrainComplete(170), "RT-N-03 host drain must be recorded");
    TEST_ASSERT(telemetry.MarkPublishCleanupComplete(180), "RT-N-03 publish must be recorded");
    TEST_ASSERT(telemetry.MarkHostReady(230), "RT-N-03 host-ready must be recorded");

    // When: The caller obtains one value object instead of an out-parameter expression.
    const RearmTelemetry::DurationValue rearm = RearmTelemetry::ReadRearmMs(telemetry.At(0));

    // Then: Availability and the calculated value cannot be evaluated out of order.
    TEST_ASSERT(rearm.available, "RT-N-03 rearm value must be available");
    TEST_ASSERT(rearm.milliseconds == 80, "RT-N-03 rearm must start at first confirmed RD_END");
}

void Test_RT_N_04_PublishCanPrecedeRdEnd_ButHostReadyRequiresAllBoundaries()
{
    // Given: Legacy R7 closes the output files before its final completion wait.
    RearmTelemetry::Buffer<1> telemetry;
    TEST_ASSERT(telemetry.StartCycle(100, true), "RT-N-04 cycle must start");

    // When: Publish completes before RD_END and host-drain completion.
    TEST_ASSERT(telemetry.MarkPublishCleanupComplete(110),
        "RT-N-04 actual publish boundary must be recordable before RD_END");
    TEST_ASSERT(telemetry.MarkDdrRdEndConfirmed(120), "RT-N-04 RD_END must be recorded");
    TEST_ASSERT(!telemetry.MarkHostReady(125), "RT-N-04 host-ready must wait for host drain");
    TEST_ASSERT(telemetry.MarkHostDrainComplete(130), "RT-N-04 host drain must be recorded");

    // Then: Host-ready becomes valid only after all independent boundaries exist.
    TEST_ASSERT(telemetry.MarkHostReady(140), "RT-N-04 host-ready must accept the completed boundary set");
}

void Test_RT_A_01_DuplicateMarkers_AreRejectedWithoutOverwrite()
{
    // Given: A cycle with all first boundary timestamps recorded.
    RearmTelemetry::Buffer<2> telemetry;
    TEST_ASSERT(telemetry.StartCycle(10, true), "RT-A-01 cycle must start");
    TEST_ASSERT(telemetry.MarkDdrRdEndConfirmed(20), "RT-A-01 first RD_END must succeed");
    TEST_ASSERT(telemetry.MarkHostDrainComplete(22), "RT-A-01 first host drain must succeed");
    TEST_ASSERT(telemetry.MarkPublishCleanupComplete(25), "RT-A-01 first publish must succeed");
    TEST_ASSERT(telemetry.MarkHostReady(30), "RT-A-01 first host ready must succeed");

    // When: Duplicate markers are attempted.
    const bool duplicateRdEnd = telemetry.MarkDdrRdEndConfirmed(21);
    const bool duplicateHostDrain = telemetry.MarkHostDrainComplete(23);
    const bool duplicatePublish = telemetry.MarkPublishCleanupComplete(26);
    const bool duplicateReady = telemetry.MarkHostReady(31);

    // Then: Every duplicate is rejected and the original timestamps remain authoritative.
    TEST_ASSERT(!duplicateRdEnd, "RT-A-01 duplicate RD_END must fail");
    TEST_ASSERT(!duplicateHostDrain, "RT-A-01 duplicate host drain must fail");
    TEST_ASSERT(!duplicatePublish, "RT-A-01 duplicate publish must fail");
    TEST_ASSERT(!duplicateReady, "RT-A-01 duplicate host ready must fail");
    TEST_ASSERT(telemetry.At(0).ddrRdEndConfirmedMs == 20, "RT-A-01 first RD_END timestamp must remain");
    TEST_ASSERT(telemetry.At(0).hostDrainCompleteMs == 22, "RT-A-01 first host-drain timestamp must remain");
    TEST_ASSERT(telemetry.At(0).publishCleanupCompleteMs == 25, "RT-A-01 first publish timestamp must remain");
    TEST_ASSERT(telemetry.At(0).hostReadyMs == 30, "RT-A-01 first host-ready timestamp must remain");
}

void Test_RT_A_02_OutOfOrderMarkers_AreRejected()
{
    // Given: A cycle detected at tick 100.
    RearmTelemetry::Buffer<1> telemetry;
    TEST_ASSERT(telemetry.StartCycle(100, true), "RT-A-02 cycle must start");

    // When: Each dependent marker is earlier than its required boundary.
    const bool earlyRdEnd = telemetry.MarkDdrRdEndConfirmed(99);
    const bool earlyPublish = telemetry.MarkPublishCleanupComplete(99);
    TEST_ASSERT(telemetry.MarkPublishCleanupComplete(105), "RT-A-02 valid independent publish must succeed");
    TEST_ASSERT(telemetry.MarkDdrRdEndConfirmed(110), "RT-A-02 valid RD_END must succeed");
    const bool earlyHostDrain = telemetry.MarkHostDrainComplete(109);
    TEST_ASSERT(telemetry.MarkHostDrainComplete(115), "RT-A-02 valid host drain must succeed");
    const bool earlyReady = telemetry.MarkHostReady(114);
    TEST_ASSERT(telemetry.MarkHostReady(120), "RT-A-02 valid host ready must succeed");

    // Then: The minus-one boundary violations are rejected.
    TEST_ASSERT(!earlyRdEnd, "RT-A-02 RD_END before trigger must fail");
    TEST_ASSERT(!earlyHostDrain, "RT-A-02 host drain before RD_END must fail");
    TEST_ASSERT(!earlyPublish, "RT-A-02 publish before trigger must fail");
    TEST_ASSERT(!earlyReady, "RT-A-02 host ready before host drain must fail");
}

void Test_RT_A_03_MissingMarkers_DoNotProduceDurations()
{
    // Given: One cycle has only a trigger and another has no next external trigger.
    RearmTelemetry::Buffer<2> telemetry;
    TEST_ASSERT(telemetry.StartCycle(10, true), "RT-A-03 cycle 1 must start");

    // When: Durations are queried before their required boundaries exist.
    std::uint64_t durationMs = 999;
    const bool missingRearm = RearmTelemetry::TryGetRearmMs(telemetry.At(0), &durationMs);
    const bool missingExternalWait = RearmTelemetry::TryGetExternalWaitMs(telemetry.At(0), &durationMs);

    // Then: Missing durations are reported as unavailable.
    TEST_ASSERT(!missingRearm, "RT-A-03 rearm without RD_END/ready must be unavailable");
    TEST_ASSERT(!missingExternalWait, "RT-A-03 external wait without ready/next trigger must be unavailable");
}

void Test_RT_B_01_CapacityPlusOne_IncrementsDropCountWithoutStopping()
{
    // Given: A buffer with room for exactly one cycle.
    RearmTelemetry::Buffer<1> telemetry;
    TEST_ASSERT(telemetry.StartCycle(10, true), "RT-B-01 first cycle must fit");
    TEST_ASSERT(telemetry.MarkDdrRdEndConfirmed(20), "RT-B-01 RD_END must be recorded");
    TEST_ASSERT(telemetry.MarkHostDrainComplete(22), "RT-B-01 host drain must be recorded");
    TEST_ASSERT(telemetry.MarkPublishCleanupComplete(25), "RT-B-01 publish must be recorded");
    TEST_ASSERT(telemetry.MarkHostReady(30), "RT-B-01 host ready must be recorded");

    // When: One more external trigger arrives than the fixed capacity permits.
    const bool secondStored = telemetry.StartCycle(50, true);

    // Then: The new cycle is dropped, while the prior external-wait boundary and drop count survive.
    TEST_ASSERT(!secondStored, "RT-B-01 capacity+1 cycle must not be stored");
    TEST_ASSERT(telemetry.Count() == 1, "RT-B-01 stored count must remain 1");
    TEST_ASSERT(telemetry.DroppedCount() == 1, "RT-B-01 drop count must become 1");
    TEST_ASSERT(telemetry.At(0).nextExternalTriggerDetectedMs == 50, "RT-B-01 prior row must capture next trigger");

    // Given/When/Then: A zero-capacity boundary also drops without accessing storage.
    RearmTelemetry::Buffer<0> zeroCapacity;
    TEST_ASSERT(!zeroCapacity.StartCycle(0, true), "RT-B-01 zero-capacity start must be dropped");
    TEST_ASSERT(zeroCapacity.Count() == 0, "RT-B-01 zero-capacity count must be zero");
    TEST_ASSERT(zeroCapacity.DroppedCount() == 1, "RT-B-01 zero-capacity drop count must be one");
}

void Test_RT_B_02_Summary_HandlesZeroOneAndNearestRankP99()
{
    // Given: An empty buffer, a one-sample buffer, and 100 ordered rearm samples.
    RearmTelemetry::Buffer<1> empty;
    RearmTelemetry::Buffer<1> single;
    TEST_ASSERT(single.StartCycle(0, true), "RT-B-02 single cycle must start");
    TEST_ASSERT(single.MarkDdrRdEndConfirmed(10), "RT-B-02 single RD_END must succeed");
    TEST_ASSERT(single.MarkHostDrainComplete(12), "RT-B-02 single host drain must succeed");
    TEST_ASSERT(single.MarkPublishCleanupComplete(15), "RT-B-02 single publish must succeed");
    TEST_ASSERT(single.MarkHostReady(20), "RT-B-02 single host ready must succeed");

    RearmTelemetry::Buffer<100> many;
    for (std::uint64_t i = 1; i <= 100; ++i)
    {
        TEST_ASSERT(many.StartCycle(i * 1000, true), "RT-B-02 many cycle must start");
        TEST_ASSERT(many.MarkDdrRdEndConfirmed(i * 1000 + 100), "RT-B-02 many RD_END must succeed");
        TEST_ASSERT(many.MarkHostDrainComplete(i * 1000 + 100), "RT-B-02 many host drain must succeed");
        TEST_ASSERT(many.MarkPublishCleanupComplete(i * 1000 + 100 + i), "RT-B-02 many publish must succeed");
        TEST_ASSERT(many.MarkHostReady(i * 1000 + 100 + i), "RT-B-02 many host ready must succeed");
    }

    // When: Rearm summaries are calculated.
    const RearmTelemetry::DurationSummary emptySummary = empty.SummarizeRearm();
    const RearmTelemetry::DurationSummary singleSummary = single.SummarizeRearm();
    const RearmTelemetry::DurationSummary manySummary = many.SummarizeRearm();

    // Then: Zero, minimum, and nearest-rank p99 boundaries are deterministic.
    TEST_ASSERT(emptySummary.sampleCount == 0, "RT-B-02 empty sample count must be zero");
    TEST_ASSERT(singleSummary.sampleCount == 1 && singleSummary.p99Ms == 10, "RT-B-02 one-sample p99 must equal the sample");
    TEST_ASSERT(manySummary.sampleCount == 100, "RT-B-02 many sample count must be 100");
    TEST_ASSERT(manySummary.p50Ms == 50, "RT-B-02 nearest-rank p50 must be 50");
    TEST_ASSERT(manySummary.p95Ms == 95, "RT-B-02 nearest-rank p95 must be 95");
    TEST_ASSERT(manySummary.p99Ms == 99, "RT-B-02 nearest-rank p99 must be 99");
    TEST_ASSERT(manySummary.maxMs == 100, "RT-B-02 maximum must be 100");
}

void Test_RT_A_04_RetroactiveTriggerBeforeHostReady_IsCountedAsDropped()
{
    // Given: One completed external cycle whose host-ready boundary is tick 30.
    RearmTelemetry::Buffer<3> telemetry;
    TEST_ASSERT(telemetry.StartCycle(10, true), "RT-A-04 first cycle must start");
    TEST_ASSERT(telemetry.MarkDdrRdEndConfirmed(20), "RT-A-04 RD_END must be recorded");
    TEST_ASSERT(telemetry.MarkHostDrainComplete(22), "RT-A-04 host drain must be recorded");
    TEST_ASSERT(telemetry.MarkPublishCleanupComplete(25), "RT-A-04 publish must be recorded");
    TEST_ASSERT(telemetry.MarkHostReady(30), "RT-A-04 host-ready must be recorded");

    // When: A clock-order violation is observed, followed by a valid next trigger.
    TEST_ASSERT(!telemetry.StartCycle(29, true), "RT-A-04 trigger before host-ready must be rejected");
    TEST_ASSERT(!telemetry.MarkDdrRdEndConfirmed(31), "RT-A-04 rejected cycle must have no active record");
    TEST_ASSERT(telemetry.StartCycle(50, true), "RT-A-04 later valid trigger must start");

    // Then: The anomaly is counted and its cycle ID is not silently reused.
    TEST_ASSERT(telemetry.DroppedCount() == 1, "RT-A-04 rejected trigger must increment dropped count");
    TEST_ASSERT(telemetry.At(1).cycleId == 3, "RT-A-04 rejected trigger must consume cycle ID 2");
    TEST_ASSERT(telemetry.At(0).nextExternalTriggerDetectedMs == 50,
        "RT-A-04 valid next trigger must remain the external-wait boundary");
}

void Test_RT_N_05_ManualThenExternal_PreservesSourceAndExternalWait()
{
    // Given: A manually started cycle reaches host-ready.
    RearmTelemetry::Buffer<2> telemetry;
    TEST_ASSERT(telemetry.StartCycle(100, false), "RT-N-05 manual cycle must start");
    TEST_ASSERT(telemetry.MarkPublishCleanupComplete(150), "RT-N-05 publish must be recorded");
    TEST_ASSERT(telemetry.MarkDdrRdEndConfirmed(160), "RT-N-05 RD_END must be recorded");
    TEST_ASSERT(telemetry.MarkHostDrainComplete(170), "RT-N-05 host drain must be recorded");
    TEST_ASSERT(telemetry.MarkHostReady(180), "RT-N-05 host-ready must be recorded");

    // When: The next cycle is externally triggered.
    TEST_ASSERT(telemetry.StartCycle(250, true), "RT-N-05 external cycle must start");

    // Then: Source labels stay per-cycle and the external wait is still separated.
    TEST_ASSERT(!telemetry.At(0).externalTrigger, "RT-N-05 first source must remain manual");
    TEST_ASSERT(telemetry.At(1).externalTrigger, "RT-N-05 second source must be external");
    const RearmTelemetry::DurationValue wait = RearmTelemetry::ReadExternalWaitMs(telemetry.At(0));
    TEST_ASSERT(wait.available && wait.milliseconds == 70,
        "RT-N-05 manual cycle must preserve the next external-trigger wait");
}

int main()
{
    std::printf("=== RearmTelemetry Unit Tests ===\n\n");

    RUN_TEST(Test_RT_N_01_TwoCycles_SeparateRearmFromExternalWait);
    RUN_TEST(Test_RT_N_02_ZeroAndMaximumTicks_AreValidBoundaries);
    RUN_TEST(Test_RT_N_03_DurationValue_CarriesAvailabilityAndMillisecondsTogether);
    RUN_TEST(Test_RT_N_04_PublishCanPrecedeRdEnd_ButHostReadyRequiresAllBoundaries);
    RUN_TEST(Test_RT_A_01_DuplicateMarkers_AreRejectedWithoutOverwrite);
    RUN_TEST(Test_RT_A_02_OutOfOrderMarkers_AreRejected);
    RUN_TEST(Test_RT_A_03_MissingMarkers_DoNotProduceDurations);
    RUN_TEST(Test_RT_A_04_RetroactiveTriggerBeforeHostReady_IsCountedAsDropped);
    RUN_TEST(Test_RT_B_01_CapacityPlusOne_IncrementsDropCountWithoutStopping);
    RUN_TEST(Test_RT_B_02_Summary_HandlesZeroOneAndNearestRankP99);
    RUN_TEST(Test_RT_N_05_ManualThenExternal_PreservesSourceAndExternalWait);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
