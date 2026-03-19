#pragma once

#include <windows.h>

#include "Ep6TransferTuningPolicy.h"

namespace Ep6TransferRetryPolicy
{
    constexpr int kEp6TimeoutRetryMaxRetries = Ep6TransferTuningPolicy::kEp6TimeoutRetryMaxRetries;
    constexpr DWORD kEp6TimeoutRetryBackoffMs = Ep6TransferTuningPolicy::kEp6TimeoutRetryBackoffMs;

    template <typename TransferFn, typename SleepFn>
    bool ExecuteWithRetry(
        TransferFn transferFn,
        SleepFn sleepFn,
        int* outAttemptCount = nullptr,
        int* outRetryCount = nullptr,
        int maxRetries = kEp6TimeoutRetryMaxRetries,
        DWORD backoffMs = kEp6TimeoutRetryBackoffMs)
    {
        int attemptCount = 0;
        int retryCount = 0;

        while (true)
        {
            ++attemptCount;
            if (transferFn())
            {
                if (outAttemptCount != nullptr)
                {
                    *outAttemptCount = attemptCount;
                }
                if (outRetryCount != nullptr)
                {
                    *outRetryCount = retryCount;
                }
                return true;
            }

            if (retryCount >= maxRetries)
            {
                if (outAttemptCount != nullptr)
                {
                    *outAttemptCount = attemptCount;
                }
                if (outRetryCount != nullptr)
                {
                    *outRetryCount = retryCount;
                }
                return false;
            }

            ++retryCount;
            if (backoffMs > 0)
            {
                sleepFn(backoffMs);
            }
        }
    }
}
