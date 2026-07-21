#include "p0s/minimal_zarr_writer.h"

#include "p0s/aead_store.h"
#include "p0s/atomic_file.h"
#include "p0s/blosc_adapter.h"
#include "p0s/error.h"
#include "p0s/store_contract.h"
#include "p0s/strict_json.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace p0s {
namespace {

constexpr std::uint8_t kStoreKeyIdMinimum = 1;
constexpr std::string_view kCreatedAt = "2026-07-21T14:12:33+09:00";
constexpr std::string_view kFinalizedAt = "2026-07-21T14:12:34+09:00";

class ScopedWriterAesKey final {
 public:
  explicit ScopedWriterAesKey(SensitiveCleanupObserver cleanup_observer)
      : cleanup_observer_(std::move(cleanup_observer)) {}

  ~ScopedWriterAesKey() noexcept {
    SecureZeroMemory(bytes_.data(), bytes_.size());
    if (!cleanup_observer_) {
      return;
    }
    const bool all_zero = std::all_of(
        bytes_.begin(), bytes_.end(),
        [](std::uint8_t byte) { return byte == std::uint8_t{0}; });
    try {
      cleanup_observer_(all_zero);
    } catch (...) {
    }
  }

  ScopedWriterAesKey(const ScopedWriterAesKey&) = delete;
  ScopedWriterAesKey& operator=(const ScopedWriterAesKey&) = delete;

  [[nodiscard]] std::array<std::uint8_t, kAes256KeySize>& mutable_bytes()
      noexcept {
    return bytes_;
  }

  [[nodiscard]] const std::array<std::uint8_t, kAes256KeySize>& bytes()
      const noexcept {
    return bytes_;
  }

 private:
  std::array<std::uint8_t, kAes256KeySize> bytes_{};
  SensitiveCleanupObserver cleanup_observer_;
};

class AlgorithmHandle final {
 public:
  explicit AlgorithmHandle(LPCWSTR algorithm) {
    const NTSTATUS status =
        BCryptOpenAlgorithmProvider(&handle_, algorithm, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
      throw Error(ErrorCode::kCryptoBackend,
                  "BCryptOpenAlgorithmProvider failed for store helper");
    }
  }

  ~AlgorithmHandle() {
    if (handle_ != nullptr) {
      BCryptCloseAlgorithmProvider(handle_, 0);
    }
  }

  AlgorithmHandle(const AlgorithmHandle&) = delete;
  AlgorithmHandle& operator=(const AlgorithmHandle&) = delete;
  [[nodiscard]] BCRYPT_ALG_HANDLE get() const noexcept { return handle_; }

 private:
  BCRYPT_ALG_HANDLE handle_ = nullptr;
};

class HashHandle final {
 public:
  HashHandle(BCRYPT_ALG_HANDLE algorithm, std::vector<std::uint8_t>& object) {
    const NTSTATUS status = BCryptCreateHash(
        algorithm, &handle_, object.data(), static_cast<ULONG>(object.size()),
        nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
      throw Error(ErrorCode::kCryptoBackend,
                  "BCryptCreateHash failed for KAT verification");
    }
  }

  ~HashHandle() {
    if (handle_ != nullptr) {
      BCryptDestroyHash(handle_);
    }
  }

  HashHandle(const HashHandle&) = delete;
  HashHandle& operator=(const HashHandle&) = delete;
  [[nodiscard]] BCRYPT_HASH_HANDLE get() const noexcept { return handle_; }

 private:
  BCRYPT_HASH_HANDLE handle_ = nullptr;
};

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw Error(ErrorCode::kFilesystem,
                "Accepted gcsa KAT cannot be opened");
  }
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

ULONG HashProperty(BCRYPT_ALG_HANDLE algorithm, LPCWSTR property) {
  ULONG value = 0;
  ULONG copied = 0;
  const NTSTATUS status = BCryptGetProperty(
      algorithm, property, reinterpret_cast<PUCHAR>(&value), sizeof(value),
      &copied, 0);
  if (!BCRYPT_SUCCESS(status) || copied != sizeof(value) || value == 0) {
    throw Error(ErrorCode::kCryptoBackend,
                "BCryptGetProperty failed for KAT verification");
  }
  return value;
}

std::string Sha256Hex(const std::string& bytes) {
  if (bytes.size() > (std::numeric_limits<ULONG>::max)()) {
    throw Error(ErrorCode::kSizeOverflow,
                "KAT byte count exceeds the CNG hash limit");
  }
  AlgorithmHandle algorithm(BCRYPT_SHA256_ALGORITHM);
  std::vector<std::uint8_t> hash_object(
      HashProperty(algorithm.get(), BCRYPT_OBJECT_LENGTH));
  std::vector<std::uint8_t> digest(
      HashProperty(algorithm.get(), BCRYPT_HASH_LENGTH));
  HashHandle hash(algorithm.get(), hash_object);
  const NTSTATUS update_status = BCryptHashData(
      hash.get(), reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
      static_cast<ULONG>(bytes.size()), 0);
  if (!BCRYPT_SUCCESS(update_status)) {
    throw Error(ErrorCode::kCryptoBackend,
                "BCryptHashData failed for KAT verification");
  }
  const NTSTATUS finish_status = BCryptFinishHash(
      hash.get(), digest.data(), static_cast<ULONG>(digest.size()), 0);
  if (!BCRYPT_SUCCESS(finish_status)) {
    throw Error(ErrorCode::kCryptoBackend,
                "BCryptFinishHash failed for KAT verification");
  }
  std::ostringstream rendered;
  rendered << std::hex << std::setfill('0');
  for (const std::uint8_t byte : digest) {
    rendered << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return rendered.str();
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
  throw Error(ErrorCode::kStoreContract,
              "Accepted gcsa KAT contains invalid hexadecimal data");
}

std::uint8_t LoadVerifiedTestKey(const std::filesystem::path& kat_path,
                                 ScopedWriterAesKey& key) {
  const std::string kat_text = ReadTextFile(kat_path);
  if (Sha256Hex(kat_text) != kAcceptedKatSha256) {
    throw Error(ErrorCode::kStoreContract,
                "Accepted gcsa KAT SHA-256 mismatch");
  }
  const Json kat = ParseStrictJson(kat_text);
  const std::uint64_t raw_key_id = RequireUnsigned(kat, "key_id");
  if (raw_key_id < kStoreKeyIdMinimum ||
      raw_key_id > (std::numeric_limits<std::uint8_t>::max)()) {
    throw Error(ErrorCode::kStoreContract,
                "Accepted gcsa KAT key_id is outside the wire range");
  }
  const auto& encoded_key = RequireString(kat, "key_hex");
  if (encoded_key.size() != kAes256KeySize * 2) {
    throw Error(ErrorCode::kStoreContract,
                "Accepted gcsa KAT key is not AES-256");
  }
  for (std::size_t byte_index = 0; byte_index < kAes256KeySize; ++byte_index) {
    const std::size_t hex_index = byte_index * 2;
    key.mutable_bytes()[byte_index] = static_cast<std::uint8_t>(
        (HexNibble(encoded_key[hex_index]) << 4) |
        HexNibble(encoded_key[hex_index + 1]));
  }
  return static_cast<std::uint8_t>(raw_key_id);
}

void CreateDirectoryChecked(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::create_directory(path, error) || error) {
    throw Error(ErrorCode::kFilesystem,
                "Prototype store directory creation failed");
  }
}

Json BuildZarray(const MeasurementArrayContract& contract,
                 std::size_t visible_rows) {
  Json chunks = Json::array();
  Json shape = Json::array();
  for (std::size_t dimension = 0; dimension < contract.rank; ++dimension) {
    chunks.push_back(contract.chunks[dimension]);
    if (dimension == 0) {
      shape.push_back(visible_rows);
    } else {
      shape.push_back(contract.trailing_shape[dimension - 1]);
    }
  }
  Json compressor{{"id", "blosc"},
                  {"cname", "lz4"},
                  {"clevel", kBloscCompressionLevel},
                  {"shuffle", kBloscShuffle},
                  {"blocksize", kBloscBlockSize}};
  return Json{{"chunks", std::move(chunks)},
              {"compressor", std::move(compressor)},
              {"dimension_separator", "."},
              {"dtype", contract.dtype},
              {"fill_value", contract.floating_fill ? Json(0.0) : Json(0)},
              {"filters", nullptr},
              {"order", "C"},
              {"shape", std::move(shape)},
              {"zarr_format", 2}};
}

double BitsToDouble(std::uint64_t bits) {
  double value = 0.0;
  static_assert(sizeof(value) == sizeof(bits));
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void AppendLittleEndian(std::vector<std::uint8_t>& output,
                        std::size_t offset,
                        std::uint64_t value,
                        std::size_t byte_count) {
  if (offset > output.size() || byte_count > output.size() - offset) {
    throw Error(ErrorCode::kSizeOverflow,
                "Synthetic chunk write exceeds its decoded buffer");
  }
  for (std::size_t byte = 0; byte < byte_count; ++byte) {
    output[offset + byte] =
        static_cast<std::uint8_t>((value >> (byte * 8)) & 0xFFU);
  }
}

Json BuildChannels() {
  Json channels = Json::array();
  for (std::size_t index = 0; index < kFlChannelOrder.size(); ++index) {
    const std::string_view name = kFlChannelOrder[index];
    const bool scatter = index < 2;
    channels.push_back(Json{{"name", name},
                            {"file_type", "fl"},
                            {"index", index},
                            {"dtype", "uint16"},
                            {"range_min", kAdcMinimum},
                            {"range_max", kAdcMaximum},
                            {"unit", "adc"},
                            {"group", scatter ? "scatter" : "fluorescence"}});
  }
  for (std::size_t index = 0; index < kGmiChannelOrder.size(); ++index) {
    channels.push_back(Json{{"name", kGmiChannelOrder[index]},
                            {"file_type", "fh"},
                            {"index", index},
                            {"dtype", "uint16"},
                            {"range_min", kAdcMinimum},
                            {"range_max", kAdcMaximum},
                            {"unit", "adc"},
                            {"group", "gmi"}});
  }
  return channels;
}

Json BuildFeatures(std::size_t committed_events) {
  Json features = Json::array();
  for (std::size_t index = 0; index < kPulseFeatureColumns.size(); ++index) {
    const std::string name(kPulseFeatureColumns[index]);
    const std::size_t split = name.rfind('_');
    Json range_min = nullptr;
    Json range_max = nullptr;
    if (committed_events > 0) {
      double minimum = BitsToDouble(SyntheticFeatureBits(0, index));
      double maximum = minimum;
      for (std::size_t event = 1; event < committed_events; ++event) {
        const double value = BitsToDouble(SyntheticFeatureBits(event, index));
        minimum = (std::min)(minimum, value);
        maximum = (std::max)(maximum, value);
      }
      if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        throw Error(ErrorCode::kStoreContract,
                    "Synthetic feature range must remain finite");
      }
      range_min = minimum;
      range_max = maximum;
    }
    features.push_back(Json{{"name", name},
                            {"source_channel", name.substr(0, split)},
                            {"feature_type", name.substr(split + 1)},
                            {"dtype", "float64"},
                            {"range_min", std::move(range_min)},
                            {"range_max", std::move(range_max)},
                            {"unit", "adc"}});
  }
  return features;
}

Json BuildManifests(const std::array<std::size_t, 2>& rows,
                    const std::array<bool, 2>& sealed) {
  Json manifests = Json::object();
  for (const auto& contract : kMeasurementArrayContracts) {
    Json entries = Json::array();
    for (std::size_t partition = 0; partition < rows.size(); ++partition) {
      entries.push_back(Json{{"partition", partition},
                             {"row_count", rows[partition]},
                             {"sealed", sealed[partition]}});
    }
    manifests[std::string(contract.name)] = std::move(entries);
  }
  return manifests;
}

Json BuildMeta(const std::array<std::size_t, 2>& rows,
               const std::array<bool, 2>& sealed,
               std::size_t generation,
               bool finalized,
               ShardingMode mode) {
  const std::size_t total_rows = rows[0] + rows[1];
  return Json{
      {"dataset_id", kSyntheticDatasetId},
      {"created_at", kCreatedAt},
      {"source_path", nullptr},
      {"n_events", total_rows},
      {"n_partitions", kSyntheticPartitionCount},
      {"events_per_partition", Json::array({rows[0], rows[1]})},
      {"channels", BuildChannels()},
      {"features", BuildFeatures(total_rows)},
      {"config", nullptr},
      {"cfg_params", Json{{"synthetic", true}}},
      {"versions",
       Json{{"format", "v1"}, {"library", "analogboard-p0s-prototype"}}},
      {"provenance", nullptr},
      {"extra",
       Json{{"partition_sharding", ShardingModeName(mode)},
            {"prototype_scope", "phase0_dependency_preflight"}}},
      {"status", finalized ? "finalized" : "open"},
      {"finalized_at", finalized ? Json(kFinalizedAt) : Json(nullptr)},
      {"write_generation", generation},
      {"meta_generation", 0},
      {"feature_schema_version", 1},
      {"partition_manifests", BuildManifests(rows, sealed)},
      {"display_name", "P0-S synthetic roundtrip"},
      {"comment", "Synthetic dependency-path validation only"},
      {"tags", Json::array({"p0-s", "synthetic"})}};
}

std::filesystem::path DatasetPath(const std::filesystem::path& root) {
  return root / "datasets" / std::string(kSyntheticDatasetId);
}

std::filesystem::path ArrayPath(const std::filesystem::path& root,
                                const MeasurementArrayContract& contract,
                                std::size_t partition) {
  return DatasetPath(root) / std::string(contract.name) /
         ("partition_" + std::to_string(partition) + ".zarr");
}

std::string ArrayRelativePath(const MeasurementArrayContract& contract,
                              std::size_t partition) {
  return std::string(contract.name) + "/partition_" +
         std::to_string(partition) + ".zarr";
}

std::vector<std::uint8_t> BuildDecodedChunk(
    const MeasurementArrayContract& contract,
    const PartitionChunkPlan& chunk) {
  if (chunk.global_events.size() != chunk.extent.row_count) {
    throw Error(ErrorCode::kStoreContract,
                "Partition chunk plan row count is inconsistent");
  }
  std::vector<std::uint8_t> decoded(contract.decoded_chunk_bytes, 0);
  const std::size_t row_bytes =
      contract.decoded_chunk_bytes / contract.chunks[0];
  if (contract.name == "pulse_features") {
    for (std::size_t row = 0; row < chunk.global_events.size(); ++row) {
      for (std::size_t feature = 0; feature < kPulseFeatureColumns.size();
           ++feature) {
        AppendLittleEndian(
            decoded, row * row_bytes + feature * sizeof(std::uint64_t),
            SyntheticFeatureBits(chunk.global_events[row], feature),
            sizeof(std::uint64_t));
      }
    }
    return decoded;
  }

  const std::size_t channel_count =
      contract.name == "gmi_waveform" ? kGmiChannelOrder.size()
                                       : kFlChannelOrder.size();
  for (std::size_t row = 0; row < chunk.global_events.size(); ++row) {
    for (std::size_t channel = 0; channel < channel_count; ++channel) {
      for (std::size_t sample = 0; sample < kWaveformSamples; ++sample) {
        const std::uint16_t value = SyntheticWaveformValue(
            contract.name, chunk.global_events[row], channel, sample);
        const std::size_t element = channel * kWaveformSamples + sample;
        AppendLittleEndian(decoded,
                           row * row_bytes + element * sizeof(value), value,
                           sizeof(value));
      }
    }
  }
  return decoded;
}

class NonceSequence final {
 public:
  NonceSequence() {
    const NTSTATUS status = BCryptGenRandom(
        nullptr, salt_.data(), static_cast<ULONG>(salt_.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
      throw Error(ErrorCode::kCryptoBackend,
                  "BCryptGenRandom failed for prototype nonce salt");
    }
  }

  [[nodiscard]] std::array<std::uint8_t, kAeadNonceSize> Next() {
    if (counter_ == (std::numeric_limits<std::uint32_t>::max)()) {
      throw Error(ErrorCode::kSizeOverflow,
                  "Prototype nonce counter is exhausted");
    }
    ++counter_;
    std::array<std::uint8_t, kAeadNonceSize> nonce{};
    std::copy(salt_.begin(), salt_.end(), nonce.begin());
    for (std::size_t index = 0; index < sizeof(counter_); ++index) {
      nonce[salt_.size() + index] = static_cast<std::uint8_t>(
          (counter_ >> ((sizeof(counter_) - index - 1) * 8)) & 0xFFU);
    }
    return nonce;
  }

 private:
  std::array<std::uint8_t, 8> salt_{};
  std::uint32_t counter_ = 0;
};

void Notify(const PublicationObserver& observer,
            std::string stage,
            std::size_t generation,
            const std::array<std::size_t, 2>& rows,
            const std::array<bool, 2>& sealed,
            std::string status,
            const std::filesystem::path& root) {
  if (observer) {
    observer(PublicationEvent{std::move(stage), generation, rows, sealed,
                              std::move(status)},
             root);
  }
}

}  // namespace

std::string_view ShardingModeName(ShardingMode mode) {
  switch (mode) {
    case ShardingMode::kRoundRobin:
      return "round-robin";
    case ShardingMode::kAppendSequential:
      return "append-sequential";
  }
  throw Error(ErrorCode::kInvalidArgument, "Sharding mode is invalid");
}

SyntheticEventLocation MapSyntheticEvent(ShardingMode mode,
                                          std::size_t global_event) {
  static_cast<void>(ShardingModeName(mode));
  if (global_event >= kSyntheticEventCount) {
    throw Error(ErrorCode::kInvalidArgument,
                "Synthetic global event is outside the five-event fixture");
  }
  if (mode == ShardingMode::kRoundRobin) {
    return {global_event % kSyntheticPartitionCount,
            global_event / kSyntheticPartitionCount};
  }
  const std::size_t first_partition_rows = kSyntheticRearmBatchSizes[0];
  return global_event < first_partition_rows
             ? SyntheticEventLocation{0, global_event}
             : SyntheticEventLocation{1,
                                      global_event - first_partition_rows};
}

std::vector<ChunkExtent> PlanChunkExtents(std::size_t visible_rows,
                                          std::size_t chunk_rows) {
  if (chunk_rows == 0) {
    throw Error(ErrorCode::kInvalidArgument,
                "Chunk row count must be greater than zero");
  }
  const std::size_t full_chunks = visible_rows / chunk_rows;
  const std::size_t tail_rows = visible_rows % chunk_rows;
  const std::size_t chunk_count = full_chunks + (tail_rows == 0 ? 0 : 1);
  const std::vector<ChunkExtent> limit_probe;
  if (chunk_count > limit_probe.max_size()) {
    throw Error(ErrorCode::kSizeOverflow,
                "Chunk plan exceeds addressable extent count");
  }

  std::vector<ChunkExtent> extents;
  extents.reserve(chunk_count);
  for (std::size_t chunk = 0; chunk < chunk_count; ++chunk) {
    const std::size_t row_begin = chunk * chunk_rows;
    const std::size_t remaining = visible_rows - row_begin;
    extents.push_back(
        {chunk, row_begin, (std::min)(remaining, chunk_rows)});
  }
  return extents;
}

std::vector<PartitionChunkPlan> PlanPartitionChunks(
    ShardingMode mode,
    std::size_t partition,
    std::size_t committed_events,
    std::size_t chunk_rows) {
  static_cast<void>(ShardingModeName(mode));
  if (partition >= kSyntheticPartitionCount) {
    throw Error(ErrorCode::kInvalidArgument,
                "Synthetic partition is outside the fixture");
  }
  if (committed_events > kSyntheticEventCount) {
    throw Error(ErrorCode::kInvalidArgument,
                "Committed event count is outside the five-event fixture");
  }

  std::vector<std::size_t> global_events;
  global_events.reserve(committed_events);
  for (std::size_t event = 0; event < committed_events; ++event) {
    const auto location = MapSyntheticEvent(mode, event);
    if (location.partition == partition) {
      global_events.push_back(event);
    }
  }

  const auto extents = PlanChunkExtents(global_events.size(), chunk_rows);
  std::vector<PartitionChunkPlan> chunks;
  chunks.reserve(extents.size());
  for (const auto& extent : extents) {
    const auto begin = global_events.begin() + extent.row_begin;
    chunks.push_back(
        {extent, std::vector<std::size_t>(begin, begin + extent.row_count)});
  }
  return chunks;
}

std::string ChunkKeyForIndex(std::size_t rank, std::size_t chunk_index) {
  if (rank == 2) {
    return std::to_string(chunk_index) + ".0";
  }
  if (rank == 3) {
    return std::to_string(chunk_index) + ".0.0";
  }
  throw Error(ErrorCode::kInvalidArgument,
              "Zarr chunk rank must be two or three");
}

StoreGeneratorArguments ParseStoreGeneratorArguments(
    int argc,
    const char* const argv[]) {
  if (argc < 3 || argv == nullptr || argv[1] == nullptr ||
      argv[2] == nullptr) {
    throw Error(
        ErrorCode::kInvalidArgument,
        "usage: p0s_store_generator <accepted-kat> <output-root> [--open] "
        "[--sharding round-robin|append-sequential]");
  }

  StoreGeneratorArguments result;
  result.accepted_kat_path = std::filesystem::path(argv[1]);
  result.output_root = std::filesystem::path(argv[2]);
  bool saw_open = false;
  bool saw_sharding = false;
  for (int index = 3; index < argc; ++index) {
    if (argv[index] == nullptr) {
      throw Error(ErrorCode::kInvalidArgument,
                  "store_generator received a null option");
    }
    const std::string option(argv[index]);
    if (option == "--open") {
      if (saw_open) {
        throw Error(
            ErrorCode::kInvalidArgument,
            "store_generator option --open was specified more than once");
      }
      saw_open = true;
      result.finalize = false;
      continue;
    }
    if (option == "--sharding") {
      if (saw_sharding) {
        throw Error(
            ErrorCode::kInvalidArgument,
            "store_generator option --sharding was specified more than once");
      }
      saw_sharding = true;
      if (index + 1 >= argc || argv[index + 1] == nullptr ||
          std::string_view(argv[index + 1]).rfind("--", 0) == 0) {
        throw Error(ErrorCode::kInvalidArgument,
                    "store_generator option --sharding requires a mode");
      }
      const std::string mode(argv[++index]);
      if (mode == "round-robin") {
        result.sharding_mode = ShardingMode::kRoundRobin;
      } else if (mode == "append-sequential") {
        result.sharding_mode = ShardingMode::kAppendSequential;
      } else {
        throw Error(
            ErrorCode::kInvalidArgument,
            "store_generator sharding mode must be round-robin or "
            "append-sequential");
      }
      continue;
    }
    throw Error(ErrorCode::kInvalidArgument,
                "store_generator received an unknown option: " + option);
  }
  return result;
}

int RunStoreGeneratorCli(int argc,
                         const char* const argv[],
                         std::ostream& output,
                         std::ostream& errors,
                         const StoreGeneratorCliSeams& seams) {
  try {
    StoreGeneratorArguments arguments;
    try {
      arguments = ParseStoreGeneratorArguments(argc, argv);
    } catch (const Error& error) {
      errors << error.what() << '\n';
      return 2;
    }

    const StoreGenerationOptions generation_options{
        arguments.output_root, arguments.accepted_kat_path, arguments.finalize,
        arguments.sharding_mode};
    StoreGenerationRunner generate = seams.generate;
    if (!generate) {
      generate = [](const StoreGenerationOptions& options) {
        GenerateSyntheticStore(options);
      };
    }
    std::exception_ptr failure;
    const std::function<void()> worker_task =
        [&generation_options, &generate, &failure] {
          try {
            generate(generation_options);
          } catch (...) {
            failure = std::current_exception();
          }
        };
    if (seams.before_worker_launch) {
      seams.before_worker_launch();
    }
    std::thread worker(worker_task);
    worker.join();
    if (failure != nullptr) {
      std::rethrow_exception(failure);
    }

    output << "synthetic_store_status="
           << (arguments.finalize ? "finalized" : "open")
           << " status=pass\n";
    return 0;
  } catch (const std::exception& error) {
    errors << "store generation failed: " << error.what() << '\n';
    return 1;
  } catch (...) {
    errors << "store generation failed: non-standard exception\n";
    return 1;
  }
}

std::uint64_t SyntheticFeatureBits(std::size_t global_event,
                                   std::size_t feature_index) {
  if (global_event >= kSyntheticEventCount ||
      feature_index >= kPulseFeatureColumns.size()) {
    throw Error(ErrorCode::kInvalidArgument,
                "Synthetic feature coordinate is outside the fixture");
  }
  if (feature_index == 0 && global_event < 2) {
    return global_event == 0 ? UINT64_C(0x8000000000000000) : UINT64_C(0);
  }
  if (feature_index == 1 && global_event < 2) {
    return global_event == 0 ? UINT64_C(1) : UINT64_C(0x8000000000000001);
  }
  if (feature_index == 2 && global_event < 2) {
    return global_event == 0 ? UINT64_C(0x7fefffffffffffff)
                             : UINT64_C(0xffefffffffffffff);
  }
  const double event_scale =
      global_event < 2 ? 1.0 : static_cast<double>(global_event);
  const double magnitude =
      (static_cast<double>(feature_index) + 0.25) * event_scale;
  const double value = global_event % 2 == 0 ? magnitude : -magnitude;
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

std::uint16_t SyntheticWaveformValue(std::string_view array_name,
                                     std::size_t global_event,
                                     std::size_t channel,
                                     std::size_t sample) {
  const std::size_t channel_count =
      array_name == "gmi_waveform"
          ? kGmiChannelOrder.size()
          : array_name == "fl_waveform" ? kFlChannelOrder.size() : 0;
  if (global_event >= kSyntheticEventCount || channel >= channel_count ||
      sample >= kWaveformSamples) {
    throw Error(ErrorCode::kInvalidArgument,
                "Synthetic waveform coordinate is outside the fixture");
  }
  const std::uint32_t array_offset =
      array_name == "gmi_waveform" ? 1000U : 30000U;
  const std::uint32_t value = array_offset +
                              static_cast<std::uint32_t>(global_event * 10000) +
                              static_cast<std::uint32_t>(channel * 2400) +
                              static_cast<std::uint32_t>(sample);
  return static_cast<std::uint16_t>(value & kAdcMaximum);
}

void GenerateSyntheticStore(const StoreGenerationOptions& options,
                            const PublicationObserver& observer) {
  if (options.output_root.empty() || options.accepted_kat_path.empty()) {
    throw Error(ErrorCode::kInvalidArgument,
                "Prototype store output and KAT paths are required");
  }
  static_cast<void>(ShardingModeName(options.sharding_mode));
  std::error_code error;
  const bool output_exists = std::filesystem::exists(options.output_root, error);
  if (error) {
    throw Error(ErrorCode::kFilesystem,
                "Prototype store output path cannot be inspected");
  }
  if (output_exists) {
    throw Error(ErrorCode::kFilesystem,
                "Prototype store output path must not already exist");
  }

  ScopedWriterAesKey loaded_key(options.sensitive_cleanup_observer);
  const std::uint8_t key_id =
      LoadVerifiedTestKey(options.accepted_kat_path, loaded_key);
  NonceRegistry nonce_registry;
  NonceSequence nonces;

  CreateDirectoryChecked(options.output_root);
  CreateDirectoryChecked(options.output_root / "datasets");
  CreateDirectoryChecked(DatasetPath(options.output_root));
  AtomicWriteText(
      options.output_root / ".gcsa_store.json",
      DumpDeterministicJson(Json{{"store_format", 1},
                                 {"producer", "analogboard"},
                                 {"capabilities",
                                  Json::array({"d21_visibility",
                                               "encrypted_chunks"})}}));

  for (const auto& contract : kMeasurementArrayContracts) {
    const auto array_root = DatasetPath(options.output_root) /
                            std::string(contract.name);
    CreateDirectoryChecked(array_root);
    for (std::size_t partition = 0; partition < kSyntheticPartitionCount;
         ++partition) {
      const auto partition_path = ArrayPath(options.output_root, contract,
                                            partition);
      CreateDirectoryChecked(partition_path);
      AtomicWriteText(partition_path / ".zarray",
                      DumpDeterministicJson(BuildZarray(contract, 0)));
    }
  }

  std::array<std::size_t, 2> rows{0, 0};
  std::array<bool, 2> sealed{false, false};
  std::size_t generation = 0;
  std::size_t committed_events = 0;
  const auto meta_path = DatasetPath(options.output_root) / "meta.json";
  AtomicWriteText(
      meta_path,
      DumpDeterministicJson(
          BuildMeta(rows, sealed, generation, false, options.sharding_mode)));
  Notify(observer, "initial_meta_published", 0, rows, sealed, "open",
         options.output_root);

  for (std::size_t cycle = 0; cycle < kSyntheticRearmBatchSizes.size();
       ++cycle) {
    const std::size_t next_committed =
        committed_events + kSyntheticRearmBatchSizes[cycle];
    auto next_rows = rows;
    for (std::size_t event = committed_events; event < next_committed;
         ++event) {
      ++next_rows[MapSyntheticEvent(options.sharding_mode, event).partition];
    }

    for (const auto& contract : kMeasurementArrayContracts) {
      for (std::size_t partition = 0; partition < kSyntheticPartitionCount;
           ++partition) {
        if (next_rows[partition] == rows[partition]) {
          continue;
        }
        const auto partition_path =
            ArrayPath(options.output_root, contract, partition);
        const auto chunks = PlanPartitionChunks(
            options.sharding_mode, partition, next_committed,
            contract.chunks[0]);
        AtomicWriteText(
            partition_path / ".zarray",
            DumpDeterministicJson(BuildZarray(contract, next_rows[partition])));
        for (const auto& chunk : chunks) {
          auto decoded = BuildDecodedChunk(contract, chunk);
          const auto frame =
              CompressBlosc(decoded.data(), decoded.size(), contract.typesize);
          std::fill(decoded.begin(), decoded.end(), std::uint8_t{0});
          const std::string chunk_key =
              ChunkKeyForIndex(contract.rank, chunk.extent.chunk_index);
          const AeadChunkContext context(
              std::string(kSyntheticDatasetId),
              ArrayRelativePath(contract, partition), chunk_key, contract.rank);
          const auto wire = EncryptAead(frame, context, key_id,
                                        loaded_key.bytes(), nonces.Next(),
                                        nonce_registry);
          AtomicWriteFile(partition_path / chunk_key, wire,
                          options.chunk_publication_observer);
        }
      }
    }

    Notify(observer, "chunks_published", generation, rows, sealed, "open",
           options.output_root);
    committed_events = next_committed;
    rows = next_rows;
    if (options.sharding_mode == ShardingMode::kAppendSequential) {
      sealed[0] = committed_events >= kSyntheticRearmBatchSizes[0];
      sealed[1] = committed_events >= kSyntheticEventCount;
    }
    ++generation;
    AtomicWriteText(
        meta_path,
        DumpDeterministicJson(BuildMeta(rows, sealed, generation, false,
                                        options.sharding_mode)));
    Notify(observer, "manifest_published", generation, rows, sealed, "open",
           options.output_root);
  }

  if (options.sharding_mode == ShardingMode::kRoundRobin) {
    sealed = {true, true};
    ++generation;
    AtomicWriteText(
        meta_path,
        DumpDeterministicJson(BuildMeta(rows, sealed, generation, false,
                                        options.sharding_mode)));
    Notify(observer, "sealed_meta_published", generation, rows, sealed, "open",
           options.output_root);
  }

  if (options.finalize) {
    AtomicWriteText(
        meta_path,
        DumpDeterministicJson(BuildMeta(rows, sealed, generation, true,
                                        options.sharding_mode)));
    Notify(observer, "finalized_meta_published", generation, rows, sealed,
           "finalized", options.output_root);
  }
}

}  // namespace p0s
