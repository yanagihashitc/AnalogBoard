#include <windows.h>

#include <iostream>
#include <string>

#include "SimulationRunnerCore.h"

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2)
    {
        std::wcerr << L"Usage: AnalogBoard_SimRunner.exe <preset>\n";
        return 1;
    }

    wchar_t cwd[MAX_PATH] = {};
    ::GetCurrentDirectoryW(MAX_PATH, cwd);

    SimRunner::SimulationRunResult result = {};
    std::wstring error;
    if (!SimRunner::RunPreset(cwd, argv[1], &result, &error))
    {
        std::wcerr << error << L"\n";
        return 1;
    }

    std::wcout
        << L"preset=" << argv[1]
        << L" status=" << WaveAcquisition::WaveAcquisitionEngine::ToString(result.summary.terminalStatus)
        << L" exit_code=" << result.exitCode
        << L" output=" << result.outputDirectory
        << L"\n";
    return result.exitCode;
}
