#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace p0s {

inline constexpr std::size_t kAes256KeySize = 32;
inline constexpr std::size_t kAeadMagicSize = 4;
inline constexpr std::size_t kAeadNonceSize = 12;
inline constexpr std::size_t kAeadTagSize = 16;
inline constexpr std::size_t kAeadHeaderSize =
    kAeadMagicSize + 2 + kAeadNonceSize;
inline constexpr std::uint8_t kAeadFormatVersion = 1;

struct AeadChunkContext {
  std::string dataset_id;
  std::string array_relative_path;
  std::string chunk_key;
  std::size_t expected_chunk_rank;

  AeadChunkContext(std::string dataset,
                   std::string array_path,
                   std::string chunk,
                   std::size_t rank);

  [[nodiscard]] std::vector<std::uint8_t> Aad() const;
};

class KeyProvider {
 public:
  virtual ~KeyProvider() = default;
  [[nodiscard]] virtual bool Lookup(
      std::uint8_t key_id,
      std::array<std::uint8_t, kAes256KeySize>& key) const = 0;
};

// Repository-safe test harness provider; production key storage is out of scope.
class TestKeyProvider final : public KeyProvider {
 public:
  void Add(std::uint8_t key_id,
           const std::array<std::uint8_t, kAes256KeySize>& key);
  [[nodiscard]] bool Lookup(
      std::uint8_t key_id,
      std::array<std::uint8_t, kAes256KeySize>& key) const override;

 private:
  std::map<std::uint8_t, std::array<std::uint8_t, kAes256KeySize>> keys_;
};

class NonceRegistry final {
 public:
  void Register(std::uint8_t key_id,
                const std::array<std::uint8_t, kAeadNonceSize>& nonce);

 private:
  std::mutex mutex_;
  std::set<std::pair<std::uint8_t,
                     std::array<std::uint8_t, kAeadNonceSize>>>
      key_nonces_;
};

[[nodiscard]] std::vector<std::uint8_t> EncryptAead(
    const std::vector<std::uint8_t>& plaintext,
    const AeadChunkContext& context,
    std::uint8_t key_id,
    const std::array<std::uint8_t, kAes256KeySize>& key,
    const std::array<std::uint8_t, kAeadNonceSize>& nonce,
    NonceRegistry& registry);

[[nodiscard]] std::vector<std::uint8_t> DecryptAead(
    const std::vector<std::uint8_t>& wire,
    const AeadChunkContext& context,
    const KeyProvider& keys);

}  // namespace p0s
