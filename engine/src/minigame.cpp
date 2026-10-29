#include "vpet/minigame.h"
#include <algorithm>
#include <cstdlib>
#include <string>
#include "scenes.h"

namespace vpet {

void Minigame::start(MiniGame type, uint64_t now_ms, uint32_t seed, MinigameTuning tuning) {
    *this = Minigame{};
    type_ = type;
    tune_ = tuning;
    start_ = now_ms;
    zone_ = 6 + int(seed % uint32_t(100 - tune_.zone_width - 12));  // somewhere in the middle 6..88%
    // Each step uses one of the two buttons that differ from the step before,
    // so a combo never repeats a letter back to back.
    uint32_t r = seed | 1;
    auto next = [&r] {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        return r;
    };
    seq_[0] = int(next() % 3);
    for (int i = 1; i < kSeqLen; ++i) seq_[i] = (seq_[i - 1] + 1 + int(next() % 2)) % 3;
}

void Minigame::finish(float m) {
    if (done_) return;
    done_ = true;
    mult_ = m;
}

void Minigame::press(Button b, uint64_t now_ms, uint32_t rnd) {
    if (done_) return;
    switch (type_) {
        case MiniGame::Timing: {
            if (b == BTN_C) break;
            int center = zone_ + tune_.zone_width / 2;
            if (std::abs(meter_ - center) <= tune_.perfect_width / 2) finish(1.5f);
            else if (meter_ >= zone_ && meter_ <= zone_ + tune_.zone_width) finish(1.25f);
            else if (meter_ >= zone_ - 8 && meter_ <= zone_ + tune_.zone_width + 8) finish(0.9f);
            else finish(0.55f);
            break;
        }
        case MiniGame::Mash:
            // Only alternating A/B presses count; hammering one button does nothing.
            if (b != BTN_C && b != last_mash_) {
                ++mash_;
                last_mash_ = b;
            }
            break;
        case MiniGame::Sequence:
            if (b == Button(1 << seq_[seq_pos_])) {
                if (++seq_pos_ == kSeqLen) finish(1.5f);
            } else {
                finish(0.5f + 0.25f * float(seq_pos_));
            }
            break;
        case MiniGame::Dice:
            if (b == BTN_B && !rolled_) {
                rolled_ = true;
                roll_at_ = now_ms;
                die_ = int(rnd % uint32_t(kDieFaces)) + 1;
            }
            break;
    }
}

void Minigame::update(uint64_t now_ms) {
    if (done_) return;
    uint64_t t = now_ms - start_;
    switch (type_) {
        case MiniGame::Timing: {
            uint64_t ph = t % (tune_.sweep_ms * 2);
            meter_ = int(ph < tune_.sweep_ms ? ph * 100 / tune_.sweep_ms : (tune_.sweep_ms * 2 - ph) * 100 / tune_.sweep_ms);
            if (t > 6000) finish(0.55f);
            break;
        }
        case MiniGame::Mash:
            if (t >= tune_.mash_ms) finish(std::min(1.5f, 0.5f + 0.75f * float(mash_) / float(tune_.mash_goal)));
            break;
        case MiniGame::Sequence:
            if (t >= tune_.seq_ms) finish(0.5f + 0.25f * float(seq_pos_));
            break;
        case MiniGame::Dice:
            if (rolled_ && now_ms - roll_at_ >= 900) finish(kDieMult[die_ - 1]);
            else if (!rolled_ && t > 5000) finish(0.45f);
            break;
    }
}

void Minigame::draw(Framebuffer& fb, const Theme& t, int y, uint64_t now_ms) const {
    uint64_t elapsed = now_ms - start_;
    Color dim = mix(t.ui, t.ui_text, 50);
    switch (type_) {
        case MiniGame::Timing: {
            fb.draw_text_centered(y, "B IN THE GOLD!", t.accent);
            constexpr int bx = 10, bw = 100;
            int by = y + 11;
            fb.rect(bx - 1, by - 1, bw + 2, 10, mix(t.ui_text, t.ui, 60));
            fb.fill_rect(bx, by, bw, 8, mix(t.ui, t.ui_text, 30));
            fb.fill_rect(bx + zone_ * bw / 100, by, tune_.zone_width * bw / 100, 8, mix(t.accent, t.ui, 90));
            int center = zone_ + tune_.zone_width / 2;
            fb.fill_rect(bx + (center - tune_.perfect_width / 2) * bw / 100, by, tune_.perfect_width * bw / 100, 8, t.accent);
            int cx = bx + meter_ * bw / 100;
            fb.fill_rect(cx - 1, by - 3, 3, 14, t.ui_text);  // the moving cursor
            fb.fill_rect(cx, by - 2, 1, 12, t.ui);
            break;
        }
        case MiniGame::Mash:
            fb.draw_text_centered(y, (now_ms / 150) % 2 ? "ALTERNATE A + B!" : "ALTERNATE A + B !", t.accent);
            draw_bar(fb, 10, y + 11, 100, 8, mash_, tune_.mash_goal, mash_ >= tune_.mash_goal ? t.accent : t.ui_text, dim);
            draw_bar(fb, 10, y + 23, 100, 2, int(tune_.mash_ms - std::min(elapsed, tune_.mash_ms)), int(tune_.mash_ms), t.ui_text, dim);
            break;
        case MiniGame::Sequence: {
            fb.draw_text_centered(y, "PRESS IN ORDER!", t.accent);
            static const char* const kNames[] = {"A", "B", "C"};
            for (int i = 0; i < kSeqLen; ++i) {
                int bx = 28 + i * 17;
                if (i < seq_pos_) fb.fill_rect(bx, y + 9, 13, 12, t.accent);
                fb.rect(bx, y + 9, 13, 12, i == seq_pos_ ? t.accent : dim);
                fb.draw_text(bx + 5, y + 13, kNames[seq_[i]], i < seq_pos_ ? t.ui : t.ui_text);
            }
            draw_bar(fb, 10, y + 24, 100, 2, int(tune_.seq_ms - std::min(elapsed, tune_.seq_ms)), int(tune_.seq_ms), t.ui_text, dim);
            break;
        }
        case MiniGame::Dice: {
            fb.draw_text_centered(y, rolled_ ? "ROLLED!" : "B: ROLL THE DIE", t.accent);
            int face = rolled_ && now_ms - roll_at_ > 400 ? die_ : int((now_ms / 70) % uint64_t(kDieFaces)) + 1;
            // A 7-sided die drawn with pips: two columns of three plus a center.
            int dx = 50, dy = y + 8;
            fb.panel(dx, dy, 20, 18, t.ui_text, mix(t.ui_text, t.ui, 120));
            static const uint16_t kPips[kDieFaces] = {0x10, 0x44, 0x54, 0x145, 0x155, 0x16D, 0x17D};
            // bit layout:  0 1 2 / 3 4 5 / 6 7 8 over a 3x3 grid (4 = center)
            for (int b = 0; b < 9; ++b)
                if (kPips[face - 1] & (1 << b)) fb.fill_rect(dx + 4 + (b % 3) * 5, dy + 3 + (b / 3) * 5, 2, 2, t.ui);
            if (rolled_ && now_ms - roll_at_ > 400)
                fb.draw_text(74, y + 14, die_ >= 6 ? "CRITICAL!" : die_ <= 2 ? "GLANCE" : "", die_ >= 6 ? t.accent : t.ui_text);
            break;
        }
    }
}

}  // namespace vpet
