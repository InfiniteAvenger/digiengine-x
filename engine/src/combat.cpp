#include "vpet/combat.h"
#include <algorithm>
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

ZoneMap make_zone_map(uint32_t seed, int zone) {
    uint32_t r = (seed ^ (uint32_t(zone) * 0x9E3779B9u)) | 1u;
    auto next = [&r] {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        return r;
    };
    for (int i = 0; i < 4; ++i) next();  // stir small seeds

    // Trails wind up the map from the entrance, each stepping at most one
    // column per floor. Where trails meet they merge into one node; where
    // they part, the node branches. Keeping them in left-to-right order means
    // no two paths cross.
    constexpr int kTrails = 4;
    ZoneMap m;
    const int last = kBossEvery - 1;
    int pos[kTrails];
    for (int& p : pos) p = int(next() % kMapCols);
    if (pos[0] == pos[1]) pos[1] = (pos[0] + 2) % kMapCols;  // the entrance always splits
    std::sort(pos, pos + kTrails);
    for (int row = 0; row < last; ++row) {
        for (int p : pos) m.at[row][p].used = true;
        if (row + 1 == last) {
            for (int p : pos) m.at[row][p].next = 1;  // every trail ends at the boss
            break;
        }
        int was[kTrails];
        std::copy(pos, pos + kTrails, was);
        for (int i = 0; i < kTrails; ++i) {
            pos[i] = std::clamp(was[i] + int(next() % 3) - 1, 0, kMapCols - 1);
            if (i > 0) pos[i] = std::max(pos[i], pos[i - 1]);
            m.at[row][was[i]].next |= uint8_t(1 << pos[i]);
        }
    }
    // Node types follow a simple RPG rhythm: the zone opens with fights, the
    // floor before the boss is all campfires, and in between most nodes are
    // fights. A floor has at most one forage spot or campfire, campfires only
    // come mid-zone, and a break is always followed by a fight, so rests and
    // forages never come back to back.
    for (int row = 0; row < last; ++row) {
        bool special_in_row = false;
        for (int c = 0; c < kMapCols; ++c) {
            MapNode& n = m.at[row][c];
            n.type = Node::Battle;
            if (!n.used || row == 0) continue;
            if (row == last - 1) {
                n.type = Node::Campfire;
                continue;
            }
            bool after_break = false;  // reached straight from a forage or campfire
            for (int p = 0; p < kMapCols; ++p)
                if (m.at[row - 1][p].used && (m.at[row - 1][p].next >> c & 1) && m.at[row - 1][p].type != Node::Battle)
                    after_break = true;
            uint32_t roll = next() % 100;
            if (special_in_row || after_break || roll >= 40 || row > last - 3) continue;
            bool mid_zone = row >= 3;  // no rest in the first few floors
            n.type = mid_zone && roll < 15 ? Node::Campfire : Node::Forage;
            special_in_row = true;
        }
    }
    m.at[last][0] = MapNode{true, Node::Boss, 0};
    return m;
}

std::vector<int> map_choices(const ZoneMap& map, int floor, int col) {
    int row = row_of(floor);
    bool anywhere = row == 0 || col < 0 || col >= kMapCols || !map.at[row - 1][col].used;
    std::vector<int> out;
    for (int c = 0; c < kMapCols; ++c)
        if (map.at[row][c].used && (anywhere || (map.at[row - 1][col].next >> c & 1))) out.push_back(c);
    return out;
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
    if (found.empty()) {  // nothing hit: pick one by the table's odds
        float total = 0;
        for (const Drop& d : table) total += d.chance;
        float pick = float(next() % 10000) / 10000.0f * total;
        found.push_back(table.back().item);
        for (const Drop& d : table) {
            if (pick < d.chance) {
                found.back() = d.item;
                break;
            }
            pick -= d.chance;
        }
    }
    return found;
}

}  // namespace vpet
