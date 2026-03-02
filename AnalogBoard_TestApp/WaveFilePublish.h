#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace wave_file_publish
{

struct WaveFilePairPath
{
    std::wstring lowFinalPath;
    std::wstring highFinalPath;
    std::wstring lowTempPath;
    std::wstring highTempPath;
};

enum class PublishResult
{
    kSuccess = 0,
    kInvalidArgument = -1,
    kLowRenameFailed = -2,
    kHighRenameFailed = -3,
};

using RenameFunction = std::function<bool(const std::wstring&, const std::wstring&)>;

struct WaveFrameSplitView
{
    const unsigned char* lowData;
    const unsigned char* highData;
    size_t lowSize;
    size_t highSize;
};

bool BuildWaveFilePairPath(const std::wstring& prefixPath, int index, WaveFilePairPath* outPath);
PublishResult PublishWaveFilePair(const WaveFilePairPath& path, const RenameFunction& renameFunction = RenameFunction());
bool BuildWaveFrameSplitView(const unsigned char* waveData, size_t frameSizeLow, size_t frameSizeHigh, int frameIndex, WaveFrameSplitView* outView);

} // namespace wave_file_publish
