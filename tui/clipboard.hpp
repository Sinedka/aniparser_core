#pragma once

#include <string_view>

namespace aniparser::tui {
bool copy_to_clipboard(std::string_view text);
} // namespace aniparser::tui
