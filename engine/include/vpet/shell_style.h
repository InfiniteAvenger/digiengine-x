#pragma once
// Colors for the handheld shell some frontends draw around the screen
// (Android). The player picks them in settings; the engine only stores them.
#include <cstdint>

namespace vpet {

struct NamedColor {
    const char* name;
    uint8_t r, g, b;
};

inline constexpr NamedColor kShellColors[] = {
    {"MINT", 0x5a, 0xb8, 0xae},   {"WHITE", 0xee, 0xea, 0xe0}, {"BLACK", 0x2a, 0x2b, 0x33},
    {"GREY", 0x8e, 0x90, 0x9c},   {"RED", 0xd9, 0x46, 0x3e},   {"CORAL", 0xef, 0x7d, 0x57},
    {"ORANGE", 0xf3, 0x9c, 0x38}, {"YELLOW", 0xf4, 0xcd, 0x4c}, {"LIME", 0x9b, 0xcf, 0x5a},
    {"GREEN", 0x3e, 0x9e, 0x5c},  {"SKY", 0x72, 0xb8, 0xf0},   {"BLUE", 0x3d, 0x6f, 0xd6},
    {"NAVY", 0x27, 0x39, 0x6b},   {"PURPLE", 0x8b, 0x5c, 0xc9}, {"PINK", 0xf2, 0x8b, 0xb6},
};
constexpr int kShellColorCount = int(sizeof(kShellColors) / sizeof(kShellColors[0]));

// Indexes into kShellColors for the body, the faceplate around the screen
// and the buttons.
struct ShellStyle {
    uint8_t body = 0, trim = 2, buttons = 5;  // mint, black, coral
    bool operator==(const ShellStyle& o) const { return body == o.body && trim == o.trim && buttons == o.buttons; }
    bool operator!=(const ShellStyle& o) const { return !(*this == o); }
};

}  // namespace vpet
