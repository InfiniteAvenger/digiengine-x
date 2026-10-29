#include "vpet/combat.h"
#include "vpet/rpg.h"

namespace vpet {

Hit resolve_hit(const Pack& pack, int power, float mult, const Fighter& attacker, const Fighter& defender) {
    int atk = attacker.buff_floors ? attacker.stats.atk * 5 / 4 : attacker.stats.atk;
    int def = defender.buff_floors ? defender.stats.def * 5 / 4 : defender.stats.def;
    Hit h;
    h.advantage = attribute_mult(pack, attacker.attribute, defender.attribute);
    h.damage = damage(power, mult * h.advantage, atk, def);
    return h;
}

bool pet_moves_first(const Stats& pet, const Stats& enemy) { return pet.spd >= enemy.spd; }

const char* node_label(Node n) {
    switch (n) {
        case Node::Battle: return "FIGHT";
        case Node::Campfire: return "REST";
        case Node::Forage: return "FORAGE";
        case Node::Boss: return "BOSS";
    }
    return "";
}

std::vector<Node> path_choices(int floor, uint32_t rnd) {
    if (floor % kBossEvery == 0) return {Node::Boss};
    rnd = rnd * 2654435761u + uint32_t(floor) * 40503u;
    if (rnd % 3 == 0) return {Node::Battle, Node::Campfire, Node::Forage};  // a three-way split
    // Two ways: a fight, plus a fight, a rest or a forage.
    switch ((rnd >> 8) % 4) {
        case 0: return {Node::Battle, Node::Battle};
        case 1:
        case 2: return {Node::Battle, Node::Forage};
        default: return {Node::Battle, Node::Campfire};
    }
}

std::vector<int> forage(const Pack& pack, int floor, uint32_t seed) {
    std::vector<Drop> table = pack.forage;
    if (table.empty())
        for (const auto& [family, drops] : pack.family_drops) table.insert(table.end(), drops.begin(), drops.end());
    if (table.empty()) return {};

    uint32_t r = seed | 1;
    auto next = [&r] {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        return r;
    };
    Enemy source;
    source.drops = table;
    std::vector<int> found = roll_drops(pack, source, floor, next);
    if (found.empty()) found.push_back(table[next() % table.size()].item);
    return found;
}

}  // namespace vpet
