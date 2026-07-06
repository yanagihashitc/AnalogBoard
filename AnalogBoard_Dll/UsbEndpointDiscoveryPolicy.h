#pragma once

#include <cstdint>

namespace UsbEndpointDiscoveryPolicy
{
    constexpr std::uint8_t kEp2OutAddress = 0x02u;
    constexpr std::uint8_t kEp4InAddress = 0x84u;
    constexpr std::uint8_t kEp6InAddress = 0x86u;
    constexpr std::uint8_t kBulkAttribute = 2u;
    constexpr std::uint8_t kInterruptAttribute = 3u;

    class EndpointDiscoveryState
    {
    public:
        void VisitEndpoint(std::uintptr_t token, std::uint8_t address, std::uint8_t attributes)
        {
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
