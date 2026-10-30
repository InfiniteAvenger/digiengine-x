#include "shell.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <thread>
#include "font.h"
#include "vpet/platform.h"

namespace vpet {
namespace {

// ---- Color and light -------------------------------------------------------

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
Rgb scale(Rgb c, float k) { return {c.r * k, c.g * k, c.b * k}; }
Rgb plus(Rgb c, float v) { return {c.r + v, c.g + v, c.b + v}; }
float luma(Rgb c) { return (0.3f * c.r + 0.59f * c.g + 0.11f * c.b) / 255.0f; }
float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }
float smoothstep(float a, float b, float x) {
    float t = clamp01((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}
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
// Source over destination, both 0xAARRGGBB; the result is opaque.
Argb blend(Argb dst, Argb src) {
    uint32_t a = src >> 24;
    auto ch = [&](int shift) {
        uint32_t s = (src >> shift) & 0xFF, d = (dst >> shift) & 0xFF;
        return ((s * a + d * (255 - a)) / 255) << shift;
    };
    return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

// One light, up and to the left, in front of the device. Surfaces are
// described by their slope (nx, ny): the normal is (nx, ny, 1) normalized,
// pointing out of the screen; a flat surface is (0, 0).
constexpr float kLx = -0.42f, kLy = -0.62f, kLz = 0.66f;

float diffuse(float nx, float ny) {
    float len = std::sqrt(nx * nx + ny * ny + 1);
    return std::max(0.0f, (nx * kLx + ny * kLy + kLz) / len) / kLz;  // 1 when flat
}

float specular(float nx, float ny, float power) {
    const float hl = std::sqrt(kLx * kLx + kLy * kLy + (kLz + 1) * (kLz + 1));
    const float hx = kLx / hl, hy = kLy / hl, hz = (kLz + 1) / hl;  // halfway to the viewer
    float len = std::sqrt(nx * nx + ny * ny + 1);
    return std::pow(std::max(0.0f, (nx * hx + ny * hy + hz) / len), power);
}

// A surface of `base` color with slope (nx, ny), shiny by `gloss`.
Rgb lit(Rgb base, float nx, float ny, float gloss, float power) {
    Rgb c = scale(base, 0.6f + 0.4f * diffuse(nx, ny));
    return lerp(c, {255, 255, 255}, clamp01(gloss * specular(nx, ny, power)));
}

// Slope of a rounded edge `depth` pixels in from the edge of a bevel
// `width` wide: steep at the edge, flat where the bevel ends.
float bevel_slope(float depth, float width) {
    float u = 1 - clamp01(depth / width);
    return 1.8f * u * u;
}

// Small per-pixel noise, the grain of molded plastic.
float grain(int x, int y) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h >> 16) & 0xFF) / 255.0f - 0.5f;
}

// ---- Shapes (signed distances, negative inside) ----------------------------

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

float circle(float px, float py, float cx, float cy, float r) { return std::hypot(px - cx, py - cy) - r; }

// Outward direction of a distance field at (x, y), by central differences.
void gradient(const std::function<float(float, float)>& f, float x, float y, float& gx, float& gy) {
    gx = f(x + 1, y) - f(x - 1, y);
    gy = f(x, y + 1) - f(x, y - 1);
    float len = std::hypot(gx, gy);
    if (len > 1e-5f) gx /= len, gy /= len;
}

// ---- Lettering ---------------------------------------------------------------

// The logo's letters, 7 rows tall, drawn as chunky rounded blocks.
struct Glyph {
    char c;
    const char* rows[7];
};
constexpr Glyph kLogoGlyphs[] = {
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'I', {"###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'N', {"#...#", "##..#", "#.#.#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
};

const Glyph& logo_glyph(char c) {
    for (const Glyph& g : kLogoGlyphs)
        if (g.c == c) return g;
    return kLogoGlyphs[0];
}

// Letters as a grid of filled cells, with a true signed distance to their
// outline (inside too), so bevels follow the letter shapes and not the cells.
struct CellGrid {
    float x0 = 0, y0 = 0, u = 1;  // top-left and cell size, in pixels
    int cols = 0, rows = 0;
    std::vector<uint8_t> cells;

    bool filled(int c, int r) const { return c >= 0 && r >= 0 && c < cols && r < rows && cells[size_t(r * cols + c)]; }

    float distance(float px, float py) const {
        int c = int(std::floor((px - x0) / u)), r = int(std::floor((py - y0) / u));
        bool in = filled(c, r);
        float best = 2.5f * u;
        for (int dr = -2; dr <= 2; ++dr)
            for (int dc = -2; dc <= 2; ++dc) {
                if (filled(c + dc, r + dr) == in) continue;
                float ccx = x0 + (float(c + dc) + 0.5f) * u, ccy = y0 + (float(r + dr) + 0.5f) * u;
                float qx = std::max(std::fabs(px - ccx) - u / 2, 0.0f), qy = std::max(std::fabs(py - ccy) - u / 2, 0.0f);
                best = std::min(best, std::hypot(qx, qy));
            }
        return in ? -best : best;
    }
};

// Grid columns for "DIGIENGINE" (a blank column between letters).
CellGrid letter_grid(const std::string& text) {
    CellGrid g;
    g.rows = 7;
    for (char c : text) g.cols += int(std::string(logo_glyph(c).rows[0]).size()) + 1;
    g.cols -= 1;
    g.cells.assign(size_t(g.cols * g.rows), 0);
    int x = 0;
    for (char c : text) {
        const Glyph& gl = logo_glyph(c);
        int w = int(std::string(gl.rows[0]).size());
        for (int r = 0; r < 7; ++r)
            for (int col = 0; col < w; ++col)
                if (gl.rows[r][col] == '#') g.cells[size_t(r * g.cols + x + col)] = 1;
        x += w + 1;
    }
    return g;
}

bool glyph_ink(char ch, int gx, int gy) {
    const char* g = glyph_for(ch);
    return g && gx >= 0 && gx < 3 && gy >= 0 && gy < 5 && g[gy * 3 + gx] == '#';
}

// Ink of `text` in the 3x5 font at unit size u, `advance` units per letter,
// top-left (x0, y0), at (x, y).
bool text_ink(const std::string& text, float x0, float y0, float u, float advance, float x, float y) {
    if (y < y0 || x < x0) return false;
    float col = (x - x0) / u;
    size_t i = size_t(col / advance);
    if (i >= text.size()) return false;
    return glyph_ink(text[i], int(col - float(i) * advance), int((y - y0) / u));
}

// Runs fn(y) for every row, split across the CPU's cores.
void parallel_rows(int h, const std::function<void(int)>& fn) {
    int n = int(std::max(1u, std::min(8u, std::thread::hardware_concurrency())));
    std::vector<std::thread> threads;
    for (int t = 0; t < n; ++t)
        threads.emplace_back([&, t] {
            for (int y = t; y < h; y += n) fn(y);
        });
    for (std::thread& th : threads) th.join();
}

}  // namespace

void Shell::build(int w, int h, const ShellStyle& style) {
    w_ = std::max(w, 1);
    h_ = std::max(h, 1);
    style_ = style;
    const float W = float(w_), H = float(h_), M = std::min(W, H);

    // ---- Layout. Sizes are fractions of the screen's side S. ----------------
    // One group, centered: tall windows stack the logo, the faceplate with
    // the buttons beside it, and a speaker grille; wide ones (landscape) put
    // the logo and grille in a column left of the faceplate.
    constexpr float kPad = 0.075f, kTail = 0.24f, kGap = 0.075f;
    constexpr float kBtnR = 0.1f, kCollar = 1.3f, kBtnStep = 0.34f;
    constexpr float kBlockH = 0.2f, kLogoGap = 0.1f, kGrilleGap = 0.13f, kGrilleH = 0.17f;
    constexpr float kLeftW = 0.75f, kLeftGap = 0.12f;  // landscape's left column
    const bool wide = W > H;
    const float rim = 0.022f * M;  // the molded lip around the front
    const float main_w = 1 + 2 * kPad + kGap + 2 * kCollar * kBtnR;
    const float content_w = main_w + (wide ? kLeftW + kLeftGap : 0);
    const float main_h = 1 + 2 * kPad + kTail;
    const float content_h = main_h + (wide ? 0 : kBlockH + kLogoGap + kGrilleGap + kGrilleH);
    const float S = std::floor(std::min((0.94f * W - 2 * rim) / content_w, (0.93f * H - 2 * rim) / content_h));

    const float half = S / 2 + kPad * S;  // the faceplate's square part
    const float left = (W - content_w * S) / 2;
    const float cx = left + (wide ? (kLeftW + kLeftGap) * S : 0) + half;
    // Sit a little above the middle on tall phones, like a face.
    const float top = wide ? (H - content_h * S) / 2 : (H - content_h * S) * 0.44f;
    const float cy = top + (wide ? 0 : (kBlockH + kLogoGap) * S) + half;
    screen_size_ = int(S);
    screen_x_ = int(std::lround(cx - S / 2));
    screen_y_ = int(std::lround(cy - S / 2));

    button_r_ = kBtnR * S;
    const float bx = cx + half + kGap * S + kCollar * button_r_;
    for (int i = 0; i < 3; ++i) {
        button_cx_[i] = int(std::lround(bx));
        button_cy_[i] = int(std::lround(cy + float(i - 1) * kBtnStep * S));
    }

    // The logo: "DIGIENGINE" and a taller X in the buttons' color, over a
    // tagline. Centered above the faceplate, or left of it when wide.
    CellGrid letters = letter_grid("DIGIENGINE"), ex = letter_grid("X");
    constexpr float kBigX = 1.35f;
    const float logo_units = float(letters.cols) + 2 + float(ex.cols) * kBigX;
    const float logo_u = (wide ? kLeftW * S : 2 * half * 0.92f) / logo_units;
    const std::string tagline = "VIRTUAL PET";
    const float tag_scale = 0.42f, tag_adv = 5.0f;
    const float logo_cx = wide ? left + kLeftW * S / 2 : cx;
    const float block_h = (7 + 2.4f + 5 * tag_scale) * logo_u;  // letters, gap, tagline
    const float logo_top = wide ? cy - half + 0.12f * S : top + (kBlockH * S - block_h) / 2;
    const float logo_x0 = logo_cx - logo_units * logo_u / 2;
    letters.x0 = logo_x0;
    letters.y0 = logo_top;
    letters.u = logo_u;
    ex.u = logo_u * kBigX;
    ex.x0 = logo_x0 + (float(letters.cols) + 2) * logo_u;
    ex.y0 = logo_top + 3.5f * logo_u - 3.5f * ex.u;
    const float tag_u = logo_u * tag_scale;
    const float tag_w = (float(tagline.size()) * tag_adv - 2) * tag_u;
    const float tag_x0 = logo_cx - tag_w / 2, tag_y0 = logo_top + (7 + 2.4f) * logo_u;

    // The keyhole tail under the faceplate, and the grille below it (or
    // under the logo when wide).
    const float tail_he = kTail * S / 2 + 0.03f * S;
    const float tail_cy = cy + half + kTail * S / 2 - 0.03f * S;
    const float hole_r = 0.019f * S, hole_step = 0.06f * S;
    const float grille_cx = wide ? logo_cx : cx;
    const float grille_cy = wide ? cy + 0.3f * S : tail_cy + tail_he + (kGrilleGap + kGrilleH / 2) * S;
    // The power light: above the A button, or beside it when wide (there's
    // no room above).
    const float led_r = 0.022f * S;
    const float led_x = wide ? std::min(bx + 0.24f * S, W - rim - 0.06f * S) : bx;
    const float led_y = wide ? float(button_cy_[0]) : cy - half + 0.02f * S;
    // Tall phones have room left over above and below: a lanyard slot at the
    // top and a seam where the case halves meet at the bottom.
    const float spare_top = top - rim, spare_bottom = H - rim - (grille_cy + kGrilleH / 2 * S);
    const bool lanyard = !wide && spare_top > 0.22f * S;
    const float lanyard_y = rim + spare_top * 0.5f;
    const bool seam = !wide && spare_bottom > 0.2f * S;
    const float seam_y = H - rim - spare_bottom * 0.45f;

    const Rgb body = named(style.body), trim = named(style.trim), btn = named(style.buttons);
    const bool dark_body = luma(body) < 0.3f, dark_trim = luma(trim) < 0.3f;
    const Rgb bezel = {16, 17, 23};
    const Rgb metal = {176, 178, 186};
    const Rgb led = {96, 232, 128};

    // Distance fields for the big shapes.
    auto face = [&](float x, float y) { return sd_round_rect(x, y, W / 2, H / 2, W / 2 - rim, H / 2 - rim, 0.07f * M); };
    auto plate = [&](float x, float y) {
        float square = sd_round_rect(x, y, cx, cy, half, half, 0.17f * S);
        float tail = sd_trapezoid(x, y, cx, tail_cy, 0.17f * S, 0.29f * S, tail_he, 0.06f * S);
        return smooth_union(square, tail, 0.07f * S);
    };
    auto frame = [&](float x, float y) {  // the dark window the LCD sits in
        return sd_round_rect(x, y, cx, cy, S / 2 + 0.032f * S, S / 2 + 0.032f * S, 0.04f * S);
    };
    const float logo_bx0 = logo_x0 - logo_u, logo_bx1 = logo_x0 + (logo_units + 1) * logo_u;
    const float logo_by0 = ex.y0 - logo_u, logo_by1 = ex.y0 + 8 * ex.u;
    auto logo = [&](float x, float y, bool* accent) {
        float a = letters.distance(x, y), b = ex.distance(x, y);
        if (accent) *accent = b < a;
        return std::min(a, b);
    };
    // Two screws hold the faceplate, low on the keyhole's tail.
    const float screw_r = 0.024f * S;
    const float screws[2][2] = {{cx - 0.17f * S, tail_cy + 0.045f * S}, {cx + 0.17f * S, tail_cy + 0.045f * S}};

    const float lip_bevel = std::max(3.0f, 0.016f * M), plate_bevel = std::max(3.0f, 0.03f * S);
    const float logo_bevel = 0.3f * logo_u;

    body_.assign(size_t(w_) * size_t(h_), 0);
    parallel_rows(h_, [&](int y) {
        for (int x = 0; x < w_; ++x) {
            const float px = float(x) + 0.5f, py = float(y) + 0.5f;
            float gx = 0, gy = 0;

            // The front face: gently domed, with a rounded lip. Outside the
            // lip is the darker side of the shell.
            float df = face(px, py);
            float nx = (px - W / 2) / W * 0.5f, ny = (py - H / 2) / H * 0.5f;
            if (df > -lip_bevel) {
                gradient(face, px, py, gx, gy);
                float s = bevel_slope(-df, lip_bevel);
                nx += gx * s;
                ny += gy * s;
            }
            Rgb c = lit(body, nx, ny, 0.35f, 24);
            Rgb side = scale(darker(body, 0.35f), 0.85f + 0.3f * (1 - py / H));
            c = lerp(side, c, coverage(df));
            c = plus(c, grain(x, y) * 5);

            // Lanyard slot: a rounded hole through the shell.
            if (lanyard && std::fabs(py - lanyard_y) < 0.08f * S) {
                auto slot = [&](float a, float b) {
                    return sd_round_rect(a, b, W / 2, lanyard_y, 0.13f * S, 0.032f * S, 0.032f * S);
                };
                float d = slot(px, py);
                if (d < 1) {
                    float s = 0, ux = 0, uy = 0;
                    if (d > -0.025f * S) {
                        gradient(slot, px, py, ux, uy);
                        s = bevel_slope(-d, 0.025f * S);
                    }
                    Rgb hole = lit(darker(body, 0.62f), -ux * s, -uy * s, 0, 1);
                    c = lerp(c, hole, coverage(d));
                }
            }
            // Where the two halves of the case meet: a fine groove.
            if (seam && df < -lip_bevel) {
                float g = py - seam_y;
                if (g > -1.5f && g < 0.5f) c = darker(c, 0.3f);
                else if (g >= 0.5f && g < 2.0f) c = lighter(c, 0.18f);
            }

            // The power light's glow on the plastic.
            float led_d = std::hypot(px - led_x, py - led_y);
            float glow = std::exp(-led_d / (0.05f * S)) * 0.35f;
            c = lerp(c, lighter(led, 0.2f), glow * (1 - coverage(led_d - led_r * 1.4f)));

            // Tagline, pressed in.
            if (py > tag_y0 - tag_u && py < tag_y0 + 6 * tag_u) {
                if (text_ink(tagline, tag_x0, tag_y0, tag_u, tag_adv, px, py))
                    c = dark_body ? lighter(c, 0.18f) : darker(c, 0.22f);
                else if (text_ink(tagline, tag_x0, tag_y0, tag_u, tag_adv, px, py - 0.4f * tag_u))
                    c = lighter(c, 0.2f);
            }

            // The logo, raised in the faceplate's color with the X in the
            // buttons' color, casting a soft shadow.
            if (px > logo_bx0 && px < logo_bx1 && py > logo_by0 && py < logo_by1) {
                float sh = logo(px - 0.18f * logo_u, py - 0.3f * logo_u, nullptr);
                c = darker(c, 0.3f * (1 - smoothstep(-0.2f * logo_u, 0.5f * logo_u, sh)));
                bool accent = false;
                float d = logo(px, py, &accent);
                if (d < 1) {
                    float lnx = 0, lny = 0;
                    if (d > -logo_bevel) {
                        gradient([&](float a, float b) { return logo(a, b, nullptr); }, px, py, gx, gy);
                        float s = bevel_slope(-d, logo_bevel);
                        lnx = gx * s;
                        lny = gy * s;
                    }
                    Rgb l = lit(accent ? btn : trim, lnx, lny, 0.7f, 30);
                    c = lerp(c, l, coverage(d));
                }
            }

            // Speaker grille: staggered rows of holes.
            if (std::fabs(px - grille_cx) < 5 * hole_step && std::fabs(py - grille_cy) < 2.5f * hole_step) {
                for (int row = -1; row <= 1; ++row) {
                    // 7 holes across the middle row, 6 staggered above and below.
                    float off = row == 0 ? 0 : hole_step / 2;
                    float hy = grille_cy + float(row) * hole_step * 0.87f;
                    int k = int(std::lround((px - grille_cx - off) / hole_step));
                    if (row == 0 ? std::abs(k) > 3 : (k < -3 || k > 2)) continue;
                    float hx = grille_cx + off + float(k) * hole_step;
                    float d = circle(px, py, hx, hy, hole_r);
                    if (d < 1) {
                        float wall = std::max(0.0f, 1 + d / (hole_r * 0.6f));  // near the rim
                        float hnx = -(px - hx) / hole_r * wall, hny = -(py - hy) / hole_r * wall;
                        Rgb hole = lit(darker(body, 0.6f), hnx * 1.5f, hny * 1.5f, 0, 1);
                        c = lerp(c, hole, coverage(d));
                    }
                }
            }

            // The power light: a little lens in a dark ring.
            {
                float d = led_d - led_r * 1.35f;
                if (d < 1) c = lerp(c, darker(body, 0.55f), coverage(d));
                float l = led_d - led_r;
                if (l < 1) {
                    float lx = (px - led_x) / led_r, ly = (py - led_y) / led_r;
                    c = lerp(c, lit(led, lx * 0.8f, ly * 0.8f, 1.2f, 40), coverage(l));
                }
            }

            // Button collars in the faceplate's color, each with a dark well
            // the cap sits in (the caps are drawn on top).
            for (int i = 0; i < 3; ++i) {
                float ox = px - float(button_cx_[i]), oy = py - float(button_cy_[i]);
                float rho = std::hypot(ox, oy);
                float rc = kCollar * button_r_, rw = 1.07f * button_r_;
                float sh = std::hypot(ox - 0.02f * S, oy - 0.035f * S) - rc;  // shadow
                if (sh < 0.06f * S) c = darker(c, 0.35f * (1 - smoothstep(-0.03f * S, 0.05f * S, sh)));
                float d = rho - rc;
                if (d > 1) continue;
                float ux = rho > 0 ? ox / rho : 0, uy = rho > 0 ? oy / rho : 0;
                float cb = 0.12f * button_r_;
                float slope = d > -cb ? bevel_slope(-d, cb) : rho < rw + cb ? -bevel_slope(rho - rw, cb) : 0;
                Rgb col = lit(trim, ux * slope, uy * slope, 0.6f, 30);
                if (rho < rw) {  // the well: lit on its lower wall
                    float s = bevel_slope(rw - rho, 0.25f * button_r_);
                    col = lit(darker(trim, dark_trim ? 0.5f : 0.65f), -ux * s, -uy * s, 0, 1);
                    col = lerp(col, darker(col, 0.5f), coverage(rw - rho - 1));
                }
                c = lerp(c, col, coverage(d));
            }

            // The faceplate: soft shadow, then the plate with a rounded edge,
            // a gloss across the top, screws, holes in the tail, and the dark
            // window around the screen.
            float sh = plate(px - 0.012f * S, py - 0.03f * S);
            if (sh < 0.08f * S) c = darker(c, 0.4f * (1 - smoothstep(-0.03f * S, 0.07f * S, sh)));
            float d = plate(px, py);
            if (d < 1) {
                float pnx = 0, pny = 0;
                if (d > -plate_bevel) {
                    gradient(plate, px, py, gx, gy);
                    float s = bevel_slope(-d, plate_bevel);
                    pnx = gx * s;
                    pny = gy * s;
                }
                Rgb p = lit(trim, pnx, pny, 0.8f, 40);
                p = plus(p, grain(x, y) * 2.5f);
                // A broad sheen over the upper part, fading down and right.
                float t = ((py - (cy - half)) + 0.35f * (px - (cx - half))) / (2 * half);
                p = lighter(p, (dark_trim ? 0.1f : 0.07f) * (1 - smoothstep(0.05f, 0.45f, t)));

                for (const auto& s : screws) {
                    float sd = circle(px, py, s[0], s[1], screw_r * 1.3f);
                    if (sd > 1) continue;
                    p = lerp(p, darker(trim, 0.5f), coverage(sd));  // the countersink
                    float hd = circle(px, py, s[0], s[1], screw_r);
                    float sx = (px - s[0]) / screw_r, sy = (py - s[1]) / screw_r;
                    Rgb head = lit(metal, sx * 0.9f, sy * 0.9f, 0.9f, 20);
                    // A cross slot, turned a little.
                    float ax = std::fabs(0.94f * sx + 0.34f * sy), ay = std::fabs(-0.34f * sx + 0.94f * sy);
                    if ((ax < 0.16f || ay < 0.16f) && std::max(ax, ay) < 0.72f) head = darker(metal, 0.6f);
                    p = lerp(p, head, coverage(hd));
                }
                for (int k = -1; k <= 1; ++k) {
                    float hx = cx + float(k) * 0.065f * S, hy = tail_cy + 0.045f * S;
                    float hd = circle(px, py, hx, hy, 0.02f * S);
                    if (hd < 1) {
                        float s = std::max(0.0f, 1 + hd / (0.012f * S));
                        p = lerp(p, lit(darker(trim, 0.7f), -(px - hx) / (0.02f * S) * s, -(py - hy) / (0.02f * S) * s, 0, 1),
                                 coverage(hd));
                    }
                }

                float fd = frame(px, py);
                if (fd < 1) {
                    // The window is sunk into the plate: its wall catches
                    // light at the bottom and falls into shade at the top.
                    float fb = 0.02f * S, wnx = 0, wny = 0;
                    if (fd > -fb) {
                        gradient(frame, px, py, gx, gy);
                        float s = bevel_slope(-fd, fb);
                        wnx = -gx * s;
                        wny = -gy * s;
                    }
                    Rgb f = lit(bezel, wnx, wny, 0.4f, 20);
                    p = lerp(p, f, coverage(fd));
                }
                c = lerp(c, p, coverage(d));
            }
            body_[size_t(y) * size_t(w_) + size_t(x)] = to_argb(c);
        }
    });
    // The screen itself starts black; the game is drawn over it.
    for (int y = std::max(0, screen_y_); y < std::min(h_, screen_y_ + screen_size_); ++y)
        for (int x = std::max(0, screen_x_); x < std::min(w_, screen_x_ + screen_size_); ++x)
            body_[size_t(y) * size_t(w_) + size_t(x)] = 0xFF000000u;

    // The glass over the LCD: gaps between its pixels, a shadow where the
    // screen sinks under the frame, a soft vignette and a diagonal glare.
    // Shade (black) and glare (white) are folded into one color with alpha.
    const int n = screen_size_;
    glass_.assign(size_t(n) * size_t(n), 0);
    const bool grid = n >= kScreenW * 3;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            float fx = float(x) + 0.5f, fy = float(y) + 0.5f, fn = float(n);
            float shade = 0;
            if (grid && ((x + 1) * kScreenW / n != x * kScreenW / n || (y + 1) * kScreenH / n != y * kScreenH / n))
                shade = 0.2f;
            float edge = std::min({fx, fy, fn - fx, fn - fy});
            shade = 1 - (1 - shade) * (1 - 0.22f * (1 - smoothstep(0, 0.07f * fn, edge)));
            shade = 1 - (1 - shade) * (1 - 0.45f * (1 - smoothstep(0, 0.035f * fn, fy)));
            shade = 1 - (1 - shade) * (1 - 0.25f * (1 - smoothstep(0, 0.025f * fn, fx)));
            float diag = (fx + 0.7f * fy) / fn;
            float glare = 0.075f * std::exp(-std::pow((diag - 0.42f) / 0.16f, 2.0f)) +
                          0.05f * (1 - smoothstep(0, 0.5f, std::hypot(fx, fy) / fn));
            float a = 1 - (1 - shade) * (1 - glare);
            float v = a > 0 ? 255.0f * glare / a : 0;
            glass_[size_t(y) * size_t(n) + size_t(x)] = to_argb({v, v, v}, a);
        }
    }

    // Buttons: glossy domed caps with the letter pressed in.
    button_size_ = int(std::ceil(2 * button_r_ + 4));
    const float r = button_r_, mid = float(button_size_) / 2;
    const Rgb letter = luma(btn) < 0.3f ? lighter(btn, 0.45f) : darker(btn, 0.42f);
    const float lu = 0.12f * r;
    for (int i = 0; i < 3; ++i) {
        const std::string key(1, char('A' + i));
        for (int down = 0; down < 2; ++down) {
            std::vector<Argb>& out = buttons_[i][down];
            out.assign(size_t(button_size_) * size_t(button_size_), 0);
            const float ccy = mid + (down ? 0.05f * r : 0.0f), rr = down ? r * 0.97f : r;
            const Rgb base = down ? darker(btn, 0.18f) : btn;
            for (int y = 0; y < button_size_; ++y) {
                for (int x = 0; x < button_size_; ++x) {
                    float ox = float(x) + 0.5f - mid, oy = float(y) + 0.5f - ccy;
                    float rho = std::hypot(ox, oy), d = rho - rr;
                    if (d > 1) continue;
                    float t = rho / rr, s = 0.25f * t + 1.3f * t * t * t;  // flatter in the middle
                    float nx = rho > 0 ? ox / rho * s : 0, ny = rho > 0 ? oy / rho * s : 0;
                    Rgb c = lit(base, nx, ny, down ? 0.35f : 0.9f, 28);
                    if (down) c = darker(c, 0.25f * (1 - smoothstep(-0.9f * rr, 0.1f * rr, oy)) * smoothstep(0.6f, 1, t));
                    if (text_ink(key, -1.5f * lu, -2.5f * lu, lu, 4, ox, oy)) c = letter;
                    else if (text_ink(key, -1.5f * lu, -2.5f * lu, lu, 4, ox, oy - 0.45f * lu)) c = lighter(c, 0.2f);
                    if (-d < 0.045f * r) c = darker(base, 0.5f);
                    out[size_t(y) * size_t(button_size_) + size_t(x)] = to_argb(c, coverage(d));
                }
            }
        }
    }
}

uint8_t Shell::button_at(int x, int y) const {
    int best = -1;
    float best_d = 1.7f * button_r_;  // generous: the touch areas nearly touch
    for (int i = 0; i < 3; ++i) {
        float d = std::hypot(float(x - button_cx_[i]), float(y - button_cy_[i]));
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
            Argb game = from_565(fb.pixels()[(y * kScreenH / screen_size_) * kScreenW + x * kScreenW / screen_size_]);
            out[size_t(wy) * size_t(w_) + size_t(wx)] = blend(game, glass_[size_t(y) * size_t(screen_size_) + size_t(x)]);
        }
    for (int i = 0; i < 3; ++i) {
        const std::vector<Argb>& b = button(i, held & (1 << i));
        for (int y = 0; y < button_size_; ++y)
            for (int x = 0; x < button_size_; ++x) {
                int wx = button_x(i) + x, wy = button_y(i) + y;
                if (wx < 0 || wy < 0 || wx >= w_ || wy >= h_) continue;
                Argb& d = out[size_t(wy) * size_t(w_) + size_t(wx)];
                d = blend(d, b[size_t(y) * size_t(button_size_) + size_t(x)]);
            }
    }
    return out;
}

}  // namespace vpet
