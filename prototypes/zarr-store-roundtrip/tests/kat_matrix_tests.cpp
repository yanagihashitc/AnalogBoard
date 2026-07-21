#include "p0s/aead_store.h"
#include "p0s/blosc_adapter.h"
#include "p0s/error.h"
#include "p0s/strict_json.h"

#include <blosc.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int checks = 0;
int kat_checks = 0;
int boundary_cases = 0;
int negative_cases = 0;

void Require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void RequireKat(bool condition, const std::string& message) {
  ++kat_checks;
  Require(condition, message);
}

template <typename Action>
void RequireError(p0s::ErrorCode code, const char* expected_message,
                  Action action, const char* label) {
  ++checks;
  ++negative_cases;
  try {
    action();
  } catch (const p0s::Error& error) {
    if (error.code() != code) {
      throw std::runtime_error(std::string(label) +
                               ": unexpected error code");
    }
    if (error.what() != std::string(expected_message)) {
      throw std::runtime_error(std::string(label) +
                               ": unstable error message");
    }
    return;
  }
  throw std::runtime_error(std::string(label) + ": no error");
}

void RequireDecryptErrorNoOutput(
    p0s::ErrorCode code, const char* expected_message,
    const std::vector<std::uint8_t>& wire,
    const p0s::AeadChunkContext& context, const p0s::KeyProvider& keys,
    const char* label) {
  ++checks;
  ++negative_cases;
  const std::vector<std::uint8_t> sentinel{0xA5, 0x5A};
  auto output = sentinel;
  try {
    output = p0s::DecryptAead(wire, context, keys);
  } catch (const p0s::Error& error) {
    if (error.code() != code) {
      throw std::runtime_error(std::string(label) +
                               ": unexpected error code");
    }
    if (error.what() != std::string(expected_message)) {
      throw std::runtime_error(std::string(label) +
                               ": unstable error message");
    }
    if (output != sentinel) {
      throw std::runtime_error(std::string(label) +
                               ": exposed partial plaintext");
    }
    return;
  }
  throw std::runtime_error(std::string(label) + ": no error");
}

std::string ReadFile(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot open the accepted gcsa KAT");
  }
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
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
  throw std::runtime_error("KAT contains non-hexadecimal data");
}

std::vector<std::uint8_t> DecodeHex(const std::string& value) {
  if (value.size() % 2 != 0) {
    throw std::runtime_error("KAT hex field has an odd length");
  }
  std::vector<std::uint8_t> decoded;
  decoded.reserve(value.size() / 2);
  for (std::size_t index = 0; index < value.size(); index += 2) {
    decoded.push_back(static_cast<std::uint8_t>(
        (HexNibble(value[index]) << 4) | HexNibble(value[index + 1])));
  }
  return decoded;
}

template <std::size_t Size>
std::array<std::uint8_t, Size> RequireArray(
    const std::vector<std::uint8_t>& value, const char* label) {
  if (value.size() != Size) {
    throw std::runtime_error(std::string(label) + " has an invalid size");
  }
  std::array<std::uint8_t, Size> result{};
  std::copy(value.begin(), value.end(), result.begin());
  return result;
}

std::vector<std::uint8_t> DecodedKatValues(const p0s::Json& inner) {
  const auto& values = inner.at("decoded_values");
  RequireKat(values.is_array() && values.size() == 12,
             "KAT decoded_values shape changed");
  std::vector<std::uint8_t> bytes;
  bytes.reserve(values.size() * sizeof(std::uint16_t));
  for (const auto& value : values) {
    RequireKat(value.is_number_unsigned(),
               "KAT decoded value is not unsigned");
    const auto word = value.get<std::uint64_t>();
    RequireKat(word <= std::numeric_limits<std::uint16_t>::max(),
               "KAT decoded value exceeds uint16");
    bytes.push_back(static_cast<std::uint8_t>(word & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((word >> 8) & 0xFF));
  }
  return bytes;
}

void TestAcceptedKat(const std::string& kat_path) {
  const auto kat = p0s::ParseStrictJson(ReadFile(kat_path));
  p0s::RequireExactObjectFields(
      kat, {"schema", "vector_id", "format_version", "key_id", "key_hex",
            "dataset_id", "array_rel_path", "chunk_key", "chunk_rank",
            "aad_hex", "nonce_hex", "plaintext_hex", "ciphertext_hex",
            "tag_hex", "wire_hex", "inner", "security_note"});
  RequireKat(p0s::RequireString(kat, "schema") == "gcsa.aead-kat.v1",
             "KAT schema changed");
  RequireKat(p0s::RequireUnsigned(kat, "format_version") ==
                 p0s::kAeadFormatVersion,
             "KAT format version changed");

  const auto& inner = kat.at("inner");
  p0s::RequireExactObjectFields(
      inner, {"description", "dtype", "shape", "compressor",
              "decoded_values"});
  RequireKat(p0s::RequireString(inner, "dtype") == "<u2",
             "KAT dtype changed");
  const auto& shape = inner.at("shape");
  RequireKat(shape == p0s::Json::array({3, 4}), "KAT shape changed");
  const auto& compressor = inner.at("compressor");
  p0s::RequireExactObjectFields(
      compressor, {"id", "cname", "clevel", "shuffle", "blocksize"});
  RequireKat(p0s::RequireString(compressor, "id") == "blosc",
             "KAT compressor id changed");
  RequireKat(p0s::RequireString(compressor, "cname") == "lz4",
             "KAT compressor name changed");
  RequireKat(p0s::RequireUnsigned(compressor, "clevel") == 5,
             "KAT compression level changed");
  RequireKat(p0s::RequireUnsigned(compressor, "shuffle") == 1,
             "KAT shuffle changed");
  RequireKat(p0s::RequireUnsigned(compressor, "blocksize") == 0,
             "KAT blocksize changed");

  const auto raw = DecodedKatValues(inner);
  const auto expected_frame =
      DecodeHex(p0s::RequireString(kat, "plaintext_hex"));
  const auto frame = p0s::CompressBlosc(raw.data(), raw.size(), 2);
  RequireKat(frame == expected_frame,
             "C++ Blosc frame differs from the accepted gcsa KAT");

  const auto key_id_value = p0s::RequireUnsigned(kat, "key_id");
  RequireKat(key_id_value > 0 && key_id_value <= 255,
             "KAT key_id is outside the wire range");
  const auto key_id = static_cast<std::uint8_t>(key_id_value);
  const auto key = RequireArray<p0s::kAes256KeySize>(
      DecodeHex(p0s::RequireString(kat, "key_hex")), "KAT key");
  const auto nonce = RequireArray<p0s::kAeadNonceSize>(
      DecodeHex(p0s::RequireString(kat, "nonce_hex")), "KAT nonce");
  const auto rank = p0s::RequireUnsigned(kat, "chunk_rank");
  const p0s::AeadChunkContext context(
      p0s::RequireString(kat, "dataset_id"),
      p0s::RequireString(kat, "array_rel_path"),
      p0s::RequireString(kat, "chunk_key"), static_cast<std::size_t>(rank));
  RequireKat(context.Aad() == DecodeHex(p0s::RequireString(kat, "aad_hex")),
             "C++ AAD differs from the accepted gcsa KAT");

  p0s::NonceRegistry nonces;
  const auto wire =
      p0s::EncryptAead(frame, context, key_id, key, nonce, nonces);
  const auto expected_ciphertext =
      DecodeHex(p0s::RequireString(kat, "ciphertext_hex"));
  const auto expected_tag = DecodeHex(p0s::RequireString(kat, "tag_hex"));
  const auto expected_wire = DecodeHex(p0s::RequireString(kat, "wire_hex"));
  RequireKat(wire == expected_wire,
             "C++ AES-GCM wire differs from the accepted gcsa KAT");
  RequireKat(std::vector<std::uint8_t>(
                 wire.begin() + static_cast<std::ptrdiff_t>(p0s::kAeadHeaderSize),
                 wire.end() - static_cast<std::ptrdiff_t>(p0s::kAeadTagSize)) ==
                 expected_ciphertext,
             "C++ AES-GCM ciphertext differs from the accepted gcsa KAT");
  RequireKat(std::vector<std::uint8_t>(
                 wire.end() - static_cast<std::ptrdiff_t>(p0s::kAeadTagSize),
                 wire.end()) == expected_tag,
             "C++ AES-GCM tag differs from the accepted gcsa KAT");

  p0s::TestKeyProvider keys;
  keys.Add(key_id, key);
  const auto decrypted_frame = p0s::DecryptAead(wire, context, keys);
  RequireKat(decrypted_frame == frame,
             "C++ KAT authentication/decryption changed the Blosc frame");
  RequireKat(p0s::DecompressBlosc(decrypted_frame, raw.size()) == raw,
             "C++ KAT decompression changed the input bits");
}

enum class Pattern { kZero, kRepetitive, kIncompressible };

std::vector<std::uint8_t> MakePattern(std::size_t size, Pattern pattern) {
  std::vector<std::uint8_t> result(size);
  if (pattern == Pattern::kZero) {
    return result;
  }
  if (pattern == Pattern::kRepetitive) {
    for (std::size_t index = 0; index < size; ++index) {
      result[index] = static_cast<std::uint8_t>(0x30 + (index % 7));
    }
    return result;
  }
  std::uint32_t state = 0x6D2B79F5U;
  for (auto& byte : result) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    byte = static_cast<std::uint8_t>(state & 0xFF);
  }
  return result;
}

void RequireBloscRoundTrip(const std::vector<std::uint8_t>& input,
                           std::size_t typesize, const char* label) {
  ++boundary_cases;
  const auto frame = p0s::CompressBlosc(input.data(), input.size(), typesize);
  Require(p0s::DecompressBlosc(frame, input.size()) == input,
          std::string(label) + " changed input bits");
}

void TestBoundaryMatrix() {
  for (const std::size_t typesize : {std::size_t{2}, std::size_t{8}}) {
    ++boundary_cases;
    const auto empty_frame = p0s::CompressBlosc(nullptr, 0, typesize);
    Require(!empty_frame.empty(),
            "zero-byte source did not produce a valid Blosc frame");
    RequireError(p0s::ErrorCode::kDecompressionFailed,
                 "Blosc lz4 decompression failed",
                 [&] {
                   static_cast<void>(
                       p0s::DecompressBlosc(empty_frame, 0));
                 },
                 "zero-byte decoded result");
    for (const std::size_t size : {std::size_t{1}, std::size_t{15},
                                   std::size_t{16}, std::size_t{17}}) {
      for (const Pattern pattern : {Pattern::kZero, Pattern::kRepetitive,
                                    Pattern::kIncompressible}) {
        RequireBloscRoundTrip(MakePattern(size, pattern), typesize,
                              "small boundary case");
      }
    }
  }

  std::vector<std::uint8_t> floating_bits;
  for (const std::uint64_t bits : {
           UINT64_C(0x7FF8000000000001), UINT64_C(0x7FF0000000000000),
           UINT64_C(0xFFF0000000000000), UINT64_C(0x8000000000000000)}) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
      floating_bits.push_back(static_cast<std::uint8_t>(bits >> shift));
    }
  }
  RequireBloscRoundTrip(floating_bits, 8,
                        "float64 NaN/positive-infinity/negative-infinity");

  RequireBloscRoundTrip(MakePattern(5 * 24 * 8, Pattern::kRepetitive), 8,
                        "partial feature chunk");
  RequireBloscRoundTrip(MakePattern(37 * 5 * 2, Pattern::kZero), 2,
                        "partial GMI chunk");
  RequireBloscRoundTrip(MakePattern(31 * 8 * 2, Pattern::kIncompressible), 2,
                        "partial FL chunk");

  RequireBloscRoundTrip(MakePattern(1'920'000, Pattern::kRepetitive), 8,
                        "full feature chunk");
  RequireBloscRoundTrip(MakePattern(48'000'000, Pattern::kZero), 2,
                        "full GMI chunk");
  RequireBloscRoundTrip(MakePattern(76'800'000, Pattern::kIncompressible), 2,
                        "full FL chunk");
}

void TestBloscNegatives() {
  const auto input = MakePattern(64, Pattern::kRepetitive);
  const auto frame = p0s::CompressBlosc(input.data(), input.size(), 2);

  const std::vector<std::uint8_t> truncated_header(
      frame.begin(),
      frame.begin() + static_cast<std::ptrdiff_t>(BLOSC_MIN_HEADER_LENGTH - 1));
  RequireError(p0s::ErrorCode::kFrameInvalid,
               "Blosc frame is shorter than its minimum header",
               [&] {
                 static_cast<void>(
                     p0s::DecompressBlosc(truncated_header, input.size()));
               },
               "truncated Blosc header");

  auto corrupted = frame;
  corrupted[12] = 0xFF;
  corrupted[13] = 0xFF;
  corrupted[14] = 0xFF;
  corrupted[15] = 0x7F;
  RequireError(p0s::ErrorCode::kFrameInvalid,
               "Blosc frame validation failed",
               [&] {
                 static_cast<void>(
                     p0s::DecompressBlosc(corrupted, input.size()));
               },
               "corrupted Blosc frame");

  RequireError(
      p0s::ErrorCode::kDecodedSizeMismatch,
      "Blosc decoded size does not match the expected Zarr chunk size",
      [&] {
        static_cast<void>(p0s::DecompressBlosc(frame, input.size() + 1));
      },
      "decoded-size mismatch");
}

void TestAeadNegatives() {
  const p0s::AeadChunkContext context(
      "ds_negative", "fl_waveform/partition_1.zarr", "0.0", 2);
  std::array<std::uint8_t, p0s::kAes256KeySize> key{};
  for (std::size_t index = 0; index < key.size(); ++index) {
    key[index] = static_cast<std::uint8_t>(index + 1);
  }
  std::array<std::uint8_t, p0s::kAeadNonceSize> nonce{};
  for (std::size_t index = 0; index < nonce.size(); ++index) {
    nonce[index] = static_cast<std::uint8_t>(0xA0 + index);
  }
  const auto plaintext = MakePattern(64, Pattern::kIncompressible);
  p0s::NonceRegistry nonces;
  const auto wire =
      p0s::EncryptAead(plaintext, context, 3, key, nonce, nonces);

  auto wrong_key = key;
  wrong_key[0] ^= 0x80;
  p0s::TestKeyProvider wrong_keys;
  wrong_keys.Add(3, wrong_key);
  RequireDecryptErrorNoOutput(
      p0s::ErrorCode::kAeadAuthentication,
      "Encrypted chunk authentication failed", wire, context, wrong_keys,
      "wrong AES key");

  p0s::TestKeyProvider keys;
  keys.Add(3, key);
  auto tag_mutation = wire;
  tag_mutation.back() ^= 1;
  RequireDecryptErrorNoOutput(
      p0s::ErrorCode::kAeadAuthentication,
      "Encrypted chunk authentication failed", tag_mutation, context, keys,
      "AES-GCM tag mutation");

  auto ciphertext_mutation = wire;
  ciphertext_mutation[p0s::kAeadHeaderSize] ^= 1;
  RequireDecryptErrorNoOutput(
      p0s::ErrorCode::kAeadAuthentication,
      "Encrypted chunk authentication failed", ciphertext_mutation, context,
      keys, "AES-GCM ciphertext mutation");

  auto truncated = wire;
  truncated.resize(p0s::kAeadHeaderSize + p0s::kAeadTagSize - 1);
  RequireDecryptErrorNoOutput(
      p0s::ErrorCode::kAeadFormat, "Encrypted chunk is truncated", truncated,
      context, keys, "encrypted chunk structural truncation");

  auto authenticated_truncation = wire;
  authenticated_truncation.pop_back();
  RequireDecryptErrorNoOutput(
      p0s::ErrorCode::kAeadAuthentication,
      "Encrypted chunk authentication failed", authenticated_truncation,
      context, keys, "encrypted chunk authenticated truncation");

  const p0s::AeadChunkContext swapped_context(
      "ds_negative", "fl_waveform/partition_1.zarr", "1.0", 2);
  RequireDecryptErrorNoOutput(
      p0s::ErrorCode::kAeadAuthentication,
      "Encrypted chunk authentication failed", wire, swapped_context, keys,
      "chunk swap and AAD mismatch");

  p0s::TestKeyProvider no_keys;
  RequireDecryptErrorNoOutput(
      p0s::ErrorCode::kAeadUnknownKey,
      "Encrypted chunk references an unknown key_id", wire, context, no_keys,
      "unknown key_id");

  RequireError(p0s::ErrorCode::kAeadNonceReuse,
               "AES-GCM key_id/nonce reuse was rejected",
               [&] {
                 static_cast<void>(
                     p0s::EncryptAead(plaintext, context, 3, key, nonce, nonces));
               },
               "nonce reuse");
}

}  // namespace

int main(int argument_count, char** arguments) {
  try {
    if (argument_count != 2) {
      throw std::runtime_error("accepted gcsa KAT path argument is required");
    }
    TestAcceptedKat(arguments[1]);
    TestBoundaryMatrix();
    TestBloscNegatives();
    TestAeadNegatives();
    std::cout << "kat_matrix_checks=" << checks << " kat_checks=" << kat_checks
              << " boundary_cases=" << boundary_cases
              << " negative_cases=" << negative_cases
              << " status=pass\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "kat_matrix_checks=" << checks << " kat_checks=" << kat_checks
              << " boundary_cases=" << boundary_cases
              << " negative_cases=" << negative_cases
              << " status=fail error=" << error.what() << "\n";
    return 1;
  }
}
