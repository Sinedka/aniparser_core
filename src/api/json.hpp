#pragma once

#include <models.hpp>

#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace yani {

// ============================================================
// null values
// ============================================================

template <typename T> struct yani_null_value {
  static T get() { return T{}; }
};

template <> struct yani_null_value<std::string> {
  static std::string get() { return ""; }
};

template <> struct yani_null_value<std::int64_t> {
  static std::int64_t get() { return std::numeric_limits<std::int64_t>::min(); }
};

template <> struct yani_null_value<double> {
  static double get() { return -1.0; }
};

template <> struct yani_null_value<bool> {
  static bool get() { return true; }
};

template <typename T> struct yani_null_value<std::vector<T>> {
  static std::vector<T> get() { return {}; }
};

// ============================================================
// from_json
// ============================================================

template <typename T> T yani_get(const nlohmann::json &j, const char* name) {
  if (!j.contains(name) || j.at(name).is_null())
    return yani_null_value<T>::get();

  const auto &value = j.at(name);
  if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
    if (value.is_number()) {
      const auto number = value.get<long double>();
      if (!std::isfinite(number) ||
          number < static_cast<long double>(std::numeric_limits<T>::lowest()) ||
          number > static_cast<long double>(std::numeric_limits<T>::max()))
        throw std::runtime_error("API numeric field out of range: " +
                                 std::string(name));
      if constexpr (std::is_integral_v<T>) {
        // Use exclusive power-of-two bounds too: MSVC long double equals
        // double and cannot exactly represent INT64_MAX.
        const auto bound = std::ldexp(1.0L, std::numeric_limits<T>::digits);
        if (number >= bound || std::trunc(number) != number)
          throw std::runtime_error("API field must be an in-range integer: " +
                                   std::string(name));
      }
      return static_cast<T>(number);
    }
    if (value.is_string()) {
      const auto text = value.get<std::string>();
      T result{};
      const auto [end, error] =
          std::from_chars(text.data(), text.data() + text.size(), result);
      if (error == std::errc{} && end == text.data() + text.size()) {
        if constexpr (std::is_floating_point_v<T>)
          if (!std::isfinite(result))
            throw std::runtime_error("Non-finite API number");
        return result;
      }
      throw std::runtime_error("API field '" + std::string(name) +
                               "' contains an invalid numeric string");
    }
  } else if constexpr (std::is_same_v<T, std::string>) {
    // Some API fields (for example episode numbers) are numeric in certain
    // responses even though their model representation is textual.
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number()) return value.dump();
    throw std::runtime_error("API field '" + std::string(name) +
                             "' must be a string or number, got " +
                             value.type_name());
  } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
    if (value.is_array()) {
      std::vector<std::string> result;
      result.reserve(value.size());
      for (const auto &item : value) {
        if (item.is_string()) {
          result.push_back(item.get<std::string>());
        } else if (item.is_number()) {
          result.push_back(item.dump());
        } else {
          throw std::runtime_error("API field '" + std::string(name) +
                                   "' contains a non-string, non-number item");
        }
      }
      return result;
    }
  }

  try {
    return value.get<T>();
  } catch (const std::exception &e) {
    throw std::runtime_error("Invalid API field '" + std::string(name) +
                             "' (JSON type " + value.type_name() +
                             "): " + e.what());
  }
}

#define YANI_FIELD(type, name) obj.name = yani_get<type>(j, #name);

#define YANI_FIELD_JSON(type, name, json_name)                                 \
  obj.name = yani_get<type>(j, json_name);

#define YANI_MODEL(name, fields)                                               \
  inline void from_json(const nlohmann::json &j, name &obj) {                  \
    if (!j.is_object())                                                        \
      throw std::runtime_error("API model must be an object: " #name);         \
    fields                                                                     \
  }

#include "models.def"

#undef YANI_MODEL
#undef YANI_FIELD_JSON
#undef YANI_FIELD

// ============================================================
// to_json
// ============================================================

#define YANI_FIELD(type, name) j[#name] = obj.name;

#define YANI_FIELD_JSON(type, name, json_name) j[json_name] = obj.name;

#define YANI_MODEL(name, fields)                                               \
  inline void to_json(nlohmann::json &j, const name &obj) { fields }

#include "models.def"

#undef YANI_MODEL
#undef YANI_FIELD_JSON
#undef YANI_FIELD

} // namespace yani
