#include <array>
#include <cstring>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "../AnalogBoard_TestApp/FpgaRegisterAddress.h"
#include "../AnalogBoard_TestApp/FpgaRegisterLogic.h"
#include "../AnalogBoard_TestApp/WaveAcquisitionEngine.h"

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

using namespace WaveAcquisition;

namespace
{
    constexpr ULONG kFrameSizeLow = 4096u;
    constexpr ULONG kFrameSizeHigh = 4096u;
    constexpr ULONG kOneWaveSize = kFrameSizeLow + kFrameSizeHigh;
    constexpr wchar_t kStartupEp4LogPrefix[] = L"[PR04][STARTUP_EP4]";

    std::array<BYTE, kEp4StatusBufferBytes> BuildStatusBuffer(const DdrStatusSnapshot& snapshot)
    {
        std::array<BYTE, kEp4StatusBufferBytes> buffer = {};
        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_L, static_cast<USHORT>(snapshot.waveWrCnt & 0xFFFFu), buffer.data());
        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_H, static_cast<USHORT>((snapshot.waveWrCnt >> 16) & 0xFFFFu), buffer.data());
        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_RD_CNT_L, static_cast<USHORT>(snapshot.waveRdCnt & 0xFFFFu), buffer.data());
        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_RD_CNT_H, static_cast<USHORT>((snapshot.waveRdCnt >> 16) & 0xFFFFu), buffer.data());

        USHORT fpgaStatus = 0;
        if (snapshot.ddrWrEnd != 0)
        {
            fpgaStatus |= 0x4;
        }
        if (snapshot.ddrRdEnd != 0)
        {
            fpgaStatus |= 0x8;
        }
        FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, fpgaStatus, buffer.data());
        return buffer;
    }

    RunConfig MakeConfig(const ULONG wavesPerFile)
    {
        RunConfig config = {};
        config.waveSizeLow = kFrameSizeLow;
        config.waveSizeHigh = kFrameSizeHigh;
        config.wavesPerFile = wavesPerFile;
        config.maxReadChunkBytes = 4u * kOneWaveSize;
        config.ep6TimeoutRetryLimit = 0;
        config.ep4PollSleepMs = 0;
        config.queueCapacity = 8u;
        config.queueWaitTimeoutMs = 50u;
        return config;
    }

    struct FakeStopToken : IStopToken
    {
        bool stopRequested = false;

        bool IsStopRequested() const override
        {
            return stopRequested;
        }
    };

    struct FakeObserver : IAcquisitionObserver
    {
        std::vector<std::wstring> logs;
        std::vector<ULONG> collectedWaveCounts;
        AcquisitionSummary lastSummary = {};

        void OnLog(const std::wstring& message) override
        {
            logs.push_back(message);
        }

        void OnCollectedWaveCount(ULONG collectedWaveCount) override
        {
            collectedWaveCounts.push_back(collectedWaveCount);
        }

        void OnCycleSummary(const AcquisitionSummary& summary) override
        {
            lastSummary = summary;
        }
    };

    struct ScriptedUsbSession : IUsbSession
    {
        std::vector<DdrStatusSnapshot> snapshots;
        std::vector<INT> ep6Results;
        INT ep4CallCount = 0;
        INT ep6CallCount = 0;

        INT Connect() override
        {
            return kUsbSuccess;
        }

        void Disconnect() override
        {
        }

        INT EP2_SendData(BYTE* buffer, size_t bufferSize) override
        {
            (void)buffer;
            (void)bufferSize;
            return kUsbSuccess;
        }

        INT EP4_GetData(BYTE* buffer, size_t bufferSize) override
        {
            if (buffer == nullptr || bufferSize < kEp4StatusBufferBytes || snapshots.empty())
            {
                return kAcquisitionErrEp4Read;
            }

            const size_t index = static_cast<size_t>((ep4CallCount < static_cast<INT>(snapshots.size())) ? ep4CallCount : static_cast<INT>(snapshots.size() - 1));
            const std::array<BYTE, kEp4StatusBufferBytes> statusBuffer = BuildStatusBuffer(snapshots[index]);
            std::memcpy(buffer, statusBuffer.data(), statusBuffer.size());
            ++ep4CallCount;
            return kUsbSuccess;
        }

        INT EP6_GetData(BYTE* buffer, ULONG size) override
        {
            if (buffer == nullptr)
            {
                return kAcquisitionErrEp6Read;
            }

            ++ep6CallCount;
            const size_t index = static_cast<size_t>((ep6CallCount - 1 < static_cast<INT>(ep6Results.size())) ? (ep6CallCount - 1) : static_cast<INT>(ep6Results.size() - 1));
            const INT result = ep6Results.empty() ? kUsbSuccess : ep6Results[index];
            if (result != kUsbSuccess)
            {
                return result;
            }

            for (ULONG i = 0; i < size; ++i)
            {
                buffer[i] = static_cast<BYTE>(i & 0xFFu);
            }

            return kUsbSuccess;
        }
    };

    struct FakeWavePairSink : IWavePairSink
    {
        DWORD writeDelayMs = 0;
        INT nonFatalPublishAt = -1;

        INT openCallCount = 0;
        INT writeCallCount = 0;
        INT publishCallCount = 0;
        INT abortCallCount = 0;
        INT nonFatalPublishCount = 0;
        ULONG totalWaveCount = 0;
        ULONG currentPairWaveCount = 0;
        INT currentPairIndex = 0;
        bool pairOpen = false;
        std::vector<ULONG> publishedWaveCounts;
        std::vector<INT> publishedPairIndices;

        INT OpenPair(INT index) override
        {
            ++openCallCount;
            currentPairIndex = index;
            pairOpen = true;
            currentPairWaveCount = 0;
            return kUsbSuccess;
        }

        INT Write(const BYTE* waveData, ULONG frameSizeLow, ULONG frameSizeHigh, INT waveCnt) override
        {
            if (writeDelayMs > 0)
            {
                ::Sleep(writeDelayMs);
            }

            ++writeCallCount;
            if (!pairOpen || waveData == nullptr || waveCnt <= 0)
            {
                return kAcquisitionErrWritePair;
            }

            totalWaveCount += static_cast<ULONG>(waveCnt);
            currentPairWaveCount += static_cast<ULONG>(waveCnt);
            const size_t expectedBytes = static_cast<size_t>(waveCnt) * static_cast<size_t>(frameSizeLow + frameSizeHigh);
            (void)expectedBytes;
            return kUsbSuccess;
        }

        INT PublishPair() override
        {
            ++publishCallCount;
            publishedWaveCounts.push_back(currentPairWaveCount);
            publishedPairIndices.push_back(currentPairIndex);
            pairOpen = false;
            currentPairWaveCount = 0;
            currentPairIndex = 0;

            if (nonFatalPublishAt == publishCallCount)
            {
                ++nonFatalPublishCount;
            }

            return kUsbSuccess;
        }

        void AbortPair() override
        {
            ++abortCallCount;
            pairOpen = false;
            currentPairWaveCount = 0;
        }

        bool HasOpenPair() const override
        {
            return pairOpen;
        }
    };

    size_t CountLogsWithPrefix(const FakeObserver& observer, const wchar_t* prefix)
    {
        size_t count = 0u;
        for (const std::wstring& line : observer.logs)
        {
            if (line.find(prefix) == 0u)
            {
                ++count;
            }
        }
        return count;
    }
}

void Test_TC_N_01_PartialFinalPair_PublishesAtCycleEnd()
{
    // Given: Two waves with a three-wave file capacity.
    ScriptedUsbSession usb = {};
    usb.snapshots = {
        { 2u * kOneWaveSize, 0u, 0, 0 },
        { 2u * kOneWaveSize, 2u * kOneWaveSize, 1, 1 }
    };
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    const RunConfig config = MakeConfig(3u);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine drains the cycle.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The partial final pair is published exactly once at the end.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "TC-N-01 terminal status should be success");
    TEST_ASSERT(summary.savedWaveCount == 2u, "TC-N-01 saved wave count should be 2");
    TEST_ASSERT(summary.publishedPairCount == 1, "TC-N-01 one partial pair should be published");
    TEST_ASSERT(sink.publishedWaveCounts.size() == 1u, "TC-N-01 sink should publish exactly one pair");
    TEST_ASSERT(sink.publishedWaveCounts[0] == 2u, "TC-N-01 published pair should contain the two saved waves");
}

void Test_TC_N_02_NonFatalPublish_StillCompletesCycle()
{
    // Given: Two single-wave pairs where the first publish is degraded but non-fatal.
    ScriptedUsbSession usb = {};
    usb.snapshots = {
        { 2u * kOneWaveSize, 0u, 0, 0 },
        { 2u * kOneWaveSize, 2u * kOneWaveSize, 1, 1 }
    };
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    sink.nonFatalPublishAt = 1;
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    const RunConfig config = MakeConfig(1u);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine drains the cycle through both pairs.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle still completes successfully and keeps later publishes visible.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "TC-N-02 terminal status should be success");
    TEST_ASSERT(summary.savedWaveCount == 2u, "TC-N-02 saved wave count should be 2");
    TEST_ASSERT(summary.publishedPairCount == 2, "TC-N-02 both pairs should be published");
    TEST_ASSERT(sink.nonFatalPublishCount == 1, "TC-N-02 non-fatal publish should be observed once");
}

void Test_TC_B_01_QueueFullTimeout_StopsReaderBeforeWriterCatchesUp()
{
    // Given: Four single-wave chunks, queue capacity one, and a blocked writer.
    ScriptedUsbSession usb = {};
    usb.snapshots = {
        { 4u * kOneWaveSize, 0u, 0, 0 },
        { 4u * kOneWaveSize, 4u * kOneWaveSize, 1, 1 }
    };
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    sink.writeDelayMs = 50u;
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(1u);
    config.maxReadChunkBytes = 4u * kOneWaveSize;
    config.queueCapacity = 1u;
    config.queueWaitTimeoutMs = 1u;
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The reader outruns the writer.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle terminates with the queue full timeout error.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::QueueFullTimeout, "TC-B-01 terminal status should be queue_full_timeout");
    TEST_ASSERT(summary.errorCode == USB_ERR_QUEUE_FULL_TIMEOUT, "TC-B-01 error code should be queue full timeout");
}

void Test_TC_B_02_PublishSequence_KeepsCompletedPairsMonotonic()
{
    // Given: Three waves and a two-wave file capacity.
    ScriptedUsbSession usb = {};
    usb.snapshots = {
        { 3u * kOneWaveSize, 0u, 0, 0 },
        { 3u * kOneWaveSize, 3u * kOneWaveSize, 1, 1 }
    };
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    const RunConfig config = MakeConfig(2u);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine publishes one full pair and one final partial pair.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: Published pair sizes stay monotonic and incomplete data is not merged into the first pair.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "TC-B-02 terminal status should be success");
    TEST_ASSERT(sink.publishedWaveCounts.size() == 2u, "TC-B-02 exactly two publish events should occur");
    TEST_ASSERT(sink.publishedWaveCounts[0] == 2u, "TC-B-02 first publish should contain the completed pair");
    TEST_ASSERT(sink.publishedWaveCounts[1] == 1u, "TC-B-02 second publish should contain only the final partial pair");
}

void Test_TC_I_01_LastNPlus1Probe_ObservesOnlyPublishedPairs()
{
    // Given: One completed pair and one later partial pair.
    ScriptedUsbSession usb = {};
    usb.snapshots = {
        { 3u * kOneWaveSize, 0u, 0, 0 },
        { 3u * kOneWaveSize, 3u * kOneWaveSize, 1, 1 }
    };
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    const RunConfig config = MakeConfig(2u);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine publishes the completed pair and later flushes the partial pair.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: A last_n + 1 style probe can observe only completed published pairs in order.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "TC-I-01 terminal status should be success");
    TEST_ASSERT(summary.publishedPairCount == 2, "TC-I-01 two published pairs should be visible");
    TEST_ASSERT(sink.publishedPairIndices.size() == 2u, "TC-I-01 exactly two published indices should be visible");
    TEST_ASSERT(sink.publishedPairIndices[0] == 1, "TC-I-01 first visible pair index should be 1");
    TEST_ASSERT(sink.publishedPairIndices[1] == 2, "TC-I-01 second visible pair index should be 2");
    TEST_ASSERT(sink.publishedWaveCounts[0] == 2u, "TC-I-01 first visible pair should be complete");
    TEST_ASSERT(sink.publishedWaveCounts[1] == 1u, "TC-I-01 second visible pair should be the later flushed partial pair");
}

void Test_TC_N_03_StartupEp4Observation_LogsSnapshotFields()
{
    // Given: A startup-stale snapshot is observed before the real cycle begins.
    ScriptedUsbSession usb = {};
    usb.snapshots = {
        { 0u, 0u, 0, 1 },
        { 2u * kOneWaveSize, 0u, 0, 0 },
        { 2u * kOneWaveSize, 2u * kOneWaveSize, 1, 1 }
    };
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    const RunConfig config = MakeConfig(3u);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine processes the startup snapshots.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The observer receives a startup EP4 log with the raw register fields.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "TC-N-03 terminal status should be success");
    const size_t startupLogCount = CountLogsWithPrefix(observer, kStartupEp4LogPrefix);
    TEST_ASSERT(startupLogCount >= 1u, "TC-N-03 startup EP4 log should be emitted");
    std::wstring startupLog;
    for (const std::wstring& line : observer.logs)
    {
        if (line.find(kStartupEp4LogPrefix) == 0u)
        {
            startupLog = line;
            break;
        }
    }
    TEST_ASSERT(startupLog.find(L"WAVE_WR_CNT=0") != std::wstring::npos, "TC-N-03 startup log should contain WAVE_WR_CNT");
    TEST_ASSERT(startupLog.find(L"WAVE_RD_CNT=0") != std::wstring::npos, "TC-N-03 startup log should contain WAVE_RD_CNT");
    TEST_ASSERT(startupLog.find(L"DDR_WR_END=0") != std::wstring::npos, "TC-N-03 startup log should contain DDR_WR_END");
    TEST_ASSERT(startupLog.find(L"DDR_RD_END=1") != std::wstring::npos, "TC-N-03 startup log should contain DDR_RD_END");
}

void Test_TC_B_03_StartupEp4Observation_IsCappedToThreeLogs()
{
    // Given: More than three startup polls occur before any active cycle is observed.
    ScriptedUsbSession usb = {};
    usb.snapshots = {
        { 0u, 0u, 1, 1 },
        { 0u, 0u, 1, 1 },
        { 0u, 0u, 1, 1 },
        { 0u, 0u, 1, 1 },
        { 0u, 0u, 1, 1 }
    };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    const RunConfig config = MakeConfig(3u);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine keeps polling the startup-stale status until it gives up.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The startup EP4 observation is capped and does not spam the log.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::EmptyCapture, "TC-B-03 terminal status should be empty_capture");
    TEST_ASSERT(CountLogsWithPrefix(observer, kStartupEp4LogPrefix) == 3u, "TC-B-03 startup EP4 logs should be capped to three");
}

int main()
{
    std::printf("=== WaveAcquisitionEngine Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_PartialFinalPair_PublishesAtCycleEnd);
    RUN_TEST(Test_TC_N_02_NonFatalPublish_StillCompletesCycle);
    RUN_TEST(Test_TC_N_03_StartupEp4Observation_LogsSnapshotFields);
    RUN_TEST(Test_TC_B_01_QueueFullTimeout_StopsReaderBeforeWriterCatchesUp);
    RUN_TEST(Test_TC_B_02_PublishSequence_KeepsCompletedPairsMonotonic);
    RUN_TEST(Test_TC_B_03_StartupEp4Observation_IsCappedToThreeLogs);
    RUN_TEST(Test_TC_I_01_LastNPlus1Probe_ObservesOnlyPublishedPairs);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
