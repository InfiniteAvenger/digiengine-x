// Full-screen menus opened from the home screen: status, feed, train.
#include <algorithm>
#include <cstdio>
#include "icons.h"
#include "scenes.h"
#include "vpet/minigame.h"
#include "vpet/rpg.h"

namespace vpet {

std::string format_duration(uint64_t ms) {
    uint64_t s = ms / 1000, m = s / 60, h = m / 60, d = h / 24;
    auto two = [](uint64_t v) { return (v < 10 ? "0" : "") + std::to_string(v); };
    if (d) return std::to_string(d) + "D " + two(h % 24) + "H";
    if (h) return std::to_string(h) + "H " + two(m % 60) + "M";
    return std::to_string(m) + "M " + two(s % 60) + "S";
}

void draw_title(Framebuffer& fb, const Theme& t, const std::string& title) {
    fb.clear(t.ui);
    fb.fill_rect(0, 0, kScreenW, 11, t.accent);
    fb.draw_text_centered(3, title, t.ui);
}

void draw_hearts(Framebuffer& fb, int x, int y, int halves, int hearts) {
    for (int i = 0; i < hearts; ++i) {
        int frame = halves >= (i + 1) * 2 ? 0 : halves == i * 2 + 1 ? 2 : 1;
        fb.draw_sprite(icons().heart, frame, x + i * 9, y);
    }
}

void draw_footer(Framebuffer& fb, const Theme& t, const std::string& text) {
    fb.fill_rect(0, kScreenH - 11, kScreenW, 11, mix(t.ui, t.ui_text, 24));
    fb.draw_text_centered(kScreenH - 8, text, mix(t.ui_text, t.ui, 60));
}

void draw_bar(Framebuffer& fb, int x, int y, int w, int h, int value, int max, Color fill, Color back) {
    fb.fill_rect(x, y, w, h, back);
    if (max > 0 && value > 0) fb.fill_rect(x, y, (value > max ? max : value) * w / max, h, fill);
}

Color rarity_color(Rarity r) {
    switch (r) {
        case Rarity::Common: return rgb(0xf4, 0xf4, 0xf4);
        case Rarity::Uncommon: return rgb(0x7f, 0xe0, 0x8a);
        case Rarity::Rare: return rgb(0x6c, 0xb4, 0xff);
        case Rarity::Epic: return rgb(0xd0, 0x8c, 0xff);
    }
    return 0;
}

void draw_item_icon(Framebuffer& fb, const Pack& pack, int item, int x, int y) {
    if (item >= 0 && pack.items[item].icon >= 0) fb.draw_sprite(pack.item_icons, pack.items[item].icon, x, y);
}

std::vector<std::string> wrap_text(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush_word = [&] {
        if (word.empty()) return;
        if (!line.empty() && line.size() + 1 + word.size() > width) {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flush_word();
        else word += c;
    }
    flush_word();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

namespace {

constexpr int kLabelX = 6;
constexpr int kValueX = 48;

// --- Status: three pages, nothing hidden ------------------------------------

class StatusScene : public Scene {
public:
    using Scene::Scene;

    void on_press(Button b) override {
        if (b == BTN_A) page_ = (page_ + 1) % kPages;
        else if (b == BTN_B && page_ == 3 && game_.species().stage != Stage::Egg) game_.push(make_moves_scene(game_));
        else game_.pop();

    }

    void draw(Framebuffer& fb) override {
        const Theme& t = game_.pack().theme;
        const Species& s = game_.species();
        const Pet& pet = game_.pet();
        uint64_t now = game_.now();
        auto since = [&](uint64_t then) { return format_duration(now > then ? now - then : 0); };
        auto row = [&](int y, const char* label, const std::string& value) {
            fb.draw_text(kLabelX, y, label, t.accent);
            fb.draw_text(kValueX, y, value, t.ui_text);
        };

        draw_title(fb, t, "STATUS " + std::to_string(page_ + 1) + "/" + std::to_string(kPages));

        if (page_ == 0) {
            row(18, "NAME", s.name);
            row(27, "STAGE", stage_name(s.stage));
            row(36, "AGE", since(pet.born_ms));
            row(45, "IN FORM", since(pet.stage_ms));
            row(54, "WEIGHT", std::to_string(pet.weight) + "G");
            if (!s.evolve.empty()) {
                // Earliest any branch could happen; past that, it is waiting on conditions.
                uint64_t due = UINT64_MAX;
                for (const EvolveRule& r : s.evolve) due = std::min(due, pet.stage_ms + uint64_t(r.after_seconds) * 1000);
                row(63, "NEXT FORM", due > now ? format_duration(due - now) : "NEEDS MORE");
            }
            std::optional<uint64_t> life = lifespan_left_ms(game_.pack(), pet, now);
            if (pet.forever) row(72, "LIFESPAN", "FOREVER");
            else if (life) row(72, "LIFESPAN", format_duration(*life) + " LEFT");
            fb.draw_sprite(s.sprite, 0, kScreenW - 4 - s.sprite.w * 2, 14, 2);
        } else if (page_ == 1) {
            fb.draw_text(kLabelX, 19, "HUNGER", t.accent);
            draw_hearts(fb, kValueX, 18, pet.hunger, max_hearts(game_.pack(), pet));
            fb.draw_text(kLabelX, 29, "STRENGTH", t.accent);
            draw_hearts(fb, kValueX, 28, pet.strength * 2, max_hearts(game_.pack(), pet));
            row(40, "POOP", std::to_string(pet.poop) + "/" + std::to_string(kMaxPoop));
            row(49, "SICK", pet.sick ? "YES" : "NO");
            row(58, "SLEEPING", pet.asleep ? "YES" : "NO");
            row(67, "LIGHTS", pet.lights_on ? "ON" : "OFF");
            row(76, "MISTAKES", std::to_string(pet.care_mistakes) + " (" + std::to_string(pet.stage_mistakes) + " NOW)");
            row(85, "TRAINED", std::to_string(pet.trainings) + "X EFFORT " + std::to_string(pet.effort));
        } else if (page_ == 3) {
            const Pack& pack = game_.pack();
            Stats st = pet_stats(pack, pet);
            row(18, "LEVEL", std::to_string(pet.level));
            row(27, "XP", std::to_string(pet.xp) + "/" + std::to_string(xp_to_next(pet.level)));
            row(36, "HP", std::to_string(st.hp));
            row(45, "ATK DEF", std::to_string(st.atk) + "  " + std::to_string(st.def));
            row(54, "SPD", std::to_string(st.spd));
            row(63, "BEST FLOOR", std::to_string(pet.run.best));
            int y = 74;
            fb.draw_text(kLabelX, y, "MOVES", t.accent);
            for (int m : equipped_moves(pack, pet)) {
                fb.draw_text(kValueX, y, pack.moves[m].name, t.ui_text);
                y += 8;
            }
        } else {
            // Exact timers: when each need changes next and how long until an
            // open call becomes a care mistake.
            const Care& c = s.care;
            auto left = [](uint32_t total, uint32_t done) { return format_duration(uint64_t(total - done) * 1000); };
            if (s.stage == Stage::Egg) {
                row(18, "CARE", "NONE YET");
            } else {
                const char* paused = pet.asleep ? "PAUSED" : nullptr;
                row(18, "HUNGER -1", paused ? paused : left(c.hunger_s, pet.hunger_s));
                row(27, "STRENGTH-1", paused ? paused : left(c.strength_s, pet.strength_s));
                row(36, "NEXT POOP", paused ? paused : left(c.poop_s, pet.poop_s));
                char clock[16];
                std::snprintf(clock, sizeof clock, "%02d:%02d-%02d:%02d", c.sleep_min / 60, c.sleep_min % 60,
                              c.wake_min / 60, c.wake_min % 60);
                row(45, "BEDTIME", c.sleep_min == c.wake_min ? "NEVER" : clock);
            }
            int y = 58;
            fb.draw_text(kLabelX, y, "CALLS", t.accent);
            bool any = false;
            auto call = [&](const char* name, const Call& cl) {
                if (!cl.active()) return;
                uint64_t deadline = cl.since_ms + uint64_t(kCallTimeoutS) * 1000;
                y += 9;
                row(y, name, deadline > now ? format_duration(deadline - now) : "NOW");
                any = true;
            };
            call(" HUNGER", pet.hunger_call);
            call(" STRENGTH", pet.strength_call);
            call(" LIGHTS", pet.lights_call);
            if (!any) fb.draw_text(kValueX, y, "NONE", t.ui_text);
        }

        draw_footer(fb, t, page_ == 3 ? "A: NEXT B: MOVES C: BACK" : "A: NEXT  C: BACK");
    }

private:
    static constexpr int kPages = 4;
    int page_ = 0;
};

// --- Feed -------------------------------------------------------------------

class FeedScene : public Scene {
public:
    using Scene::Scene;

    void on_press(Button b) override {
        if (b == BTN_A) {
            cursor_ = (cursor_ + 1) % kCount;
        } else if (b == BTN_B) {
            Pet& pet = game_.pet();
            if (cursor_ == 0) {
                if (feed_kibble(game_.pack(), pet)) game_.play_eat(-1);  // the home screen shows the meal
                else game_.toast("FULL!");
            }
            else game_.toast(give_medicine(pet) ? "ALL BETTER!" : "NOT SICK");
            game_.pop();
        } else {
            game_.pop();
        }
    }

    void draw(Framebuffer& fb) override {
        const Theme& t = game_.pack().theme;
        const Pet& pet = game_.pet();
        draw_title(fb, t, "FEED");

        static const char* const kNames[kCount] = {"KIBBLE", "MEDICINE"};
        static const char* const kInfo[kCount] = {"+1/2 HUNGER  +1G", "CURES SICKNESS"};
        for (int i = 0; i < kCount; ++i) {
            int y = 22 + i * 12;
            if (i == cursor_) fb.fill_rect(4, y - 3, kScreenW - 8, 11, t.accent);
            fb.draw_text(10, y, kNames[i], i == cursor_ ? t.ui : t.ui_text);
        }
        fb.draw_text_centered(52, kInfo[cursor_], t.ui_text);

        fb.draw_text(kLabelX, 70, "HUNGER", t.accent);
        draw_hearts(fb, kValueX, 69, pet.hunger, max_hearts(game_.pack(), pet));
        fb.draw_text(kLabelX, 81, "SICK", t.accent);
        fb.draw_text(kValueX, 81, pet.sick ? "YES" : "NO", t.ui_text);

        draw_footer(fb, t, "A: NEXT B: USE C: BACK");
    }

private:
    static constexpr int kCount = 2;
    int cursor_ = 0;
};

// --- Train: a random drill each session, built on the shared minigames ------

class TrainScene : public Scene {
public:
    explicit TrainScene(Game& game) : Scene(game), drill_(int(game.rand() % kDrillCount)) {}

    void on_press(Button b) override {
        if (phase_ == Phase::Result) return;
        // C quits only before the drill starts; during it, every button is input
        // (the combo drill uses C).
        if (phase_ == Phase::Ready) {
            if (b == BTN_C) game_.pop();
            return;
        }
        if (phase_ == Phase::Play) mini_.press(b, game_.now(), game_.rand());
    }

    void update(uint64_t now) override {
        if (start_ == 0) start_ = now;
        if (phase_ == Phase::Ready && now - start_ >= kReadyMs) {
            phase_ = Phase::Play;
            mini_.start(kDrills[drill_].game, now, game_.rand(), MinigameTuning::training());
        }
        if (phase_ == Phase::Play) {
            mini_.update(now);
            if (mini_.done()) {
                phase_ = Phase::Result;
                result_at_ = now;
                strength_before_ = game_.pet().strength;
            }
        }
        if (phase_ == Phase::Result && !applied_ && now - result_at_ >= kResultMs) {
            applied_ = true;
            finish_training(game_.pack(), game_.pet(), mini_.success());
            game_.pop();
        }
    }

    void draw(Framebuffer& fb) override {
        const Theme& t = game_.pack().theme;
        const Species& s = game_.species();
        uint64_t now = game_.now();
        draw_title(fb, t, "TRAINING");
        fb.draw_text_centered(15, kDrills[drill_].name, t.accent);

        int py = 24, frame = 0;
        if (phase_ == Phase::Result && mini_.success()) {  // happy hops
            frame = int((now / 120) % 2);
            py -= frame * 3;
        }
        fb.draw_sprite(s.sprite, frame, (kScreenW - 32) / 2, py, 2);

        Color dim = mix(t.ui_text, t.ui, 90);
        if (phase_ == Phase::Ready) {
            fb.draw_text_centered(64, (now / 250) % 2 ? "READY..." : "READY", t.ui_text);
            fb.draw_text_centered(76, kDrills[drill_].info, dim);
        } else if (phase_ == Phase::Play) {
            mini_.draw(fb, t, 62, now);
        } else {
            bool full = strength_before_ >= max_hearts(game_.pack(), game_.pet());
            const char* verdict = mini_.mult() >= 1.5f ? "PERFECT!" : mini_.success() ? "GREAT!" : "MISSED...";
            fb.draw_text_centered(66, verdict, mini_.success() ? t.accent : t.ui_text);
            fb.draw_text_centered(76, mini_.success() ? (full ? "STRENGTH IS FULL" : "+1 STRENGTH") : "-1G ANYWAY", dim);
        }

        fb.draw_text(kLabelX, 97, "STRENGTH", t.accent);
        draw_hearts(fb, kValueX, 96, game_.pet().strength * 2, max_hearts(game_.pack(), game_.pet()));
        draw_footer(fb, t, phase_ == Phase::Ready ? "C: QUIT" : phase_ == Phase::Play ? "GO!" : "");
    }

private:
    struct Drill {
        const char* name;
        const char* info;
        MiniGame game;
    };
    static constexpr int kDrillCount = 3;
    static constexpr Drill kDrills[kDrillCount] = {
        {"TARGET DRILL", "STOP IN THE GOLD", MiniGame::Timing},
        {"SPEED BAG", "MASH FOR POWER", MiniGame::Mash},
        {"COMBO DRILL", "REPEAT THE COMBO", MiniGame::Sequence},
    };
    static constexpr uint64_t kReadyMs = 1200, kResultMs = 1300;

    enum class Phase { Ready, Play, Result };
    Phase phase_ = Phase::Ready;
    int drill_;
    Minigame mini_;
    uint64_t start_ = 0, result_at_ = 0;
    int strength_before_ = 0;
    bool applied_ = false;
};

}  // namespace

std::unique_ptr<Scene> make_status_scene(Game& game) { return std::make_unique<StatusScene>(game); }
std::unique_ptr<Scene> make_feed_scene(Game& game) { return std::make_unique<FeedScene>(game); }
std::unique_ptr<Scene> make_train_scene(Game& game) { return std::make_unique<TrainScene>(game); }

}  // namespace vpet
