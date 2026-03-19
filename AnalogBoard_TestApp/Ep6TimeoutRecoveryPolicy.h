#pragma once

#include <windows.h>

#include <algorithm>
#include <cstddef>

namespace Ep6TimeoutRecoveryPolicy
{
    constexpr DWORD kFirstTimeoutRetryBackoffMs = 20u;
    constexpr size_t kFirstTimeoutRetryReadClampBytes = 64u * 1024u;

    inline DWORD ResolveRetryBackoffMs(const bool shouldThrottleRetry)
    {
        return shouldThrottleRetry ? kFirstTimeoutRetryBackoffMs : 0u;
    }

    inline size_t ResolveRetryReadSizeBytes(
        const size_t plannedReadSizeBytes,
        const size_t alignmentBytes,
        const bool shouldThrottleRetry)
    {
        if (!shouldThrottleRetry || plannedReadSizeBytes == 0u)
        {
            return plannedReadSizeBytes;
        }

        size_t clampedBytes = (std::min)(plannedReadSizeBytes, kFirstTimeoutRetryReadClampBytes);
        if (alignmentBytes == 0u)
        {
            return clampedBytes;
        }

        const size_t remainder = clampedBytes % alignmentBytes;
        if (remainder != 0u)
        {
            clampedBytes -= remainder;
        }

        if (clampedBytes == 0u)
        {
            return (std::min)(plannedReadSizeBytes, alignmentBytes);
        }

        return clampedBytes;
    }
}
