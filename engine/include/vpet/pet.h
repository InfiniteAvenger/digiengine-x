#pragma once
#include <cstdint>
#include <string>
#include "vpet/pack.h"

namespace vpet {

constexpr int kMaxHearts = 4;
constexpr int kMaxPoop = 4;
constexpr uint32_t kCallTimeoutS = 10 * 60;  // an ignored call becomes a care mistake

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
};

struct Pet {
    int species = -1;
    uint64_t born_ms = 0;
    uint64_t stage_ms = 0;  // when the current form was reached
    uint64_t sim_ms = 0;    // simulated up to this time

    uint8_t hunger = 0, strength = 0, poop = 0;  // hearts / poop count
    uint16_t weight = 0;
    uint16_t care_mistakes = 0;
    uint16_t trainings = 0;
    bool sick = false;
    bool asleep = false;
    bool lights_on = true;

    // Awake seconds since the last hunger/strength drop and the last poop.
    uint32_t hunger_s = 0, strength_s = 0, poop_s = 0;

    Call hunger_call, strength_call, lights_call;

    // RPG progress
    int level = 1;
    uint32_t xp = 0;
    Stats bonus;  // permanent, from chips
    Run run;

    // Home (will move out of Pet when cold storage holds several pets)
    std::vector<uint16_t> inventory;  // count per pack.items index
    Plot garden[kGardenPlots];

    bool needs_attention() const { return hunger_call.active() || strength_call.active() || lights_call.active(); }
};

enum SimEvent : uint32_t {
    EV_HATCHED = 1 << 0,
    EV_EVOLVED = 1 << 1,
    EV_SLEPT = 1 << 2,
    EV_WOKE = 1 << 3,
    EV_POOPED = 1 << 4,
    EV_SICK = 1 << 5,
    EV_MISTAKE = 1 << 6,
};

Pet new_pet(const Pack& pack, int species, uint64_t now_ms);

// Advances the pet to now_ms in one-second steps, so a game that was closed
// for hours catches up exactly as if it had been running (capped at 30 days).
// Returns a mask of SimEvents that happened.
uint32_t simulate(const Pack& pack, Pet& pet, uint64_t now_ms, int32_t utc_offset_s);

// Care actions. Each returns false when it has no effect (full, not sick...).
bool feed_kibble(Pet& pet);
bool give_medicine(Pet& pet);
bool clean_up(Pet& pet);
void toggle_lights(Pet& pet);
void finish_training(const Pack& pack, Pet& pet, bool success);

// Plain "key value" text, one field per line. Species are stored by id so
// saves survive pack edits; an unknown id makes load_pet fail.
std::string save_pet(const Pack& pack, const Pet& pet);
bool load_pet(const Pack& pack, const std::string& text, Pet& out);

}  // namespace vpet
