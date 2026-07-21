#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace p0s {

using Json = nlohmann::json;

inline constexpr std::size_t kMaxJsonNestingDepth = 64;

[[nodiscard]] Json ParseStrictJson(std::string_view text);
[[nodiscard]] std::string DumpDeterministicJson(const Json& value);

void RequireExactObjectFields(
    const Json& object,
    std::initializer_list<std::string_view> required_fields);

[[nodiscard]] const std::string& RequireString(const Json& object,
                                               std::string_view field);
[[nodiscard]] std::uint64_t RequireUnsigned(const Json& object,
                                            std::string_view field);

}  // namespace p0s
