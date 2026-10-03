#pragma once

#include <models.hpp>

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
  static std::int64_t get() { return 1LL << 63; }
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

template <typename T> T yani_get(const nlohmann::json &j, const char *name) {
  if (!j.contains(name) || j.at(name).is_null())
    return yani_null_value<T>::get();

  return j.at(name).get<T>();
}

#define YANI_FIELD(type, name) obj.name = yani_get<type>(j, #name);

#define YANI_FIELD_JSON(type, name, json_name)                                 \
  obj.name = yani_get<type>(j, json_name);

#define YANI_MODEL(name, fields)                                               \
  inline void from_json(const nlohmann::json &j, name &obj) { fields }

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
