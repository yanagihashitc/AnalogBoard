#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace SimRunner
{
    enum class Ep6ResultKind
    {
        Success,
        Timeout,
        Disconnect,
    };

    struct SimulationScenario
    {
        std::wstring presetName;
        ULONG waveSizeLow = 0;
        ULONG waveSizeHigh = 0;
        ULONG wavesPerFile = 0;
        ULONG totalWaveCount = 0;
        ULONG producerStepBytes = 0;
        ULONG maxReadChunkBytes = 0x4000u;
        INT timeoutRetryLimit = 0;
        DWORD writeDelayMs = 0;
        INT writeFailAt = 0;
        INT publishFailAt = 0;
        std::vector<Ep6ResultKind> ep6Results;
    };

    bool LoadScenarioFromFile(
        const std::wstring& path,
        SimulationScenario* outScenario,
        std::wstring* outError);
}
