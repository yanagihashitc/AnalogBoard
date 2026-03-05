#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "../AnalogBoard_TestApp/SavePathValidation.h"

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

void Test_T9_ValidateSavePath_ExistingDirectory_Succeeds()
{
    const fs::path dir = fs::temp_directory_path() / L"save_path_t9";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    const SavePathValidation::Result result = SavePathValidation::ValidateSavePath(dir.wstring());

    TEST_ASSERT(result.code == SavePathValidation::kSavePathValidationOk, "T9 code must be OK");
    TEST_ASSERT(!result.normalizedPath.empty(), "T9 normalizedPath must not be empty");
    TEST_ASSERT(SavePathValidation::BuildWarningMessage(result).empty(), "T9 warning message must be empty");
}

void Test_T10_ValidateSavePath_NotFound_ReturnsExpectedError()
{
    const fs::path dir = fs::temp_directory_path() / L"save_path_t10_not_found";
    std::error_code ec;
    fs::remove_all(dir, ec);

    const SavePathValidation::Result result = SavePathValidation::ValidateSavePath(dir.wstring());

    TEST_ASSERT(result.code == SavePathValidation::kSavePathOutputPathNotFound, "T10 code must be NOT_FOUND");
    TEST_ASSERT(result.message.find(dir.wstring()) != std::wstring::npos, "T10 message must include path");
}

bool AlwaysNotWritable(const std::wstring&, DWORD* outLastError)
{
    if (outLastError != nullptr)
    {
        *outLastError = ERROR_ACCESS_DENIED;
    }
    return false;
}

void Test_T11_ValidateSavePath_NotWritable_ReturnsExpectedError()
{
    const fs::path dir = fs::temp_directory_path() / L"save_path_t11";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    const SavePathValidation::Result result =
        SavePathValidation::ValidateSavePath(dir.wstring(), AlwaysNotWritable);

    TEST_ASSERT(result.code == SavePathValidation::kSavePathOutputPathNotWritable, "T11 code must be NOT_WRITABLE");
    TEST_ASSERT(result.message.find(L"writ") != std::wstring::npos, "T11 message should mention writable");
}

void Test_T12_ValidateSavePath_PathTraversal_ReturnsExpectedError()
{
    const fs::path base = fs::temp_directory_path() / L"save_path_t12";
    std::error_code ec;
    fs::create_directories(base, ec);
    const std::wstring rawPath = (base / L"..\\blocked").wstring();

    const SavePathValidation::Result result = SavePathValidation::ValidateSavePath(rawPath);

    TEST_ASSERT(result.code == SavePathValidation::kSavePathInvalidOutputPath, "T12 code must be INVALID_PATH");
}

void Test_T13_WarningClears_WhenPathBecomesValid()
{
    const fs::path invalidPath = fs::temp_directory_path() / L"save_path_t13_not_found";
    std::error_code ec;
    fs::remove_all(invalidPath, ec);
    const SavePathValidation::Result invalidResult = SavePathValidation::ValidateSavePath(invalidPath.wstring());
    TEST_ASSERT(!SavePathValidation::BuildWarningMessage(invalidResult).empty(), "T13 invalid path should have warning");

    const fs::path validPath = fs::temp_directory_path() / L"save_path_t13_valid";
    fs::remove_all(validPath, ec);
    fs::create_directories(validPath, ec);
    const SavePathValidation::Result validResult = SavePathValidation::ValidateSavePath(validPath.wstring());
    TEST_ASSERT(validResult.code == SavePathValidation::kSavePathValidationOk, "T13 valid path should be OK");
    TEST_ASSERT(SavePathValidation::BuildWarningMessage(validResult).empty(), "T13 warning must clear on valid path");
}

int main()
{
    std::printf("=== SavePathValidation Unit Tests ===\n\n");

    RUN_TEST(Test_T9_ValidateSavePath_ExistingDirectory_Succeeds);
    RUN_TEST(Test_T10_ValidateSavePath_NotFound_ReturnsExpectedError);
    RUN_TEST(Test_T11_ValidateSavePath_NotWritable_ReturnsExpectedError);
    RUN_TEST(Test_T12_ValidateSavePath_PathTraversal_ReturnsExpectedError);
    RUN_TEST(Test_T13_WarningClears_WhenPathBecomesValid);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount > 0 ? 1 : 0;
}
