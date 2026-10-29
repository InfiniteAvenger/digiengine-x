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
        for (const LearnRule& r : s.learns)
            if (r.level == pet.level) msgs.push_back("LEARNED " + pack.moves[r.move].name + "!");
    }
}

std::vector<int> equipped_moves(const Pack& pack, const Pet& pet) {
    std::vector<LearnRule> known;
    for (const LearnRule& r : pack.species[pet.species].learns)
        if (r.level <= pet.level) known.push_back(r);
    std::stable_sort(known.begin(), known.end(), [](const LearnRule& a, const LearnRule& b) { return a.level < b.level; });
    std::vector<int> moves;
    for (size_t i = known.size() > kMaxEquippedMoves ? known.size() - kMaxEquippedMoves : 0; i < known.size(); ++i)
        moves.push_back(known[i].move);
    return moves;
}

// --- Items --------------------------------------------------------------------

int count(const Pet& pet, int item) { return item >= 0 && size_t(item) < pet.inventory.size() ? pet.inventory[item] : 0; }

void add_item(Pet& pet, int item, int n) {
    int v = std::clamp(count(pet, item) + n, 0, 999);
    pet.inventory[item] = uint16_t(v);
}

bool use_item_at_home(const Pack& pack, Pet& pet, int item, std::string& msg) {
    const ItemDef& it = pack.items[item];
    const Effect& e = it.effect;
    if (count(pet, item) <= 0) return msg = "NONE LEFT", false;
    if (it.material || !e.at_home()) return msg = it.material ? "INGREDIENT" : "USE IN QUESTS", false;
    if (pack.species[pet.species].stage == Stage::Egg) return msg = "STILL AN EGG", false;
    if (pet.asleep) return msg = "ZZZ...", false;
    bool full = e.hunger && pet.hunger >= kMaxHearts && !e.strength && !e.cure && !e.bonus.hp && !e.bonus.atk &&
                !e.bonus.def && !e.bonus.spd;
    if (full) return msg = "FULL!", false;

    pet.hunger = uint8_t(std::min(kMaxHearts, pet.hunger + e.hunger));
    pet.strength = uint8_t(std::min(kMaxHearts, pet.strength + e.strength));
    if (e.hunger) pet.hunger_call = Call{};
    if (e.strength) pet.strength_call = Call{};
    if (e.cure) pet.sick = false;
    pet.bonus.hp += e.bonus.hp;
    pet.bonus.atk += e.bonus.atk;
    pet.bonus.def += e.bonus.def;
    pet.bonus.spd += e.bonus.spd;
    pet.weight = uint16_t(pet.weight + e.hunger);
    add_item(pet, item, -1);
    msg = e.hunger ? "YUM!" : "USED " + it.name;
    return true;
}

bool can_craft(const Pack& pack, const Pet& pet, int recipe) {
    for (auto [item, n] : pack.recipes[recipe].needs)
        if (count(pet, item) < n) return false;
    return true;
}

bool craft(const Pack& pack, Pet& pet, int recipe) {
    if (!can_craft(pack, pet, recipe)) return false;
    const Recipe& r = pack.recipes[recipe];
    for (auto [item, n] : r.needs) add_item(pet, item, -n);
    add_item(pet, r.makes, r.count);
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

bool plant(const Pack& pack, Pet& pet, int plot, int crop, uint64_t now_ms) {
    if (pet.garden[plot].crop >= 0 || count(pet, pack.crops[crop].seed) <= 0) return false;
    add_item(pet, pack.crops[crop].seed, -1);
    pet.garden[plot] = Plot{crop, now_ms};
    return true;
}

int harvest(const Pack& pack, Pet& pet, int plot, uint64_t now_ms, uint32_t roll) {
    Plot& p = pet.garden[plot];
    if (crop_stage(pack, p, now_ms) != kCropRipe) return 0;
    const Crop& c = pack.crops[p.crop];
    add_item(pet, c.yields, c.count);
    if (roll % 100 < 50) add_item(pet, c.seed, 1);
    p = Plot{};
    return c.count;
}

// --- Quest --------------------------------------------------------------------

Stats enemy_stats(const Enemy& e, int floor) {
    auto grow = [&](int v) { return v + v * 15 * (floor - 1) / 100; };
    Stats s{grow(e.stats.hp), grow(e.stats.atk), grow(e.stats.def), grow(e.stats.spd)};
    if (e.boss) s.hp += s.hp / 2;
    return s;
}

int pick_enemy(const Pack& pack, int floor, uint32_t rnd) {
    bool want_boss = floor % kBossEvery == 0;
    std::vector<int> pool;
    for (size_t i = 0; i < pack.enemies.size(); ++i)
        if (pack.enemies[i].boss == want_boss) pool.push_back(int(i));
    if (pool.empty())  // pack has no bosses (or only bosses): use anything
        for (size_t i = 0; i < pack.enemies.size(); ++i) pool.push_back(int(i));
    return pool.empty() ? -1 : pool[rnd % pool.size()];
}

int damage(int power, float mult, int atk, int def) {
    float d = float(power) * mult * float(atk) / float(std::max(def, 1)) * 0.5f + 1.0f;
    return std::max(1, int(d + 0.5f));
}

uint32_t floor_xp(int floor) { return uint32_t(5 + 3 * floor); }

}  // namespace vpet
