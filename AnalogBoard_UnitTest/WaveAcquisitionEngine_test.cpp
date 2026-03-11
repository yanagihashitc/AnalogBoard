#include <windows.h>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "../AnalogBoard_TestApp/FpgaRegisterLogic.h"
#include "../AnalogBoard_TestApp/WaveAcquisitionEngine.h"

using namespace WaveAcquisition;

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

namespace
{
    constexpr ULONG kSmallWaveSizeLow = 32;
    constexpr ULONG kSmallWaveSizeHigh = 32;
    constexpr ULONG kSmallWaveSize = kSmallWaveSizeLow + kSmallWaveSizeHigh;
    constexpr ULONG kLargeWaveSizeLow = 2048;
    constexpr ULONG kLargeWaveSizeHigh = 2048;
    constexpr ULONG kLargeWaveSize = kLargeWaveSizeLow + kLargeWaveSizeHigh;

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
        bool sawSummary = false;
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
            sawSummary = true;
            lastSummary = summary;
        }
    };

    struct FakeWavePairSink : IWavePairSink
    {
        INT failOpenPairAt = -1;
        INT failWriteAt = -1;
        INT failPublishAt = -1;
        DWORD writeDelayMs = 0;

        INT openCallCount = 0;
        INT writeCallCount = 0;
        INT publishCallCount = 0;
        INT abortCallCount = 0;
        ULONG totalWaveCount = 0;
        ULONGLONG totalBytesWritten = 0;
        bool pairOpen = false;

        INT OpenPair(INT index) override
        {
            ++openCallCount;
            if (failOpenPairAt == openCallCount)
            {
                return kAcquisitionErrOpenPair;
            }

            pairOpen = true;
            (void)index;
            return kUsbSuccess;
        }

        INT Write(const BYTE* waveData, ULONG frameSizeLow, ULONG frameSizeHigh, INT waveCnt) override
        {
            ++writeCallCount;
            if (writeDelayMs > 0)
            {
                ::Sleep(writeDelayMs);
            }

            if (failWriteAt == writeCallCount)
            {
                return kAcquisitionErrWritePair;
            }

            TEST_ASSERT(pairOpen, "FakeWavePairSink write requires an open pair");
            TEST_ASSERT(waveData != nullptr, "FakeWavePairSink write requires waveData");

            totalWaveCount += static_cast<ULONG>(waveCnt);
            totalBytesWritten += static_cast<ULONGLONG>(waveCnt) *
                static_cast<ULONGLONG>(frameSizeLow + frameSizeHigh);
            return kUsbSuccess;
        }

        INT PublishPair() override
        {
            ++publishCallCount;
            if (failPublishAt == publishCallCount)
            {
                return kAcquisitionErrPublishPair;
            }

            TEST_ASSERT(pairOpen, "FakeWavePairSink publish requires an open pair");
            pairOpen = false;
            return kUsbSuccess;
        }

        void AbortPair() override
        {
            ++abortCallCount;
            pairOpen = false;
        }

        bool HasOpenPair() const override
        {
            return pairOpen;
        }
    };

    struct FakeUsbSession : IUsbSession
    {
        ULONG totalLogicalBytes = 0;
        ULONG producerStepBytes = 0;
        std::vector<INT> ep6Results;

        ULONG producedLogicalBytes = 0;
        ULONG readLogicalBytes = 0;
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
            ++ep4CallCount;
            if (buffer == nullptr || bufferSize < kEp4StatusBufferBytes)
            {
                return kAcquisitionErrEp4Read;
            }

            if (producerStepBytes == 0)
            {
                producedLogicalBytes = totalLogicalBytes;
            }
            else if (producedLogicalBytes < totalLogicalBytes)
            {
                producedLogicalBytes = (std::min)(totalLogicalBytes, producedLogicalBytes + producerStepBytes);
            }

            std::memset(buffer, 0, bufferSize);

            ULONG registerWaveWrCnt = 0;
            if (producedLogicalBytes > 0)
            {
                registerWaveWrCnt = producedLogicalBytes - static_cast<ULONG>(kDdrCompletionPaddingBytes);
            }

            ULONG registerWaveRdCnt = 0;
            if (readLogicalBytes > 0)
            {
                registerWaveRdCnt = readLogicalBytes - static_cast<ULONG>(kDdrCompletionPaddingBytes);
            }

            FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_H, static_cast<USHORT>((registerWaveWrCnt >> 16) & 0xFFFF), buffer);
            FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_L, static_cast<USHORT>(registerWaveWrCnt & 0xFFFF), buffer);
            FpgaRegLogic::Reg_Write(FPGAREG_WAVE_RD_CNT_H, static_cast<USHORT>((registerWaveRdCnt >> 16) & 0xFFFF), buffer);
            FpgaRegLogic::Reg_Write(FPGAREG_WAVE_RD_CNT_L, static_cast<USHORT>(registerWaveRdCnt & 0xFFFF), buffer);

            USHORT fpgaStatus = 0;
            if (producedLogicalBytes >= totalLogicalBytes && totalLogicalBytes != 0)
            {
                fpgaStatus |= 0x4;
            }
            if (readLogicalBytes >= totalLogicalBytes && totalLogicalBytes != 0)
            {
                fpgaStatus |= 0x8;
            }
            FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, fpgaStatus, buffer);
            return kUsbSuccess;
        }

        INT EP6_GetData(BYTE* buffer, ULONG size) override
        {
            ++ep6CallCount;
            if (buffer == nullptr)
            {
                return kAcquisitionErrEp6Read;
            }

            INT result = kUsbSuccess;
            if (ep6CallCount <= static_cast<INT>(ep6Results.size()))
            {
                result = ep6Results[static_cast<size_t>(ep6CallCount - 1)];
            }

            if (result != kUsbSuccess)
            {
                return result;
            }

            for (ULONG i = 0; i < size; ++i)
            {
                buffer[i] = static_cast<BYTE>((readLogicalBytes + i) & 0xFFu);
            }

            const ULONG remainingLogicalBytes = totalLogicalBytes - readLogicalBytes;
            const ULONG logicalBytesReadThisCall = (std::min)(remainingLogicalBytes, size);
            readLogicalBytes += logicalBytesReadThisCall;
            return kUsbSuccess;
        }
    };

    RunConfig MakeConfig(ULONG waveSizeLow, ULONG waveSizeHigh, ULONG wavesPerFile)
    {
        RunConfig config = {};
        config.waveSizeLow = waveSizeLow;
        config.waveSizeHigh = waveSizeHigh;
        config.wavesPerFile = wavesPerFile;
        config.maxReadChunkBytes = static_cast<ULONG>(kEp6ReadAlignmentBytes);
        config.ep6TimeoutRetryLimit = 0;
        config.ep4PollSleepMs = 0;
        return config;
    }
}

void Test_TC_N_01_NormalComplete_ReturnsSuccessSummary()
{
    // Given: A normal acquisition scenario with no injected failures.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kSmallWaveSize * 4;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 2);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine runs the acquisition cycle.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle completes successfully and emits a populated summary.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "TC-N-01 terminal status must be success");
    TEST_ASSERT(summary.errorCode == kUsbSuccess, "TC-N-01 error code must be success");
    TEST_ASSERT(summary.savedWaveCount == 4, "TC-N-01 savedWaveCount must be 4");
    TEST_ASSERT(summary.publishedPairCount == 2, "TC-N-01 publishedPairCount must be 2");
    TEST_ASSERT(summary.metrics.ep6.callCount == 1, "TC-N-01 ep6 callCount must be 1");
    TEST_ASSERT(summary.metrics.save.callCount == 2, "TC-N-01 save callCount must be 2");
    TEST_ASSERT(summary.metrics.latestDdrWrEnd == 1, "TC-N-01 DDR_WR_END must be 1");
    TEST_ASSERT(observer.sawSummary, "TC-N-01 observer must receive summary");
    TEST_ASSERT(sink.publishCallCount == 2, "TC-N-01 sink publish must be called twice");
}

void Test_TC_N_02_TimeoutRecover_ContinuesAfterSingleTimeout()
{
    // Given: A scenario where one EP6 timeout is followed by successful reads.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kSmallWaveSize * 4;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbErrTransferTimeout, kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 2);
    config.ep6TimeoutRetryLimit = 1;
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine retries within the configured timeout budget.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle recovers and finishes successfully with timeoutCount=1.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "TC-N-02 terminal status must be success");
    TEST_ASSERT(summary.metrics.ep6TimeoutCount == 1, "TC-N-02 timeout count must be 1");
    TEST_ASSERT(summary.metrics.ep6.callCount == 2, "TC-N-02 ep6 callCount must be 2");
    TEST_ASSERT(summary.savedWaveCount == 4, "TC-N-02 savedWaveCount must be 4");
    TEST_ASSERT(!observer.logs.empty(), "TC-N-02 observer must receive retry log");
}

void Test_TC_N_03_QueuePressure_TracksBacklogWhileCompleting()
{
    // Given: A slow sink that creates queue pressure while USB reads keep succeeding.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kLargeWaveSize * 8;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbSuccess, kUsbSuccess };
    FakeWavePairSink sink = {};
    sink.writeDelayMs = 5;
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kLargeWaveSizeLow, kLargeWaveSizeHigh, 2);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine drains the available data.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle completes successfully and records maxBacklogBytes > 0.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "TC-N-03 terminal status must be success");
    TEST_ASSERT(summary.metrics.maxWaveBacklogBytes > 0, "TC-N-03 backlog must be positive");
    TEST_ASSERT(summary.savedWaveCount == 8, "TC-N-03 savedWaveCount must be 8");
    TEST_ASSERT(summary.metrics.save.callCount >= 2, "TC-N-03 save callCount must be >= 2");
}

void Test_TC_A_01_PersistentTimeout_StopsAfterRetryBudget()
{
    // Given: A scenario where EP6 keeps timing out beyond the retry budget.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kSmallWaveSize * 4;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbErrTransferTimeout, kUsbErrTransferTimeout };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 2);
    config.ep6TimeoutRetryLimit = 1;
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine exhausts the allowed retries.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle terminates with the timeout terminal status.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Ep6Timeout, "TC-A-01 terminal status must be ep6_timeout");
    TEST_ASSERT(summary.errorCode == kUsbErrTransferTimeout, "TC-A-01 error code must be timeout");
    TEST_ASSERT(summary.metrics.ep6TimeoutCount == 2, "TC-A-01 timeout count must be 2");
    TEST_ASSERT(sink.abortCallCount == 0, "TC-A-01 no pair should be open to abort");
}

void Test_TC_A_02_DisconnectMidstream_StopsWithDisconnectStatus()
{
    // Given: A scenario where the USB session disconnects during EP6 reads.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kLargeWaveSize * 8;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbSuccess, kUsbErrNoDevice };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kLargeWaveSizeLow, kLargeWaveSizeHigh, 2);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine encounters the disconnect error.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle terminates with the disconnect terminal status.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::UsbDisconnect, "TC-A-02 terminal status must be usb_disconnect");
    TEST_ASSERT(summary.errorCode == kUsbErrNoDevice, "TC-A-02 error code must be no device");
    TEST_ASSERT(summary.savedWaveCount == 4, "TC-A-02 first chunk must still be saved");
    TEST_ASSERT(sink.abortCallCount == 0, "TC-A-02 no open pair remains when disconnect occurs after publish");
}

void Test_TC_A_03_PublishFailure_StopsWithPublishFailedStatus()
{
    // Given: A scenario where publish fails after a successful write.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kSmallWaveSize * 2;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    sink.failPublishAt = 1;
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 2);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine tries to publish the completed pair.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle terminates with the publish-failed terminal status.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::PublishFailed, "TC-A-03 terminal status must be publish_failed");
    TEST_ASSERT(summary.errorCode == kAcquisitionErrPublishPair, "TC-A-03 error code must be publish pair");
    TEST_ASSERT(sink.abortCallCount == 1, "TC-A-03 publish failure must abort the open pair");
}

void Test_TC_B_01_ZeroWavesPerFile_IsRejected()
{
    // Given: An invalid config with wavesPerFile equal to zero.
    FakeUsbSession usb = {};
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 0);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine validates the config.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle is rejected as invalid config.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::InvalidConfig, "TC-B-01 terminal status must be invalid_config");
    TEST_ASSERT(summary.errorCode == kAcquisitionErrInvalidConfig, "TC-B-01 error code must be invalid config");
    TEST_ASSERT(observer.sawSummary, "TC-B-01 observer must receive summary");
}

void Test_TC_B_02_ZeroWaveSizes_IsRejected()
{
    // Given: An invalid config with both low/high wave sizes set to zero.
    FakeUsbSession usb = {};
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(0, 0, 2);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine validates the config.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle is rejected as invalid config.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::InvalidConfig, "TC-B-02 terminal status must be invalid_config");
    TEST_ASSERT(summary.errorCode == kAcquisitionErrInvalidConfig, "TC-B-02 error code must be invalid config");
}

void Test_TC_B_03_ZeroRetryBudget_FailsOnFirstTimeout()
{
    // Given: A config that allows zero timeout retries and a timeout on the first EP6 read.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kSmallWaveSize * 4;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbErrTransferTimeout };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 2);
    config.ep6TimeoutRetryLimit = 0;
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine observes the first timeout.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle terminates immediately with the timeout terminal status.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Ep6Timeout, "TC-B-03 terminal status must be ep6_timeout");
    TEST_ASSERT(summary.metrics.ep6.callCount == 1, "TC-B-03 ep6 callCount must be 1");
    TEST_ASSERT(summary.savedWaveCount == 0, "TC-B-03 savedWaveCount must be 0");
}

void Test_TC_B_04_RetryBudgetOne_AllowsSingleRecovery()
{
    // Given: A config that allows one timeout retry and a timeout followed by success.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kSmallWaveSize * 2;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbErrTransferTimeout, kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 2);
    config.ep6TimeoutRetryLimit = 1;
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine uses the single retry opportunity.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle still completes successfully.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "TC-B-04 terminal status must be success");
    TEST_ASSERT(summary.metrics.ep6TimeoutCount == 1, "TC-B-04 timeout count must be 1");
    TEST_ASSERT(summary.savedWaveCount == 2, "TC-B-04 savedWaveCount must be 2");
}

void Test_TC_B_05_StopRequestedBeforeStart_ReturnsStopped()
{
    // Given: A stop token that is already requested before the cycle begins.
    FakeUsbSession usb = {};
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    stopToken.stopRequested = true;
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 2);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine starts the cycle.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle exits without processing data and reports stopped.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Stopped, "TC-B-05 terminal status must be stopped");
    TEST_ASSERT(summary.errorCode == kAcquisitionErrStopped, "TC-B-05 error code must be stopped");
    TEST_ASSERT(usb.ep4CallCount == 0, "TC-B-05 EP4 must not be called");
    TEST_ASSERT(usb.ep6CallCount == 0, "TC-B-05 EP6 must not be called");
}

int main()
{
    std::printf("=== WaveAcquisitionEngine Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_NormalComplete_ReturnsSuccessSummary);
    RUN_TEST(Test_TC_N_02_TimeoutRecover_ContinuesAfterSingleTimeout);
    RUN_TEST(Test_TC_N_03_QueuePressure_TracksBacklogWhileCompleting);
    RUN_TEST(Test_TC_A_01_PersistentTimeout_StopsAfterRetryBudget);
    RUN_TEST(Test_TC_A_02_DisconnectMidstream_StopsWithDisconnectStatus);
    RUN_TEST(Test_TC_A_03_PublishFailure_StopsWithPublishFailedStatus);
    RUN_TEST(Test_TC_B_01_ZeroWavesPerFile_IsRejected);
    RUN_TEST(Test_TC_B_02_ZeroWaveSizes_IsRejected);
    RUN_TEST(Test_TC_B_03_ZeroRetryBudget_FailsOnFirstTimeout);
    RUN_TEST(Test_TC_B_04_RetryBudgetOne_AllowsSingleRecovery);
    RUN_TEST(Test_TC_B_05_StopRequestedBeforeStart_ReturnsStopped);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
