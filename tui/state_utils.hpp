#pragma once

#include <cstddef>
#include <string>

namespace aniparser::tui {
inline void erase_last_utf8(std::string &text) {
  if (text.empty()) return;
  auto pos = text.size() - 1;
  while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xc0) == 0x80)
    --pos;
  text.erase(pos);
}

inline std::size_t bounded_selection(std::size_t selected, std::size_t count) {
  return count == 0 ? 0 : (selected < count ? selected : count - 1);
}
} // namespace aniparser::tui
