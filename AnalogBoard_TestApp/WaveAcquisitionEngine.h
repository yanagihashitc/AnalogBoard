#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "AcquisitionPerfMetrics.h"

#ifndef USB_SUCCESS
#define USB_SUCCESS (0)
#endif

#ifndef USB_ERR_INVALID_OUTPUT_PATH
#define USB_ERR_INVALID_OUTPUT_PATH (-20001)
#endif

#ifndef USB_ERR_OUTPUT_PATH_NOT_FOUND
#define USB_ERR_OUTPUT_PATH_NOT_FOUND (-20002)
#endif

#ifndef USB_ERR_OUTPUT_PATH_NOT_WRITABLE
#define USB_ERR_OUTPUT_PATH_NOT_WRITABLE (-20003)
#endif

#ifndef USB_ERR_INVALID_STATE
#define USB_ERR_INVALID_STATE (-20010)
#endif

#ifndef USB_ERR_DEVICE_DISCONNECTED
#define USB_ERR_DEVICE_DISCONNECTED (-20011)
#endif

#ifndef USB_ERR_THREAD_STOP_TIMEOUT
#define USB_ERR_THREAD_STOP_TIMEOUT (-20012)
#endif

#ifndef USB_ERR_QUEUE_FULL_TIMEOUT
#define USB_ERR_QUEUE_FULL_TIMEOUT (-20013)
#endif

namespace WaveAcquisition
{
    constexpr INT kUsbSuccess = USB_SUCCESS;
    constexpr INT kUsbErrNoDevice = -1;
    constexpr INT kUsbErrTransferTimeout = -10;

    constexpr INT kAcquisitionErrInvalidConfig = -21001;
    constexpr INT kAcquisitionErrStopped = -21002;
    constexpr INT kAcquisitionErrEp4Read = -21003;
    constexpr INT kAcquisitionErrEp6Read = -21004;
    constexpr INT kAcquisitionErrOpenPair = -21005;
    constexpr INT kAcquisitionErrWritePair = -21006;
    constexpr INT kAcquisitionErrPublishPair = -21007;
    constexpr INT kAcquisitionErrAlignment = -21008;
    constexpr INT kAcquisitionErrEmptyCapture = -21009;

    constexpr size_t kEp4StatusBufferBytes = 128u * 2u;
    constexpr size_t kEp6ReadAlignmentBytes = 0x4000u;
    constexpr size_t kDdrCompletionPaddingBytes = 32u;
    constexpr INT kDdrSettlingPollLimit = 200;

    enum class EngineStatus
    {
        Idle,
        Sampling,
        Draining,
        Publish,
        Completed,
        Error
    };

    enum class TerminalStatus
    {
        Success,
        InvalidConfig,
        Stopped,
        Ep4ReadFailed,
        Ep6Timeout,
        UsbDisconnect,
        Ep6ReadFailed,
        QueueFullTimeout,
        OpenPairFailed,
        WriteFailed,
        PublishFailed,
        EmptyCapture,
        AlignmentError,
    };

    struct AcquisitionConfig
    {
        DWORD queueCapacity = 8u;
        DWORD queueWaitTimeoutMs = 200u;
        DWORD ddrPollTimeoutMs = 10000u;
        DWORD stopWaitTimeoutMs = 5000u;
        INT maxUsbRetryCount = 3;
    };

    struct DdrStatusSnapshot
    {
        ULONG waveWrCnt = 0;
        ULONG waveRdCnt = 0;
        INT ddrWrEnd = 0;
        INT ddrRdEnd = 0;
    };

    struct WaveChunk
    {
        std::vector<BYTE> payload;
        ULONG frameSizeLow = 0u;
        ULONG frameSizeHigh = 0u;
        INT waveCount = 0;
        bool isTerminal = false;
    };

    struct RunConfig
    {
        ULONG waveSizeLow = 0u;
        ULONG waveSizeHigh = 0u;
        ULONG wavesPerFile = 0u;
        ULONG maxReadChunkBytes = 1024u * 1024u * 256u;
        INT ep6TimeoutRetryLimit = 0;
        DWORD ep4PollSleepMs = 0u;
        DWORD queueCapacity = 8u;
        DWORD queueWaitTimeoutMs = 200u;
    };

    struct AcquisitionSummary
    {
        TerminalStatus terminalStatus = TerminalStatus::InvalidConfig;
        INT errorCode = kAcquisitionErrInvalidConfig;
        AcquisitionPerfMetrics::CycleMetrics metrics;
        ULONG savedWaveCount = 0u;
        ULONG ignoredTailBytes = 0u;
        INT publishedPairCount = 0;
        INT settlingPollCount = 0;
        bool sawDdrWrEndClear = false;
    };

    template <typename T>
    class BlockingQueue
    {
    public:
        explicit BlockingQueue(const DWORD capacity)
            : capacity_(capacity > 0u ? static_cast<size_t>(capacity) : 1u)
        {
        }

        bool Enqueue(T&& item, const DWORD timeoutMs)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto canWrite = [this]()
            {
                return stopRequested_ || queue_.size() < capacity_;
            };

            if (!WaitForWritable(lock, timeoutMs, canWrite))
            {
                return false;
            }

            if (stopRequested_)
            {
                return false;
            }

            queue_.push_back(std::move(item));
            notEmptyCv_.notify_one();
            return true;
        }

        bool Dequeue(T& out, const DWORD timeoutMs)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto canRead = [this]()
            {
                return stopRequested_ || !queue_.empty();
            };

            if (!WaitForReadable(lock, timeoutMs, canRead))
            {
                return false;
            }

            if (stopRequested_ || queue_.empty())
            {
                return false;
            }

            out = std::move(queue_.front());
            queue_.pop_front();
            notFullCv_.notify_one();
            return true;
        }

        void RequestStop()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopRequested_ = true;
            notEmptyCv_.notify_all();
            notFullCv_.notify_all();
        }

        bool IsStopRequested() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return stopRequested_;
        }

        size_t Size() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.size();
        }

        size_t Capacity() const
        {
            return capacity_;
        }

    private:
        template <typename Predicate>
        bool WaitForWritable(
            std::unique_lock<std::mutex>& lock,
            const DWORD timeoutMs,
            Predicate&& predicate)
        {
            if (predicate())
            {
                return true;
            }

            if (timeoutMs == 0u)
            {
                return false;
            }

            return notFullCv_.wait_for(
                lock,
                std::chrono::milliseconds(timeoutMs),
                std::forward<Predicate>(predicate));
        }

        template <typename Predicate>
        bool WaitForReadable(
            std::unique_lock<std::mutex>& lock,
            const DWORD timeoutMs,
            Predicate&& predicate)
        {
            if (predicate())
            {
                return true;
            }

            if (timeoutMs == 0u)
            {
                return false;
            }

            return notEmptyCv_.wait_for(
                lock,
                std::chrono::milliseconds(timeoutMs),
                std::forward<Predicate>(predicate));
        }

        std::deque<T> queue_;
        const size_t capacity_;
        mutable std::mutex mutex_;
        std::condition_variable notEmptyCv_;
        std::condition_variable notFullCv_;
        bool stopRequested_ = false;
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
