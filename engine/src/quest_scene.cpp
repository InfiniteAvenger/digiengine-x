// Infinite quest. Each zone of 10 floors is an overhead map of winding paths
// with the boss at the top; the pet walks it one floor at a time, picking a
// branch where the path splits: a fight, a campfire to rest, or a forage
// spot. HP carries between floors and going home keeps the run. Fainting ends the
// run (back to floor 1) and the pet comes home sick, but keeps its level,
// items and everything at home. The rules live in vpet/combat.h.
#include <algorithm>
#include <cctype>
#include <deque>
#include "icons.h"
#include "scenes.h"
#include "vpet/combat.h"
#include "vpet/minigame.h"
#include "vpet/rpg.h"

namespace vpet {
namespace {

constexpr int kArenaTop = 12;
constexpr int kHorizon = 66;
constexpr int kPanelY = 80;
constexpr uint64_t kMessageMs = 1400;

constexpr uint64_t kWalkMs = 700;  // walking one row up the map

enum class Phase { Lobby, Map, Walk, Messages, Choose, Items, Mini, Cleared, Campfire, Over };

// --- Overhead map look ----------------------------------------------------------

constexpr int kMapTop = 12, kMapPanelY = 100;
constexpr int kRowGap = 22;    // pixels between floors on the map
constexpr int kStandY = 90;    // where the pet's row sits on screen
constexpr int kEntryRow = -1;  // the zone entrance, below the first floor

// Each zone gets a biome, cycling as the run goes deeper.
struct Biome {
    Color ground, patch, path, path_edge, plant, plant_dark, trunk, speck;
};
const Biome& biome(int zone) {
    static const Biome kBiomes[] = {
        {rgb(0x4c, 0x9a, 0x52), rgb(0x3e, 0x86, 0x48), rgb(0xd9, 0xb8, 0x7a), rgb(0x9a, 0x7a, 0x4a),
         rgb(0x2f, 0x6e, 0x3c), rgb(0x22, 0x52, 0x2e), rgb(0x6b, 0x4a, 0x2b), rgb(0xff, 0xcd, 0x75)},  // meadow
        {rgb(0xd9, 0xb0, 0x6a), rgb(0xc8, 0x9c, 0x58), rgb(0xf2, 0xdc, 0xa4), rgb(0xa8, 0x82, 0x4c),
         rgb(0x4c, 0x9a, 0x52), rgb(0x2f, 0x6e, 0x3c), rgb(0x8b, 0x6a, 0x45), rgb(0x8b, 0x6a, 0x45)},  // desert
        {rgb(0xdc, 0xe8, 0xf0), rgb(0xc4, 0xd4, 0xe2), rgb(0xa4, 0xb8, 0xcc), rgb(0x7a, 0x8e, 0xa6),
         rgb(0x2a, 0x6a, 0x6a), rgb(0x1e, 0x4c, 0x52), rgb(0x56, 0x4a, 0x40), rgb(0x94, 0xb0, 0xc2)},  // snow
        {rgb(0x4a, 0x3a, 0x48), rgb(0x3c, 0x2e, 0x3c), rgb(0x86, 0x64, 0x64), rgb(0x2a, 0x20, 0x2a),
         rgb(0x26, 0x1e, 0x28), rgb(0x1a, 0x14, 0x1c), rgb(0x26, 0x1e, 0x28), rgb(0xef, 0x7d, 0x57)},  // ashland
    };
    return kBiomes[zone % 4];
}

uint32_t hash3(int a, int b, uint32_t c) {
    uint32_t h = uint32_t(a) * 374761393u + uint32_t(b) * 668265263u + c * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Calls fn(x, y) for each point from (x0, y0) to (x1, y1), one pixel apart.
template <typename Fn>
void along(int x0, int y0, int x1, int y1, Fn fn) {
    int steps = std::max({std::abs(x1 - x0), std::abs(y1 - y0), 1});
    for (int i = 0; i <= steps; ++i) fn(i, steps, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
}

// A dirt road between two points: 4 pixels wide with a darker edge.
void draw_road(Framebuffer& fb, int x0, int y0, int x1, int y1, Color fill, Color edge) {
    along(x0, y0, x1, y1, [&](int, int, int x, int y) { fb.fill_rect(x - 2, y - 2, 5, 5, edge); });
    along(x0, y0, x1, y1, [&](int, int, int x, int y) { fb.fill_rect(x - 1, y - 1, 3, 3, fill); });
}

// Faint footprints for paths further ahead.
void draw_footprints(Framebuffer& fb, int x0, int y0, int x1, int y1, Color c) {
    along(x0, y0, x1, y1, [&](int i, int steps, int x, int y) {
        if (i > 7 && i < steps - 7 && i % 4 == 0) fb.set(x, y, c);
    });
}

// Marching dots along a road, for the way the pet is about to go.
void draw_march(Framebuffer& fb, int x0, int y0, int x1, int y1, Color c, uint64_t now) {
    int phase = int(now / 90) % 4;
    along(x0, y0, x1, y1, [&](int i, int steps, int x, int y) {
        if (i > 7 && i < steps - 7 && (i + 4 - phase) % 4 == 0) fb.fill_rect(x, y, 1, 1, c);
    });
}

// A filled circle of radius r around the center of pixel (cx, cy).
void disc(Framebuffer& fb, int cx, int cy, float r, Color c) {
    int ri = int(r) + 1;
    for (int dy = -ri; dy <= ri; ++dy)
        for (int dx = -ri; dx <= ri; ++dx)
            if (float(dx * dx + dy * dy) <= r * r + r * 0.8f) fb.set(cx + dx, cy + dy, c);  // rounder pixel circles
}

// A flat ellipse, for shadows on the ground.
void shadow(Framebuffer& fb, int cx, int cy, int rx, Color c) {
    fb.fill_rect(cx - rx + 1, cy - 1, rx * 2 - 1, 3, c);
    fb.fill_rect(cx - rx, cy, rx * 2 + 1, 1, c);
}

// The rim color of each kind of node on the map.
Color node_rim(Node n) {
    switch (n) {
        case Node::Battle: return rgb(0xd8, 0x4a, 0x4a);
        case Node::Campfire: return rgb(0xef, 0x8a, 0x3c);
        case Node::Forage: return rgb(0x48, 0xb0, 0x5c);
        case Node::Boss: return rgb(0xff, 0xcd, 0x4c);
    }
    return rgb(0xd8, 0x4a, 0x4a);
}

class QuestScene : public Scene {
public:
    explicit QuestScene(Game& game) : Scene(game) {}

    void on_press(Button b) override {
        switch (phase_) {
            case Phase::Lobby:
                if (b == BTN_B) open_map();
                else if (b == BTN_C) game_.pop();
                break;
            case Phase::Map:
                if (b == BTN_A) pick_ = (pick_ + 1) % int(choices_.size());
                else if (b == BTN_B) {
                    phase_ = Phase::Walk;
                    walk_from_ = game_.now();
                } else game_.pop();  // the run waits where it is
                break;
            case Phase::Walk: break;
            case Phase::Campfire:
                if (b == BTN_B) open_map();
                else if (b == BTN_C) game_.pop();
                break;
            case Phase::Messages:
                if (b != BTN_C) next_message();
                break;
            case Phase::Choose:
                if (b == BTN_A) cursor_ = (cursor_ + 1) % int(moves_.size() + 1);
                else if (b == BTN_B) choose();
                break;
            case Phase::Items:
                if (b == BTN_A) item_cursor_ = (item_cursor_ + 1) % int(items_.size());
                else if (b == BTN_B) use_item(items_[item_cursor_]);
                else phase_ = Phase::Choose;
                break;
            case Phase::Mini: mini_press(b); break;
            case Phase::Cleared:
                if (b == BTN_B) open_map();
                else if (b == BTN_C) game_.pop();
                break;
            case Phase::Over: game_.pop(); break;
        }
    }

    void update(uint64_t now) override {
        if (phase_ == Phase::Messages && now >= msg_until_) next_message();
        if (phase_ == Phase::Mini) mini_update(now);
        if (phase_ == Phase::Walk && now >= walk_from_ + kWalkMs) take_node(target().type);
    }

    void draw(Framebuffer& fb) override;

private:
    void draw_map(Framebuffer& fb);

    // --- Flow -----------------------------------------------------------------

    void open_map() {
        Run& run = game_.pet().run;
        if (!run.map_seed) run.map_seed = game_.rand() | 1u;  // a fresh run gets fresh maps
        map_ = make_zone_map(run.map_seed, zone_of(run.floor));
        choices_ = map_choices(map_, run.floor, run.col);
        // Start the cursor on the branch straight ahead (or nearest to it).
        pick_ = 0;
        for (size_t i = 1; i < choices_.size(); ++i)
            if (std::abs(choices_[i] - run.col) < std::abs(choices_[size_t(pick_)] - run.col)) pick_ = int(i);
        phase_ = Phase::Map;
    }

    int target_col() const { return choices_[size_t(pick_)]; }
    const MapNode& target() const { return map_.at[row_of(game_.pet().run.floor)][target_col()]; }

    // The floor is done: the pet now stands on the node it walked to.
    void advance() {
        Run& run = game_.pet().run;
        run.col = target_col();
        ++run.floor;
    }

    void take_node(Node n) {
        const Pack& pack = game_.pack();
        Pet& pet = game_.pet();
        switch (n) {
            case Node::Battle:
            case Node::Boss: start_floor(); break;
            case Node::Campfire: {
                Stats st = pet_stats(pack, pet);
                int hp = pet.run.hp < 0 ? st.hp : pet.run.hp;
                int healed = std::min(st.hp, hp + st.hp * kCampfireHealPercent / 100);
                pet.run.hp = healed;
                advance();  // resting moves the run along
                say({"A QUIET CAMPFIRE...", "+" + std::to_string(healed - hp) + " HP"}, Phase::Campfire);
                break;
            }
            case Node::Forage: {
                std::vector<std::string> msgs{"SEARCHING THE AREA..."};
                for (int item : forage(pack, pet.run.floor, game_.rand())) {
                    add_item(game_.home(), item, 1);
                    msgs.push_back("FOUND " + pack.items[item].name + "!");
                }
                advance();
                open_map();  // prepare the next choice, shown after the messages
                say(msgs, Phase::Map);
                break;
            }
        }
    }

    void start_floor() {
        const Pack& pack = game_.pack();
        Pet& pet = game_.pet();
        if (pet.hunger < 1) {
            game_.toast("TOO HUNGRY");
            game_.pop();
            return;
        }
        moves_ = equipped_moves(pack, pet);
        if (moves_.empty() || pack.enemies.empty()) {
            game_.toast(moves_.empty() ? "NO MOVES YET" : "NO ENEMIES");
            game_.pop();
            return;
        }
        pet.hunger = uint8_t(pet.hunger - 1);  // each floor costs half a heart
        ps_ = pet_stats(pack, pet);
        if (pet.run.hp < 0 || pet.run.hp > ps_.hp) pet.run.hp = ps_.hp;
        EnemyPick pick = pick_enemy(pack, pet.run.floor, game_.rand());
        enemy_ = pick.index;
        boss_ = pick.boss;
        const Enemy& e = pack.enemies[enemy_];
        es_ = enemy_stats(e, pet.run.floor, boss_);
        ehp_ = es_.hp;
        cursor_ = 0;
        say({"FLOOR " + std::to_string(pet.run.floor) + (boss_ ? " - BOSS!" : ""), "A WILD " + e.name + " APPEARS!"},
            Phase::Choose);
    }

    void say(std::vector<std::string> msgs, Phase then) {
        queue_.assign(msgs.begin(), msgs.end());
        after_ = then;
        phase_ = Phase::Messages;
        msg_until_ = game_.now() + kMessageMs;
        if (queue_.empty()) phase_ = then;
    }

    void next_message() {
        if (!queue_.empty()) queue_.pop_front();
        msg_until_ = game_.now() + kMessageMs;
        if (queue_.empty()) phase_ = after_;
    }

    void choose() {
        if (cursor_ < int(moves_.size())) {
            start_mini(game_.pack().moves[moves_[cursor_]].game);
            return;
        }
        items_.clear();
        const Pack& pack = game_.pack();
        for (size_t i = 0; i < pack.items.size(); ++i)
            if (!pack.items[i].material && pack.items[i].effect.in_quest() && count(game_.home(), int(i)) > 0)
                items_.push_back(int(i));
        if (items_.empty()) return game_.toast("NO QUEST ITEMS");
        item_cursor_ = 0;
        phase_ = Phase::Items;
    }

    void use_item(int item) {
        const Pack& pack = game_.pack();
        Pet& pet = game_.pet();
        const ItemDef& it = pack.items[item];
        std::vector<std::string> msgs{"USED " + it.name + "!"};
        if (it.effect.heal) {
            int before = pet.run.hp;
            pet.run.hp = std::min(ps_.hp, pet.run.hp + ps_.hp * it.effect.heal / 100);
            msgs.push_back("+" + std::to_string(pet.run.hp - before) + " HP");
        }
        if (it.effect.atk_buff) {
            pet.run.atk_buff += it.effect.atk_buff;
            msgs.push_back("ATTACK UP!");
        }
        if (it.effect.def_buff) {
            pet.run.def_buff += it.effect.def_buff;
            msgs.push_back("DEFENSE UP!");
        }
        add_item(game_.home(), item, -1);
        cursor_ = 0;
        resolve(-1, 0, msgs);  // using an item gives the enemy a free turn
    }

    // move < 0: the pet used an item instead of attacking.
    void resolve(int move, float mult, std::vector<std::string> msgs) {
        const Pack& pack = game_.pack();
        Pet& pet = game_.pet();
        const Species& s = game_.species();
        const Enemy& e = pack.enemies[enemy_];

        auto pet_turn = [&] {
            if (move < 0) return;
            const Move& m = pack.moves[move];
            Hit h = resolve_hit(pack, m.power, mult, {ps_, s.attribute, pet.run.atk_buff}, {es_, e.attribute, 0});
            game_.sfx(mult >= 1.1f ? Game::Sfx::Hit : Game::Sfx::Miss);
            ehp_ -= h.damage;
            bool dice = mini_.type() == MiniGame::Dice;
            const char* rating = dice && mini_.die() >= 6 ? "CRITICAL! "
                                 : dice && mini_.die() <= 2 ? "GLANCING. "
                                 : mult >= 1.4f            ? "PERFECT! "
                                 : mult >= 1.1f            ? "GREAT! "
                                 : mult < 0.75f            ? "WEAK... "
                                                           : "";
            msgs.push_back(s.name + " USED " + m.name + "!");
            const char* edge = h.advantage > 1.0f ? "ADVANTAGE! " : h.advantage < 1.0f ? "RESISTED. " : "";
            msgs.push_back(std::string(rating) + edge + std::to_string(h.damage) + " DAMAGE");
        };
        auto enemy_turn = [&] {
            const Move& m = pack.moves[e.moves[game_.rand() % e.moves.size()]];
            float roll = 0.8f + float(game_.rand() % 41) / 100.0f;
            Hit h = resolve_hit(pack, m.power, roll, {es_, e.attribute, 0}, {ps_, s.attribute, pet.run.def_buff});
            pet.run.hp -= h.damage;
            msgs.push_back(e.name + " USED " + m.name + "!");
            msgs.push_back(s.name + " TOOK " + std::to_string(h.damage));
        };

        if (move >= 0 && pet_moves_first(ps_, es_)) {
            pet_turn();
            if (ehp_ > 0) enemy_turn();
        } else {
            enemy_turn();
            if (pet.run.hp > 0) pet_turn();
        }

        if (ehp_ <= 0) win(msgs);
        else if (pet.run.hp <= 0) lose(msgs);
        else say(msgs, Phase::Choose);
    }

    void win(std::vector<std::string>& msgs) {
        const Pack& pack = game_.pack();
        Pet& pet = game_.pet();
        const Enemy& e = pack.enemies[enemy_];
        int floor = pet.run.floor;
        msgs.push_back(e.name + " WAS DEFEATED!");
        uint32_t xp = floor_xp(floor);
        msgs.push_back("+" + std::to_string(xp) + " XP");
        gain_xp(pack, pet, xp, msgs);
        for (int item : roll_drops(pack, e, floor, [this] { return game_.rand(); })) {
            add_item(game_.home(), item, 1);
            msgs.push_back("GOT " + pack.items[item].name + "!");
        }
        if (pet.run.atk_buff) --pet.run.atk_buff;
        if (pet.run.def_buff) --pet.run.def_buff;
        pet.run.best = std::max(pet.run.best, floor);
        advance();
        ps_ = pet_stats(pack, pet);  // a level-up raises max HP
        game_.sfx(Game::Sfx::Clear);
        say(msgs, Phase::Cleared);
    }

    void lose(std::vector<std::string>& msgs) {
        Pet& pet = game_.pet();
        msgs.push_back(game_.species().name + " FAINTED!");
        msgs.push_back("RUN ENDED ON FLOOR " + std::to_string(pet.run.floor));
        pet.run = Run{1, pet.run.best, -1, 0, 0};
        pet.sick = true;
        game_.sfx(Game::Sfx::Fainted);
        say(msgs, Phase::Over);
    }

    // --- Minigames ----------------------------------------------------------------

    void start_mini(MiniGame g) {
        mini_.start(g, game_.now(), game_.rand());
        phase_ = Phase::Mini;
    }

    void mini_press(Button b) {
        mini_.press(b, game_.now(), game_.rand());
        if (mini_.done()) resolve(moves_[cursor_], mini_.mult(), {});
    }

    void mini_update(uint64_t now) {
        mini_.update(now);
        if (mini_.done()) resolve(moves_[cursor_], mini_.mult(), {});
    }

    Phase phase_ = Phase::Lobby, after_ = Phase::Choose;
    std::deque<std::string> queue_;
    uint64_t msg_until_ = 0;

    ZoneMap map_;               // the current zone
    std::vector<int> choices_;  // columns the pet can walk to next
    int pick_ = 0;              // index into choices_
    uint64_t walk_from_ = 0;
    int enemy_ = -1;
    bool boss_ = false;
    Stats es_, ps_;
    int ehp_ = 0;
    std::vector<int> moves_, items_;
    int cursor_ = 0, item_cursor_ = 0;
    Minigame mini_;
};

void QuestScene::draw(Framebuffer& fb) {
    const Pack& pack = game_.pack();
    const Theme& t = pack.theme;
    const Pet& pet = game_.pet();
    const Species& s = game_.species();
    uint64_t now = game_.now();
    Color dim = mix(t.ui, t.ui_text, 50);
    Stats ps = pet_stats(pack, pet);
    int hp = pet.run.hp < 0 ? ps.hp : pet.run.hp;
    if (phase_ == Phase::Map || phase_ == Phase::Walk) return draw_map(fb);

    draw_scenery(fb, t, kArenaTop, kHorizon, kPanelY, now, game_.local_minute(), false);

    fb.fill_rect(0, 0, kScreenW, kArenaTop, t.ui);
    fb.draw_text(4, 3, "FLOOR " + std::to_string(pet.run.floor), t.accent);
    std::string best = "BEST " + std::to_string(pet.run.best);
    fb.draw_text(kScreenW - 4 - Framebuffer::text_width(best), 3, best, t.ui_text);

    bool fighting = phase_ != Phase::Lobby && phase_ != Phase::Over && phase_ != Phase::Map &&
                    phase_ != Phase::Campfire && enemy_ >= 0;
    int bob = int((now / 350) % 2);
    if (fighting && ehp_ > 0) {
        const Enemy& e = pack.enemies[enemy_];
        fb.draw_sprite(e.sprite, 0, 80, kArenaTop + 10 + bob, 2);
        // The panel grows to fit long names, stopping short of the enemy sprite.
        std::string name = e.name.substr(0, 18);
        int inner = std::max(50, Framebuffer::text_width(name));
        fb.panel(3, 15, inner + 4, 14, mix(t.ui, t.bg_top, 40), mix(t.ui, t.ui_text, 70));
        fb.draw_text(5, 17, name, boss_ ? t.accent : t.ui_text);
        if (!e.attribute.empty()) {  // 3-letter attribute tag, right-aligned, when it fits
            std::string tag = e.attribute.substr(0, 3);
            for (char& c : tag) c = char(toupper(static_cast<unsigned char>(c)));
            if (Framebuffer::text_width(name) + Framebuffer::text_width(tag) + 3 <= inner)
                fb.draw_text(5 + inner - Framebuffer::text_width(tag), 17, tag, dim);
        }
        draw_bar(fb, 5, 24, inner, 3, ehp_, es_.hp, rgb(0xe0, 0x40, 0x50), dim);
    }

    int pet_x = phase_ == Phase::Lobby ? (kScreenW - 32) / 2 : 12;
    fb.draw_sprite(s.sprite, phase_ == Phase::Over ? 0 : bob, pet_x, kHorizon - 28, 2, true);
    if (phase_ != Phase::Lobby) {
        fb.panel(63, 56, 54, 14, mix(t.ui, t.bg_top, 40), mix(t.ui, t.ui_text, 70));
        std::string hp_text = std::to_string(std::max(hp, 0)) + "/" + std::to_string(ps.hp);
        fb.draw_text(65, 58, "LV" + std::to_string(pet.level), t.accent);
        fb.draw_text(115 - Framebuffer::text_width(hp_text), 58, hp_text, t.ui_text);
        draw_bar(fb, 65, 65, 50, 3, hp, ps.hp, rgb(0x38, 0xb7, 0x64), dim);
    }

    fb.fill_rect(0, kPanelY, kScreenW, kScreenH - kPanelY, t.ui);
    fb.fill_rect(0, kPanelY, kScreenW, 1, mix(t.ui, t.accent, 60));
    int y = kPanelY + 5;
    switch (phase_) {
        case Phase::Lobby:
            fb.draw_text(6, y, "HP " + std::to_string(hp) + "/" + std::to_string(ps.hp), t.ui_text);
            fb.draw_text(6, y + 9, "FIGHTS COST HALF A HEART", dim);
            fb.draw_text(6, y + 18, "LOSE AND THE RUN RESETS", dim);
            draw_footer(fb, t, "B: ENTER  C: HOME");
            break;
        case Phase::Map:
        case Phase::Walk: break;  // drawn by draw_map
        case Phase::Campfire:
            fb.draw_text_centered(y + 2, "RESTED BY THE FIRE", t.accent);
            fb.draw_text_centered(y + 12, "HP " + std::to_string(hp) + "/" + std::to_string(ps.hp), t.ui_text);
            draw_footer(fb, t, "B: ONWARD  C: PAUSE + HOME");
            break;
        case Phase::Messages: {
            if (queue_.empty()) break;
            std::vector<std::string> lines = wrap_text(queue_.front(), 28);
            for (size_t l = 0; l < lines.size() && l < 3; ++l) fb.draw_text(6, y + int(l) * 9, lines[l], t.ui_text);
            if ((now / 300) % 2) fb.draw_text(kScreenW - 9, kScreenH - 9, ">", t.accent);
            break;
        }
        case Phase::Choose: {
            int n = int(moves_.size()) + 1;
            // Two columns of 59px: icon, then up to 12 letters, all inside the
            // highlight box (longer names are cut).
            constexpr int kColW = 59, kMaxChars = 12;
            for (int i = 0; i < n; ++i) {
                int ox = 1 + (i % 2) * kColW, oy = y + (i / 2) * 11;
                bool item = i >= int(moves_.size());
                std::string label = item ? "ITEM" : pack.moves[moves_[i]].name.substr(0, kMaxChars);
                bool sel = i == cursor_;
                if (sel) fb.panel(ox, oy - 3, kColW, 11, mix(t.ui, t.accent, 60), t.accent);
                fb.draw_sprite(icons().ui, item ? kUiItem : move_icon(pack.moves[moves_[i]].game), ox + 1, oy - 2);
                fb.draw_text(ox + 10, oy, label, sel ? t.accent : t.ui_text);
            }
            std::string info = cursor_ < int(moves_.size())
                                   ? std::string(minigame_name(pack.moves[moves_[cursor_]].game)) + "  POWER " +
                                         std::to_string(pack.moves[moves_[cursor_]].power)
                                   : "HEAL OR BUFF";
            for (char& c : info) c = char(toupper(c));
            fb.draw_text(6, kScreenH - 9, info, mix(t.ui, t.ui_text, 130));
            break;
        }
        case Phase::Items:
            for (size_t i = 0; i < items_.size() && i < 3; ++i) {
                size_t idx = items_.size() > 3 && size_t(item_cursor_) >= 3 ? item_cursor_ - 2 + i : i;
                if (idx >= items_.size()) break;
                const ItemDef& it = pack.items[items_[idx]];
                bool sel = int(idx) == item_cursor_;
                if (sel) fb.draw_text(2, y + int(i) * 9, ">", t.accent);
                draw_item_icon(fb, pack, items_[idx], 7, y + int(i) * 9 - 2);
                fb.draw_text(17, y + int(i) * 9, it.name, sel ? t.accent : t.ui_text);
                std::string n = "X" + std::to_string(count(game_.home(), items_[idx]));
                fb.draw_text(kScreenW - 6 - Framebuffer::text_width(n), y + int(i) * 9, n, t.ui_text);
            }
            fb.draw_text(6, kScreenH - 9, "B: USE  C: BACK", dim);
            break;
        case Phase::Mini: mini_.draw(fb, t, kPanelY + 4, now); break;
        case Phase::Cleared:
            fb.draw_text_centered(y, "FLOOR " + std::to_string(pet.run.floor - 1) + " CLEAR!", t.accent);
            fb.draw_text_centered(y + 10, "HUNGER LEFT", dim);
            draw_hearts(fb, (kScreenW - hearts_width(max_hearts(pack, pet))) / 2, y + 18, pet.hunger, max_hearts(pack, pet));
            draw_footer(fb, t, "B: NEXT FLOOR  C: HOME");
            break;
        case Phase::Over:
            fb.draw_text_centered(y + 2, "RUN OVER", t.accent);
            fb.draw_text_centered(y + 12, "BEST FLOOR " + std::to_string(pet.run.best), t.ui_text);
            draw_footer(fb, t, "ANY BUTTON: HOME");
            break;
    }
}

// The zone seen from above: floors run bottom to top, the pet stands on the
// node it last cleared, and the map scrolls up a row as it walks on.
void QuestScene::draw_map(Framebuffer& fb) {
    const Pack& pack = game_.pack();
    const Theme& t = pack.theme;
    const Pet& pet = game_.pet();
    const Sprite& pet_sprite = game_.species().sprite;
    uint64_t now = game_.now();
    const int floor = pet.run.floor, zone = zone_of(floor), row = row_of(floor);
    const int stand = row == 0 ? kEntryRow : row - 1;
    const Biome& b = biome(zone);
    const uint32_t seed = pet.run.map_seed;

    // Walking slides the view up one row.
    int walked = phase_ == Phase::Walk ? int(std::min<uint64_t>(now - walk_from_, kWalkMs) * kRowGap / kWalkMs) : 0;
    int scroll = stand * kRowGap + walked;  // world y = screen y - scroll
    auto row_y = [&](int r) { return kStandY - r * kRowGap + scroll; };
    auto node_x = [&](int r, int c) {
        if (r == kEntryRow || r == kBossEvery - 1) return kScreenW / 2;
        return 15 + c * 30 + int(hash3(r, c, seed) % 7) - 3;
    };
    int stand_col = stand == kEntryRow ? 0 : pet.run.col >= 0 ? pet.run.col : choices_.empty() ? 0 : choices_[0];

    // Ground: broad soft blotches in three tones (smooth noise, not tiles),
    // darker toward the top and bottom edges.
    const Color light = mix(b.ground, rgb(0xff, 0xff, 0xff), 22);
    auto noise = [&](int x, int wy) {  // bilinear value noise on an 8-pixel grid, 0-255
        int gx = x >> 3, gy = (wy + 800) >> 3, fx = x & 7, fy = (wy + 800) & 7;
        auto v = [&](int a, int c) { return int(hash3(a, c, seed ^ 0x9e3779b9u) & 0xFF); };
        int top = v(gx, gy) * (8 - fx) + v(gx + 1, gy) * fx, bot = v(gx, gy + 1) * (8 - fx) + v(gx + 1, gy + 1) * fx;
        return (top * (8 - fy) + bot * fy) / 64;
    };
    for (int y = kMapTop; y < kMapPanelY; ++y) {
        int wy = y - scroll;
        for (int x = 0; x < kScreenW; ++x) {
            int n = noise(x, wy) + ((x ^ y) & 1) * 10;  // a touch of dither at the blotch edges
            Color c = n < 70 ? b.patch : n > 190 ? light : b.ground;
            int edge = std::min(y - kMapTop, kMapPanelY - 1 - y);
            if (edge < 4 && ((x + y) & 1 || edge < 2)) c = mix(c, rgb(0, 0, 0), 50 - edge * 10);
            fb.set(x, y, c);
        }
    }
    auto near_node = [&](int x, int wy) {
        for (int r = 0; r < kBossEvery; ++r)
            for (int c = 0; c < kMapCols; ++c)
                if (map_.at[r][c].used && std::abs(x - node_x(r, c)) < 12 &&
                    std::abs(wy - (kStandY - r * kRowGap)) < 12)
                    return true;
        return std::abs(x - kScreenW / 2) < 10 && std::abs(wy - (kStandY - kEntryRow * kRowGap)) < 10;
    };
    // Scenery: round trees with a shadow, grass tufts and little flowers.
    for (int cy = (kMapTop - scroll) / 10 - 1; cy * 10 < kMapPanelY - scroll + 10; ++cy) {
        for (int cx = 0; cx < kScreenW / 10; ++cx) {
            uint32_t h = hash3(cx + 100, cy + 1000, seed ^ 0x5bd1e995u);
            int x = cx * 10 + int(h >> 8) % 7, wy = cy * 10 + int(h >> 12) % 7, y = wy + scroll;
            if (near_node(x, wy)) continue;
            switch (h % 9) {
                case 0:  // a tree (a cactus or pine in other biomes, by color)
                    shadow(fb, x + 1, y + 2, 3, mix(b.patch, rgb(0, 0, 0), 60));
                    fb.set(x, y + 1, b.trunk);
                    fb.set(x, y + 2, b.trunk);
                    fb.fill_rect(x - 2, y - 4, 5, 5, b.plant);
                    fb.fill_rect(x - 3, y - 3, 7, 3, b.plant);
                    fb.fill_rect(x - 1, y - 5, 3, 1, b.plant);
                    fb.fill_rect(x + 1, y - 2, 2, 2, b.plant_dark);
                    fb.fill_rect(x - 2, y, 5, 1, b.plant_dark);
                    fb.set(x - 1, y - 4, mix(b.plant, rgb(0xff, 0xff, 0xff), 70));
                    fb.set(x - 2, y - 3, mix(b.plant, rgb(0xff, 0xff, 0xff), 50));
                    break;
                case 1:
                case 2:  // a grass tuft
                    fb.set(x - 1, y - 1, b.plant);
                    fb.set(x, y - 2, b.plant);
                    fb.set(x + 1, y - 1, b.plant);
                    fb.set(x, y - 1, b.plant_dark);
                    break;
                case 3:  // a flower
                    fb.set(x, y + 1, b.plant_dark);
                    fb.set(x - 1, y, b.speck);
                    fb.set(x + 1, y, b.speck);
                    fb.set(x, y - 1, b.speck);
                    fb.set(x, y, mix(b.speck, rgb(0x80, 0x40, 0x20), 120));
                    break;
                case 4: fb.set(x, y, b.speck); break;
                default: break;
            }
        }
    }

    // Paths: roads already walked (worn) and the ways on from here; faint
    // footprints for the paths further ahead.
    const int last = kBossEvery - 1;
    const Color worn = mix(b.path, b.ground, 110), worn_edge = mix(b.path_edge, b.ground, 110);
    auto link = [&](int r0, int c0, int r1, int c1) {
        int x0 = node_x(r0, c0), y0 = row_y(r0), x1 = node_x(r1, c1), y1 = row_y(r1);
        if (r1 <= stand) draw_road(fb, x0, y0, x1, y1, worn, worn_edge);
        else draw_footprints(fb, x0, y0, x1, y1, b.path_edge);
    };
    for (int r = 0; r < last; ++r)
        for (int c = 0; c < kMapCols; ++c)
            for (int n = 0; n < kMapCols; ++n)
                if (map_.at[r][c].used && (map_.at[r][c].next >> n & 1)) link(r, c, r + 1, n);
    for (int c = 0; c < kMapCols; ++c)
        if (map_.at[0][c].used) link(kEntryRow, 0, 0, c);
    int sx = node_x(stand, stand_col), sy = row_y(stand);
    for (size_t i = 0; i < choices_.size(); ++i) {
        bool sel = int(i) == pick_;
        if (phase_ == Phase::Walk && !sel) continue;
        int tx = node_x(row, choices_[i]), ty = row_y(row);
        draw_road(fb, sx, sy, tx, ty, sel ? mix(b.path, rgb(0xff, 0xff, 0xff), 60) : b.path, b.path_edge);
        if (sel) draw_march(fb, sx, sy, tx, ty, t.accent, now);
    }

    // Nodes: round tokens on the ground, rimmed by kind. Cleared ones are
    // grey; the boss is bigger with a gold rim; the pick gets a bouncing arrow.
    const Color face = rgb(0x2a, 0x2e, 0x46), outline = rgb(0x1a, 0x1c, 0x2c);
    const Color ground_shadow = mix(b.patch, rgb(0, 0, 0), 70);
    for (int r = 0; r <= last; ++r) {
        int y = row_y(r);
        if (y < kMapTop - 12 || y > kMapPanelY + 12) continue;
        for (int c = 0; c < kMapCols; ++c) {
            const MapNode& n = map_.at[r][c];
            if (!n.used) continue;
            int x = node_x(r, c);
            bool boss = n.type == Node::Boss, done = r <= stand;
            bool picked = !choices_.empty() && r == row && c == target_col() && phase_ == Phase::Map;
            float rad = boss ? 10.0f : 8.0f;
            Color rim = done ? rgb(0x6a, 0x6c, 0x78) : node_rim(n.type);
            if (picked && (now / 250) % 2) rim = mix(rim, rgb(0xff, 0xff, 0xff), 110);
            shadow(fb, x + 1, y + int(rad) + 1, int(rad) - 1, ground_shadow);
            if (boss && (now / 400) % 2) disc(fb, x, y, rad + 2.5f, mix(b.ground, t.accent, 120));
            disc(fb, x, y, rad, outline);
            disc(fb, x, y, rad - 1, rim);
            disc(fb, x, y - 1, rad - 2.5f, mix(rim, rgb(0xff, 0xff, 0xff), 90));  // lit top edge
            disc(fb, x, y, rad - 2.5f, done ? mix(face, rgb(0x60, 0x60, 0x68), 120) : face);
            // Cleared nodes show their icon as a grey silhouette.
            if (done) fb.draw_mask(icons().nodes, int(n.type), x - 4, y - 4, rgb(0x7a, 0x7c, 0x88));
            else fb.draw_sprite(icons().nodes, int(n.type), x - 4, y - 4);
        }
    }
    if (!choices_.empty() && phase_ == Phase::Map) {
        // An arrow bobbing over the chosen node.
        int x = node_x(row, target_col()), y = row_y(row) - (row == last ? 18 : 15) - int((now / 200) % 3 == 1);
        for (int k = 0; k < 4; ++k) fb.fill_rect(x - 3 + k, y + k, 7 - 2 * k, 1, outline);
        for (int k = 0; k < 3; ++k) fb.fill_rect(x - 2 + k, y + k, 5 - 2 * k, 1, t.accent);
    }

    // The pet, standing on its node or walking up the road.
    if (pet_sprite.valid()) {
        int tx = choices_.empty() ? sx : node_x(row, target_col()), ty = row_y(row);
        int px = sx, py = sy;
        if (phase_ == Phase::Walk) {
            int dt = int(std::min<uint64_t>(now - walk_from_, kWalkMs));
            px = sx + (tx - sx) * dt / int(kWalkMs);
            py = sy + (ty - sy) * dt / int(kWalkMs);
        }
        int frame = phase_ == Phase::Walk ? int(now / 150) % 2 : int(now / 350) % 2;
        shadow(fb, px, py + 4, pet_sprite.w / 2 - 2, ground_shadow);
        fb.draw_sprite(pet_sprite, frame, px - pet_sprite.w / 2, py - pet_sprite.h + 4 - (frame && phase_ == Phase::Walk),
                       1, tx > sx);  // sprites face left
    }

    // Top bar: the zone, and a track of the floors to the boss (cleared ones
    // filled, this one blinking, the boss as a crown).
    fb.fill_rect(0, 0, kScreenW, kMapTop, t.ui);
    fb.fill_rect(0, kMapTop - 1, kScreenW, 1, mix(t.ui, t.accent, 60));
    fb.draw_text(4, 3, "ZONE " + std::to_string(zone + 1), t.accent);
    const int track_x = 44, step = 7;
    fb.fill_rect(track_x + 2, 5, step * last, 1, mix(t.ui, t.ui_text, 50));
    for (int r = 0; r < last; ++r) {
        int x = track_x + r * step;
        Color c = r < row ? t.accent : r == row ? ((now / 300) % 2 ? t.ui_text : t.accent) : mix(t.ui, t.ui_text, 70);
        fb.fill_rect(x, 4, 4, 3, c);
        fb.fill_rect(x + 1, 3, 2, 5, c);
    }
    {
        int x = track_x + last * step;  // the boss: a little crown
        Color c = row == last ? ((now / 300) % 2 ? t.ui_text : t.accent) : node_rim(Node::Boss);
        fb.fill_rect(x, 4, 7, 4, c);
        fb.set(x, 2, c);
        fb.set(x, 3, c);
        fb.set(x + 3, 2, c);
        fb.set(x + 3, 3, c);
        fb.set(x + 6, 2, c);
        fb.set(x + 6, 3, c);
    }

    // Bottom panel: where the chosen path leads, HP, and the buttons.
    Stats ps = pet_stats(pack, pet);
    int hp = pet.run.hp < 0 ? ps.hp : pet.run.hp;
    fb.fill_rect(0, kMapPanelY, kScreenW, kScreenH - kMapPanelY, t.ui);
    fb.fill_rect(0, kMapPanelY, kScreenW, 1, mix(t.ui, t.accent, 60));
    if (!choices_.empty()) {
        Color rim = node_rim(target().type);
        disc(fb, 7, kMapPanelY + 4, 3.5f, rim);
        fb.draw_text(13, kMapPanelY + 2, node_label(target().type), rim);
        if (choices_.size() == 1)
            fb.draw_text(17 + Framebuffer::text_width(node_label(target().type)), kMapPanelY + 2, "ONE WAY",
                         mix(t.ui, t.ui_text, 110));
    }
    // HP as a small bar with the numbers beside it.
    std::string hp_text = std::to_string(hp) + "/" + std::to_string(ps.hp);
    int hx = kScreenW - 4 - Framebuffer::text_width(hp_text);
    fb.draw_text(hx, kMapPanelY + 2, hp_text, t.ui_text);
    Color hp_fill = hp * 3 < ps.hp ? rgb(0xe0, 0x40, 0x50) : rgb(0x38, 0xb7, 0x64);
    draw_bar(fb, hx - 25, kMapPanelY + 3, 22, 3, hp, ps.hp, hp_fill, mix(t.ui, t.ui_text, 40));
    if (phase_ == Phase::Map)
        draw_footer(fb, t, choices_.size() > 1 ? "A: TURN B: GO C: HOME" : "B: GO C: HOME");
}

}  // namespace

std::unique_ptr<Scene> make_quest_scene(Game& game) { return std::make_unique<QuestScene>(game); }

}  // namespace vpet
