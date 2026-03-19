#pragma once

#include <string>

#include "WaveAcquisitionEngine.h"

namespace AcquisitionLogMessageFormatter
{
    inline std::wstring BuildEngineEnterLog()
    {
        return L"[PR04][ENGINE_ENTER] run_cycle";
    }

    inline std::wstring BuildEngineExitLog(const WaveAcquisition::AcquisitionSummary& summary)
    {
        return
            L"[PR04][ENGINE_EXIT] status=" +
            std::wstring(WaveAcquisition::WaveAcquisitionEngine::ToString(summary.terminalStatus)) +
            L" error=" + std::to_wstring(summary.errorCode) +
            L" savedWaveCount=" + std::to_wstring(summary.savedWaveCount) +
            L" publishedPairs=" + std::to_wstring(summary.publishedPairCount);
    }

    inline std::wstring BuildCycleSummaryLog(const WaveAcquisition::AcquisitionSummary& summary)
    {
        return
            L"[PR01][CYCLE] ep6Calls=" + std::to_wstring(summary.metrics.ep6.callCount) +
            L" ep6Bytes=" + std::to_wstring(summary.metrics.ep6.totalBytes) +
            L" ep6Timeouts=" + std::to_wstring(summary.metrics.ep6TimeoutCount) +
            L" ep6AvgMs=" + std::to_wstring(summary.metrics.GetEp6AverageElapsedMs()) +
            L" ep6MaxMs=" + std::to_wstring(summary.metrics.ep6.maxElapsedMs) +
            L" saveCalls=" + std::to_wstring(summary.metrics.save.callCount) +
            L" saveBytes=" + std::to_wstring(summary.metrics.save.totalBytes) +
            L" saveAvgMs=" + std::to_wstring(summary.metrics.GetSaveAverageElapsedMs()) +
            L" saveMaxMs=" + std::to_wstring(summary.metrics.save.maxElapsedMs) +
            L" ddrPolls=" + std::to_wstring(summary.metrics.ddrStatusPollCount) +
            L" ddrWaitPolls=" + std::to_wstring(summary.metrics.ddrWriteWaitPollCount) +
            L" maxBacklogBytes=" + std::to_wstring(summary.metrics.maxWaveBacklogBytes) +
            L" WAVE_WR_CNT=" + std::to_wstring(summary.metrics.latestWaveWrCnt) +
            L" WAVE_RD_CNT=" + std::to_wstring(summary.metrics.latestWaveRdCnt) +
            L" DDR_WR_END=" + std::to_wstring(summary.metrics.latestDdrWrEnd) +
            L" DDR_RD_END=" + std::to_wstring(summary.metrics.latestDdrRdEnd) +
            L" timeoutReadSize=" + std::to_wstring(summary.metrics.timeout.requestedReadSizeBytes) +
            L" timeoutUnreadBytes=" + std::to_wstring(summary.metrics.timeout.unreadBytes) +
            L" timeoutReadableUpperBoundBytes=" + std::to_wstring(summary.metrics.timeout.readableUpperBoundBytes) +
            L" timeoutBacklogBytes=" + std::to_wstring(summary.metrics.timeout.backlogBytes) +
            L" timeoutWait=" + std::to_wstring(summary.metrics.timeout.waitTimeoutObserved ? 1 : 0) +
            L" timeoutEp4Fail=" + std::to_wstring(summary.metrics.timeout.ep4ReadFailureObserved ? 1 : 0) +
            L" status=" + std::wstring(WaveAcquisition::WaveAcquisitionEngine::ToString(summary.terminalStatus)) +
            L" error=" + std::to_wstring(summary.errorCode) +
            L" ignoredTail=" + std::to_wstring(summary.ignoredTailBytes) +
            L" publishedPairs=" + std::to_wstring(summary.publishedPairCount);
    }
}
