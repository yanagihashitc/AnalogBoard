#pragma once

#include <climits>
#include <windows.h>

namespace ReadRequestBurstPolicy
{
    constexpr ULONG kUsbTransferAlignmentBytes = 0x4000;
    constexpr int kMaxFilesPerReadBurst = 4;

    inline ULONG AlignUpToUsbTransferBoundary(ULONGLONG valueBytes)
    {
        if (valueBytes == 0)
        {
            return 0;
        }

        const ULONGLONG alignment = static_cast<ULONGLONG>(kUsbTransferAlignmentBytes);
        const ULONGLONG aligned =
            ((valueBytes + alignment - 1) / alignment) * alignment;
        if (aligned > static_cast<ULONGLONG>(ULONG_MAX))
        {
            return ULONG_MAX;
        }

        return static_cast<ULONG>(aligned);
    }

    inline ULONG ResolveReadBurstCapBytes(
        ULONG oneFileSizeBytes,
        ULONG legacyCapBytes,
        int maxFilesPerBurst = kMaxFilesPerReadBurst)
    {
        if (legacyCapBytes == 0)
        {
            return 0;
        }

        if (oneFileSizeBytes == 0 || maxFilesPerBurst <= 0)
        {
            return legacyCapBytes;
        }

        const ULONGLONG desiredBurstBytes =
            static_cast<ULONGLONG>(oneFileSizeBytes) * static_cast<ULONGLONG>(maxFilesPerBurst);
        const ULONG alignedBurstBytes = AlignUpToUsbTransferBoundary(desiredBurstBytes);
        if (alignedBurstBytes == 0)
        {
            return legacyCapBytes;
        }

        return alignedBurstBytes < legacyCapBytes ? alignedBurstBytes : legacyCapBytes;
    }
}
