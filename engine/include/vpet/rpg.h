#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "vpet/pack.h"
#include "vpet/pet.h"

// Game rules for leveling, items, the garden and quest combat. Pure functions
// of their inputs (randomness is passed in), so they are easy to test.
namespace vpet {

constexpr int kMaxEquippedMoves = 3;

// --- Stats and levels ----------------------------------------------------------

// Species base stats grown 10% per level, plus permanent chip bonuses.
Stats pet_stats(const Pack& pack, const Pet& pet);
uint32_t xp_to_next(int level);
// Adds XP, applying level-ups. Appends a message per level and new move.
void gain_xp(const Pack& pack, Pet& pet, uint32_t xp, std::vector<std::string>& msgs);
// Every move the current form knows at its level, oldest first.
std::vector<int> learned_moves(const Pack& pack, const Pet& pet);
// The player's picks (pet.moves) if any are still known, else the newest
// three learned.
std::vector<int> equipped_moves(const Pack& pack, const Pet& pet);
// Equips or unequips a learned move (1 to 3 equipped). `msg` says what happened.
bool toggle_move(const Pack& pack, Pet& pet, int move, std::string& msg);

// --- Items --------------------------------------------------------------------

int count(const Home& home, int item);
void add_item(Home& home, int item, int n);
// Uses an item from the bag at home. On failure `msg` says why.
bool use_item_at_home(const Pack& pack, Pet& pet, Home& home, int item, std::string& msg);
bool can_craft(const Pack& pack, const Home& home, int recipe);
bool craft(const Pack& pack, Home& home, int recipe);

// --- Garden -------------------------------------------------------------------

constexpr int kCropRipe = 3;
// 0 planted, 1 sprout, 2 growing, 3 ripe.
int crop_stage(const Pack& pack, const Plot& plot, uint64_t now_ms);
uint64_t crop_ms_left(const Pack& pack, const Plot& plot, uint64_t now_ms);
bool plant(const Pack& pack, Home& home, int plot, int crop, uint64_t now_ms);
// Harvests a ripe plot. `roll` (0-99) decides the 50% chance of a seed back.
// Returns the number of crops gained, 0 if not ripe.
int harvest(const Pack& pack, Home& home, int plot, uint64_t now_ms, uint32_t roll);

// --- Quest --------------------------------------------------------------------

constexpr int kBossEvery = 10;

// Enemy stats grow 15% per floor above the enemy's first floor; bosses get +50% HP.
Stats enemy_stats(const Enemy& e, int floor, bool boss);

struct EnemyPick {
    int index = -1;    // into pack.enemies
    bool boss = false;
};

// Picks an enemy whose floor range covers `floor`. Every 10th floor is a boss:
// a boss enemy in range if the pack has one, else a regular one promoted.
// Floors past every range use the deepest tier available.
EnemyPick pick_enemy(const Pack& pack, int floor, uint32_t rnd);
// mult is the minigame result, about 0.5 (poor) to 1.5 (perfect).
int damage(int power, float mult, int atk, int def);
// Attribute advantage from the pack: 1.25 if `attacker` beats `defender`,
// 0.75 if the reverse, else 1.
float attribute_mult(const Pack& pack, const std::string& attacker, const std::string& defender);
uint32_t floor_xp(int floor);
// Gives at most one item. Drops are rolled from the least likely up and the
// first hit wins, so rare items keep their full chance. `rng` must return
// 0..9999. Deeper floors drop more: +2% per floor, and +4% for rare and epic.
template <typename Rng>
std::vector<int> roll_drops(const Pack& pack, const Enemy& e, int floor, Rng rng) {
    std::vector<Drop> order = e.drops;
    std::stable_sort(order.begin(), order.end(), [](const Drop& a, const Drop& b) { return a.chance < b.chance; });
    for (const Drop& d : order) {
        Rarity r = pack.items[d.item].rarity;
        float per_floor = r == Rarity::Rare || r == Rarity::Epic ? 0.04f : 0.02f;
        float bonus = 1.0f + per_floor * float(floor);
        if (float(rng() % 10000) < d.chance * bonus * 10000.0f) return {d.item};
    }
    return {};
}

}  // namespace vpet
