#pragma once

#include <windows.h>

#include <cstddef>
#include <new>

namespace UsbTransferHelpers
{
    constexpr DWORD kEp2Ep4MutexWaitTimeoutMs = 5000;

    inline bool IsValidHandle(HANDLE handle)
    {
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    }

    enum class TransferEndpoint
    {
        Ep2,
        Ep4,
        Ep6,
    };

    inline bool RequiresEp2Ep4Mutex(TransferEndpoint endpoint)
    {
        // CyAPI endpoint independence has not been proven for this device/session,
        // so every transfer path stays behind the shared mutex.
        UNREFERENCED_PARAMETER(endpoint);
        return true;
    }

    inline void ResetOverlappedWithEvent(OVERLAPPED* overlapped, HANDLE eventHandle)
    {
        if (overlapped == nullptr)
        {
            return;
        }

        ::ZeroMemory(overlapped, sizeof(*overlapped));
        overlapped->hEvent = eventHandle;
    }

    template <typename ReleaseFunc>
    inline bool ReleaseMutexIfOwned(bool ownsMutex, HANDLE mutexHandle, ReleaseFunc releaseFunc)
    {
        if (!ownsMutex || !IsValidHandle(mutexHandle))
        {
            return false;
        }

        return releaseFunc(mutexHandle) != FALSE;
    }

    inline bool ReleaseMutexIfOwned(bool ownsMutex, HANDLE mutexHandle)
    {
        return ReleaseMutexIfOwned(ownsMutex, mutexHandle, ::ReleaseMutex);
    }

    struct WinHandleCloser
    {
        void operator()(HANDLE handle) const
        {
            if (IsValidHandle(handle))
            {
                ::CloseHandle(handle);
            }
        }
    };

    template <typename Closer = WinHandleCloser>
    class ScopedHandle
    {
    public:
        ScopedHandle() = default;

        explicit ScopedHandle(HANDLE handle, Closer closer = Closer())
            : handle_(handle)
            , closer_(closer)
        {
        }

        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;

        ScopedHandle(ScopedHandle&& other) noexcept
            : handle_(other.handle_)
            , closer_(other.closer_)
        {
            other.handle_ = nullptr;
        }

        ScopedHandle& operator=(ScopedHandle&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                handle_ = other.handle_;
                closer_ = other.closer_;
                other.handle_ = nullptr;
            }
            return *this;
        }

        ~ScopedHandle()
        {
            Reset();
        }

        HANDLE Get() const
        {
            return handle_;
        }

        bool IsValid() const
        {
            return IsValidHandle(handle_);
        }

        void Reset(HANDLE newHandle = nullptr)
        {
            if (IsValid())
            {
                closer_(handle_);
            }
            handle_ = newHandle;
        }

    private:
        HANDLE handle_ = nullptr;
        Closer closer_ = Closer();
    };

    // Not internally synchronized. Callers must hold external synchronization
    // before mixing EnsureSize/Data/Reset across threads.
    class ReusableTransferBuffer
    {
    public:
        ReusableTransferBuffer() = default;

        ReusableTransferBuffer(const ReusableTransferBuffer&) = delete;
        ReusableTransferBuffer& operator=(const ReusableTransferBuffer&) = delete;

        ~ReusableTransferBuffer()
        {
            Reset();
        }

        bool EnsureSize(size_t requiredSize)
        {
            if (requiredSize == 0 || capacity_ >= requiredSize)
            {
                return true;
            }

            BYTE* newBuffer = new (std::nothrow) BYTE[requiredSize];
            if (newBuffer == nullptr)
            {
                return false;
            }

            delete[] buffer_;
            buffer_ = newBuffer;
            capacity_ = requiredSize;
            return true;
        }

        bool ZeroFill(size_t sizeToClear)
        {
            if (sizeToClear == 0)
            {
                return true;
            }

            if (buffer_ == nullptr || capacity_ < sizeToClear)
            {
                return false;
            }

            ::ZeroMemory(buffer_, sizeToClear);
            return true;
        }

        BYTE* Data() const
        {
            return buffer_;
        }

        size_t Capacity() const
        {
            return capacity_;
        }

        void Reset()
        {
            delete[] buffer_;
            buffer_ = nullptr;
            capacity_ = 0;
        }

    private:
        BYTE* buffer_ = nullptr;
        size_t capacity_ = 0;
    };
}
