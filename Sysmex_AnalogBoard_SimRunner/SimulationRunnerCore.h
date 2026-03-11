#pragma once

#include <string>

#include "../AnalogBoard_TestApp/WaveAcquisitionEngine.h"

namespace SimRunner
{
    struct SimulationRunResult
    {
        int exitCode = 1;
        std::wstring outputDirectory;
        std::wstring runnerLogPath;
        std::wstring summaryPath;
        WaveAcquisition::AcquisitionSummary summary;
    };

    std::wstring ResolveRepoRootFromExecutablePath(const std::wstring& executablePath);

    int ExitCodeFromStatus(WaveAcquisition::TerminalStatus status);

    bool RunPreset(
        const std::wstring& repoRoot,
        const std::wstring& presetName,
        SimulationRunResult* outResult,
        std::wstring* outError);
}
