#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace UsbEndpointDiscoveryPolicy
{
    constexpr std::uint8_t kEp2OutAddress = 0x02u;
    constexpr std::uint8_t kEp4InAddress = 0x84u;
    constexpr std::uint8_t kEp6InAddress = 0x86u;
    constexpr std::uint8_t kBulkAttribute = 2u;
    constexpr std::uint8_t kInterruptAttribute = 3u;

    struct ConnectDiagnostic
    {
        const char* event = "unknown";
        int deviceIndex = -1;
        int altIndex = -1;
        int endpointIndex = -1;
        std::uint8_t address = 0u;
        std::uint8_t attributes = 0u;
        bool hasEp2 = false;
        bool hasEp4 = false;
        bool hasEp6 = false;
        int result = 0;
        std::uint32_t usbdStatus = 0u;
        std::uint32_t ntStatus = 0u;
        std::uint32_t lastError = 0u;
        std::uint32_t win32LastError = 0u;
    };

    inline bool FormatConnectDiagnostic(
        const ConnectDiagnostic& diagnostic,
        char* output,
        std::size_t outputSize)
    {
        if (output == nullptr || outputSize == 0u)
        {
            return false;
        }

        const int written = std::snprintf(
            output,
            outputSize,
            "[PR01][DLL][CONNECT] event=%s dev=%d alt=%d endpoint=%d "
            "addr=0x%02X attr=%u ep2=%d ep4=%d ep6=%d result=%d "
            "usbdStatus=0x%08lX ntStatus=0x%08lX lastError=%lu win32LastError=%lu\n",
            diagnostic.event != nullptr ? diagnostic.event : "unknown",
            diagnostic.deviceIndex,
            diagnostic.altIndex,
            diagnostic.endpointIndex,
            static_cast<unsigned int>(diagnostic.address),
            static_cast<unsigned int>(diagnostic.attributes),
            diagnostic.hasEp2 ? 1 : 0,
            diagnostic.hasEp4 ? 1 : 0,
            diagnostic.hasEp6 ? 1 : 0,
            diagnostic.result,
            static_cast<unsigned long>(diagnostic.usbdStatus),
            static_cast<unsigned long>(diagnostic.ntStatus),
            static_cast<unsigned long>(diagnostic.lastError),
            static_cast<unsigned long>(diagnostic.win32LastError));

        if (written < 0 || static_cast<std::size_t>(written) >= outputSize)
        {
            output[outputSize - 1u] = '\0';
            return false;
        }
        return true;
    }

    class EndpointDiscoveryState
    {
    public:
        void VisitEndpoint(
            std::uintptr_t token,
            std::uint8_t address,
            std::uint8_t attributes)
        {
            if (token == 0u)
            {
                return;
            }

            if (attributes == kInterruptAttribute)
            {
                if (address == kEp2OutAddress)
                {
                    ep2Token_ = token;
                    hasEp2_ = true;
                }
                else if (address == kEp4InAddress)
                {
                    ep4Token_ = token;
                    hasEp4_ = true;
                }
            }
            else if (attributes == kBulkAttribute && address == kEp6InAddress)
            {
                ep6Token_ = token;
                hasEp6_ = true;
            }
        }

        bool HasEp2() const { return hasEp2_; }
        bool HasEp4() const { return hasEp4_; }
        bool HasEp6() const { return hasEp6_; }
        bool IsComplete() const { return hasEp2_ && hasEp4_ && hasEp6_; }

        std::uintptr_t Ep2Token() const { return ep2Token_; }
        std::uintptr_t Ep4Token() const { return ep4Token_; }
        std::uintptr_t Ep6Token() const { return ep6Token_; }

    private:
        bool hasEp2_ = false;
        bool hasEp4_ = false;
        bool hasEp6_ = false;
        std::uintptr_t ep2Token_ = 0u;
        std::uintptr_t ep4Token_ = 0u;
        std::uintptr_t ep6Token_ = 0u;
    };

    class AltEndpointSelectionState
    {
    public:
        void ConsiderAlt(int altIndex, const EndpointDiscoveryState& endpoints)
        {
            if (endpoints.IsComplete())
            {
                // Preserve the legacy last-complete-alt tie-break intentionally.
                selectedAltIndex_ = altIndex;
                hasSelection_ = true;
            }
        }

        bool HasSelection() const { return hasSelection_; }
        int SelectedAltIndex() const { return selectedAltIndex_; }

    private:
        bool hasSelection_ = false;
        int selectedAltIndex_ = -1;
    };
}
