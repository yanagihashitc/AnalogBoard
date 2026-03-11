#pragma once

#include <windows.h>

#include "FpgaRegisterAddress.h"
#include "FpgaRegisterLogic.h"

namespace FpgaRegEncoding
{
    constexpr ULONG kDdrAddressUnitBytes = 32u;

    constexpr USHORT kFpgaStBitDdrLink = 0x0001u;
    constexpr USHORT kFpgaStBitAdcSetEnd = 0x0002u;
    constexpr USHORT kFpgaStBitDdrWrEnd = 0x0004u;
    constexpr USHORT kFpgaStBitDdrRdEnd = 0x0008u;
    constexpr USHORT kFpgaStBitMeasTrg = 0x0010u;

    inline void EncodeWaveWrCnt(ULONG writtenBytes, BYTE* ep4Buffer)
    {
        const ULONG addrUnits = writtenBytes / kDdrAddressUnitBytes;
        const ULONG ddrLimAddr = (addrUnits > 0u) ? (addrUnits - 1u) : 0u;
        const ULONG regValue = ddrLimAddr * kDdrAddressUnitBytes;

        FpgaRegLogic::Reg_Write(
            FPGAREG_WAVE_WR_CNT_H,
            static_cast<USHORT>((regValue >> 16) & 0xFFFFu),
            ep4Buffer);
        FpgaRegLogic::Reg_Write(
            FPGAREG_WAVE_WR_CNT_L,
            static_cast<USHORT>(regValue & 0xFFFFu),
            ep4Buffer);
    }

    inline void EncodeWaveRdCnt(ULONG readBytes, BYTE* ep4Buffer)
    {
        FpgaRegLogic::Reg_Write(
            FPGAREG_WAVE_RD_CNT_H,
            static_cast<USHORT>((readBytes >> 16) & 0xFFFFu),
            ep4Buffer);
        FpgaRegLogic::Reg_Write(
            FPGAREG_WAVE_RD_CNT_L,
            static_cast<USHORT>(readBytes & 0xFFFFu),
            ep4Buffer);
    }

    inline void EncodeFpgaSt(
        bool ddrLink,
        bool adcSetEnd,
        bool ddrWrEnd,
        bool ddrRdEnd,
        bool measTrg,
        BYTE* ep4Buffer)
    {
        USHORT fpgaStatus = 0;
        if (ddrLink)
        {
            fpgaStatus |= kFpgaStBitDdrLink;
        }
        if (adcSetEnd)
        {
            fpgaStatus |= kFpgaStBitAdcSetEnd;
        }
        if (ddrWrEnd)
        {
            fpgaStatus |= kFpgaStBitDdrWrEnd;
        }
        if (ddrRdEnd)
        {
            fpgaStatus |= kFpgaStBitDdrRdEnd;
        }
        if (measTrg)
        {
            fpgaStatus |= kFpgaStBitMeasTrg;
        }

        FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, fpgaStatus, ep4Buffer);
    }
}
