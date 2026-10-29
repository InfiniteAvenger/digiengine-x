#pragma once
#include <cstdint>
#include <string>

namespace vpet {

// Fixed logical canvas. Platforms scale it up by whole pixels.
constexpr int kScreenW = 120;
constexpr int kScreenH = 120;

using Color = uint16_t;  // RGB565, matches common ESP32 SPI displays

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) {
    return Color(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Blends a toward b; t = 0 gives a, t = 256 gives b.
constexpr Color mix(Color a, Color b, int t) {
    int r = ((a >> 11) * (256 - t) + (b >> 11) * t) >> 8;
    int g = (((a >> 5) & 0x3F) * (256 - t) + ((b >> 5) & 0x3F) * t) >> 8;
    int bl = ((a & 0x1F) * (256 - t) + (b & 0x1F) * t) >> 8;
    return Color((r << 11) | (g << 5) | bl);
}

struct Sprite;

class Framebuffer {
public:
    void clear(Color c);
    void set(int x, int y, Color c) {
        if (unsigned(x) < unsigned(kScreenW) && unsigned(y) < unsigned(kScreenH)) px_[y * kScreenW + x] = c;
    }
    void fill_rect(int x, int y, int w, int h, Color c);
    void rect(int x, int y, int w, int h, Color c);
    // Filled box with a 1px border and clipped corners, for UI panels.
    void panel(int x, int y, int w, int h, Color fill, Color border);
    // Ordered-dither cover: level 0 draws nothing, 16 covers every pixel.
    void dither(Color c, int level);
    void draw_sprite(const Sprite& s, int frame, int x, int y, int scale = 1, bool flip = false);
    // Draws every opaque pixel of the sprite in one color (for UI icons).
    void draw_mask(const Sprite& s, int frame, int x, int y, Color c);
    // Built-in 3x5 font, 4px advance. Lowercase is drawn as uppercase.
    void draw_text(int x, int y, const std::string& text, Color c);
    void draw_text_centered(int y, const std::string& text, Color c);
    static int text_width(const std::string& text);

    const Color* pixels() const { return px_; }

private:
    Color px_[kScreenW * kScreenH] = {};
};

}  // namespace vpet
