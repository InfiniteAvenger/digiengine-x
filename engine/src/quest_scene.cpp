// Infinite quest. Each floor is one battle; HP carries between floors and the
// player can head home between floors without losing the run. Fainting ends
// the run (back to floor 1) and the pet comes home sick, but keeps its level,
// items and everything at home.
#include <algorithm>
#include <cctype>
#include <deque>
#include "scenes.h"
#include "vpet/minigame.h"
#include "vpet/rpg.h"

namespace vpet {
namespace {

constexpr int kArenaTop = 12;
constexpr int kHorizon = 66;
constexpr int kPanelY = 80;
constexpr uint64_t kMessageMs = 1400;

enum class Phase { Lobby, Messages, Choose, Items, Mini, Cleared, Over };

class QuestScene : public Scene {
public:
    explicit QuestScene(Game& game) : Scene(game) {}

    void on_press(Button b) override {
        switch (phase_) {
            case Phase::Lobby:
                if (b == BTN_B) start_floor();
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
                if (b == BTN_B) start_floor();
                else if (b == BTN_C) game_.pop();
                break;
            case Phase::Over: game_.pop(); break;
        }
    }

    void update(uint64_t now) override {
        if (phase_ == Phase::Messages && now >= msg_until_) next_message();
        if (phase_ == Phase::Mini) mini_update(now);
    }

    void draw(Framebuffer& fb) override;

private:
    // --- Flow -----------------------------------------------------------------

    void start_floor() {
        const Pack& pack = game_.pack();
        Pet& pet = game_.pet();
        if (pet.hunger == 0) {
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
        --pet.hunger;  // each floor costs a hunger heart
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
            int atk = pet.run.atk_buff ? ps_.atk * 5 / 4 : ps_.atk;
            int dmg = damage(m.power, mult, atk, es_.def);
            ehp_ -= dmg;
            const char* rating = mult >= 1.4f ? "PERFECT! " : mult >= 1.1f ? "GREAT! " : mult < 0.75f ? "WEAK... " : "";
            msgs.push_back(s.name + " USED " + m.name + "!");
            msgs.push_back(std::string(rating) + std::to_string(dmg) + " DAMAGE");
        };
        auto enemy_turn = [&] {
            const Move& m = pack.moves[e.moves[game_.rand() % e.moves.size()]];
            float roll = 0.8f + float(game_.rand() % 41) / 100.0f;
            int def = pet.run.def_buff ? ps_.def * 5 / 4 : ps_.def;
            int dmg = damage(m.power, roll, es_.atk, def);
            pet.run.hp -= dmg;
            msgs.push_back(e.name + " USED " + m.name + "!");
            msgs.push_back(s.name + " TOOK " + std::to_string(dmg));
        };

        if (move >= 0 && ps_.spd >= es_.spd) {
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
        for (int item : roll_drops(e, floor, [this] { return game_.rand(); })) {
            add_item(game_.home(), item, 1);
            msgs.push_back("GOT " + pack.items[item].name + "!");
        }
        if (pet.run.atk_buff) --pet.run.atk_buff;
        if (pet.run.def_buff) --pet.run.def_buff;
        pet.run.best = std::max(pet.run.best, floor);
        ++pet.run.floor;
        ps_ = pet_stats(pack, pet);  // a level-up raises max HP
        say(msgs, Phase::Cleared);
    }

    void lose(std::vector<std::string>& msgs) {
        Pet& pet = game_.pet();
        msgs.push_back(game_.species().name + " FAINTED!");
        msgs.push_back("RUN ENDED ON FLOOR " + std::to_string(pet.run.floor));
        pet.run = Run{1, pet.run.best, -1, 0, 0};
        pet.sick = true;
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

    draw_scenery(fb, t, kArenaTop, kHorizon, kPanelY, now, game_.local_minute(), false);

    fb.fill_rect(0, 0, kScreenW, kArenaTop, t.ui);
    fb.draw_text(4, 3, "FLOOR " + std::to_string(pet.run.floor), t.accent);
    std::string best = "BEST " + std::to_string(pet.run.best);
    fb.draw_text(kScreenW - 4 - Framebuffer::text_width(best), 3, best, t.ui_text);

    bool fighting = phase_ != Phase::Lobby && phase_ != Phase::Over && enemy_ >= 0;
    int bob = int((now / 350) % 2);
    if (fighting && ehp_ > 0) {
        const Enemy& e = pack.enemies[enemy_];
        fb.draw_sprite(e.sprite, 0, 80, kArenaTop + 10 + bob, 2);
        fb.panel(3, 15, 54, 14, mix(t.ui, t.bg_top, 40), mix(t.ui, t.ui_text, 70));
        fb.draw_text(5, 17, e.name, boss_ ? t.accent : t.ui_text);
        draw_bar(fb, 5, 24, 50, 3, ehp_, es_.hp, rgb(0xe0, 0x40, 0x50), dim);
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
            fb.draw_text(6, y + 9, "EACH FLOOR COSTS 1 HUNGER", dim);
            fb.draw_text(6, y + 18, "LOSE AND THE RUN RESETS", dim);
            draw_footer(fb, t, "B: ENTER  C: HOME");
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
            for (int i = 0; i < n; ++i) {
                int ox = 6 + (i % 2) * 57, oy = y + (i / 2) * 10;
                std::string label = i < int(moves_.size()) ? pack.moves[moves_[i]].name : "ITEM";
                bool sel = i == cursor_;
                if (sel) fb.draw_text(ox - 4, oy, ">", t.accent);
                fb.draw_text(ox, oy, label, sel ? t.accent : t.ui_text);
            }
            std::string info = cursor_ < int(moves_.size())
                                   ? std::string(minigame_name(pack.moves[moves_[cursor_]].game)) + "  POWER " +
                                         std::to_string(pack.moves[moves_[cursor_]].power)
                                   : "HEAL OR BUFF";
            for (char& c : info) c = char(toupper(c));
            fb.draw_text(6, kScreenH - 9, info, dim);
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
            draw_hearts(fb, (kScreenW - 36) / 2, y + 18, pet.hunger);
            draw_footer(fb, t, "B: NEXT FLOOR  C: HOME");
            break;
        case Phase::Over:
            fb.draw_text_centered(y + 2, "RUN OVER", t.accent);
            fb.draw_text_centered(y + 12, "BEST FLOOR " + std::to_string(pet.run.best), t.ui_text);
            draw_footer(fb, t, "ANY BUTTON: HOME");
            break;
    }
}

}  // namespace

std::unique_ptr<Scene> make_quest_scene(Game& game) { return std::make_unique<QuestScene>(game); }

}  // namespace vpet
