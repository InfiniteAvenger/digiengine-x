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
    pet.stage_mistakes = pet.effort = 0;
    uint16_t base = pack.species[species].care.weight;
    if (pet.weight < base) pet.weight = base;
}

uint32_t tick(const Pack& pack, Pet& pet, uint64_t t, int32_t utc_offset_s) {
    uint32_t ev = 0;

    // First rule (in pack order) whose conditions all hold wins. If none do,
    // the pet stays and the rules are checked again next second.
    const Species& before = pack.species[pet.species];
    for (const EvolveRule& rule : before.evolve) {
        if (evolution_ready(rule, pet, t)) {
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
    if (missed) {
        ev |= EV_MISTAKE;
        pet.stage_mistakes = uint16_t(pet.stage_mistakes + 1);
    }
    return ev;
}

}  // namespace

bool evolution_ready(const EvolveRule& rule, const Pet& pet, uint64_t now_ms) {
    return now_ms >= pet.stage_ms + uint64_t(rule.after_seconds) * 1000 &&
           (rule.max_care_mistakes < 0 || pet.stage_mistakes <= rule.max_care_mistakes) &&
           pet.level >= rule.min_level && pet.effort >= rule.min_effort && pet.run.best >= rule.min_quest_floor;
}

Pet new_pet(const Pack& pack, int species, uint64_t now_ms) {
    Pet p;
    p.species = species;
    p.born_ms = p.stage_ms = p.sim_ms = now_ms;
    p.weight = pack.species[species].care.weight;
    return p;
}

Home new_home(const Pack& pack) {
    Home h;
    h.inventory.assign(pack.items.size(), 0);
    for (auto [item, count] : pack.start_items) h.inventory[item] = uint16_t(count);
    return h;
}

std::optional<uint64_t> lifespan_left_ms(const Pack& pack, const Pet& pet, uint64_t now_ms) {
    if (!pack.lifespan_s || pet.forever || pack.species[pet.species].stage == Stage::Egg) return std::nullopt;
    uint64_t end = pet.born_ms + uint64_t(pack.lifespan_s) * 1000;
    return end > now_ms ? end - now_ms : 0;
}

uint32_t simulate(const Pack& pack, Pet& pet, uint64_t now_ms, int32_t utc_offset_s) {
    // A clock that went backwards just pauses the pet until time catches up.
    if (now_ms <= pet.sim_ms) return 0;
    if (now_ms - pet.sim_ms > kMaxCatchupMs) pet.sim_ms = now_ms - kMaxCatchupMs;
    uint32_t ev = 0;
    while (pet.sim_ms + 1000 <= now_ms) {
        pet.sim_ms += 1000;
        ev |= tick(pack, pet, pet.sim_ms, utc_offset_s);
        std::optional<uint64_t> left = lifespan_left_ms(pack, pet, pet.sim_ms);
        if (left && *left == 0) {
            ev |= EV_DIED;
            break;
        }
    }
    return ev;
}

void freeze(Pet& pet, uint64_t now_ms) { pet.frozen_ms = now_ms; }

void thaw(Pet& pet, uint64_t now_ms) {
    uint64_t dt = now_ms > pet.frozen_ms ? now_ms - pet.frozen_ms : 0;
    pet.born_ms += dt;
    pet.stage_ms += dt;
    pet.sim_ms += dt;
    for (Call* c : {&pet.hunger_call, &pet.strength_call, &pet.lights_call})
        if (c->since_ms) c->since_ms += dt;
    pet.frozen_ms = 0;
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
    if (success) pet.effort = uint16_t(pet.effort + 1);
    if (success && pet.strength < kMaxHearts) {
        ++pet.strength;
        pet.strength_call = Call{};
    }
    if (pet.weight > pack.species[pet.species].care.weight) --pet.weight;
}


namespace {

using Fields = std::map<std::string, std::string>;

void write_pet(std::ostringstream& o, const Pack& pack, const Pet& p, const std::string& prefix) {
    auto key = [&](const char* k) -> std::ostringstream& { return static_cast<std::ostringstream&>(o << prefix << k << ' '); };
    auto call = [&](const char* k, const Call& c) { key(k) << c.since_ms << ' ' << int(c.missed) << '\n'; };
    key("species") << pack.species[p.species].id << '\n';
    key("born") << p.born_ms << '\n';
    key("stage_since") << p.stage_ms << '\n';
    key("sim") << p.sim_ms << '\n';
    key("hunger") << int(p.hunger) << '\n';
    key("strength") << int(p.strength) << '\n';
    key("poop") << int(p.poop) << '\n';
    key("weight") << p.weight << '\n';
    key("mistakes") << p.care_mistakes << '\n';
    key("trainings") << p.trainings << '\n';
    key("stage_mistakes") << p.stage_mistakes << '\n';
    key("effort") << p.effort << '\n';
    key("sick") << int(p.sick) << '\n';
    key("asleep") << int(p.asleep) << '\n';
    key("lights") << int(p.lights_on) << '\n';
    key("forever") << int(p.forever) << '\n';
    key("timers") << p.hunger_s << ' ' << p.strength_s << ' ' << p.poop_s << '\n';
    call("hunger_call", p.hunger_call);
    call("strength_call", p.strength_call);
    call("lights_call", p.lights_call);
    key("level") << p.level << '\n';
    key("xp") << p.xp << '\n';
    key("bonus") << p.bonus.hp << ' ' << p.bonus.atk << ' ' << p.bonus.def << ' ' << p.bonus.spd << '\n';
    key("run") << p.run.floor << ' ' << p.run.best << ' ' << p.run.hp << ' ' << p.run.atk_buff << ' ' << p.run.def_buff
               << '\n';
    if (p.frozen_ms) key("frozen") << p.frozen_ms << '\n';
}

bool read_pet(const Pack& pack, Fields& f, const std::string& prefix, Pet& out) {
    auto get = [&](const char* k) -> std::string& { return f[prefix + k]; };
    Pet p;
    p.species = pack.find_species(get("species"));
    if (p.species < 0) return false;

    auto num = [&](const char* k, auto& v) {
        std::istringstream s(get(k));
        uint64_t x = 0;
        if (s >> x) v = decltype(+v)(x);
    };
    auto flag = [&](const char* k, bool& v) {
        if (f.count(prefix + k)) v = get(k) == "1";
    };
    auto call = [&](const char* k, Call& c) {
        std::istringstream s(get(k));
        int missed = 0;
        s >> c.since_ms >> missed;
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
    num("stage_mistakes", p.stage_mistakes);
    num("effort", p.effort);
    flag("sick", p.sick);
    flag("asleep", p.asleep);
    flag("lights", p.lights_on);
    flag("forever", p.forever);
    std::istringstream(get("timers")) >> p.hunger_s >> p.strength_s >> p.poop_s;
    call("hunger_call", p.hunger_call);
    call("strength_call", p.strength_call);
    call("lights_call", p.lights_call);
    num("level", p.level);
    num("xp", p.xp);
    std::istringstream(get("bonus")) >> p.bonus.hp >> p.bonus.atk >> p.bonus.def >> p.bonus.spd;
    std::istringstream(get("run")) >> p.run.floor >> p.run.best >> p.run.hp >> p.run.atk_buff >> p.run.def_buff;
    num("frozen", p.frozen_ms);

    if (p.hunger > kMaxHearts) p.hunger = kMaxHearts;
    if (p.strength > kMaxHearts) p.strength = kMaxHearts;
    if (p.poop > kMaxPoop) p.poop = kMaxPoop;
    if (p.level < 1) p.level = 1;
    out = p;
    return true;
}

}  // namespace

std::string write_save(const Pack& pack, const Save& s) {
    std::ostringstream o;
    o << "format 1\n";
    write_pet(o, pack, s.active, "");
    for (int i = 0; i < kCryoSlots; ++i)
        if (s.cryo[i]) write_pet(o, pack, *s.cryo[i], "cryo" + std::to_string(i + 1) + ".");

    // "id:count" and "seed_id:planted_ms" tokens.
    o << "inventory";
    for (size_t i = 0; i < s.home.inventory.size(); ++i)
        if (s.home.inventory[i]) o << ' ' << pack.items[i].id << ':' << s.home.inventory[i];
    o << "\ngarden";
    for (const Plot& plot : s.home.garden) {
        if (plot.crop < 0) o << " -";
        else o << ' ' << pack.items[pack.crops[plot.crop].seed].id << ':' << plot.planted_ms;
    }
    o << '\n';
    return o.str();
}

bool read_save(const Pack& pack, const std::string& text, Save& out) {
    Fields f;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t sp = line.find(' ');
        if (sp != std::string::npos) f[line.substr(0, sp)] = line.substr(sp + 1);
    }
    if (f["format"] != "1") return false;

    Save s;
    if (!read_pet(pack, f, "", s.active)) return false;
    for (int i = 0; i < kCryoSlots; ++i) {
        Pet p;
        std::string prefix = "cryo" + std::to_string(i + 1) + ".";
        if (f.count(prefix + "species") && read_pet(pack, f, prefix, p)) s.cryo[i] = p;
    }

    // Items and crops the pack no longer has are dropped.
    s.home.inventory.assign(pack.items.size(), 0);
    if (!f.count("inventory"))  // save from before items existed
        for (auto [item, n] : pack.start_items) s.home.inventory[item] = uint16_t(n);
    std::istringstream inv(f["inventory"]);
    for (std::string tok; inv >> tok;) {
        size_t colon = tok.find(':');
        int idx = pack.find_item(tok.substr(0, colon));
        if (idx >= 0 && colon != std::string::npos)
            s.home.inventory[idx] = uint16_t(std::strtoul(tok.c_str() + colon + 1, nullptr, 10));
    }
    std::istringstream garden(f["garden"]);
    std::string tok;
    for (int i = 0; i < kGardenPlots && garden >> tok; ++i) {
        size_t colon = tok.find(':');
        if (colon == std::string::npos) continue;
        int seed = pack.find_item(tok.substr(0, colon));
        for (size_t c = 0; c < pack.crops.size(); ++c)
            if (pack.crops[c].seed == seed) {
                s.home.garden[i].crop = int(c);
                s.home.garden[i].planted_ms = std::strtoull(tok.c_str() + colon + 1, nullptr, 10);
            }
    }
    out = std::move(s);
    return true;
}

}  // namespace vpet
