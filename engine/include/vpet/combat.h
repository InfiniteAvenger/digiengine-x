#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "vpet/pack.h"
#include "vpet/pet.h"
#include "vpet/rpg.h"

// Quest combat and path rules, kept free of drawing and input so they can be
// tested directly. The quest screen drives them.
namespace vpet {

// --- Battle --------------------------------------------------------------------

struct Fighter {
    Stats stats;
    std::string attribute;
    int buff_floors = 0;  // >0: +25% (attack when hitting, defense when hit)
};

struct Hit {
    int damage = 0;
    float advantage = 1.0f;  // attribute multiplier that applied
};

// One attack: move power x minigame result x attribute advantage, against the
// defender's (possibly buffed) defense.
Hit resolve_hit(const Pack& pack, int power, float mult, const Fighter& attacker, const Fighter& defender);

// Faster acts first; ties go to the pet.
bool pet_moves_first(const Stats& pet, const Stats& enemy);

// --- The path map ------------------------------------------------------------------

enum class Node : uint8_t { Battle, Campfire, Forage, Boss };
const char* node_label(Node n);

// Each zone of kBossEvery floors is one overhead map: a row per floor, up to
// kMapCols nodes per row, and the boss alone on the last row. Paths wind
// upward and branch or merge, so some nodes offer a choice and some only
// one way on. Maps are generated from Run::map_seed and the zone number.
constexpr int kMapCols = 4;

struct MapNode {
    bool used = false;
    Node type = Node::Battle;
    uint8_t next = 0;  // bit c: a path leads to column c of the next row
};

struct ZoneMap {
    MapNode at[kBossEvery][kMapCols];
};

inline int zone_of(int floor) { return (floor - 1) / kBossEvery; }
inline int row_of(int floor) { return (floor - 1) % kBossEvery; }

// Rows have 1-4 nodes (only the boss on the last), the first row at least 2,
// all battles. Every node is reachable, and no two paths cross.
ZoneMap make_zone_map(uint32_t seed, int zone);

// Columns the pet can move to for `floor`, standing at column `col` of the
// row below (col < 0, or the zone's first floor: all of the first row).
std::vector<int> map_choices(const ZoneMap& map, int floor, int col);

constexpr int kCampfireHealPercent = 30;

// Forage: rolls the pack's "forage" table (or every family table if none),
// deeper floors finding rarer things. Always finds exactly one thing.
std::vector<int> forage(const Pack& pack, int floor, uint32_t seed);

}  // namespace vpet
