#pragma once

#include <windows.h>

#include <algorithm>

namespace SimRunner
{
    enum class MeasState
    {
        Idle,
        Init,
        Measuring,
        Wait,
        RdWait,
    };

    struct FpgaDdrModelConfig
    {
        ULONG totalWaveBytes = 0;
        ULONG burstSizeBytes = 0x4000u;
        ULONG producerStepBytes = 0;
        ULONG producerBurstsPerPoll = 0;
        INT initPollCount = 1;
        INT waitPollCount = 1;
        INT startupStaleDdrWrEndPolls = 0;
        ULONG startupStaleWaveWrCntBytes = 0;
    };

    class FpgaDdrModel
    {
    public:
        explicit FpgaDdrModel(const FpgaDdrModelConfig& config)
            : config_(config)
            , startupStaleRemainingPolls_((config.startupStaleDdrWrEndPolls > 0) ? config.startupStaleDdrWrEndPolls : 0)
        {
        }

        FpgaDdrModel() = default;

        void AdvanceOnePoll()
        {
            if (startupStaleRemainingPolls_ > 0)
            {
                startupStaleActive_ = true;
                --startupStaleRemainingPolls_;
                return;
            }

            startupStaleActive_ = false;
            switch (state_)
            {
            case MeasState::Idle:
                state_ = MeasState::Init;
                stateCounter_ = 0;
                break;

            case MeasState::Init:
                ++stateCounter_;
                if (stateCounter_ >= NormalizePollCount(config_.initPollCount))
                {
                    state_ = MeasState::Measuring;
                    stateCounter_ = 0;
                }
                break;

            case MeasState::Measuring:
                AdvanceWriteProgress();
                if (writtenBytes_ >= GetVisibleTotalBytes())
                {
                    state_ = MeasState::Wait;
                    stateCounter_ = 0;
                }
                break;

            case MeasState::Wait:
                ++stateCounter_;
                if (stateCounter_ >= NormalizePollCount(config_.waitPollCount))
                {
                    state_ = MeasState::RdWait;
                    stateCounter_ = 0;
                }
                break;

            case MeasState::RdWait:
                break;
            }
        }

        void OnEp6ReadCompleted(ULONG bytesRead)
        {
            readBytes_ += bytesRead;
        }

        MeasState GetState() const
        {
            return state_;
        }

        ULONG GetWrittenBytes() const
        {
            if (startupStaleActive_)
            {
                return config_.startupStaleWaveWrCntBytes;
            }

            return writtenBytes_;
        }

        ULONG GetReadBytes() const
        {
            if (startupStaleActive_)
            {
                return config_.startupStaleWaveWrCntBytes;
            }

            return readBytes_;
        }

        bool IsAdcSetEnd() const
        {
            return IsStartupStaleDdrWrEndActive() || state_ != MeasState::Idle;
        }

        bool IsDdrWrEnd() const
        {
            return IsStartupStaleDdrWrEndActive() || state_ == MeasState::Wait || state_ == MeasState::RdWait;
        }

        bool IsDdrRdEnd() const
        {
            if (startupStaleActive_)
            {
                return (config_.startupStaleWaveWrCntBytes != 0u);
            }

            return state_ == MeasState::RdWait && readBytes_ >= writtenBytes_;
        }

        bool IsMeasTrg() const
        {
            return IsStartupStaleDdrWrEndActive() || state_ == MeasState::Init || state_ == MeasState::Measuring;
        }

        bool IsDdrWStop() const
        {
            return state_ == MeasState::RdWait;
        }

        bool IsStartupStaleDdrWrEndActive() const
        {
            return startupStaleActive_;
        }

    private:
        static INT NormalizePollCount(INT pollCount)
        {
            return (pollCount > 0) ? pollCount : 1;
        }

        static ULONG AlignUp(ULONG value, ULONG alignment)
        {
            if (value == 0 || alignment == 0)
            {
                return value;
            }

            const ULONG remainder = value % alignment;
            return (remainder == 0) ? value : (value + alignment - remainder);
        }

        ULONG GetVisibleTotalBytes() const
        {
            return AlignUp(config_.totalWaveBytes, 32u);
        }

        void AdvanceWriteProgress()
        {
            if (config_.producerBurstsPerPoll == 0)
            {
                AdvanceLegacyProgress();
                return;
            }

            const ULONG visibleTotal = GetVisibleTotalBytes();
            if (writtenBytes_ >= visibleTotal)
            {
                return;
            }

            ULONG remainingBytes = visibleTotal - writtenBytes_;
            for (ULONG burst = 0; burst < config_.producerBurstsPerPoll && remainingBytes > 0; ++burst)
            {
                const ULONG stepBytes = (std::min)(config_.burstSizeBytes, remainingBytes);
                writtenBytes_ += stepBytes;
                remainingBytes -= stepBytes;
            }
        }

        void AdvanceLegacyProgress()
        {
            const ULONG visibleTotalBytes = GetVisibleTotalBytes();
            if (config_.producerStepBytes == 0)
            {
                writtenBytes_ = visibleTotalBytes;
                return;
            }

            if (writtenBytes_ < visibleTotalBytes)
            {
                writtenBytes_ = (std::min)(visibleTotalBytes, writtenBytes_ + config_.producerStepBytes);
            }
        }

        FpgaDdrModelConfig config_ = {};
        INT startupStaleRemainingPolls_ = 0;
        bool startupStaleActive_ = false;
        MeasState state_ = MeasState::Idle;
        INT stateCounter_ = 0;
        ULONG writtenBytes_ = 0;
        ULONG readBytes_ = 0;
    };
}
