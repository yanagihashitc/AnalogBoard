#include "WaveFilePublish.h"

#include <limits>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace wave_file_publish
{
namespace
{

bool HasValidPath(const std::wstring& path)
{
    return !path.empty();
}

bool HasValidFrameSize(size_t frameSizeLow, size_t frameSizeHigh)
{
    if ((frameSizeLow == 0U) && (frameSizeHigh == 0U))
    {
        return false;
    }

    return frameSizeLow <= ((std::numeric_limits<size_t>::max)() - frameSizeHigh);
}

bool DefaultRename(const std::wstring& srcPath, const std::wstring& dstPath)
{
#ifdef _WIN32
    return MoveFileExW(srcPath.c_str(), dstPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != FALSE;
#else
    (void)srcPath;
    (void)dstPath;
    return false;
#endif
}

} // namespace

bool BuildWaveFilePairPath(const std::wstring& prefixPath, int index, WaveFilePairPath* outPath)
{
    if (outPath == nullptr)
    {
        return false;
    }

    if (prefixPath.empty() || index < 0)
    {
        return false;
    }

    const std::wstring indexString = std::to_wstring(index);

    outPath->lowFinalPath = prefixPath + L"_fl_" + indexString + L".bin";
    outPath->highFinalPath = prefixPath + L"_fh_" + indexString + L".bin";
    outPath->lowTempPath = outPath->lowFinalPath + L".tmp";
    outPath->highTempPath = outPath->highFinalPath + L".tmp";

    return true;
}

PublishResult PublishWaveFilePair(const WaveFilePairPath& path, const RenameFunction& renameFunction)
{
    if (!HasValidPath(path.lowFinalPath) ||
        !HasValidPath(path.highFinalPath) ||
        !HasValidPath(path.lowTempPath) ||
        !HasValidPath(path.highTempPath))
    {
        return PublishResult::kInvalidArgument;
    }

    const RenameFunction rename = renameFunction ? renameFunction : DefaultRename;

    if (!rename(path.lowTempPath, path.lowFinalPath))
    {
        return PublishResult::kLowRenameFailed;
    }

    if (!rename(path.highTempPath, path.highFinalPath))
    {
        // Best-effort rollback to avoid leaving only one side published.
        (void)rename(path.lowFinalPath, path.lowTempPath);
        return PublishResult::kHighRenameFailed;
    }

    return PublishResult::kSuccess;
}

bool BuildWaveFrameSplitView(const unsigned char* waveData, size_t frameSizeLow, size_t frameSizeHigh, int frameIndex, WaveFrameSplitView* outView)
{
    if ((waveData == nullptr) || (outView == nullptr))
    {
        return false;
    }

    if ((frameIndex < 0) || !HasValidFrameSize(frameSizeLow, frameSizeHigh))
    {
        return false;
    }

    const size_t oneFrameSize = frameSizeLow + frameSizeHigh;
    const size_t offset = oneFrameSize * static_cast<size_t>(frameIndex);

    outView->lowData = waveData + offset;
    outView->highData = outView->lowData + frameSizeLow;
    outView->lowSize = frameSizeLow;
    outView->highSize = frameSizeHigh;

    return true;
}

} // namespace wave_file_publish
