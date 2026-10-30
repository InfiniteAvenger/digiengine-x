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
    // Faint diagonal texture so full-screen menus aren't flat.
    Color grain = mix(t.ui, t.ui_text, 12);
    for (int y = 12; y < kScreenH; ++y)
        for (int x = (6 - y % 6) % 6; x < kScreenW; x += 6) fb.set(x, y, grain);
    // Beveled header: light top edge, dark bottom edge, then a soft shadow.
    fb.fill_rect(0, 0, kScreenW, 11, t.accent);
    fb.fill_rect(0, 0, kScreenW, 1, mix(t.accent, rgb(0xff, 0xff, 0xff), 110));
    fb.fill_rect(0, 10, kScreenW, 1, mix(t.accent, rgb(0, 0, 0), 70));
    fb.fill_rect(0, 11, kScreenW, 1, mix(t.ui, rgb(0, 0, 0), 120));
    fb.draw_text_centered(4, title, mix(t.accent, rgb(0, 0, 0), 60));  // engraved shadow
    fb.draw_text_centered(3, title, t.ui);
}

void draw_page_dots(Framebuffer& fb, const Theme& t, int page, int count) {
    for (int i = 0; i < count; ++i) {
        int x = kScreenW - 6 - (count - 1 - i) * 5;
        Color c = i == page ? t.ui : mix(t.accent, rgb(0, 0, 0), 60);
        fb.fill_rect(x - 1, 4, 3, 3, c);
    }
}

void draw_card(Framebuffer& fb, const Theme& t, int x, int y, int w, int h) {
    fb.panel(x, y, w, h, mix(t.ui, t.ui_text, 14), mix(t.ui, t.ui_text, 40));
    fb.fill_rect(x + 1, y + 1, w - 2, 1, mix(t.ui, t.ui_text, 26));  // top highlight
}

void draw_hearts(Framebuffer& fb, int x, int y, int halves, int hearts) {
    for (int i = 0; i < hearts; ++i) {
        int frame = halves >= (i + 1) * 2 ? 0 : halves == i * 2 + 1 ? 2 : 1;
        fb.draw_sprite(icons().heart, frame, x + i * 9, y);
    }
}

void draw_footer(Framebuffer& fb, const Theme& t, const std::string& text) {
    const int y = kScreenH - 11;
    fb.fill_rect(0, y, kScreenW, 11, mix(t.ui, t.ui_text, 22));
    fb.fill_rect(0, y, kScreenW, 1, mix(t.ui, t.accent, 70));

    // "A: NEXT B: CHOOSE C: BACK" -> [A] NEXT [B] CHOOSE [C] BACK, with each
    // button letter drawn as a little key cap.
    struct Part {
        char key;  // 0 for plain text
        std::string label;
    };
    std::vector<Part> parts;
    for (size_t i = 0; i < text.size();) {
        bool key = i + 1 < text.size() && text[i] >= 'A' && text[i] <= 'C' && text[i + 1] == ':' &&
                   (i == 0 || text[i - 1] == ' ');
        if (key) {
            size_t start = i + 2;
            while (start < text.size() && text[start] == ' ') ++start;
            size_t end = start;
            while (end < text.size() && !(end + 1 < text.size() && text[end] == ' ' && text[end + 1] >= 'A' &&
                                          text[end + 1] <= 'C' && end + 2 < text.size() && text[end + 2] == ':'))
                ++end;
            std::string label = text.substr(start, end - start);
            while (!label.empty() && label.back() == ' ') label.pop_back();
            parts.push_back({text[i], label});
            i = end;
            while (i < text.size() && text[i] == ' ') ++i;
        } else {
            parts.push_back({0, text.substr(i)});
            break;
        }
    }
    int width = 0;
    for (const Part& p : parts) width += (p.key ? 9 : 0) + Framebuffer::text_width(p.label) + 6;
    int x = (kScreenW - (width - 6)) / 2;
    for (const Part& p : parts) {
        if (p.key) {
            fb.panel(x, y + 2, 7, 8, t.accent, mix(t.accent, rgb(0, 0, 0), 80));
            fb.draw_text(x + 2, y + 3, std::string(1, p.key), t.ui);
            x += 9;
        }
        fb.draw_text(x, y + 3, p.label, mix(t.ui_text, t.ui, 40));
        x += Framebuffer::text_width(p.label) + 6;
    }
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

// --- Status: an overview, then life, battle and timer details -----------------

class StatusScene : public Scene {
public:
    using Scene::Scene;

    void on_press(Button b) override {
        if (b == BTN_A) page_ = (page_ + 1) % kPages;
        else if (b == BTN_B && page_ == kBattlePage && game_.species().stage != Stage::Egg)
            game_.push(make_moves_scene(game_));
        else game_.pop();
    }

    void draw(Framebuffer& fb) override {
        static const char* const kTitles[kPages] = {"STATUS", "LIFE", "BATTLE", "TIMERS"};
        const Theme& t = game_.pack().theme;
        draw_title(fb, t, kTitles[page_]);
        draw_page_dots(fb, t, page_, kPages);
        switch (page_) {
            case 0: draw_overview(fb, t); break;
            case 1: draw_life(fb, t); break;
            case kBattlePage: draw_battle(fb, t); break;
            default: draw_timers(fb, t); break;
        }
        draw_footer(fb, t, page_ == kBattlePage ? "A: NEXT B: MOVES C: BACK" : "A: NEXT  C: BACK");
    }

private:
    static constexpr int kPages = 4, kBattlePage = 2;
    static constexpr int kRowGap = 10;

    static Color dim(const Theme& t) { return mix(t.ui_text, t.ui, 110); }

    // One detail row: a quiet label, the value on the right, a hairline under.
    static void row(Framebuffer& fb, const Theme& t, int y, const std::string& label, const std::string& value) {
        fb.draw_text(6, y, label, dim(t));
        fb.draw_text(kScreenW - 6 - Framebuffer::text_width(value), y, value, t.ui_text);
        fb.fill_rect(4, y + 7, kScreenW - 8, 1, mix(t.ui, t.ui_text, 22));
    }

    std::string since(uint64_t then) const {
        uint64_t now = game_.now();
        return format_duration(now > then ? now - then : 0);
    }

    // Earliest any evolution branch could happen (UINT64_MAX: none).
    uint64_t next_form_due() const {
        uint64_t due = UINT64_MAX;
        for (const EvolveRule& r : game_.species().evolve)
            due = std::min(due, game_.pet().stage_ms + uint64_t(r.after_seconds) * 1000);
        return due;
    }

    // Name, form, level, both meters, the four battle stats and any needs.
    void draw_overview(Framebuffer& fb, const Theme& t) const {
        const Pack& pack = game_.pack();
        const Species& s = game_.species();
        const Pet& pet = game_.pet();
        bool egg = s.stage == Stage::Egg;

        // Portrait window with a sky-and-grass backdrop.
        int px = 4, py = 15;
        fb.panel(px, py, 36, 36, t.bg_top, mix(t.ui, t.accent, 120));
        fb.fill_rect(px + 1, py + 1, 34, 12, mix(t.bg_top, rgb(0xff, 0xff, 0xff), 70));
        fb.fill_rect(px + 1, py + 27, 34, 8, t.bg_bottom);
        fb.fill_rect(px + 1, py + 27, 34, 1, mix(t.bg_bottom, rgb(0xff, 0xff, 0xff), 60));
        fb.draw_sprite(s.sprite, 0, px + 2, py + 2, 2);

        const int tx = 45;
        fb.draw_text(tx, 17, s.name, t.accent);
        if (pet.forever) fb.draw_sprite(icons().forever, 0, tx + Framebuffer::text_width(s.name) + 3, 17);
        fb.draw_text(tx, 26, stage_label(s.stage), dim(t));

        if (egg) {
            uint64_t due = next_form_due(), now = game_.now();
            row(fb, t, 58, "HATCHES IN", due == UINT64_MAX ? "-" : due > now ? format_duration(due - now) : "SOON");
            row(fb, t, 58 + kRowGap, "AGE", since(pet.born_ms));
            return;
        }

        std::string xp = std::to_string(pet.xp) + "/" + std::to_string(xp_to_next(pet.level));
        fb.draw_text(tx, 36, "LV " + std::to_string(pet.level), t.ui_text);
        fb.draw_text(kScreenW - 5 - Framebuffer::text_width(xp), 36, xp, dim(t));
        draw_bar(fb, tx, 44, kScreenW - 5 - tx, 3, int(pet.xp), int(xp_to_next(pet.level)), t.accent,
                 mix(t.ui, t.ui_text, 30));

        // Meters, hearts right-aligned.
        int hearts = max_hearts(pack, pet), hx = kScreenW - 5 - hearts_width(hearts);
        fb.draw_text(6, 56, "HUNGER", dim(t));
        draw_hearts(fb, hx, 55, pet.hunger, hearts);
        fb.draw_text(6, 66, "STRENGTH", dim(t));
        draw_hearts(fb, hx, 65, pet.strength * 2, hearts);

        // Four stat tiles.
        Stats st = pet_stats(pack, pet);
        const std::pair<const char*, int> tiles[] = {{"HP", st.hp}, {"ATK", st.atk}, {"DEF", st.def}, {"SPD", st.spd}};
        for (int i = 0; i < 4; ++i) {
            int x = 4 + i * 28, y = 75;
            std::string v = std::to_string(tiles[i].second);
            fb.panel(x, y, 26, 19, mix(t.ui, t.ui_text, 16), mix(t.ui, t.ui_text, 34));
            fb.draw_text(x + (26 - Framebuffer::text_width(tiles[i].first)) / 2, y + 3, tiles[i].first, dim(t));
            fb.draw_text(x + (26 - Framebuffer::text_width(v)) / 2, y + 11, v, t.ui_text);
        }

        // Condition tags, or ALL GOOD when nothing needs doing.
        std::vector<std::pair<std::string, Color>> tags;
        if (pet.sick) tags.push_back({"SICK", rgb(0xe0, 0x40, 0x50)});
        if (pet.asleep) tags.push_back({"ASLEEP", rgb(0x6c, 0xb4, 0xff)});
        if (pet.poop) tags.push_back({"POOP " + std::to_string(pet.poop), rgb(0xb8, 0x86, 0x5b)});
        if (pet.needs_attention()) tags.push_back({"CALLING", t.accent});
        if (tags.empty()) tags.push_back({"ALL GOOD", rgb(0x38, 0xb7, 0x64)});
        int x = 4;
        for (const auto& [text, c] : tags) {
            int w = Framebuffer::text_width(text) + 6;
            if (x + w > kScreenW - 4) break;
            fb.panel(x, 97, w, 9, mix(t.ui, c, 60), c);
            fb.draw_text(x + 3, 99, text, c);
            x += w + 3;
        }
    }

    void draw_life(Framebuffer& fb, const Theme& t) const {
        const Species& s = game_.species();
        const Pet& pet = game_.pet();
        uint64_t now = game_.now();
        int y = 17;
        auto next = [&](const std::string& label, const std::string& value) {
            row(fb, t, y, label, value);
            y += kRowGap;
        };
        next("AGE", since(pet.born_ms));
        next("IN THIS FORM", since(pet.stage_ms));
        if (!s.evolve.empty()) {  // past the earliest time, it is waiting on conditions
            uint64_t due = next_form_due();
            next("NEXT FORM", due > now ? format_duration(due - now) : "NEEDS MORE");
        }
        std::optional<uint64_t> life = lifespan_left_ms(game_.pack(), pet, now);
        if (pet.forever) next("LIFESPAN", "FOREVER");
        else if (life) next("LIFESPAN", format_duration(*life));
        next("WEIGHT", std::to_string(pet.weight) + "G");
        next("MISTAKES", std::to_string(pet.care_mistakes) + " (" + std::to_string(pet.stage_mistakes) + " NOW)");
        next("TRAINED", std::to_string(pet.trainings) + "X");
        next("EFFORT", std::to_string(pet.effort));
        if (s.stage != Stage::Egg) {
            const Care& c = s.care;
            char clock[16];
            std::snprintf(clock, sizeof clock, "%02d:%02d-%02d:%02d", c.sleep_min / 60, c.sleep_min % 60,
                          c.wake_min / 60, c.wake_min % 60);
            next("BEDTIME", c.sleep_min == c.wake_min ? "NEVER" : clock);
        }
    }

    void draw_battle(Framebuffer& fb, const Theme& t) const {
        const Pack& pack = game_.pack();
        const Pet& pet = game_.pet();
        std::string attr = game_.species().attribute.empty() ? "-" : game_.species().attribute;
        for (char& ch : attr) ch = char(toupper(static_cast<unsigned char>(ch)));
        int y = 17;
        row(fb, t, y, "LEVEL", std::to_string(pet.level));
        row(fb, t, y += kRowGap, "XP", std::to_string(pet.xp) + "/" + std::to_string(xp_to_next(pet.level)));
        row(fb, t, y += kRowGap, "ATTRIBUTE", attr);
        row(fb, t, y += kRowGap, "BEST FLOOR", std::to_string(pet.run.best));
        fb.draw_text(6, y += kRowGap + 3, "MOVES", t.accent);
        y += 10;
        std::vector<int> moves = equipped_moves(pack, pet);
        if (moves.empty()) fb.draw_text(6, y, "NONE YET", dim(t));
        for (int m : moves) {
            const Move& mv = pack.moves[m];
            std::string pow = "PWR " + std::to_string(mv.power);
            fb.draw_sprite(icons().ui, move_icon(mv.game), 5, y - 2);
            fb.draw_text(16, y, mv.name, t.ui_text);
            fb.draw_text(kScreenW - 6 - Framebuffer::text_width(pow), y, pow, dim(t));
            y += 11;
        }
    }

    // Exact timers: when each need changes next and how long until an open
    // call becomes a care mistake.
    void draw_timers(Framebuffer& fb, const Theme& t) const {
        const Species& s = game_.species();
        const Pet& pet = game_.pet();
        uint64_t now = game_.now();
        int y = 17;
        if (s.stage == Stage::Egg) {
            row(fb, t, y, "CARE", "NONE YET");
        } else {
            const Care& c = s.care;
            auto left = [](uint32_t total, uint32_t done) {
                return format_duration(uint64_t(total > done ? total - done : 0) * 1000);
            };
            const char* paused = pet.asleep ? "PAUSED" : nullptr;
            row(fb, t, y, "HUNGER -1/2", paused ? paused : left(std::max<uint32_t>(1, c.hunger_s / 2), pet.hunger_s));
            row(fb, t, y += kRowGap, "STRENGTH -1", paused ? paused : left(c.strength_s, pet.strength_s));
            row(fb, t, y += kRowGap, "NEXT POOP", paused ? paused : left(c.poop_s, pet.poop_s));
        }
        fb.draw_text(6, y += kRowGap + 3, "CALLS", t.accent);
        y += 10;
        bool any = false;
        auto call = [&](const char* name, const Call& cl) {
            if (!cl.active()) return;
            uint64_t deadline = cl.since_ms + uint64_t(kCallTimeoutS) * 1000;
            row(fb, t, y, name, deadline > now ? "MISS IN " + format_duration(deadline - now) : "NOW");
            y += kRowGap;
            any = true;
        };
        call("HUNGER", pet.hunger_call);
        call("STRENGTH", pet.strength_call);
        call("LIGHTS", pet.lights_call);
        if (!any) fb.draw_text(6, y, "NONE", dim(t));
    }

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
            if (i == cursor_) fb.panel(4, y - 3, kScreenW - 8, 11, mix(t.ui, t.accent, 60), t.accent);
            if (i == 0) fb.draw_sprite(icons().kibble, 0, 7, y - 2);
            else fb.draw_sprite(icons().ui, kUiMedicine, 7, y - 2);
            fb.draw_text(18, y, kNames[i], i == cursor_ ? t.accent : t.ui_text);
        }
        fb.draw_text_centered(50, kInfo[cursor_], mix(t.ui_text, t.ui, 40));
        draw_card(fb, t, 2, 63, kScreenW - 4, 28);

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
                game_.sfx(mini_.success() ? Game::Sfx::Hit : Game::Sfx::Miss);
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

        draw_card(fb, t, 2, 92, kScreenW - 4, 15);
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
