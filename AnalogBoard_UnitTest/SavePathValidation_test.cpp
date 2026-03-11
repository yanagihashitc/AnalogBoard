#include <windows.h>

#include <filesystem>
#include <string>

#include "TestFramework.h"
#include "../AnalogBoard_TestApp/SavePathValidation.h"

namespace fs = std::filesystem;

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

void Test_T14_ContainsControlCharacter_Boundary31And32()
{
    // Given: One path contains U+001F and another contains U+0020.
    std::wstring withU001F = L"C:\\tmp\\probe";
    withU001F.push_back(static_cast<wchar_t>(31));
    withU001F += L"x";
    const std::wstring withU0020 = L"C:\\tmp\\probe x";

    // When: Control character check is evaluated.
    const bool hasControl31 = SavePathValidation::ContainsControlCharacter(withU001F);
    const bool hasControl32 = SavePathValidation::ContainsControlCharacter(withU0020);

    // Then: U+001F is treated as control and U+0020 is not.
    TEST_ASSERT(hasControl31, "T14 U+001F must be detected as control character");
    TEST_ASSERT(!hasControl32, "T14 U+0020 must not be detected as control character");
}

void Test_T15_DefaultWriteProbeWithSeed_RetryOnFileExists()
{
    // Given: Attempt0 probe path already exists.
    const fs::path dir = fs::temp_directory_path() / L"save_path_t15";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    constexpr DWORD kPid = 4242;
    constexpr ULONGLONG kTick = 123456789ULL;
    const std::wstring attempt0Path =
        SavePathValidation::BuildWriteProbePathForTest(dir.wstring(), kPid, kTick, 0);
    const std::wstring attempt1Path =
        SavePathValidation::BuildWriteProbePathForTest(dir.wstring(), kPid, kTick, 1);

    HANDLE collision = ::CreateFileW(
        attempt0Path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    TEST_ASSERT(collision != INVALID_HANDLE_VALUE, "T15 create collision file");
    if (collision != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(collision);
    }

    // When: Write probe retries on ERROR_FILE_EXISTS.
    DWORD lastError = ERROR_SUCCESS;
    const bool probeOk = SavePathValidation::DefaultWriteProbeWithSeed(
        dir.wstring(),
        &lastError,
        kPid,
        kTick,
        4);

    // Then: Probe succeeds via a later attempt and leaves no temp file.
    TEST_ASSERT(probeOk, "T15 probe should succeed after retry");
    TEST_ASSERT(lastError == ERROR_SUCCESS, "T15 last error should be success");
    TEST_ASSERT(fs::exists(attempt0Path), "T15 collision file should remain untouched");
    TEST_ASSERT(!fs::exists(attempt1Path), "T15 retry probe temp file should be cleaned up");
}

void Test_T16_DefaultWriteProbeWithSeed_AllAttemptsCollide()
{
    // Given: All probe candidates already exist.
    const fs::path dir = fs::temp_directory_path() / L"save_path_t16";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    constexpr DWORD kPid = 5252;
    constexpr ULONGLONG kTick = 987654321ULL;
    constexpr int kMaxAttempts = 3;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
    {
        const std::wstring path =
            SavePathValidation::BuildWriteProbePathForTest(dir.wstring(), kPid, kTick, attempt);
        HANDLE h = ::CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        TEST_ASSERT(h != INVALID_HANDLE_VALUE, "T16 create collision file");
        if (h != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(h);
        }
    }

    // When: Probe attempts are exhausted.
    DWORD lastError = ERROR_SUCCESS;
    const bool probeOk = SavePathValidation::DefaultWriteProbeWithSeed(
        dir.wstring(),
        &lastError,
        kPid,
        kTick,
        kMaxAttempts);

    // Then: Probe fails with already-exists error.
    TEST_ASSERT(!probeOk, "T16 probe should fail when all attempts collide");
    TEST_ASSERT(lastError == ERROR_ALREADY_EXISTS, "T16 last error should be already exists");
}

void Test_T17_ShouldValidateForUiTrigger_Startup()
{
    // Given: Startup trigger.
    const SavePathValidation::UiValidationTrigger trigger =
        SavePathValidation::UiValidationTrigger::kStartup;

    // When: UI trigger policy is evaluated.
    const bool shouldValidate = SavePathValidation::ShouldValidateForUiTrigger(trigger);

    // Then: Startup must run validation.
    TEST_ASSERT(shouldValidate, "T17 startup trigger must run validation");
}

void Test_T18_ShouldValidateForUiTrigger_FolderDialogCancel()
{
    // Given: Folder dialog cancel trigger.
    const SavePathValidation::UiValidationTrigger trigger =
        SavePathValidation::UiValidationTrigger::kFolderDialogCancel;

    // When: UI trigger policy is evaluated.
    const bool shouldValidate = SavePathValidation::ShouldValidateForUiTrigger(trigger);

    // Then: Cancel must also run validation.
    TEST_ASSERT(shouldValidate, "T18 cancel trigger must run validation");
}

void Test_T19_ShouldNotValidateForUiTrigger_TextChanged()
{
    // Given: Edit text changed trigger.
    const SavePathValidation::UiValidationTrigger trigger =
        SavePathValidation::UiValidationTrigger::kTextChanged;

    // When: UI trigger policy is evaluated.
    const bool shouldValidate = SavePathValidation::ShouldValidateForUiTrigger(trigger);

    // Then: Heavy validation remains deferred for text change.
    TEST_ASSERT(!shouldValidate, "T19 text-changed trigger must not run validation");
}

void Test_T20_ShouldShowDialogForUiTrigger_Startup()
{
    // Given: Startup trigger.
    const SavePathValidation::UiValidationTrigger trigger =
        SavePathValidation::UiValidationTrigger::kStartup;

    // When: Dialog policy is evaluated.
    const bool shouldShowDialog = SavePathValidation::ShouldShowDialogForUiTrigger(trigger);

    // Then: Startup must show warning dialog on invalid path.
    TEST_ASSERT(shouldShowDialog, "T20 startup trigger must show dialog");
}

void Test_T21_ShouldShowDialogForUiTrigger_FolderDialogCancel()
{
    // Given: Folder dialog cancel trigger.
    const SavePathValidation::UiValidationTrigger trigger =
        SavePathValidation::UiValidationTrigger::kFolderDialogCancel;

    // When: Dialog policy is evaluated.
    const bool shouldShowDialog = SavePathValidation::ShouldShowDialogForUiTrigger(trigger);

    // Then: Cancel must show warning dialog on invalid path.
    TEST_ASSERT(shouldShowDialog, "T21 cancel trigger must show dialog");
}

void Test_T22_ShouldShowDialogForUiTrigger_FolderDialogConfirmed()
{
    // Given: Folder dialog confirmed trigger.
    const SavePathValidation::UiValidationTrigger trigger =
        SavePathValidation::UiValidationTrigger::kFolderDialogConfirmed;

    // When: Dialog policy is evaluated.
    const bool shouldShowDialog = SavePathValidation::ShouldShowDialogForUiTrigger(trigger);

    // Then: Confirmed folder must show dialog so user gets immediate feedback.
    TEST_ASSERT(shouldShowDialog, "T22 confirmed trigger must show dialog");
}

void Test_T23_ShouldValidateForUiTrigger_SetParameters()
{
    // Given: SetParameters trigger.
    const SavePathValidation::UiValidationTrigger trigger =
        SavePathValidation::UiValidationTrigger::kSetParameters;

    // When: UI trigger policy is evaluated.
    const bool shouldValidate = SavePathValidation::ShouldValidateForUiTrigger(trigger);

    // Then: SetParameters must run validation.
    TEST_ASSERT(shouldValidate, "T23 SetParameters trigger must run validation");
}

void Test_T24_ShouldValidateForUiTrigger_FolderDialogConfirmed()
{
    // Given: Folder dialog confirmed trigger.
    const SavePathValidation::UiValidationTrigger trigger =
        SavePathValidation::UiValidationTrigger::kFolderDialogConfirmed;

    // When: UI trigger policy is evaluated.
    const bool shouldValidate = SavePathValidation::ShouldValidateForUiTrigger(trigger);

    // Then: Confirmed folder must run validation.
    TEST_ASSERT(shouldValidate, "T24 confirmed trigger must run validation");
}

void Test_T25_ShouldShowDialogForUiTrigger_SetParameters()
{
    // Given: SetParameters trigger.
    const SavePathValidation::UiValidationTrigger trigger =
        SavePathValidation::UiValidationTrigger::kSetParameters;

    // When: Dialog policy is evaluated.
    const bool shouldShowDialog = SavePathValidation::ShouldShowDialogForUiTrigger(trigger);

    // Then: SetParameters must show dialog on invalid path.
    TEST_ASSERT(shouldShowDialog, "T25 SetParameters trigger must show dialog");
}

void Test_T26_ShouldNotShowDialogForUiTrigger_TextChanged()
{
    // Given: Text changed trigger.
    const SavePathValidation::UiValidationTrigger trigger =
        SavePathValidation::UiValidationTrigger::kTextChanged;

    // When: Dialog policy is evaluated.
    const bool shouldShowDialog = SavePathValidation::ShouldShowDialogForUiTrigger(trigger);

    // Then: Text change must not show dialog.
    TEST_ASSERT(!shouldShowDialog, "T26 text-changed trigger must not show dialog");
}

void Test_T27_ShouldValidateStartupAfterSuccessfulConfigImport()
{
    // Given: Default config import completed successfully.
    const bool importSucceeded = true;

    // When: Startup validation policy is evaluated with import result.
    const bool shouldValidate =
        SavePathValidation::ShouldValidateStartupAfterConfigImport(importSucceeded);

    // Then: Startup save-path validation must run.
    TEST_ASSERT(shouldValidate, "T27 successful import must enable startup validation");
}

void Test_T28_ShouldNotValidateStartupAfterFailedConfigImport()
{
    // Given: Default config import failed before SavePath was reliably loaded.
    const bool importSucceeded = false;

    // When: Startup validation policy is evaluated with import result.
    const bool shouldValidate =
        SavePathValidation::ShouldValidateStartupAfterConfigImport(importSucceeded);

    // Then: Startup save-path validation must be skipped.
    TEST_ASSERT(!shouldValidate, "T28 failed import must skip startup validation");
}

void Test_T29_ResolveSavePathForSetParameters_UseNormalizedWhenValidated()
{
    // Given: Validation is enabled and normalized path is available.
    const std::wstring rawUiPath = L"C:\\raw_path";
    const std::wstring normalizedPath = L"C:\\normalized_path";

    // When: SavePath assignment policy for SetParameters is evaluated.
    const std::wstring resolved = SavePathValidation::ResolveSavePathForSetParameters(
        rawUiPath,
        normalizedPath,
        true);

    // Then: Normalized path must be selected.
    TEST_ASSERT(resolved == normalizedPath, "T29 should use normalized path when validated");
}

void Test_T30_ResolveSavePathForSetParameters_FallbackToUiPathWhenValidationSkipped()
{
    // Given: Validation is skipped.
    const std::wstring rawUiPath = L"C:\\raw_path";
    const std::wstring normalizedPath = L"";

    // When: SavePath assignment policy for SetParameters is evaluated.
    const std::wstring resolved = SavePathValidation::ResolveSavePathForSetParameters(
        rawUiPath,
        normalizedPath,
        false);

    // Then: Raw UI path must be preserved.
    TEST_ASSERT(resolved == rawUiPath, "T30 should fallback to UI path when validation is skipped");
}

void Test_T31_ResolveSavePathForSetParameters_FallbackToUiPathWhenNormalizedEmpty()
{
    // Given: Validation path is selected but normalized output is unexpectedly empty.
    const std::wstring rawUiPath = L"C:\\raw_path";
    const std::wstring normalizedPath = L"";

    // When: SavePath assignment policy for SetParameters is evaluated.
    const std::wstring resolved = SavePathValidation::ResolveSavePathForSetParameters(
        rawUiPath,
        normalizedPath,
        true);

    // Then: Raw UI path must be used as a defensive fallback.
    TEST_ASSERT(resolved == rawUiPath, "T31 should fallback to UI path when normalized path is empty");
}

void Test_T32_ResolveSavePathForSetParameters_KeepEmptyUiPathWhenValidationSkipped()
{
    // Given: Validation is skipped and UI path is empty.
    const std::wstring rawUiPath = L"";
    const std::wstring normalizedPath = L"C:\\normalized_path";

    // When: SavePath assignment policy for SetParameters is evaluated.
    const std::wstring resolved = SavePathValidation::ResolveSavePathForSetParameters(
        rawUiPath,
        normalizedPath,
        false);

    // Then: Empty UI path should be preserved unchanged.
    TEST_ASSERT(resolved == rawUiPath, "T32 should keep UI path even when it is empty");
}

int main()
{
    std::printf("=== SavePathValidation Unit Tests ===\n\n");

    RUN_TEST(Test_T9_ValidateSavePath_ExistingDirectory_Succeeds);
    RUN_TEST(Test_T10_ValidateSavePath_NotFound_ReturnsExpectedError);
    RUN_TEST(Test_T11_ValidateSavePath_NotWritable_ReturnsExpectedError);
    RUN_TEST(Test_T12_ValidateSavePath_PathTraversal_ReturnsExpectedError);
    RUN_TEST(Test_T13_WarningClears_WhenPathBecomesValid);
    RUN_TEST(Test_T14_ContainsControlCharacter_Boundary31And32);
    RUN_TEST(Test_T15_DefaultWriteProbeWithSeed_RetryOnFileExists);
    RUN_TEST(Test_T16_DefaultWriteProbeWithSeed_AllAttemptsCollide);
    RUN_TEST(Test_T17_ShouldValidateForUiTrigger_Startup);
    RUN_TEST(Test_T18_ShouldValidateForUiTrigger_FolderDialogCancel);
    RUN_TEST(Test_T19_ShouldNotValidateForUiTrigger_TextChanged);
    RUN_TEST(Test_T20_ShouldShowDialogForUiTrigger_Startup);
    RUN_TEST(Test_T21_ShouldShowDialogForUiTrigger_FolderDialogCancel);
    RUN_TEST(Test_T22_ShouldShowDialogForUiTrigger_FolderDialogConfirmed);
    RUN_TEST(Test_T23_ShouldValidateForUiTrigger_SetParameters);
    RUN_TEST(Test_T24_ShouldValidateForUiTrigger_FolderDialogConfirmed);
    RUN_TEST(Test_T25_ShouldShowDialogForUiTrigger_SetParameters);
    RUN_TEST(Test_T26_ShouldNotShowDialogForUiTrigger_TextChanged);
    RUN_TEST(Test_T27_ShouldValidateStartupAfterSuccessfulConfigImport);
    RUN_TEST(Test_T28_ShouldNotValidateStartupAfterFailedConfigImport);
    RUN_TEST(Test_T29_ResolveSavePathForSetParameters_UseNormalizedWhenValidated);
    RUN_TEST(Test_T30_ResolveSavePathForSetParameters_FallbackToUiPathWhenValidationSkipped);
    RUN_TEST(Test_T31_ResolveSavePathForSetParameters_FallbackToUiPathWhenNormalizedEmpty);
    RUN_TEST(Test_T32_ResolveSavePathForSetParameters_KeepEmptyUiPathWhenValidationSkipped);

    std::printf("\n=== Results: %d tests, %d passed, %d failed ===\n", g_TestCount, g_PassCount, g_FailCount);
    return g_FailCount > 0 ? 1 : 0;
}
