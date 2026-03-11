#include "WaveAcquisitionEngine.h"

#include <algorithm>
#include <vector>

#include "FpgaRegisterLogic.h"

namespace WaveAcquisition
{
    namespace
    {
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

        void FinalizeSummary(IAcquisitionObserver* observer, const AcquisitionSummary& summary)
        {
            if (observer != nullptr)
            {
                observer->OnCycleSummary(summary);
            }
        }

        size_t GetPendingSize(const std::vector<BYTE>& pendingBytes, size_t pendingOffset)
        {
            if (pendingOffset >= pendingBytes.size())
            {
                return 0;
            }

            return pendingBytes.size() - pendingOffset;
        }

        void CompactPendingBytes(std::vector<BYTE>* pendingBytes, size_t* pendingOffset)
        {
            if (pendingBytes == nullptr || pendingOffset == nullptr)
            {
                return;
            }

            if (*pendingOffset == 0)
            {
                return;
            }

            if (*pendingOffset >= pendingBytes->size())
            {
                pendingBytes->clear();
                *pendingOffset = 0;
                return;
            }

            pendingBytes->erase(pendingBytes->begin(), pendingBytes->begin() + static_cast<std::ptrdiff_t>(*pendingOffset));
            *pendingOffset = 0;
        }

        bool IsConfigValid(const RunConfig& config)
        {
            if (config.wavesPerFile == 0)
            {
                return false;
            }

            if (config.waveSizeLow == 0 && config.waveSizeHigh == 0)
            {
                return false;
            }

            if (config.maxReadChunkBytes == 0)
            {
                return false;
            }

            if ((config.maxReadChunkBytes % static_cast<ULONG>(kEp6ReadAlignmentBytes)) != 0)
            {
                return false;
            }

            return true;
        }

        INT WriteAvailableFrames(
            const RunConfig& config,
            IWavePairSink* wavePairSink,
            IAcquisitionObserver* observer,
            AcquisitionSummary* summary,
            INT* nextPairIndex,
            std::vector<BYTE>* pendingBytes,
            size_t* pendingOffset)
        {
            if (wavePairSink == nullptr || summary == nullptr || nextPairIndex == nullptr || pendingBytes == nullptr || pendingOffset == nullptr)
            {
                return kAcquisitionErrWritePair;
            }

            const size_t oneWaveSize =
                static_cast<size_t>(config.waveSizeLow) + static_cast<size_t>(config.waveSizeHigh);
            if (oneWaveSize == 0)
            {
                return kAcquisitionErrInvalidConfig;
            }

            while (GetPendingSize(*pendingBytes, *pendingOffset) >= oneWaveSize)
            {
                if (!wavePairSink->HasOpenPair())
                {
                    const INT openResult = wavePairSink->OpenPair(++(*nextPairIndex));
                    if (openResult != kUsbSuccess)
                    {
                        return openResult;
                    }
                }

                ULONG roomInCurrentPair = config.wavesPerFile - (summary->savedWaveCount % config.wavesPerFile);
                if (roomInCurrentPair == 0)
                {
                    roomInCurrentPair = config.wavesPerFile;
                }

                const ULONG availableWaveCount = static_cast<ULONG>(GetPendingSize(*pendingBytes, *pendingOffset) / oneWaveSize);
                const ULONG writeWaveCount = (std::min)(roomInCurrentPair, availableWaveCount);

                const ULONGLONG writeStartMs = ::GetTickCount64();
                const INT writeResult = wavePairSink->Write(
                    pendingBytes->data() + *pendingOffset,
                    config.waveSizeLow,
                    config.waveSizeHigh,
                    static_cast<INT>(writeWaveCount));
                const ULONGLONG writeElapsedMs = ::GetTickCount64() - writeStartMs;

                summary->metrics.RecordSaveTransfer(
                    writeElapsedMs,
                    static_cast<ULONGLONG>(writeWaveCount) * static_cast<ULONGLONG>(oneWaveSize));

                if (writeResult != kUsbSuccess)
                {
                    return writeResult;
                }

                *pendingOffset += static_cast<size_t>(writeWaveCount) * oneWaveSize;
                summary->savedWaveCount += writeWaveCount;
                if (observer != nullptr)
                {
                    observer->OnCollectedWaveCount(summary->savedWaveCount);
                }

                if ((summary->savedWaveCount % config.wavesPerFile) == 0)
                {
                    const INT publishResult = wavePairSink->PublishPair();
                    if (publishResult != kUsbSuccess)
                    {
                        return publishResult;
                    }

                    ++summary->publishedPairCount;
                }

                if (*pendingOffset >= pendingBytes->size() / 2)
                {
                    CompactPendingBytes(pendingBytes, pendingOffset);
                }
            }

            return kUsbSuccess;
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
            if (summary == nullptr)
            {
                return false;
            }

            if (result == kUsbErrTransferTimeout)
            {
                summary->terminalStatus = TerminalStatus::Ep6Timeout;
                summary->errorCode = result;
                return true;
            }

            if (result == kUsbErrNoDevice)
            {
                summary->terminalStatus = TerminalStatus::UsbDisconnect;
                summary->errorCode = result;
                return true;
            }

            if (result != kUsbSuccess)
            {
                summary->terminalStatus = TerminalStatus::Ep6ReadFailed;
                summary->errorCode = result;
                return true;
            }

            return false;
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
            FinalizeSummary(observer_, summary);
            return summary;
        }

        if (IsStopRequested(stopToken_))
        {
            summary.terminalStatus = TerminalStatus::Stopped;
            summary.errorCode = kAcquisitionErrStopped;
            FinalizeSummary(observer_, summary);
            return summary;
        }

        const size_t oneWaveSize =
            static_cast<size_t>(config.waveSizeLow) + static_cast<size_t>(config.waveSizeHigh);

        bool ddrWriteCompleted = false;
        size_t maxDdrBytes = 0;
        size_t savedDdrBytes = 0;
        INT nextPairIndex = 0;
        INT consecutiveTimeoutCount = 0;

        std::vector<BYTE> ep4Buffer(kEp4StatusBufferBytes, 0);
        std::vector<BYTE> transferBuffer;
        std::vector<BYTE> pendingBytes;
        size_t pendingOffset = 0;

        while (true)
        {
            if (IsStopRequested(stopToken_))
            {
                summary.terminalStatus = TerminalStatus::Stopped;
                summary.errorCode = kAcquisitionErrStopped;
                AbortIfNeeded(wavePairSink_);
                FinalizeSummary(observer_, summary);
                return summary;
            }

            size_t availableDdrBytes = ddrWriteCompleted ? maxDdrBytes : 0;

            if (!ddrWriteCompleted)
            {
                WaitBeforeEp4Poll(pollWaiter_, config.ep4PollSleepMs);

                const INT ep4Result = usbSession_->EP4_GetData(ep4Buffer.data(), ep4Buffer.size());
                if (ep4Result != kUsbSuccess)
                {
                    summary.terminalStatus = TerminalStatus::Ep4ReadFailed;
                    summary.errorCode = ep4Result;
                    AbortIfNeeded(wavePairSink_);
                    FinalizeSummary(observer_, summary);
                    return summary;
                }

                summary.metrics.IncrementDdrStatusPoll();
                const DdrStatusSnapshot snapshot = DecodeDdrStatus(ep4Buffer.data(), ep4Buffer.size());
                summary.metrics.RecordDdrStatus(
                    snapshot.waveWrCnt,
                    snapshot.waveRdCnt,
                    snapshot.ddrWrEnd,
                    snapshot.ddrRdEnd);

                availableDdrBytes = static_cast<size_t>(snapshot.waveWrCnt);
                if (availableDdrBytes != 0)
                {
                    availableDdrBytes += kDdrCompletionPaddingBytes;
                }

                if (snapshot.ddrWrEnd == 1)
                {
                    ddrWriteCompleted = true;
                    maxDdrBytes = availableDdrBytes;
                }
                else if (availableDdrBytes == 0)
                {
                    continue;
                }
            }

            if (ddrWriteCompleted && savedDdrBytes >= maxDdrBytes)
            {
                break;
            }

            if (availableDdrBytes < savedDdrBytes)
            {
                availableDdrBytes = savedDdrBytes;
            }

            size_t unreadLogicalBytes = availableDdrBytes - savedDdrBytes;
            if (unreadLogicalBytes == 0)
            {
                if (ddrWriteCompleted)
                {
                    break;
                }

                continue;
            }

            size_t bytesToRead = (std::min)(static_cast<size_t>(config.maxReadChunkBytes), unreadLogicalBytes);
            size_t logicalBytesFromRead = bytesToRead;
            if (ddrWriteCompleted && bytesToRead == unreadLogicalBytes)
            {
                const size_t remainder = bytesToRead % kEp6ReadAlignmentBytes;
                if (remainder != 0)
                {
                    bytesToRead += kEp6ReadAlignmentBytes - remainder;
                }
            }

            if (bytesToRead == 0 || (bytesToRead % kEp6ReadAlignmentBytes) != 0)
            {
                summary.terminalStatus = TerminalStatus::AlignmentError;
                summary.errorCode = kAcquisitionErrAlignment;
                AbortIfNeeded(wavePairSink_);
                FinalizeSummary(observer_, summary);
                return summary;
            }

            transferBuffer.resize(bytesToRead);
            const ULONGLONG ep6StartMs = ::GetTickCount64();
            const INT ep6Result = usbSession_->EP6_GetData(
                transferBuffer.data(),
                static_cast<ULONG>(bytesToRead));
            const ULONGLONG ep6ElapsedMs = ::GetTickCount64() - ep6StartMs;

            summary.metrics.RecordEp6Transfer(
                ep6ElapsedMs,
                static_cast<ULONGLONG>(bytesToRead),
                ep6Result == kUsbErrTransferTimeout);

            if (ep6Result == kUsbErrTransferTimeout)
            {
                if (consecutiveTimeoutCount >= config.ep6TimeoutRetryLimit)
                {
                    summary.terminalStatus = TerminalStatus::Ep6Timeout;
                    summary.errorCode = ep6Result;
                    AbortIfNeeded(wavePairSink_);
                    FinalizeSummary(observer_, summary);
                    return summary;
                }

                ++consecutiveTimeoutCount;
                NotifyLog(observer_, L"Recoverable EP6 timeout.");
                continue;
            }

            consecutiveTimeoutCount = 0;
            if (MapEp6Failure(ep6Result, &summary))
            {
                AbortIfNeeded(wavePairSink_);
                FinalizeSummary(observer_, summary);
                return summary;
            }

            savedDdrBytes += logicalBytesFromRead;
            pendingBytes.insert(
                pendingBytes.end(),
                transferBuffer.begin(),
                transferBuffer.begin() + static_cast<std::ptrdiff_t>(logicalBytesFromRead));

            const INT writeResult = WriteAvailableFrames(
                config,
                wavePairSink_,
                observer_,
                &summary,
                &nextPairIndex,
                &pendingBytes,
                &pendingOffset);
            if (writeResult != kUsbSuccess)
            {
                if (writeResult == kAcquisitionErrOpenPair)
                {
                    summary.terminalStatus = TerminalStatus::OpenPairFailed;
                    summary.errorCode = writeResult;
                }
                else if (writeResult == kAcquisitionErrPublishPair)
                {
                    summary.terminalStatus = TerminalStatus::PublishFailed;
                    summary.errorCode = writeResult;
                }
                else
                {
                    summary.terminalStatus = TerminalStatus::WriteFailed;
                    summary.errorCode = writeResult;
                }

                AbortIfNeeded(wavePairSink_);
                FinalizeSummary(observer_, summary);
                return summary;
            }
        }

        CompactPendingBytes(&pendingBytes, &pendingOffset);
        const size_t pendingTailBytes = pendingBytes.size();
        if (pendingTailBytes < oneWaveSize)
        {
            summary.ignoredTailBytes = static_cast<ULONG>(pendingTailBytes);
        }

        if (wavePairSink_->HasOpenPair())
        {
            const INT publishResult = wavePairSink_->PublishPair();
            if (publishResult != kUsbSuccess)
            {
                summary.terminalStatus = TerminalStatus::PublishFailed;
                summary.errorCode = publishResult;
                AbortIfNeeded(wavePairSink_);
                FinalizeSummary(observer_, summary);
                return summary;
            }

            ++summary.publishedPairCount;
        }

        summary.terminalStatus = TerminalStatus::Success;
        summary.errorCode = kUsbSuccess;
        FinalizeSummary(observer_, summary);
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
        case TerminalStatus::Success:
            return L"success";
        case TerminalStatus::InvalidConfig:
            return L"invalid_config";
        case TerminalStatus::Stopped:
            return L"stopped";
        case TerminalStatus::Ep4ReadFailed:
            return L"ep4_read_failed";
        case TerminalStatus::Ep6Timeout:
            return L"ep6_timeout";
        case TerminalStatus::UsbDisconnect:
            return L"usb_disconnect";
        case TerminalStatus::Ep6ReadFailed:
            return L"ep6_read_failed";
        case TerminalStatus::OpenPairFailed:
            return L"open_pair_failed";
        case TerminalStatus::WriteFailed:
            return L"write_failed";
        case TerminalStatus::PublishFailed:
            return L"publish_failed";
        case TerminalStatus::AlignmentError:
            return L"alignment_error";
        default:
            return L"unknown";
        }
    }
}
