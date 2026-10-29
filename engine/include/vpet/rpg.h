#pragma once
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
// The newest moves learned at the current level, up to kMaxEquippedMoves.
std::vector<int> equipped_moves(const Pack& pack, const Pet& pet);

// --- Items --------------------------------------------------------------------

int count(const Pet& pet, int item);
void add_item(Pet& pet, int item, int n);
// Uses an item from the bag at home. On failure `msg` says why.
bool use_item_at_home(const Pack& pack, Pet& pet, int item, std::string& msg);
bool can_craft(const Pack& pack, const Pet& pet, int recipe);
bool craft(const Pack& pack, Pet& pet, int recipe);

// --- Garden -------------------------------------------------------------------

constexpr int kCropRipe = 3;
// 0 planted, 1 sprout, 2 growing, 3 ripe.
int crop_stage(const Pack& pack, const Plot& plot, uint64_t now_ms);
uint64_t crop_ms_left(const Pack& pack, const Plot& plot, uint64_t now_ms);
bool plant(const Pack& pack, Pet& pet, int plot, int crop, uint64_t now_ms);
// Harvests a ripe plot. `roll` (0-99) decides the 50% chance of a seed back.
// Returns the number of crops gained, 0 if not ripe.
int harvest(const Pack& pack, Pet& pet, int plot, uint64_t now_ms, uint32_t roll);

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
uint32_t floor_xp(int floor);
// Rolls each drop; `rng` is called once per drop and must return 0..9999.
template <typename Rng>
std::vector<int> roll_drops(const Enemy& e, int floor, Rng rng) {
    std::vector<int> got;
    float bonus = 1.0f + 0.02f * float(floor);  // deeper floors drop more
    for (const Drop& d : e.drops)
        if (float(rng() % 10000) < d.chance * bonus * 10000.0f) got.push_back(d.item);
    return got;
}

}  // namespace vpet
