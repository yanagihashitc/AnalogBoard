#include "p0s/strict_json.h"

#include "p0s/error.h"

#include <cmath>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace p0s {
namespace {

class StrictSax final : public nlohmann::json_sax<Json> {
 public:
  bool null() override { return true; }
  bool boolean(bool) override { return true; }
  bool number_integer(number_integer_t) override { return true; }
  bool number_unsigned(number_unsigned_t) override { return true; }
  bool number_float(number_float_t value, const string_t&) override {
    if (!std::isfinite(value)) {
      failure_ = ErrorCode::kJsonNonFinite;
      message_ = "JSON contains a non-finite number";
      return false;
    }
    return true;
  }
  bool string(string_t&) override { return true; }
  bool binary(binary_t&) override { return true; }
  bool start_object(std::size_t) override {
    if (!StartContainer()) {
      return false;
    }
    object_keys_.emplace_back();
    return true;
  }
  bool key(string_t& value) override {
    if (object_keys_.empty()) {
      failure_ = ErrorCode::kJsonParse;
      message_ = "JSON key appeared outside an object";
      return false;
    }
    if (!object_keys_.back().insert(value).second) {
      failure_ = ErrorCode::kJsonDuplicateKey;
      message_ = "JSON contains a duplicate object key: " + value;
      return false;
    }
    return true;
  }
  bool end_object() override {
    if (object_keys_.empty()) {
      failure_ = ErrorCode::kJsonParse;
      message_ = "JSON object nesting is invalid";
      return false;
    }
    object_keys_.pop_back();
    return EndContainer();
  }
  bool start_array(std::size_t) override { return StartContainer(); }
  bool end_array() override { return EndContainer(); }
  bool parse_error(std::size_t,
                   const std::string&,
                   const nlohmann::detail::exception& error) override {
    failure_ = ErrorCode::kJsonParse;
    message_ = std::string("JSON parse failed: ") + error.what();
    return false;
  }

  [[nodiscard]] ErrorCode failure() const noexcept { return failure_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }

 private:
  bool StartContainer() {
    if (nesting_depth_ == kMaxJsonNestingDepth) {
      failure_ = ErrorCode::kJsonParse;
      message_ = "JSON nesting depth exceeds limit of " +
                 std::to_string(kMaxJsonNestingDepth);
      return false;
    }
    ++nesting_depth_;
    return true;
  }

  bool EndContainer() {
    if (nesting_depth_ == 0) {
      failure_ = ErrorCode::kJsonParse;
      message_ = "JSON container nesting is invalid";
      return false;
    }
    --nesting_depth_;
    return true;
  }

  std::vector<std::unordered_set<std::string>> object_keys_;
  std::size_t nesting_depth_ = 0;
  ErrorCode failure_ = ErrorCode::kJsonParse;
  std::string message_ = "JSON parse failed";
};

void RejectNonFinite(const Json& value) {
  if (value.is_number_float() && !std::isfinite(value.get<double>())) {
    throw Error(ErrorCode::kJsonNonFinite,
                "JSON contains a non-finite number");
  }
  if (value.is_array() || value.is_object()) {
    for (const auto& child : value) {
      RejectNonFinite(child);
    }
  }
}

const Json& RequireField(const Json& object, std::string_view field) {
  if (!object.is_object()) {
    throw Error(ErrorCode::kJsonTypeMismatch,
                "JSON contract root must be an object");
  }
  const auto iterator = object.find(std::string(field));
  if (iterator == object.end()) {
    throw Error(ErrorCode::kJsonMissingField,
                "JSON is missing required field: " + std::string(field));
  }
  return *iterator;
}

}  // namespace

Json ParseStrictJson(std::string_view text) {
  StrictSax sax;
  if (!Json::sax_parse(text.begin(), text.end(), &sax,
                       nlohmann::json::input_format_t::json, true)) {
    throw Error(sax.failure(), sax.message());
  }
  try {
    return Json::parse(text.begin(), text.end(), nullptr, true, false);
  } catch (const nlohmann::json::exception& error) {
    throw Error(ErrorCode::kJsonParse,
                std::string("JSON parse failed: ") + error.what());
  }
}

std::string DumpDeterministicJson(const Json& value) {
  RejectNonFinite(value);
  try {
    return value.dump(2, ' ', false, Json::error_handler_t::strict) + "\n";
  } catch (const nlohmann::json::exception& error) {
    throw Error(ErrorCode::kJsonParse,
                std::string("JSON serialization failed: ") + error.what());
  }
}

void RequireExactObjectFields(
    const Json& object,
    std::initializer_list<std::string_view> required_fields) {
  if (!object.is_object()) {
    throw Error(ErrorCode::kJsonTypeMismatch,
                "JSON contract root must be an object");
  }
  std::set<std::string> required;
  for (const std::string_view field : required_fields) {
    required.emplace(field);
  }
  for (const std::string& field : required) {
    if (!object.contains(field)) {
      throw Error(ErrorCode::kJsonMissingField,
                  "JSON is missing required field: " + field);
    }
  }
  for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
    if (required.find(iterator.key()) == required.end()) {
      throw Error(ErrorCode::kJsonUnexpectedField,
                  "JSON contains unexpected field: " + iterator.key());
    }
  }
}

const std::string& RequireString(const Json& object, std::string_view field) {
  const Json& value = RequireField(object, field);
  if (!value.is_string()) {
    throw Error(ErrorCode::kJsonTypeMismatch,
                "JSON field must be a string: " + std::string(field));
  }
  return value.get_ref<const std::string&>();
}

std::uint64_t RequireUnsigned(const Json& object, std::string_view field) {
  const Json& value = RequireField(object, field);
  if (!value.is_number_unsigned()) {
    throw Error(ErrorCode::kJsonTypeMismatch,
                "JSON field must be an unsigned integer: " +
                    std::string(field));
  }
  return value.get<std::uint64_t>();
}

}  // namespace p0s
