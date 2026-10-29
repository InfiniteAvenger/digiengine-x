#include "vpet/framebuffer.h"
#include <algorithm>
#include "font.h"
#include "vpet/sprite.h"

namespace vpet {

void Framebuffer::clear(Color c) { std::fill(std::begin(px_), std::end(px_), c); }

void Framebuffer::fill_rect(int x, int y, int w, int h, Color c) {
    int x0 = std::max(x, 0), y0 = std::max(y, 0);
    int x1 = std::min(x + w, kScreenW), y1 = std::min(y + h, kScreenH);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) px_[yy * kScreenW + xx] = c;
}

void Framebuffer::rect(int x, int y, int w, int h, Color c) {
    fill_rect(x, y, w, 1, c);
    fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y, 1, h, c);
    fill_rect(x + w - 1, y, 1, h, c);
}

void Framebuffer::panel(int x, int y, int w, int h, Color fill, Color border) {
    fill_rect(x + 1, y + 1, w - 2, h - 2, fill);
    fill_rect(x + 1, y, w - 2, 1, border);
    fill_rect(x + 1, y + h - 1, w - 2, 1, border);
    fill_rect(x, y + 1, 1, h - 2, border);
    fill_rect(x + w - 1, y + 1, 1, h - 2, border);
}

void Framebuffer::dither(Color c, int level) {
    static const uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    if (level <= 0) return;
    for (int y = 0; y < kScreenH; ++y)
        for (int x = 0; x < kScreenW; ++x)
            if (kBayer4[y & 3][x & 3] < level) px_[y * kScreenW + x] = c;
}

void Framebuffer::draw_sprite(const Sprite& s, int frame, int x, int y, int scale, bool flip) {
    if (!s.valid()) return;
    frame = ((frame % s.frames) + s.frames) % s.frames;
    const uint8_t* src = &s.pixels[size_t(frame) * s.w * s.h];
    for (int sy = 0; sy < s.h; ++sy) {
        for (int sx = 0; sx < s.w; ++sx) {
            uint8_t idx = src[sy * s.w + (flip ? s.w - 1 - sx : sx)];
            if (idx) fill_rect(x + sx * scale, y + sy * scale, scale, scale, s.palette[idx]);
        }
    }
}

void Framebuffer::draw_mask(const Sprite& s, int frame, int x, int y, Color c) {
    if (!s.valid()) return;
    frame = ((frame % s.frames) + s.frames) % s.frames;
    const uint8_t* src = &s.pixels[size_t(frame) * s.w * s.h];
    for (int sy = 0; sy < s.h; ++sy)
        for (int sx = 0; sx < s.w; ++sx)
            if (src[sy * s.w + sx]) set(x + sx, y + sy, c);
}

void Framebuffer::draw_icon(const Sprite& s, int frame, int x, int y, Color outline, Color fade_to, int fade) {
    if (!s.valid()) return;
    frame = ((frame % s.frames) + s.frames) % s.frames;
    const uint8_t* src = &s.pixels[size_t(frame) * s.w * s.h];
    Color edge = mix(outline, fade_to, fade);
    // Outline first: every opaque pixel paints its 8 neighbours, then the
    // colors go on top, leaving the edge only outside the shape.
    for (int sy = 0; sy < s.h; ++sy)
        for (int sx = 0; sx < s.w; ++sx)
            if (src[sy * s.w + sx]) fill_rect(x + sx - 1, y + sy - 1, 3, 3, edge);
    for (int sy = 0; sy < s.h; ++sy)
        for (int sx = 0; sx < s.w; ++sx)
            if (uint8_t idx = src[sy * s.w + sx]) set(x + sx, y + sy, mix(s.palette[idx], fade_to, fade));
}

void Framebuffer::draw_text(int x, int y, const std::string& text, Color c) {
    for (char ch : text) {
        if (const char* g = glyph_for(ch)) {
            for (int r = 0; r < 5; ++r)
                for (int col = 0; col < 3; ++col)
                    if (g[r * 3 + col] == '#') set(x + col, y + r, c);
        }
        x += 4;
    }
}

void Framebuffer::draw_text_centered(int y, const std::string& text, Color c) {
    draw_text((kScreenW - text_width(text)) / 2, y, text, c);
}

int Framebuffer::text_width(const std::string& text) { return text.empty() ? 0 : int(text.size()) * 4 - 1; }

}  // namespace vpet
