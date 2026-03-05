#pragma once

#include <windows.h>

#include <cwctype>
#include <string>

namespace SavePathValidation
{
constexpr int kSavePathValidationOk = 0;
constexpr int kSavePathInvalidOutputPath = -20001;
constexpr int kSavePathOutputPathNotFound = -20002;
constexpr int kSavePathOutputPathNotWritable = -20003;

enum class UiValidationTrigger
{
    kStartup,
    kFolderDialogConfirmed,
    kFolderDialogCancel,
    kSetParameters,
    kTextChanged
};

struct Result
{
    int code = kSavePathValidationOk;
    std::wstring normalizedPath;
    std::wstring message;
};

using WriteProbe = bool(*)(const std::wstring& directoryPath, DWORD* outLastError);

inline bool IsPathSeparator(const wchar_t ch)
{
    return ch == L'\\' || ch == L'/';
}

inline std::wstring Trim(const std::wstring& value)
{
    size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin]))
    {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1]))
    {
        --end;
    }

    return value.substr(begin, end - begin);
}

inline bool IsAbsolutePath(const std::wstring& path)
{
    if (path.size() >= 2 && path[1] == L':')
    {
        return true;
    }
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
    {
        return true;
    }
    return false;
}

inline bool ContainsControlCharacter(const std::wstring& path)
{
    for (const wchar_t ch : path)
    {
        if (static_cast<unsigned int>(ch) <= 31u)
        {
            return true;
        }
    }
    return false;
}

inline bool ContainsPathTraversalSegment(const std::wstring& path)
{
    std::wstring token;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        const wchar_t ch = (i < path.size()) ? path[i] : L'\\';
        if (IsPathSeparator(ch))
        {
            if (token == L"..")
            {
                return true;
            }
            token.clear();
            continue;
        }

        token.push_back(ch);
    }

    return false;
}

inline bool IsReservedBaseName(const std::wstring& baseNameUpper)
{
    if (baseNameUpper == L"CON" ||
        baseNameUpper == L"PRN" ||
        baseNameUpper == L"AUX" ||
        baseNameUpper == L"NUL")
    {
        return true;
    }

    if (baseNameUpper.size() == 4)
    {
        if (baseNameUpper.substr(0, 3) == L"COM" ||
            baseNameUpper.substr(0, 3) == L"LPT")
        {
            const wchar_t suffix = baseNameUpper[3];
            return suffix >= L'1' && suffix <= L'9';
        }
    }

    return false;
}

inline bool ContainsReservedNameSegment(const std::wstring& path)
{
    std::wstring segment;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        const wchar_t ch = (i < path.size()) ? path[i] : L'\\';
        if (IsPathSeparator(ch))
        {
            if (!segment.empty())
            {
                while (!segment.empty() && (segment.back() == L' ' || segment.back() == L'.'))
                {
                    segment.pop_back();
                }

                // Skip drive roots such as "C:" so they are not treated as reserved basenames.
                if (!segment.empty() && segment.back() != L':')
                {
                    const size_t dotPos = segment.find(L'.');
                    std::wstring baseName = dotPos == std::wstring::npos
                        ? segment
                        : segment.substr(0, dotPos);

                    for (wchar_t& c : baseName)
                    {
                        c = static_cast<wchar_t>(std::towupper(c));
                    }

                    if (!baseName.empty() && IsReservedBaseName(baseName))
                    {
                        return true;
                    }
                }
            }

            segment.clear();
            continue;
        }

        segment.push_back(ch);
    }

    return false;
}

inline std::wstring StripTrailingSeparator(const std::wstring& path)
{
    std::wstring out = path;
    while (out.size() > 3 && IsPathSeparator(out.back()))
    {
        out.pop_back();
    }
    return out;
}

inline Result MakeError(const int code, const std::wstring& message)
{
    Result result;
    result.code = code;
    result.message = message;
    return result;
}

inline std::wstring BuildWriteProbePathForTest(
    const std::wstring& directoryPath,
    const DWORD processId,
    const ULONGLONG tickBase,
    const int attempt)
{
    std::wstring probePath = directoryPath;
    if (!probePath.empty() && !IsPathSeparator(probePath.back()))
    {
        probePath += L"\\";
    }

    probePath += L".__write_probe_";
    probePath += std::to_wstring(processId);
    probePath += L"_";
    probePath += std::to_wstring(tickBase);
    probePath += L"_";
    probePath += std::to_wstring(attempt);
    probePath += L".tmp";
    return probePath;
}

inline bool DefaultWriteProbeWithSeed(
    const std::wstring& directoryPath,
    DWORD* outLastError,
    const DWORD processId,
    const ULONGLONG tickBase,
    const int maxAttempts)
{
    if (outLastError != nullptr)
    {
        *outLastError = ERROR_SUCCESS;
    }
    if (maxAttempts <= 0)
    {
        if (outLastError != nullptr)
        {
            *outLastError = ERROR_INVALID_PARAMETER;
        }
        return false;
    }

    for (int attempt = 0; attempt < maxAttempts; ++attempt)
    {
        const std::wstring probePath = BuildWriteProbePathForTest(directoryPath, processId, tickBase, attempt);
        HANDLE handle = ::CreateFileW(
            probePath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            const DWORD createError = ::GetLastError();
            if (createError == ERROR_FILE_EXISTS || createError == ERROR_ALREADY_EXISTS)
            {
                continue;
            }

            if (outLastError != nullptr)
            {
                *outLastError = createError;
            }
            return false;
        }

        DWORD written = 0;
        const BYTE marker = 0x5A;
        const BOOL writeOk = ::WriteFile(handle, &marker, 1, &written, nullptr);
        const DWORD writeLastError = writeOk ? ERROR_SUCCESS : ::GetLastError();
        ::CloseHandle(handle);
        ::DeleteFileW(probePath.c_str());

        if (!writeOk || written != 1)
        {
            if (outLastError != nullptr)
            {
                *outLastError = writeLastError != ERROR_SUCCESS ? writeLastError : ERROR_WRITE_FAULT;
            }
            return false;
        }
        return true;
    }

    if (outLastError != nullptr)
    {
        *outLastError = ERROR_ALREADY_EXISTS;
    }
    return false;
}

inline bool DefaultWriteProbe(const std::wstring& directoryPath, DWORD* outLastError)
{
    constexpr int kProbeCreateMaxAttempts = 8;
    return DefaultWriteProbeWithSeed(
        directoryPath,
        outLastError,
        ::GetCurrentProcessId(),
        ::GetTickCount64(),
        kProbeCreateMaxAttempts);
}

inline Result ValidateSavePath(
    const std::wstring& rawPath,
    WriteProbe writeProbe = DefaultWriteProbe)
{
    const std::wstring trimmed = Trim(rawPath);
    if (trimmed.empty())
    {
        return MakeError(kSavePathInvalidOutputPath, L"SavePath cannot be empty.");
    }

    if (!IsAbsolutePath(trimmed))
    {
        return MakeError(kSavePathInvalidOutputPath, L"SavePath must be an absolute path.");
    }

    if (ContainsControlCharacter(trimmed))
    {
        return MakeError(kSavePathInvalidOutputPath, L"SavePath contains control characters.");
    }

    if (ContainsPathTraversalSegment(trimmed))
    {
        return MakeError(kSavePathInvalidOutputPath, L"SavePath must not include '..' traversal.");
    }

    if (ContainsReservedNameSegment(trimmed))
    {
        return MakeError(kSavePathInvalidOutputPath, L"SavePath contains a Windows reserved name.");
    }

    wchar_t fullPath[MAX_PATH] = { 0 };
    const DWORD fullPathLength = ::GetFullPathNameW(trimmed.c_str(), MAX_PATH, fullPath, nullptr);
    if (fullPathLength == 0 || fullPathLength >= MAX_PATH)
    {
        return MakeError(kSavePathInvalidOutputPath, L"SavePath cannot be normalized.");
    }

    const std::wstring normalizedPath = StripTrailingSeparator(fullPath);
    const DWORD attributes = ::GetFileAttributesW(normalizedPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        return MakeError(
            kSavePathOutputPathNotFound,
            L"SavePath directory does not exist: " + normalizedPath);
    }

    DWORD writeProbeError = ERROR_SUCCESS;
    if (writeProbe != nullptr && !writeProbe(normalizedPath, &writeProbeError))
    {
        std::wstring message = L"SavePath is not writable: " + normalizedPath;
        if (writeProbeError != ERROR_SUCCESS)
        {
            message += L" (GetLastError=" + std::to_wstring(writeProbeError) + L")";
        }
        return MakeError(kSavePathOutputPathNotWritable, message);
    }

    Result success;
    success.code = kSavePathValidationOk;
    success.normalizedPath = normalizedPath;
    return success;
}

inline std::wstring BuildWarningMessage(const Result& result)
{
    if (result.code == kSavePathValidationOk)
    {
        return L"";
    }
    return result.message;
}

inline bool ShouldValidateForUiTrigger(const UiValidationTrigger trigger)
{
    switch (trigger)
    {
    case UiValidationTrigger::kTextChanged:
        return false;
    case UiValidationTrigger::kStartup:
    case UiValidationTrigger::kFolderDialogConfirmed:
    case UiValidationTrigger::kFolderDialogCancel:
    case UiValidationTrigger::kSetParameters:
        return true;
    default:
        return true;
    }
}

inline bool ShouldValidateStartupAfterConfigImport(const bool importSucceeded)
{
    return importSucceeded && ShouldValidateForUiTrigger(UiValidationTrigger::kStartup);
}

inline bool ShouldShowDialogForUiTrigger(const UiValidationTrigger trigger)
{
    switch (trigger)
    {
    case UiValidationTrigger::kStartup:
    case UiValidationTrigger::kFolderDialogConfirmed:
    case UiValidationTrigger::kFolderDialogCancel:
    case UiValidationTrigger::kSetParameters:
        return true;
    case UiValidationTrigger::kTextChanged:
    default:
        return false;
    }
}

} // namespace SavePathValidation
