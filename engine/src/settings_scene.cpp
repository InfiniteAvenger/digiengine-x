// Hidden settings (hold A+C for 3 seconds): switch packs and versions, pick
// equipped moves, and see pack info. Built on the shared list screen.
#include <algorithm>
#include "list_scene.h"
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
    auto detail = [](int) { return std::string("VPET ENGINE - OPEN SOURCE"); };
    return std::make_unique<ListScene>(g, "ABOUT", rows, detail, [](int) {}, "", true);
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
        return std::vector<Row>{{"PACK", pack, c}, {"VERSION", version, c}, {"MOVES", "", c}, {"ABOUT", "", c}};
    };
    auto detail = [g](int row) {
        switch (row) {
            case 0: return std::string(g->pack_names().size() > 1 ? "SWITCH TO ANOTHER INSTALLED PACK" : "ONLY ONE PACK INSTALLED");
            case 1: return std::string(g->pack().rosters.size() > 1 ? "B: NEXT VERSION. USED FOR NEW EGGS" : "THIS PACK HAS ONE VERSION");
            case 2: return std::string("CHOOSE YOUR 3 EQUIPPED MOVES");
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
            default: g->push(make_about(*g)); break;
        }
    };
    return std::make_unique<ListScene>(game, "SETTINGS", rows, detail, pick, "", true);
}

}  // namespace vpet
