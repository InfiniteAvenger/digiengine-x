#include "vpet/pet.h"
#include <cstdlib>
#include <map>
#include <sstream>

namespace vpet {
namespace {

constexpr uint64_t kMaxCatchupMs = 30ull * 24 * 3600 * 1000;

int minute_of_day(uint64_t t_ms, int32_t utc_offset_s) {
    int64_t local_s = int64_t(t_ms / 1000) + utc_offset_s;
    return int(((local_s / 60) % 1440 + 1440) % 1440);
}

bool in_sleep_window(int minute, const Care& c) {
    if (c.sleep_min == c.wake_min) return false;
    if (c.sleep_min < c.wake_min) return minute >= c.sleep_min && minute < c.wake_min;
    return minute >= c.sleep_min || minute < c.wake_min;  // window crosses midnight
}

// Returns true when this call just turned into a care mistake.
bool update_call(Call& call, bool needed, uint64_t t, uint16_t& mistakes) {
    if (!needed) {
        call = Call{};
        return false;
    }
    if (call.since_ms == 0) call.since_ms = t;
    if (!call.missed && t - call.since_ms >= uint64_t(kCallTimeoutS) * 1000) {
        call.missed = true;
        ++mistakes;
        return true;
    }
    return false;
}

void enter_form(const Pack& pack, Pet& pet, int species, uint64_t t) {
    pet.species = species;
    pet.stage_ms = t;
    pet.hunger_s = pet.strength_s = pet.poop_s = 0;
    uint16_t base = pack.species[species].care.weight;
    if (pet.weight < base) pet.weight = base;
}

uint32_t tick(const Pack& pack, Pet& pet, uint64_t t, int32_t utc_offset_s) {
    uint32_t ev = 0;

    const Species& before = pack.species[pet.species];
    for (const EvolveRule& rule : before.evolve) {
        if (t >= pet.stage_ms + uint64_t(rule.after_seconds) * 1000) {
            ev |= before.stage == Stage::Egg ? EV_HATCHED : EV_EVOLVED;
            enter_form(pack, pet, rule.to, t);
            break;
        }
    }

    const Species& s = pack.species[pet.species];
    if (s.stage == Stage::Egg) return ev;
    const Care& c = s.care;

    bool sleepy = in_sleep_window(minute_of_day(t, utc_offset_s), c);
    if (sleepy != pet.asleep) {
        pet.asleep = sleepy;
        ev |= sleepy ? EV_SLEPT : EV_WOKE;
        if (!sleepy) pet.lights_on = true;
    }

    if (!pet.asleep) {
        if (c.hunger_s && ++pet.hunger_s >= c.hunger_s) {
            pet.hunger_s = 0;
            if (pet.hunger) --pet.hunger;
        }
        if (c.strength_s && ++pet.strength_s >= c.strength_s) {
            pet.strength_s = 0;
            if (pet.strength) --pet.strength;
        }
        if (c.poop_s && ++pet.poop_s >= c.poop_s) {
            pet.poop_s = 0;
            if (pet.poop < kMaxPoop) {
                ++pet.poop;
                ev |= EV_POOPED;
                if (pet.poop == kMaxPoop && !pet.sick) {
                    pet.sick = true;
                    ev |= EV_SICK;
                }
            }
        }
    }

    // Hunger and strength calls only run while awake, so nights are safe.
    bool missed = false;
    missed |= update_call(pet.hunger_call, pet.hunger == 0 && !pet.asleep, t, pet.care_mistakes);
    missed |= update_call(pet.strength_call, pet.strength == 0 && !pet.asleep, t, pet.care_mistakes);
    missed |= update_call(pet.lights_call, pet.asleep && pet.lights_on, t, pet.care_mistakes);
    if (missed) ev |= EV_MISTAKE;
    return ev;
}

}  // namespace

Pet new_pet(const Pack& pack, int species, uint64_t now_ms) {
    Pet p;
    p.species = species;
    p.born_ms = p.stage_ms = p.sim_ms = now_ms;
    p.weight = pack.species[species].care.weight;
    p.inventory.assign(pack.items.size(), 0);
    for (auto [item, count] : pack.start_items) p.inventory[item] = uint16_t(count);
    return p;
}

uint32_t simulate(const Pack& pack, Pet& pet, uint64_t now_ms, int32_t utc_offset_s) {
    // A clock that went backwards just pauses the pet until time catches up.
    if (now_ms <= pet.sim_ms) return 0;
    if (now_ms - pet.sim_ms > kMaxCatchupMs) pet.sim_ms = now_ms - kMaxCatchupMs;
    uint32_t ev = 0;
    while (pet.sim_ms + 1000 <= now_ms) {
        pet.sim_ms += 1000;
        ev |= tick(pack, pet, pet.sim_ms, utc_offset_s);
    }
    return ev;
}

bool feed_kibble(Pet& pet) {
    if (pet.hunger >= kMaxHearts) return false;
    ++pet.hunger;
    ++pet.weight;
    pet.hunger_call = Call{};  // answered now rather than on the next tick
    return true;
}

bool give_medicine(Pet& pet) {
    if (!pet.sick) return false;
    pet.sick = false;
    return true;
}

bool clean_up(Pet& pet) {
    if (!pet.poop) return false;
    pet.poop = 0;
    return true;
}

void toggle_lights(Pet& pet) {
    pet.lights_on = !pet.lights_on;
    if (!pet.lights_on) pet.lights_call = Call{};
}

void finish_training(const Pack& pack, Pet& pet, bool success) {
    ++pet.trainings;
    if (success && pet.strength < kMaxHearts) {
        ++pet.strength;
        pet.strength_call = Call{};
    }
    if (pet.weight > pack.species[pet.species].care.weight) --pet.weight;
}

std::string save_pet(const Pack& pack, const Pet& p) {
    std::ostringstream o;
    auto call = [&](const char* key, const Call& c) { o << key << ' ' << c.since_ms << ' ' << int(c.missed) << '\n'; };
    o << "format 1\n"
      << "species " << pack.species[p.species].id << '\n'
      << "born " << p.born_ms << '\n'
      << "stage_since " << p.stage_ms << '\n'
      << "sim " << p.sim_ms << '\n'
      << "hunger " << int(p.hunger) << '\n'
      << "strength " << int(p.strength) << '\n'
      << "poop " << int(p.poop) << '\n'
      << "weight " << p.weight << '\n'
      << "mistakes " << p.care_mistakes << '\n'
      << "trainings " << p.trainings << '\n'
      << "sick " << int(p.sick) << '\n'
      << "asleep " << int(p.asleep) << '\n'
      << "lights " << int(p.lights_on) << '\n'
      << "timers " << p.hunger_s << ' ' << p.strength_s << ' ' << p.poop_s << '\n';
    call("hunger_call", p.hunger_call);
    call("strength_call", p.strength_call);
    call("lights_call", p.lights_call);

    o << "level " << p.level << '\n'
      << "xp " << p.xp << '\n'
      << "bonus " << p.bonus.hp << ' ' << p.bonus.atk << ' ' << p.bonus.def << ' ' << p.bonus.spd << '\n'
      << "run " << p.run.floor << ' ' << p.run.best << ' ' << p.run.hp << ' ' << p.run.atk_buff << ' '
      << p.run.def_buff << '\n';
    o << "inventory";
    for (size_t i = 0; i < p.inventory.size(); ++i)
        if (p.inventory[i]) o << ' ' << pack.items[i].id << ':' << p.inventory[i];
    o << "\ngarden";
    for (const Plot& plot : p.garden) {
        if (plot.crop < 0) o << " -";
        else o << ' ' << pack.items[pack.crops[plot.crop].seed].id << ':' << plot.planted_ms;
    }
    o << '\n';
    return o.str();
}

bool load_pet(const Pack& pack, const std::string& text, Pet& out) {
    std::map<std::string, std::string> fields;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t sp = line.find(' ');
        if (sp != std::string::npos) fields[line.substr(0, sp)] = line.substr(sp + 1);
    }
    if (fields["format"] != "1") return false;

    Pet p;
    p.species = pack.find_species(fields["species"]);
    if (p.species < 0) return false;

    auto num = [&](const char* key, auto& v) {
        std::istringstream f(fields[key]);
        uint64_t x = 0;
        f >> x;
        v = decltype(+v)(x);
    };
    auto flag = [&](const char* key, bool& v) { v = fields[key] == "1"; };
    auto call = [&](const char* key, Call& c) {
        std::istringstream f(fields[key]);
        int missed = 0;
        f >> c.since_ms >> missed;
        c.missed = missed != 0;
    };

    num("born", p.born_ms);
    num("stage_since", p.stage_ms);
    num("sim", p.sim_ms);
    num("hunger", p.hunger);
    num("strength", p.strength);
    num("poop", p.poop);
    num("weight", p.weight);
    num("mistakes", p.care_mistakes);
    num("trainings", p.trainings);
    flag("sick", p.sick);
    flag("asleep", p.asleep);
    flag("lights", p.lights_on);
    std::istringstream timers(fields["timers"]);
    timers >> p.hunger_s >> p.strength_s >> p.poop_s;
    call("hunger_call", p.hunger_call);
    call("strength_call", p.strength_call);
    call("lights_call", p.lights_call);

    if (fields.count("level")) num("level", p.level);
    num("xp", p.xp);
    std::istringstream(fields["bonus"]) >> p.bonus.hp >> p.bonus.atk >> p.bonus.def >> p.bonus.spd;
    if (fields.count("run"))
        std::istringstream(fields["run"]) >> p.run.floor >> p.run.best >> p.run.hp >> p.run.atk_buff >> p.run.def_buff;

    // "id:count" / "seed_id:planted_ms" tokens; ids the pack no longer has are dropped.
    p.inventory.assign(pack.items.size(), 0);
    if (!fields.count("inventory"))  // save from before items existed
        for (auto [item, n] : pack.start_items) p.inventory[item] = uint16_t(n);
    std::istringstream inv(fields["inventory"]);
    for (std::string tok; inv >> tok;) {
        size_t colon = tok.find(':');
        int idx = pack.find_item(tok.substr(0, colon));
        if (idx >= 0 && colon != std::string::npos)
            p.inventory[idx] = uint16_t(std::strtoul(tok.c_str() + colon + 1, nullptr, 10));
    }
    std::istringstream garden(fields["garden"]);
    std::string tok;
    for (int i = 0; i < kGardenPlots && garden >> tok; ++i) {
        size_t colon = tok.find(':');
        if (colon == std::string::npos) continue;
        int seed = pack.find_item(tok.substr(0, colon));
        for (size_t c = 0; c < pack.crops.size(); ++c)
            if (pack.crops[c].seed == seed) {
                p.garden[i].crop = int(c);
                p.garden[i].planted_ms = std::strtoull(tok.c_str() + colon + 1, nullptr, 10);
            }
    }

    if (p.hunger > kMaxHearts) p.hunger = kMaxHearts;
    if (p.strength > kMaxHearts) p.strength = kMaxHearts;
    if (p.poop > kMaxPoop) p.poop = kMaxPoop;
    out = p;
    return true;
}

}  // namespace vpet
