#pragma once

#include "WaveDataFileIO.h"

namespace WavePairPublishPolicy
{
    enum class FinalizeOutcome
    {
        kFatal,
        kPublished,
        kRetainedTmpPair,
    };

    inline FinalizeOutcome ClassifyFinalizeOutcome(
        bool closeLowSucceeded,
        bool closeHighSucceeded,
        const WaveDataFileIO::PublishPairResult& publishResult)
    {
        if (!closeLowSucceeded || !closeHighSucceeded)
        {
            return FinalizeOutcome::kFatal;
        }

        if (publishResult.success)
        {
            return FinalizeOutcome::kPublished;
        }

        return FinalizeOutcome::kRetainedTmpPair;
    }
}
