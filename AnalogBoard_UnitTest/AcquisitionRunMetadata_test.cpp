#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include <windows.h>

#include "../AnalogBoard_TestApp/AcquisitionRunMetadata.h"

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
    std::wstring MakeTempPath(const wchar_t* fileName)
    {
        wchar_t tempPath[MAX_PATH] = {};
        const DWORD length = ::GetTempPathW(MAX_PATH, tempPath);
        std::wstring path(tempPath, tempPath + length);
        path += fileName;
        return path;
    }

    void WriteAsciiFile(const std::wstring& path, const char* content)
    {
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"wb") == 0 && file != nullptr)
        {
            std::fwrite(content, 1, std::strlen(content), file);
            std::fclose(file);
        }
    }

    std::string ReadAsciiFile(const std::wstring& path)
    {
        std::ifstream file(path.c_str(), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    WaveAcquisition::AcquisitionSummary MakeSummary(
        WaveAcquisition::TerminalStatus status,
        INT errorCode,
        INT publishedPairs,
        ULONG savedWaveCount,
        ULONG ignoredTailBytes)
    {
        WaveAcquisition::AcquisitionSummary summary = {};
        summary.terminalStatus = status;
        summary.errorCode = errorCode;
        summary.publishedPairCount = publishedPairs;
        summary.savedWaveCount = savedWaveCount;
        summary.ignoredTailBytes = ignoredTailBytes;
        return summary;
    }
}

void Test_TC_N_01_AppendRunResultMetadata_AppendsSuccessStatus()
{
    const std::wstring path = MakeTempPath(L"AcquisitionRunMetadata_success_cfg.txt");
    WriteAsciiFile(path, "# cfg\n");
    const WaveAcquisition::AcquisitionSummary summary =
        MakeSummary(WaveAcquisition::TerminalStatus::Success, 0, 9, 4391u, 0u);

    // Given: An existing cfg file and a successful acquisition summary.
    // When: Run result metadata is appended.
    const AcquisitionRunMetadata::AppendResult result =
        AcquisitionRunMetadata::AppendRunResultMetadata(path.c_str(), summary);
    const std::string contents = ReadAsciiFile(path);

    // Then: The success status and counters are appended to the cfg file.
    TEST_ASSERT(result.success, "TC-N-01 append should succeed");
    TEST_ASSERT(contents.find("Run Status:,success") != std::string::npos, "TC-N-01 success status should be appended");
    TEST_ASSERT(contents.find("Published Pairs:,9") != std::string::npos, "TC-N-01 published pairs should be appended");
    TEST_ASSERT(contents.find("Saved Wave Count:,4391") != std::string::npos, "TC-N-01 saved wave count should be appended");

    ::DeleteFileW(path.c_str());
}

void Test_TC_N_02_AppendRunResultMetadata_AppendsFailureStatus()
{
    const std::wstring path = MakeTempPath(L"AcquisitionRunMetadata_failure_cfg.txt");
    WriteAsciiFile(path, "# cfg\n");
    const WaveAcquisition::AcquisitionSummary summary =
        MakeSummary(WaveAcquisition::TerminalStatus::Ep6Timeout, -10, 1, 832u, 0u);

    // Given: An existing cfg file and a failed acquisition summary.
    // When: Run result metadata is appended.
    const AcquisitionRunMetadata::AppendResult result =
        AcquisitionRunMetadata::AppendRunResultMetadata(path.c_str(), summary);
    const std::string contents = ReadAsciiFile(path);

    // Then: The failure status and retained-pair count are appended to the cfg file.
    TEST_ASSERT(result.success, "TC-N-02 append should succeed");
    TEST_ASSERT(contents.find("Run Status:,ep6_timeout") != std::string::npos, "TC-N-02 failure status should be appended");
    TEST_ASSERT(contents.find("Run Error Code:,-10") != std::string::npos, "TC-N-02 error code should be appended");
    TEST_ASSERT(contents.find("Published Pairs:,1") != std::string::npos, "TC-N-02 published pairs should be appended");

    ::DeleteFileW(path.c_str());
}

void Test_TC_B_01_AppendRunResultMetadata_MissingFileFailsWithoutCreatingNewFile()
{
    const std::wstring path = MakeTempPath(L"AcquisitionRunMetadata_missing_cfg.txt");
    ::DeleteFileW(path.c_str());
    const WaveAcquisition::AcquisitionSummary summary =
        MakeSummary(WaveAcquisition::TerminalStatus::Ep6Timeout, -10, 0, 0u, 0u);

    // Given: A cfg path that does not exist.
    // When: Run result metadata is appended.
    const AcquisitionRunMetadata::AppendResult result =
        AcquisitionRunMetadata::AppendRunResultMetadata(path.c_str(), summary);

    // Then: The append fails and does not create a new cfg file implicitly.
    TEST_ASSERT(!result.success, "TC-B-01 append should fail for missing cfg");
    TEST_ASSERT(::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES, "TC-B-01 missing cfg should remain absent");
}

void Test_TC_B_02_AppendRunResultMetadata_PreservesZeroCounters()
{
    const std::wstring path = MakeTempPath(L"AcquisitionRunMetadata_zero_cfg.txt");
    WriteAsciiFile(path, "# cfg\n");
    const WaveAcquisition::AcquisitionSummary summary =
        MakeSummary(WaveAcquisition::TerminalStatus::EmptyCapture, -21009, 0, 0u, 0u);

    // Given: An existing cfg file and zero-valued counters.
    // When: Run result metadata is appended.
    const AcquisitionRunMetadata::AppendResult result =
        AcquisitionRunMetadata::AppendRunResultMetadata(path.c_str(), summary);
    const std::string contents = ReadAsciiFile(path);

    // Then: Zero counters are written without being dropped or reformatted.
    TEST_ASSERT(result.success, "TC-B-02 append should succeed");
    TEST_ASSERT(contents.find("Published Pairs:,0") != std::string::npos, "TC-B-02 zero published pairs should be appended");
    TEST_ASSERT(contents.find("Saved Wave Count:,0") != std::string::npos, "TC-B-02 zero saved wave count should be appended");
    TEST_ASSERT(contents.find("Ignored Tail Bytes:,0") != std::string::npos, "TC-B-02 zero ignored tail should be appended");

    ::DeleteFileW(path.c_str());
}

int main()
{
    std::printf("=== AcquisitionRunMetadata Unit Tests ===\n\n");

    RUN_TEST(Test_TC_N_01_AppendRunResultMetadata_AppendsSuccessStatus);
    RUN_TEST(Test_TC_N_02_AppendRunResultMetadata_AppendsFailureStatus);
    RUN_TEST(Test_TC_B_01_AppendRunResultMetadata_MissingFileFailsWithoutCreatingNewFile);
    RUN_TEST(Test_TC_B_02_AppendRunResultMetadata_PreservesZeroCounters);

    std::printf("\n=== Summary ===\n");
    std::printf("Total: %d\n", g_TestCount);
    std::printf("Passed: %d\n", g_PassCount);
    std::printf("Failed: %d\n", g_FailCount);

    return (g_FailCount == 0) ? 0 : 1;
}
