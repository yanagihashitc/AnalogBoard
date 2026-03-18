#pragma once

#include <windows.h>

#include <algorithm>

namespace AcquisitionPerfMetrics
{
    struct TransferMetrics
    {
        ULONGLONG callCount = 0;
        ULONGLONG totalElapsedMs = 0;
        ULONGLONG maxElapsedMs = 0;
        ULONGLONG totalBytes = 0;

        void Record(ULONGLONG elapsedMs, ULONGLONG bytes)
        {
            ++callCount;
            totalElapsedMs += elapsedMs;
            totalBytes += bytes;
            maxElapsedMs = (std::max)(maxElapsedMs, elapsedMs);
        }

        ULONGLONG AverageElapsedMs() const
        {
            return (callCount > 0) ? (totalElapsedMs / callCount) : 0;
        }
    };

    // Single-thread only. The collector stores plain counters and expects
    // updates/reset to happen on the acquisition thread.
    struct CycleMetrics
    {
        struct TimeoutTelemetry
        {
            bool observed = false;
            bool drainingHintSeen = false;
            ULONG requestedReadSizeBytes = 0;
            ULONGLONG unreadBytes = 0;
            ULONGLONG readableUpperBoundBytes = 0;
            ULONG backlogBytes = 0;
            ULONG waveWrCnt = 0;
            ULONG waveRdCnt = 0;
            INT ddrWrEnd = 0;
            INT ddrRdEnd = 0;
            bool waitTimeoutObserved = false;
            bool ep4ReadFailureObserved = false;
        };

        TransferMetrics ep6;
        TransferMetrics save;
        TimeoutTelemetry timeout;
        ULONG ep6TimeoutCount = 0;
        ULONG ddrStatusPollCount = 0;
        ULONG ddrWriteWaitPollCount = 0;
        ULONG latestWaveWrCnt = 0;
        ULONG latestWaveRdCnt = 0;
        ULONG maxWaveBacklogBytes = 0;
        INT latestDdrWrEnd = 0;
        INT latestDdrRdEnd = 0;

        void Reset()
        {
            *this = CycleMetrics{};
        }

        void RecordEp6Transfer(ULONGLONG elapsedMs, ULONGLONG bytes, bool isTimeout)
        {
            ep6.Record(elapsedMs, bytes);
            if (isTimeout)
            {
                ++ep6TimeoutCount;
            }
        }

        void RecordSaveTransfer(ULONGLONG elapsedMs, ULONGLONG bytes)
        {
            save.Record(elapsedMs, bytes);
        }

        void RecordTimeoutTelemetry(
            ULONG requestedReadSizeBytes,
            ULONGLONG unreadBytes,
            ULONGLONG readableUpperBoundBytes,
            ULONG waveWrCnt,
            ULONG waveRdCnt,
            INT ddrWrEnd,
            INT ddrRdEnd,
            bool drainingHintSeen)
        {
            timeout.observed = true;
            timeout.drainingHintSeen = drainingHintSeen;
            timeout.requestedReadSizeBytes = requestedReadSizeBytes;
            timeout.unreadBytes = unreadBytes;
            timeout.readableUpperBoundBytes = readableUpperBoundBytes;
            timeout.waveWrCnt = waveWrCnt;
            timeout.waveRdCnt = waveRdCnt;
            timeout.ddrWrEnd = ddrWrEnd;
            timeout.ddrRdEnd = ddrRdEnd;
            timeout.backlogBytes = (waveWrCnt >= waveRdCnt) ? (waveWrCnt - waveRdCnt) : 0;
        }

        void MarkTimeoutWaitTimeout()
        {
            if (timeout.observed)
            {
                timeout.waitTimeoutObserved = true;
            }
        }

        void MarkTimeoutEp4ReadFailure()
        {
            if (timeout.observed)
            {
                timeout.ep4ReadFailureObserved = true;
            }
        }

        void IncrementDdrStatusPoll()
        {
            ++ddrStatusPollCount;
        }

        void IncrementDdrWriteWaitPoll()
        {
            ++ddrWriteWaitPollCount;
        }

        void RecordDdrStatus(ULONG waveWrCnt, ULONG waveRdCnt, INT ddrWrEnd, INT ddrRdEnd)
        {
            latestWaveWrCnt = waveWrCnt;
            latestWaveRdCnt = waveRdCnt;
            latestDdrWrEnd = ddrWrEnd;
            latestDdrRdEnd = ddrRdEnd;

            const ULONG backlogBytes = (waveWrCnt >= waveRdCnt) ? (waveWrCnt - waveRdCnt) : 0;
            maxWaveBacklogBytes = (std::max)(maxWaveBacklogBytes, backlogBytes);
        }

        ULONGLONG GetEp6AverageElapsedMs() const
        {
            return ep6.AverageElapsedMs();
        }

        ULONGLONG GetSaveAverageElapsedMs() const
        {
            return save.AverageElapsedMs();
        }
    };
}
