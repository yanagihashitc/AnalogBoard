#pragma once

#include <windows.h>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

namespace WaveDataFileIO
{
    constexpr INT kSaveWaveDataOk = 0;
    constexpr INT kSaveWaveDataWriteLowFailed = -1;
    constexpr INT kSaveWaveDataWriteHighFailed = -2;

    class StdFileWriter
    {
    public:
        StdFileWriter() = default;
        ~StdFileWriter()
        {
            Close();
        }

        bool Open(LPCWSTR path)
        {
            Close();
            if (path == nullptr)
            {
                return false;
            }

            FILE* fp = nullptr;
            if (_wfopen_s(&fp, path, L"wb") != 0)
            {
                return false;
            }

            file_ = fp;
            return true;
        }

        bool Write(const BYTE* data, ULONG size)
        {
            if (!IsOpen())
            {
                return false;
            }

            if (size == 0)
            {
                return true;
            }

            if (data == nullptr)
            {
                return false;
            }

            const size_t wrote = std::fwrite(data, 1, static_cast<size_t>(size), file_);
            return wrote == static_cast<size_t>(size);
        }

        bool Flush()
        {
            if (!IsOpen())
            {
                return false;
            }

            return std::fflush(file_) == 0;
        }

        bool Close()
        {
            if (!IsOpen())
            {
                return true;
            }

            FILE* fp = file_;
            file_ = nullptr;
            return std::fclose(fp) == 0;
        }

        bool IsOpen() const
        {
            return file_ != nullptr;
        }

    private:
        FILE* file_ = nullptr;
    };

    template <typename FileWriter>
    INT SaveWaveDataToFileImpl(
        FileWriter& fpLow,
        FileWriter& fpHigh,
        const BYTE* waveData,
        ULONG frameSizeLow,
        ULONG frameSizeHigh,
        INT waveCnt)
    {
        if (waveCnt < 0)
        {
            return kSaveWaveDataWriteLowFailed;
        }

        if (waveCnt > 0 && waveData == nullptr)
        {
            return kSaveWaveDataWriteLowFailed;
        }

        const size_t frameSize = static_cast<size_t>(frameSizeLow) + static_cast<size_t>(frameSizeHigh);
        for (INT i = 0; i < waveCnt; ++i)
        {
            const BYTE* frameTop = waveData + (static_cast<size_t>(i) * frameSize);

            if (frameSizeLow > 0 && !fpLow.Write(frameTop, frameSizeLow))
            {
                return kSaveWaveDataWriteLowFailed;
            }

            if (frameSizeHigh > 0 && !fpHigh.Write(frameTop + static_cast<size_t>(frameSizeLow), frameSizeHigh))
            {
                return kSaveWaveDataWriteHighFailed;
            }
        }

        return kSaveWaveDataOk;
    }

    struct RenameAttemptResult
    {
        bool success = false;
        bool retried = false;
        DWORD lastError = ERROR_SUCCESS;
    };

    inline RenameAttemptResult RenameTempFileWithRetry(
        LPCWSTR tmpPath,
        LPCWSTR finalPath,
        DWORD retryWaitMs = 100)
    {
        RenameAttemptResult result = {};
        if (tmpPath == nullptr || finalPath == nullptr)
        {
            result.lastError = ERROR_INVALID_PARAMETER;
            return result;
        }

        if (::MoveFileExW(tmpPath, finalPath, MOVEFILE_REPLACE_EXISTING) != FALSE)
        {
            result.success = true;
            result.lastError = ERROR_SUCCESS;
            return result;
        }

        result.lastError = ::GetLastError();
        result.retried = true;
        ::Sleep(retryWaitMs);

        if (::MoveFileExW(tmpPath, finalPath, MOVEFILE_REPLACE_EXISTING) != FALSE)
        {
            result.success = true;
            result.lastError = ERROR_SUCCESS;
            return result;
        }

        result.lastError = ::GetLastError();
        return result;
    }

    struct PublishPairResult
    {
        bool success = false;
        RenameAttemptResult low;
        RenameAttemptResult high;
        bool rollbackAttempted = false;
        bool rollbackSucceeded = false;
        DWORD rollbackLastError = ERROR_SUCCESS;
    };

    inline PublishPairResult PublishWavePairAtomic(
        LPCWSTR tmpPathLow,
        LPCWSTR finalPathLow,
        LPCWSTR tmpPathHigh,
        LPCWSTR finalPathHigh,
        DWORD retryWaitMs = 100)
    {
        PublishPairResult result = {};

        result.low = RenameTempFileWithRetry(tmpPathLow, finalPathLow, retryWaitMs);
        if (!result.low.success)
        {
            return result;
        }

        result.high = RenameTempFileWithRetry(tmpPathHigh, finalPathHigh, retryWaitMs);
        if (!result.high.success)
        {
            result.rollbackAttempted = true;
            result.rollbackSucceeded = (::DeleteFileW(finalPathLow) != FALSE);
            if (!result.rollbackSucceeded)
            {
                result.rollbackLastError = ::GetLastError();
            }
            return result;
        }

        result.success = true;
        return result;
    }

    struct CleanupFailureInfo
    {
        std::wstring path;
        DWORD lastError = ERROR_SUCCESS;
    };

    struct CleanupTmpResult
    {
        int deletedCount = 0;
        int failedCount = 0;
        std::vector<CleanupFailureInfo> failures;
    };

    inline bool EndsWithCaseInsensitive(const std::wstring& text, const wchar_t* suffix)
    {
        if (suffix == nullptr)
        {
            return false;
        }

        const std::wstring suffixStr(suffix);
        if (text.size() < suffixStr.size())
        {
            return false;
        }

        const size_t start = text.size() - suffixStr.size();
        for (size_t i = 0; i < suffixStr.size(); ++i)
        {
            if (std::towlower(text[start + i]) != std::towlower(suffixStr[i]))
            {
                return false;
            }
        }

        return true;
    }

    inline bool ContainsCaseInsensitive(const std::wstring& text, const wchar_t* token)
    {
        if (token == nullptr)
        {
            return false;
        }

        std::wstring lowerText = text;
        for (wchar_t& c : lowerText)
        {
            c = static_cast<wchar_t>(std::towlower(c));
        }

        std::wstring lowerToken(token);
        for (wchar_t& c : lowerToken)
        {
            c = static_cast<wchar_t>(std::towlower(c));
        }

        return lowerText.find(lowerToken) != std::wstring::npos;
    }

    inline bool IsCleanupTargetFileName(const std::wstring& fileName)
    {
        if (!EndsWithCaseInsensitive(fileName, L".bin.tmp"))
        {
            return false;
        }

        return ContainsCaseInsensitive(fileName, L"_fl_") || ContainsCaseInsensitive(fileName, L"_fh_");
    }

    inline CleanupTmpResult CleanupResidualBinTmpFiles(LPCWSTR directoryPath)
    {
        CleanupTmpResult result = {};
        if (directoryPath == nullptr || directoryPath[0] == L'\0')
        {
            return result;
        }

        std::wstring dir(directoryPath);
        if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/')
        {
            dir.push_back(L'\\');
        }

        const std::wstring searchPath = dir + L"*.bin.tmp";

        WIN32_FIND_DATAW findData = {};
        HANDLE hFind = ::FindFirstFileW(searchPath.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE)
        {
            return result;
        }

        do
        {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                continue;
            }

            const std::wstring fileName(findData.cFileName);
            if (!IsCleanupTargetFileName(fileName))
            {
                continue;
            }

            const std::wstring fullPath = dir + fileName;
            if (::DeleteFileW(fullPath.c_str()) != FALSE)
            {
                ++result.deletedCount;
            }
            else
            {
                ++result.failedCount;
                CleanupFailureInfo fail = {};
                fail.path = fullPath;
                fail.lastError = ::GetLastError();
                result.failures.push_back(fail);
            }
        } while (::FindNextFileW(hFind, &findData) != FALSE);

        ::FindClose(hFind);
        return result;
    }
}
