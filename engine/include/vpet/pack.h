#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "vpet/sprite.h"

namespace vpet {

class Platform;

enum class Stage : uint8_t { Egg, Baby, Baby2, Child, Adult, Perfect, Ultimate };

const char* stage_name(Stage s);
bool parse_stage(const std::string& s, Stage& out);

struct EvolveRule {
    int to = -1;  // species index
    uint32_t after_seconds = 0;
};

// How fast a form's needs change. Timers count awake seconds only.
struct Care {
    uint32_t hunger_s = 0;    // seconds per hunger heart lost
    uint32_t strength_s = 0;  // seconds per strength heart lost
    uint32_t poop_s = 0;      // seconds between poops
    uint16_t sleep_min = 0;   // bedtime, minutes after local midnight
    uint16_t wake_min = 0;    // wake time; equal to sleep_min means never sleeps
    uint16_t weight = 0;      // base (minimum) weight in grams
};

// Defaults per stage, used for any care field a pack leaves out.
Care default_care(Stage s);

struct Stats {
    int hp = 0, atk = 0, def = 0, spd = 0;
};

// Level-1 combat stats per stage, used when a species has no "stats".
Stats default_stats(Stage s);

// --- Items -------------------------------------------------------------------

enum class Rarity : uint8_t { Common, Uncommon, Rare, Epic };
const char* rarity_name(Rarity r);

// What using an item does. Home effects apply to the pet; quest effects apply
// during a run.
struct Effect {
    int hunger = 0, strength = 0;  // hearts
    bool cure = false;             // cures sickness
    Stats bonus;                   // permanent stat increase (chips)
    int heal = 0;                  // quest: percent of max HP restored
    int atk_buff = 0, def_buff = 0;  // quest: floors of +25% ATK / DEF

    bool at_home() const { return hunger || strength || cure || bonus.hp || bonus.atk || bonus.def || bonus.spd; }
    bool in_quest() const { return heal || atk_buff || def_buff; }
};

struct ItemDef {
    std::string id, name, info;
    bool material = false;  // crafting/cooking ingredient rather than a usable item
    Rarity rarity = Rarity::Common;
    Effect effect;
    int icon = -1;  // frame in Pack::item_icons, -1 = none
};

struct Recipe {
    int makes = -1;  // item index
    int count = 1;
    std::vector<std::pair<int, int>> needs;  // (item index, amount)
};

struct Crop {
    std::string name;
    int seed = -1, yields = -1;  // item indices
    int count = 1;
    uint32_t grow_s = 0;
    Sprite sprite;  // 4 frames: planted, sprout, growing, ripe
};

// --- Combat ------------------------------------------------------------------

// The minigame that charges a move.
enum class MiniGame : uint8_t { Timing, Mash, Sequence, Dice };
const char* minigame_name(MiniGame g);

struct Move {
    std::string id, name;
    int power = 10;
    MiniGame game = MiniGame::Timing;
};

struct Drop {
    int item = -1;
    float chance = 0;
};

struct Enemy {
    std::string id, name;
    Sprite sprite;
    Stats stats;  // at floor 1
    std::vector<int> moves;
    std::vector<Drop> drops;
    bool boss = false;  // appears on every 10th floor
};

struct LearnRule {
    int move = -1;
    int level = 1;
};

struct Species {
    std::string id, name;
    Stage stage = Stage::Egg;
    Sprite sprite;
    Care care;
    Stats stats;
    std::vector<LearnRule> learns;
    std::vector<EvolveRule> evolve;
};

struct Roster {
    std::string id, name;
    std::vector<int> eggs;  // species indices
};

struct Theme {
    Color bg_top = rgb(0x9b, 0xd4, 0xff);
    Color bg_bottom = rgb(0x6b, 0xbf, 0x59);
    Color ui = rgb(0x1a, 0x1c, 0x2c);
    Color ui_text = rgb(0xf4, 0xf4, 0xf4);
    Color accent = rgb(0xff, 0xcd, 0x75);
};

struct Pack {
    std::string id, name, version, author, license;
    Theme theme;
    std::vector<Species> species;
    std::vector<Roster> rosters;
    std::vector<ItemDef> items;  // materials and usable items, one index space
    Sprite item_icons;           // 8x8 frames referenced by ItemDef::icon
    std::vector<Recipe> recipes;
    std::vector<Crop> crops;
    std::vector<Move> moves;
    std::vector<Enemy> enemies;
    std::vector<std::pair<int, int>> start_items;  // given with a new egg

    int find_species(const std::string& id) const;
    int find_item(const std::string& id) const;
    int find_move(const std::string& id) const;
};

// Loads <dir>/pack.json and every sprite it references.
bool load_pack(Platform& platform, const std::string& dir, Pack& out, std::string& err);

}  // namespace vpet
