#include "shell.h"
#include <algorithm>
#include <cmath>
#include "font.h"
#include "vpet/platform.h"

namespace vpet {
namespace {

// An original soft-plastic look: mint body, dark screen bezel, coral buttons.
constexpr Color kBackdrop = rgb(0x10, 0x10, 0x14);
constexpr Color kOutline = rgb(0x1a, 0x1c, 0x2c);
constexpr Color kBody = rgb(0x5a, 0xb8, 0xae);
constexpr Color kBodyLight = rgb(0x9a, 0xe2, 0xd6);
constexpr Color kBodyShade = rgb(0x37, 0x86, 0x80);
constexpr Color kBodyDeep = rgb(0x2a, 0x6a, 0x66);
constexpr Color kBezel = rgb(0x2a, 0x2f, 0x3e);
constexpr Color kBezelRim = rgb(0x44, 0x4c, 0x62);
constexpr Color kButton = rgb(0xef, 0x7d, 0x57);
constexpr Color kButtonLight = rgb(0xff, 0xb8, 0x96);
constexpr Color kButtonShade = rgb(0xb1, 0x4e, 0x3a);

struct Btn {
    int x, y;
    uint8_t bit;
    char key;
};
constexpr Btn kButtons[] = {{180, 50, BTN_A, 'A'}, {180, 82, BTN_B, 'B'}, {180, 114, BTN_C, 'C'}};
constexpr int kButtonR = 11, kWellR = 13, kHitR = 16;

// Signed distance from a pixel center to a rounded rectangle (negative inside).
float rounded_rect(int x, int y, float x0, float y0, float x1, float y1, float r) {
    float px = float(x) + 0.5f, py = float(y) + 0.5f;
    float dx = std::max(std::fabs(px - (x0 + x1) / 2) - ((x1 - x0) / 2 - r), 0.0f);
    float dy = std::max(std::fabs(py - (y0 + y1) / 2) - ((y1 - y0) / 2 - r), 0.0f);
    return std::sqrt(dx * dx + dy * dy) - r;
}

float circle(int x, int y, int cx, int cy, float r) {
    float dx = float(x) + 0.5f - float(cx), dy = float(y) + 0.5f - float(cy);
    return std::sqrt(dx * dx + dy * dy) - r;
}

void glyph(std::vector<Color>& px, int x, int y, char ch, Color c) {
    if (const char* g = glyph_for(ch))
        for (int r = 0; r < 5; ++r)
            for (int col = 0; col < 3; ++col)
                if (g[r * 3 + col] == '#') px[size_t((y + r) * kShellW + x + col)] = c;
}

// Pressed into the plastic: dark letters with a light edge below.
void engrave(std::vector<Color>& px, int x, int y, const char* text) {
    for (int i = 0; text[i]; ++i) {
        glyph(px, x + i * 4, y + 1, text[i], kBodyLight);
        glyph(px, x + i * 4, y, text[i], kBodyDeep);
    }
}

}  // namespace

Shell::Shell() : body_(size_t(kShellW * kShellH)), px_(size_t(kShellW * kShellH)) {
    const float bx0 = 1, by0 = 1, bx1 = kShellW - 1, by1 = kShellH - 1, br = 34;
    const float sx0 = kShellScreenX - 7, sy0 = kShellScreenY - 7;
    const float sx1 = kShellScreenX + kScreenW + 7, sy1 = kShellScreenY + kScreenH + 7;
    for (int y = 0; y < kShellH; ++y) {
        for (int x = 0; x < kShellW; ++x) {
            Color& c = body_[size_t(y * kShellW + x)];
            float d = rounded_rect(x, y, bx0, by0, bx1, by1, br);
            if (d > 0) {
                c = kBackdrop;
                continue;
            }
            if (d > -1.2f) {
                c = kOutline;
                continue;
            }
            // Bevel: pixels whose up-left neighbour is closer to the edge face the light.
            bool lit = rounded_rect(x - 2, y - 2, bx0, by0, bx1, by1, br) > d;
            c = d > -4 ? (lit ? kBodyLight : kBodyShade) : kBody;

            // The screen sits in a recessed bezel.
            float s = rounded_rect(x, y, sx0, sy0, sx1, sy1, 9);
            if (s <= 1.5f && s > 0) c = kBodyShade;
            if (s <= 0) {
                bool top = rounded_rect(x - 2, y - 2, sx0, sy0, sx1, sy1, 9) > s;
                c = s > -1.5f ? (top ? kOutline : kBezelRim) : kBezel;
            }

            // A recessed well around each button.
            for (const Btn& b : kButtons) {
                float w = circle(x, y, b.x, b.y, float(kWellR));
                if (w <= 0) c = w > -1.2f ? kBodyDeep : kBodyShade;
            }
        }
    }
    engrave(body_, 168, 20, "DIGI X");  // centered over the buttons (x 180)
}

void Shell::compose(const Framebuffer& fb, uint8_t held) {
    px_ = body_;
    const Color* src = fb.pixels();
    for (int y = 0; y < kScreenH; ++y)
        std::copy(src + y * kScreenW, src + (y + 1) * kScreenW,
                  px_.begin() + (kShellScreenY + y) * kShellW + kShellScreenX);

    for (const Btn& b : kButtons) {
        bool down = held & b.bit;
        int cy = b.y + (down ? 1 : 0);  // a pressed button sinks a pixel
        for (int y = cy - kButtonR - 1; y <= cy + kButtonR + 1; ++y) {
            for (int x = b.x - kButtonR - 1; x <= b.x + kButtonR + 1; ++x) {
                float d = circle(x, y, b.x, cy, float(kButtonR));
                if (d > 0) continue;
                Color& c = px_[size_t(y * kShellW + x)];
                if (d > -1.2f) {
                    c = kOutline;
                } else if (down) {
                    c = d > -3 ? kButtonShade : mix(kButton, kButtonShade, 90);
                } else {
                    bool lit = circle(x - 2, y - 2, b.x, cy, float(kButtonR)) > d;
                    c = d > -3.5f ? (lit ? kButtonLight : kButtonShade) : kButton;
                }
            }
        }
        glyph(px_, b.x - 1, cy - 2, b.key, down ? mix(kButtonShade, kOutline, 120) : kButtonShade);
    }
}

uint8_t Shell::button_at(int x, int y) {
    for (const Btn& b : kButtons)
        if (circle(x, y, b.x, b.y, float(kHitR)) <= 0) return b.bit;
    return 0;
}

}  // namespace vpet
