#pragma once

#include <atomic>

class AcquisitionShutdownCoordinator
{
public:
    enum class CloseDecision
    {
        kCloseNow,
        kRequestStop,
        kWaitForFinalization,
    };

    bool TryBeginThread()
    {
        State expected = State::kIdle;
        return state_.compare_exchange_strong(
            expected,
            State::kStartingOrRunning,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    void ThreadStartFailed()
    {
        state_.store(State::kIdle, std::memory_order_release);
    }

    bool ShouldRunThread() const
    {
        return state_.load(std::memory_order_acquire) == State::kStartingOrRunning;
    }

    CloseDecision RequestClose()
    {
        State current = state_.load(std::memory_order_acquire);
        for (;;)
        {
            if (current == State::kIdle)
            {
                return CloseDecision::kCloseNow;
            }
            if (current == State::kStopRequested)
            {
                return CloseDecision::kWaitForFinalization;
            }

            const State next = current == State::kStartingOrRunning
                ? State::kStopRequested
                : State::kIdle;
            if (state_.compare_exchange_weak(
                    current,
                    next,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                return next == State::kStopRequested
                    ? CloseDecision::kRequestStop
                    : CloseDecision::kCloseNow;
            }
        }
    }

    bool ThreadFinalized()
    {
        State current = state_.load(std::memory_order_acquire);
        for (;;)
        {
            if (current == State::kIdle || current == State::kFinalizedClosePending)
            {
                return false;
            }

            const bool closeWasRequested = current == State::kStopRequested;
            const State next = closeWasRequested
                ? State::kFinalizedClosePending
                : State::kIdle;
            if (state_.compare_exchange_weak(
                    current,
                    next,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                return closeWasRequested;
            }
        }
    }

private:
    enum class State
    {
        kIdle,
        kStartingOrRunning,
        kStopRequested,
        kFinalizedClosePending,
    };

    std::atomic<State> state_{ State::kIdle };
};
