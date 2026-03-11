#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "../AnalogBoard_TestApp/WaveAcquisitionEngine.h"
#include "../AnalogBoard_TestApp/FpgaRegisterAddress.h"
#include "../AnalogBoard_TestApp/FpgaRegisterEncoding.h"
#include "../AnalogBoard_TestApp/FpgaRegisterLogic.h"
#include "../AnalogBoard_SimRunner/FpgaDdrModel.h"
#include "../AnalogBoard_SimRunner/SimulationEp4StatusHelper.h"

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
    constexpr ULONG kBurstSizeBytes = static_cast<ULONG>(kEp6ReadAlignmentBytes);

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

    struct FakePollWaiter : IPollWaiter
    {
        std::vector<DWORD> waitCalls;

        void Wait(DWORD milliseconds) override
        {
            waitCalls.push_back(milliseconds);
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
        ULONG producerBurstsPerPoll = 0;
        INT initPollCount = 1;
        INT waitPollCount = 1;
        std::vector<INT> ep6Results;

        INT ep4CallCount = 0;
        INT ep6CallCount = 0;
        bool modelInitialized = false;
        SimRunner::FpgaDdrModel ddrModel;

        void InitializeModelIfNeeded()
        {
            if (modelInitialized)
            {
                return;
            }

            SimRunner::FpgaDdrModelConfig config = {};
            config.totalWaveBytes = totalLogicalBytes;
            config.burstSizeBytes = kBurstSizeBytes;
            config.producerStepBytes = producerStepBytes;
            config.producerBurstsPerPoll = producerBurstsPerPoll;
            config.initPollCount = initPollCount;
            config.waitPollCount = waitPollCount;
            ddrModel = SimRunner::FpgaDdrModel(config);
            modelInitialized = true;
        }

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

            InitializeModelIfNeeded();
            ddrModel.AdvanceOnePoll();
            SimulationEp4StatusHelper::WriteStatusBuffer(
                buffer,
                bufferSize,
                ddrModel.GetWrittenBytes(),
                ddrModel.GetReadBytes(),
                true,
                ddrModel.IsAdcSetEnd(),
                ddrModel.IsDdrWrEnd(),
                ddrModel.IsDdrRdEnd(),
                ddrModel.IsMeasTrg());
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

            InitializeModelIfNeeded();
            const ULONG readOffset = ddrModel.GetReadBytes();
            for (ULONG i = 0; i < size; ++i)
            {
                buffer[i] = static_cast<BYTE>((readOffset + i) & 0xFFu);
            }

            ddrModel.OnEp6ReadCompleted(size);
            return kUsbSuccess;
        }
    };

    std::array<BYTE, kEp4StatusBufferBytes> BuildStatusBuffer(
        ULONG writtenBytes,
        ULONG readBytes,
        bool ddrLink,
        bool adcSetEnd,
        bool ddrWrEnd,
        bool ddrRdEnd,
        bool measTrg)
    {
        std::array<BYTE, kEp4StatusBufferBytes> buffer = {};
        SimulationEp4StatusHelper::WriteStatusBuffer(
            buffer.data(),
            buffer.size(),
            writtenBytes,
            readBytes,
            ddrLink,
            adcSetEnd,
            ddrWrEnd,
            ddrRdEnd,
            measTrg);
        return buffer;
    }

    std::array<BYTE, kEp4StatusBufferBytes> BuildStatusBufferFromModel(const SimRunner::FpgaDdrModel& model)
    {
        return BuildStatusBuffer(
            model.GetWrittenBytes(),
            model.GetReadBytes(),
            true,
            model.IsAdcSetEnd(),
            model.IsDdrWrEnd(),
            model.IsDdrRdEnd(),
            model.IsMeasTrg());
    }

    USHORT ReadFpgaStatusWord(const std::array<BYTE, kEp4StatusBufferBytes>& buffer)
    {
        return FpgaRegLogic::Reg_Read(FPGAREG_FPGA_ST, buffer.data());
    }

    bool AdvanceModelUntilState(
        SimRunner::FpgaDdrModel* model,
        SimRunner::MeasState targetState,
        INT maxPollCount = 32)
    {
        if (model == nullptr)
        {
            return false;
        }

        for (INT poll = 0; poll < maxPollCount; ++poll)
        {
            model->AdvanceOnePoll();
            if (model->GetState() == targetState)
            {
                return true;
            }
        }

        return false;
    }

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

void Test_TC_A_04_WriteFailure_StopsWithWriteFailedStatus()
{
    // Given: A scenario where write fails after a pair is opened.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kSmallWaveSize * 2;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    sink.failWriteAt = 1;
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 2);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine tries to persist the first completed pair.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle terminates with write_failed and aborts the open pair.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::WriteFailed, "TC-A-04 terminal status must be write_failed");
    TEST_ASSERT(summary.errorCode == kAcquisitionErrWritePair, "TC-A-04 error code must be write pair");
    TEST_ASSERT(sink.abortCallCount == 1, "TC-A-04 write failure must abort the open pair");
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

void Test_TC_B_06_ProducerStepBelowPadding_DoesNotUnderflowWaveWriteCount()
{
    // Given: EP4 production progress smaller than the DDR completion padding.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kSmallWaveSize;
    usb.producerStepBytes = static_cast<ULONG>(kDdrCompletionPaddingBytes - 16u);
    std::array<BYTE, kEp4StatusBufferBytes> ep4Buffer = {};

    // When: The fake USB session emits the EP4 status registers.
    usb.EP4_GetData(ep4Buffer.data(), ep4Buffer.size());
    usb.EP4_GetData(ep4Buffer.data(), ep4Buffer.size());
    const INT result = usb.EP4_GetData(ep4Buffer.data(), ep4Buffer.size());
    const DdrStatusSnapshot snapshot = WaveAcquisitionEngine::DecodeDdrStatus(ep4Buffer.data(), ep4Buffer.size());

    // Then: WAVE_WR_CNT stays at zero instead of wrapping around ULONG.
    TEST_ASSERT(result == kUsbSuccess, "TC-B-06 EP4 read must succeed");
    TEST_ASSERT(snapshot.waveWrCnt == 0, "TC-B-06 WAVE_WR_CNT must clamp to zero");
}

void Test_TC_B_07_UnalignedMaxReadChunk_IsRejected()
{
    // Given: A config whose maxReadChunkBytes is not aligned to EP6 requirements.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kSmallWaveSize * 2;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 2);
    config.maxReadChunkBytes = static_cast<ULONG>(kEp6ReadAlignmentBytes - 1u);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine validates the config.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The cycle is rejected as invalid_config before any transfer starts.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::InvalidConfig, "TC-B-07 terminal status must be invalid_config");
    TEST_ASSERT(summary.errorCode == kAcquisitionErrInvalidConfig, "TC-B-07 error code must be invalid config");
}

void Test_TC_B_08_Ep4PollSleepZero_YieldsBeforeEachPoll()
{
    // Given: A multi-poll acquisition with EP4 sleep set to zero.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kSmallWaveSize * 4;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    FakePollWaiter pollWaiter = {};
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 2);
    config.ep4PollSleepMs = 0;
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken, &pollWaiter);

    // When: The engine runs the acquisition cycle.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: Every EP4 poll is preceded by a zero-duration yield.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "TC-B-08 terminal status must be success");
    TEST_ASSERT(!pollWaiter.waitCalls.empty(), "TC-B-08 wait hook must be called");
    TEST_ASSERT(pollWaiter.waitCalls.size() == static_cast<size_t>(usb.ep4CallCount), "TC-B-08 wait hook count must match EP4 polls");
    TEST_ASSERT(std::all_of(pollWaiter.waitCalls.begin(), pollWaiter.waitCalls.end(), [](DWORD value) { return value == 0; }), "TC-B-08 wait hook values must stay zero");
}

void Test_TC_B_09_Ep4PollSleepPositive_UsesConfiguredDelay()
{
    // Given: A multi-poll acquisition with a positive EP4 sleep.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kSmallWaveSize * 4;
    usb.producerStepBytes = usb.totalLogicalBytes;
    usb.ep6Results = { kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    FakePollWaiter pollWaiter = {};
    RunConfig config = MakeConfig(kSmallWaveSizeLow, kSmallWaveSizeHigh, 2);
    config.ep4PollSleepMs = 5;
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken, &pollWaiter);

    // When: The engine runs the acquisition cycle.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: Every EP4 poll uses the configured delay.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "TC-B-09 terminal status must be success");
    TEST_ASSERT(!pollWaiter.waitCalls.empty(), "TC-B-09 wait hook must be called");
    TEST_ASSERT(std::all_of(pollWaiter.waitCalls.begin(), pollWaiter.waitCalls.end(), [](DWORD value) { return value == 5; }), "TC-B-09 wait hook values must stay five");
}

void Test_L1_01_EncodeWaveWrCnt_MatchesFpgaSpec()
{
    // Given: One full EP6-aligned burst has been written to DDR.
    std::array<BYTE, kEp4StatusBufferBytes> ep4Buffer = {};

    // When: The FPGA-style write counter encoding is written into EP4.
    FpgaRegEncoding::EncodeWaveWrCnt(kBurstSizeBytes, ep4Buffer.data());
    const DdrStatusSnapshot snapshot = WaveAcquisitionEngine::DecodeDdrStatus(ep4Buffer.data(), ep4Buffer.size());

    // Then: The host sees the FPGA register value and reconstructs the burst size by adding padding.
    TEST_ASSERT(snapshot.waveWrCnt == (kBurstSizeBytes - static_cast<ULONG>(kDdrCompletionPaddingBytes)), "L1-01 WAVE_WR_CNT must match FPGA encoding");
    TEST_ASSERT(snapshot.waveWrCnt + static_cast<ULONG>(kDdrCompletionPaddingBytes) == kBurstSizeBytes, "L1-01 host-visible bytes must reconstruct the burst size");
}

void Test_L1_02_EncodeWaveWrCnt_ZeroBytes_ReturnsZero()
{
    // Given: No DDR data has been written yet.
    std::array<BYTE, kEp4StatusBufferBytes> ep4Buffer = {};

    // When: The FPGA-style write counter encodes zero bytes.
    FpgaRegEncoding::EncodeWaveWrCnt(0, ep4Buffer.data());
    const DdrStatusSnapshot snapshot = WaveAcquisitionEngine::DecodeDdrStatus(ep4Buffer.data(), ep4Buffer.size());

    // Then: WAVE_WR_CNT remains zero.
    TEST_ASSERT(snapshot.waveWrCnt == 0, "L1-02 WAVE_WR_CNT must stay zero");
}

void Test_L1_03_FpgaSt_AllBits_RoundTrip()
{
    // Given: All FPGA_ST bits are asserted in the EP4 status buffer.
    const std::array<BYTE, kEp4StatusBufferBytes> ep4Buffer = BuildStatusBuffer(
        0,
        0,
        true,
        true,
        true,
        true,
        true);

    // When: The host decodes the status word.
    const DdrStatusSnapshot snapshot = WaveAcquisitionEngine::DecodeDdrStatus(ep4Buffer.data(), ep4Buffer.size());
    const USHORT fpgaStatus = ReadFpgaStatusWord(ep4Buffer);

    // Then: Only the intended bits are visible and write/read end decode remains correct.
    TEST_ASSERT((fpgaStatus & FpgaRegEncoding::kFpgaStBitDdrLink) != 0, "L1-03 DDR link bit must be set");
    TEST_ASSERT((fpgaStatus & FpgaRegEncoding::kFpgaStBitAdcSetEnd) != 0, "L1-03 ADC set end bit must be set");
    TEST_ASSERT((fpgaStatus & FpgaRegEncoding::kFpgaStBitDdrWrEnd) != 0, "L1-03 DDR write end bit must be set");
    TEST_ASSERT((fpgaStatus & FpgaRegEncoding::kFpgaStBitDdrRdEnd) != 0, "L1-03 DDR read end bit must be set");
    TEST_ASSERT((fpgaStatus & FpgaRegEncoding::kFpgaStBitMeasTrg) != 0, "L1-03 measurement trigger bit must be set");
    TEST_ASSERT(snapshot.ddrWrEnd == 1, "L1-03 DDR_WR_END must decode to 1");
    TEST_ASSERT(snapshot.ddrRdEnd == 1, "L1-03 DDR_RD_END must decode to 1");
}

void Test_L1_04_FpgaSt_Bit4MeasTrg_DoesNotAffectWrEnd()
{
    // Given: Only the measurement trigger bit is asserted in FPGA_ST.
    const std::array<BYTE, kEp4StatusBufferBytes> ep4Buffer = BuildStatusBuffer(
        0,
        0,
        true,
        true,
        false,
        false,
        true);

    // When: The host decodes the FPGA status word.
    const DdrStatusSnapshot snapshot = WaveAcquisitionEngine::DecodeDdrStatus(ep4Buffer.data(), ep4Buffer.size());

    // Then: Measurement trigger must not pollute DDR write-end decoding.
    TEST_ASSERT(snapshot.ddrWrEnd == 0, "L1-04 DDR_WR_END must remain 0");
    TEST_ASSERT(snapshot.ddrRdEnd == 0, "L1-04 DDR_RD_END must remain 0");
    TEST_ASSERT(FpgaRegLogic::RegGet_SampleStartSt(ep4Buffer.data()), "L1-04 sample start bit must be visible");
}

void Test_L2_01_InitPhase_WaveWrCntStaysZero()
{
    // Given: A DDR model with a two-poll INIT phase before measurement starts.
    SimRunner::FpgaDdrModelConfig config = {};
    config.totalWaveBytes = kBurstSizeBytes * 2u;
    config.burstSizeBytes = kBurstSizeBytes;
    config.producerBurstsPerPoll = 1;
    config.initPollCount = 2;
    SimRunner::FpgaDdrModel model(config);

    // When: The first two EP4 polls are emitted during INIT.
    model.AdvanceOnePoll();
    const std::array<BYTE, kEp4StatusBufferBytes> firstBuffer = BuildStatusBufferFromModel(model);
    const DdrStatusSnapshot firstSnapshot = WaveAcquisitionEngine::DecodeDdrStatus(firstBuffer.data(), firstBuffer.size());
    model.AdvanceOnePoll();
    const std::array<BYTE, kEp4StatusBufferBytes> secondBuffer = BuildStatusBufferFromModel(model);
    const DdrStatusSnapshot secondSnapshot = WaveAcquisitionEngine::DecodeDdrStatus(secondBuffer.data(), secondBuffer.size());

    // Then: No write progress is visible and measurement trigger remains asserted.
    TEST_ASSERT(firstSnapshot.waveWrCnt == 0, "L2-01 first INIT poll must keep WAVE_WR_CNT at zero");
    TEST_ASSERT(secondSnapshot.waveWrCnt == 0, "L2-01 second INIT poll must keep WAVE_WR_CNT at zero");
    TEST_ASSERT(firstSnapshot.ddrWrEnd == 0, "L2-01 first INIT poll must keep DDR_WR_END low");
    TEST_ASSERT(secondSnapshot.ddrWrEnd == 0, "L2-01 second INIT poll must keep DDR_WR_END low");
    TEST_ASSERT(FpgaRegLogic::RegGet_SampleStartSt(firstBuffer.data()), "L2-01 first INIT poll must assert measurement trigger");
    TEST_ASSERT(FpgaRegLogic::RegGet_SampleStartSt(secondBuffer.data()), "L2-01 second INIT poll must assert measurement trigger");
}

void Test_L2_02_MeasPhase_WaveWrCntIncreasesInBurstSteps()
{
    // Given: A DDR model that produces one burst per measurement poll.
    SimRunner::FpgaDdrModelConfig config = {};
    config.totalWaveBytes = kBurstSizeBytes * 3u;
    config.burstSizeBytes = kBurstSizeBytes;
    config.producerBurstsPerPoll = 1;
    config.initPollCount = 1;
    SimRunner::FpgaDdrModel model(config);
    TEST_ASSERT(AdvanceModelUntilState(&model, SimRunner::MeasState::Measuring), "L2-02 measuring state must be reachable");

    // When: Two measurement polls advance the visible DDR write counter.
    model.AdvanceOnePoll();
    const DdrStatusSnapshot firstSnapshot = WaveAcquisitionEngine::DecodeDdrStatus(BuildStatusBufferFromModel(model).data(), kEp4StatusBufferBytes);
    model.AdvanceOnePoll();
    const DdrStatusSnapshot secondSnapshot = WaveAcquisitionEngine::DecodeDdrStatus(BuildStatusBufferFromModel(model).data(), kEp4StatusBufferBytes);

    // Then: WAVE_WR_CNT advances in burst-sized jumps.
    TEST_ASSERT(firstSnapshot.waveWrCnt == (kBurstSizeBytes - static_cast<ULONG>(kDdrCompletionPaddingBytes)), "L2-02 first measurement poll must expose the first burst");
    TEST_ASSERT(secondSnapshot.waveWrCnt == (kBurstSizeBytes * 2u - static_cast<ULONG>(kDdrCompletionPaddingBytes)), "L2-02 second measurement poll must expose two bursts");
}

void Test_L2_03_WaitPhase_DdrWrEndAsserted()
{
    // Given: A DDR model that has finished writing all visible bytes.
    SimRunner::FpgaDdrModelConfig config = {};
    config.totalWaveBytes = kBurstSizeBytes;
    config.burstSizeBytes = kBurstSizeBytes;
    config.producerBurstsPerPoll = 1;
    config.initPollCount = 1;
    config.waitPollCount = 2;
    SimRunner::FpgaDdrModel model(config);
    TEST_ASSERT(AdvanceModelUntilState(&model, SimRunner::MeasState::Wait), "L2-03 wait state must be reachable");

    // When: The host decodes the WAIT-phase EP4 status registers.
    const std::array<BYTE, kEp4StatusBufferBytes> ep4Buffer = BuildStatusBufferFromModel(model);
    const DdrStatusSnapshot snapshot = WaveAcquisitionEngine::DecodeDdrStatus(ep4Buffer.data(), ep4Buffer.size());

    // Then: DDR write completion is visible while DDR read completion remains low.
    TEST_ASSERT(snapshot.ddrWrEnd == 1, "L2-03 WAIT phase must assert DDR_WR_END");
    TEST_ASSERT(snapshot.ddrRdEnd == 0, "L2-03 WAIT phase must keep DDR_RD_END low");
    TEST_ASSERT(!FpgaRegLogic::RegGet_SampleStartSt(ep4Buffer.data()), "L2-03 WAIT phase must deassert measurement trigger");
}

void Test_L2_04_RdWaitPhase_DdrRdEndAssertedWhenCaughtUp()
{
    // Given: A DDR model that has completed writing and later catches up on reads.
    SimRunner::FpgaDdrModelConfig config = {};
    config.totalWaveBytes = kBurstSizeBytes;
    config.burstSizeBytes = kBurstSizeBytes;
    config.producerBurstsPerPoll = 1;
    config.initPollCount = 1;
    config.waitPollCount = 1;
    SimRunner::FpgaDdrModel model(config);
    TEST_ASSERT(AdvanceModelUntilState(&model, SimRunner::MeasState::Wait), "L2-04 wait state must be reachable");
    model.OnEp6ReadCompleted(kBurstSizeBytes);
    TEST_ASSERT(AdvanceModelUntilState(&model, SimRunner::MeasState::RdWait), "L2-04 rd_wait state must be reachable");

    // When: The host decodes the RD_WAIT-phase EP4 status registers after reads catch up.
    const std::array<BYTE, kEp4StatusBufferBytes> ep4Buffer = BuildStatusBufferFromModel(model);
    const DdrStatusSnapshot snapshot = WaveAcquisitionEngine::DecodeDdrStatus(ep4Buffer.data(), ep4Buffer.size());

    // Then: DDR read completion becomes visible.
    TEST_ASSERT(snapshot.ddrWrEnd == 1, "L2-04 RD_WAIT phase must keep DDR_WR_END asserted");
    TEST_ASSERT(snapshot.ddrRdEnd == 1, "L2-04 RD_WAIT phase must assert DDR_RD_END");
}

void Test_L3_01_BurstBoundary_AvailableBytesAlignedToChunk()
{
    // Given: A burst-aligned DDR model after the first full burst completes.
    SimRunner::FpgaDdrModelConfig config = {};
    config.totalWaveBytes = kBurstSizeBytes * 2u;
    config.burstSizeBytes = kBurstSizeBytes;
    config.producerBurstsPerPoll = 1;
    config.initPollCount = 1;
    SimRunner::FpgaDdrModel model(config);
    TEST_ASSERT(AdvanceModelUntilState(&model, SimRunner::MeasState::Measuring), "L3-01 measuring state must be reachable");

    // When: The first measurement poll completes one burst.
    model.AdvanceOnePoll();
    const DdrStatusSnapshot snapshot = WaveAcquisitionEngine::DecodeDdrStatus(BuildStatusBufferFromModel(model).data(), kEp4StatusBufferBytes);

    // Then: The host-visible bytes align exactly to one EP6 chunk after adding the FPGA padding.
    TEST_ASSERT(snapshot.waveWrCnt == (kBurstSizeBytes - static_cast<ULONG>(kDdrCompletionPaddingBytes)), "L3-01 raw write count must expose one burst minus padding");
    TEST_ASSERT(snapshot.waveWrCnt + static_cast<ULONG>(kDdrCompletionPaddingBytes) == kBurstSizeBytes, "L3-01 host-visible bytes must align to one EP6 chunk");
}

void Test_L3_02_PartialWaveAtBurstBoundary_PreservesPendingBytes()
{
    // Given: Waves whose size does not divide evenly into a burst boundary.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = 3000u * 20u;
    usb.producerBurstsPerPoll = 1;
    usb.initPollCount = 1;
    usb.waitPollCount = 1;
    usb.ep6Results = { kUsbSuccess, kUsbSuccess, kUsbSuccess, kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(1500, 1500, 5);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine drains the burst-aligned DDR model.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: All waves are saved without losing the partial-wave remainder between bursts.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "L3-02 terminal status must be success");
    TEST_ASSERT(summary.savedWaveCount == 20, "L3-02 savedWaveCount must be 20");
    TEST_ASSERT(summary.ignoredTailBytes == 0, "L3-02 no tail bytes should remain");
}

void Test_L3_03_LastBurst_PaddedRead_PreservesWaveCount()
{
    // Given: A total byte count whose final DDR-visible read requires alignment padding.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = 1000u * 50u;
    usb.producerBurstsPerPoll = 1;
    usb.initPollCount = 1;
    usb.waitPollCount = 1;
    usb.ep6Results = { kUsbSuccess, kUsbSuccess, kUsbSuccess, kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(500, 500, 10);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine completes the final padded EP6 read.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: All logical waves are preserved and only the alignment tail is ignored.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "L3-03 terminal status must be success");
    TEST_ASSERT(summary.savedWaveCount == 50, "L3-03 savedWaveCount must be 50");
    TEST_ASSERT(summary.ignoredTailBytes == 16, "L3-03 alignment tail must be recorded");
}

void Test_L3_04_DdrRdEnd_ExactBurstMatch()
{
    // Given: Read completion exactly matches the number of written bursts.
    SimRunner::FpgaDdrModelConfig config = {};
    config.totalWaveBytes = kBurstSizeBytes * 2u;
    config.burstSizeBytes = kBurstSizeBytes;
    config.producerBurstsPerPoll = 1;
    config.initPollCount = 1;
    config.waitPollCount = 1;
    SimRunner::FpgaDdrModel model(config);
    TEST_ASSERT(AdvanceModelUntilState(&model, SimRunner::MeasState::Wait), "L3-04 wait state must be reachable");
    model.OnEp6ReadCompleted(kBurstSizeBytes);
    TEST_ASSERT(!BuildStatusBufferFromModel(model).empty(), "L3-04 status buffer helper must be callable");
    const DdrStatusSnapshot beforeCatchUp = WaveAcquisitionEngine::DecodeDdrStatus(BuildStatusBufferFromModel(model).data(), kEp4StatusBufferBytes);
    model.OnEp6ReadCompleted(kBurstSizeBytes);
    TEST_ASSERT(AdvanceModelUntilState(&model, SimRunner::MeasState::RdWait), "L3-04 rd_wait state must be reachable");
    const DdrStatusSnapshot afterCatchUp = WaveAcquisitionEngine::DecodeDdrStatus(BuildStatusBufferFromModel(model).data(), kEp4StatusBufferBytes);

    // Then: DDR_RD_END stays low before the exact match and asserts at equality.
    TEST_ASSERT(beforeCatchUp.ddrRdEnd == 0, "L3-04 DDR_RD_END must stay low before exact burst match");
    TEST_ASSERT(afterCatchUp.ddrRdEnd == 1, "L3-04 DDR_RD_END must assert at exact burst equality");
}

void Test_L3_05_BacklogCalculation_DiscreteJumps()
{
    // Given: A burst-aligned producer exposes data in discrete DDR jumps.
    FakeUsbSession usb = {};
    usb.totalLogicalBytes = kBurstSizeBytes * 3u;
    usb.producerBurstsPerPoll = 1;
    usb.initPollCount = 1;
    usb.waitPollCount = 1;
    usb.ep6Results = { kUsbSuccess, kUsbSuccess, kUsbSuccess };
    FakeWavePairSink sink = {};
    FakeObserver observer = {};
    FakeStopToken stopToken = {};
    RunConfig config = MakeConfig(kLargeWaveSizeLow, kLargeWaveSizeHigh, 2);
    WaveAcquisitionEngine engine(&usb, &sink, &observer, &stopToken);

    // When: The engine processes the burst-aligned acquisition.
    const AcquisitionSummary summary = engine.RunCycle(config);

    // Then: The max backlog reflects the discrete burst-sized jump.
    TEST_ASSERT(summary.terminalStatus == TerminalStatus::Success, "L3-05 terminal status must be success");
    TEST_ASSERT(summary.metrics.maxWaveBacklogBytes == (kBurstSizeBytes - static_cast<ULONG>(kDdrCompletionPaddingBytes)), "L3-05 max backlog must capture one discrete burst jump");
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
    RUN_TEST(Test_TC_A_04_WriteFailure_StopsWithWriteFailedStatus);
    RUN_TEST(Test_TC_B_01_ZeroWavesPerFile_IsRejected);
    RUN_TEST(Test_TC_B_02_ZeroWaveSizes_IsRejected);
    RUN_TEST(Test_TC_B_03_ZeroRetryBudget_FailsOnFirstTimeout);
    RUN_TEST(Test_TC_B_04_RetryBudgetOne_AllowsSingleRecovery);
    RUN_TEST(Test_TC_B_05_StopRequestedBeforeStart_ReturnsStopped);
    RUN_TEST(Test_TC_B_06_ProducerStepBelowPadding_DoesNotUnderflowWaveWriteCount);
    RUN_TEST(Test_TC_B_07_UnalignedMaxReadChunk_IsRejected);
    RUN_TEST(Test_TC_B_08_Ep4PollSleepZero_YieldsBeforeEachPoll);
    RUN_TEST(Test_TC_B_09_Ep4PollSleepPositive_UsesConfiguredDelay);
    RUN_TEST(Test_L1_01_EncodeWaveWrCnt_MatchesFpgaSpec);
    RUN_TEST(Test_L1_02_EncodeWaveWrCnt_ZeroBytes_ReturnsZero);
    RUN_TEST(Test_L1_03_FpgaSt_AllBits_RoundTrip);
    RUN_TEST(Test_L1_04_FpgaSt_Bit4MeasTrg_DoesNotAffectWrEnd);
    RUN_TEST(Test_L2_01_InitPhase_WaveWrCntStaysZero);
    RUN_TEST(Test_L2_02_MeasPhase_WaveWrCntIncreasesInBurstSteps);
    RUN_TEST(Test_L2_03_WaitPhase_DdrWrEndAsserted);
    RUN_TEST(Test_L2_04_RdWaitPhase_DdrRdEndAssertedWhenCaughtUp);
    RUN_TEST(Test_L3_01_BurstBoundary_AvailableBytesAlignedToChunk);
    RUN_TEST(Test_L3_02_PartialWaveAtBurstBoundary_PreservesPendingBytes);
    RUN_TEST(Test_L3_03_LastBurst_PaddedRead_PreservesWaveCount);
    RUN_TEST(Test_L3_04_DdrRdEnd_ExactBurstMatch);
    RUN_TEST(Test_L3_05_BacklogCalculation_DiscreteJumps);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
