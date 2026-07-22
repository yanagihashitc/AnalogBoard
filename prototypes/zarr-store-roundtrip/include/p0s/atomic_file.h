#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace p0s {

struct AtomicPublicationObservation {
  std::filesystem::path destination_path;
  std::filesystem::path temporary_path;
};

using AtomicPublicationObserver =
    std::function<void(const AtomicPublicationObservation&)>;
// Test seam: an empty operation uses FlushFileBuffers on the private handle.
using AtomicFlushOperation = std::function<bool()>;

void AtomicWriteFile(const std::filesystem::path& path,
                     const std::vector<std::uint8_t>& bytes,
                     const AtomicPublicationObserver& observer = {},
                     const AtomicFlushOperation& flush_operation = {});
void AtomicWriteText(const std::filesystem::path& path,
                     const std::string& text,
                     const AtomicPublicationObserver& observer = {},
                     const AtomicFlushOperation& flush_operation = {});

}  // namespace p0s
