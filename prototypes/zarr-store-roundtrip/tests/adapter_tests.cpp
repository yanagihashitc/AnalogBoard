#include "p0s/aead_store.h"
#include "p0s/blosc_adapter.h"
#include "p0s/error.h"
#include "p0s/strict_json.h"

#include <blosc.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int checks = 0;

void Require(bool condition, const char* message) {
  ++checks;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Action>
void RequireError(p0s::ErrorCode code,
                  Action action,
                  const char* message,
                  const char* expected_error_message = nullptr) {
  ++checks;
  try {
    action();
  } catch (const p0s::Error& error) {
    if (error.code() != code) {
      throw std::runtime_error(std::string(message) +
                               ": unexpected error code");
    }
    if (expected_error_message != nullptr &&
        std::string(error.what()) != expected_error_message) {
      throw std::runtime_error(std::string(message) +
                               ": unexpected error message");
    }
    return;
  }
  throw std::runtime_error(std::string(message) + ": no error");
}

std::string NestedArrays(std::size_t depth) {
  return std::string(depth, '[') + "0" + std::string(depth, ']');
}

void TestDependencyIdentity() {
  Require(p0s::kBloscShuffle == BLOSC_SHUFFLE,
          "adapter shuffle does not match BLOSC_SHUFFLE");
  Require(std::string(blosc_get_version_string()) == "1.21.6",
          "unexpected c-blosc version");
  const std::string compressors = blosc_list_compressors();
  Require(compressors.find("lz4") != std::string::npos,
          "lz4 is missing from the compressor list");
  Require(compressors.find("zlib") == std::string::npos,
          "zlib must be disabled in the accepted profile");
  Require(compressors.find("zstd") == std::string::npos,
          "zstd must be disabled in the accepted profile");

  char* library = nullptr;
  char* version = nullptr;
  const int result = blosc_get_complib_info("lz4", &library, &version);
  Require(result >= 0, "cannot query LZ4 library information");
  Require(library != nullptr && std::string(library) == "LZ4",
          "unexpected LZ4 library identity");
  Require(version != nullptr && std::string(version) == "1.9.4",
          "unexpected internal LZ4 version");
  std::free(library);
  std::free(version);
}

void TestBloscRoundTrip() {
  std::vector<std::uint16_t> words{0, 1, 2, 3, 4, 5, 6, 7};
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(words.data());
  const std::size_t size = words.size() * sizeof(words.front());
  const auto frame = p0s::CompressBlosc(bytes, size, 2);
  const auto restored = p0s::DecompressBlosc(frame, size);
  Require(restored == std::vector<std::uint8_t>(bytes, bytes + size),
          "uint16 Blosc roundtrip changed bytes");

  std::vector<double> features{0.0, -0.0, 1.25, -8.5};
  const auto* feature_bytes =
      reinterpret_cast<const std::uint8_t*>(features.data());
  const std::size_t feature_size = features.size() * sizeof(features.front());
  const auto feature_frame = p0s::CompressBlosc(feature_bytes, feature_size, 8);
  Require(p0s::DecompressBlosc(feature_frame, feature_size) ==
              std::vector<std::uint8_t>(feature_bytes,
                                        feature_bytes + feature_size),
          "float64 Blosc roundtrip changed bits");

  RequireError(p0s::ErrorCode::kUnsupportedTypesize,
               [&] { static_cast<void>(p0s::CompressBlosc(bytes, size, 4)); },
               "unsupported typesize must fail");
  RequireError(p0s::ErrorCode::kDecodedSizeMismatch,
               [&] { static_cast<void>(p0s::DecompressBlosc(frame, size + 1)); },
               "decoded-size mismatch must fail");
}

void TestStrictJson() {
  const auto parsed = p0s::ParseStrictJson(R"({"b":2,"a":"value"})");
  Require(p0s::DumpDeterministicJson(parsed) ==
              "{\n  \"a\": \"value\",\n  \"b\": 2\n}\n",
          "JSON serialization is not deterministic");
  p0s::RequireExactObjectFields(parsed, {"a", "b"});
  Require(p0s::RequireString(parsed, "a") == "value",
          "strict string field changed");
  Require(p0s::RequireUnsigned(parsed, "b") == 2,
          "strict unsigned field changed");

  RequireError(p0s::ErrorCode::kJsonDuplicateKey,
               [] { static_cast<void>(p0s::ParseStrictJson(R"({"a":1,"a":2})")); },
               "duplicate JSON key must fail");
  RequireError(p0s::ErrorCode::kJsonParse,
               [] { static_cast<void>(p0s::ParseStrictJson("{\"a\":NaN}")); },
               "non-finite JSON token must fail");
  RequireError(p0s::ErrorCode::kJsonNonFinite,
               [] {
                 static_cast<void>(p0s::DumpDeterministicJson(
                     p0s::Json(std::numeric_limits<double>::infinity())));
               },
               "non-finite JSON value must fail serialization");
  RequireError(p0s::ErrorCode::kJsonParse,
               [] {
                 const std::string invalid{"\"\xFF\"", 3};
                 static_cast<void>(p0s::ParseStrictJson(invalid));
               },
               "invalid UTF-8 must fail");
  RequireError(p0s::ErrorCode::kJsonTypeMismatch,
               [&] { static_cast<void>(p0s::RequireString(parsed, "b")); },
               "JSON type mismatch must fail");
  RequireError(p0s::ErrorCode::kJsonMissingField,
               [&] { static_cast<void>(p0s::RequireString(parsed, "missing")); },
               "missing JSON field must fail");
  RequireError(p0s::ErrorCode::kJsonUnexpectedField,
               [&] { p0s::RequireExactObjectFields(parsed, {"a"}); },
               "unexpected JSON field must fail");
  RequireError(p0s::ErrorCode::kJsonParse,
               [] { static_cast<void>(p0s::ParseStrictJson("{} trailing")); },
               "trailing JSON data must fail");

  // Given: JSON nested exactly to the strict contract boundary.
  // When: The document is parsed.
  const auto boundary =
      p0s::ParseStrictJson(NestedArrays(p0s::kMaxJsonNestingDepth));
  // Then: The boundary document remains accepted.
  Require(boundary.is_array(), "maximum JSON nesting depth must succeed");

  // Given: JSON nested one level beyond the strict contract boundary.
  const std::string too_deep =
      NestedArrays(p0s::kMaxJsonNestingDepth + 1);
  // When: The document is parsed.
  // Then: Parsing fails with the stable contract error code and message.
  RequireError(p0s::ErrorCode::kJsonParse,
               [&] { static_cast<void>(p0s::ParseStrictJson(too_deep)); },
               "excessive JSON nesting must fail cleanly",
               "JSON nesting depth exceeds limit of 64");
}

void TestAeadRoundTrip() {
  p0s::AeadChunkContext context{"ds_test", "pulse_features/partition_0.zarr",
                                "0.0", 2};
  p0s::TestKeyProvider keys;
  std::array<std::uint8_t, p0s::kAes256KeySize> key{};
  key.fill(0x42);
  keys.Add(7, key);
  std::array<std::uint8_t, p0s::kAeadNonceSize> nonce{};
  for (std::size_t index = 0; index < nonce.size(); ++index) {
    nonce[index] = static_cast<std::uint8_t>(index);
  }
  p0s::NonceRegistry nonces;
  const std::vector<std::uint8_t> plaintext{0, 1, 2, 3, 0xFF};
  const auto wire = p0s::EncryptAead(plaintext, context, 7, key, nonce, nonces);
  Require(p0s::DecryptAead(wire, context, keys) == plaintext,
          "AES-GCM roundtrip changed bytes");
  Require(wire.size() == p0s::kAeadHeaderSize + plaintext.size() +
                             p0s::kAeadTagSize,
          "AES-GCM wire size is invalid");

  RequireError(p0s::ErrorCode::kAeadNonceReuse,
               [&] {
                 static_cast<void>(
                     p0s::EncryptAead(plaintext, context, 7, key, nonce, nonces));
               },
               "nonce reuse must fail");
  auto rotated_key = key;
  rotated_key.fill(0x43);
  Require(!p0s::EncryptAead(plaintext, context, 8, rotated_key, nonce, nonces)
               .empty(),
          "nonce uniqueness must be scoped to key_id");
  p0s::TestKeyProvider empty_keys;
  RequireError(p0s::ErrorCode::kAeadUnknownKey,
               [&] { static_cast<void>(p0s::DecryptAead(wire, context, empty_keys)); },
               "unknown key ID must fail");

  auto mutated = wire;
  mutated.back() ^= 1;
  RequireError(p0s::ErrorCode::kAeadAuthentication,
               [&] { static_cast<void>(p0s::DecryptAead(mutated, context, keys)); },
               "tag mutation must fail authentication");
}

}  // namespace

int main() {
  try {
    TestDependencyIdentity();
    TestBloscRoundTrip();
    TestStrictJson();
    TestAeadRoundTrip();
    std::cout << "adapter_checks=" << checks << " status=pass\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "adapter_checks=" << checks << " status=fail error="
              << error.what() << "\n";
    return 1;
  }
}
