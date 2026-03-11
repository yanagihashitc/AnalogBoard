#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../AnalogBoard_SimRunner/SimulationScenario.h"

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

void Test_TC_A_01_NegativeUnsignedField_IsRejected()
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
    TEST_ASSERT(!ok, "TC-A-01 load must fail");
    TEST_ASSERT(error.find(L"write_delay_ms") != std::wstring::npos, "TC-A-01 error must mention write_delay_ms");
    TEST_ASSERT(scenario.writeDelayMs == 0, "TC-A-01 output scenario must remain default");
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

int main()
{
    std::printf("=== SimulationScenario Unit Tests ===\n\n");

    RUN_TEST(Test_TC_A_01_NegativeUnsignedField_IsRejected);
    RUN_TEST(Test_TC_N_01_ValidScenario_LoadsSuccessfully);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
