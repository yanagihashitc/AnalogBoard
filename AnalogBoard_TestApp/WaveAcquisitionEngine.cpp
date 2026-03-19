#include "pch.h"

#include "WaveAcquisitionEngine.h"

#include <algorithm>
#include <thread>

#include "AcquisitionCompletionLogic.h"
#include "Ep6TimeoutRecoveryPolicy.h"
#include "FpgaRegisterLogic.h"

namespace WaveAcquisition
{
    namespace
    {
        struct TimeoutRecoveryObservation
        {
            bool pendingResumeLog = false;
            bool shouldThrottleNextRead = false;
            INT ordinal = 0;
            ULONG requestedReadSizeBytes = 0u;
            ULONGLONG unreadBytes = 0u;
            ULONGLONG readableUpperBoundBytes = 0u;
            DdrStatusSnapshot snapshot = {};
        };

        bool IsStopRequested(IStopToken* stopToken)
        {
            return stopToken != nullptr && stopToken->IsStopRequested();
        }

        void WaitBeforeEp4Poll(IPollWaiter* pollWaiter, DWORD milliseconds)
        {
            if (pollWaiter != nullptr)
            {
                pollWaiter->Wait(milliseconds);
                return;
            }

            ::Sleep(milliseconds);
        }

        void NotifyLog(IAcquisitionObserver* observer, const std::wstring& message)
        {
            if (observer != nullptr)
            {
                observer->OnLog(message);
            }
        }

        void NotifyTimeoutSnapshot(
            IAcquisitionObserver* observer,
            ULONG requestedReadSizeBytes,
            ULONGLONG unreadBytes,
            ULONGLONG readableUpperBoundBytes,
            ULONG waveWrCnt,
            ULONG waveRdCnt,
            INT ddrWrEnd,
            INT ddrRdEnd,
            bool drainingHintSeen)
        {
            if (observer == nullptr)
            {
                return;
            }

            const std::wstring stage = drainingHintSeen ? L"draining" : L"active";
            const std::wstring message =
                L"[PR01][TIMEOUT] readSize=" + std::to_wstring(requestedReadSizeBytes) +
                L" unreadBytes=" + std::to_wstring(unreadBytes) +
                L" readableUpperBoundBytes=" + std::to_wstring(readableUpperBoundBytes) +
                L" backlogBytes=" + std::to_wstring((waveWrCnt >= waveRdCnt) ? (waveWrCnt - waveRdCnt) : 0u) +
                L" WAVE_WR_CNT=" + std::to_wstring(waveWrCnt) +
                L" WAVE_RD_CNT=" + std::to_wstring(waveRdCnt) +
                L" DDR_WR_END=" + std::to_wstring(ddrWrEnd) +
                L" DDR_RD_END=" + std::to_wstring(ddrRdEnd) +
                L" stage=" + stage;
            observer->OnLog(message);
        }

        void NotifyStartupSnapshot(
            IAcquisitionObserver* observer,
            INT pollIndex,
            const DdrStatusSnapshot& snapshot,
            size_t savedDdrBytes)
        {
            if (observer == nullptr)
            {
                return;
            }

            const std::wstring message =
                L"[PR04][STARTUP_EP4] poll=" + std::to_wstring(pollIndex) +
                L" WAVE_WR_CNT=" + std::to_wstring(snapshot.waveWrCnt) +
                L" WAVE_RD_CNT=" + std::to_wstring(snapshot.waveRdCnt) +
                L" DDR_WR_END=" + std::to_wstring(snapshot.ddrWrEnd) +
                L" DDR_RD_END=" + std::to_wstring(snapshot.ddrRdEnd) +
                L" savedBytes=" + std::to_wstring(savedDdrBytes);
            observer->OnLog(message);
        }

        void NotifyTimeoutRecoveryTimeout(
            IAcquisitionObserver* observer,
            INT ordinal,
            ULONG requestedReadSizeBytes,
            ULONGLONG unreadBytes,
            ULONGLONG readableUpperBoundBytes,
            const DdrStatusSnapshot& snapshot)
        {
            if (observer == nullptr)
            {
                return;
            }

            const ULONGLONG backlogBytes =
                (snapshot.waveWrCnt >= snapshot.waveRdCnt) ? (snapshot.waveWrCnt - snapshot.waveRdCnt) : 0u;
            const std::wstring message =
                L"[PR04][TIMEOUT_RECOVERY] phase=timeout" +
                std::wstring(L" ordinal=") + std::to_wstring(ordinal) +
                L" requestedReadSize=" + std::to_wstring(requestedReadSizeBytes) +
                L" unreadBytes=" + std::to_wstring(unreadBytes) +
                L" readableUpperBoundBytes=" + std::to_wstring(readableUpperBoundBytes) +
                L" backlogBytes=" + std::to_wstring(backlogBytes) +
                L" WAVE_WR_CNT=" + std::to_wstring(snapshot.waveWrCnt) +
                L" WAVE_RD_CNT=" + std::to_wstring(snapshot.waveRdCnt) +
                L" DDR_WR_END=" + std::to_wstring(snapshot.ddrWrEnd) +
                L" DDR_RD_END=" + std::to_wstring(snapshot.ddrRdEnd);
            observer->OnLog(message);
        }

        void NotifyTimeoutRecoveryResume(
            IAcquisitionObserver* observer,
            const TimeoutRecoveryObservation& previousTimeout,
            const DdrStatusSnapshot& currentSnapshot,
            ULONGLONG currentUnreadBytes,
            ULONGLONG currentReadableUpperBoundBytes,
            ULONG nextPlannedReadSizeBytes,
            bool drainingHintSeen,
            DWORD retryBackoffMs)
        {
            if (observer == nullptr)
            {
                return;
            }

            const ULONGLONG previousBacklogBytes =
                (previousTimeout.snapshot.waveWrCnt >= previousTimeout.snapshot.waveRdCnt)
                ? (previousTimeout.snapshot.waveWrCnt - previousTimeout.snapshot.waveRdCnt)
                : 0u;
            const ULONGLONG currentBacklogBytes =
                (currentSnapshot.waveWrCnt >= currentSnapshot.waveRdCnt)
                ? (currentSnapshot.waveWrCnt - currentSnapshot.waveRdCnt)
                : 0u;
            const LONGLONG deltaWr =
                static_cast<LONGLONG>(currentSnapshot.waveWrCnt) -
                static_cast<LONGLONG>(previousTimeout.snapshot.waveWrCnt);
            const LONGLONG deltaRd =
                static_cast<LONGLONG>(currentSnapshot.waveRdCnt) -
                static_cast<LONGLONG>(previousTimeout.snapshot.waveRdCnt);
            const LONGLONG deltaBacklog =
                static_cast<LONGLONG>(currentBacklogBytes) -
                static_cast<LONGLONG>(previousBacklogBytes);
            const std::wstring stage = drainingHintSeen ? L"draining" : L"active";
            const std::wstring message =
                L"[PR04][TIMEOUT_RECOVERY] phase=resume" +
                std::wstring(L" ordinal=") + std::to_wstring(previousTimeout.ordinal) +
                L" priorRequestedReadSize=" + std::to_wstring(previousTimeout.requestedReadSizeBytes) +
                L" priorUnreadBytes=" + std::to_wstring(previousTimeout.unreadBytes) +
                L" priorReadableUpperBoundBytes=" + std::to_wstring(previousTimeout.readableUpperBoundBytes) +
                L" currentUnreadBytes=" + std::to_wstring(currentUnreadBytes) +
                L" currentReadableUpperBoundBytes=" + std::to_wstring(currentReadableUpperBoundBytes) +
                L" nextPlannedReadSize=" + std::to_wstring(nextPlannedReadSizeBytes) +
                L" retryBackoffMs=" + std::to_wstring(retryBackoffMs) +
                L" deltaWr=" + std::to_wstring(deltaWr) +
                L" deltaRd=" + std::to_wstring(deltaRd) +
                L" deltaBacklog=" + std::to_wstring(deltaBacklog) +
                L" WAVE_WR_CNT=" + std::to_wstring(currentSnapshot.waveWrCnt) +
                L" WAVE_RD_CNT=" + std::to_wstring(currentSnapshot.waveRdCnt) +
                L" DDR_WR_END=" + std::to_wstring(currentSnapshot.ddrWrEnd) +
                L" DDR_RD_END=" + std::to_wstring(currentSnapshot.ddrRdEnd) +
                L" stage=" + stage;
            observer->OnLog(message);
        }

        void FinalizeSummary(
            IAcquisitionObserver* observer,
            AcquisitionSummary* summary,
            INT settlingPollCount,
            bool sawDdrWrEndClear)
        {
            if (summary == nullptr)
            {
                return;
            }

            summary->settlingPollCount = settlingPollCount;
            summary->sawDdrWrEndClear = sawDdrWrEndClear;
            if (observer != nullptr)
            {
                observer->OnCycleSummary(*summary);
            }
        }

        size_t GetPendingSize(const std::vector<BYTE>& pendingBytes, size_t pendingOffset)
        {
            return (pendingOffset >= pendingBytes.size()) ? 0u : (pendingBytes.size() - pendingOffset);
        }

        void CompactPendingBytes(std::vector<BYTE>* pendingBytes, size_t* pendingOffset)
        {
            if (pendingBytes == nullptr || pendingOffset == nullptr || *pendingOffset == 0u)
            {
                return;
            }

            if (*pendingOffset >= pendingBytes->size())
            {
                pendingBytes->clear();
                *pendingOffset = 0u;
                return;
            }

            pendingBytes->erase(
                pendingBytes->begin(),
                pendingBytes->begin() + static_cast<std::ptrdiff_t>(*pendingOffset));
            *pendingOffset = 0u;
        }

        bool IsConfigValid(const RunConfig& config)
        {
            if (config.wavesPerFile == 0u)
            {
                return false;
            }

            if (config.waveSizeLow == 0u && config.waveSizeHigh == 0u)
            {
                return false;
            }

            if (config.maxReadChunkBytes == 0u)
            {
                return false;
            }

            return (config.maxReadChunkBytes % static_cast<ULONG>(kEp6ReadAlignmentBytes)) == 0u;
        }

        void AbortIfNeeded(IWavePairSink* wavePairSink)
        {
            if (wavePairSink != nullptr && wavePairSink->HasOpenPair())
            {
                wavePairSink->AbortPair();
            }
        }

        bool MapEp6Failure(INT result, AcquisitionSummary* summary)
        {
            if (summary == nullptr || result == kUsbSuccess)
            {
                return false;
            }

            if (result == kUsbErrTransferTimeout)
            {
                summary->terminalStatus = TerminalStatus::Ep6Timeout;
            }
            else if (result == kUsbErrNoDevice || result == USB_ERR_DEVICE_DISCONNECTED)
            {
                summary->terminalStatus = TerminalStatus::UsbDisconnect;
            }
            else
            {
                summary->terminalStatus = TerminalStatus::Ep6ReadFailed;
            }

            summary->errorCode = result;
            return true;
        }

        bool MapWriterFailure(INT errorCode, AcquisitionSummary* summary)
        {
            if (summary == nullptr || errorCode == kUsbSuccess)
            {
                return false;
            }

            switch (errorCode)
            {
            case USB_ERR_QUEUE_FULL_TIMEOUT:
                summary->terminalStatus = TerminalStatus::QueueFullTimeout;
                break;
            case kAcquisitionErrOpenPair:
                summary->terminalStatus = TerminalStatus::OpenPairFailed;
                break;
            case kAcquisitionErrPublishPair:
                summary->terminalStatus = TerminalStatus::PublishFailed;
                break;
            case kAcquisitionErrStopped:
                summary->terminalStatus = TerminalStatus::Stopped;
                break;
            default:
                summary->terminalStatus = TerminalStatus::WriteFailed;
                break;
            }

            summary->errorCode = errorCode;
            return true;
        }

        INT EnqueueReadyChunks(
            const RunConfig& config,
            BlockingQueue<WaveChunk>* queue,
            std::vector<BYTE>* pendingBytes,
            size_t* pendingOffset,
            IStopToken* stopToken,
            const std::atomic<INT>& writerErrorCode)
        {
            if (queue == nullptr || pendingBytes == nullptr || pendingOffset == nullptr)
            {
                return kAcquisitionErrWritePair;
            }

            const size_t oneWaveSize =
                static_cast<size_t>(config.waveSizeLow) + static_cast<size_t>(config.waveSizeHigh);
            while (GetPendingSize(*pendingBytes, *pendingOffset) >= oneWaveSize)
            {
                const ULONG availableWaveCount =
                    static_cast<ULONG>(GetPendingSize(*pendingBytes, *pendingOffset) / oneWaveSize);
                const ULONG chunkWaveCount = (std::min)(config.wavesPerFile, availableWaveCount);
                const size_t chunkBytes = static_cast<size_t>(chunkWaveCount) * oneWaveSize;

                WaveChunk chunk;
                chunk.frameSizeLow = config.waveSizeLow;
                chunk.frameSizeHigh = config.waveSizeHigh;
                chunk.waveCount = static_cast<INT>(chunkWaveCount);
                chunk.payload.assign(
                    pendingBytes->begin() + static_cast<std::ptrdiff_t>(*pendingOffset),
                    pendingBytes->begin() + static_cast<std::ptrdiff_t>(*pendingOffset + chunkBytes));

                if (!queue->Enqueue(std::move(chunk), config.queueWaitTimeoutMs))
                {
                    const INT writerResult = writerErrorCode.load();
                    if (writerResult != kUsbSuccess)
                    {
                        return writerResult;
                    }

                    return IsStopRequested(stopToken) ? kAcquisitionErrStopped : USB_ERR_QUEUE_FULL_TIMEOUT;
                }

                *pendingOffset += chunkBytes;
                if (*pendingOffset >= pendingBytes->size() / 2u)
                {
                    CompactPendingBytes(pendingBytes, pendingOffset);
                }
            }

            return kUsbSuccess;
        }

        std::pair<size_t, size_t> ResolveReadWindowBytes(
            size_t unreadLogicalBytes,
            ULONG maxReadChunkBytes,
            bool drainingHintSeen)
        {
            size_t bytesToRead = (std::min)(static_cast<size_t>(maxReadChunkBytes), unreadLogicalBytes);
            size_t logicalBytesFromRead = bytesToRead;
            if (drainingHintSeen && bytesToRead == unreadLogicalBytes)
            {
                const size_t remainder = bytesToRead % kEp6ReadAlignmentBytes;
                if (remainder != 0u)
                {
                    bytesToRead += kEp6ReadAlignmentBytes - remainder;
                }
            }
            else
            {
                const size_t remainder = bytesToRead % kEp6ReadAlignmentBytes;
                if (remainder != 0u)
                {
                    bytesToRead -= remainder;
                    logicalBytesFromRead = bytesToRead;
                }
            }

            return { bytesToRead, logicalBytesFromRead };
        }
    }

    WaveAcquisitionEngine::WaveAcquisitionEngine(
        IUsbSession* usbSession,
        IWavePairSink* wavePairSink,
        IAcquisitionObserver* observer,
        IStopToken* stopToken,
        IPollWaiter* pollWaiter)
        : usbSession_(usbSession)
        , wavePairSink_(wavePairSink)
        , observer_(observer)
        , stopToken_(stopToken)
        , pollWaiter_(pollWaiter)
    {
    }

    AcquisitionSummary WaveAcquisitionEngine::RunCycle(const RunConfig& config)
    {
        AcquisitionSummary summary = {};
        if (!IsConfigValid(config) || usbSession_ == nullptr || wavePairSink_ == nullptr)
        {
            summary.terminalStatus = TerminalStatus::InvalidConfig;
            summary.errorCode = kAcquisitionErrInvalidConfig;
            FinalizeSummary(observer_, &summary, 0, false);
            return summary;
        }

        if (IsStopRequested(stopToken_))
        {
            summary.terminalStatus = TerminalStatus::Stopped;
            summary.errorCode = kAcquisitionErrStopped;
            FinalizeSummary(observer_, &summary, 0, false);
            return summary;
        }

        const size_t oneWaveSize =
            static_cast<size_t>(config.waveSizeLow) + static_cast<size_t>(config.waveSizeHigh);

        BlockingQueue<WaveChunk> queue(config.queueCapacity);
        std::atomic<INT> writerErrorCode(kUsbSuccess);
        ULONG writerSavedWaveCount = 0u;
        INT writerPublishedPairCount = 0;
        AcquisitionPerfMetrics::TransferMetrics writerSaveMetrics = {};

        std::thread writerThread([&]()
        {
            INT nextPairIndex = 0;
            ULONG currentPairWaveCount = 0u;
            ULONG localSavedWaveCount = 0u;
            INT localPublishedPairCount = 0;

            while (true)
            {
                WaveChunk chunk;
                if (!queue.Dequeue(chunk, config.queueWaitTimeoutMs))
                {
                    if (queue.IsStopRequested() || IsStopRequested(stopToken_))
                    {
                        writerErrorCode.store(kAcquisitionErrStopped);
                        AbortIfNeeded(wavePairSink_);
                        return;
                    }
                    continue;
                }

                if (chunk.isTerminal)
                {
                    break;
                }

                size_t payloadOffset = 0u;
                while (payloadOffset < chunk.payload.size())
                {
                    if (!wavePairSink_->HasOpenPair())
                    {
                        if (wavePairSink_->OpenPair(++nextPairIndex) != kUsbSuccess)
                        {
                            writerErrorCode.store(kAcquisitionErrOpenPair);
                            AbortIfNeeded(wavePairSink_);
                            return;
                        }
                        currentPairWaveCount = 0u;
                    }

                    ULONG roomInCurrentPair = config.wavesPerFile - currentPairWaveCount;
                    const ULONG remainingWaveCount =
                        static_cast<ULONG>((chunk.payload.size() - payloadOffset) / oneWaveSize);
                    const ULONG writeWaveCount = (std::min)(roomInCurrentPair, remainingWaveCount);

                    const ULONGLONG writeStartMs = ::GetTickCount64();
                    const INT writeResult = wavePairSink_->Write(
                        chunk.payload.data() + payloadOffset,
                        chunk.frameSizeLow,
                        chunk.frameSizeHigh,
                        static_cast<INT>(writeWaveCount));
                    const ULONGLONG writeElapsedMs = ::GetTickCount64() - writeStartMs;
                    writerSaveMetrics.Record(
                        writeElapsedMs,
                        static_cast<ULONGLONG>(writeWaveCount) * static_cast<ULONGLONG>(oneWaveSize));

                    if (writeResult != kUsbSuccess)
                    {
                        writerErrorCode.store(kAcquisitionErrWritePair);
                        AbortIfNeeded(wavePairSink_);
                        return;
                    }

                    payloadOffset += static_cast<size_t>(writeWaveCount) * oneWaveSize;
                    currentPairWaveCount += writeWaveCount;
                    localSavedWaveCount += writeWaveCount;
                    writerSavedWaveCount = localSavedWaveCount;
                    if (observer_ != nullptr)
                    {
                        observer_->OnCollectedWaveCount(localSavedWaveCount);
                    }

                    if (currentPairWaveCount == config.wavesPerFile)
                    {
                        if (wavePairSink_->PublishPair() != kUsbSuccess)
                        {
                            writerErrorCode.store(kAcquisitionErrPublishPair);
                            AbortIfNeeded(wavePairSink_);
                            return;
                        }
                        ++localPublishedPairCount;
                        writerPublishedPairCount = localPublishedPairCount;
                        currentPairWaveCount = 0u;
                    }
                }
            }

            if (wavePairSink_->HasOpenPair())
            {
                if (wavePairSink_->PublishPair() != kUsbSuccess)
                {
                    writerErrorCode.store(kAcquisitionErrPublishPair);
                    AbortIfNeeded(wavePairSink_);
                    return;
                }
                ++localPublishedPairCount;
            }

            writerPublishedPairCount = localPublishedPairCount;
        });

        auto JoinWriterAndPopulateSummary = [&]()
        {
            if (writerThread.joinable())
            {
                writerThread.join();
            }
            summary.metrics.save = writerSaveMetrics;
            summary.savedWaveCount = writerSavedWaveCount;
            summary.publishedPairCount = writerPublishedPairCount;
        };

        auto FinishAndReturn = [&](INT settlingPollCount, bool sawDdrWrEndClear) -> AcquisitionSummary
        {
            JoinWriterAndPopulateSummary();
            FinalizeSummary(observer_, &summary, settlingPollCount, sawDdrWrEndClear);
            return summary;
        };

        size_t savedDdrBytes = 0u;
        INT consecutiveTimeoutCount = 0;
        INT startupObservationLogCount = 0;
        INT timeoutRecoveryOrdinal = 0;
        bool sawDdrWrEndClear = false;
        INT settlingPollCount = 0;
        TimeoutRecoveryObservation timeoutRecovery = {};
        AcquisitionCompletionLogic::Ep4CompletionState completionState = {};
        std::vector<BYTE> ep4Buffer(kEp4StatusBufferBytes, 0u);
        std::vector<BYTE> transferBuffer;
        std::vector<BYTE> pendingBytes;
        size_t pendingOffset = 0u;

        while (true)
        {
            if (writerErrorCode.load() != kUsbSuccess)
            {
                queue.RequestStop();
                MapWriterFailure(writerErrorCode.load(), &summary);
                return FinishAndReturn(settlingPollCount, sawDdrWrEndClear);
            }

            if (IsStopRequested(stopToken_))
            {
                queue.RequestStop();
                summary.terminalStatus = TerminalStatus::Stopped;
                summary.errorCode = kAcquisitionErrStopped;
                return FinishAndReturn(settlingPollCount, sawDdrWrEndClear);
            }

            WaitBeforeEp4Poll(pollWaiter_, config.ep4PollSleepMs);
            const INT ep4Result = usbSession_->EP4_GetData(ep4Buffer.data(), ep4Buffer.size());
            if (ep4Result != kUsbSuccess)
            {
                queue.RequestStop();
                summary.terminalStatus = TerminalStatus::Ep4ReadFailed;
                summary.errorCode = ep4Result;
                return FinishAndReturn(settlingPollCount, sawDdrWrEndClear);
            }

            summary.metrics.IncrementDdrStatusPoll();
            const DdrStatusSnapshot snapshot = DecodeDdrStatus(ep4Buffer.data(), ep4Buffer.size());
            summary.metrics.RecordDdrStatus(snapshot.waveWrCnt, snapshot.waveRdCnt, snapshot.ddrWrEnd, snapshot.ddrRdEnd);
            if (snapshot.ddrWrEnd == 0)
            {
                sawDdrWrEndClear = true;
            }

            if (!completionState.activeCycleObserved &&
                savedDdrBytes == 0u &&
                startupObservationLogCount < 3)
            {
                ++startupObservationLogCount;
                NotifyStartupSnapshot(
                    observer_,
                    startupObservationLogCount,
                    snapshot,
                    savedDdrBytes);
            }

            const bool isStartupStalePoll =
                !completionState.activeCycleObserved &&
                savedDdrBytes == 0u &&
                snapshot.ddrWrEnd == 1 &&
                (snapshot.waveWrCnt == 0u || snapshot.ddrRdEnd == 1);
            if (isStartupStalePoll)
            {
                ++settlingPollCount;
                if (settlingPollCount > kDdrSettlingPollLimit)
                {
                    queue.RequestStop();
                    summary.terminalStatus = TerminalStatus::EmptyCapture;
                    summary.errorCode = kAcquisitionErrEmptyCapture;
                    return FinishAndReturn(settlingPollCount, sawDdrWrEndClear);
                }
                continue;
            }

            const AcquisitionCompletionLogic::Ep4CompletionDecision completionDecision =
                AcquisitionCompletionLogic::ObserveEp4Completion(
                    &completionState,
                    { snapshot.waveWrCnt, snapshot.waveRdCnt, snapshot.ddrWrEnd, snapshot.ddrRdEnd },
                    savedDdrBytes);
            if (completionDecision.acquisitionComplete)
            {
                break;
            }

            size_t unreadLogicalBytes = completionDecision.unreadBytes;
            if (unreadLogicalBytes == 0u)
            {
                continue;
            }

            const std::pair<size_t, size_t> readWindow = ResolveReadWindowBytes(
                unreadLogicalBytes,
                config.maxReadChunkBytes,
                completionDecision.drainingHintSeen);
            size_t bytesToRead = readWindow.first;
            size_t logicalBytesFromRead = readWindow.second;
            const bool shouldThrottleRetry = timeoutRecovery.shouldThrottleNextRead;
            const DWORD retryBackoffMs =
                Ep6TimeoutRecoveryPolicy::ResolveRetryBackoffMs(shouldThrottleRetry);
            if (shouldThrottleRetry)
            {
                bytesToRead = Ep6TimeoutRecoveryPolicy::ResolveRetryReadSizeBytes(
                    bytesToRead,
                    kEp6ReadAlignmentBytes,
                    true);
                logicalBytesFromRead = (std::min)(logicalBytesFromRead, bytesToRead);
                timeoutRecovery.shouldThrottleNextRead = false;
            }

            if (timeoutRecovery.pendingResumeLog)
            {
                NotifyTimeoutRecoveryResume(
                    observer_,
                    timeoutRecovery,
                    snapshot,
                    unreadLogicalBytes,
                    completionDecision.readableUpperBoundBytes,
                    static_cast<ULONG>(bytesToRead),
                    completionDecision.drainingHintSeen,
                    retryBackoffMs);
                timeoutRecovery.pendingResumeLog = false;
            }

            if (bytesToRead == 0u)
            {
                continue;
            }

            if ((bytesToRead % kEp6ReadAlignmentBytes) != 0u)
            {
                queue.RequestStop();
                summary.terminalStatus = TerminalStatus::AlignmentError;
                summary.errorCode = kAcquisitionErrAlignment;
                return FinishAndReturn(settlingPollCount, sawDdrWrEndClear);
            }

            if (retryBackoffMs > 0u)
            {
                WaitBeforeEp4Poll(pollWaiter_, retryBackoffMs);
            }

            transferBuffer.resize(bytesToRead);
            const ULONGLONG ep6StartMs = ::GetTickCount64();
            const INT ep6Result = usbSession_->EP6_GetData(transferBuffer.data(), static_cast<ULONG>(bytesToRead));
            const ULONGLONG ep6ElapsedMs = ::GetTickCount64() - ep6StartMs;
            summary.metrics.RecordEp6Transfer(
                ep6ElapsedMs,
                static_cast<ULONGLONG>(bytesToRead),
                ep6Result == kUsbErrTransferTimeout);

            if (ep6Result == kUsbErrTransferTimeout)
            {
                ++timeoutRecoveryOrdinal;
                NotifyTimeoutRecoveryTimeout(
                    observer_,
                    timeoutRecoveryOrdinal,
                    static_cast<ULONG>(bytesToRead),
                    unreadLogicalBytes,
                    completionDecision.readableUpperBoundBytes,
                    snapshot);
                timeoutRecovery.pendingResumeLog = true;
                timeoutRecovery.ordinal = timeoutRecoveryOrdinal;
                timeoutRecovery.requestedReadSizeBytes = static_cast<ULONG>(bytesToRead);
                timeoutRecovery.unreadBytes = unreadLogicalBytes;
                timeoutRecovery.readableUpperBoundBytes = completionDecision.readableUpperBoundBytes;
                timeoutRecovery.snapshot = snapshot;
                timeoutRecovery.shouldThrottleNextRead = true;
                summary.metrics.RecordTimeoutTelemetry(
                    static_cast<ULONG>(bytesToRead),
                    unreadLogicalBytes,
                    completionDecision.readableUpperBoundBytes,
                    snapshot.waveWrCnt,
                    snapshot.waveRdCnt,
                    snapshot.ddrWrEnd,
                    snapshot.ddrRdEnd);
                NotifyTimeoutSnapshot(
                    observer_,
                    static_cast<ULONG>(bytesToRead),
                    unreadLogicalBytes,
                    completionDecision.readableUpperBoundBytes,
                    snapshot.waveWrCnt,
                    snapshot.waveRdCnt,
                    snapshot.ddrWrEnd,
                    snapshot.ddrRdEnd,
                    completionDecision.drainingHintSeen);

                if (consecutiveTimeoutCount >= config.ep6TimeoutRetryLimit)
                {
                    queue.RequestStop();
                    summary.terminalStatus = TerminalStatus::Ep6Timeout;
                    summary.errorCode = ep6Result;
                    return FinishAndReturn(settlingPollCount, sawDdrWrEndClear);
                }

                ++consecutiveTimeoutCount;
                NotifyLog(observer_, L"Recoverable EP6 timeout.");
                continue;
            }

            consecutiveTimeoutCount = 0;
            if (MapEp6Failure(ep6Result, &summary))
            {
                queue.RequestStop();
                return FinishAndReturn(settlingPollCount, sawDdrWrEndClear);
            }

            savedDdrBytes += logicalBytesFromRead;
            pendingBytes.insert(
                pendingBytes.end(),
                transferBuffer.begin(),
                transferBuffer.begin() + static_cast<std::ptrdiff_t>(logicalBytesFromRead));

            const INT enqueueResult = EnqueueReadyChunks(
                config,
                &queue,
                &pendingBytes,
                &pendingOffset,
                stopToken_,
                writerErrorCode);
            if (enqueueResult != kUsbSuccess)
            {
                queue.RequestStop();
                MapWriterFailure(enqueueResult, &summary);
                return FinishAndReturn(settlingPollCount, sawDdrWrEndClear);
            }
        }

        CompactPendingBytes(&pendingBytes, &pendingOffset);
        if (pendingBytes.size() < oneWaveSize)
        {
            summary.ignoredTailBytes = static_cast<ULONG>(pendingBytes.size());
        }

        WaveChunk terminalChunk;
        terminalChunk.isTerminal = true;
        if (!queue.Enqueue(std::move(terminalChunk), config.queueWaitTimeoutMs))
        {
            queue.RequestStop();
            MapWriterFailure(writerErrorCode.load() != kUsbSuccess ? writerErrorCode.load() : USB_ERR_QUEUE_FULL_TIMEOUT, &summary);
            return FinishAndReturn(settlingPollCount, sawDdrWrEndClear);
        }

        JoinWriterAndPopulateSummary();
        if (MapWriterFailure(writerErrorCode.load(), &summary))
        {
            FinalizeSummary(observer_, &summary, settlingPollCount, sawDdrWrEndClear);
            return summary;
        }

        if (summary.savedWaveCount == 0u)
        {
            summary.terminalStatus = TerminalStatus::EmptyCapture;
            summary.errorCode = kAcquisitionErrEmptyCapture;
        }
        else
        {
            summary.terminalStatus = TerminalStatus::Success;
            summary.errorCode = kUsbSuccess;
        }

        FinalizeSummary(observer_, &summary, settlingPollCount, sawDdrWrEndClear);
        return summary;
    }

    DdrStatusSnapshot WaveAcquisitionEngine::DecodeDdrStatus(const BYTE* ep4Buffer, size_t bufferSize)
    {
        DdrStatusSnapshot snapshot = {};
        if (ep4Buffer == nullptr || bufferSize < kEp4StatusBufferBytes)
        {
            return snapshot;
        }

        snapshot.waveWrCnt = FpgaRegLogic::RegGet_DDRWaveCnt(ep4Buffer);
        snapshot.waveRdCnt = FpgaRegLogic::RegGet_DDRReadCnt(ep4Buffer);
        snapshot.ddrWrEnd = FpgaRegLogic::RegGet_DDRWriteEnd(ep4Buffer);
        snapshot.ddrRdEnd = FpgaRegLogic::RegGet_DDRReadEnd(ep4Buffer);
        return snapshot;
    }

    const wchar_t* WaveAcquisitionEngine::ToString(TerminalStatus status)
    {
        switch (status)
        {
        case TerminalStatus::Success: return L"success";
        case TerminalStatus::InvalidConfig: return L"invalid_config";
        case TerminalStatus::Stopped: return L"stopped";
        case TerminalStatus::Ep4ReadFailed: return L"ep4_read_failed";
        case TerminalStatus::Ep6Timeout: return L"ep6_timeout";
        case TerminalStatus::UsbDisconnect: return L"usb_disconnect";
        case TerminalStatus::Ep6ReadFailed: return L"ep6_read_failed";
        case TerminalStatus::QueueFullTimeout: return L"queue_full_timeout";
        case TerminalStatus::OpenPairFailed: return L"open_pair_failed";
        case TerminalStatus::WriteFailed: return L"write_failed";
        case TerminalStatus::PublishFailed: return L"publish_failed";
        case TerminalStatus::EmptyCapture: return L"empty_capture";
        case TerminalStatus::AlignmentError: return L"alignment_error";
        default: return L"unknown";
        }
    }
}
