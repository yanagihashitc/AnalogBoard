#include "SavePathValidation.h"

#include <Windows.h>

#include <cwctype>
#include <sstream>

namespace save_path_validation
{
namespace
{
bool IsPathSeparator(const wchar_t ch)
{
    return (ch == L'\\') || (ch == L'/');
}

std::wstring TrimWideSpace(const std::wstring& text)
{
    size_t begin = 0U;
    size_t end = text.size();

    while ((begin < end) && (std::iswspace(static_cast<wint_t>(text[begin])) != 0))
    {
        ++begin;
    }

    while ((end > begin) && (std::iswspace(static_cast<wint_t>(text[end - 1U])) != 0))
    {
        --end;
    }

    return text.substr(begin, end - begin);
}

bool ContainsParentTraversalSegment(const std::wstring& path)
{
    size_t pos = 0U;
    while (pos < path.size())
    {
        while ((pos < path.size()) && IsPathSeparator(path[pos]))
        {
            ++pos;
        }

        const size_t begin = pos;
        while ((pos < path.size()) && !IsPathSeparator(path[pos]))
        {
            ++pos;
        }

        if ((pos > begin) && (path.compare(begin, pos - begin, L"..") == 0))
        {
            return true;
        }
    }

    return false;
}

std::wstring BuildProbePath(const std::wstring& directoryPath)
{
    std::wstringstream fileName;
    fileName << L".savepath_probe_" << GetCurrentProcessId() << L"_" << GetTickCount64() << L".tmp";

    if (directoryPath.empty())
    {
        return fileName.str();
    }

    if (IsPathSeparator(directoryPath.back()))
    {
        return directoryPath + fileName.str();
    }

    return directoryPath + L"\\" + fileName.str();
}

std::wstring BuildWin32ErrorDetail(const wchar_t* context, const DWORD errorCode)
{
    std::wstringstream ss;
    ss << context << L" (Win32Error=" << errorCode << L")";
    return ss.str();
}
} // namespace

bool DefaultWriteProbe(const std::wstring& directoryPath, std::wstring* outDetail)
{
    const std::wstring probePath = BuildProbePath(directoryPath);
    const HANDLE fileHandle = ::CreateFileW(
        probePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr);

    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        if (outDetail != nullptr)
        {
            *outDetail = BuildWin32ErrorDetail(L"CreateFileW failed", ::GetLastError());
        }
        return false;
    }

    const unsigned char probeByte = 0x00;
    DWORD writtenBytes = 0U;
    const BOOL writeOk = ::WriteFile(fileHandle, &probeByte, 1U, &writtenBytes, nullptr);
    const DWORD writeError = ::GetLastError();
    ::CloseHandle(fileHandle);

    if ((writeOk == FALSE) || (writtenBytes != 1U))
    {
        if (outDetail != nullptr)
        {
            *outDetail = BuildWin32ErrorDetail(L"WriteFile failed", writeError);
        }
        return false;
    }

    return true;
}

ValidationResult ValidateSavePath(const std::wstring& rawPath, const WriteProbeFn& writeProbe)
{
    const std::wstring trimmedPath = TrimWideSpace(rawPath);
    if (trimmedPath.empty())
    {
        return { ValidationCode::kEmptyPath, L"SavePath cannot be empty." };
    }

    if (ContainsParentTraversalSegment(trimmedPath))
    {
        return { ValidationCode::kParentTraversal, L"SavePath must not contain '..' traversal segments." };
    }

    const DWORD attributes = ::GetFileAttributesW(trimmedPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return { ValidationCode::kDirectoryNotFound, L"SavePath directory does not exist: " + trimmedPath };
    }

    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U)
    {
        return { ValidationCode::kNotDirectory, L"SavePath is not a directory: " + trimmedPath };
    }

    const WriteProbeFn& probe = writeProbe ? writeProbe : DefaultWriteProbe;
    std::wstring detail;
    if (!probe(trimmedPath, &detail))
    {
        std::wstring message = L"SavePath is not writable: " + trimmedPath;
        if (!detail.empty())
        {
            message += L" | " + detail;
        }
        return { ValidationCode::kNotWritable, message };
    }

    return { ValidationCode::kSuccess, L"" };
}
} // namespace save_path_validation

