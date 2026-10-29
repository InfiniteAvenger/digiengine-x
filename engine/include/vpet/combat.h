#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "vpet/pack.h"
#include "vpet/pet.h"

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

// The choices for a floor. Boss floors offer only the boss. Otherwise there
// are 2 or 3 nodes, the first is always a battle, and a 3-node choice always
// includes a campfire (so the run can always be paused).
std::vector<Node> path_choices(int floor, uint32_t rnd);

constexpr int kCampfireHealPercent = 30;

// Forage: rolls the pack's "forage" table (or every family table if none),
// deeper floors finding more. Always finds at least one thing.
std::vector<int> forage(const Pack& pack, int floor, uint32_t seed);

}  // namespace vpet
