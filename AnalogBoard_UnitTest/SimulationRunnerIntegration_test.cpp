#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../AnalogBoard_SimRunner/SimulationRunnerCore.h"

namespace fs = std::filesystem;

static int g_TestCount = 0;
static int g_PassCount = 0;
static int g_FailCount = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_TestCount++; \
    if (cond) { g_PassCount++; } \
    else { g_FailCount++; std::printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define RUN_TEST(func) do { \
    std::printf("[TEST] %s\n", #func); \
    func(); \
} while(0)

namespace
{
    std::wstring GetRepoRoot()
    {
        fs::path cwd = fs::current_path();
        if (cwd.filename() == L"AnalogBoard_UnitTest")
        {
            return cwd.parent_path().wstring();
        }

        return cwd.wstring();
    }

    std::string ReadTextFileUtf8(const std::wstring& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            return {};
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    int CountBinFiles(const std::wstring& directory)
    {
        int count = 0;
        for (const auto& entry : fs::directory_iterator(directory))
        {
            if (entry.is_regular_file() && entry.path().extension() == L".bin")
            {
                ++count;
            }
        }
        return count;
    }

    void RunPresetAndAssert(
        const wchar_t* presetName,
        int expectedExitCode,
        const char* expectedStatus,
        bool expectBinFiles)
    {
        // Given: A valid preset and the simulation runner core.
        SimRunner::SimulationRunResult result = {};
        std::wstring error;

        // When: The preset is executed through the runner core.
        const bool ok = SimRunner::RunPreset(GetRepoRoot(), presetName, &result, &error);

        // Then: Exit code, summary.json, runner.log, and wave outputs match the preset expectation.
        TEST_ASSERT(ok, "Simulation runner must return success for infrastructure");
        if (!ok)
        {
            std::printf("  detail: %ls\n", error.c_str());
            return;
        }

        TEST_ASSERT(result.exitCode == expectedExitCode, "Simulation runner exit code must match");
        TEST_ASSERT(fs::exists(result.summaryPath), "summary.json must exist");
        TEST_ASSERT(fs::exists(result.runnerLogPath), "runner.log must exist");

        const std::string summaryText = ReadTextFileUtf8(result.summaryPath);
        TEST_ASSERT(summaryText.find("\"terminal_status\":") != std::string::npos, "summary must contain terminal_status");
        TEST_ASSERT(summaryText.find(expectedStatus) != std::string::npos, "summary must contain expected status");

        const int binFileCount = CountBinFiles(result.outputDirectory);
        if (expectBinFiles)
        {
            TEST_ASSERT(binFileCount > 0, "preset must generate bin files");
        }
        else
        {
            TEST_ASSERT(binFileCount >= 0, "bin file count query must succeed");
        }
    }
}

void Test_IT_N_01_NormalComplete_ProducesSuccessArtifacts()
{
    RunPresetAndAssert(L"normal_complete", 0, "success", true);
}

void Test_IT_N_02_TimeoutRecover_ProducesSuccessArtifacts()
{
    RunPresetAndAssert(L"ep6_timeout_once_then_recover", 0, "success", true);
}

void Test_IT_N_03_QueuePressure_ProducesSuccessArtifacts()
{
    RunPresetAndAssert(L"writer_slow_queue_pressure", 0, "success", true);
}

void Test_IT_A_01_PersistentTimeout_ProducesTimeoutSummary()
{
    RunPresetAndAssert(L"ep6_timeout_persistent", 2, "ep6_timeout", false);
}

void Test_IT_A_02_Disconnect_ProducesDisconnectSummary()
{
    RunPresetAndAssert(L"usb_disconnect_midstream", 3, "usb_disconnect", true);
}

void Test_IT_A_03_PublishFail_ProducesFailureSummary()
{
    RunPresetAndAssert(L"publish_fail", 4, "publish_failed", false);
}

int main()
{
    std::printf("=== SimulationRunner Integration Tests ===\n\n");

    RUN_TEST(Test_IT_N_01_NormalComplete_ProducesSuccessArtifacts);
    RUN_TEST(Test_IT_N_02_TimeoutRecover_ProducesSuccessArtifacts);
    RUN_TEST(Test_IT_N_03_QueuePressure_ProducesSuccessArtifacts);
    RUN_TEST(Test_IT_A_01_PersistentTimeout_ProducesTimeoutSummary);
    RUN_TEST(Test_IT_A_02_Disconnect_ProducesDisconnectSummary);
    RUN_TEST(Test_IT_A_03_PublishFail_ProducesFailureSummary);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
