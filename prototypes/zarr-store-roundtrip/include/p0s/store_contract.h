#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace p0s {

inline constexpr std::string_view kSyntheticDatasetId = "tube_1";
inline constexpr std::string_view kAcceptedKatSha256 =
    "cd0ee69428b483ddff4a10a84d15732ed9a7aabd2b85c99adbb97168f8fe60aa";
inline constexpr std::size_t kSyntheticPartitionCount = 2;
inline constexpr std::size_t kSyntheticEventCount = 5;
inline constexpr std::array<std::size_t, 2> kSyntheticRearmBatchSizes{2, 3};
inline constexpr std::size_t kWaveformSamples = 2400;
inline constexpr std::uint16_t kAdcMinimum = 0;
inline constexpr std::uint16_t kAdcMaximum = 16383;
static_assert(kSyntheticPartitionCount == 2,
              "The sharding comparison fixture pins two partitions");
static_assert(kSyntheticRearmBatchSizes[0] + kSyntheticRearmBatchSizes[1] ==
                  kSyntheticEventCount,
              "The re-arm batches must cover the five-event fixture");

enum class ShardingMode : std::uint8_t {
  kRoundRobin,
  kAppendSequential,
};

struct SyntheticEventLocation {
  std::size_t partition;
  std::size_t row;
};

inline constexpr std::array<std::string_view, 24> kPulseFeatureColumns{
    "FSC_A", "FSC_H", "FSC_W", "SSC_A", "SSC_H", "SSC_W",
    "FL1_A", "FL1_H", "FL1_W", "FL2_A", "FL2_H", "FL2_W",
    "FL3_A", "FL3_H", "FL3_W", "FL4_A", "FL4_H", "FL4_W",
    "FL5_A", "FL5_H", "FL5_W", "FL6_A", "FL6_H", "FL6_W"};

inline constexpr std::array<std::string_view, 8> kFlChannelOrder{
    "FSC", "SSC", "FL1", "FL2", "FL3", "FL4", "FL5", "FL6"};

inline constexpr std::array<std::string_view, 5> kGmiChannelOrder{
    "fsGMI", "ssGMI", "flGMI", "dGMI", "bfGMI"};

struct MeasurementArrayContract {
  std::string_view name;
  std::string_view dtype;
  std::size_t rank;
  std::array<std::size_t, 3> chunks;
  std::array<std::size_t, 2> trailing_shape;
  std::size_t typesize;
  std::size_t decoded_chunk_bytes;
  bool floating_fill;
};

inline constexpr std::array<MeasurementArrayContract, 3>
    kMeasurementArrayContracts{{
        {"pulse_features", "<f8", 2, {10000, 24, 0}, {24, 0}, 8,
         1'920'000, true},
        {"gmi_waveform", "<u2", 3, {2000, 5, 2400}, {5, 2400}, 2,
         48'000'000, false},
        {"fl_waveform", "<u2", 3, {2000, 8, 2400}, {8, 2400}, 2,
         76'800'000, false},
    }};

}  // namespace p0s
