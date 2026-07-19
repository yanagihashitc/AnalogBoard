#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace RearmTelemetry
{
    constexpr std::size_t kValidationCapacity = 128;

    struct CycleRecord
    {
        std::uint64_t cycleId = 0;
        bool externalTrigger = false;
        std::uint64_t triggerDetectedMs = 0;
        bool hasDdrRdEndConfirmed = false;
        std::uint64_t ddrRdEndConfirmedMs = 0;
        bool hasHostDrainComplete = false;
        std::uint64_t hostDrainCompleteMs = 0;
        bool hasPublishCleanupComplete = false;
        std::uint64_t publishCleanupCompleteMs = 0;
        bool hasHostReady = false;
        std::uint64_t hostReadyMs = 0;
        bool hasNextExternalTriggerDetected = false;
        std::uint64_t nextExternalTriggerDetectedMs = 0;
    };

    struct DurationSummary
    {
        std::size_t sampleCount = 0;
        std::uint64_t p50Ms = 0;
        std::uint64_t p95Ms = 0;
        std::uint64_t p99Ms = 0;
        std::uint64_t maxMs = 0;
    };

    struct DurationValue
    {
        bool available = false;
        std::uint64_t milliseconds = 0;
    };

    inline DurationValue ReadRearmMs(const CycleRecord& record)
    {
        DurationValue value;
        if (!record.hasDdrRdEndConfirmed ||
            !record.hasHostReady ||
            record.hostReadyMs < record.ddrRdEndConfirmedMs)
        {
            return value;
        }

        value.available = true;
        value.milliseconds = record.hostReadyMs - record.ddrRdEndConfirmedMs;
        return value;
    }

    inline DurationValue ReadExternalWaitMs(const CycleRecord& record)
    {
        DurationValue value;
        if (!record.hasHostReady ||
            !record.hasNextExternalTriggerDetected ||
            record.nextExternalTriggerDetectedMs < record.hostReadyMs)
        {
            return value;
        }

        value.available = true;
        value.milliseconds = record.nextExternalTriggerDetectedMs - record.hostReadyMs;
        return value;
    }

    inline bool TryGetRearmMs(const CycleRecord& record, std::uint64_t* outDurationMs)
    {
        if (outDurationMs == nullptr)
        {
            return false;
        }

        const DurationValue value = ReadRearmMs(record);
        if (value.available)
        {
            *outDurationMs = value.milliseconds;
        }
        return value.available;
    }

    inline bool TryGetExternalWaitMs(const CycleRecord& record, std::uint64_t* outDurationMs)
    {
        if (outDurationMs == nullptr)
        {
            return false;
        }

        const DurationValue value = ReadExternalWaitMs(record);
        if (value.available)
        {
            *outDurationMs = value.milliseconds;
        }
        return value.available;
    }

    template <std::size_t Capacity>
    class Buffer
    {
    public:
        bool StartCycle(std::uint64_t triggerDetectedMs, bool externalTrigger)
        {
            const std::uint64_t cycleId = nextCycleId_++;
            if (externalTrigger && lastStoredIndex_ != kNoIndex)
            {
                CycleRecord& previous = records_[lastStoredIndex_];
                if (!previous.hasNextExternalTriggerDetected)
                {
                    if (previous.hasHostReady && triggerDetectedMs < previous.hostReadyMs)
                    {
                        ++droppedCount_;
                        activeIndex_ = kNoIndex;
                        return false;
                    }

                    previous.hasNextExternalTriggerDetected = true;
                    previous.nextExternalTriggerDetectedMs = triggerDetectedMs;
                }
            }

            if (count_ >= Capacity)
            {
                ++droppedCount_;
                activeIndex_ = kNoIndex;
                return false;
            }

            CycleRecord& record = records_[count_];
            record = CycleRecord{};
            record.cycleId = cycleId;
            record.externalTrigger = externalTrigger;
            record.triggerDetectedMs = triggerDetectedMs;
            activeIndex_ = count_;
            lastStoredIndex_ = count_;
            ++count_;
            return true;
        }

        bool MarkDdrRdEndConfirmed(std::uint64_t timestampMs)
        {
            CycleRecord* record = ActiveRecord();
            if (record == nullptr || record->hasDdrRdEndConfirmed || timestampMs < record->triggerDetectedMs)
            {
                return false;
            }

            record->hasDdrRdEndConfirmed = true;
            record->ddrRdEndConfirmedMs = timestampMs;
            return true;
        }

        bool MarkHostDrainComplete(std::uint64_t timestampMs)
        {
            CycleRecord* record = ActiveRecord();
            if (record == nullptr ||
                record->hasHostDrainComplete ||
                !record->hasDdrRdEndConfirmed ||
                timestampMs < record->ddrRdEndConfirmedMs)
            {
                return false;
            }

            record->hasHostDrainComplete = true;
            record->hostDrainCompleteMs = timestampMs;
            return true;
        }

        bool MarkPublishCleanupComplete(std::uint64_t timestampMs)
        {
            CycleRecord* record = ActiveRecord();
            if (record == nullptr ||
                record->hasPublishCleanupComplete ||
                timestampMs < record->triggerDetectedMs)
            {
                return false;
            }

            record->hasPublishCleanupComplete = true;
            record->publishCleanupCompleteMs = timestampMs;
            return true;
        }

        bool MarkHostReady(std::uint64_t timestampMs)
        {
            CycleRecord* record = ActiveRecord();
            if (record == nullptr ||
                record->hasHostReady ||
                !record->hasDdrRdEndConfirmed ||
                !record->hasHostDrainComplete ||
                !record->hasPublishCleanupComplete ||
                timestampMs < record->ddrRdEndConfirmedMs ||
                timestampMs < record->hostDrainCompleteMs ||
                timestampMs < record->publishCleanupCompleteMs)
            {
                return false;
            }

            record->hasHostReady = true;
            record->hostReadyMs = timestampMs;
            return true;
        }

        std::size_t Count() const
        {
            return count_;
        }

        std::size_t DroppedCount() const
        {
            return droppedCount_;
        }

        const CycleRecord& At(std::size_t index) const
        {
            return records_[index];
        }

        DurationSummary SummarizeRearm() const
        {
            return Summarize(&TryGetRearmMs);
        }

        DurationSummary SummarizeExternalWait() const
        {
            return Summarize(&TryGetExternalWaitMs);
        }

    private:
        using DurationReader = bool (*)(const CycleRecord&, std::uint64_t*);
        static constexpr std::size_t kNoIndex = (std::numeric_limits<std::size_t>::max)();

        CycleRecord* ActiveRecord()
        {
            return (activeIndex_ == kNoIndex) ? nullptr : &records_[activeIndex_];
        }

        DurationSummary Summarize(DurationReader reader) const
        {
            std::array<std::uint64_t, Capacity> durations = {};
            std::size_t sampleCount = 0;
            for (std::size_t i = 0; i < count_; ++i)
            {
                std::uint64_t durationMs = 0;
                if (reader(records_[i], &durationMs))
                {
                    durations[sampleCount++] = durationMs;
                }
            }

            DurationSummary summary;
            summary.sampleCount = sampleCount;
            if (sampleCount == 0)
            {
                return summary;
            }

            std::sort(durations.begin(), durations.begin() + sampleCount);
            summary.p50Ms = NearestRank(durations, sampleCount, 50);
            summary.p95Ms = NearestRank(durations, sampleCount, 95);
            summary.p99Ms = NearestRank(durations, sampleCount, 99);
            summary.maxMs = durations[sampleCount - 1];
            return summary;
        }

        static std::uint64_t NearestRank(
            const std::array<std::uint64_t, Capacity>& sortedValues,
            std::size_t count,
            std::size_t percentile)
        {
            const std::size_t rank = ((percentile * count) + 99) / 100;
            return sortedValues[(rank > 0 ? rank : 1) - 1];
        }

        std::array<CycleRecord, Capacity> records_ = {};
        std::size_t count_ = 0;
        std::size_t droppedCount_ = 0;
        std::size_t activeIndex_ = kNoIndex;
        std::size_t lastStoredIndex_ = kNoIndex;
        std::uint64_t nextCycleId_ = 1;
    };
}
