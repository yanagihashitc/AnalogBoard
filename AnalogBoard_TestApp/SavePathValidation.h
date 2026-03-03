#pragma once

#include <functional>
#include <string>

namespace save_path_validation
{
enum class ValidationCode
{
    kSuccess = 0,
    kEmptyPath,
    kParentTraversal,
    kDirectoryNotFound,
    kNotDirectory,
    kNotWritable,
};

struct ValidationResult
{
    ValidationCode code = ValidationCode::kSuccess;
    std::wstring message;
};

using WriteProbeFn = std::function<bool(const std::wstring& directoryPath, std::wstring* outDetail)>;

ValidationResult ValidateSavePath(const std::wstring& rawPath, const WriteProbeFn& writeProbe = WriteProbeFn());
bool DefaultWriteProbe(const std::wstring& directoryPath, std::wstring* outDetail);
} // namespace save_path_validation
