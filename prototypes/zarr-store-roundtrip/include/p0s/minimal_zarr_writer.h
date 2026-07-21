#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace p0s {

struct StoreGenerationOptions {
  std::filesystem::path output_root;
  std::filesystem::path accepted_kat_path;
  bool finalize = true;
};

struct PublicationEvent {
  std::string stage;
  std::size_t write_generation;
  std::array<std::size_t, 2> visible_rows;
  std::array<bool, 2> sealed;
  std::string status;
};

using PublicationObserver = std::function<void(
    const PublicationEvent&, const std::filesystem::path&)>;

void GenerateSyntheticStore(const StoreGenerationOptions& options,
                            const PublicationObserver& observer = {});

[[nodiscard]] std::uint64_t SyntheticFeatureBits(std::size_t global_event,
                                                 std::size_t feature_index);
[[nodiscard]] std::uint16_t SyntheticWaveformValue(
    std::string_view array_name,
    std::size_t global_event,
    std::size_t channel,
    std::size_t sample);

}  // namespace p0s
