#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "p0s/atomic_file.h"
#include "p0s/store_contract.h"

namespace p0s {

using SensitiveCleanupObserver = std::function<void(bool)>;

struct StoreGenerationOptions {
  std::filesystem::path output_root;
  std::filesystem::path accepted_kat_path;
  bool finalize = true;
  ShardingMode sharding_mode = ShardingMode::kRoundRobin;
  AtomicPublicationObserver chunk_publication_observer = {};
  SensitiveCleanupObserver sensitive_cleanup_observer = {};
  // Zero preserves the Contract RC layout; nonzero is only for writer tests.
  std::size_t row_chunk_size_for_testing = 0;
};

using StoreGenerationRunner =
    std::function<void(const StoreGenerationOptions&)>;
using StoreWorkerLaunchObserver = std::function<void()>;

struct StoreGeneratorCliSeams {
  StoreGenerationRunner generate = {};
  StoreWorkerLaunchObserver before_worker_launch = {};
};

struct ChunkExtent {
  std::size_t chunk_index;
  std::size_t row_begin;
  std::size_t row_count;
};

struct PartitionChunkPlan {
  ChunkExtent extent;
  std::vector<std::size_t> global_events;
};

struct StoreGeneratorArguments {
  std::filesystem::path accepted_kat_path;
  std::filesystem::path output_root;
  bool finalize = true;
  ShardingMode sharding_mode = ShardingMode::kRoundRobin;
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

[[nodiscard]] std::string_view ShardingModeName(ShardingMode mode);
[[nodiscard]] SyntheticEventLocation MapSyntheticEvent(
    ShardingMode mode,
    std::size_t global_event);
[[nodiscard]] std::vector<ChunkExtent> PlanChunkExtents(
    std::size_t visible_rows,
    std::size_t chunk_rows);
[[nodiscard]] std::vector<PartitionChunkPlan> PlanPartitionChunks(
    ShardingMode mode,
    std::size_t partition,
    std::size_t committed_events,
    std::size_t chunk_rows);
[[nodiscard]] std::string ChunkKeyForIndex(std::size_t rank,
                                           std::size_t chunk_index);
[[nodiscard]] StoreGeneratorArguments ParseStoreGeneratorArguments(
    int argc,
    const char* const argv[]);
int RunStoreGeneratorCli(int argc,
                         const char* const argv[],
                         std::ostream& output,
                         std::ostream& errors,
                         const StoreGeneratorCliSeams& seams = {});

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
