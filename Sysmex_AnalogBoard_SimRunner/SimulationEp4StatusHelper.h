#pragma once

#include <windows.h>

#include <cstddef>
#include <cstring>

#include "../AnalogBoard_TestApp/FpgaRegisterEncoding.h"
#include "../AnalogBoard_TestApp/WaveAcquisitionEngine.h"

namespace SimulationEp4StatusHelper
{
    inline void WriteStatusBuffer(
        BYTE* buffer,
        size_t bufferSize,
        ULONG producedLogicalBytes,
        ULONG readLogicalBytes,
        bool ddrLink,
        bool adcSetEnd,
        bool ddrWrEnd,
        bool ddrRdEnd,
        bool measTrg)
    {
        if (buffer == nullptr || bufferSize < WaveAcquisition::kEp4StatusBufferBytes)
        {
            return;
        }

        std::memset(buffer, 0, bufferSize);
        FpgaRegEncoding::EncodeWaveWrCnt(producedLogicalBytes, buffer);
        FpgaRegEncoding::EncodeWaveRdCnt(readLogicalBytes, buffer);
        FpgaRegEncoding::EncodeFpgaSt(ddrLink, adcSetEnd, ddrWrEnd, ddrRdEnd, measTrg, buffer);
    }
}
