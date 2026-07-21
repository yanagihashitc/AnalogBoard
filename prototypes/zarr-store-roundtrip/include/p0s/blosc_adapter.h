#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p0s {

inline constexpr int kBloscCompressionLevel = 5;
inline constexpr int kBloscShuffle = 1;
inline constexpr std::size_t kBloscBlockSize = 0;
inline constexpr int kBloscInternalThreads = 1;

[[nodiscard]] std::vector<std::uint8_t> CompressBlosc(
    const std::uint8_t* source,
    std::size_t source_size,
    std::size_t typesize);

[[nodiscard]] std::vector<std::uint8_t> DecompressBlosc(
    const std::vector<std::uint8_t>& frame,
    std::size_t expected_size);

}  // namespace p0s
