#ifdef _WIN32
#include "pch.h"
#endif
#include "WaveFilePublish.h"

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

} // namespace wave_file_publish
