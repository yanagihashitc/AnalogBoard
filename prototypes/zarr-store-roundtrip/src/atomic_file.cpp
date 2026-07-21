#include "p0s/atomic_file.h"

#include "p0s/error.h"

#include <windows.h>

#include <algorithm>
#include <limits>
#include <string>
#include <system_error>

namespace p0s {
namespace {

std::string ErrorMessage(const char* action, DWORD error) {
  return std::string(action) + " failed with Windows error " +
         std::to_string(error);
}

class FileHandle final {
 public:
  explicit FileHandle(HANDLE handle) : handle_(handle) {}
  ~FileHandle() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
  }

  FileHandle(const FileHandle&) = delete;
  FileHandle& operator=(const FileHandle&) = delete;

  [[nodiscard]] HANDLE get() const noexcept { return handle_; }

  void CloseChecked() {
    if (handle_ == INVALID_HANDLE_VALUE) {
      return;
    }
    const HANDLE handle = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    if (!CloseHandle(handle)) {
      const DWORD error = GetLastError();
      throw Error(ErrorCode::kFilesystem,
                  ErrorMessage("CloseHandle for atomic file", error));
    }
  }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

bool FlushTemporaryFile(HANDLE handle,
                        const AtomicFlushOperation& flush_operation) {
  return flush_operation ? flush_operation() : FlushFileBuffers(handle) != 0;
}

}  // namespace

void AtomicWriteFile(const std::filesystem::path& path,
                     const std::vector<std::uint8_t>& bytes,
                     const AtomicPublicationObserver& observer,
                     const AtomicFlushOperation& flush_operation) {
  std::error_code filesystem_error;
  const bool parent_is_directory =
      std::filesystem::is_directory(path.parent_path(), filesystem_error);
  if (filesystem_error || path.empty() || !path.has_filename() ||
      !parent_is_directory) {
    throw Error(ErrorCode::kFilesystem,
                "Atomic file parent directory is absent");
  }

  auto temporary = path;
  temporary += L".p0s.tmp";
  const HANDLE raw_handle = CreateFileW(
      temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (raw_handle == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    throw Error(ErrorCode::kFilesystem,
                ErrorMessage("CreateFileW for atomic temporary", error));
  }

  try {
    FileHandle handle(raw_handle);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      const std::size_t remaining = bytes.size() - offset;
      const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
          remaining, std::numeric_limits<DWORD>::max()));
      DWORD written = 0;
      if (!WriteFile(handle.get(), bytes.data() + offset, request, &written,
                     nullptr) ||
          written == 0 || written > request) {
        const DWORD error = GetLastError();
        throw Error(ErrorCode::kFilesystem,
                    ErrorMessage("WriteFile for atomic temporary", error));
      }
      offset += written;
    }
    if (!FlushTemporaryFile(handle.get(), flush_operation)) {
      const DWORD error = GetLastError();
      throw Error(ErrorCode::kFilesystem,
                  ErrorMessage("FlushFileBuffers for atomic temporary", error));
    }
    handle.CloseChecked();
    if (observer) {
      observer(AtomicPublicationObservation{path, temporary});
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      const DWORD error = GetLastError();
      throw Error(ErrorCode::kFilesystem,
                  ErrorMessage("MoveFileExW for atomic publication", error));
    }
  } catch (...) {
    DeleteFileW(temporary.c_str());
    throw;
  }
}

void AtomicWriteText(const std::filesystem::path& path,
                     const std::string& text,
                     const AtomicPublicationObserver& observer,
                     const AtomicFlushOperation& flush_operation) {
  AtomicWriteFile(path,
                  std::vector<std::uint8_t>(text.begin(), text.end()), observer,
                  flush_operation);
}

}  // namespace p0s
