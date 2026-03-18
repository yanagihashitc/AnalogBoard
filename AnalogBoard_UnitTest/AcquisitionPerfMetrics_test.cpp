#include <windows.h>

#include "TestFramework.h"
#include "../AnalogBoard_TestApp/AcquisitionPerfMetrics.h"

void Test_TC_N_01_RecordEp6Transfer_AggregatesCountTimeAndBytes()
{
    // Given: A fresh cycle metrics collector with two EP6 transfers.
    AcquisitionPerfMetrics::CycleMetrics metrics;

    // When: EP6 transfers are recorded.
    metrics.RecordEp6Transfer(12, 4096, false);
    metrics.RecordEp6Transfer(20, 8192, false);

    // Then: Count, totals, max, and average are aggregated.
    TEST_ASSERT(metrics.ep6.callCount == 2, "TC-N-01 ep6 callCount must be 2");
    TEST_ASSERT(metrics.ep6.totalElapsedMs == 32, "TC-N-01 ep6 totalElapsedMs must be 32");
    TEST_ASSERT(metrics.ep6.maxElapsedMs == 20, "TC-N-01 ep6 maxElapsedMs must be 20");
    TEST_ASSERT(metrics.ep6.totalBytes == 12288, "TC-N-01 ep6 totalBytes must be 12288");
    TEST_ASSERT(metrics.GetEp6AverageElapsedMs() == 16, "TC-N-01 ep6 avg must be 16");
}

void Test_TC_N_02_RecordDdrStatus_TracksLatestAndMaxBacklog()
{
    // Given: A collector with multiple DDR status snapshots.
    AcquisitionPerfMetrics::CycleMetrics metrics;

    // When: WR/RD counters move forward over time.
    metrics.RecordDdrStatus(100, 40, 0, 0);
    metrics.RecordDdrStatus(220, 110, 1, 0);
    metrics.RecordDdrStatus(260, 180, 1, 1);

    // Then: Latest state and max backlog are preserved.
    TEST_ASSERT(metrics.latestWaveWrCnt == 260, "TC-N-02 latest WR must be 260");
    TEST_ASSERT(metrics.latestWaveRdCnt == 180, "TC-N-02 latest RD must be 180");
    TEST_ASSERT(metrics.latestDdrWrEnd == 1, "TC-N-02 latest DDR_WR_END must be 1");
    TEST_ASSERT(metrics.latestDdrRdEnd == 1, "TC-N-02 latest DDR_RD_END must be 1");
    TEST_ASSERT(metrics.maxWaveBacklogBytes == 110, "TC-N-02 max backlog must be 110");
}

void Test_TC_B_01_EmptyCollector_ReturnsZeroAverages()
{
    // Given: A fresh collector with no samples.
    AcquisitionPerfMetrics::CycleMetrics metrics;

    // When: Averages are queried before any record.
    const ULONGLONG ep6Average = metrics.GetEp6AverageElapsedMs();
    const ULONGLONG saveAverage = metrics.GetSaveAverageElapsedMs();

    // Then: Zero averages are returned.
    TEST_ASSERT(ep6Average == 0, "TC-B-01 ep6 avg must be 0");
    TEST_ASSERT(saveAverage == 0, "TC-B-01 save avg must be 0");
    TEST_ASSERT(metrics.maxWaveBacklogBytes == 0, "TC-B-01 max backlog must be 0");
}

void Test_TC_B_02_RecordDdrStatus_InvertedCounters_ClampBacklogToZero()
{
    // Given: A snapshot where RD temporarily exceeds WR.
    AcquisitionPerfMetrics::CycleMetrics metrics;

    // When: The snapshot is recorded.
    metrics.RecordDdrStatus(80, 120, 0, 0);

    // Then: Backlog is clamped to zero instead of underflowing.
    TEST_ASSERT(metrics.maxWaveBacklogBytes == 0, "TC-B-02 backlog must clamp to 0");
}

void Test_TC_B_03_RecordEp6Transfer_TimeoutsIncreaseCounter()
{
    // Given: A collector with successful and timeout EP6 reads.
    AcquisitionPerfMetrics::CycleMetrics metrics;

    // When: Timeout results are recorded.
    metrics.RecordEp6Transfer(8, 4096, false);
    metrics.RecordEp6Transfer(15, 4096, true);
    metrics.RecordEp6Transfer(30, 4096, true);

    // Then: Timeout count reflects timeout records only.
    TEST_ASSERT(metrics.ep6TimeoutCount == 2, "TC-B-03 timeout count must be 2");
    TEST_ASSERT(metrics.ep6.callCount == 3, "TC-B-03 ep6 callCount must be 3");
}

void Test_TC_B_04_RecordSaveTransfer_SingleSampleUsesSameAvgAndMax()
{
    // Given: A collector with one save sample.
    AcquisitionPerfMetrics::CycleMetrics metrics;

    // When: A save transfer is recorded.
    metrics.RecordSaveTransfer(25, 16384);

    // Then: Average and max match the single sample.
    TEST_ASSERT(metrics.save.callCount == 1, "TC-B-04 save callCount must be 1");
    TEST_ASSERT(metrics.save.totalElapsedMs == 25, "TC-B-04 save totalElapsedMs must be 25");
    TEST_ASSERT(metrics.save.maxElapsedMs == 25, "TC-B-04 save maxElapsedMs must be 25");
    TEST_ASSERT(metrics.save.totalBytes == 16384, "TC-B-04 save totalBytes must be 16384");
    TEST_ASSERT(metrics.GetSaveAverageElapsedMs() == 25, "TC-B-04 save avg must be 25");
}

void Test_TC_N_03_RecordTimeoutTelemetry_CapturesTimeoutSnapshot()
{
    // Given: A collector with a timeout-time EP6 snapshot.
    AcquisitionPerfMetrics::CycleMetrics metrics;

    // When: Timeout telemetry is recorded.
    metrics.RecordTimeoutTelemetry(65536, 123456, 222222, 300000, 176544, 1, 0, false);

    // Then: The timeout snapshot is preserved for later logging.
    TEST_ASSERT(metrics.timeout.observed == true, "TC-N-03 timeout must be observed");
    TEST_ASSERT(metrics.timeout.drainingHintSeen == false, "TC-N-03 timeout stage must stay active");
    TEST_ASSERT(metrics.timeout.requestedReadSizeBytes == 65536, "TC-N-03 read size must be captured");
    TEST_ASSERT(metrics.timeout.unreadBytes == 123456, "TC-N-03 unread bytes must be captured");
    TEST_ASSERT(metrics.timeout.readableUpperBoundBytes == 222222, "TC-N-03 readable upper bound must be captured");
    TEST_ASSERT(metrics.timeout.backlogBytes == 123456, "TC-N-03 backlog bytes must be captured");
    TEST_ASSERT(metrics.timeout.waveWrCnt == 300000, "TC-N-03 WR count must be captured");
    TEST_ASSERT(metrics.timeout.waveRdCnt == 176544, "TC-N-03 RD count must be captured");
    TEST_ASSERT(metrics.timeout.ddrWrEnd == 1, "TC-N-03 DDR_WR_END must be captured");
    TEST_ASSERT(metrics.timeout.ddrRdEnd == 0, "TC-N-03 DDR_RD_END must be captured");
}

void Test_TC_N_04_MarkTimeoutPostOutcome_CapturesWaitTimeoutAndEp4Failure()
{
    // Given: A collector with one timeout already recorded.
    AcquisitionPerfMetrics::CycleMetrics metrics;
    metrics.RecordTimeoutTelemetry(98304, 4000, 8000, 16000, 12000, 0, 0, true);

    // When: Post-timeout outcomes are recorded.
    metrics.MarkTimeoutWaitTimeout();
    metrics.MarkTimeoutEp4ReadFailure();

    // Then: The follow-up outcome is preserved for the cycle summary.
    TEST_ASSERT(metrics.timeout.drainingHintSeen == true, "TC-N-04 timeout stage must capture draining");
    TEST_ASSERT(metrics.timeout.waitTimeoutObserved == true, "TC-N-04 wait timeout must be captured");
    TEST_ASSERT(metrics.timeout.ep4ReadFailureObserved == true, "TC-N-04 EP4 read failure must be captured");
}

void Test_TC_B_05_WithoutTimeout_TelemetryRemainsDefault()
{
    // Given: A fresh collector without timeout events.
    AcquisitionPerfMetrics::CycleMetrics metrics;

    // When: Timeout telemetry is never recorded.
    metrics.MarkTimeoutWaitTimeout();
    metrics.MarkTimeoutEp4ReadFailure();

    // Then: Timeout telemetry remains at the default state.
    TEST_ASSERT(metrics.timeout.observed == false, "TC-B-05 timeout must remain unobserved");
    TEST_ASSERT(metrics.timeout.requestedReadSizeBytes == 0, "TC-B-05 read size must remain 0");
    TEST_ASSERT(metrics.timeout.waitTimeoutObserved == false, "TC-B-05 wait timeout must remain false");
    TEST_ASSERT(metrics.timeout.ep4ReadFailureObserved == false, "TC-B-05 EP4 read failure must remain false");
}

int main()
{
    std::printf("=== AcquisitionPerfMetrics Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_RecordEp6Transfer_AggregatesCountTimeAndBytes);
    RUN_TEST(Test_TC_N_02_RecordDdrStatus_TracksLatestAndMaxBacklog);
    RUN_TEST(Test_TC_B_01_EmptyCollector_ReturnsZeroAverages);
    RUN_TEST(Test_TC_B_02_RecordDdrStatus_InvertedCounters_ClampBacklogToZero);
    RUN_TEST(Test_TC_B_03_RecordEp6Transfer_TimeoutsIncreaseCounter);
    RUN_TEST(Test_TC_B_04_RecordSaveTransfer_SingleSampleUsesSameAvgAndMax);
    RUN_TEST(Test_TC_N_03_RecordTimeoutTelemetry_CapturesTimeoutSnapshot);
    RUN_TEST(Test_TC_N_04_MarkTimeoutPostOutcome_CapturesWaitTimeoutAndEp4Failure);
    RUN_TEST(Test_TC_B_05_WithoutTimeout_TelemetryRemainsDefault);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
