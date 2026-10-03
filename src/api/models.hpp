#pragma once

#include <nlohmann/json.hpp>

namespace yani {

#define YANI_FIELD(type, name) type name{};

#define YANI_FIELD_JSON(type, name, json_name) type name{};

#define YANI_OPTIONAL_FIELD(type, name) std::optional<type> name{};

#define YANI_OPTIONAL_FIELD_JSON(type, name, json_name)                        \
  std::optional<type> name{};

#define YANI_MODEL(name, fields)                                               \
  struct name {                                                                \
    fields                                                                     \
  };

#include "models.def"

#undef YANI_MODEL
#undef YANI_OPTIONAL_FIELD
#undef YANI_OPTIONAL_FIELD_JSON
#undef YANI_FIELD_JSON
#undef YANI_FIELD
} // namespace yani
