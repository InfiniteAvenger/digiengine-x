#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "vpet/framebuffer.h"

namespace vpet {

// Palette-indexed sprite with one or more animation frames.
// Palette index 0 is always transparent.
struct Sprite {
    int w = 0, h = 0, frames = 0;
    std::vector<Color> palette;
    std::vector<uint8_t> pixels;  // frames * w * h indices

    bool valid() const { return frames > 0; }
};

// Parses the text .spr format described in docs/PACK_FORMAT.md.
bool parse_sprite(const std::string& text, Sprite& out, std::string& err);

bool parse_hex_color(const std::string& hex, Color& out);

}  // namespace vpet
