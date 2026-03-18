#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "TestFramework.h"
#include "../AnalogBoard_SimRunner/SimulationRunnerCore.h"

namespace fs = std::filesystem;

namespace
{
    enum class BinExpectation
    {
        Some,
        None,
        DontCare,
    };

    class ScopedCurrentPath
    {
    public:
        ScopedCurrentPath()
            : originalPath_(fs::current_path())
        {
        }

        ~ScopedCurrentPath()
        {
            std::error_code ignored;
            fs::current_path(originalPath_, ignored);
        }

    private:
        fs::path originalPath_;
    };

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

    bool RemoveDirectoryRecursively(const std::wstring& directory)
    {
        std::error_code error;
        fs::remove_all(directory, error);
        return !error;
    }

    void RunPresetAndAssert(
        const wchar_t* presetName,
        int expectedExitCode,
        const char* expectedStatus,
        BinExpectation binExpectation)
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
        if (binExpectation == BinExpectation::Some)
        {
            TEST_ASSERT(binFileCount > 0, "preset must generate bin files");
        }
        else if (binExpectation == BinExpectation::None)
        {
            TEST_ASSERT(binFileCount == 0, "preset must not generate bin files");
        }

        TEST_ASSERT(RemoveDirectoryRecursively(result.outputDirectory), "simulation output directory cleanup must succeed");
        TEST_ASSERT(!fs::exists(result.outputDirectory), "simulation output directory must be cleaned up after assertions");
    }
}

void Test_IT_B_01_ResolveRepoRootFromExecutablePath_FindsRepositoryRoot()
{
    // Given: The standard x64\Debug executable path under this repository.
    const std::wstring expectedRepoRoot = GetRepoRoot();
    const std::wstring executablePath =
        (fs::path(expectedRepoRoot) / L"x64" / L"Debug" / L"AnalogBoard_SimRunner.exe").wstring();

    // When: The repo root is derived from the executable path.
    const std::wstring resolvedRepoRoot = SimRunner::ResolveRepoRootFromExecutablePath(executablePath);

    // Then: The helper finds the repository root instead of depending on the current working directory.
    TEST_ASSERT(resolvedRepoRoot == expectedRepoRoot, "executable-path resolution must find the repository root");
}

void Test_IT_B_02_ResolveRepoRootFromRelativeExecutablePath_FindsRepositoryRoot()
{
    // Given: A relative executable path from the repository root.
    ScopedCurrentPath scopedCurrentPath = {};
    const std::wstring expectedRepoRoot = GetRepoRoot();
    std::error_code setPathError;
    fs::current_path(expectedRepoRoot, setPathError);
    TEST_ASSERT(!setPathError, "relative-path setup must succeed");
    if (setPathError)
    {
        return;
    }

    const std::wstring relativeExecutablePath =
        (fs::path(L"x64") / L"Debug" / L"AnalogBoard_SimRunner.exe").wstring();

    // When: The repo root is derived from the relative executable path.
    const std::wstring resolvedRepoRoot = SimRunner::ResolveRepoRootFromExecutablePath(relativeExecutablePath);

    // Then: The helper still resolves the repository root correctly.
    TEST_ASSERT(resolvedRepoRoot == expectedRepoRoot, "relative executable path must resolve to the repository root");
}

void Test_IT_N_01_NormalComplete_ProducesSuccessArtifacts()
{
    RunPresetAndAssert(L"normal_complete", 0, "success", BinExpectation::Some);
}

void Test_IT_N_02_TimeoutRecover_ProducesSuccessArtifacts()
{
    RunPresetAndAssert(L"ep6_timeout_once_then_recover", 0, "success", BinExpectation::Some);
}

void Test_IT_N_03_QueuePressure_ProducesSuccessArtifacts()
{
    RunPresetAndAssert(L"writer_slow_queue_pressure", 0, "success", BinExpectation::Some);
}

void Test_IT_A_01_PersistentTimeout_ProducesTimeoutSummary()
{
    RunPresetAndAssert(L"ep6_timeout_persistent", 2, "ep6_timeout", BinExpectation::DontCare);
}

void Test_IT_A_02_Disconnect_ProducesDisconnectSummary()
{
    RunPresetAndAssert(L"usb_disconnect_midstream", 3, "usb_disconnect", BinExpectation::Some);
}

void Test_IT_A_03_PublishFail_ProducesFailureSummary()
{
    RunPresetAndAssert(L"publish_fail", 4, "publish_failed", BinExpectation::DontCare);
}

void Test_IT_A_04_WriteFail_ProducesFailureSummary()
{
    RunPresetAndAssert(L"write_fail", 5, "write_failed", BinExpectation::DontCare);
}

void Test_IT_N_04_SlowProducer_ProducesSuccessArtifacts()
{
    RunPresetAndAssert(L"slow_producer", 0, "success", BinExpectation::Some);
}

void Test_IT_N_05_BurstBoundaryStress_ProducesSuccessArtifacts()
{
    RunPresetAndAssert(L"burst_boundary_stress", 0, "success", BinExpectation::Some);
}

void Test_IT_A_05_EmptyCapture_ProducesFailureSummary()
{
    RunPresetAndAssert(L"empty_capture", 12, "empty_capture", BinExpectation::None);
}

void Test_IT_N_06_StaleDdrWrEndRdWait_LogsInternalWstopLag()
{
    // Given: A preset that starts with stale DDR_WR_END and later enters RD_WAIT.
    SimRunner::SimulationRunResult result = {};
    std::wstring error;

    // When: The preset is executed through the runner core.
    const bool ok = SimRunner::RunPreset(GetRepoRoot(), L"stale_ddrwrend_rdwait", &result, &error);

    // Then: The runner succeeds and the log captures the host/internal lag at drain entry.
    TEST_ASSERT(ok, "stale_ddrwrend_rdwait preset must load and run");
    if (!ok)
    {
        std::printf("  detail: %ls\n", error.c_str());
        return;
    }

    const std::string runnerLog = ReadTextFileUtf8(result.runnerLogPath);
    TEST_ASSERT(runnerLog.find("startup_stale=1") != std::string::npos, "runner log must record startup stale DDR_WR_END");
    TEST_ASSERT(runnerLog.find("startup_stale=1 wave_wr_cnt=16384 wave_rd_cnt=16384 ddr_wr_end=1 ddr_rd_end=1 ddr_wstop=0") != std::string::npos, "runner log must capture stale WAVE_WR_CNT before the new cycle starts");
    TEST_ASSERT(runnerLog.find("ddr_wr_end=1 ddr_rd_end=0 ddr_wstop=0") != std::string::npos, "runner log must capture RD_WAIT entry before internal WSTOP");
    TEST_ASSERT(runnerLog.find("ddr_wstop=1") != std::string::npos, "runner log must later capture internal WSTOP assertion");
    TEST_ASSERT(RemoveDirectoryRecursively(result.outputDirectory), "simulation output directory cleanup must succeed");
}

void Test_IT_A_06_HighDensityTimeoutActive_CapturesTimeoutSnapshot()
{
    // Given: A preset that times out during active acquisition with a large backlog.
    SimRunner::SimulationRunResult result = {};
    std::wstring error;

    // When: The preset is executed through the runner core.
    const bool ok = SimRunner::RunPreset(GetRepoRoot(), L"high_density_timeout_active", &result, &error);

    // Then: Summary and runner log preserve the timeout snapshot and incomplete terminal state.
    TEST_ASSERT(ok, "high_density_timeout_active preset must load and run");
    if (!ok)
    {
        std::printf("  detail: %ls\n", error.c_str());
        return;
    }

    const std::string summaryText = ReadTextFileUtf8(result.summaryPath);
    const std::string runnerLog = ReadTextFileUtf8(result.runnerLogPath);
    TEST_ASSERT(summaryText.find("\"terminal_status\": \"ep6_timeout\"") != std::string::npos, "summary must keep ep6_timeout terminal status");
    TEST_ASSERT(summaryText.find("\"DDR_RD_END\": 0") != std::string::npos, "summary must preserve incomplete DDR_RD_END");
    TEST_ASSERT(summaryText.find("\"timeout_read_size\":") != std::string::npos, "summary must include timeout read size");
    TEST_ASSERT(summaryText.find("\"timeout_stage\": \"active\"") != std::string::npos, "summary must mark the timeout stage as active");
    TEST_ASSERT(runnerLog.find("[timeout] stage=active") != std::string::npos, "runner log must record the timeout stage");
    TEST_ASSERT(runnerLog.find("readable_upper_bound_bytes=") != std::string::npos, "runner log must record timeout upper bound");
    TEST_ASSERT(RemoveDirectoryRecursively(result.outputDirectory), "simulation output directory cleanup must succeed");
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
    RUN_TEST(Test_IT_A_04_WriteFail_ProducesFailureSummary);
    RUN_TEST(Test_IT_A_05_EmptyCapture_ProducesFailureSummary);
    RUN_TEST(Test_IT_N_04_SlowProducer_ProducesSuccessArtifacts);
    RUN_TEST(Test_IT_N_05_BurstBoundaryStress_ProducesSuccessArtifacts);
    RUN_TEST(Test_IT_N_06_StaleDdrWrEndRdWait_LogsInternalWstopLag);
    RUN_TEST(Test_IT_A_06_HighDensityTimeoutActive_CapturesTimeoutSnapshot);
    RUN_TEST(Test_IT_B_01_ResolveRepoRootFromExecutablePath_FindsRepositoryRoot);
    RUN_TEST(Test_IT_B_02_ResolveRepoRootFromRelativeExecutablePath_FindsRepositoryRoot);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
