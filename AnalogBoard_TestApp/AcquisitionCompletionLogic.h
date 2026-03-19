#pragma once

#include <cstddef>
#include <cstdint>

namespace AcquisitionCompletionLogic
{
    constexpr std::size_t kReadablePaddingBytes = 32u;

    struct Ep4CompletionSnapshot
    {
        std::uint32_t waveWrCnt = 0;
        std::uint32_t waveRdCnt = 0;
        int ddrWrEnd = 0;
        int ddrRdEnd = 0;
    };

    struct Ep4CompletionState
    {
        bool activeCycleObserved = false;
        bool drainingHintSeen = false;
        std::size_t readableUpperBoundBytes = 0;

        void Reset()
        {
            *this = Ep4CompletionState{};
        }
    };

    struct Ep4CompletionDecision
    {
        std::size_t readableUpperBoundBytes = 0;
        std::size_t unreadBytes = 0;
        bool shouldRead = false;
        bool enteredDraining = false;
        bool acquisitionComplete = false;
        bool activeCycleObserved = false;
        bool drainingHintSeen = false;
    };

    inline std::size_t ToReadableUpperBoundBytes(std::uint32_t waveWrCnt)
    {
        return (waveWrCnt == 0u) ? 0u : (static_cast<std::size_t>(waveWrCnt) + kReadablePaddingBytes);
    }

    inline Ep4CompletionDecision ObserveEp4Completion(
        Ep4CompletionState* state,
        const Ep4CompletionSnapshot& snapshot,
        std::size_t savedBytes)
    {
        Ep4CompletionDecision decision;
        if (state == nullptr)
        {
            return decision;
        }

        const std::size_t currentReadableUpperBoundBytes =
            ToReadableUpperBoundBytes(snapshot.waveWrCnt);

        const bool startupStaleCompletionSnapshot =
            !state->activeCycleObserved &&
            savedBytes == 0u &&
            snapshot.ddrRdEnd == 1 &&
            (snapshot.waveWrCnt == 0u || snapshot.ddrWrEnd == 1);
        if (startupStaleCompletionSnapshot)
        {
            decision.activeCycleObserved = state->activeCycleObserved;
            decision.drainingHintSeen = state->drainingHintSeen;
            return decision;
        }

        if (snapshot.ddrWrEnd == 0 || currentReadableUpperBoundBytes != 0u || savedBytes != 0u)
        {
            state->activeCycleObserved = true;
        }

        if (currentReadableUpperBoundBytes > state->readableUpperBoundBytes)
        {
            state->readableUpperBoundBytes = currentReadableUpperBoundBytes;
        }

        if (snapshot.ddrWrEnd == 1 && state->activeCycleObserved)
        {
            decision.enteredDraining = !state->drainingHintSeen;
            state->drainingHintSeen = true;
        }

        decision.readableUpperBoundBytes = state->readableUpperBoundBytes;
        if (decision.readableUpperBoundBytes > savedBytes)
        {
            decision.unreadBytes = decision.readableUpperBoundBytes - savedBytes;
        }
        decision.shouldRead = (decision.unreadBytes != 0u);
        decision.acquisitionComplete =
            (snapshot.ddrRdEnd == 1) &&
            state->activeCycleObserved &&
            !decision.shouldRead;
        decision.activeCycleObserved = state->activeCycleObserved;
        decision.drainingHintSeen = state->drainingHintSeen;
        return decision;
    }
}
