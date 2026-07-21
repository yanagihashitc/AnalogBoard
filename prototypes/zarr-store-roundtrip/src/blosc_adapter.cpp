#include "p0s/blosc_adapter.h"

#include "p0s/error.h"

#include <blosc.h>

#include <limits>
#include <string>

namespace p0s {
namespace {

void ValidateTypesize(std::size_t typesize) {
  if (typesize != 2 && typesize != 8) {
    throw Error(ErrorCode::kUnsupportedTypesize,
                "Blosc typesize must be 2 or 8 bytes");
  }
}

void ValidateSourceSize(std::size_t source_size) {
  if (source_size > static_cast<std::size_t>(BLOSC_MAX_BUFFERSIZE)) {
    throw Error(ErrorCode::kSizeOverflow,
                "Blosc source size exceeds BLOSC_MAX_BUFFERSIZE");
  }
}

}  // namespace

std::vector<std::uint8_t> CompressBlosc(const std::uint8_t* source,
                                        std::size_t source_size,
                                        std::size_t typesize) {
  ValidateTypesize(typesize);
  ValidateSourceSize(source_size);
  if (source == nullptr && source_size != 0) {
    throw Error(ErrorCode::kInvalidArgument,
                "Blosc source is null for a non-empty input");
  }
  if (source_size >
      std::numeric_limits<std::size_t>::max() - BLOSC_MAX_OVERHEAD) {
    throw Error(ErrorCode::kSizeOverflow,
                "Blosc destination capacity arithmetic overflow");
  }

  const std::size_t capacity = source_size + BLOSC_MAX_OVERHEAD;
  std::vector<std::uint8_t> frame(capacity);
  const int compressed_size = blosc_compress_ctx(
      kBloscCompressionLevel, kBloscShuffle, typesize, source_size, source,
      frame.data(), frame.size(), "lz4", kBloscBlockSize,
      kBloscInternalThreads);
  if (compressed_size <= 0) {
    throw Error(ErrorCode::kCompressionFailed,
                "Blosc lz4 compression failed");
  }
  const auto result_size = static_cast<std::size_t>(compressed_size);
  if (result_size > frame.size() || result_size < BLOSC_MIN_HEADER_LENGTH) {
    throw Error(ErrorCode::kCompressionFailed,
                "Blosc returned an invalid compressed size");
  }
  frame.resize(result_size);
  return frame;
}

std::vector<std::uint8_t> DecompressBlosc(
    const std::vector<std::uint8_t>& frame,
    std::size_t expected_size) {
  ValidateSourceSize(expected_size);
  if (frame.size() < BLOSC_MIN_HEADER_LENGTH) {
    throw Error(ErrorCode::kFrameInvalid,
                "Blosc frame is shorter than its minimum header");
  }

  std::size_t validated_size = 0;
  if (blosc_cbuffer_validate(frame.data(), frame.size(), &validated_size) != 0) {
    throw Error(ErrorCode::kFrameInvalid,
                "Blosc frame validation failed");
  }
  if (validated_size != expected_size) {
    throw Error(ErrorCode::kDecodedSizeMismatch,
                "Blosc decoded size does not match the expected Zarr chunk size");
  }

  std::vector<std::uint8_t> output(expected_size);
  const int decoded_size = blosc_decompress_ctx(
      frame.data(), output.data(), output.size(), kBloscInternalThreads);
  if (decoded_size <= 0) {
    throw Error(ErrorCode::kDecompressionFailed,
                "Blosc lz4 decompression failed");
  }
  if (static_cast<std::size_t>(decoded_size) != expected_size) {
    throw Error(ErrorCode::kDecodedSizeMismatch,
                "Blosc decompressor returned an unexpected byte count");
  }
  return output;
}

}  // namespace p0s
