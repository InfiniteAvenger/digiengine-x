#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "vpet/pack.h"

namespace vpet {

constexpr int kMaxPoop = 4;
constexpr uint32_t kCallTimeoutS = 10 * 60;  // an ignored call becomes a care mistake
constexpr int kCryoSlots = 2;

// A need the player must answer (empty hunger, empty strength, lights left on
// at bedtime). Each call costs at most one care mistake.
struct Call {
    uint64_t since_ms = 0;  // 0 = no call
    bool missed = false;    // timed out; already counted as a mistake

    bool active() const { return since_ms != 0 && !missed; }
};

constexpr int kGardenPlots = 8;  // 2 rows x 4

struct Plot {
    int crop = -1;  // index into pack.crops, -1 = empty
    uint64_t planted_ms = 0;
};

// The current infinite-quest run. Survives going home; dying resets it.
struct Run {
    int floor = 1;
    int best = 0;  // highest floor ever cleared
    int hp = -1;   // -1 = full
    int atk_buff = 0, def_buff = 0;  // floors remaining
    uint32_t map_seed = 0;  // 0 = roll a new map when the run is next opened
    int col = -1;           // map column of the last floor taken, -1 = zone entrance
};

struct Pet {
    int species = -1;
    uint64_t born_ms = 0;
    uint64_t stage_ms = 0;  // when the current form was reached
    uint64_t sim_ms = 0;    // simulated up to this time

    uint8_t hunger = 0;    // half hearts, 0..max_hearts() * 2
    uint8_t strength = 0;  // hearts, 0..max_hearts()
    uint8_t poop = 0;
    uint16_t weight = 0;
    uint16_t care_mistakes = 0;
    uint16_t trainings = 0;
    // Counted in the current form only; evolution rules check these.
    uint16_t stage_mistakes = 0;
    uint16_t effort = 0;  // successful trainings
    bool sick = false;
    bool asleep = false;
    bool lights_on = true;
    bool forever = false;  // Forever Partner: never dies of old age

    // Awake seconds since the last hunger/strength drop and the last poop.
    uint32_t hunger_s = 0, strength_s = 0, poop_s = 0;

    Call hunger_call, strength_call, lights_call;

    // RPG progress
    int level = 1;
    uint32_t xp = 0;
    Stats bonus;  // permanent, from chips
    Run run;
    std::vector<int> moves;  // equipped move indices; empty = newest three learned

    uint64_t frozen_ms = 0;  // when it went into cryo (only while stored)
    uint64_t awake_until_ms = 0;  // woken during bedtime: stays up until then

    bool needs_attention() const { return hunger_call.active() || strength_call.active() || lights_call.active(); }
};

// Size of the current form's hunger and strength meters, in hearts. Hunger
// is tracked in half hearts, so its full value is twice this.
inline int max_hearts(const Pack& pack, const Pet& pet) { return pack.species[pet.species].care.hearts; }

// Shared by every pet: the bag and the garden.
struct Home {
    std::vector<uint16_t> inventory;  // count per pack.items index
    Plot garden[kGardenPlots];
};

// Everything in one save slot.
struct Save {
    std::string roster;  // id of the version new eggs hatch from
    Pet active;
    Home home;
    std::optional<Pet> cryo[kCryoSlots];
};

enum SimEvent : uint32_t {
    EV_HATCHED = 1 << 0,
    EV_EVOLVED = 1 << 1,
    EV_SLEPT = 1 << 2,
    EV_WOKE = 1 << 3,
    EV_POOPED = 1 << 4,
    EV_SICK = 1 << 5,
    EV_MISTAKE = 1 << 6,
    EV_DIED = 1 << 7,  // reached the pack's lifespan
};

Pet new_pet(const Pack& pack, int species, uint64_t now_ms);
Home new_home(const Pack& pack);  // with the pack's start_items

// True if every condition of `rule` holds right now (time included).
bool evolution_ready(const EvolveRule& rule, const Pet& pet, uint64_t now_ms);

// Remaining lifespan, or nullopt if the pet can't die of old age (no lifespan
// in the pack, still an egg, or a Forever Partner).
std::optional<uint64_t> lifespan_left_ms(const Pack& pack, const Pet& pet, uint64_t now_ms);

// Advances the pet to now_ms in one-second steps, so a game that was closed
// for hours catches up exactly as if it had been running (capped at 30 days).
// Returns a mask of SimEvents that happened. Stops at EV_DIED.
uint32_t simulate(const Pack& pack, Pet& pet, uint64_t now_ms, int32_t utc_offset_s);

// Cryo: freezing records the time; thawing shifts every timestamp forward by
// the time spent frozen, so age, needs, calls and evolution all resume
// exactly where they paused.
void freeze(Pet& pet, uint64_t now_ms);
void thaw(Pet& pet, uint64_t now_ms);

// Care actions. Each returns false when it has no effect (full, not sick...).
bool feed_kibble(const Pack& pack, Pet& pet);
bool give_medicine(Pet& pet);
bool clean_up(Pet& pet);
void toggle_lights(Pet& pet);

// Debug: switches the pet straight to another form, as an evolution would,
// keeping its meters within the new form's hearts.
void change_form(const Pack& pack, Pet& pet, int species, uint64_t now_ms);
void finish_training(const Pack& pack, Pet& pet, bool success);

// Plain "key value" text, one field per line. Species and items are stored by
// id so saves survive pack edits. Fails if the active pet's species is gone.
std::string write_save(const Pack& pack, const Save& save);
bool read_save(const Pack& pack, const std::string& text, Save& out);

}  // namespace vpet
