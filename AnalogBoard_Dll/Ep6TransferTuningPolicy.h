#pragma once

#include <windows.h>

namespace Ep6TransferTuningPolicy
{
    constexpr ULONG kEp6BulkEndpointTimeoutMs = 30000u;
    constexpr int kEp6TimeoutRetryMaxRetries = 1;
    constexpr DWORD kEp6TimeoutRetryBackoffMs = 5u;

    inline void ApplyBulkInDefaults(ULONG* timeoutMs)
    {
        if (timeoutMs != nullptr)
        {
            *timeoutMs = kEp6BulkEndpointTimeoutMs;
        }
    }
}
