#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "TestFramework.h"
#include "../AnalogBoard_SimRunner/SimulationScenario.h"

namespace fs = std::filesystem;

namespace
{
    class ScopedTempScenarioFile
    {
    public:
        explicit ScopedTempScenarioFile(const char* jsonText)
        {
            wchar_t tempPath[MAX_PATH] = {};
            wchar_t tempFile[MAX_PATH] = {};
            ::GetTempPathW(MAX_PATH, tempPath);
            ::GetTempFileNameW(tempPath, L"scn", 0, tempFile);
            path_ = tempFile;

            std::ofstream output(path_, std::ios::out | std::ios::trunc | std::ios::binary);
            output << jsonText;
        }

        ~ScopedTempScenarioFile()
        {
            if (!path_.empty())
            {
                std::error_code ignored;
                fs::remove(path_, ignored);
            }
        }

        const std::wstring& GetPath() const
        {
            return path_;
        }

    private:
        std::wstring path_;
    };
}

void Test_TC_A_04_NegativeUnsignedField_IsRejected()
{
    // Given: A scenario JSON with a negative unsigned field.
    const char* json = R"({
  "wave_size_low": 32,
  "wave_size_high": 32,
  "waves_per_file": 2,
  "total_wave_count": 4,
  "producer_step_bytes": 256,
  "max_read_chunk_bytes": 16384,
  "timeout_retry_limit": 0,
  "write_delay_ms": -1,
  "write_fail_at": 0,
  "publish_fail_at": 0,
  "ep6_results": ["success"]
})";
    ScopedTempScenarioFile tempFile(json);
    SimRunner::SimulationScenario scenario = {};
    std::wstring error;

    // When: The scenario file is loaded.
    const bool ok = SimRunner::LoadScenarioFromFile(tempFile.GetPath(), &scenario, &error);

    // Then: Loading fails with a field-specific validation error.
    TEST_ASSERT(!ok, "TC-A-04 load must fail");
    TEST_ASSERT(error.find(L"write_delay_ms") != std::wstring::npos, "TC-A-04 error must mention write_delay_ms");
    TEST_ASSERT(scenario.writeDelayMs == 0, "TC-A-04 output scenario must remain default");
}

void Test_TC_N_01_ValidScenario_LoadsSuccessfully()
{
    // Given: A scenario JSON with valid unsigned fields.
    const char* json = R"({
  "wave_size_low": 32,
  "wave_size_high": 32,
  "waves_per_file": 2,
  "total_wave_count": 4,
  "producer_step_bytes": 256,
  "max_read_chunk_bytes": 16384,
  "timeout_retry_limit": 0,
  "write_delay_ms": 5,
  "write_fail_at": 0,
  "publish_fail_at": 0,
  "ep6_results": ["success"]
})";
    ScopedTempScenarioFile tempFile(json);
    SimRunner::SimulationScenario scenario = {};
    std::wstring error;

    // When: The scenario file is loaded.
    const bool ok = SimRunner::LoadScenarioFromFile(tempFile.GetPath(), &scenario, &error);

    // Then: Loading succeeds and preserves the configured unsigned value.
    TEST_ASSERT(ok, "TC-N-01 load must succeed");
    TEST_ASSERT(error.empty(), "TC-N-01 error must be empty");
    TEST_ASSERT(scenario.writeDelayMs == 5, "TC-N-01 writeDelayMs must be 5");
}

void Test_TC_N_02_MultilineEp6Results_LoadsSuccessfully()
{
    // Given: A scenario JSON with a pretty-printed ep6_results array.
    const char* json = R"({
  "wave_size_low": 32,
  "wave_size_high": 32,
  "waves_per_file": 2,
  "total_wave_count": 4,
  "producer_step_bytes": 256,
  "max_read_chunk_bytes": 16384,
  "timeout_retry_limit": 0,
  "write_delay_ms": 5,
  "write_fail_at": 0,
  "publish_fail_at": 0,
  "ep6_results": [
    "success",
    "timeout"
  ]
})";
    ScopedTempScenarioFile tempFile(json);
    SimRunner::SimulationScenario scenario = {};
    std::wstring error;

    // When: The scenario file is loaded.
    const bool ok = SimRunner::LoadScenarioFromFile(tempFile.GetPath(), &scenario, &error);

    // Then: Loading succeeds and the multiline array is parsed in order.
    TEST_ASSERT(ok, "TC-N-02 load must succeed");
    TEST_ASSERT(error.empty(), "TC-N-02 error must be empty");
    TEST_ASSERT(scenario.ep6Results.size() == 2, "TC-N-02 ep6Results must contain 2 entries");
    TEST_ASSERT(
        scenario.ep6Results.size() >= 2 &&
        scenario.ep6Results[0] == SimRunner::Ep6ResultKind::Success &&
        scenario.ep6Results[1] == SimRunner::Ep6ResultKind::Timeout,
        "TC-N-02 ep6Results must preserve token order");
}

void Test_TC_A_01_OutOfRangeUnsignedField_IsRejected()
{
    // Given: A scenario JSON with a required ULONG field above its maximum value.
    const char* json = R"({
  "wave_size_low": 4294967296,
  "wave_size_high": 32,
  "waves_per_file": 2,
  "total_wave_count": 4,
  "producer_step_bytes": 256,
  "max_read_chunk_bytes": 16384,
  "timeout_retry_limit": 0,
  "write_delay_ms": 5,
  "write_fail_at": 0,
  "publish_fail_at": 0,
  "ep6_results": ["success"]
})";
    ScopedTempScenarioFile tempFile(json);
    SimRunner::SimulationScenario scenario = {};
    std::wstring error;

    // When: The scenario file is loaded.
    const bool ok = SimRunner::LoadScenarioFromFile(tempFile.GetPath(), &scenario, &error);

    // Then: Loading fails with an out-of-range validation error.
    TEST_ASSERT(!ok, "TC-A-01 load must fail");
    TEST_ASSERT(error == L"wave_size_low is out of range", "TC-A-01 error must describe out-of-range wave_size_low");
}

void Test_TC_A_02_OutOfRangeRequiredIntField_IsRejected()
{
    // Given: A scenario JSON with a required INT field above its maximum value.
    const char* json = R"({
  "wave_size_low": 32,
  "wave_size_high": 32,
  "waves_per_file": 2,
  "total_wave_count": 4,
  "producer_step_bytes": 256,
  "max_read_chunk_bytes": 16384,
  "timeout_retry_limit": 2147483648,
  "write_delay_ms": 5,
  "write_fail_at": 0,
  "publish_fail_at": 0,
  "ep6_results": ["success"]
})";
    ScopedTempScenarioFile tempFile(json);
    SimRunner::SimulationScenario scenario = {};
    std::wstring error;

    // When: The scenario file is loaded.
    const bool ok = SimRunner::LoadScenarioFromFile(tempFile.GetPath(), &scenario, &error);

    // Then: Loading fails with an out-of-range validation error.
    TEST_ASSERT(!ok, "TC-A-02 load must fail");
    TEST_ASSERT(error == L"timeout_retry_limit is out of range", "TC-A-02 error must describe out-of-range timeout_retry_limit");
}

void Test_TC_A_03_OutOfRangeOptionalIntField_IsRejected()
{
    // Given: A scenario JSON with an optional INT field above its maximum value.
    const char* json = R"({
  "wave_size_low": 32,
  "wave_size_high": 32,
  "waves_per_file": 2,
  "total_wave_count": 4,
  "producer_step_bytes": 256,
  "init_poll_count": 2147483648,
  "max_read_chunk_bytes": 16384,
  "timeout_retry_limit": 0,
  "write_delay_ms": 5,
  "write_fail_at": 0,
  "publish_fail_at": 0,
  "ep6_results": ["success"]
})";
    ScopedTempScenarioFile tempFile(json);
    SimRunner::SimulationScenario scenario = {};
    std::wstring error;

    // When: The scenario file is loaded.
    const bool ok = SimRunner::LoadScenarioFromFile(tempFile.GetPath(), &scenario, &error);

    // Then: Loading fails instead of silently falling back to defaults.
    TEST_ASSERT(!ok, "TC-A-03 load must fail");
    TEST_ASSERT(error == L"init_poll_count is out of range", "TC-A-03 error must describe out-of-range init_poll_count");
}

int main()
{
    std::printf("=== SimulationScenario Unit Tests ===\n\n");

    RUN_TEST(Test_TC_A_01_OutOfRangeUnsignedField_IsRejected);
    RUN_TEST(Test_TC_A_02_OutOfRangeRequiredIntField_IsRejected);
    RUN_TEST(Test_TC_A_03_OutOfRangeOptionalIntField_IsRejected);
    RUN_TEST(Test_TC_A_04_NegativeUnsignedField_IsRejected);
    RUN_TEST(Test_TC_N_01_ValidScenario_LoadsSuccessfully);
    RUN_TEST(Test_TC_N_02_MultilineEp6Results_LoadsSuccessfully);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
