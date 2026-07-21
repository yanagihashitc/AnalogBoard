#include "p0s/aead_store.h"

#include "p0s/error.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <regex>
#include <utility>

namespace p0s {
namespace {

constexpr std::array<std::uint8_t, kAeadMagicSize> kAeadMagic{'G', 'C', 'S',
                                                              'A'};

struct AeadWireView {
  std::uint8_t key_id;
  const std::uint8_t* nonce;
  const std::uint8_t* ciphertext;
  std::size_t ciphertext_size;
  const std::uint8_t* tag;
};

class AlgorithmHandle final {
 public:
  AlgorithmHandle() {
    const NTSTATUS status = BCryptOpenAlgorithmProvider(
        &handle_, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
      throw Error(ErrorCode::kCryptoBackend,
                  "BCryptOpenAlgorithmProvider(AES) failed");
    }
    const NTSTATUS mode_status = BCryptSetProperty(
        handle_, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0);
    if (!BCRYPT_SUCCESS(mode_status)) {
      BCryptCloseAlgorithmProvider(handle_, 0);
      handle_ = nullptr;
      throw Error(ErrorCode::kCryptoBackend,
                  "BCryptSetProperty(GCM) failed");
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

class KeyHandle final {
 public:
  KeyHandle(BCRYPT_ALG_HANDLE algorithm,
            const std::array<std::uint8_t, kAes256KeySize>& key) {
    ULONG object_size = 0;
    ULONG copied = 0;
    const NTSTATUS property_status = BCryptGetProperty(
        algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &copied, 0);
    if (!BCRYPT_SUCCESS(property_status) || copied != sizeof(object_size) ||
        object_size == 0) {
      throw Error(ErrorCode::kCryptoBackend,
                  "BCrypt AES key-object size query failed");
    }
    key_object_.resize(object_size);
    const NTSTATUS key_status = BCryptGenerateSymmetricKey(
        algorithm, &handle_, key_object_.data(), object_size,
        const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
    if (!BCRYPT_SUCCESS(key_status)) {
      throw Error(ErrorCode::kCryptoBackend,
                  "BCryptGenerateSymmetricKey failed");
    }
  }

  ~KeyHandle() {
    if (handle_ != nullptr) {
      BCryptDestroyKey(handle_);
    }
    std::fill(key_object_.begin(), key_object_.end(), std::uint8_t{0});
  }

  KeyHandle(const KeyHandle&) = delete;
  KeyHandle& operator=(const KeyHandle&) = delete;
  [[nodiscard]] BCRYPT_KEY_HANDLE get() const noexcept { return handle_; }

 private:
  BCRYPT_KEY_HANDLE handle_ = nullptr;
  std::vector<std::uint8_t> key_object_;
};

ULONG CheckedUlong(std::size_t value, const char* field) {
  if (value > std::numeric_limits<ULONG>::max()) {
    throw Error(ErrorCode::kSizeOverflow,
                std::string("AES-GCM ") + field + " exceeds ULONG_MAX");
  }
  return static_cast<ULONG>(value);
}

std::vector<std::uint8_t> BuildAeadWire(
    std::uint8_t key_id,
    const std::array<std::uint8_t, kAeadNonceSize>& nonce,
    const std::vector<std::uint8_t>& ciphertext,
    const std::array<std::uint8_t, kAeadTagSize>& tag) {
  if (ciphertext.size() > std::numeric_limits<std::size_t>::max() -
                              kAeadHeaderSize - kAeadTagSize) {
    throw Error(ErrorCode::kSizeOverflow, "AES-GCM wire size overflow");
  }
  std::vector<std::uint8_t> wire;
  wire.reserve(kAeadHeaderSize + ciphertext.size() + kAeadTagSize);
  wire.insert(wire.end(), kAeadMagic.begin(), kAeadMagic.end());
  wire.push_back(kAeadFormatVersion);
  wire.push_back(key_id);
  wire.insert(wire.end(), nonce.begin(), nonce.end());
  wire.insert(wire.end(), ciphertext.begin(), ciphertext.end());
  wire.insert(wire.end(), tag.begin(), tag.end());
  return wire;
}

AeadWireView ParseAeadWire(const std::vector<std::uint8_t>& wire) {
  if (wire.size() < kAeadHeaderSize + kAeadTagSize) {
    throw Error(ErrorCode::kAeadFormat, "Encrypted chunk is truncated");
  }
  if (!std::equal(kAeadMagic.begin(), kAeadMagic.end(), wire.begin())) {
    throw Error(ErrorCode::kAeadFormat,
                "Encrypted chunk has invalid GCSA magic");
  }
  if (wire[kAeadMagicSize] != kAeadFormatVersion) {
    throw Error(ErrorCode::kAeadVersion,
                "Encrypted chunk has an unsupported format version");
  }
  const std::size_t ciphertext_size =
      wire.size() - kAeadHeaderSize - kAeadTagSize;
  const std::uint8_t* nonce = wire.data() + kAeadMagicSize + 2;
  const std::uint8_t* ciphertext = wire.data() + kAeadHeaderSize;
  return AeadWireView{wire[kAeadMagicSize + 1], nonce, ciphertext,
                      ciphertext_size, ciphertext + ciphertext_size};
}

bool IsValidUtf8(const std::string& value) {
  std::size_t index = 0;
  while (index < value.size()) {
    const auto byte = static_cast<std::uint8_t>(value[index]);
    std::size_t remaining = 0;
    std::uint32_t code_point = 0;
    if (byte <= 0x7F) {
      ++index;
      continue;
    }
    if ((byte & 0xE0) == 0xC0) {
      remaining = 1;
      code_point = byte & 0x1F;
      if (code_point < 2) {
        return false;
      }
    } else if ((byte & 0xF0) == 0xE0) {
      remaining = 2;
      code_point = byte & 0x0F;
    } else if ((byte & 0xF8) == 0xF0) {
      remaining = 3;
      code_point = byte & 0x07;
    } else {
      return false;
    }
    if (index + remaining >= value.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset <= remaining; ++offset) {
      const auto continuation =
          static_cast<std::uint8_t>(value[index + offset]);
      if ((continuation & 0xC0) != 0x80) {
        return false;
      }
      code_point = (code_point << 6) | (continuation & 0x3F);
    }
    if ((remaining == 2 && code_point < 0x800) ||
        (remaining == 3 && code_point < 0x10000) ||
        (code_point >= 0xD800 && code_point <= 0xDFFF) ||
        code_point > 0x10FFFF) {
      return false;
    }
    index += remaining + 1;
  }
  return true;
}

void ValidateContext(const AeadChunkContext& context) {
  if (context.dataset_id.empty() || context.dataset_id == "." ||
      context.dataset_id == ".." ||
      context.dataset_id.find('\0') != std::string::npos ||
      context.dataset_id.find('/') != std::string::npos ||
      context.dataset_id.find('\\') != std::string::npos ||
      !IsValidUtf8(context.dataset_id)) {
    throw Error(ErrorCode::kAeadContext,
                "dataset_id must be one non-empty valid UTF-8 path component");
  }
  static const std::regex array_path(
      R"((pulse_features|gmi_waveform|fl_waveform)(\.zarr|/partition_(0|[1-9][0-9]*)\.zarr))");
  if (!std::regex_match(context.array_relative_path, array_path)) {
    throw Error(ErrorCode::kAeadContext,
                "array_relative_path is not a canonical measurement array path");
  }
  static const std::regex chunk_key(R"((0|[1-9][0-9]*)(\.(0|[1-9][0-9]*))*)");
  if (!std::regex_match(context.chunk_key, chunk_key)) {
    throw Error(ErrorCode::kAeadContext,
                "chunk_key must contain canonical dot-separated indices");
  }
  if (context.expected_chunk_rank == 0 ||
      static_cast<std::size_t>(
          std::count(context.chunk_key.begin(), context.chunk_key.end(), '.')) +
              1 !=
          context.expected_chunk_rank) {
    throw Error(ErrorCode::kAeadContext,
                "chunk_key rank does not match the expected Zarr rank");
  }
}

std::vector<std::uint8_t> EncryptCng(
    const std::vector<std::uint8_t>& plaintext,
    const std::vector<std::uint8_t>& aad,
    const std::array<std::uint8_t, kAes256KeySize>& key,
    const std::array<std::uint8_t, kAeadNonceSize>& nonce,
    std::array<std::uint8_t, kAeadTagSize>& tag) {
  AlgorithmHandle algorithm;
  KeyHandle key_handle(algorithm.get(), key);
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
  BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = const_cast<PUCHAR>(nonce.data());
  info.cbNonce = CheckedUlong(nonce.size(), "nonce size");
  info.pbAuthData = const_cast<PUCHAR>(aad.data());
  info.cbAuthData = CheckedUlong(aad.size(), "AAD size");
  info.pbTag = tag.data();
  info.cbTag = CheckedUlong(tag.size(), "tag size");

  const ULONG plaintext_size = CheckedUlong(plaintext.size(), "plaintext size");
  std::vector<std::uint8_t> ciphertext(plaintext.size());
  ULONG written = 0;
  const NTSTATUS status = BCryptEncrypt(
      key_handle.get(), const_cast<PUCHAR>(plaintext.data()),
      plaintext_size, &info, nullptr, 0,
      ciphertext.data(), CheckedUlong(ciphertext.size(), "ciphertext capacity"),
      &written, 0);
  if (!BCRYPT_SUCCESS(status) || written != ciphertext.size()) {
    throw Error(ErrorCode::kCryptoBackend, "BCryptEncrypt(AES-256-GCM) failed");
  }
  return ciphertext;
}

std::vector<std::uint8_t> DecryptCng(
    const std::uint8_t* ciphertext,
    std::size_t ciphertext_size,
    const std::vector<std::uint8_t>& aad,
    const std::array<std::uint8_t, kAes256KeySize>& key,
    const std::uint8_t* nonce,
    const std::uint8_t* tag) {
  AlgorithmHandle algorithm;
  KeyHandle key_handle(algorithm.get(), key);
  std::array<std::uint8_t, kAeadTagSize> mutable_tag{};
  std::copy_n(tag, mutable_tag.size(), mutable_tag.begin());
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
  BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = const_cast<PUCHAR>(nonce);
  info.cbNonce = CheckedUlong(kAeadNonceSize, "nonce size");
  info.pbAuthData = const_cast<PUCHAR>(aad.data());
  info.cbAuthData = CheckedUlong(aad.size(), "AAD size");
  info.pbTag = mutable_tag.data();
  info.cbTag = CheckedUlong(mutable_tag.size(), "tag size");

  const ULONG checked_ciphertext_size =
      CheckedUlong(ciphertext_size, "ciphertext size");
  std::vector<std::uint8_t> plaintext(ciphertext_size);
  ULONG written = 0;
  const NTSTATUS status = BCryptDecrypt(
      key_handle.get(), const_cast<PUCHAR>(ciphertext),
      checked_ciphertext_size, &info, nullptr, 0,
      plaintext.data(), CheckedUlong(plaintext.size(), "plaintext capacity"),
      &written, 0);
  if (!BCRYPT_SUCCESS(status) || written != plaintext.size()) {
    std::fill(plaintext.begin(), plaintext.end(), std::uint8_t{0});
    throw Error(ErrorCode::kAeadAuthentication,
                "Encrypted chunk authentication failed");
  }
  return plaintext;
}

}  // namespace

AeadChunkContext::AeadChunkContext(std::string dataset,
                                   std::string array_path,
                                   std::string chunk,
                                   std::size_t rank)
    : dataset_id(std::move(dataset)),
      array_relative_path(std::move(array_path)),
      chunk_key(std::move(chunk)),
      expected_chunk_rank(rank) {
  ValidateContext(*this);
}

std::vector<std::uint8_t> AeadChunkContext::Aad() const {
  ValidateContext(*this);
  std::size_t aad_size = dataset_id.size();
  const auto checked_add = [&aad_size](std::size_t value) {
    if (value > std::numeric_limits<std::size_t>::max() - aad_size) {
      throw Error(ErrorCode::kSizeOverflow, "AES-GCM AAD size overflow");
    }
    aad_size += value;
  };
  checked_add(1);
  checked_add(array_relative_path.size());
  checked_add(1);
  checked_add(chunk_key.size());
  std::vector<std::uint8_t> aad;
  aad.reserve(aad_size);
  aad.insert(aad.end(), dataset_id.begin(), dataset_id.end());
  aad.push_back(0);
  aad.insert(aad.end(), array_relative_path.begin(), array_relative_path.end());
  aad.push_back(0);
  aad.insert(aad.end(), chunk_key.begin(), chunk_key.end());
  return aad;
}

void TestKeyProvider::Add(
    std::uint8_t key_id,
    const std::array<std::uint8_t, kAes256KeySize>& key) {
  if (key_id == 0) {
    throw Error(ErrorCode::kAeadKey, "AEAD key_id must be in the range 1..255");
  }
  if (keys_.find(key_id) != keys_.end()) {
    throw Error(ErrorCode::kAeadKey, "AEAD key_id is already registered");
  }
  for (const auto& entry : keys_) {
    if (entry.second == key) {
      throw Error(ErrorCode::kAeadKey,
                  "AEAD key registry reuses key material under another key_id");
    }
  }
  keys_.emplace(key_id, key);
}

bool TestKeyProvider::Lookup(
    std::uint8_t key_id,
    std::array<std::uint8_t, kAes256KeySize>& key) const {
  const auto iterator = keys_.find(key_id);
  if (iterator == keys_.end()) {
    return false;
  }
  key = iterator->second;
  return true;
}

void NonceRegistry::Register(
    std::uint8_t key_id,
    const std::array<std::uint8_t, kAeadNonceSize>& nonce) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!key_nonces_.emplace(key_id, nonce).second) {
    throw Error(ErrorCode::kAeadNonceReuse,
                "AES-GCM key_id/nonce reuse was rejected");
  }
}

std::vector<std::uint8_t> EncryptAead(
    const std::vector<std::uint8_t>& plaintext,
    const AeadChunkContext& context,
    std::uint8_t key_id,
    const std::array<std::uint8_t, kAes256KeySize>& key,
    const std::array<std::uint8_t, kAeadNonceSize>& nonce,
    NonceRegistry& registry) {
  if (key_id == 0) {
    throw Error(ErrorCode::kAeadKey, "AEAD key_id must be in the range 1..255");
  }
  ValidateContext(context);
  registry.Register(key_id, nonce);
  const auto aad = context.Aad();
  std::array<std::uint8_t, kAeadTagSize> tag{};
  const auto ciphertext = EncryptCng(plaintext, aad, key, nonce, tag);
  return BuildAeadWire(key_id, nonce, ciphertext, tag);
}

std::vector<std::uint8_t> DecryptAead(const std::vector<std::uint8_t>& wire,
                                      const AeadChunkContext& context,
  const KeyProvider& keys) {
  ValidateContext(context);
  const AeadWireView parsed = ParseAeadWire(wire);
  std::array<std::uint8_t, kAes256KeySize> key{};
  if (parsed.key_id == 0 || !keys.Lookup(parsed.key_id, key)) {
    throw Error(ErrorCode::kAeadUnknownKey,
                "Encrypted chunk references an unknown key_id");
  }
  return DecryptCng(parsed.ciphertext, parsed.ciphertext_size, context.Aad(), key,
                    parsed.nonce, parsed.tag);
}

}  // namespace p0s
