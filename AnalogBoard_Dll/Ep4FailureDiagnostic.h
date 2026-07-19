#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace Ep4FailureDiagnostic
{
    constexpr std::uint8_t kExpectedEndpointAddress = 0x84u;

    struct Record
    {
        std::uint64_t monotonicMs = 0u;
        std::uint8_t endpoint = 0u;
        std::int32_t requestedLength = 0;
        std::int32_t returnedLength = 0;
        std::uint32_t usbdStatus = 0u;
        std::uint32_t ntStatus = 0u;
        std::uint32_t cypressLastError = 0u;
        std::uint32_t win32LastError = 0u;
    };

    class SingleRecordBuffer
    {
    public:
        void Capture(const Record& record)
        {
            record_ = record;
            hasRecord_ = true;
        }

        bool Consume(Record* output)
        {
            if (output == nullptr || !hasRecord_)
            {
                return false;
            }

            *output = record_;
            hasRecord_ = false;
            return true;
        }

        void Reset()
        {
            record_ = Record{};
            hasRecord_ = false;
        }

    private:
        Record record_{};
        bool hasRecord_ = false;
    };

    inline bool FormatRecord(const Record& record, char* output, std::size_t outputSize)
    {
        if (output == nullptr || outputSize == 0u)
        {
            return false;
        }

        const int written = std::snprintf(
            output,
            outputSize,
            "[PR01][DLL][EP4_FAILURE] endpoint=0x%02X requestedLength=%ld returnedLength=%ld "
            "usbdStatus=0x%08lX ntStatus=0x%08lX cypressLastError=%lu "
            "win32LastError=%lu monotonicMs=%llu",
            static_cast<unsigned int>(record.endpoint),
            static_cast<long>(record.requestedLength),
            static_cast<long>(record.returnedLength),
            static_cast<unsigned long>(record.usbdStatus),
            static_cast<unsigned long>(record.ntStatus),
            static_cast<unsigned long>(record.cypressLastError),
            static_cast<unsigned long>(record.win32LastError),
            static_cast<unsigned long long>(record.monotonicMs));

        if (written < 0 || static_cast<std::size_t>(written) >= outputSize)
        {
            output[outputSize - 1u] = '\0';
            return false;
        }
        return true;
    }
}
