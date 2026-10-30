#pragma once
// The handheld shell drawn around the game screen on PC and Android: a
// rounded body with the screen in a bezel and three round buttons on the
// right. Pure pixels, no platform calls, so any frontend can use it.
#include <cstdint>
#include <vector>
#include "vpet/framebuffer.h"

namespace vpet {

constexpr int kShellW = 204, kShellH = 152;
constexpr int kShellScreenX = 20, kShellScreenY = 16;  // where the 120x120 screen sits

class Shell {
public:
    Shell();

    // Composes the shell, the game screen and the buttons (`held` = BTN_*
    // bits, drawn pressed) into pixels(): kShellW x kShellH, RGB565.
    void compose(const Framebuffer& fb, uint8_t held);
    const Color* pixels() const { return px_.data(); }

    // The BTN_* bit of the button at shell pixel (x, y), or 0. Generous, so
    // thumbs hit easily.
    static uint8_t button_at(int x, int y);

private:
    std::vector<Color> body_;  // the static body, drawn once
    std::vector<Color> px_;
};

}  // namespace vpet
