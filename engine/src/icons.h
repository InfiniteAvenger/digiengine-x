#pragma once
#include "vpet/sprite.h"

namespace vpet {

// UI sprites built into the engine (packs may override these later).
struct Icons {
    Sprite poop;       // 8x8
    Sprite heart;      // 7x6, frame 0 full, 1 empty, 2 half
    Sprite attention;  // 7x7
    Sprite sick;       // 7x7
    Sprite menu;       // 9x9 masks, one frame per home menu slot
    Sprite kibble;     // 8x8 bowl, the free food
    Sprite forever;    // 9x5 infinity badge for Forever Partners
};

const Icons& icons();

}  // namespace vpet
