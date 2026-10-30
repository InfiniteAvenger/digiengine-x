// Hidden settings (hold A+C for 3 seconds): switch packs and versions, pick
// equipped moves, set the clock, use debug tools and see pack info. Built on
// the shared list screen.
#include <algorithm>
#include <cstdio>
#include "font.h"
#include "list_scene.h"
#include "vpet/combat.h"
#include "vpet/rpg.h"

namespace vpet {
namespace {

std::unique_ptr<Scene> make_pack_picker(Game& g) {
    auto rows = [&g] {
        std::vector<Row> out;
        Color c = g.pack().theme.ui_text;
        for (size_t i = 0; i < g.pack_names().size(); ++i)
            out.push_back({g.pack_names()[i], int(i) == g.pack_index() ? "OPEN" : "", c});
        return out;
    };
    auto detail = [&g](int row) {
        return row == g.pack_index() ? std::string("THE PACK YOU ARE PLAYING")
                                     : std::string("B: SWITCH. EACH PACK KEEPS ITS OWN SAVE");
    };
    auto pick = [&g](int row) {
        if (row == g.pack_index()) return g.toast("ALREADY OPEN");
        g.request_pack(row);  // applied at the end of this frame; returns to home
    };
    return std::make_unique<ListScene>(g, "PACKS", rows, detail, pick, "NO PACKS", true);
}

std::unique_ptr<Scene> make_about(Game& g) {
    auto rows = [&g] {
        const Pack& p = g.pack();
        Color c = p.theme.ui_text;
        auto up = [](std::string s) {
            for (char& ch : s) ch = char(toupper(static_cast<unsigned char>(ch)));
            return s;
        };
        return std::vector<Row>{{"PACK", up(p.name), c},
                                {"VERSION", up(p.version), c},
                                {"BY", up(p.author), c},
                                {"LICENSE", up(p.license), c},
                                {"CREATURES", std::to_string(p.species.size()), c},
                                {"ITEMS", std::to_string(p.items.size()), c}};
    };
    auto detail = [](int) { return std::string("DIGIENGINE X - OPEN SOURCE"); };
    return std::make_unique<ListScene>(g, "ABOUT", rows, detail, [](int) {}, "", true);
}

// Set the local time: A adds one to the chosen field, B moves on (and saves
// after the minutes), C cancels.
class ClockScene : public Scene {
public:
    explicit ClockScene(Game& game) : Scene(game) {
        int m = game.local_minute();
        hour_ = m / 60;
        minute_ = m % 60;
    }
    bool is_settings() const override { return true; }

    void on_press(Button b) override {
        if (b == BTN_C) return game_.pop();
        if (b == BTN_A) {
            if (field_ == 0) hour_ = (hour_ + 1) % 24;
            else minute_ = (minute_ + 1) % 60;
        } else if (field_ == 0) {
            field_ = 1;
        } else {
            game_.set_local_time(hour_, minute_);
            game_.toast("CLOCK SET");
            game_.pop();
        }
    }

    void draw(Framebuffer& fb) override {
        const Theme& t = game_.pack().theme;
        draw_title(fb, t, "CLOCK");
        char hh[3], mm[3];
        std::snprintf(hh, sizeof hh, "%02d", hour_);
        std::snprintf(mm, sizeof mm, "%02d", minute_);
        // Big digits: the 3x5 font drawn at 3x scale into a temporary sprite.
        auto big = [&](int x, const char* text, bool selected) {
            fb.panel(x - 3, 34, 30, 24, selected ? mix(t.ui, t.accent, 80) : mix(t.ui, t.ui_text, 20),
                     selected ? t.accent : mix(t.ui, t.ui_text, 60));
            for (int c = 0; c < 2; ++c)
                if (const char* g = glyph_for(text[c]))
                    for (int gy = 0; gy < 5; ++gy)
                        for (int gx = 0; gx < 3; ++gx)
                            if (g[gy * 3 + gx] == '#') fb.fill_rect(x + c * 12 + gx * 3, 39 + gy * 3, 3, 3, t.ui_text);
        };
        big(28, hh, field_ == 0);
        fb.fill_rect(59, 42, 2, 2, t.ui_text);
        fb.fill_rect(59, 49, 2, 2, t.ui_text);
        big(67, mm, field_ == 1);
        fb.draw_text_centered(66, field_ == 0 ? "SET THE HOUR" : "SET THE MINUTES", mix(t.ui_text, t.ui, 60));
        fb.draw_text_centered(78, "CHANGES LOCAL TIME ONLY", mix(t.ui_text, t.ui, 100));
        draw_footer(fb, t, field_ == 0 ? "A: +1 B: NEXT C: BACK" : "A: +1 B: SAVE C: BACK");
    }

private:
    int hour_ = 0, minute_ = 0;
    int field_ = 0;  // 0 hours, 1 minutes
};

// Debug: a list of forms to jump to (next forms or earlier ones).
std::unique_ptr<Scene> make_form_picker(Game& g, bool forward) {
    auto forms = [&g, forward] {
        const Pack& pack = g.pack();
        std::vector<int> out;
        auto add = [&out](int s) {
            if (s >= 0 && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
        };
        if (forward) {
            for (const EvolveRule& r : g.species().evolve) add(r.to);
        } else {
            for (size_t s = 0; s < pack.species.size(); ++s)
                for (const EvolveRule& r : pack.species[s].evolve)
                    if (r.to == g.pet().species) add(int(s));
        }
        return out;
    };
    auto rows = [&g, forms] {
        std::vector<Row> out;
        for (int s : forms())
            out.push_back({g.pack().species[s].name, stage_label(g.pack().species[s].stage), g.pack().theme.ui_text});
        return out;
    };
    auto detail = [forward](int) {
        return std::string(forward ? "B: EVOLVE INTO THIS FORM NOW" : "B: GO BACK TO THIS FORM NOW");
    };
    auto pick = [&g, forms, forward](int row) {
        std::vector<int> f = forms();
        if (row >= int(f.size())) return;
        change_form(g.pack(), g.pet(), f[row], g.now());
        g.sfx(Game::Sfx::Evolve);
        g.toast((forward ? "EVOLVED: " : "DEVOLVED: ") + g.species().name);
        g.save();
        g.pop();
    };
    return std::make_unique<ListScene>(g, forward ? "EVOLVE" : "DEVOLVE", rows, detail, pick,
                                       forward ? "NO NEXT FORM" : "NO EARLIER FORM", true);
}

// Debug tools for trying things quickly: change form, wake, fill, and so on.
std::unique_ptr<Scene> make_debug_scene(Game& g) {
    static const char* const kRows[] = {"EVOLVE",   "DEVOLVE", "WAKE UP",    "FILL HEARTS", "HEAL + CLEAN",
                                        "LEVEL UP", "AGE +1H", "ITEMS +5",   "QUEST +1 ZONE", "NEW EGG"};
    auto rows = [&g] {
        Color c = g.pack().theme.ui_text;
        const Pet& pet = g.pet();
        std::vector<Row> out;
        for (const char* label : kRows) out.push_back({label, "", c});
        out[2].right = pet.asleep ? "ASLEEP" : "AWAKE";
        out[5].right = "LV" + std::to_string(pet.level);
        out[8].right = "FLOOR " + std::to_string(pet.run.floor);
        return out;
    };
    auto detail = [](int row) {
        static const char* const kInfo[] = {
            "PICK A NEXT FORM AND EVOLVE NOW",  "PICK AN EARLIER FORM TO GO BACK TO", "STAYS AWAKE FOR 2 HOURS",
            "HUNGER AND STRENGTH TO FULL",      "CURE SICKNESS, CLEAR POOP",          "GAIN ONE LEVEL",
            "ONE MORE HOUR IN THIS FORM (FOR TIMED EVOLUTIONS)", "5 OF EVERY ITEM INTO THE BAG",
            "SKIP THE QUEST AHEAD 10 FLOORS",   "REPLACE THIS PET WITH A NEW EGG"};
        return std::string(kInfo[row]);
    };
    auto pick = [&g](int row) {
        const Pack& pack = g.pack();
        Pet& pet = g.pet();
        bool egg = g.species().stage == Stage::Egg;
        switch (row) {
            case 0: g.push(make_form_picker(g, true)); return;
            case 1: g.push(make_form_picker(g, false)); return;
            case 2:
                if (!pet.asleep) return g.toast("ALREADY AWAKE");
                pet.awake_until_ms = g.now() + 2 * 3600 * 1000;
                pet.asleep = false;
                pet.lights_on = true;
                pet.lights_call = Call{};
                g.toast("WOKE UP");
                break;
            case 3:
                if (egg) return g.toast("STILL AN EGG");
                pet.hunger = uint8_t(max_hearts(pack, pet) * 2);
                pet.strength = uint8_t(max_hearts(pack, pet));
                pet.hunger_call = pet.strength_call = Call{};
                g.toast("HEARTS FULL");
                break;
            case 4:
                pet.sick = false;
                pet.poop = 0;
                g.toast("HEALED AND CLEAN");
                break;
            case 5: {
                std::vector<std::string> msgs;
                gain_xp(pack, pet, xp_to_next(pet.level), msgs);
                g.toast("LEVEL " + std::to_string(pet.level));
                break;
            }
            case 6:
                pet.stage_ms = pet.stage_ms > 3600 * 1000 ? pet.stage_ms - 3600 * 1000 : 0;
                g.toast("+1 HOUR IN FORM");
                break;
            case 7:
                for (size_t i = 0; i < pack.items.size(); ++i) add_item(g.home(), int(i), 5);
                g.toast("+5 OF EVERYTHING");
                break;
            case 8:
                pet.run.floor = (zone_of(pet.run.floor) + 1) * kBossEvery + 1;
                pet.run.best = std::max(pet.run.best, pet.run.floor - 1);
                pet.run.col = -1;
                g.toast("ZONE " + std::to_string(zone_of(pet.run.floor) + 1));
                break;
            default:
                g.new_egg();
                g.toast("NEW EGG");
                break;
        }
        g.save();
    };
    return std::make_unique<ListScene>(g, "DEBUG", rows, detail, pick, "", true);
}

}  // namespace

std::unique_ptr<Scene> make_moves_scene(Game& g) {
    auto rows = [&g] {
        std::vector<Row> out;
        const Pack& pack = g.pack();
        std::vector<int> eq = equipped_moves(pack, g.pet());
        for (int m : learned_moves(pack, g.pet())) {
            bool on = std::find(eq.begin(), eq.end(), m) != eq.end();
            out.push_back({pack.moves[m].name, on ? "EQUIPPED" : "-", on ? pack.theme.ui_text : rgb(0x6a, 0x6e, 0x80)});
        }
        return out;
    };
    auto detail = [&g](int row) {
        const Pack& pack = g.pack();
        std::vector<int> known = learned_moves(pack, g.pet());
        if (row >= int(known.size())) return std::string();
        const Move& m = pack.moves[known[row]];
        std::string s = std::string(minigame_name(m.game)) + " POWER " + std::to_string(m.power) + ". B: TOGGLE (UP TO 3)";
        for (char& c : s) c = char(toupper(static_cast<unsigned char>(c)));
        return s;
    };
    auto pick = [&g](int row) {
        std::vector<int> known = learned_moves(g.pack(), g.pet());
        if (row >= int(known.size())) return;
        std::string msg;
        toggle_move(g.pack(), g.pet(), known[row], msg);
        g.toast(msg);
    };
    return std::make_unique<ListScene>(g, "MOVES", rows, detail, pick, "NO MOVES YET");
}

std::unique_ptr<Scene> make_settings_scene(Game& game) {
    Game* g = &game;
    auto rows = [g] {
        const Pack& p = g->pack();
        Color c = p.theme.ui_text;
        std::string pack = g->pack_names()[size_t(g->pack_index())], version = p.rosters[size_t(g->roster_index())].name;
        for (std::string* s : {&pack, &version})
            for (char& ch : *s) ch = char(toupper(static_cast<unsigned char>(ch)));
        char clock[6];
        int m = g->local_minute();
        std::snprintf(clock, sizeof clock, "%02d:%02d", m / 60, m % 60);
        return std::vector<Row>{{"PACK", pack, c},  {"VERSION", version, c}, {"MOVES", "", c},
                                {"SOUND", g->sound_on() ? "ON" : "OFF", c}, {"CLOCK", clock, c}, {"DEBUG", "", c},
                                {"ABOUT", "", c}};
    };
    auto detail = [g](int row) {
        switch (row) {
            case 0: return std::string(g->pack_names().size() > 1 ? "SWITCH TO ANOTHER INSTALLED PACK" : "ONLY ONE PACK INSTALLED");
            case 1: return std::string(g->pack().rosters.size() > 1 ? "B: NEXT VERSION. USED FOR NEW EGGS" : "THIS PACK HAS ONE VERSION");
            case 2: return std::string("CHOOSE YOUR 3 EQUIPPED MOVES");
            case 3: return std::string("B: TURN BEEPS AND JINGLES ON OR OFF");
            case 4: return std::string("SET THE TIME OF DAY (BEDTIMES, SKY)");
            case 5: return std::string("TEST TOOLS: EVOLVE, WAKE, FILL...");
            default: return std::string("PACK DETAILS");
        }
    };
    auto pick = [g](int row) {
        switch (row) {
            case 0: g->push(make_pack_picker(*g)); break;
            case 1:
                g->set_roster(g->roster_index() + 1);
                g->toast("NEXT EGG: " + g->pack().rosters[size_t(g->roster_index())].name);
                break;
            case 2:
                if (g->species().stage == Stage::Egg) g->toast("STILL AN EGG");
                else g->push(make_moves_scene(*g));
                break;
            case 3:
                g->set_sound(!g->sound_on());
                g->toast(g->sound_on() ? "SOUND ON" : "SOUND OFF");
                break;
            case 4: g->push(std::make_unique<ClockScene>(*g)); break;
            case 5: g->push(make_debug_scene(*g)); break;
            default: g->push(make_about(*g)); break;
        }
    };
    return std::make_unique<ListScene>(game, "SETTINGS", rows, detail, pick, "", true);
}

}  // namespace vpet
