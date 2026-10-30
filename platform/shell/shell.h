#pragma once
// The handheld shell drawn around the game screen on Android. It fills the
// whole window at the window's own resolution and is lit like a real toy: a
// plastic front with a molded lip, an embossed DIGIENGINE X logo, a glossy
// keyhole-shaped faceplate in a contrasting color with screws, a speaker
// grille and power light, and three domed buttons in collars to the right.
// Colors come from the player's ShellStyle. Pure pixels, no platform calls,
// so any frontend can use it.
#include <cstdint>
#include <vector>
#include "vpet/framebuffer.h"
#include "vpet/shell_style.h"

namespace vpet {

using Argb = uint32_t;  // 0xAARRGGBB

class Shell {
public:
    // Lays the shell out for a w x h pixel window and draws it.
    void build(int w, int h, const ShellStyle& style);
    int width() const { return w_; }
    int height() const { return h_; }
    const ShellStyle& style() const { return style_; }

    // The body, faceplate and logo, without the buttons; the screen area is
    // black. w x h, opaque.
    const std::vector<Argb>& body() const { return body_; }

    // Where the 120x120 game screen goes, in window pixels (a square; the
    // game is scaled up to fill it).
    int screen_x() const { return screen_x_; }
    int screen_y() const { return screen_y_; }
    int screen_size() const { return screen_size_; }
    // Drawn over the game: the LCD's pixel grid, its recessed edge shadow
    // and glare on the glass. screen_size() square, with alpha.
    const std::vector<Argb>& glass() const { return glass_; }

    // Button i (0 = A, 1 = B, 2 = C) as a square sprite with alpha, up or
    // pressed, and where its top-left corner goes.
    const std::vector<Argb>& button(int i, bool down) const { return buttons_[i][down ? 1 : 0]; }
    int button_size() const { return button_size_; }
    int button_x(int i) const { return button_cx_[i] - button_size_ / 2; }
    int button_y(int i) const { return button_cy_[i] - button_size_ / 2; }
    int button_cx(int i) const { return button_cx_[i]; }
    int button_cy(int i) const { return button_cy_[i]; }

    // The BTN_* bit of the button at window pixel (x, y), or 0. Generous, so
    // thumbs hit easily.
    uint8_t button_at(int x, int y) const;

    // Everything in one image (`held` buttons drawn pressed), for screenshots.
    std::vector<Argb> compose(const Framebuffer& fb, uint8_t held) const;

private:
    int w_ = 0, h_ = 0;
    ShellStyle style_;
    std::vector<Argb> body_, glass_;
    int screen_x_ = 0, screen_y_ = 0, screen_size_ = 0;
    int button_size_ = 0;
    float button_r_ = 0;
    int button_cx_[3] = {}, button_cy_[3] = {};
    std::vector<Argb> buttons_[3][2];
};

}  // namespace vpet
