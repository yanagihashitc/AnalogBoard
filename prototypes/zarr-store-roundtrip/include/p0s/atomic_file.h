#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace p0s {

void AtomicWriteFile(const std::filesystem::path& path,
                     const std::vector<std::uint8_t>& bytes);
void AtomicWriteText(const std::filesystem::path& path,
                     const std::string& text);

}  // namespace p0s
