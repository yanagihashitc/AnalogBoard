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

    template <typename FileWriterLow, typename FileWriterHigh>
    INT SaveWaveDataToFileImpl(
        FileWriterLow* fpLow,
        FileWriterHigh* fpHigh,
        const BYTE* waveData,
        ULONG frameSizeLow,
        ULONG frameSizeHigh,
        INT waveCnt)
    {
        // Pointer overload intentionally treats nullptr writer as "lane disabled".
        // Callers can write only one lane by passing nullptr for the other writer.
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

            if (frameSizeLow > 0 && fpLow != nullptr && !fpLow->Write(frameTop, frameSizeLow))
            {
                return kSaveWaveDataWriteLowFailed;
            }

            if (frameSizeHigh > 0 && fpHigh != nullptr &&
                !fpHigh->Write(frameTop + static_cast<size_t>(frameSizeLow), frameSizeHigh))
            {
                return kSaveWaveDataWriteHighFailed;
            }
        }

        return kSaveWaveDataOk;
    }

    template <typename FileWriterLow, typename FileWriterHigh>
    INT SaveWaveDataToFileImpl(
        FileWriterLow& fpLow,
        FileWriterHigh& fpHigh,
        const BYTE* waveData,
        ULONG frameSizeLow,
        ULONG frameSizeHigh,
        INT waveCnt)
    {
        return SaveWaveDataToFileImpl(&fpLow, &fpHigh, waveData, frameSizeLow, frameSizeHigh, waveCnt);
    }

    struct RenameAttemptResult
    {
        bool success = false;
        bool retried = false;
        int attemptCount = 0;
        DWORD lastError = ERROR_SUCCESS;
    };

    inline bool MakeLowRollbackBackupPath(
        LPCWSTR lowPath,
        std::wstring* outBackupPath,
        DWORD pid,
        ULONGLONG tick,
        int maxCandidates = 32)
    {
        if (lowPath == nullptr || outBackupPath == nullptr || maxCandidates <= 0)
        {
            ::SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }

        for (int i = 0; i < maxCandidates; ++i)
        {
            std::wstring candidate = lowPath;
            candidate += L".rollback.";
            candidate += std::to_wstring(pid);
            candidate += L".";
            candidate += std::to_wstring(tick + static_cast<ULONGLONG>(i));
            if (::GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                *outBackupPath = candidate;
                return true;
            }
        }

        ::SetLastError(ERROR_ALREADY_EXISTS);
        return false;
    }

    inline bool MakeLowRollbackBackupPath(
        LPCWSTR lowPath,
        std::wstring* outBackupPath,
        int maxCandidates = 32)
    {
        return MakeLowRollbackBackupPath(
            lowPath,
            outBackupPath,
            ::GetCurrentProcessId(),
            ::GetTickCount64(),
            maxCandidates);
    }

    inline RenameAttemptResult RenameTempFileWithRetry(
        LPCWSTR tmpPath,
        LPCWSTR finalPath,
        DWORD retryWaitMs = 100,
        int maxRetries = 1)
    {
        RenameAttemptResult result = {};
        if (tmpPath == nullptr || finalPath == nullptr)
        {
            result.lastError = ERROR_INVALID_PARAMETER;
            return result;
        }
        if (maxRetries < 0)
        {
            result.lastError = ERROR_INVALID_PARAMETER;
            return result;
        }

        ++result.attemptCount;
        if (::MoveFileExW(tmpPath, finalPath, MOVEFILE_REPLACE_EXISTING) != FALSE)
        {
            result.success = true;
            result.lastError = ERROR_SUCCESS;
            return result;
        }

        result.lastError = ::GetLastError();
        // Total attempts = 1 initial try + maxRetries.
        for (int retry = 0; retry < maxRetries; ++retry)
        {
            result.retried = true;
            if (retryWaitMs > 0)
            {
                ::Sleep(retryWaitMs);
            }

            ++result.attemptCount;
            if (::MoveFileExW(tmpPath, finalPath, MOVEFILE_REPLACE_EXISTING) != FALSE)
            {
                result.success = true;
                result.lastError = ERROR_SUCCESS;
                return result;
            }

            result.lastError = ::GetLastError();
        }
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
        DWORD retryWaitMs = 100,
        int maxRetries = 1)
    {
        PublishPairResult result = {};
        std::wstring lowBackupPath;
        bool lowBackupExists = false;

        DWORD lowAttrs = ::GetFileAttributesW(finalPathLow);
        if (lowAttrs != INVALID_FILE_ATTRIBUTES && (lowAttrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            if (!MakeLowRollbackBackupPath(finalPathLow, &lowBackupPath))
            {
                result.low.lastError = ::GetLastError();
                return result;
            }

            if (::MoveFileExW(finalPathLow, lowBackupPath.c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE)
            {
                result.low.lastError = ::GetLastError();
                return result;
            }

            lowBackupExists = true;
        }

        result.low = RenameTempFileWithRetry(tmpPathLow, finalPathLow, retryWaitMs, maxRetries);
        if (!result.low.success)
        {
            if (lowBackupExists)
            {
                result.rollbackAttempted = true;
                result.rollbackSucceeded = (::MoveFileExW(lowBackupPath.c_str(), finalPathLow, MOVEFILE_REPLACE_EXISTING) != FALSE);
                if (!result.rollbackSucceeded)
                {
                    result.rollbackLastError = ::GetLastError();
                }
            }
            return result;
        }

        result.high = RenameTempFileWithRetry(tmpPathHigh, finalPathHigh, retryWaitMs, maxRetries);
        if (!result.high.success)
        {
            result.rollbackAttempted = true;

            const bool restoredLowTmp =
                (::MoveFileExW(finalPathLow, tmpPathLow, MOVEFILE_REPLACE_EXISTING) != FALSE);
            if (!restoredLowTmp)
            {
                result.rollbackLastError = ::GetLastError();
                return result;
            }

            if (lowBackupExists)
            {
                const bool restoredLow =
                    (::MoveFileExW(lowBackupPath.c_str(), finalPathLow, MOVEFILE_REPLACE_EXISTING) != FALSE);
                if (!restoredLow)
                {
                    result.rollbackLastError = ::GetLastError();
                }
                result.rollbackSucceeded = restoredLow;
            }
            else
            {
                result.rollbackSucceeded = true;
            }
            return result;
        }

        if (lowBackupExists)
        {
            ::DeleteFileW(lowBackupPath.c_str());
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
        if (EndsWithCaseInsensitive(fileName, L".bin.tmp"))
        {
            return ContainsCaseInsensitive(fileName, L"_fl_") || ContainsCaseInsensitive(fileName, L"_fh_");
        }

        if (ContainsCaseInsensitive(fileName, L".bin.rollback."))
        {
            return ContainsCaseInsensitive(fileName, L"_fl_") || ContainsCaseInsensitive(fileName, L"_fh_");
        }

        return false;
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

        const std::wstring searchPath = dir + L"*";

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
