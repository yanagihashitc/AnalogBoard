#pragma once

namespace ExternalTriggerPollingPolicy
{
    constexpr unsigned int kIdlePollIntervalMs = 10u;

    enum class Action
    {
        kTransferFailed,
        kTriggered,
        kModeChanged,
        kCancelled,
        kContinueAfterDelay,
    };

    struct Decision
    {
        Action action;
        unsigned int delayMs;
    };

    inline Decision Evaluate(
        bool transferSucceeded,
        bool triggerDetected,
        bool manualModeChanged,
        bool samplingRequested,
        bool applicationShutdownRequested = false)
    {
        if (!transferSucceeded)
        {
            return { Action::kTransferFailed, 0u };
        }
        if (applicationShutdownRequested)
        {
            return { Action::kCancelled, 0u };
        }
        if (triggerDetected)
        {
            return { Action::kTriggered, 0u };
        }
        if (manualModeChanged)
        {
            return { Action::kModeChanged, 0u };
        }
        if (!samplingRequested)
        {
            return { Action::kCancelled, 0u };
        }
        return { Action::kContinueAfterDelay, kIdlePollIntervalMs };
    }
}
