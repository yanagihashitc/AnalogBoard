#pragma once

#include "WaveAcquisitionEngine.h"

namespace AcquisitionCycleRecoveryPolicy
{
    inline bool ShouldContinueRuntimeAfterCycle(
        const bool isManualMode,
        const WaveAcquisition::TerminalStatus terminalStatus)
    {
        if (isManualMode)
        {
            return false;
        }

        return terminalStatus == WaveAcquisition::TerminalStatus::Success;
    }

    inline bool ShouldAttemptStopSamplingAfterCycle(
        const bool isManualMode,
        const WaveAcquisition::TerminalStatus terminalStatus)
    {
        if (isManualMode)
        {
            return true;
        }

        return terminalStatus != WaveAcquisition::TerminalStatus::Success &&
            terminalStatus != WaveAcquisition::TerminalStatus::Stopped &&
            terminalStatus != WaveAcquisition::TerminalStatus::UsbDisconnect &&
            terminalStatus != WaveAcquisition::TerminalStatus::Ep4ReadFailed &&
            terminalStatus != WaveAcquisition::TerminalStatus::InvalidConfig;
    }
}
