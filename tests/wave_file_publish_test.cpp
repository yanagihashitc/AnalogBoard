#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "../AnalogBoard_TestApp/WaveFilePublish.h"
#include "../AnalogBoard_TestApp/SavePathValidation.h"

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

void AssertEqual(
    save_path_validation::ValidationCode actual,
    save_path_validation::ValidationCode expected,
    const char* message)
{
    if (actual != expected)
    {
        throw std::runtime_error(message);
    }
}

void AssertBufferEqual(const unsigned char* actual, const unsigned char* expected, size_t size, const char* message)
{
    if ((size != 0U) && (std::memcmp(actual, expected, size) != 0))
    {
        throw std::runtime_error(message);
    }
}

void AssertByteVectorEqual(const std::vector<unsigned char>& actual, const std::vector<unsigned char>& expected, const char* message)
{
    if (actual != expected)
    {
        throw std::runtime_error(message);
    }
}

std::filesystem::path ToFsPath(const std::wstring& path)
{
    return std::filesystem::path(path);
}

class ScopedFileCleanup
{
public:
    void Add(const std::wstring& path)
    {
        paths_.push_back(path);
    }

    ~ScopedFileCleanup()
    {
        for (const auto& path : paths_)
        {
            std::error_code ec;
            std::filesystem::remove(ToFsPath(path), ec);
        }
    }

private:
    std::vector<std::wstring> paths_;
};

std::wstring BuildUniquePrefixPath()
{
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto base = std::filesystem::temp_directory_path() / ("wave_publish_test_" + std::to_string(tick));
    return base.wstring();
}

bool ExistsFile(const std::wstring& path)
{
    std::error_code ec;
    return std::filesystem::exists(ToFsPath(path), ec);
}

void WriteBinaryFile(const std::wstring& path, const std::vector<unsigned char>& payload)
{
    std::ofstream output(ToFsPath(path), std::ios::binary | std::ios::trunc);
    AssertTrue(output.is_open(), "Failed to open binary file for writing");

    if (!payload.empty())
    {
        output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    }
    output.flush();
    AssertTrue(output.good(), "Failed to write binary payload");
}

std::vector<unsigned char> ReadBinaryFile(const std::wstring& path)
{
    std::ifstream input(ToFsPath(path), std::ios::binary);
    AssertTrue(input.is_open(), "Failed to open binary file for reading");

    const std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return std::vector<unsigned char>(bytes.begin(), bytes.end());
}

bool RenameReplaceByFilesystem(const std::wstring& srcPath, const std::wstring& dstPath)
{
    std::error_code ec;
    std::filesystem::remove(ToFsPath(dstPath), ec);
    ec.clear();
    std::filesystem::rename(ToFsPath(srcPath), ToFsPath(dstPath), ec);
    return !ec;
}

bool BuildLegacySplitViewAebf296(
    const unsigned char* waveData,
    size_t frameSizeLow,
    size_t frameSizeHigh,
    int frameIndex,
    wave_file_publish::WaveFrameSplitView* outView)
{
    if ((waveData == nullptr) || (outView == nullptr))
    {
        return false;
    }

    if ((frameIndex < 0) || ((frameSizeLow == 0U) && (frameSizeHigh == 0U)))
    {
        return false;
    }

    if (frameSizeLow > (std::numeric_limits<size_t>::max() - frameSizeHigh))
    {
        return false;
    }

    const size_t frameSize = frameSizeLow + frameSizeHigh;
    const unsigned char* frameStart = waveData + (frameSize * static_cast<size_t>(frameIndex));

    outView->lowData = frameStart;
    outView->highData = frameStart + frameSizeLow;
    outView->lowSize = frameSizeLow;
    outView->highSize = frameSizeHigh;

    return true;
}

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    AssertTrue(input.is_open(), "Failed to open text file");
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

bool IsAsciiWhitespace(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r') || (c == '\f') || (c == '\v');
}

std::string StripAsciiWhitespace(const std::string& text)
{
    std::string normalized;
    normalized.reserve(text.size());

    for (char c : text)
    {
        if (!IsAsciiWhitespace(c))
        {
            normalized.push_back(c);
        }
    }

    return normalized;
}

size_t CountOccurrences(const std::string& text, const std::string& token)
{
    if (token.empty())
    {
        return 0U;
    }

    size_t count = 0U;
    size_t pos = 0U;
    while (true)
    {
        pos = text.find(token, pos);
        if (pos == std::string::npos)
        {
            break;
        }
        ++count;
        pos += token.size();
    }

    return count;
}

std::filesystem::path FindDialogMainSourcePath()
{
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::path("AnalogBoard_TestApp") / "Dialog1_Main.cpp",
        std::filesystem::path("..") / "AnalogBoard_TestApp" / "Dialog1_Main.cpp",
        std::filesystem::path("..") / ".." / "AnalogBoard_TestApp" / "Dialog1_Main.cpp",
        std::filesystem::path("..") / ".." / ".." / "AnalogBoard_TestApp" / "Dialog1_Main.cpp",
    };

    for (const auto& candidate : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
        {
            return candidate;
        }
    }

    throw std::runtime_error("Dialog1_Main.cpp not found for source contract test");
}

std::filesystem::path FindSavePathValidationSourcePath()
{
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::path("AnalogBoard_TestApp") / "SavePathValidation.cpp",
        std::filesystem::path("..") / "AnalogBoard_TestApp" / "SavePathValidation.cpp",
        std::filesystem::path("..") / ".." / "AnalogBoard_TestApp" / "SavePathValidation.cpp",
        std::filesystem::path("..") / ".." / ".." / "AnalogBoard_TestApp" / "SavePathValidation.cpp",
    };

    for (const auto& candidate : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
        {
            return candidate;
        }
    }

    throw std::runtime_error("SavePathValidation.cpp not found for source contract test");
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
    // Then: Publish result is success and rename order is low then high
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
    AssertEqual(called[0], paths.lowTempPath + L"=>" + paths.lowFinalPath, "First publish should be low file");
    AssertEqual(called[1], paths.highTempPath + L"=>" + paths.highFinalPath, "Second publish should be high file");
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

void TestBuildWaveFrameSplitView_IndexZero()
{
    // Given: Wave data with low/high data stored in low-first order
    // When: Building frame split view for frame index zero
    // Then: low/high pointers and sizes should point to frame 0 segments
    const unsigned char waveData[] = {
        0x10, 0x11, 0x20, 0x21, 0x22,
        0x30, 0x31, 0x40, 0x41, 0x42,
    };
    wave_file_publish::WaveFrameSplitView view{};
    const bool ok = wave_file_publish::BuildWaveFrameSplitView(waveData, 2U, 3U, 0, &view);

    AssertTrue(ok, "BuildWaveFrameSplitView should succeed for frame index zero");
    AssertTrue(view.lowSize == 2U, "low frame size mismatch");
    AssertTrue(view.highSize == 3U, "high frame size mismatch");
    AssertBufferEqual(view.lowData, waveData, 2U, "low frame payload mismatch at index zero");
    AssertBufferEqual(view.highData, waveData + 2, 3U, "high frame payload mismatch at index zero");
}

void TestBuildWaveFrameSplitView_IndexPlusOne()
{
    // Given: Wave data with two frames in low-first order
    // When: Building frame split view for frame index one
    // Then: low/high pointers should point to frame 1 segments without swapping
    const unsigned char waveData[] = {
        0x10, 0x11, 0x20, 0x21, 0x22,
        0x30, 0x31, 0x40, 0x41, 0x42,
    };
    wave_file_publish::WaveFrameSplitView view{};
    const bool ok = wave_file_publish::BuildWaveFrameSplitView(waveData, 2U, 3U, 1, &view);

    AssertTrue(ok, "BuildWaveFrameSplitView should succeed for frame index one");
    AssertBufferEqual(view.lowData, waveData + 5, 2U, "low frame payload mismatch at index one");
    AssertBufferEqual(view.highData, waveData + 7, 3U, "high frame payload mismatch at index one");
}

void TestBuildWaveFrameSplitView_NullWaveData()
{
    // Given: Null wave data pointer
    // When: Building frame split view
    // Then: Function returns false
    wave_file_publish::WaveFrameSplitView view{};
    const bool ok = wave_file_publish::BuildWaveFrameSplitView(nullptr, 2U, 3U, 0, &view);

    AssertTrue(!ok, "BuildWaveFrameSplitView should fail for null wave data");
}

void TestBuildWaveFrameSplitView_NullOutView()
{
    // Given: Valid wave data and null output pointer
    // When: Building frame split view
    // Then: Function returns false
    const unsigned char waveData[] = {0x10, 0x11, 0x20};
    const bool ok = wave_file_publish::BuildWaveFrameSplitView(waveData, 2U, 1U, 0, nullptr);

    AssertTrue(!ok, "BuildWaveFrameSplitView should fail for null out view");
}

void TestBuildWaveFrameSplitView_NegativeIndex()
{
    // Given: Valid wave data and negative frame index
    // When: Building frame split view
    // Then: Function returns false
    const unsigned char waveData[] = {0x10, 0x11, 0x20};
    wave_file_publish::WaveFrameSplitView view{};
    const bool ok = wave_file_publish::BuildWaveFrameSplitView(waveData, 2U, 1U, -1, &view);

    AssertTrue(!ok, "BuildWaveFrameSplitView should fail for negative frame index");
}

void TestBuildWaveFrameSplitView_ZeroTotalFrameSize()
{
    // Given: Valid wave data and zero low/high frame sizes
    // When: Building frame split view
    // Then: Function returns false
    const unsigned char waveData[] = {0x10};
    wave_file_publish::WaveFrameSplitView view{};
    const bool ok = wave_file_publish::BuildWaveFrameSplitView(waveData, 0U, 0U, 0, &view);

    AssertTrue(!ok, "BuildWaveFrameSplitView should fail when both frame sizes are zero");
}

void TestBuildWaveFrameSplitView_Aebf296Compatibility_TwoFrameOutput()
{
    // Given: Two frames arranged as [Low0][High0][Low1][High1] like aebf296 SaveWaveDataToFile input
    // When: Splitting each frame and accumulating low/high outputs independently
    // Then: Aggregated low/high streams should match aebf296 low-first/high-next serialization
    const unsigned char waveData[] = {
        0x10, 0x11, 0x20, 0x21, 0x22,
        0x30, 0x31, 0x40, 0x41, 0x42,
    };
    std::vector<unsigned char> lowOutput;
    std::vector<unsigned char> highOutput;

    for (int frameIndex = 0; frameIndex < 2; ++frameIndex)
    {
        wave_file_publish::WaveFrameSplitView view{};
        const bool ok = wave_file_publish::BuildWaveFrameSplitView(waveData, 2U, 3U, frameIndex, &view);
        AssertTrue(ok, "BuildWaveFrameSplitView should succeed in compatibility test");
        lowOutput.insert(lowOutput.end(), view.lowData, view.lowData + view.lowSize);
        highOutput.insert(highOutput.end(), view.highData, view.highData + view.highSize);
    }

    const std::vector<unsigned char> expectedLow = {0x10, 0x11, 0x30, 0x31};
    const std::vector<unsigned char> expectedHigh = {0x20, 0x21, 0x22, 0x40, 0x41, 0x42};
    AssertByteVectorEqual(lowOutput, expectedLow, "aebf296-compatible low output mismatch");
    AssertByteVectorEqual(highOutput, expectedHigh, "aebf296-compatible high output mismatch");
}

void TestBuildWaveFrameSplitView_LowSizeZero()
{
    // Given: Frame layout where low size is zero and high contains all frame bytes
    // When: Splitting frame index one
    // Then: low size remains zero and high points to the correct frame boundary
    const unsigned char waveData[] = {0x10, 0x11, 0x12, 0x20, 0x21, 0x22};
    wave_file_publish::WaveFrameSplitView view{};
    const bool ok = wave_file_publish::BuildWaveFrameSplitView(waveData, 0U, 3U, 1, &view);

    AssertTrue(ok, "BuildWaveFrameSplitView should accept zero low size");
    AssertTrue(view.lowSize == 0U, "low size should be zero");
    AssertTrue(view.highSize == 3U, "high size should be three");
    AssertTrue(view.lowData == (waveData + 3), "low pointer should point to frame start when low size is zero");
    AssertBufferEqual(view.highData, waveData + 3, 3U, "high payload mismatch when low size is zero");
}

void TestBuildWaveFrameSplitView_HighSizeZero()
{
    // Given: Frame layout where high size is zero and low contains all frame bytes
    // When: Splitting frame index one
    // Then: high size remains zero and low points to the correct frame boundary
    const unsigned char waveData[] = {0x10, 0x11, 0x12, 0x20, 0x21, 0x22};
    wave_file_publish::WaveFrameSplitView view{};
    const bool ok = wave_file_publish::BuildWaveFrameSplitView(waveData, 3U, 0U, 1, &view);

    AssertTrue(ok, "BuildWaveFrameSplitView should accept zero high size");
    AssertTrue(view.lowSize == 3U, "low size should be three");
    AssertTrue(view.highSize == 0U, "high size should be zero");
    AssertBufferEqual(view.lowData, waveData + 3, 3U, "low payload mismatch when high size is zero");
    AssertTrue(view.highData == (waveData + 6), "high pointer should be low end when high size is zero");
}

void TestBuildWaveFrameSplitView_FrameSizeOverflow()
{
    // Given: Frame sizes that overflow when added
    // When: Building frame split view
    // Then: Function returns false to prevent overflowed pointer arithmetic
    const unsigned char waveData[] = {0x10};
    wave_file_publish::WaveFrameSplitView view{};
    const bool ok = wave_file_publish::BuildWaveFrameSplitView(
        waveData,
        std::numeric_limits<size_t>::max(),
        1U,
        0,
        &view);

    AssertTrue(!ok, "BuildWaveFrameSplitView should fail on overflowed frame size");
}

void TestBuildWaveFrameSplitView_FrameOffsetMultiplicationOverflow()
{
    // Given: Frame geometry where frameSize * frameIndex overflows size_t
    // When: Building frame split view
    // Then: Function returns false to prevent wrapped frame offset
    const unsigned char waveData[] = {0x10};
    wave_file_publish::WaveFrameSplitView view{};
    const size_t maxSize = (std::numeric_limits<size_t>::max)();
    const int maxFrameIndex = (std::numeric_limits<int>::max)();
    const size_t overflowFrameSize = (maxSize / static_cast<size_t>(maxFrameIndex)) + 1U;
    const bool ok = wave_file_publish::BuildWaveFrameSplitView(
        waveData,
        overflowFrameSize,
        0U,
        maxFrameIndex,
        &view);

    AssertTrue(!ok, "BuildWaveFrameSplitView should fail when frame offset multiplication overflows");
}

void TestBuildWaveFrameSplitView_ThreeFrameSequentialOrder()
{
    // Given: Three sequential frames in low-first/high-next layout
    // When: Splitting all frames in order
    // Then: Collected low/high streams preserve frame order without swap
    const unsigned char waveData[] = {
        0x01, 0x11, 0x12,
        0x02, 0x21, 0x22,
        0x03, 0x31, 0x32,
    };
    std::vector<unsigned char> lowOutput;
    std::vector<unsigned char> highOutput;

    for (int frameIndex = 0; frameIndex < 3; ++frameIndex)
    {
        wave_file_publish::WaveFrameSplitView view{};
        const bool ok = wave_file_publish::BuildWaveFrameSplitView(waveData, 1U, 2U, frameIndex, &view);
        AssertTrue(ok, "BuildWaveFrameSplitView should succeed in sequential order test");
        lowOutput.insert(lowOutput.end(), view.lowData, view.lowData + view.lowSize);
        highOutput.insert(highOutput.end(), view.highData, view.highData + view.highSize);
    }

    const std::vector<unsigned char> expectedLow = {0x01, 0x02, 0x03};
    const std::vector<unsigned char> expectedHigh = {0x11, 0x12, 0x21, 0x22, 0x31, 0x32};
    AssertByteVectorEqual(lowOutput, expectedLow, "low order mismatch in sequential test");
    AssertByteVectorEqual(highOutput, expectedHigh, "high order mismatch in sequential test");
}

void TestBuildWaveFrameSplitView_Aebf296Compatibility_ExhaustiveSmallGrid()
{
    // Given: aebf296 frame layout formula and dense combinations of low/high sizes
    // When: Splitting all frames for each combination
    // Then: BuildWaveFrameSplitView must match aebf296-compatible pointer/size results exactly
    for (size_t lowSize = 0U; lowSize <= 6U; ++lowSize)
    {
        for (size_t highSize = 0U; highSize <= 6U; ++highSize)
        {
            if ((lowSize == 0U) && (highSize == 0U))
            {
                continue;
            }

            const size_t frameSize = lowSize + highSize;
            for (int frameCount = 1; frameCount <= 5; ++frameCount)
            {
                std::vector<unsigned char> waveData(static_cast<size_t>(frameCount) * frameSize);
                for (size_t i = 0; i < waveData.size(); ++i)
                {
                    waveData[i] = static_cast<unsigned char>((i * 19U + lowSize * 7U + highSize * 11U + static_cast<size_t>(frameCount)) & 0xFFU);
                }

                std::vector<unsigned char> lowExpected;
                std::vector<unsigned char> highExpected;
                std::vector<unsigned char> lowActual;
                std::vector<unsigned char> highActual;

                for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
                {
                    wave_file_publish::WaveFrameSplitView actualView{};
                    wave_file_publish::WaveFrameSplitView legacyView{};
                    const bool actualOk = wave_file_publish::BuildWaveFrameSplitView(waveData.data(), lowSize, highSize, frameIndex, &actualView);
                    const bool legacyOk = BuildLegacySplitViewAebf296(waveData.data(), lowSize, highSize, frameIndex, &legacyView);

                    AssertTrue(actualOk, "BuildWaveFrameSplitView should succeed in exhaustive compatibility test");
                    AssertTrue(legacyOk, "Legacy split should succeed in exhaustive compatibility test");
                    AssertTrue(actualView.lowSize == legacyView.lowSize, "low size mismatch in exhaustive compatibility test");
                    AssertTrue(actualView.highSize == legacyView.highSize, "high size mismatch in exhaustive compatibility test");
                    AssertBufferEqual(actualView.lowData, legacyView.lowData, lowSize, "low payload mismatch in exhaustive compatibility test");
                    AssertBufferEqual(actualView.highData, legacyView.highData, highSize, "high payload mismatch in exhaustive compatibility test");

                    lowExpected.insert(lowExpected.end(), legacyView.lowData, legacyView.lowData + legacyView.lowSize);
                    highExpected.insert(highExpected.end(), legacyView.highData, legacyView.highData + legacyView.highSize);
                    lowActual.insert(lowActual.end(), actualView.lowData, actualView.lowData + actualView.lowSize);
                    highActual.insert(highActual.end(), actualView.highData, actualView.highData + actualView.highSize);
                }

                AssertByteVectorEqual(lowActual, lowExpected, "low stream mismatch in exhaustive compatibility test");
                AssertByteVectorEqual(highActual, highExpected, "high stream mismatch in exhaustive compatibility test");
            }
        }
    }
}

void TestDialogMainSourceContract_Aebf296LowHighOrder()
{
    // Given: Dialog1_Main source code in repository
    // When: Inspecting function signatures and call sites as source-level contract
    // Then: low/high meaning and call order must stay aligned with aebf296-compatible data layout
    const std::filesystem::path sourcePath = FindDialogMainSourcePath();
    const std::string source = ReadTextFile(sourcePath);
    const std::string normalized = StripAsciiWhitespace(source);

    AssertTrue(
        CountOccurrences(normalized, "INTSaveWaveDataToFile(CFile*fp_l,CFile*fp_h,PBYTEWaveData,ULONGFrameSize_L,ULONGFrameSize_H,INTWaveCnt") >= 2U,
        "SaveWaveDataToFile should keep low/high argument order in declaration and definition");
    AssertTrue(
        CountOccurrences(normalized, "INTSaveWaveDataToFile(CFile*fp_h,CFile*fp_l,PBYTEWaveData,ULONGFrameSize_L,ULONGFrameSize_H,INTWaveCnt") == 0U,
        "SaveWaveDataToFile should not swap high/low argument order");

    AssertTrue(
        CountOccurrences(normalized, "INTCreateWaveDataFile(CFile*fp_l,CFile*fp_h,constCString&TimeStamp,INTIndex,wave_file_publish::WaveFilePairPath*outPath") >= 2U,
        "CreateWaveDataFile should keep low/high argument order in declaration and definition");
    AssertTrue(
        CountOccurrences(normalized, "INTCreateWaveDataFile(CFile*fp_h,CFile*fp_l,") == 0U,
        "CreateWaveDataFile should not swap high/low argument order");

    AssertTrue(
        CountOccurrences(normalized, "SaveWaveDataToFile(&File_Low,&File_High,") >= 1U,
        "SaveWaveDataToFile call sites should pass low file before high file");
    AssertTrue(
        CountOccurrences(normalized, "SaveWaveDataToFile(&File_High,&File_Low,") == 0U,
        "SaveWaveDataToFile call sites should not pass high file before low file");

    AssertTrue(
        CountOccurrences(normalized, "CreateWaveDataFile(&File_Low,&File_High,") >= 1U,
        "CreateWaveDataFile call sites should pass low file before high file");
    AssertTrue(
        CountOccurrences(normalized, "CreateWaveDataFile(&File_High,&File_Low,") == 0U,
        "CreateWaveDataFile call sites should not pass high file before low file");

    AssertTrue(
        CountOccurrences(normalized, "BuildWaveFrameSplitView(WaveData,(size_t)FrameSize_L,(size_t)FrameSize_H,i,&frameView)") >= 1U,
        "SaveWaveDataToFile should use BuildWaveFrameSplitView to keep split logic explicit");
    AssertTrue(
        CountOccurrences(normalized, "fp_l->Write(frameView.lowData,FrameSize_L);") >= 1U,
        "Low output write should use frameView.lowData");
    AssertTrue(
        CountOccurrences(normalized, "fp_h->Write(frameView.highData,FrameSize_H);") >= 1U,
        "High output write should use frameView.highData");
}

void TestValidateSavePath_NormalDirectory()
{
    // Given: Existing writable temp directory
    // When: Validating save path
    // Then: Validation succeeds with kSuccess
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto result = save_path_validation::ValidateSavePath(tempDir.wstring());

    AssertEqual(result.code, save_path_validation::ValidationCode::kSuccess, "ValidateSavePath should accept writable temp directory");
}

void TestValidateSavePath_EmptyPath()
{
    // Given: Empty save path
    // When: Validating save path
    // Then: Validation fails with kEmptyPath
    const auto result = save_path_validation::ValidateSavePath(L"");

    AssertEqual(result.code, save_path_validation::ValidationCode::kEmptyPath, "ValidateSavePath should reject empty path");
}

void TestValidateSavePath_WhitespaceOnlyPath()
{
    // Given: Whitespace-only save path
    // When: Validating save path
    // Then: Validation fails with kEmptyPath
    const auto result = save_path_validation::ValidateSavePath(L"   \t");

    AssertEqual(result.code, save_path_validation::ValidationCode::kEmptyPath, "ValidateSavePath should reject whitespace-only path");
}

void TestValidateSavePath_ParentTraversal()
{
    // Given: Path containing parent traversal segment
    // When: Validating save path
    // Then: Validation fails with kParentTraversal
    const auto result = save_path_validation::ValidateSavePath(L"C:\\tmp\\..\\capture");

    AssertEqual(result.code, save_path_validation::ValidationCode::kParentTraversal, "ValidateSavePath should reject parent traversal");
}

void TestValidateSavePath_DirectoryNotFound()
{
    // Given: Non-existing directory path
    // When: Validating save path
    // Then: Validation fails with kDirectoryNotFound
    const auto uniquePath = BuildUniquePrefixPath() + L"_missing_dir";
    const auto result = save_path_validation::ValidateSavePath(uniquePath);

    AssertEqual(result.code, save_path_validation::ValidationCode::kDirectoryNotFound, "ValidateSavePath should reject missing directory");
}

void TestValidateSavePath_NotDirectory()
{
    // Given: Existing file path
    // When: Validating save path
    // Then: Validation fails with kNotDirectory
    const auto filePath = BuildUniquePrefixPath() + L"_savepath_file.txt";
    ScopedFileCleanup cleanup;
    cleanup.Add(filePath);
    WriteBinaryFile(filePath, { 0x01, 0x02 });

    const auto result = save_path_validation::ValidateSavePath(filePath);
    AssertEqual(result.code, save_path_validation::ValidationCode::kNotDirectory, "ValidateSavePath should reject non-directory path");
}

void TestValidateSavePath_NotWritableByProbeFailure()
{
    // Given: Existing directory and write probe that always fails
    // When: Validating save path
    // Then: Validation fails with kNotWritable
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto result = save_path_validation::ValidateSavePath(
        tempDir.wstring(),
        [](const std::wstring&, std::wstring*) { return false; });

    AssertEqual(result.code, save_path_validation::ValidationCode::kNotWritable, "ValidateSavePath should report not writable when probe fails");
}

void TestDialogMainSourceContract_SavePathValidation()
{
    // Given: Dialog1_Main source code in repository
    // When: Inspecting save path validation call sites
    // Then: Save path validation should be invoked in Set Parameters and save path update path
    const std::filesystem::path sourcePath = FindDialogMainSourcePath();
    const std::string source = ReadTextFile(sourcePath);
    const std::string normalized = StripAsciiWhitespace(source);

    AssertTrue(
        CountOccurrences(normalized, "ValidateSavePathForUi(") >= 5U,
        "Dialog1_Main should call ValidateSavePathForUi in multiple save path entry points");
    AssertTrue(
        CountOccurrences(normalized, "save_path_validation::ValidateSavePath(std::wstring(savePath.GetString()))") >= 1U,
        "ValidateSavePathForUi should delegate to save_path_validation::ValidateSavePath");
}

void TestSavePathValidationSourceContract_ProbeDoesNotRequireDeletePermission()
{
    // Given: SavePathValidation source code in repository
    // When: Inspecting write probe implementation details
    // Then: Probe should not require delete-on-close permission and should cleanup best-effort
    const std::filesystem::path sourcePath = FindSavePathValidationSourcePath();
    const std::string source = ReadTextFile(sourcePath);
    const std::string normalized = StripAsciiWhitespace(source);

    AssertTrue(
        CountOccurrences(normalized, "FILE_FLAG_DELETE_ON_CLOSE") == 0U,
        "DefaultWriteProbe should not use FILE_FLAG_DELETE_ON_CLOSE");
    AssertTrue(
        CountOccurrences(normalized, "::DeleteFileW(probePath.c_str());") >= 1U,
        "DefaultWriteProbe should perform best-effort cleanup after probing");
}

void TestPublishWaveFilePair_EndToEnd_Aebf296CompatibleContent()
{
    // Given: aebf296-compatible two-frame payload and tmp paths
    // When: Writing low/high tmp files and publishing them to final names
    // Then: Final file contents match low/high streams and tmp files are removed
    wave_file_publish::WaveFilePairPath paths;
    const bool pathOk = wave_file_publish::BuildWaveFilePairPath(BuildUniquePrefixPath(), 7, &paths);
    AssertTrue(pathOk, "BuildWaveFilePairPath should succeed for integration content test");

    ScopedFileCleanup cleanup;
    cleanup.Add(paths.lowTempPath);
    cleanup.Add(paths.highTempPath);
    cleanup.Add(paths.lowFinalPath);
    cleanup.Add(paths.highFinalPath);

    const unsigned char waveData[] = {
        0x10, 0x11, 0x20, 0x21, 0x22,
        0x30, 0x31, 0x40, 0x41, 0x42,
    };
    std::vector<unsigned char> lowPayload;
    std::vector<unsigned char> highPayload;

    for (int frameIndex = 0; frameIndex < 2; ++frameIndex)
    {
        wave_file_publish::WaveFrameSplitView view{};
        const bool splitOk = wave_file_publish::BuildWaveFrameSplitView(waveData, 2U, 3U, frameIndex, &view);
        AssertTrue(splitOk, "BuildWaveFrameSplitView should succeed in integration content test");
        lowPayload.insert(lowPayload.end(), view.lowData, view.lowData + view.lowSize);
        highPayload.insert(highPayload.end(), view.highData, view.highData + view.highSize);
    }

    WriteBinaryFile(paths.lowTempPath, lowPayload);
    WriteBinaryFile(paths.highTempPath, highPayload);

    const auto result = wave_file_publish::PublishWaveFilePair(paths, RenameReplaceByFilesystem);
    AssertEqual(result, wave_file_publish::PublishResult::kSuccess, "Publish should succeed in integration content test");

    AssertTrue(!ExistsFile(paths.lowTempPath), "Low tmp file should be removed after publish");
    AssertTrue(!ExistsFile(paths.highTempPath), "High tmp file should be removed after publish");
    AssertTrue(ExistsFile(paths.lowFinalPath), "Low final file should exist after publish");
    AssertTrue(ExistsFile(paths.highFinalPath), "High final file should exist after publish");

    const auto actualLow = ReadBinaryFile(paths.lowFinalPath);
    const auto actualHigh = ReadBinaryFile(paths.highFinalPath);
    AssertByteVectorEqual(actualLow, lowPayload, "Low final payload mismatch in integration content test");
    AssertByteVectorEqual(actualHigh, highPayload, "High final payload mismatch in integration content test");
}

void TestPublishWaveFilePair_EndToEnd_OverwriteExistingFinal()
{
    // Given: Existing final files and new tmp payloads
    // When: Publishing tmp files with replace semantics
    // Then: Existing finals are overwritten with new payload
    wave_file_publish::WaveFilePairPath paths;
    const bool pathOk = wave_file_publish::BuildWaveFilePairPath(BuildUniquePrefixPath(), 8, &paths);
    AssertTrue(pathOk, "BuildWaveFilePairPath should succeed for overwrite test");

    ScopedFileCleanup cleanup;
    cleanup.Add(paths.lowTempPath);
    cleanup.Add(paths.highTempPath);
    cleanup.Add(paths.lowFinalPath);
    cleanup.Add(paths.highFinalPath);

    const std::vector<unsigned char> oldLow = {0xAA, 0xBB};
    const std::vector<unsigned char> oldHigh = {0xCC, 0xDD};
    const std::vector<unsigned char> newLow = {0x01, 0x02, 0x03};
    const std::vector<unsigned char> newHigh = {0x11, 0x12, 0x13, 0x14};

    WriteBinaryFile(paths.lowFinalPath, oldLow);
    WriteBinaryFile(paths.highFinalPath, oldHigh);
    WriteBinaryFile(paths.lowTempPath, newLow);
    WriteBinaryFile(paths.highTempPath, newHigh);

    const auto result = wave_file_publish::PublishWaveFilePair(paths, RenameReplaceByFilesystem);
    AssertEqual(result, wave_file_publish::PublishResult::kSuccess, "Publish should succeed in overwrite test");

    const auto actualLow = ReadBinaryFile(paths.lowFinalPath);
    const auto actualHigh = ReadBinaryFile(paths.highFinalPath);
    AssertByteVectorEqual(actualLow, newLow, "Low final file should be overwritten");
    AssertByteVectorEqual(actualHigh, newHigh, "High final file should be overwritten");
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
        TestBuildWaveFrameSplitView_IndexZero();
        TestBuildWaveFrameSplitView_IndexPlusOne();
        TestBuildWaveFrameSplitView_NullWaveData();
        TestBuildWaveFrameSplitView_NullOutView();
        TestBuildWaveFrameSplitView_NegativeIndex();
        TestBuildWaveFrameSplitView_ZeroTotalFrameSize();
        TestBuildWaveFrameSplitView_Aebf296Compatibility_TwoFrameOutput();
        TestBuildWaveFrameSplitView_LowSizeZero();
        TestBuildWaveFrameSplitView_HighSizeZero();
        TestBuildWaveFrameSplitView_FrameSizeOverflow();
        TestBuildWaveFrameSplitView_FrameOffsetMultiplicationOverflow();
        TestBuildWaveFrameSplitView_ThreeFrameSequentialOrder();
        TestBuildWaveFrameSplitView_Aebf296Compatibility_ExhaustiveSmallGrid();
        TestDialogMainSourceContract_Aebf296LowHighOrder();
        TestValidateSavePath_NormalDirectory();
        TestValidateSavePath_EmptyPath();
        TestValidateSavePath_WhitespaceOnlyPath();
        TestValidateSavePath_ParentTraversal();
        TestValidateSavePath_DirectoryNotFound();
        TestValidateSavePath_NotDirectory();
        TestValidateSavePath_NotWritableByProbeFailure();
        TestDialogMainSourceContract_SavePathValidation();
        TestSavePathValidationSourceContract_ProbeDoesNotRequireDeletePermission();
        TestPublishWaveFilePair_EndToEnd_Aebf296CompatibleContent();
        TestPublishWaveFilePair_EndToEnd_OverwriteExistingFinal();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "TEST FAILED: " << ex.what() << std::endl;
        return 1;
    }

    std::cout << "All tests passed." << std::endl;
    return 0;
}
