#include "vpet/rpg.h"
#include <algorithm>

namespace vpet {

// --- Stats and levels ----------------------------------------------------------

Stats pet_stats(const Pack& pack, const Pet& pet) {
    const Stats& base = pack.species[pet.species].stats;
    auto grow = [&](int v) { return v + v * (pet.level - 1) / 10; };
    return {grow(base.hp) + pet.bonus.hp, grow(base.atk) + pet.bonus.atk, grow(base.def) + pet.bonus.def,
            grow(base.spd) + pet.bonus.spd};
}

uint32_t xp_to_next(int level) { return uint32_t(20 * level); }

void gain_xp(const Pack& pack, Pet& pet, uint32_t xp, std::vector<std::string>& msgs) {
    const Species& s = pack.species[pet.species];
    pet.xp += xp;
    while (pet.xp >= xp_to_next(pet.level)) {
        pet.xp -= xp_to_next(pet.level);
        ++pet.level;
        msgs.push_back(s.name + " GREW TO LV " + std::to_string(pet.level) + "!");
        for (const LearnRule& r : s.learns) {
            if (r.level != pet.level) continue;
            msgs.push_back("LEARNED " + pack.moves[r.move].name + "!");
            // With hand-picked moves and a free slot, equip the new one too.
            if (!pet.moves.empty() && pet.moves.size() < size_t(kMaxEquippedMoves)) pet.moves.push_back(r.move);
        }
    }
}

std::vector<int> learned_moves(const Pack& pack, const Pet& pet) {
    std::vector<LearnRule> known;
    for (const LearnRule& r : pack.species[pet.species].learns)
        if (r.level <= pet.level) known.push_back(r);
    std::stable_sort(known.begin(), known.end(), [](const LearnRule& a, const LearnRule& b) { return a.level < b.level; });
    std::vector<int> moves;
    for (const LearnRule& r : known)
        if (std::find(moves.begin(), moves.end(), r.move) == moves.end()) moves.push_back(r.move);
    return moves;
}

std::vector<int> equipped_moves(const Pack& pack, const Pet& pet) {
    std::vector<int> known = learned_moves(pack, pet);
    // The player's own picks, minus any the current form no longer knows.
    std::vector<int> chosen;
    for (int m : pet.moves)
        if (std::find(known.begin(), known.end(), m) != known.end() && chosen.size() < size_t(kMaxEquippedMoves))
            chosen.push_back(m);
    if (!chosen.empty()) return chosen;
    size_t from = known.size() > size_t(kMaxEquippedMoves) ? known.size() - kMaxEquippedMoves : 0;
    return std::vector<int>(known.begin() + std::ptrdiff_t(from), known.end());
}

bool toggle_move(const Pack& pack, Pet& pet, int move, std::string& msg) {
    std::vector<int> eq = equipped_moves(pack, pet);
    auto it = std::find(eq.begin(), eq.end(), move);
    if (it != eq.end()) {
        if (eq.size() == 1) return msg = "KEEP AT LEAST ONE", false;
        eq.erase(it);
        msg = "UNEQUIPPED";
    } else {
        if (eq.size() >= size_t(kMaxEquippedMoves)) return msg = "3 MOVES MAX", false;
        eq.push_back(move);
        msg = "EQUIPPED";
    }
    pet.moves = eq;
    return true;
}

// --- Items --------------------------------------------------------------------

int count(const Home& home, int item) {
    return item >= 0 && size_t(item) < home.inventory.size() ? home.inventory[item] : 0;
}

void add_item(Home& home, int item, int n) {
    if (item < 0 || size_t(item) >= home.inventory.size()) return;
    home.inventory[item] = uint16_t(std::clamp(count(home, item) + n, 0, 999));
}

bool use_item_at_home(const Pack& pack, Pet& pet, Home& home, int item, std::string& msg) {
    const ItemDef& it = pack.items[item];
    const Effect& e = it.effect;
    if (count(home, item) <= 0) return msg = "NONE LEFT", false;
    if (it.material || !e.at_home()) return msg = it.material ? "INGREDIENT" : "USE IN QUESTS", false;
    if (pack.species[pet.species].stage == Stage::Egg) return msg = "STILL AN EGG", false;
    if (pet.asleep) return msg = "ZZZ...", false;
    if (e.forever && pet.forever) return msg = "ALREADY FOREVER", false;
    bool only_food = !e.strength && !e.cure && !e.forever && !e.bonus.hp && !e.bonus.atk && !e.bonus.def && !e.bonus.spd;
    int hearts = max_hearts(pack, pet);
    if (e.hunger && pet.hunger >= hearts * 2 && only_food) return msg = "FULL!", false;

    pet.hunger = uint8_t(std::min(hearts * 2, pet.hunger + e.hunger * 2));  // effects are whole hearts
    pet.strength = uint8_t(std::min(hearts, pet.strength + e.strength));
    if (e.hunger) pet.hunger_call = Call{};
    if (e.strength) pet.strength_call = Call{};
    if (e.cure) pet.sick = false;
    if (e.forever) pet.forever = true;
    pet.bonus.hp += e.bonus.hp;
    pet.bonus.atk += e.bonus.atk;
    pet.bonus.def += e.bonus.def;
    pet.bonus.spd += e.bonus.spd;
    pet.weight = uint16_t(pet.weight + e.hunger);
    add_item(home, item, -1);
    msg = e.forever ? "PARTNERS FOREVER!" : e.hunger ? "YUM!" : "USED " + it.name;
    return true;
}

bool can_craft(const Pack& pack, const Home& home, int recipe) {
    for (auto [item, n] : pack.recipes[recipe].needs)
        if (count(home, item) < n) return false;
    return true;
}

bool craft(const Pack& pack, Home& home, int recipe) {
    if (!can_craft(pack, home, recipe)) return false;
    const Recipe& r = pack.recipes[recipe];
    for (auto [item, n] : r.needs) add_item(home, item, -n);
    add_item(home, r.makes, r.count);
    return true;
}

// --- Garden -------------------------------------------------------------------

int crop_stage(const Pack& pack, const Plot& plot, uint64_t now_ms) {
    if (plot.crop < 0) return -1;
    uint64_t grow_ms = uint64_t(pack.crops[plot.crop].grow_s) * 1000;
    uint64_t age = now_ms > plot.planted_ms ? now_ms - plot.planted_ms : 0;
    if (grow_ms == 0 || age >= grow_ms) return kCropRipe;
    return int(age * kCropRipe / grow_ms);
}

uint64_t crop_ms_left(const Pack& pack, const Plot& plot, uint64_t now_ms) {
    if (plot.crop < 0) return 0;
    uint64_t done = plot.planted_ms + uint64_t(pack.crops[plot.crop].grow_s) * 1000;
    return done > now_ms ? done - now_ms : 0;
}

bool plant(const Pack& pack, Home& home, int plot, int crop, uint64_t now_ms) {
    if (home.garden[plot].crop >= 0 || count(home, pack.crops[crop].seed) <= 0) return false;
    add_item(home, pack.crops[crop].seed, -1);
    home.garden[plot] = Plot{crop, now_ms};
    return true;
}

int harvest(const Pack& pack, Home& home, int plot, uint64_t now_ms, uint32_t roll) {
    Plot& p = home.garden[plot];
    if (crop_stage(pack, p, now_ms) != kCropRipe) return 0;
    const Crop& c = pack.crops[p.crop];
    add_item(home, c.yields, c.count);
    if (roll % 100 < 50) add_item(home, c.seed, 1);
    p = Plot{};
    return c.count;
}

// --- Quest --------------------------------------------------------------------

Stats enemy_stats(const Enemy& e, int floor, bool boss) {
    int above = std::max(0, floor - e.min_floor);
    auto grow = [&](int v) { return v + v * 15 * above / 100; };
    Stats s{grow(e.stats.hp), grow(e.stats.atk), grow(e.stats.def), grow(e.stats.spd)};
    if (boss) s.hp += s.hp / 2;
    return s;
}

EnemyPick pick_enemy(const Pack& pack, int floor, uint32_t rnd) {
    auto in_range = [&](const Enemy& e) { return floor >= e.min_floor && (e.max_floor == 0 || floor <= e.max_floor); };
    bool boss_floor = floor % kBossEvery == 0;

    std::vector<int> bosses, regulars;
    for (size_t i = 0; i < pack.enemies.size(); ++i) {
        if (!in_range(pack.enemies[i])) continue;
        (pack.enemies[i].boss ? bosses : regulars).push_back(int(i));
    }
    if (boss_floor && !bosses.empty()) return {bosses[rnd % bosses.size()], true};

    if (regulars.empty()) {
        // Past every range: the deepest tier that has started.
        int deepest = 0;
        for (const Enemy& e : pack.enemies)
            if (!e.boss && e.min_floor <= floor) deepest = std::max(deepest, e.min_floor);
        for (size_t i = 0; i < pack.enemies.size(); ++i)
            if (!pack.enemies[i].boss && pack.enemies[i].min_floor == deepest) regulars.push_back(int(i));
    }
    if (regulars.empty())  // only bosses exist
        for (size_t i = 0; i < pack.enemies.size(); ++i) regulars.push_back(int(i));
    if (regulars.empty()) return {};
    if (boss_floor) {
        // No boss written for this floor: promote one from the strongest tier
        // in range (the one that starts deepest).
        int deepest = 0;
        for (int i : regulars) deepest = std::max(deepest, pack.enemies[i].min_floor);
        std::vector<int> strongest;
        for (int i : regulars)
            if (pack.enemies[i].min_floor == deepest) strongest.push_back(i);
        regulars.swap(strongest);
    }
    int idx = regulars[rnd % regulars.size()];
    return {idx, boss_floor || pack.enemies[idx].boss};
}

int damage(int power, float mult, int atk, int def) {
    float d = float(power) * mult * float(atk) / float(std::max(def, 1)) * 0.5f + 1.0f;
    return std::max(1, int(d + 0.5f));
}

uint32_t floor_xp(int floor) { return uint32_t(5 + 3 * floor); }

float attribute_mult(const Pack& pack, const std::string& attacker, const std::string& defender) {
    if (attacker.empty() || defender.empty()) return 1.0f;
    auto beats = [&](const std::string& a, const std::string& b) {
        for (const auto& [name, list] : pack.attributes)
            if (name == a) return std::find(list.begin(), list.end(), b) != list.end();
        return false;
    };
    if (beats(attacker, defender)) return 1.25f;
    if (beats(defender, attacker)) return 0.75f;
    return 1.0f;
}

}  // namespace vpet
