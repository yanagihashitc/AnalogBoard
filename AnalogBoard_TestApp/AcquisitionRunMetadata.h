#pragma once

#include <windows.h>

#include <cstdio>
#include <string>

#include "WaveAcquisitionEngine.h"

namespace AcquisitionRunMetadata
{
    struct AppendResult
    {
        bool success = false;
        DWORD lastError = ERROR_SUCCESS;
    };

    inline AppendResult AppendRunResultMetadata(
        LPCWSTR cfgPath,
        const WaveAcquisition::AcquisitionSummary& summary)
    {
        AppendResult result = {};

        if (cfgPath == nullptr || cfgPath[0] == L'\0')
        {
            result.lastError = ERROR_INVALID_PARAMETER;
            return result;
        }

        const DWORD fileAttributes = ::GetFileAttributesW(cfgPath);
        if (fileAttributes == INVALID_FILE_ATTRIBUTES)
        {
            result.lastError = ::GetLastError();
            return result;
        }

        FILE* file = nullptr;
        if (_wfopen_s(&file, cfgPath, L"a") != 0 || file == nullptr)
        {
            result.lastError = ::GetLastError();
            if (result.lastError == ERROR_SUCCESS)
            {
                result.lastError = ERROR_OPEN_FAILED;
            }
            return result;
        }

        std::string status = "unknown";
        if (const wchar_t* statusWide = WaveAcquisition::WaveAcquisitionEngine::ToString(summary.terminalStatus))
        {
            status.clear();
            while (*statusWide != L'\0')
            {
                status.push_back(static_cast<char>(*statusWide));
                ++statusWide;
            }
        }

        const int writeResult = std::fprintf(
            file,
            "\n# Acquisition Result\n"
            "Run Status:,%s\n"
            "Run Error Code:,%d\n"
            "Published Pairs:,%d\n"
            "Saved Wave Count:,%lu\n"
            "Ignored Tail Bytes:,%lu\n",
            status.c_str(),
            summary.errorCode,
            summary.publishedPairCount,
            summary.savedWaveCount,
            summary.ignoredTailBytes);

        const int flushResult = std::fflush(file);
        const int closeResult = std::fclose(file);
        if (writeResult < 0 || flushResult != 0 || closeResult != 0)
        {
            result.lastError = ::GetLastError();
            if (result.lastError == ERROR_SUCCESS)
            {
                result.lastError = ERROR_WRITE_FAULT;
            }
            return result;
        }

        result.success = true;
        return result;
    }
}
