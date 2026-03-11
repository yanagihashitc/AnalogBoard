#pragma once

#include <windows.h>

#include <cstddef>
#include <cstring>

#include "../AnalogBoard_TestApp/FpgaRegisterLogic.h"
#include "../AnalogBoard_TestApp/WaveAcquisitionEngine.h"

namespace SimulationEp4StatusHelper
{
    inline ULONG ClampLogicalBytesToRegisterCount(ULONG logicalBytes)
    {
        return logicalBytes > static_cast<ULONG>(WaveAcquisition::kDdrCompletionPaddingBytes)
            ? logicalBytes - static_cast<ULONG>(WaveAcquisition::kDdrCompletionPaddingBytes)
            : 0;
    }

    inline void WriteStatusBuffer(
        BYTE* buffer,
        size_t bufferSize,
        ULONG producedLogicalBytes,
        ULONG readLogicalBytes,
        ULONG totalLogicalBytes)
    {
        if (buffer == nullptr || bufferSize < WaveAcquisition::kEp4StatusBufferBytes)
        {
            return;
        }

        std::memset(buffer, 0, bufferSize);

        const ULONG registerWaveWrCnt = ClampLogicalBytesToRegisterCount(producedLogicalBytes);
        const ULONG registerWaveRdCnt = ClampLogicalBytesToRegisterCount(readLogicalBytes);

        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_H, static_cast<USHORT>((registerWaveWrCnt >> 16) & 0xFFFF), buffer);
        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_WR_CNT_L, static_cast<USHORT>(registerWaveWrCnt & 0xFFFF), buffer);
        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_RD_CNT_H, static_cast<USHORT>((registerWaveRdCnt >> 16) & 0xFFFF), buffer);
        FpgaRegLogic::Reg_Write(FPGAREG_WAVE_RD_CNT_L, static_cast<USHORT>(registerWaveRdCnt & 0xFFFF), buffer);

        USHORT fpgaStatus = 0;
        if (producedLogicalBytes >= totalLogicalBytes && totalLogicalBytes != 0)
        {
            fpgaStatus |= 0x4;
        }
        if (readLogicalBytes >= totalLogicalBytes && totalLogicalBytes != 0)
        {
            fpgaStatus |= 0x8;
        }

        FpgaRegLogic::Reg_Write(FPGAREG_FPGA_ST, fpgaStatus, buffer);
    }
}
