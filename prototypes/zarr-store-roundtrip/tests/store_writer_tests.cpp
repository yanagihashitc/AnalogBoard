#include "p0s/aead_store.h"
#include "p0s/atomic_file.h"
#include "p0s/blosc_adapter.h"
#include "p0s/error.h"
#include "p0s/minimal_zarr_writer.h"
#include "p0s/store_contract.h"
#include "p0s/strict_json.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

int checks = 0;
int publication_checks = 0;

void Require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot read generated text file");
  }
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot read generated binary file");
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                   std::istreambuf_iterator<char>());
}

class TemporaryDirectory final {
 public:
  TemporaryDirectory() {
    root_ = std::filesystem::temp_directory_path() /
            ("analogboard_p0s_store_writer_" +
             std::to_string(GetCurrentProcessId()) + "_" +
             std::to_string(GetTickCount64()));
    if (!std::filesystem::create_directory(root_)) {
      throw std::runtime_error("cannot create test-owned temporary directory");
    }
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return root_;
  }

 private:
  std::filesystem::path root_;
};

std::filesystem::path DatasetPath(const std::filesystem::path& root) {
  return root / "datasets" / std::string(p0s::kSyntheticDatasetId);
}

std::filesystem::path ArrayPath(const std::filesystem::path& root,
                                std::string_view array_name,
                                std::size_t partition) {
  return DatasetPath(root) / std::string(array_name) /
         ("partition_" + std::to_string(partition) + ".zarr");
}

std::string ChunkKey(std::size_t rank, std::size_t chunk_index = 0) {
  return p0s::ChunkKeyForIndex(rank, chunk_index);
}

double BitsToDouble(std::uint64_t bits) {
  double value = 0.0;
  static_assert(sizeof(value) == sizeof(bits));
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void RequireTypedError(const std::function<void()>& action,
                       p0s::ErrorCode code,
                       const std::string& message) {
  try {
    action();
  } catch (const p0s::Error& error) {
    Require(error.code() == code, "operation returned an unexpected error code");
    Require(std::string(error.what()) == message,
            "operation returned an unstable error message");
    return;
  }
  throw std::runtime_error("operation did not fail loud");
}

p0s::Json ReadJson(const std::filesystem::path& path) {
  return p0s::ParseStrictJson(ReadText(path));
}

void CheckMetaState(const std::filesystem::path& root,
                    std::size_t generation,
                    const std::array<std::size_t, 2>& rows,
                    const std::array<bool, 2>& sealed,
                    const std::string& status) {
  const auto meta = ReadJson(DatasetPath(root) / "meta.json");
  Require(meta.at("write_generation") == generation,
          "meta generation overclaims or lags the publication event");
  Require(meta.at("events_per_partition") ==
              p0s::Json::array({rows[0], rows[1]}),
          "meta partition rows differ from the committed prefix");
  Require(meta.at("status") == status,
          "meta status differs from the publication event");
  Require(meta.at("n_events") == rows[0] + rows[1],
          "meta event count differs from the committed prefix");
  for (std::size_t feature = 0; feature < p0s::kPulseFeatureColumns.size();
       ++feature) {
    const auto& feature_meta = meta.at("features").at(feature);
    if (rows[0] + rows[1] == 0) {
      Require(feature_meta.at("range_min").is_null() &&
                  feature_meta.at("range_max").is_null(),
              "zero-row feature range must remain null");
      continue;
    }
    double expected_min =
        BitsToDouble(p0s::SyntheticFeatureBits(0, feature));
    double expected_max = expected_min;
    for (std::size_t event = 1; event < rows[0] + rows[1]; ++event) {
      const double value =
          BitsToDouble(p0s::SyntheticFeatureBits(event, feature));
      expected_min = (std::min)(expected_min, value);
      expected_max = (std::max)(expected_max, value);
    }
    Require(feature_meta.at("range_min") == expected_min &&
                feature_meta.at("range_max") == expected_max,
            "feature range includes uncommitted or omits committed events");
  }
  for (const auto& contract : p0s::kMeasurementArrayContracts) {
    const auto& entries =
        meta.at("partition_manifests").at(std::string(contract.name));
    for (std::size_t partition = 0; partition < rows.size(); ++partition) {
      Require(entries.at(partition).at("row_count") == rows[partition],
              "manifest row count differs from committed metadata");
      Require(entries.at(partition).at("sealed") == sealed[partition],
              "manifest seal differs from committed metadata");
      if (rows[partition] > 0) {
        Require(std::filesystem::is_regular_file(
                    ArrayPath(root, contract.name, partition) /
                    ChunkKey(contract.rank)),
                "manifest must not claim a row without its chunk");
      }
    }
  }
}

void CheckPublicationEvent(const p0s::PublicationEvent& event,
                           const std::filesystem::path& root) {
  ++publication_checks;
  CheckMetaState(root, event.write_generation, event.visible_rows, event.sealed,
                 event.status);
  if (event.stage == "initial_meta_published") {
    for (const auto& contract : p0s::kMeasurementArrayContracts) {
      for (std::size_t partition = 0;
           partition < p0s::kSyntheticPartitionCount; ++partition) {
        Require(!std::filesystem::exists(
                    ArrayPath(root, contract.name, partition) /
                    ChunkKey(contract.rank)),
                "zero-row partition must not publish a chunk file");
        const auto zarray =
            ReadJson(ArrayPath(root, contract.name, partition) / ".zarray");
        Require(zarray.at("shape").at(0) == 0,
                "zero-row partition must publish a zero-row shape");
      }
    }
  }
}

void CheckExactZarray(const std::filesystem::path& root,
                      const p0s::MeasurementArrayContract& contract,
                      std::size_t partition,
                      std::size_t expected_rows) {
  const auto zarray = ReadJson(ArrayPath(root, contract.name, partition) /
                               ".zarray");
  p0s::RequireExactObjectFields(
      zarray, {"chunks", "compressor", "dimension_separator", "dtype",
               "fill_value", "filters", "order", "shape", "zarr_format"});
  Require(zarray.at("dtype") == std::string(contract.dtype),
          "Zarr dtype changed");
  Require(zarray.at("dimension_separator") == ".",
          "Zarr dimension separator changed");
  Require(zarray.at("order") == "C", "Zarr order changed");
  Require(zarray.at("zarr_format") == 2, "Zarr format changed");
  Require(zarray.at("filters").is_null(), "Zarr filters must remain null");
  if (contract.floating_fill) {
    Require(zarray.at("fill_value").is_number_float() &&
                zarray.at("fill_value").get<double>() == 0.0,
            "floating Zarr fill value changed type or value");
  } else {
    Require(zarray.at("fill_value").is_number_integer() &&
                zarray.at("fill_value").get<std::int64_t>() == 0,
            "integer Zarr fill value changed type or value");
  }
  Require(zarray.at("shape").at(0) == expected_rows,
          "Zarr visible shape changed");
  Require(zarray.at("chunks").size() == contract.rank,
          "Zarr chunk rank changed");
  Require(zarray.at("shape").size() == contract.rank,
          "Zarr shape rank changed");
  for (std::size_t dimension = 0; dimension < contract.rank; ++dimension) {
    Require(zarray.at("chunks").at(dimension) == contract.chunks[dimension],
            "Zarr chunk dimension changed");
    if (dimension > 0) {
      Require(zarray.at("shape").at(dimension) ==
                  contract.trailing_shape[dimension - 1],
              "Zarr trailing shape changed");
    }
  }
  const auto& compressor = zarray.at("compressor");
  p0s::RequireExactObjectFields(
      compressor, {"id", "cname", "clevel", "shuffle", "blocksize"});
  Require(compressor == p0s::Json{{"blocksize", 0},
                                  {"clevel", 5},
                                  {"cname", "lz4"},
                                  {"id", "blosc"},
                                  {"shuffle", 1}},
          "Zarr inner compressor profile changed");
}

void CheckFinalStore(const std::filesystem::path& root,
                     p0s::ShardingMode mode) {
  const auto marker = ReadJson(root / ".gcsa_store.json");
  Require(marker == p0s::Json{{"capabilities",
                               p0s::Json::array(
                                   {"d21_visibility", "encrypted_chunks"})},
                              {"producer", "analogboard"},
                              {"store_format", 1}},
          "strict store marker changed");

  const std::array<std::size_t, 2> expected_rows =
      mode == p0s::ShardingMode::kRoundRobin
          ? std::array<std::size_t, 2>{3, 2}
          : std::array<std::size_t, 2>{2, 3};
  const std::size_t expected_generation =
      mode == p0s::ShardingMode::kRoundRobin ? 3 : 2;
  CheckMetaState(root, expected_generation, expected_rows, {true, true},
                 "finalized");
  const auto meta = ReadJson(DatasetPath(root) / "meta.json");
  p0s::RequireExactObjectFields(
      meta,
      {"dataset_id", "created_at", "source_path", "n_events",
       "n_partitions", "events_per_partition", "channels", "features",
       "config", "cfg_params", "versions", "provenance", "extra", "status",
       "finalized_at", "write_generation", "meta_generation",
       "feature_schema_version", "partition_manifests", "display_name",
       "comment", "tags"});
  Require(meta.size() == 22, "strict meta field set changed");
  Require(!meta.contains("sharding_mode"),
          "sharding mode must not add a strict top-level meta field");
  Require(meta.at("extra").at("partition_sharding") ==
              std::string(p0s::ShardingModeName(mode)),
          "existing additive meta extra does not identify the candidate");
  Require(meta.at("dataset_id") == std::string(p0s::kSyntheticDatasetId),
          "dataset id changed");
  Require(meta.at("n_events") == p0s::kSyntheticEventCount &&
              meta.at("n_partitions") == 2,
          "synthetic event or partition count changed");
  Require(meta.at("feature_schema_version") == 1,
          "feature schema version changed");
  Require(meta.at("features").size() == p0s::kPulseFeatureColumns.size(),
          "feature count changed");
  Require(meta.at("channels").size() ==
              p0s::kFlChannelOrder.size() + p0s::kGmiChannelOrder.size(),
          "channel count changed");
  for (std::size_t index = 0; index < p0s::kPulseFeatureColumns.size();
       ++index) {
    Require(meta.at("features").at(index).at("name") ==
                std::string(p0s::kPulseFeatureColumns[index]),
            "feature order changed");
  }
  for (const auto& channel : meta.at("channels")) {
    Require(channel.at("range_min") == p0s::kAdcMinimum &&
                channel.at("range_max") == p0s::kAdcMaximum,
            "channel metadata does not declare the 14-bit ADC range");
  }

  std::set<std::pair<std::uint8_t,
                     std::array<std::uint8_t, p0s::kAeadNonceSize>>>
      key_nonces;
  for (const auto& contract : p0s::kMeasurementArrayContracts) {
    for (std::size_t partition = 0;
         partition < p0s::kSyntheticPartitionCount; ++partition) {
      CheckExactZarray(root, contract, partition, expected_rows[partition]);
      const auto wire = ReadBytes(ArrayPath(root, contract.name, partition) /
                                  ChunkKey(contract.rank));
      Require(wire.size() >= p0s::kAeadHeaderSize + p0s::kAeadTagSize,
              "encrypted chunk is truncated");
      Require(wire[0] == 'G' && wire[1] == 'C' && wire[2] == 'S' &&
                  wire[3] == 'A' && wire[4] == p0s::kAeadFormatVersion,
              "encrypted chunk wire header changed");
      std::array<std::uint8_t, p0s::kAeadNonceSize> nonce{};
      std::copy_n(wire.begin() + 6, nonce.size(), nonce.begin());
      Require(key_nonces.emplace(wire[5], nonce).second,
              "encrypted store reused a nonce for one key");
    }
  }
  Require(key_nonces.size() == 6,
          "encrypted store must contain six unique chunk nonces");

  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    Require(entry.path().extension() != ".tmp" &&
                entry.path().filename().wstring().find(L".p0s.tmp") ==
                    std::wstring::npos,
            "atomic publication left a temporary file");
  }
}

void CheckDeterministicJson(const std::filesystem::path& first,
                            const std::filesystem::path& second) {
  std::vector<std::filesystem::path> relative_paths{
      ".gcsa_store.json",
      std::filesystem::path("datasets") /
          std::string(p0s::kSyntheticDatasetId) / "meta.json"};
  for (const auto& contract : p0s::kMeasurementArrayContracts) {
    for (std::size_t partition = 0;
         partition < p0s::kSyntheticPartitionCount; ++partition) {
      relative_paths.push_back(
          std::filesystem::path("datasets") /
          std::string(p0s::kSyntheticDatasetId) /
          std::string(contract.name) /
          ("partition_" + std::to_string(partition) + ".zarr") / ".zarray");
    }
  }
  for (const auto& relative : relative_paths) {
    Require(ReadText(first / relative) == ReadText(second / relative),
            "deterministic JSON differs across two writer runs");
  }
}

std::uint8_t HexNibble(char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<std::uint8_t>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<std::uint8_t>(value - 'a' + 10);
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<std::uint8_t>(value - 'A' + 10);
  }
  throw std::runtime_error("accepted KAT contains invalid hexadecimal data");
}

p0s::TestKeyProvider LoadTestKeys(const std::filesystem::path& kat_path) {
  const auto kat = ReadJson(kat_path);
  const std::string key_hex = kat.at("key_hex").get<std::string>();
  Require(key_hex.size() == p0s::kAes256KeySize * 2,
          "accepted KAT key is not AES-256");
  std::array<std::uint8_t, p0s::kAes256KeySize> key{};
  for (std::size_t index = 0; index < key.size(); ++index) {
    key[index] = static_cast<std::uint8_t>(
        (HexNibble(key_hex[index * 2]) << 4) |
        HexNibble(key_hex[index * 2 + 1]));
  }
  p0s::TestKeyProvider keys;
  keys.Add(kat.at("key_id").get<std::uint8_t>(), key);
  return keys;
}

std::vector<std::uint8_t> DecodeChunk(
    const std::filesystem::path& root,
    const p0s::MeasurementArrayContract& contract,
    std::size_t partition,
    const p0s::TestKeyProvider& keys,
    std::size_t chunk_index = 0) {
  const std::string chunk_key = ChunkKey(contract.rank, chunk_index);
  const p0s::AeadChunkContext context(
      std::string(p0s::kSyntheticDatasetId),
      std::string(contract.name) + "/partition_" +
          std::to_string(partition) + ".zarr",
      chunk_key, contract.rank);
  const auto frame = p0s::DecryptAead(
      ReadBytes(ArrayPath(root, contract.name, partition) / chunk_key), context,
      keys);
  return p0s::DecompressBlosc(frame, contract.decoded_chunk_bytes);
}

void CheckDeterministicDecodedChunks(const std::filesystem::path& first,
                                     const std::filesystem::path& second,
                                     const std::filesystem::path& kat) {
  // Given: Two stores generated independently with the same candidate mode.
  const auto keys = LoadTestKeys(kat);

  // When: Every final authenticated chunk is decrypted and decompressed.
  for (const auto& contract : p0s::kMeasurementArrayContracts) {
    for (std::size_t partition = 0;
         partition < p0s::kSyntheticPartitionCount; ++partition) {
      const auto first_decoded = DecodeChunk(first, contract, partition, keys);
      const auto second_decoded =
          DecodeChunk(second, contract, partition, keys);

      // Then: Nonce differences do not alter any decoded logical bytes.
      Require(first_decoded == second_decoded,
              "decoded logical chunk differs across deterministic runs");
    }
  }
}

void CheckFiveEventMappingAndValidation() {
  // Given: The discriminating five-event fixture and both candidate modes.
  const std::array<p0s::SyntheticEventLocation, p0s::kSyntheticEventCount>
      round_robin_expected{{{0, 0}, {1, 0}, {0, 1}, {1, 1}, {0, 2}}};
  const std::array<p0s::SyntheticEventLocation, p0s::kSyntheticEventCount>
      sequential_expected{{{0, 0}, {0, 1}, {1, 0}, {1, 1}, {1, 2}}};
  const std::array<std::array<p0s::SyntheticEventLocation,
                             p0s::kSyntheticEventCount>,
                   2>
      expected{round_robin_expected, sequential_expected};
  const std::array<p0s::ShardingMode, 2> modes{
      p0s::ShardingMode::kRoundRobin,
      p0s::ShardingMode::kAppendSequential};

  // When: Every global event is mapped through the public shared planner seam.
  for (std::size_t mode = 0; mode < modes.size(); ++mode) {
    for (std::size_t event = 0; event < p0s::kSyntheticEventCount; ++event) {
      const auto location = p0s::MapSyntheticEvent(modes[mode], event);

      // Then: Partition and local row match the candidate's exact contract.
      Require(location.partition == expected[mode][event].partition &&
                  location.row == expected[mode][event].row,
              "five-event sharding mapping changed");
    }
  }

  // Then: The upper fixture boundary and an invalid enum fail with stable types.
  RequireTypedError(
      [] {
        static_cast<void>(p0s::MapSyntheticEvent(
            p0s::ShardingMode::kRoundRobin, p0s::kSyntheticEventCount));
      },
      p0s::ErrorCode::kInvalidArgument,
      "Synthetic global event is outside the five-event fixture");
  RequireTypedError(
      [] {
        static_cast<void>(p0s::MapSyntheticEvent(
            static_cast<p0s::ShardingMode>(255), 0));
      },
      p0s::ErrorCode::kInvalidArgument, "Sharding mode is invalid");
}

void CheckChunkPlanningBoundaries() {
  // Given: Every production row chunk size and 0, 1, C-1, C, C+1 rows.
  for (const auto& contract : p0s::kMeasurementArrayContracts) {
    const std::size_t chunk_rows = contract.chunks[0];
    const std::array<std::size_t, 5> visible_rows{
        0, 1, chunk_rows - 1, chunk_rows, chunk_rows + 1};
    const std::array<std::size_t, 5> expected_chunks{0, 1, 1, 1, 2};

    // When: The pure chunk extent planner sees each boundary.
    for (std::size_t index = 0; index < visible_rows.size(); ++index) {
      const auto extents =
          p0s::PlanChunkExtents(visible_rows[index], chunk_rows);

      // Then: Full and tail extents cover the visible prefix exactly once.
      Require(extents.size() == expected_chunks[index],
              "chunk planner returned an unexpected chunk count");
      std::size_t covered = 0;
      for (std::size_t chunk = 0; chunk < extents.size(); ++chunk) {
        Require(extents[chunk].chunk_index == chunk &&
                    extents[chunk].row_begin == covered &&
                    extents[chunk].row_count > 0 &&
                    extents[chunk].row_count <= chunk_rows,
                "chunk planner returned a malformed extent");
        covered += extents[chunk].row_count;
      }
      Require(covered == visible_rows[index],
              "chunk planner did not cover the visible prefix");
    }
  }

  // Then: A zero chunk size fails instead of dividing or looping indefinitely.
  RequireTypedError(
      [] { static_cast<void>(p0s::PlanChunkExtents(1, 0)); },
      p0s::ErrorCode::kInvalidArgument,
      "Chunk row count must be greater than zero");
  RequireTypedError(
      [] {
        static_cast<void>(p0s::PlanChunkExtents(
            (std::numeric_limits<std::size_t>::max)(), 1));
      },
      p0s::ErrorCode::kSizeOverflow,
      "Chunk plan exceeds addressable extent count");

  // Then: Chunk index is encoded in the leading Zarr coordinate component.
  Require(p0s::ChunkKeyForIndex(2, 0) == "0.0" &&
              p0s::ChunkKeyForIndex(2, 1) == "1.0" &&
              p0s::ChunkKeyForIndex(3, 0) == "0.0.0" &&
              p0s::ChunkKeyForIndex(3, 1) == "1.0.0",
          "chunk key does not encode its row-chunk index");
}

void CheckPartitionPlannerUsesFiveEventMapping() {
  // Given: All five events are committed in each candidate mode.
  const std::array<std::vector<std::size_t>, 2> round_robin_expected{
      std::vector<std::size_t>{0, 2, 4}, std::vector<std::size_t>{1, 3}};
  const std::array<std::vector<std::size_t>, 2> sequential_expected{
      std::vector<std::size_t>{0, 1}, std::vector<std::size_t>{2, 3, 4}};
  const std::array<std::array<std::vector<std::size_t>, 2>, 2> expected{
      round_robin_expected, sequential_expected};
  const std::array<p0s::ShardingMode, 2> modes{
      p0s::ShardingMode::kRoundRobin,
      p0s::ShardingMode::kAppendSequential};

  // When: The same partition planner used by the writer plans each partition.
  for (std::size_t mode = 0; mode < modes.size(); ++mode) {
    for (std::size_t partition = 0;
         partition < p0s::kSyntheticPartitionCount; ++partition) {
      const auto chunks = p0s::PlanPartitionChunks(
          modes[mode], partition, p0s::kSyntheticEventCount,
          p0s::kMeasurementArrayContracts.front().chunks[0]);

      // Then: Its ordered global rows are exactly the candidate mapping.
      Require(chunks.size() == 1,
              "five-event fixture must fit in one feature chunk");
      Require(chunks.front().global_events == expected[mode][partition],
              "partition planner diverged from global event mapping");
      Require(chunks.front().extent.row_count ==
                  expected[mode][partition].size(),
              "partition planner row count changed");
    }
  }
}

void CheckSyntheticValuesAreFiveEventAndFourteenBit() {
  // Given: Every waveform coordinate in the five-event fixture.
  std::uint16_t maximum = 0;
  for (const auto& contract : p0s::kMeasurementArrayContracts) {
    if (contract.name == "pulse_features") {
      continue;
    }
    const std::size_t channel_count =
        contract.name == "gmi_waveform" ? p0s::kGmiChannelOrder.size()
                                        : p0s::kFlChannelOrder.size();

    // When: Synthetic raw ADC values are evaluated across both waveform arrays.
    for (std::size_t event = 0; event < p0s::kSyntheticEventCount; ++event) {
      for (std::size_t channel = 0; channel < channel_count; ++channel) {
        for (std::size_t sample = 0; sample < p0s::kWaveformSamples; ++sample) {
          maximum = (std::max)(
              maximum, p0s::SyntheticWaveformValue(
                           contract.name, event, channel, sample));
        }
      }
    }
  }

  // Then: The generated range reaches but never wraps beyond the 14-bit maximum.
  Require(maximum == p0s::kAdcMaximum,
          "synthetic waveform does not exercise the 14-bit maximum");
  RequireTypedError(
      [] {
        static_cast<void>(p0s::SyntheticWaveformValue(
            "gmi_waveform", p0s::kSyntheticEventCount, 0, 0));
      },
      p0s::ErrorCode::kInvalidArgument,
      "Synthetic waveform coordinate is outside the fixture");
  RequireTypedError(
      [] {
        static_cast<void>(p0s::SyntheticFeatureBits(
            p0s::kSyntheticEventCount, 0));
      },
      p0s::ErrorCode::kInvalidArgument,
      "Synthetic feature coordinate is outside the fixture");
}

void RequireCliUsageFailure(const std::vector<std::string>& arguments,
                            const std::filesystem::path& output_path) {
  std::vector<const char*> argv;
  argv.reserve(arguments.size());
  for (const auto& argument : arguments) {
    argv.push_back(argument.c_str());
  }
  std::ostringstream output;
  std::ostringstream errors;
  const int exit_code = p0s::RunStoreGeneratorCli(
      static_cast<int>(argv.size()), argv.data(), output, errors);
  Require(exit_code == 2, "generator parser failure did not return exit 2");
  Require(output.str().empty(),
          "generator parser failure wrote a success message");
  Require(!errors.str().empty(),
          "generator parser failure did not explain the failure");
  Require(!std::filesystem::exists(output_path),
          "generator parser failure created an output store");
}

void CheckStoreGeneratorCliParser(const std::filesystem::path& root) {
  // Given: Existing and new supported command-line forms.
  const char* default_args[]{"generator", "accepted.json", "store"};
  const char* round_robin_args[]{"generator", "accepted.json", "store",
                                 "--sharding", "round-robin"};
  const char* open_args[]{"generator", "accepted.json", "store", "--open"};
  const char* open_sequential_args[]{"generator", "accepted.json", "store",
                                     "--open", "--sharding",
                                     "append-sequential"};
  const char* sequential_open_args[]{"generator", "accepted.json", "store",
                                     "--sharding", "append-sequential",
                                     "--open"};

  // When: The shared parser processes default, explicit, and --open forms.
  const auto defaults = p0s::ParseStoreGeneratorArguments(3, default_args);
  const auto round_robin =
      p0s::ParseStoreGeneratorArguments(5, round_robin_args);
  const auto open = p0s::ParseStoreGeneratorArguments(4, open_args);
  const auto open_sequential =
      p0s::ParseStoreGeneratorArguments(6, open_sequential_args);
  const auto sequential_open =
      p0s::ParseStoreGeneratorArguments(6, sequential_open_args);

  // Then: Existing --open behavior and exact candidate selection are retained.
  Require(defaults.finalize &&
              defaults.sharding_mode == p0s::ShardingMode::kRoundRobin,
          "generator default mode changed");
  Require(round_robin.finalize &&
              round_robin.sharding_mode == p0s::ShardingMode::kRoundRobin,
          "generator explicit round-robin mode changed");
  Require(!open.finalize &&
              open.sharding_mode == p0s::ShardingMode::kRoundRobin,
          "generator legacy --open form changed");
  Require(!open_sequential.finalize &&
              open_sequential.sharding_mode ==
                  p0s::ShardingMode::kAppendSequential,
          "generator --open append-sequential mode changed");
  Require(!sequential_open.finalize &&
              sequential_open.sharding_mode ==
                  p0s::ShardingMode::kAppendSequential,
          "generator flag order changed candidate selection");
  Require(defaults.accepted_kat_path == "accepted.json" &&
              defaults.output_root == "store",
          "generator positional arguments changed");

  const char* unknown[]{"generator", "kat", "store", "--unknown"};
  const char* duplicate_open[]{"generator", "kat", "store", "--open",
                               "--open"};
  const char* duplicate_mode[]{"generator", "kat", "store", "--sharding",
                               "round-robin", "--sharding",
                               "append-sequential"};
  const char* missing_mode[]{"generator", "kat", "store", "--sharding"};
  const char* missing_mode_before_flag[]{"generator", "kat", "store",
                                         "--sharding", "--open"};
  const char* invalid_mode[]{"generator", "kat", "store", "--sharding",
                             "striped"};
  const char* extra[]{"generator", "kat", "store", "extra"};

  // Then: Unknown, duplicate, incomplete, and invalid flags fail loud.
  RequireTypedError(
      [&] { static_cast<void>(p0s::ParseStoreGeneratorArguments(4, unknown)); },
      p0s::ErrorCode::kInvalidArgument,
      "store_generator received an unknown option: --unknown");
  RequireTypedError(
      [&] {
        static_cast<void>(
            p0s::ParseStoreGeneratorArguments(5, duplicate_open));
      },
      p0s::ErrorCode::kInvalidArgument,
      "store_generator option --open was specified more than once");
  RequireTypedError(
      [&] {
        static_cast<void>(
            p0s::ParseStoreGeneratorArguments(7, duplicate_mode));
      },
      p0s::ErrorCode::kInvalidArgument,
      "store_generator option --sharding was specified more than once");
  RequireTypedError(
      [&] {
        static_cast<void>(p0s::ParseStoreGeneratorArguments(4, missing_mode));
      },
      p0s::ErrorCode::kInvalidArgument,
      "store_generator option --sharding requires a mode");
  RequireTypedError(
      [&] {
        static_cast<void>(
            p0s::ParseStoreGeneratorArguments(5, missing_mode_before_flag));
      },
      p0s::ErrorCode::kInvalidArgument,
      "store_generator option --sharding requires a mode");
  RequireTypedError(
      [&] {
        static_cast<void>(p0s::ParseStoreGeneratorArguments(5, invalid_mode));
      },
      p0s::ErrorCode::kInvalidArgument,
      "store_generator sharding mode must be round-robin or append-sequential");

  // Given: The real CLI runner receives each invalid form before writer startup.
  const auto unknown_output = root / "unknown_cli_output";
  const auto duplicate_output = root / "duplicate_cli_output";
  const auto missing_output = root / "missing_cli_output";
  const auto invalid_output = root / "invalid_cli_output";
  const auto extra_output = root / "extra_cli_output";

  // When: Unknown, duplicate, missing, and extra options reach the entrypoint.
  RequireCliUsageFailure(
      {"generator", "kat", unknown_output.string(), "--unknown"},
      unknown_output);
  RequireCliUsageFailure(
      {"generator", "kat", duplicate_output.string(), "--open", "--open"},
      duplicate_output);
  RequireCliUsageFailure(
      {"generator", "kat", missing_output.string(), "--sharding"},
      missing_output);
  RequireCliUsageFailure({"generator", "kat", invalid_output.string(),
                          "--sharding", "striped"},
                         invalid_output);
  RequireCliUsageFailure(
      {"generator", "kat", extra_output.string(), "extra"}, extra_output);

  // Then: The parser also preserves the stable typed error for extra input.
  RequireTypedError(
      [&] { static_cast<void>(p0s::ParseStoreGeneratorArguments(4, extra)); },
      p0s::ErrorCode::kInvalidArgument,
      "store_generator received an unknown option: extra");
}

void RequireCliExecutionFailure(
    const std::vector<std::string>& arguments,
    const std::filesystem::path& output_path,
    const p0s::StoreGeneratorCliSeams& seams,
    const std::string& expected_error) {
  std::vector<const char*> argv;
  argv.reserve(arguments.size());
  for (const auto& argument : arguments) {
    argv.push_back(argument.c_str());
  }
  std::ostringstream output;
  std::ostringstream errors;
  const int exit_code = p0s::RunStoreGeneratorCli(
      static_cast<int>(argv.size()), argv.data(), output, errors, seams);
  Require(exit_code == 1, "generator execution failure did not return exit 1");
  Require(output.str().empty(),
          "generator execution failure wrote a success message");
  Require(errors.str() == expected_error,
          "generator execution failure returned an unexpected diagnostic");
  Require(!std::filesystem::exists(output_path),
          "generator execution failure created an output store");
}

void CheckStoreGeneratorCliExecutionFailures(
    const std::filesystem::path& root) {
  // Given: Thread launch fails before a worker can start.
  const auto launch_output = root / "launcher_system_error_output";
  bool worker_ran = false;
  p0s::StoreGeneratorCliSeams launch_failure;
  launch_failure.generate = [&worker_ran](const p0s::StoreGenerationOptions&) {
    worker_ran = true;
  };
  launch_failure.before_worker_launch = [] {
    throw std::system_error(
        std::make_error_code(std::errc::resource_unavailable_try_again),
        "injected thread construction failure");
  };

  // When: The CLI receives the standard exception from its launcher seam.
  RequireCliExecutionFailure(
      {"generator", "kat", launch_output.string()}, launch_output,
      launch_failure,
      "store generation failed: injected thread construction failure: " +
          std::make_error_code(std::errc::resource_unavailable_try_again)
              .message() +
          "\n");
  // Then: The fixed worker implementation was never entered.
  Require(!worker_ran, "before-launch failure still ran the worker task");

  // Given: Generation fails inside the worker with a standard exception.
  const auto standard_output = root / "worker_standard_error_output";
  p0s::StoreGeneratorCliSeams standard_failure;
  standard_failure.generate = [](const p0s::StoreGenerationOptions&) {
    throw std::runtime_error("injected worker failure");
  };

  // When: The default worker launcher joins the failing generation task.
  RequireCliExecutionFailure(
      {"generator", "kat", standard_output.string()}, standard_output,
      standard_failure, "store generation failed: injected worker failure\n");

  // Given: Generation fails inside the worker with a non-standard exception.
  const auto nonstandard_output = root / "worker_nonstandard_error_output";
  p0s::StoreGeneratorCliSeams nonstandard_failure;
  nonstandard_failure.generate = [](const p0s::StoreGenerationOptions&) {
    throw 7;
  };

  // When: The CLI normalizes the unknown worker failure after joining.
  RequireCliExecutionFailure(
      {"generator", "kat", nonstandard_output.string()}, nonstandard_output,
      nonstandard_failure,
      "store generation failed: non-standard exception\n");
}

using KeyNonce =
    std::pair<std::uint8_t, std::array<std::uint8_t, p0s::kAeadNonceSize>>;

KeyNonce ReadKeyNonce(const std::filesystem::path& chunk_path) {
  const auto wire = ReadBytes(chunk_path);
  Require(wire.size() >= p0s::kAeadHeaderSize + p0s::kAeadTagSize,
          "encrypted chunk is truncated while observing a rewrite");
  std::array<std::uint8_t, p0s::kAeadNonceSize> nonce{};
  std::copy_n(wire.begin() + 6, nonce.size(), nonce.begin());
  return {wire[5], nonce};
}

void CheckShapeFirstChunkCut(
    const p0s::AtomicPublicationObservation& observation,
    p0s::ShardingMode mode,
    std::size_t& publication_count) {
  // Given: A flushed encrypted chunk is about to replace its coordinate.
  const std::size_t publications_per_cycle =
      mode == p0s::ShardingMode::kRoundRobin ? 6 : 3;
  Require(publication_count < publications_per_cycle * 2,
          "writer emitted too many atomic chunk cuts");
  const std::size_t cycle = publication_count / publications_per_cycle;
  const std::string partition_name =
      observation.destination_path.parent_path().filename().string();
  const std::size_t partition =
      partition_name == "partition_0.zarr"
          ? 0
          : partition_name == "partition_1.zarr" ? 1
                                                  : p0s::kSyntheticPartitionCount;
  Require(partition < p0s::kSyntheticPartitionCount,
          "atomic chunk cut has an unexpected partition path");
  const std::array<std::array<std::size_t, 2>, 2> round_robin_rows{
      std::array<std::size_t, 2>{1, 1}, std::array<std::size_t, 2>{3, 2}};
  const std::array<std::array<std::size_t, 2>, 2> sequential_rows{
      std::array<std::size_t, 2>{2, 0}, std::array<std::size_t, 2>{2, 3}};
  const auto& expected_rows =
      mode == p0s::ShardingMode::kRoundRobin ? round_robin_rows
                                              : sequential_rows;

  // When: The crash-cut-visible .zarray is read before chunk rename.
  const auto zarray =
      ReadJson(observation.destination_path.parent_path() / ".zarray");
  const auto dataset_path = observation.destination_path.parent_path()
                                .parent_path()
                                .parent_path();
  const auto meta = ReadJson(dataset_path / "meta.json");

  // Then: Shape is ahead, authoritative meta is old, and flushed temp exists.
  Require(zarray.at("shape").at(0) == expected_rows[cycle][partition],
          "writer published a chunk before its next physical shape");
  Require(meta.at("n_events") == (cycle == 0 ? 0 : 2),
          "writer advanced manifest metadata before chunk publication");
  Require(std::filesystem::is_regular_file(observation.temporary_path),
          "chunk atomic cut does not expose its flushed temporary wire");
  ++publication_count;
}

struct ObservedWire {
  std::string coordinate;
  KeyNonce key_nonce;
};

void CaptureChunkPublication(
    const std::filesystem::path& root,
    p0s::ShardingMode mode,
    const p0s::PublicationEvent& event,
    std::size_t& cycle,
    std::vector<ObservedWire>& wires) {
  if (event.stage != "chunks_published") {
    return;
  }
  Require(cycle < p0s::kSyntheticRearmBatchSizes.size(),
          "writer emitted too many chunk publication cycles");
  const std::size_t committed_before =
      event.visible_rows[0] + event.visible_rows[1];
  const std::size_t committed_after =
      committed_before + p0s::kSyntheticRearmBatchSizes[cycle];
  for (std::size_t partition = 0;
       partition < p0s::kSyntheticPartitionCount; ++partition) {
    const auto previous = p0s::PlanPartitionChunks(
        mode, partition, committed_before,
        p0s::kMeasurementArrayContracts.front().chunks[0]);
    const auto next = p0s::PlanPartitionChunks(
        mode, partition, committed_after,
        p0s::kMeasurementArrayContracts.front().chunks[0]);
    const std::size_t previous_rows =
        previous.empty() ? 0 : previous.back().extent.row_begin +
                                   previous.back().extent.row_count;
    const std::size_t next_rows =
        next.empty()
            ? 0
            : next.back().extent.row_begin + next.back().extent.row_count;
    if (previous_rows == next_rows) {
      continue;
    }
    for (const auto& contract : p0s::kMeasurementArrayContracts) {
      const auto chunks = p0s::PlanPartitionChunks(
          mode, partition, committed_after, contract.chunks[0]);
      for (const auto& chunk : chunks) {
        const std::string chunk_key =
            ChunkKey(contract.rank, chunk.extent.chunk_index);
        wires.push_back(
            {std::string(contract.name) + "/partition_" +
                 std::to_string(partition) + ".zarr/" + chunk_key,
             ReadKeyNonce(ArrayPath(root, contract.name, partition) /
                          chunk_key)});
      }
    }
  }
  ++cycle;
}

void CheckNonceHistory(const std::vector<ObservedWire>& wires,
                       p0s::ShardingMode mode) {
  // Given: Every post-rename wire was captured before the next write cycle.
  const std::size_t expected_publications =
      mode == p0s::ShardingMode::kRoundRobin ? 12 : 6;
  const std::size_t expected_rewrites =
      mode == p0s::ShardingMode::kRoundRobin ? 6 : 0;

  // When: Key/nonce pairs and logical coordinates are counted across history.
  std::set<KeyNonce> key_nonces;
  std::map<std::string, std::size_t> coordinate_counts;
  for (const auto& wire : wires) {
    Require(key_nonces.emplace(wire.key_nonce).second,
            "writer reused a nonce across publication history");
    ++coordinate_counts[wire.coordinate];
  }
  std::size_t rewrites = 0;
  for (const auto& [coordinate, count] : coordinate_counts) {
    static_cast<void>(coordinate);
    rewrites += count - 1;
  }

  // Then: RR replaces six coordinates once; sequential never rewrites a seal.
  Require(wires.size() == expected_publications &&
              key_nonces.size() == expected_publications,
          "writer emitted an unexpected encrypted publication count");
  Require(coordinate_counts.size() == 6 && rewrites == expected_rewrites,
          "writer rewrite count differs from the candidate lifecycle");
}

std::uint64_t ReadLittleEndian(const std::vector<std::uint8_t>& input,
                               std::size_t offset,
                               std::size_t byte_count) {
  Require(offset <= input.size() && byte_count <= input.size() - offset,
          "decoded value is outside its chunk buffer");
  std::uint64_t value = 0;
  for (std::size_t byte = 0; byte < byte_count; ++byte) {
    value |= static_cast<std::uint64_t>(input[offset + byte]) << (byte * 8);
  }
  return value;
}

void CheckDecodedRowsAndTailFill(const std::filesystem::path& root,
                                 const std::filesystem::path& kat,
                                 p0s::ShardingMode mode) {
  // Given: The final cycle-2 chunks and the same public planner used by writer.
  const auto keys = LoadTestKeys(kat);

  // When: Each chunk is authenticated, decompressed, and walked by local row.
  for (const auto& contract : p0s::kMeasurementArrayContracts) {
    const std::size_t row_bytes =
        contract.decoded_chunk_bytes / contract.chunks[0];
    for (std::size_t partition = 0;
         partition < p0s::kSyntheticPartitionCount; ++partition) {
      const auto chunks = p0s::PlanPartitionChunks(
          mode, partition, p0s::kSyntheticEventCount, contract.chunks[0]);
      for (const auto& chunk : chunks) {
        const auto decoded = DecodeChunk(root, contract, partition, keys,
                                         chunk.extent.chunk_index);
        Require(decoded.size() == contract.decoded_chunk_bytes,
                "writer did not emit a full decoded Zarr chunk buffer");
        bool logical_values_match = true;
        for (std::size_t row = 0; row < chunk.global_events.size(); ++row) {
          const std::size_t global_event = chunk.global_events[row];
          if (contract.name == "pulse_features") {
            for (std::size_t feature = 0;
                 feature < p0s::kPulseFeatureColumns.size(); ++feature) {
              const std::size_t offset =
                  row * row_bytes + feature * sizeof(std::uint64_t);
              logical_values_match =
                  logical_values_match &&
                  ReadLittleEndian(decoded, offset, sizeof(std::uint64_t)) ==
                      p0s::SyntheticFeatureBits(global_event, feature);
            }
          } else {
            const std::size_t channel_count =
                contract.name == "gmi_waveform"
                    ? p0s::kGmiChannelOrder.size()
                    : p0s::kFlChannelOrder.size();
            for (std::size_t channel = 0; channel < channel_count; ++channel) {
              for (std::size_t sample = 0; sample < p0s::kWaveformSamples;
                   ++sample) {
                const std::size_t element =
                    channel * p0s::kWaveformSamples + sample;
                const std::size_t offset =
                    row * row_bytes + element * sizeof(std::uint16_t);
                logical_values_match =
                    logical_values_match &&
                    ReadLittleEndian(decoded, offset, sizeof(std::uint16_t)) ==
                        p0s::SyntheticWaveformValue(
                            contract.name, global_event, channel, sample);
              }
            }
          }
        }

        // Then: RR rows preserve [0,2,4]/[1,3], and unused tail bytes are zero.
        Require(logical_values_match,
                "decoded chunk rows diverge from the shared mapping planner");
        const std::size_t used_bytes =
            chunk.extent.row_count * row_bytes;
        Require(std::all_of(decoded.begin() + used_bytes, decoded.end(),
                            [](std::uint8_t byte) { return byte == 0; }),
                "partial chunk tail fill is not zero");
      }
    }
  }
}

void CheckLifecycle(const std::vector<p0s::PublicationEvent>& events,
                    p0s::ShardingMode mode) {
  // Given: Publication events captured from a completed five-event writer run.
  std::vector<p0s::PublicationEvent> manifests;
  std::vector<p0s::PublicationEvent> seals;
  std::vector<p0s::PublicationEvent> finalized;

  // When: Committed lifecycle stages are separated from pre-commit chunk cuts.
  for (const auto& event : events) {
    if (event.stage == "manifest_published") {
      manifests.push_back(event);
    } else if (event.stage == "sealed_meta_published") {
      seals.push_back(event);
    } else if (event.stage == "finalized_meta_published") {
      finalized.push_back(event);
    }
  }

  // Then: Both candidates commit exactly two re-arm batches before finalize.
  Require(events.front().stage == "initial_meta_published" &&
              events.front().visible_rows == std::array<std::size_t, 2>{0, 0} &&
              events.front().sealed == std::array<bool, 2>{false, false} &&
              events.front().status == "open",
          "writer initial lifecycle state changed");
  const std::size_t expected_event_count =
      mode == p0s::ShardingMode::kRoundRobin ? 7 : 6;
  Require(events.size() == expected_event_count && manifests.size() == 2 &&
              finalized.size() == 1,
          "writer emitted an unexpected lifecycle stage count");
  Require(events[1].stage == "chunks_published" &&
              events[2].stage == "manifest_published" &&
              events[3].stage == "chunks_published" &&
              events[4].stage == "manifest_published",
          "writer re-arm publication ordering changed");
  Require(manifests[0].write_generation == 1 &&
              manifests[1].write_generation == 2,
          "re-arm manifest generation is not monotonic");
  if (mode == p0s::ShardingMode::kRoundRobin) {
    Require(manifests[0].visible_rows ==
                    std::array<std::size_t, 2>{1, 1} &&
                manifests[0].sealed ==
                    std::array<bool, 2>{false, false} &&
                manifests[1].visible_rows ==
                    std::array<std::size_t, 2>{3, 2} &&
                manifests[1].sealed ==
                    std::array<bool, 2>{false, false},
            "round-robin re-arm lifecycle changed");
    Require(seals.size() == 1 &&
                seals[0].visible_rows ==
                    std::array<std::size_t, 2>{3, 2} &&
                seals[0].sealed == std::array<bool, 2>{true, true} &&
                seals[0].status == "open" &&
                seals[0].write_generation == 3 &&
                finalized[0].write_generation == 3,
            "round-robin partitions were not sealed after both cycles");
  } else {
    Require(manifests[0].visible_rows ==
                    std::array<std::size_t, 2>{2, 0} &&
                manifests[0].sealed == std::array<bool, 2>{true, false} &&
                manifests[1].visible_rows ==
                    std::array<std::size_t, 2>{2, 3} &&
                manifests[1].sealed == std::array<bool, 2>{true, true},
            "append-sequential re-arm lifecycle changed");
    Require(seals.empty(),
            "append-sequential emitted a redundant sealing transition");
    Require(finalized[0].write_generation == 2,
            "append-sequential finalize changed content generation");
  }
  Require(finalized[0].sealed == std::array<bool, 2>{true, true} &&
              finalized[0].status == "finalized",
          "writer finalized before every partition was sealed");
}

void CheckOpenTerminal(const std::vector<p0s::PublicationEvent>& events,
                       const std::filesystem::path& root,
                       p0s::ShardingMode mode) {
  // Given: A full five-event run requested with finalize=false.
  const std::array<std::size_t, 2> expected_rows =
      mode == p0s::ShardingMode::kRoundRobin
          ? std::array<std::size_t, 2>{3, 2}
          : std::array<std::size_t, 2>{2, 3};
  const std::size_t expected_generation =
      mode == p0s::ShardingMode::kRoundRobin ? 3 : 2;
  const std::size_t expected_event_count =
      mode == p0s::ShardingMode::kRoundRobin ? 6 : 5;

  // When: Its terminal metadata and publication history are inspected.
  const auto meta = ReadJson(DatasetPath(root) / "meta.json");
  const bool finalized_event =
      std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.stage == "finalized_meta_published";
      });

  // Then: Complete sealed content remains open without a finalized timestamp.
  Require(events.size() == expected_event_count && !finalized_event,
          "open writer emitted a finalized lifecycle event");
  Require(events.back().write_generation == expected_generation &&
              events.back().visible_rows == expected_rows &&
              events.back().sealed == std::array<bool, 2>{true, true} &&
              events.back().status == "open",
          "open writer terminal event changed");
  CheckMetaState(root, expected_generation, expected_rows, {true, true},
                 "open");
  Require(meta.at("finalized_at").is_null(),
          "open writer published a finalized timestamp");
}

void CheckExistingRootFails(const std::filesystem::path& root,
                            const std::filesystem::path& kat) {
  try {
    p0s::GenerateSyntheticStore({root, kat, true});
  } catch (const p0s::Error& error) {
    Require(error.code() == p0s::ErrorCode::kFilesystem,
            "existing root returned an unexpected typed error");
    Require(std::string(error.what()) ==
                "Prototype store output path must not already exist",
            "existing root returned an unstable error message");
    return;
  }
  throw std::runtime_error("existing output root did not fail loud");
}

std::filesystem::path AtomicTemporaryPath(std::filesystem::path path) {
  path += L".p0s.tmp";
  return path;
}

void CheckAtomicObserverRunsAfterFlushBeforeRename(
    const std::filesystem::path& root) {
  // Given: A published destination and a different complete replacement.
  const auto destination = root / "observed_publication.json";
  const auto temporary = AtomicTemporaryPath(destination);
  p0s::AtomicWriteText(destination, "old payload");
  int flush_calls = 0;
  bool flushed = false;
  bool observed = false;

  // When: Atomic publication reaches its post-flush, pre-rename observer.
  p0s::AtomicWriteText(
      destination, "new payload",
      [&flushed, &observed, &destination, &temporary](
          const p0s::AtomicPublicationObservation& observation) {
        observed = true;

        // Then: The old target remains authoritative while the complete temp is
        // available for deterministic inspection.
        Require(flushed, "observer ran before the flush operation completed");
        Require(observation.destination_path == destination,
                "observer received an unexpected destination path");
        Require(observation.temporary_path == temporary,
                "observer received an unexpected temporary path");
        Require(ReadText(observation.destination_path) == "old payload",
                "observer ran after the destination was replaced");
        Require(ReadText(observation.temporary_path) == "new payload",
                "observer saw an incomplete temporary payload");
      },
      [&flush_calls, &flushed]() {
        ++flush_calls;
        flushed = true;
        return true;
      });

  // Then: Normal publication completes and removes the temporary name.
  Require(flush_calls == 1,
          "successful atomic publication did not flush exactly once");
  Require(observed, "atomic publication did not invoke its observer");
  Require(ReadText(destination) == "new payload",
          "atomic publication did not replace the destination");
  Require(!std::filesystem::exists(temporary),
          "successful atomic publication left its temporary file");
}

class AtomicObserverFailure final : public std::runtime_error {
 public:
  AtomicObserverFailure() : std::runtime_error("atomic observer failure") {}
};

void CheckAtomicObserverFailurePreservesExceptionAndCleansTemp(
    const std::filesystem::path& root) {
  // Given: An existing destination and an observer that fails before rename.
  const auto destination = root / "observer_failure.json";
  const auto temporary = AtomicTemporaryPath(destination);
  p0s::AtomicWriteText(destination, "old payload");

  // When: The observer throws its typed failure.
  try {
    p0s::AtomicWriteText(
        destination, "new payload",
        [](const p0s::AtomicPublicationObservation&) {
          throw AtomicObserverFailure();
        });
  } catch (const AtomicObserverFailure& error) {
    // Then: The original exception is preserved, the old target remains, and
    // the unpublished temporary file is removed.
    Require(std::string(error.what()) == "atomic observer failure",
            "observer failure message changed during cleanup");
    Require(ReadText(destination) == "old payload",
            "observer failure replaced the destination");
    Require(!std::filesystem::exists(temporary),
            "observer failure left an atomic temporary file");
    return;
  }
  throw std::runtime_error("atomic observer failure was not preserved");
}

void CheckAtomicFlushFailurePreventsObserverAndRename(
    const std::filesystem::path& root) {
  // Given: An existing target, an observer, and a deterministic flush failure.
  const auto destination = root / "flush_failure.json";
  const auto temporary = AtomicTemporaryPath(destination);
  p0s::AtomicWriteText(destination, "old payload");
  int flush_calls = 0;
  bool observed = false;

  // When: The injected flush operation reports a stable Windows failure.
  try {
    p0s::AtomicWriteText(
        destination, "new payload",
        [&observed](const p0s::AtomicPublicationObservation&) {
          observed = true;
        },
        [&flush_calls]() {
          ++flush_calls;
          SetLastError(ERROR_WRITE_FAULT);
          return false;
        });
  } catch (const p0s::Error& error) {
    // Then: Flush precedes observation and rename, the old target remains
    // authoritative, the temp is removed, and the typed error is stable.
    Require(error.code() == p0s::ErrorCode::kFilesystem,
            "flush failure returned an unexpected typed error");
    Require(std::string(error.what()) ==
                "FlushFileBuffers for atomic temporary failed with Windows "
                "error " +
                    std::to_string(ERROR_WRITE_FAULT),
            "flush failure returned an unstable error message");
    Require(flush_calls == 1,
            "atomic publication did not invoke flush exactly once");
    Require(!observed, "atomic publication observed an unflushed temporary");
    Require(ReadText(destination) == "old payload",
            "flush failure replaced the destination");
    Require(!std::filesystem::exists(temporary),
            "flush failure left an atomic temporary file");
    return;
  }
  throw std::runtime_error("atomic publication accepted a flush failure");
}

void CheckAtomicWritePublishesEmptyPayload(const std::filesystem::path& root) {
  // Given: A new destination and the minimum zero-byte payload.
  const auto destination = root / "empty_payload.bin";
  const auto temporary = AtomicTemporaryPath(destination);
  bool observed = false;

  // When: The empty payload reaches the post-flush, pre-rename observer.
  p0s::AtomicWriteFile(
      destination, {},
      [&observed](const p0s::AtomicPublicationObservation& observation) {
        observed = true;

        // Then: The complete temporary file exists and is exactly empty.
        Require(std::filesystem::is_regular_file(observation.temporary_path),
                "empty publication observer did not receive a temporary file");
        Require(std::filesystem::file_size(observation.temporary_path) == 0,
                "empty publication temporary file was not zero bytes");
        Require(!std::filesystem::exists(observation.destination_path),
                "empty publication target became visible before rename");
      });

  // Then: The empty destination is atomically visible and no temp remains.
  Require(observed, "empty publication did not invoke its observer");
  Require(std::filesystem::is_regular_file(destination),
          "empty atomic publication did not create its destination");
  Require(std::filesystem::file_size(destination) == 0,
          "empty atomic publication changed payload size");
  Require(!std::filesystem::exists(temporary),
          "empty atomic publication left its temporary file");
}

void CheckAtomicWriteRejectsMissingParent(
    const std::filesystem::path& root) {
  // Given: A destination whose parent directory does not exist.
  const auto destination = root / "missing" / "metadata.json";
  const auto temporary = AtomicTemporaryPath(destination);

  // When: Atomic publication is attempted.
  try {
    p0s::AtomicWriteText(destination, "{}\n");
  } catch (const p0s::Error& error) {
    // Then: The typed failure is stable and no temporary file remains.
    Require(error.code() == p0s::ErrorCode::kFilesystem,
            "missing parent returned an unexpected typed error");
    Require(std::string(error.what()) ==
                "Atomic file parent directory is absent",
            "missing parent returned an unstable error message");
    Require(!std::filesystem::exists(temporary),
            "missing-parent failure left an atomic temporary file");
    return;
  }
  throw std::runtime_error("atomic write accepted an absent parent directory");
}

void CheckAtomicWriteCleansUpAfterMoveFailure(
    const std::filesystem::path& root) {
  // Given: An existing directory occupies the publication destination.
  const auto destination = root / "directory_destination";
  const auto temporary = AtomicTemporaryPath(destination);
  if (!std::filesystem::create_directory(destination)) {
    throw std::runtime_error("cannot create atomic destination directory");
  }

  // When: The temporary file is written but atomic replacement cannot publish.
  try {
    p0s::AtomicWriteText(destination, "{}\n");
  } catch (const p0s::Error& error) {
    // Then: The move failure is preserved and the closed temporary is removed.
    Require(error.code() == p0s::ErrorCode::kFilesystem,
            "move failure returned an unexpected typed error");
    const std::string message(error.what());
    Require(message.rfind("MoveFileExW for atomic publication failed with "
                          "Windows error ",
                          0) == 0,
            "move failure returned an unstable error message");
    Require(std::filesystem::is_directory(destination),
            "move failure replaced the destination directory");
    Require(!std::filesystem::exists(temporary),
            "move failure left an atomic temporary file");
    return;
  }
  throw std::runtime_error("atomic write replaced a destination directory");
}

struct ObservedStoreRun {
  std::vector<p0s::PublicationEvent> events;
  std::vector<ObservedWire> wires;
  std::size_t chunk_cuts = 0;
  std::size_t key_cleanup_calls = 0;
  bool key_cleanup_all_zero = true;
};

ObservedStoreRun GenerateObservedStore(const std::filesystem::path& root,
                                       const std::filesystem::path& kat,
                                       p0s::ShardingMode mode) {
  ObservedStoreRun run;
  std::size_t cycle = 0;
  p0s::GenerateSyntheticStore(
      {root, kat, true, mode,
       [&run, mode](const p0s::AtomicPublicationObservation& observation) {
         CheckShapeFirstChunkCut(observation, mode, run.chunk_cuts);
       },
       [&run](bool all_zero) {
         ++run.key_cleanup_calls;
         run.key_cleanup_all_zero = run.key_cleanup_all_zero && all_zero;
       }},
      [&run, &cycle, mode](const p0s::PublicationEvent& event,
                           const std::filesystem::path& store_root) {
        CheckPublicationEvent(event, store_root);
        CaptureChunkPublication(store_root, mode, event, cycle, run.wires);
        run.events.push_back(event);
      });
  return run;
}

class InjectedKeyUseFailure final : public std::runtime_error {
 public:
  InjectedKeyUseFailure() : std::runtime_error("injected key-use failure") {}
};

void CheckWriterKeyCleanupOnException(const std::filesystem::path& root,
                                      const std::filesystem::path& kat) {
  // Given: A writer callback fails after the verified AES key has been used.
  std::size_t cleanup_calls = 0;
  bool cleanup_all_zero = true;
  const auto output = root / "key_cleanup_failure";

  // When: Chunk publication unwinds the writer through the exception path.
  try {
    p0s::GenerateSyntheticStore(
        {output, kat, true, p0s::ShardingMode::kRoundRobin,
         [](const p0s::AtomicPublicationObservation&) {
           throw InjectedKeyUseFailure();
         },
         [&cleanup_calls, &cleanup_all_zero](bool all_zero) {
           ++cleanup_calls;
           cleanup_all_zero = cleanup_all_zero && all_zero;
         }});
  } catch (const InjectedKeyUseFailure& error) {
    // Then: The original failure survives and the private key copy is zeroed.
    Require(std::string(error.what()) == "injected key-use failure",
            "writer replaced the key-use failure during unwinding");
    Require(cleanup_calls == 1,
            "writer did not clean its verified key exactly once on failure");
    Require(cleanup_all_zero,
            "writer cleanup observer saw non-zero verified key bytes");
    return;
  }
  throw std::runtime_error("writer did not preserve key-use failure");
}

std::vector<p0s::PublicationEvent> GenerateOpenStore(
    const std::filesystem::path& root,
    const std::filesystem::path& kat,
    p0s::ShardingMode mode) {
  std::vector<p0s::PublicationEvent> events;
  p0s::GenerateSyntheticStore(
      {root, kat, false, mode},
      [&events](const p0s::PublicationEvent& event,
                const std::filesystem::path& store_root) {
        CheckPublicationEvent(event, store_root);
        events.push_back(event);
      });
  return events;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error("usage: p0s_store_writer_tests <accepted-kat>");
    }
    TemporaryDirectory temporary;
    const auto round_robin_first = temporary.path() / "round_robin_a";
    const auto round_robin_second = temporary.path() / "round_robin_b";
    const auto sequential_first = temporary.path() / "sequential_a";
    const auto sequential_second = temporary.path() / "sequential_b";
    const auto round_robin_open = temporary.path() / "round_robin_open";
    const auto sequential_open = temporary.path() / "sequential_open";
    const std::filesystem::path kat(argv[1]);

    CheckFiveEventMappingAndValidation();
    CheckChunkPlanningBoundaries();
    CheckPartitionPlannerUsesFiveEventMapping();
    CheckSyntheticValuesAreFiveEventAndFourteenBit();
    CheckStoreGeneratorCliParser(temporary.path());
    CheckStoreGeneratorCliExecutionFailures(temporary.path());
    CheckAtomicObserverRunsAfterFlushBeforeRename(temporary.path());
    CheckAtomicObserverFailurePreservesExceptionAndCleansTemp(temporary.path());
    CheckAtomicFlushFailurePreventsObserverAndRename(temporary.path());
    CheckAtomicWritePublishesEmptyPayload(temporary.path());
    CheckAtomicWriteRejectsMissingParent(temporary.path());
    CheckAtomicWriteCleansUpAfterMoveFailure(temporary.path());
    CheckWriterKeyCleanupOnException(temporary.path(), kat);

    const auto round_robin_run = GenerateObservedStore(
        round_robin_first, kat, p0s::ShardingMode::kRoundRobin);
    p0s::GenerateSyntheticStore(
        {round_robin_second, kat, true, p0s::ShardingMode::kRoundRobin});

    const auto sequential_run = GenerateObservedStore(
        sequential_first, kat, p0s::ShardingMode::kAppendSequential);
    p0s::GenerateSyntheticStore(
        {sequential_second, kat, true,
         p0s::ShardingMode::kAppendSequential});

    const auto round_robin_open_events = GenerateOpenStore(
        round_robin_open, kat, p0s::ShardingMode::kRoundRobin);
    const auto sequential_open_events = GenerateOpenStore(
        sequential_open, kat, p0s::ShardingMode::kAppendSequential);

    Require(publication_checks == 24,
            "writer emitted an unexpected publication event count");
    Require(round_robin_run.chunk_cuts == 12 &&
                sequential_run.chunk_cuts == 6,
            "writer emitted an unexpected atomic chunk cut count");
    // Given/When: Both successful sharding runs release their verified keys.
    // Then: Each private key copy was securely cleared exactly once.
    Require(round_robin_run.key_cleanup_calls == 1 &&
                sequential_run.key_cleanup_calls == 1,
            "successful writer did not clean each verified key exactly once");
    Require(round_robin_run.key_cleanup_all_zero &&
                sequential_run.key_cleanup_all_zero,
            "successful writer cleanup left verified key bytes non-zero");
    CheckLifecycle(round_robin_run.events, p0s::ShardingMode::kRoundRobin);
    CheckLifecycle(sequential_run.events,
                   p0s::ShardingMode::kAppendSequential);
    CheckNonceHistory(round_robin_run.wires, p0s::ShardingMode::kRoundRobin);
    CheckNonceHistory(sequential_run.wires,
                      p0s::ShardingMode::kAppendSequential);
    CheckOpenTerminal(round_robin_open_events, round_robin_open,
                      p0s::ShardingMode::kRoundRobin);
    CheckOpenTerminal(sequential_open_events, sequential_open,
                      p0s::ShardingMode::kAppendSequential);
    CheckFinalStore(round_robin_first, p0s::ShardingMode::kRoundRobin);
    CheckFinalStore(round_robin_second, p0s::ShardingMode::kRoundRobin);
    CheckFinalStore(sequential_first,
                    p0s::ShardingMode::kAppendSequential);
    CheckFinalStore(sequential_second,
                    p0s::ShardingMode::kAppendSequential);
    CheckDecodedRowsAndTailFill(round_robin_first, kat,
                                p0s::ShardingMode::kRoundRobin);
    CheckDecodedRowsAndTailFill(sequential_first, kat,
                                p0s::ShardingMode::kAppendSequential);
    CheckDeterministicJson(round_robin_first, round_robin_second);
    CheckDeterministicJson(sequential_first, sequential_second);
    CheckDeterministicDecodedChunks(round_robin_first, round_robin_second, kat);
    CheckDeterministicDecodedChunks(sequential_first, sequential_second, kat);
    CheckExistingRootFails(round_robin_first, kat);

    std::cout << "store_writer_checks=" << checks
              << " publication_events=" << publication_checks
              << " encrypted_publications=54 deterministic_runs=4 "
                 "status=pass\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "store_writer_tests failed: " << error.what() << '\n';
    return 1;
  }
}
