#include "shell.h"
#include <algorithm>
#include <cmath>
#include "font.h"
#include "vpet/platform.h"

namespace vpet {
namespace {

struct Rgb {
    float r, g, b;  // 0-255
};

Rgb named(int i) {
    const NamedColor& c = kShellColors[i];
    return {float(c.r), float(c.g), float(c.b)};
}
Rgb lerp(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb lighter(Rgb c, float t) { return lerp(c, {255, 255, 255}, t); }
Rgb darker(Rgb c, float t) { return lerp(c, {0, 0, 0}, t); }
float luma(Rgb c) { return (0.3f * c.r + 0.59f * c.g + 0.11f * c.b) / 255.0f; }
float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }
// How much of a pixel a shape covers, from its signed distance (negative inside).
float coverage(float d) { return clamp01(0.5f - d); }

Argb to_argb(Rgb c, float a = 1.0f) {
    auto ch = [](float v) { return uint32_t(std::lround(std::min(255.0f, std::max(0.0f, v)))); };
    return ch(a * 255.0f) << 24 | ch(c.r) << 16 | ch(c.g) << 8 | ch(c.b);
}
Argb from_565(Color c) {
    uint32_t r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return 0xFF000000u | ((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2));
}

float sd_round_rect(float px, float py, float cx, float cy, float hw, float hh, float r) {
    float qx = std::fabs(px - cx) - (hw - r), qy = std::fabs(py - cy) - (hh - r);
    float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

// A trapezoid centered at (cx, cy), half width `top` at its top edge and
// `bottom` at its bottom edge, half height `he`, corners rounded by `r`.
float sd_trapezoid(float px, float py, float cx, float cy, float top, float bottom, float he, float r) {
    float x = std::fabs(px - cx), y = cy - py;  // y up
    float r1 = bottom - r, r2 = top - r, h = he - r;
    float cax = x - std::min(x, y < 0 ? r1 : r2), cay = std::fabs(y) - h;
    float k1x = r2, k1y = h, k2x = r2 - r1, k2y = 2 * h;
    float t = clamp01(((k1x - x) * k2x + (k1y - y) * k2y) / (k2x * k2x + k2y * k2y));
    float cbx = x - k1x + k2x * t, cby = y - k1y + k2y * t;
    float s = (cbx < 0 && cay < 0) ? -1.0f : 1.0f;
    return s * std::sqrt(std::min(cax * cax + cay * cay, cbx * cbx + cby * cby)) - r;
}

// Union of two shapes with a rounded fillet of size k where they meet.
float smooth_union(float a, float b, float k) {
    float h = clamp01(0.5f + 0.5f * (b - a) / k);
    return b + (a - b) * h - k * h * (1 - h);
}

bool glyph_ink(char ch, int gx, int gy) {
    const char* g = glyph_for(ch);
    return g && gx >= 0 && gx < 3 && gy >= 0 && gy < 5 && g[gy * 3 + gx] == '#';
}

// Ink of `text` in the 3x5 font at unit size u, top-left (x0, y0), at (x, y).
bool text_ink(const char* text, float x0, float y0, float u, float x, float y) {
    if (y < y0 || x < x0) return false;
    int col = int((x - x0) / u), row = int((y - y0) / u);
    int i = col / 4;
    for (int k = 0; k <= i; ++k)
        if (!text[k]) return false;
    return glyph_ink(text[i], col % 4, row);
}

constexpr const char* kLogo = "DIGIENGINE X";
constexpr int kLogoCols = 12 * 4 - 1;

}  // namespace

void Shell::build(int w, int h, const ShellStyle& style) {
    w_ = std::max(w, 1);
    h_ = std::max(h, 1);
    style_ = style;
    const float W = float(w_), H = float(h_);

    // Sizes are fractions of the screen's side S. The content (logo,
    // faceplate with its keyhole tail, and the button column) is fitted to
    // the window and centered; the body fills everything.
    constexpr float kPad = 0.075f, kTail = 0.24f, kLogoU = 0.018f, kLogoGap = 0.07f, kGap = 0.07f;
    constexpr float kBtnR = 0.1f, kWell = 1.22f, kBtnStep = 0.34f;
    // Tall windows put the logo above the faceplate; wide ones (landscape),
    // where height is short, put it in the free space to the left.
    const bool wide = W > H;
    const float logo_h = wide ? 0 : 5 * kLogoU + kLogoGap;
    const float content_w = 1 + 2 * kPad + kGap + 2 * kWell * kBtnR;
    const float content_h = logo_h + 1 + 2 * kPad + kTail;
    const float S = std::floor(std::min(0.95f * W / content_w, 0.92f * H / content_h));

    const float half = S / 2 + kPad * S;  // the faceplate's square part
    const float cx = (W - content_w * S) / 2 + half;
    const float top = (H - content_h * S) / 2;
    const float cy = top + logo_h * S + half;
    float logo_u = kLogoU * S, logo_x = cx - kLogoCols * logo_u / 2, logo_y = top;
    if (wide) {
        float room = cx - half - 0.06f * H;  // left of the faceplate, less a margin
        logo_u = std::min(logo_u, 0.8f * room / kLogoCols);
        logo_x = (cx - half) / 2 - kLogoCols * logo_u / 2;
        logo_y = cy - half;
    }
    screen_size_ = int(S);
    screen_x_ = int(std::lround(cx - S / 2));
    screen_y_ = int(std::lround(cy - S / 2));

    button_r_ = kBtnR * S;
    const float bx = cx + half + kGap * S + kWell * button_r_;
    for (int i = 0; i < 3; ++i) {
        button_cx_[i] = int(std::lround(bx));
        button_cy_[i] = int(std::lround(cy + float(i - 1) * kBtnStep * S));
    }

    const Rgb body = named(style.body), trim = named(style.trim), btn = named(style.buttons);
    const Rgb bezel = {18, 19, 25};
    const Rgb ink = luma(body) < 0.3f ? lighter(body, 0.22f) : darker(body, 0.25f);
    const Rgb trim_ink = luma(trim) < 0.3f ? lighter(trim, 0.18f) : darker(trim, 0.4f);

    // The faceplate: the square around the screen joined to a tail that
    // flares toward the bottom, like a keyhole.
    const float tail_he = kTail * S / 2 + 0.03f * S;
    const float tail_cy = cy + half + kTail * S / 2 - 0.03f * S;
    auto plate = [&](float x, float y) {
        float square = sd_round_rect(x, y, cx, cy, half, half, 0.16f * S);
        float tail = sd_trapezoid(x, y, cx, tail_cy, 0.17f * S, 0.28f * S, tail_he, 0.05f * S);
        return smooth_union(square, tail, 0.06f * S);
    };
    const float bevel = std::max(2.0f, 0.02f * S), light = std::max(1.0f, 0.01f * S);
    const float edge = 0.025f * std::min(W, H);

    body_.assign(size_t(w_) * size_t(h_), 0);
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            const float px = float(x) + 0.5f, py = float(y) + 0.5f;

            // Plastic body: lit from above, darker toward the window edges.
            Rgb c = lerp(lighter(body, 0.1f), darker(body, 0.12f), py / H);
            float e = std::min({px, py, W - px, H - py}) / edge;
            if (e < 1) c = darker(c, 0.25f * (1 - e));

            // The logo, pressed into the plastic with a light lower edge.
            if (py >= logo_y && py < logo_y + 6 * logo_u) {
                if (text_ink(kLogo, logo_x, logo_y, logo_u, px, py)) c = ink;
                else if (text_ink(kLogo, logo_x, logo_y, logo_u, px, py - 0.35f * logo_u)) c = lighter(c, 0.25f);
            }

            // Wells around the buttons.
            for (int i = 0; i < 3; ++i) {
                float dx = px - float(button_cx_[i]), dy = py - float(button_cy_[i]);
                float d = std::sqrt(dx * dx + dy * dy) - kWell * button_r_;
                if (d < 1) {
                    Rgb well = -d < 0.06f * button_r_ ? darker(body, 0.32f) : darker(body, 0.2f);
                    c = lerp(c, well, coverage(d));
                }
            }

            // The faceplate: a shadow below it, a groove around it, then the
            // plate with a bevel lit from the top left.
            float d = plate(px, py);
            if (d < 0.04f * S) {
                c = lerp(c, darker(c, 0.18f), coverage(plate(px, py - 0.015f * S)));
                if (d > 0) c = lerp(c, darker(body, 0.35f), clamp01(1 - d / std::max(1.5f, 0.012f * S)));
                Rgb p = lerp(lighter(trim, 0.06f), darker(trim, 0.08f), clamp01((py - (cy - half)) / (2 * half + kTail * S)));
                if (-d < bevel) {
                    float f = 1 + d / bevel;
                    bool lit = plate(px - light, py - light) > d;
                    p = lit ? lighter(p, 0.28f * f) : darker(p, 0.25f * f);
                }
                // Speaker holes in the tail.
                for (int k = -1; k <= 1; ++k) {
                    float hx = px - (cx + float(k) * 0.065f * S), hy = py - (tail_cy + 0.02f * S);
                    float hd = std::sqrt(hx * hx + hy * hy) - 0.02f * S;
                    p = lerp(p, trim_ink, coverage(hd));
                }
                // The dark frame around the screen, sunk into the plate.
                float sd = sd_round_rect(px, py, cx, cy, S / 2 + 0.03f * S, S / 2 + 0.03f * S, 0.035f * S);
                if (sd < 0.012f * S) p = lerp(p, darker(p, 0.35f), coverage(sd - 0.012f * S));
                p = lerp(p, bezel, coverage(sd));
                c = lerp(c, p, coverage(d));
            }
            body_[size_t(y) * size_t(w_) + size_t(x)] = to_argb(c);
        }
    }
    // The screen itself starts black; the game is drawn over it.
    for (int y = std::max(0, screen_y_); y < std::min(h_, screen_y_ + screen_size_); ++y)
        for (int x = std::max(0, screen_x_); x < std::min(w_, screen_x_ + screen_size_); ++x)
            body_[size_t(y) * size_t(w_) + size_t(x)] = 0xFF000000u;

    // Buttons: a domed cap with an outline, a highlight and its letter.
    button_size_ = int(std::ceil(2 * button_r_ + 4));
    const float r = button_r_, mid = float(button_size_) / 2;
    const Rgb letter = luma(btn) < 0.3f ? lighter(btn, 0.5f) : darker(btn, 0.45f);
    const float lu = 0.13f * r;
    for (int i = 0; i < 3; ++i) {
        const char key[2] = {char('A' + i), 0};
        for (int down = 0; down < 2; ++down) {
            std::vector<Argb>& out = buttons_[i][down];
            out.assign(size_t(button_size_) * size_t(button_size_), 0);
            const float ccy = mid + (down ? 0.05f * r : 0.0f);
            const Rgb base = down ? darker(btn, 0.14f) : btn;
            for (int y = 0; y < button_size_; ++y) {
                for (int x = 0; x < button_size_; ++x) {
                    float px = float(x) + 0.5f - mid, py = float(y) + 0.5f - ccy;
                    float d = std::sqrt(px * px + py * py) - r;
                    if (d > 1) continue;
                    float t = clamp01(0.5f - (0.5f * px + 0.7f * py) / r * (down ? 0.3f : 0.6f));
                    Rgb c = lerp(darker(base, 0.18f), lighter(base, 0.22f), t);
                    float sx = px + 0.35f * r, sy = py + 0.42f * r;  // soft shine, top left
                    float s = std::sqrt(sx * sx + sy * sy) / (0.24f * r);
                    if (!down && s < 1) c = lighter(c, 0.3f * (1 - s * s));
                    if (text_ink(key, -1.5f * lu, -2.5f * lu, lu, px, py)) c = letter;
                    if (-d < 0.07f * r) c = darker(base, 0.45f);
                    out[size_t(y) * size_t(button_size_) + size_t(x)] = to_argb(c, coverage(d));
                }
            }
        }
    }
}

uint8_t Shell::button_at(int x, int y) const {
    int best = -1;
    float best_d = 1.7f * button_r_;  // generous: the circles nearly touch
    for (int i = 0; i < 3; ++i) {
        float dx = float(x - button_cx_[i]), dy = float(y - button_cy_[i]);
        float d = std::sqrt(dx * dx + dy * dy);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best < 0 ? 0 : uint8_t(1 << best);
}

std::vector<Argb> Shell::compose(const Framebuffer& fb, uint8_t held) const {
    std::vector<Argb> out = body_;
    for (int y = 0; y < screen_size_; ++y)
        for (int x = 0; x < screen_size_; ++x) {
            int wx = screen_x_ + x, wy = screen_y_ + y;
            if (wx < 0 || wy < 0 || wx >= w_ || wy >= h_) continue;
            out[size_t(wy) * size_t(w_) + size_t(wx)] =
                from_565(fb.pixels()[(y * kScreenH / screen_size_) * kScreenW + x * kScreenW / screen_size_]);
        }
    for (int i = 0; i < 3; ++i) {
        const std::vector<Argb>& b = button(i, held & (1 << i));
        for (int y = 0; y < button_size_; ++y)
            for (int x = 0; x < button_size_; ++x) {
                int wx = button_x(i) + x, wy = button_y(i) + y;
                if (wx < 0 || wy < 0 || wx >= w_ || wy >= h_) continue;
                Argb s = b[size_t(y) * size_t(button_size_) + size_t(x)];
                uint32_t a = s >> 24;
                if (!a) continue;
                Argb& d = out[size_t(wy) * size_t(w_) + size_t(wx)];
                auto ch = [&](int shift) {
                    uint32_t sc = (s >> shift) & 0xFF, dc = (d >> shift) & 0xFF;
                    return ((sc * a + dc * (255 - a)) / 255) << shift;
                };
                d = 0xFF000000u | ch(16) | ch(8) | ch(0);
            }
    }
    return out;
}

}  // namespace vpet
