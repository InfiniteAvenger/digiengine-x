// Procedural backdrop shared by the home screen and the quest arena: a
// dithered sky that follows the local clock (day, dusk, night), sun or moon,
// drifting clouds, rolling hills and grassy ground, all tinted from the
// pack's theme colors.
#include <cmath>
#include "scenes.h"

namespace vpet {
namespace {

constexpr Color kWhite = rgb(0xff, 0xff, 0xff);
constexpr Color kNightTop = rgb(0x10, 0x12, 0x2a);
constexpr Color kNightLow = rgb(0x2c, 0x2e, 0x5c);
constexpr int kBayer[2][2] = {{0, 2}, {3, 1}};

// Vertical gradient from a to b in `steps` flat bands with 2x2 ordered
// dithering at the band edges, which reads as smooth on a pixel display.
void dithered_gradient(Framebuffer& fb, int y0, int y1, Color a, Color b, int steps) {
    int h = y1 - y0;
    if (h <= 0) return;
    for (int y = y0; y < y1; ++y) {
        int v = (y - y0) * steps * 4 / h;  // quarter steps
        for (int x = 0; x < kScreenW; ++x) {
            int band = v / 4 + (v % 4 > kBayer[y & 1][x & 1] ? 1 : 0);
            if (band > steps) band = steps;
            fb.set(x, y, mix(a, b, band * 256 / steps));
        }
    }
}

void disc(Framebuffer& fb, int cx, int cy, int r, Color c) {
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x * x + y * y <= r * r + r) fb.set(cx + x, cy + y, c);
}

void cloud(Framebuffer& fb, int x, int y, Color c) {
    fb.fill_rect(x + 2, y + 3, 18, 4, c);
    fb.fill_rect(x + 5, y + 1, 7, 3, c);
    fb.fill_rect(x + 10, y, 6, 3, c);
    fb.fill_rect(x, y + 5, 22, 2, c);
}

}  // namespace

void draw_scenery(Framebuffer& fb, const Theme& t, int top, int horizon, int bottom, uint64_t now_ms,
                  int local_minute, bool dark) {
    bool night = dark || local_minute >= 20 * 60 || local_minute < 6 * 60;
    bool dusk = !night && (local_minute >= 18 * 60 || local_minute < 7 * 60);

    Color sky_top, sky_low;
    if (night) {
        sky_top = kNightTop;
        sky_low = kNightLow;
    } else if (dusk) {
        sky_top = mix(t.bg_top, rgb(0x5d, 0x27, 0x5d), 140);
        sky_low = mix(t.accent, rgb(0xef, 0x7d, 0x57), 110);
    } else {
        sky_top = mix(t.bg_top, rgb(0x3b, 0x8c, 0xe0), 90);
        sky_low = mix(t.bg_top, kWhite, 120);
    }
    Color ground = night ? mix(t.bg_bottom, kNightTop, 170) : t.bg_bottom;

    dithered_gradient(fb, top, horizon, sky_top, sky_low, 5);

    int sky_h = horizon - top;
    if (night) {
        for (int i = 0; i < 16; ++i) {
            int sx = (i * 37 + 11) % kScreenW, sy = top + 2 + (i * 23 + 5) % (sky_h - 14);
            if ((now_ms / 500 + uint64_t(i)) % 6) fb.set(sx, sy, mix(kWhite, sky_low, i % 3 * 60));
        }
        disc(fb, 18, top + 11, 5, rgb(0xf4, 0xf0, 0xd8));
        disc(fb, 21, top + 9, 4, sky_top);  // bite out of the moon -> crescent
    } else {
        disc(fb, 18, top + 12, 6, dusk ? rgb(0xff, 0x9e, 0x5e) : rgb(0xff, 0xe7, 0x8a));
        disc(fb, 18, top + 12, 4, dusk ? rgb(0xff, 0xc0, 0x80) : rgb(0xff, 0xf6, 0xc8));
        Color cc = mix(kWhite, sky_low, 40);
        int span = kScreenW + 50;
        cloud(fb, int((now_ms / 700) % uint64_t(span)) - 25, top + 6, cc);
        cloud(fb, int((now_ms / 1100 + 70) % uint64_t(span)) - 25, top + 20, mix(cc, sky_top, 50));
    }

    // Two layers of hills, the far one hazier.
    Color far_hill = mix(ground, sky_low, 140), near_hill = mix(ground, sky_low, 60);
    for (int x = 0; x < kScreenW; ++x) {
        int far = horizon - 9 - int(4 * std::sin(x * 0.07f + 1.0f) + 2 * std::sin(x * 0.19f));
        int near = horizon - 3 - int(3 * std::sin(x * 0.12f + 2.5f));
        fb.fill_rect(x, far, 1, horizon - far, far_hill);
        fb.fill_rect(x, near, 1, horizon - near, near_hill);
    }

    // Ground: bright grass edge, then a dithered fade to a darker base.
    fb.fill_rect(0, horizon, kScreenW, 1, mix(ground, kWhite, 70));
    dithered_gradient(fb, horizon + 1, bottom, ground, mix(ground, rgb(0, 0, 0), 60), 3);
    Color tuft = mix(ground, rgb(0, 0, 0), 90);
    int ground_h = bottom - horizon - 5;
    for (int i = 0; ground_h > 0 && i < 14; ++i) {
        int gx = (i * 29 + 7) % (kScreenW - 3), gy = horizon + 3 + (i * 13) % ground_h;
        fb.set(gx, gy, tuft);
        fb.set(gx + 2, gy, tuft);
        fb.set(gx + 1, gy + 1, tuft);
    }
}

}  // namespace vpet
