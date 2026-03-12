#pragma once

#include <windows.h>

#include <cstddef>
#include <string>

#include "AcquisitionPerfMetrics.h"

namespace WaveAcquisition
{
    constexpr INT kUsbSuccess = 0;
    constexpr INT kUsbErrNoDevice = -1;
    constexpr INT kUsbErrTransferTimeout = -10;

    constexpr INT kAcquisitionErrInvalidConfig = -20001;
    constexpr INT kAcquisitionErrStopped = -20002;
    constexpr INT kAcquisitionErrEp4Read = -20003;
    constexpr INT kAcquisitionErrEp6Read = -20004;
    constexpr INT kAcquisitionErrOpenPair = -20005;
    constexpr INT kAcquisitionErrWritePair = -20006;
    constexpr INT kAcquisitionErrPublishPair = -20007;
    constexpr INT kAcquisitionErrAlignment = -20008;
    constexpr INT kAcquisitionErrEmptyCapture = -20009;

    constexpr size_t kEp4StatusBufferBytes = 128u * 2u;
    constexpr size_t kEp6ReadAlignmentBytes = 0x4000u;
    constexpr size_t kDdrCompletionPaddingBytes = 32u;

    enum class TerminalStatus
    {
        Success,
        InvalidConfig,
        Stopped,
        Ep4ReadFailed,
        Ep6Timeout,
        UsbDisconnect,
        Ep6ReadFailed,
        OpenPairFailed,
        WriteFailed,
        PublishFailed,
        EmptyCapture,
        AlignmentError,
    };

    struct DdrStatusSnapshot
    {
        ULONG waveWrCnt = 0;
        ULONG waveRdCnt = 0;
        INT ddrWrEnd = 0;
        INT ddrRdEnd = 0;
    };

    struct RunConfig
    {
        ULONG waveSizeLow = 0;
        ULONG waveSizeHigh = 0;
        ULONG wavesPerFile = 0;
        ULONG maxReadChunkBytes = 1024u * 1024u * 256u;
        INT ep6TimeoutRetryLimit = 0;
        DWORD ep4PollSleepMs = 0;
    };

    struct AcquisitionSummary
    {
        TerminalStatus terminalStatus = TerminalStatus::InvalidConfig;
        INT errorCode = kAcquisitionErrInvalidConfig;
        AcquisitionPerfMetrics::CycleMetrics metrics;
        ULONG savedWaveCount = 0;
        ULONG ignoredTailBytes = 0;
        INT publishedPairCount = 0;
    };

    class IUsbSession
    {
    public:
        virtual ~IUsbSession() = default;

        virtual INT Connect() = 0;
        virtual void Disconnect() = 0;
        virtual INT EP2_SendData(BYTE* buffer, size_t bufferSize) = 0;
        virtual INT EP4_GetData(BYTE* buffer, size_t bufferSize) = 0;
        virtual INT EP6_GetData(BYTE* buffer, ULONG size) = 0;
    };

    class IWavePairSink
    {
    public:
        virtual ~IWavePairSink() = default;

        virtual INT OpenPair(INT index) = 0;
        virtual INT Write(const BYTE* waveData, ULONG frameSizeLow, ULONG frameSizeHigh, INT waveCnt) = 0;
        virtual INT PublishPair() = 0;
        virtual void AbortPair() = 0;
        virtual bool HasOpenPair() const = 0;
    };

    class IAcquisitionObserver
    {
    public:
        virtual ~IAcquisitionObserver() = default;

        virtual void OnLog(const std::wstring& message) = 0;
        virtual void OnCollectedWaveCount(ULONG collectedWaveCount) = 0;
        virtual void OnCycleSummary(const AcquisitionSummary& summary) = 0;
    };

    class IStopToken
    {
    public:
        virtual ~IStopToken() = default;

        virtual bool IsStopRequested() const = 0;
    };

    class IPollWaiter
    {
    public:
        virtual ~IPollWaiter() = default;

        virtual void Wait(DWORD milliseconds) = 0;
    };

    class WaveAcquisitionEngine
    {
    public:
        WaveAcquisitionEngine(
            IUsbSession* usbSession,
            IWavePairSink* wavePairSink,
            IAcquisitionObserver* observer,
            IStopToken* stopToken,
            IPollWaiter* pollWaiter = nullptr);

        AcquisitionSummary RunCycle(const RunConfig& config);

        static DdrStatusSnapshot DecodeDdrStatus(const BYTE* ep4Buffer, size_t bufferSize);
        static const wchar_t* ToString(TerminalStatus status);

    private:
        IUsbSession* usbSession_ = nullptr;
        IWavePairSink* wavePairSink_ = nullptr;
        IAcquisitionObserver* observer_ = nullptr;
        IStopToken* stopToken_ = nullptr;
        IPollWaiter* pollWaiter_ = nullptr;
    };
}
