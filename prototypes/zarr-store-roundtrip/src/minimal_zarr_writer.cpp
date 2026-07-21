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
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace p0s {
namespace {

constexpr std::uint8_t kStoreKeyIdMinimum = 1;
constexpr std::string_view kCreatedAt = "2026-07-21T14:12:33+09:00";
constexpr std::string_view kFinalizedAt = "2026-07-21T14:12:34+09:00";

struct TestKeyMaterial {
  std::uint8_t key_id;
  std::array<std::uint8_t, kAes256KeySize> key;
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
  if (bytes.size() > std::numeric_limits<ULONG>::max()) {
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

std::vector<std::uint8_t> DecodeHex(const std::string& value) {
  if (value.size() % 2 != 0) {
    throw Error(ErrorCode::kStoreContract,
                "Accepted gcsa KAT contains odd-length hexadecimal data");
  }
  std::vector<std::uint8_t> result;
  result.reserve(value.size() / 2);
  for (std::size_t index = 0; index < value.size(); index += 2) {
    result.push_back(static_cast<std::uint8_t>(
        (HexNibble(value[index]) << 4) | HexNibble(value[index + 1])));
  }
  return result;
}

TestKeyMaterial LoadVerifiedTestKey(const std::filesystem::path& kat_path) {
  const std::string kat_text = ReadTextFile(kat_path);
  if (Sha256Hex(kat_text) != kAcceptedKatSha256) {
    throw Error(ErrorCode::kStoreContract,
                "Accepted gcsa KAT SHA-256 mismatch");
  }
  const Json kat = ParseStrictJson(kat_text);
  const std::uint64_t raw_key_id = RequireUnsigned(kat, "key_id");
  if (raw_key_id < kStoreKeyIdMinimum ||
      raw_key_id > std::numeric_limits<std::uint8_t>::max()) {
    throw Error(ErrorCode::kStoreContract,
                "Accepted gcsa KAT key_id is outside the wire range");
  }
  const auto decoded = DecodeHex(RequireString(kat, "key_hex"));
  if (decoded.size() != kAes256KeySize) {
    throw Error(ErrorCode::kStoreContract,
                "Accepted gcsa KAT key is not AES-256");
  }
  TestKeyMaterial result{static_cast<std::uint8_t>(raw_key_id), {}};
  std::copy(decoded.begin(), decoded.end(), result.key.begin());
  return result;
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
                  {"clevel", 5},
                  {"shuffle", 1},
                  {"blocksize", 0}};
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
                            {"range_min", 0},
                            {"range_max", 65535},
                            {"unit", "adc"},
                            {"group", scatter ? "scatter" : "fluorescence"}});
  }
  for (std::size_t index = 0; index < kGmiChannelOrder.size(); ++index) {
    channels.push_back(Json{{"name", kGmiChannelOrder[index]},
                            {"file_type", "fh"},
                            {"index", index},
                            {"dtype", "uint16"},
                            {"range_min", 0},
                            {"range_max", 65535},
                            {"unit", "adc"},
                            {"group", "gmi"}});
  }
  return channels;
}

Json BuildFeatures() {
  Json features = Json::array();
  for (std::size_t index = 0; index < kPulseFeatureColumns.size(); ++index) {
    const std::string name(kPulseFeatureColumns[index]);
    const std::size_t split = name.rfind('_');
    const double first = BitsToDouble(SyntheticFeatureBits(0, index));
    const double second = BitsToDouble(SyntheticFeatureBits(1, index));
    features.push_back(Json{{"name", name},
                            {"source_channel", name.substr(0, split)},
                            {"feature_type", name.substr(split + 1)},
                            {"dtype", "float64"},
                            {"range_min", std::min(first, second)},
                            {"range_max", std::max(first, second)},
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
               bool finalized) {
  const std::size_t total_rows = rows[0] + rows[1];
  return Json{
      {"dataset_id", kSyntheticDatasetId},
      {"created_at", kCreatedAt},
      {"source_path", nullptr},
      {"n_events", total_rows},
      {"n_partitions", kSyntheticPartitionCount},
      {"events_per_partition", Json::array({rows[0], rows[1]})},
      {"channels", BuildChannels()},
      {"features", BuildFeatures()},
      {"config", nullptr},
      {"cfg_params", Json{{"synthetic", true}}},
      {"versions",
       Json{{"format", "v1"}, {"library", "analogboard-p0s-prototype"}}},
      {"provenance", nullptr},
      {"extra",
       Json{{"partition_append_observation", "round_robin_reader_order_only"},
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

std::string ChunkKey(const MeasurementArrayContract& contract) {
  return contract.rank == 2 ? "0.0" : "0.0.0";
}

std::vector<std::uint8_t> BuildDecodedChunk(
    const MeasurementArrayContract& contract,
    std::size_t global_event) {
  std::vector<std::uint8_t> decoded(contract.decoded_chunk_bytes, 0);
  if (contract.name == "pulse_features") {
    for (std::size_t feature = 0; feature < kPulseFeatureColumns.size();
         ++feature) {
      AppendLittleEndian(decoded, feature * sizeof(std::uint64_t),
                         SyntheticFeatureBits(global_event, feature),
                         sizeof(std::uint64_t));
    }
    return decoded;
  }

  const std::size_t channel_count =
      contract.name == "gmi_waveform" ? kGmiChannelOrder.size()
                                       : kFlChannelOrder.size();
  for (std::size_t channel = 0; channel < channel_count; ++channel) {
    for (std::size_t sample = 0; sample < kWaveformSamples; ++sample) {
      const std::uint16_t value = SyntheticWaveformValue(
          contract.name, global_event, channel, sample);
      const std::size_t element = channel * kWaveformSamples + sample;
      AppendLittleEndian(decoded, element * sizeof(value), value,
                         sizeof(value));
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
    if (counter_ == std::numeric_limits<std::uint32_t>::max()) {
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

std::uint64_t SyntheticFeatureBits(std::size_t global_event,
                                   std::size_t feature_index) {
  if (global_event >= kSyntheticPartitionCount ||
      feature_index >= kPulseFeatureColumns.size()) {
    throw Error(ErrorCode::kInvalidArgument,
                "Synthetic feature coordinate is outside the fixture");
  }
  if (feature_index == 0) {
    return global_event == 0 ? UINT64_C(0x8000000000000000) : UINT64_C(0);
  }
  if (feature_index == 1) {
    return global_event == 0 ? UINT64_C(1) : UINT64_C(0x8000000000000001);
  }
  if (feature_index == 2) {
    return global_event == 0 ? UINT64_C(0x7fefffffffffffff)
                             : UINT64_C(0xffefffffffffffff);
  }
  const double magnitude = static_cast<double>(feature_index) + 0.25;
  const double value = global_event == 0 ? magnitude : -magnitude;
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
  if (global_event >= kSyntheticPartitionCount || channel >= channel_count ||
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
  return static_cast<std::uint16_t>(value & 0xFFFFU);
}

void GenerateSyntheticStore(const StoreGenerationOptions& options,
                            const PublicationObserver& observer) {
  if (options.output_root.empty() || options.accepted_kat_path.empty()) {
    throw Error(ErrorCode::kInvalidArgument,
                "Prototype store output and KAT paths are required");
  }
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

  TestKeyMaterial loaded_key = LoadVerifiedTestKey(options.accepted_kat_path);
  const std::uint8_t key_id = loaded_key.key_id;
  TestKeyProvider test_keys;
  test_keys.Add(key_id, loaded_key.key);
  std::fill(loaded_key.key.begin(), loaded_key.key.end(), std::uint8_t{0});
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
  const auto meta_path = DatasetPath(options.output_root) / "meta.json";
  AtomicWriteText(meta_path,
                  DumpDeterministicJson(BuildMeta(rows, sealed, 0, false)));
  Notify(observer, "initial_meta_published", 0, rows, sealed, "open",
         options.output_root);

  for (std::size_t partition = 0; partition < kSyntheticPartitionCount;
       ++partition) {
    for (const auto& contract : kMeasurementArrayContracts) {
      const auto partition_path =
          ArrayPath(options.output_root, contract, partition);
      AtomicWriteText(partition_path / ".zarray",
                      DumpDeterministicJson(BuildZarray(contract, 1)));
      auto decoded = BuildDecodedChunk(contract, partition);
      const auto frame =
          CompressBlosc(decoded.data(), decoded.size(), contract.typesize);
      std::fill(decoded.begin(), decoded.end(), std::uint8_t{0});
      const std::string chunk_key = ChunkKey(contract);
      const AeadChunkContext context(std::string(kSyntheticDatasetId),
                                     ArrayRelativePath(contract, partition),
                                     chunk_key, contract.rank);
      std::array<std::uint8_t, kAes256KeySize> encryption_key{};
      if (!test_keys.Lookup(key_id, encryption_key)) {
        throw Error(ErrorCode::kAeadUnknownKey,
                    "Prototype test key is unavailable");
      }
      const auto wire = EncryptAead(frame, context, key_id, encryption_key,
                                    nonces.Next(), nonce_registry);
      std::fill(encryption_key.begin(), encryption_key.end(), std::uint8_t{0});
      AtomicWriteFile(partition_path / chunk_key, wire);
    }

    Notify(observer, "chunks_published", partition, rows, sealed, "open",
           options.output_root);
    rows[partition] = 1;
    sealed[partition] = true;
    const std::size_t generation = partition + 1;
    AtomicWriteText(
        meta_path,
        DumpDeterministicJson(BuildMeta(rows, sealed, generation, false)));
    Notify(observer, "manifest_published", generation, rows, sealed, "open",
           options.output_root);
  }

  if (options.finalize) {
    AtomicWriteText(meta_path,
                    DumpDeterministicJson(BuildMeta(rows, sealed, 2, true)));
    Notify(observer, "finalized_meta_published", 2, rows, sealed,
           "finalized", options.output_root);
  }
}

}  // namespace p0s
