#include "p0s/aead_store.h"
#include "p0s/atomic_file.h"
#include "p0s/error.h"
#include "p0s/minimal_zarr_writer.h"
#include "p0s/store_contract.h"
#include "p0s/strict_json.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
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

std::string ChunkKey(std::size_t rank) {
  return rank == 2 ? "0.0" : "0.0.0";
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
  if (event.stage == "chunks_published") {
    const std::size_t partition = event.visible_rows[0] == 0 ? 0 : 1;
    for (const auto& contract : p0s::kMeasurementArrayContracts) {
      Require(std::filesystem::is_regular_file(
                  ArrayPath(root, contract.name, partition) /
                  ChunkKey(contract.rank)),
              "chunk must exist before manifest publication");
    }
  }
}

void CheckExactZarray(const std::filesystem::path& root,
                      const p0s::MeasurementArrayContract& contract,
                      std::size_t partition) {
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
  Require(zarray.at("shape").at(0) == 1, "Zarr visible shape changed");
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

void CheckFinalStore(const std::filesystem::path& root) {
  const auto marker = ReadJson(root / ".gcsa_store.json");
  Require(marker == p0s::Json{{"capabilities",
                               p0s::Json::array(
                                   {"d21_visibility", "encrypted_chunks"})},
                              {"producer", "analogboard"},
                              {"store_format", 1}},
          "strict store marker changed");

  CheckMetaState(root, 2, {1, 1}, {true, true}, "finalized");
  const auto meta = ReadJson(DatasetPath(root) / "meta.json");
  Require(meta.size() == 22, "strict meta field set changed");
  Require(meta.at("dataset_id") == std::string(p0s::kSyntheticDatasetId),
          "dataset id changed");
  Require(meta.at("n_events") == 2 && meta.at("n_partitions") == 2,
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

  std::set<std::pair<std::uint8_t,
                     std::array<std::uint8_t, p0s::kAeadNonceSize>>>
      key_nonces;
  for (const auto& contract : p0s::kMeasurementArrayContracts) {
    for (std::size_t partition = 0;
         partition < p0s::kSyntheticPartitionCount; ++partition) {
      CheckExactZarray(root, contract, partition);
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

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      throw std::runtime_error("usage: p0s_store_writer_tests <accepted-kat>");
    }
    TemporaryDirectory temporary;
    const auto first = temporary.path() / "store_a";
    const auto second = temporary.path() / "store_b";
    const std::filesystem::path kat(argv[1]);

    CheckAtomicObserverRunsAfterFlushBeforeRename(temporary.path());
    CheckAtomicObserverFailurePreservesExceptionAndCleansTemp(temporary.path());
    CheckAtomicFlushFailurePreventsObserverAndRename(temporary.path());
    CheckAtomicWritePublishesEmptyPayload(temporary.path());
    CheckAtomicWriteRejectsMissingParent(temporary.path());
    CheckAtomicWriteCleansUpAfterMoveFailure(temporary.path());

    p0s::GenerateSyntheticStore(
        {first, kat, true},
        [](const p0s::PublicationEvent& event,
           const std::filesystem::path& root) {
          CheckPublicationEvent(event, root);
        });
    p0s::GenerateSyntheticStore({second, kat, true});

    Require(publication_checks == 6,
            "writer emitted an unexpected publication event count");
    CheckFinalStore(first);
    CheckFinalStore(second);
    CheckDeterministicJson(first, second);
    CheckExistingRootFails(first, kat);

    std::cout << "store_writer_checks=" << checks
              << " publication_events=" << publication_checks
              << " encrypted_chunks=6 deterministic_runs=2 status=pass\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "store_writer_tests failed: " << error.what() << '\n';
    return 1;
  }
}
