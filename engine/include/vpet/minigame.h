#pragma once
#include <cstdint>
#include "vpet/framebuffer.h"
#include "vpet/pack.h"
#include "vpet/platform.h"

namespace vpet {

// How forgiving a minigame is.
struct MinigameTuning {
    int zone_width = 14;       // timing: gold zone, percent of the bar
    int perfect_width = 4;     // timing: bright center of the zone
    uint64_t sweep_ms = 650;   // timing: one pass of the cursor
    int mash_goal = 11;        // mash: presses for a hit
    uint64_t mash_ms = 2500;
    uint64_t seq_ms = 3500;    // sequence: time for the whole combo

    static MinigameTuning battle() { return {}; }
    static MinigameTuning training() { return {26, 8, 850, 9, 2500, 5000}; }
};

// One 3-button charge minigame, shared by training and quest moves. Its
// result is a multiplier from about 0.5 (poor) to 1.5 (perfect); 1.25 and up
// counts as a hit.
class Minigame {
public:
    static constexpr int kSeqLen = 4;
    // Dice: a 7-sided die. 1-2 glance off, 6-7 are critical hits.
    static constexpr int kDieFaces = 7;
    static constexpr float kDieMult[kDieFaces] = {0.5f, 0.6f, 0.85f, 1.0f, 1.15f, 1.5f, 1.5f};

    // `seed` places the timing zone and picks the sequence.
    void start(MiniGame type, uint64_t now_ms, uint32_t seed, MinigameTuning tuning = MinigameTuning::battle());
    // `rnd` is only used by the dice roll.
    void press(Button b, uint64_t now_ms, uint32_t rnd);
    void update(uint64_t now_ms);

    bool done() const { return done_; }
    float mult() const { return mult_; }
    bool success() const { return done_ && mult_ >= 1.25f; }
    MiniGame type() const { return type_; }
    const MinigameTuning& tuning() const { return tune_; }

    // For tests and drawing.
    int zone_start() const { return zone_; }
    int cursor() const { return meter_; }
    int sequence(int i) const { return seq_[i]; }  // 0=A 1=B 2=C
    int die() const { return die_; }               // last roll, 1-7

    // Instruction line at y, widget below; fits in 30 rows.
    void draw(Framebuffer& fb, const Theme& t, int y, uint64_t now_ms) const;

private:
    void finish(float m);

    MiniGame type_ = MiniGame::Timing;
    MinigameTuning tune_;
    uint64_t start_ = 0, roll_at_ = 0;
    int meter_ = 0, zone_ = 50, mash_ = 0, seq_pos_ = 0, die_ = 1;
    int seq_[kSeqLen] = {};
    Button last_mash_ = Button(0);  // mash: the last button that counted
    bool rolled_ = false, done_ = false;
    float mult_ = 1.0f;
};

}  // namespace vpet
