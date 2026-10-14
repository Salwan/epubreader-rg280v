#pragma once
#include <array>
#include <cstdint>

namespace epub {
struct Palette { const char* name; uint32_t background, text, accent, muted, panel; };
// Append new themes so persisted numeric selections retain their meaning.
extern const std::array<Palette, 6> palettes;
}
