#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../AnalogBoard_TestApp/WaveFilePublish.h"

namespace {

void AssertTrue(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void AssertEqual(const std::wstring& actual, const std::wstring& expected, const char* message)
{
    if (actual != expected)
    {
        std::wcerr << L"Actual: " << actual << L"\nExpected: " << expected << std::endl;
        throw std::runtime_error(message);
    }
}

void AssertEqual(wave_file_publish::PublishResult actual, wave_file_publish::PublishResult expected, const char* message)
{
    if (actual != expected)
    {
        throw std::runtime_error(message);
    }
}

void TestBuildWaveFilePairPath_Normal()
{
    // Given: Valid prefix and index
    // When: Building wave file paths
    // Then: Final/tmp low/high paths follow naming convention
    wave_file_publish::WaveFilePairPath paths;
    const bool ok = wave_file_publish::BuildWaveFilePairPath(L"\\20260226_1200", 1, &paths);

    AssertTrue(ok, "BuildWaveFilePairPath should succeed for normal input");
    AssertEqual(paths.lowFinalPath, L"\\20260226_1200_fl_1.bin", "low final path mismatch");
    AssertEqual(paths.highFinalPath, L"\\20260226_1200_fh_1.bin", "high final path mismatch");
    AssertEqual(paths.lowTempPath, L"\\20260226_1200_fl_1.bin.tmp", "low tmp path mismatch");
    AssertEqual(paths.highTempPath, L"\\20260226_1200_fh_1.bin.tmp", "high tmp path mismatch");
}

void TestBuildWaveFilePairPath_IndexZero()
{
    // Given: Valid prefix and boundary index zero
    // When: Building wave file paths
    // Then: Path names are generated with index zero
    wave_file_publish::WaveFilePairPath paths;
    const bool ok = wave_file_publish::BuildWaveFilePairPath(L"\\20260226_1200", 0, &paths);

    AssertTrue(ok, "BuildWaveFilePairPath should accept index zero");
    AssertEqual(paths.lowFinalPath, L"\\20260226_1200_fl_0.bin", "low final path mismatch for zero");
    AssertEqual(paths.highFinalPath, L"\\20260226_1200_fh_0.bin", "high final path mismatch for zero");
}

void TestBuildWaveFilePairPath_EmptyPrefix()
{
    // Given: Empty prefix
    // When: Building wave file paths
    // Then: Function returns false
    wave_file_publish::WaveFilePairPath paths;
    const bool ok = wave_file_publish::BuildWaveFilePairPath(L"", 1, &paths);

    AssertTrue(!ok, "BuildWaveFilePairPath should fail for empty prefix");
}

void TestBuildWaveFilePairPath_NegativeIndex()
{
    // Given: Negative index
    // When: Building wave file paths
    // Then: Function returns false
    wave_file_publish::WaveFilePairPath paths;
    const bool ok = wave_file_publish::BuildWaveFilePairPath(L"\\20260226_1200", -1, &paths);

    AssertTrue(!ok, "BuildWaveFilePairPath should fail for negative index");
}

void TestBuildWaveFilePairPath_NullOutPath()
{
    // Given: Valid prefix and null output pointer
    // When: Building wave file paths
    // Then: Function returns false
    const bool ok = wave_file_publish::BuildWaveFilePairPath(L"\\20260226_1200", 1, nullptr);

    AssertTrue(!ok, "BuildWaveFilePairPath should fail for null out path");
}

void TestPublishWaveFilePair_Success()
{
    // Given: Valid paths and successful rename callback
    // When: Publishing tmp files
    // Then: Publish result is success and both rename calls are executed
    wave_file_publish::WaveFilePairPath paths;
    const bool ok = wave_file_publish::BuildWaveFilePairPath(L"\\20260226_1200", 3, &paths);
    AssertTrue(ok, "BuildWaveFilePairPath should succeed");

    std::vector<std::wstring> called;
    auto renameFn = [&](const std::wstring& from, const std::wstring& to) {
        called.push_back(from + L"=>" + to);
        return true;
    };

    const auto result = wave_file_publish::PublishWaveFilePair(paths, renameFn);
    AssertEqual(result, wave_file_publish::PublishResult::kSuccess, "Publish should succeed");
    AssertTrue(called.size() == 2, "Rename should be called twice");
}

void TestPublishWaveFilePair_LowRenameFail()
{
    // Given: Valid paths and low rename failure callback
    // When: Publishing tmp files
    // Then: Publish result is low rename failure
    wave_file_publish::WaveFilePairPath paths;
    const bool ok = wave_file_publish::BuildWaveFilePairPath(L"\\20260226_1200", 4, &paths);
    AssertTrue(ok, "BuildWaveFilePairPath should succeed");

    auto renameFn = [&](const std::wstring& from, const std::wstring&) {
        return from != paths.lowTempPath;
    };

    const auto result = wave_file_publish::PublishWaveFilePair(paths, renameFn);
    AssertEqual(result, wave_file_publish::PublishResult::kLowRenameFailed, "Publish should fail at low rename");
}

void TestPublishWaveFilePair_HighRenameFail()
{
    // Given: Valid paths and high rename failure callback
    // When: Publishing tmp files
    // Then: Publish result is high rename failure after low rename succeeds
    wave_file_publish::WaveFilePairPath paths;
    const bool ok = wave_file_publish::BuildWaveFilePairPath(L"\\20260226_1200", 5, &paths);
    AssertTrue(ok, "BuildWaveFilePairPath should succeed");

    auto renameFn = [&](const std::wstring& from, const std::wstring&) {
        return from != paths.highTempPath;
    };

    const auto result = wave_file_publish::PublishWaveFilePair(paths, renameFn);
    AssertEqual(result, wave_file_publish::PublishResult::kHighRenameFailed, "Publish should fail at high rename");
}

void TestPublishWaveFilePair_HighRenameFail_RollbackLow()
{
    // Given: High rename fails after low rename succeeded
    // When: Publishing tmp files
    // Then: Low side rollback is attempted to avoid partial publish
    wave_file_publish::WaveFilePairPath paths;
    const bool ok = wave_file_publish::BuildWaveFilePairPath(L"\\20260226_1200", 6, &paths);
    AssertTrue(ok, "BuildWaveFilePairPath should succeed");

    std::vector<std::wstring> called;
    auto renameFn = [&](const std::wstring& from, const std::wstring& to) {
        called.push_back(from + L"=>" + to);
        if (from == paths.highTempPath)
        {
            return false;
        }
        return true;
    };

    const auto result = wave_file_publish::PublishWaveFilePair(paths, renameFn);
    AssertEqual(result, wave_file_publish::PublishResult::kHighRenameFailed, "Publish should fail at high rename");
    AssertTrue(called.size() == 3, "Rollback rename should be attempted");
    AssertEqual(called[2], paths.lowFinalPath + L"=>" + paths.lowTempPath, "Low rollback path mismatch");
}

void TestPublishWaveFilePair_InvalidArgument()
{
    // Given: Empty file paths
    // When: Publishing tmp files
    // Then: Publish result is invalid argument
    const wave_file_publish::WaveFilePairPath invalidPath{};

    const auto result = wave_file_publish::PublishWaveFilePair(invalidPath);
    AssertEqual(result, wave_file_publish::PublishResult::kInvalidArgument, "Publish should fail for invalid path");
}

} // namespace

int main()
{
    try
    {
        TestBuildWaveFilePairPath_Normal();
        TestBuildWaveFilePairPath_IndexZero();
        TestBuildWaveFilePairPath_EmptyPrefix();
        TestBuildWaveFilePairPath_NegativeIndex();
        TestBuildWaveFilePairPath_NullOutPath();
        TestPublishWaveFilePair_Success();
        TestPublishWaveFilePair_LowRenameFail();
        TestPublishWaveFilePair_HighRenameFail();
        TestPublishWaveFilePair_HighRenameFail_RollbackLow();
        TestPublishWaveFilePair_InvalidArgument();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "TEST FAILED: " << ex.what() << std::endl;
        return 1;
    }

    std::cout << "All tests passed." << std::endl;
    return 0;
}
