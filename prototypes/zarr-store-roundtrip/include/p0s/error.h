#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace p0s {

enum class ErrorCode {
  kInvalidArgument,
  kSizeOverflow,
  kUnsupportedTypesize,
  kCompressionFailed,
  kFrameInvalid,
  kDecodedSizeMismatch,
  kDecompressionFailed,
  kJsonParse,
  kJsonDuplicateKey,
  kJsonNonFinite,
  kJsonTypeMismatch,
  kJsonMissingField,
  kJsonUnexpectedField,
  kFilesystem,
  kStoreContract,
  kAeadContext,
  kAeadKey,
  kAeadUnknownKey,
  kAeadNonceReuse,
  kAeadFormat,
  kAeadVersion,
  kAeadAuthentication,
  kCryptoBackend,
};

class Error final : public std::runtime_error {
 public:
  Error(ErrorCode code, std::string message)
      : std::runtime_error(std::move(message)), code_(code) {}

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
};

}  // namespace p0s
