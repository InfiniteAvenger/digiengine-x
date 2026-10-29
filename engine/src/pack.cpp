#include "vpet/pack.h"
#include <algorithm>
#include <cctype>
#include "vpet/json.h"
#include "vpet/platform.h"
#include "vpet/png.h"

namespace vpet {

namespace {
constexpr const char* kStageNames[] = {"egg", "baby1", "baby2", "child", "adult", "perfect", "ultimate", "super_ultimate"};
constexpr const char* kStageLabels[] = {"EGG", "BABY I", "BABY II", "CHILD", "ADULT", "PERFECT", "ULTIMATE", "SUPER ULT."};
constexpr const char* kRarityNames[] = {"common", "uncommon", "rare", "epic"};
constexpr const char* kGameNames[] = {"timing", "mash", "sequence", "dice"};

template <size_t N>
int index_of(const char* const (&names)[N], const std::string& s) {
    for (size_t i = 0; i < N; ++i)
        if (s == names[i]) return int(i);
    return -1;
}

void read_theme_color(const Json& theme, const char* key, Color& out) {
    Color c;
    if (parse_hex_color(theme[key].str_or(""), c)) out = c;
}

// "HH:MM" -> minutes after midnight.
bool parse_clock(const std::string& s, uint16_t& out) {
    if (s.size() != 5 || s[2] != ':') return false;
    for (int i : {0, 1, 3, 4})
        if (s[i] < '0' || s[i] > '9') return false;
    int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h > 23 || m > 59) return false;
    out = uint16_t(h * 60 + m);
    return true;
}

bool read_care(const Json& j, Care& care, std::string& err) {
    auto minutes = [&](const char* key, uint32_t& out) {
        double v = j[key].num_or(-1);
        if (v > 0) out = uint32_t(v * 60.0);
    };
    minutes("hunger_minutes", care.hunger_s);
    minutes("strength_minutes", care.strength_s);
    minutes("poop_minutes", care.poop_s);
    care.weight = uint16_t(j["weight"].int_or(care.weight));
    care.hearts = uint8_t(std::clamp(j["hearts"].int_or(care.hearts), 1, int(kMaxHeartsCap)));
    for (auto [key, out] : {std::pair<const char*, uint16_t*>{"sleep", &care.sleep_min}, {"wake", &care.wake_min}}) {
        if (j[key].is_null()) continue;
        if (!parse_clock(j[key].str_or(""), *out)) {
            err = std::string("care.") + key + " must look like \"21:00\"";
            return false;
        }
    }
    return true;
}

void read_stats(const Json& j, Stats& s) {
    s.hp = j["hp"].int_or(s.hp);
    s.atk = j["atk"].int_or(s.atk);
    s.def = j["def"].int_or(s.def);
    s.spd = j["spd"].int_or(s.spd);
}

void read_effect(const Json& j, Effect& e) {
    e.hunger = j["hunger"].int_or(0);
    e.strength = j["strength"].int_or(0);
    e.cure = j["cure"].bool_or(false);
    read_stats(j, e.bonus);
    e.heal = j["heal"].int_or(0);
    e.atk_buff = j["atk_buff"].int_or(0);
    e.def_buff = j["def_buff"].int_or(0);
    e.forever = j["forever"].bool_or(false);
}

// Loads a .spr text sprite or a .png sheet. For PNGs, `owner` may give
// "frame_size": [w, h]; by default frames are squares as tall as the image,
// laid out left to right.
bool load_sprite_file(Platform& platform, const std::string& dir, const std::string& rel, Sprite& out,
                      std::string& err, const Json& owner = Json()) {
    std::string path = dir + "/" + rel, text;
    if (!platform.read_file(path, text)) {
        err = "cannot read " + path;
        return false;
    }
    bool png = rel.size() > 4 && (rel.compare(rel.size() - 4, 4, ".png") == 0 || rel.compare(rel.size() - 4, 4, ".PNG") == 0);
    if (png) {
        Image img;
        const Json& fs = owner["frame_size"];
        if (!decode_png(text, img, err) || !sprite_from_image(img, fs[size_t(0)].int_or(0), fs[size_t(1)].int_or(0), out, err)) {
            err = path + ": " + err;
            return false;
        }
        return true;
    }
    if (!parse_sprite(text, out, err)) {
        err = path + " " + err;
        return false;
    }
    return true;
}

// Resolves an item id, reporting `where` on failure.
bool item_ref(const Pack& pack, const std::string& id, const std::string& where, int& out, std::string& err) {
    out = pack.find_item(id);
    if (out < 0) err = where + ": unknown item '" + id + "'";
    return out >= 0;
}

bool load_items(const Json& list, bool material, Pack& out, std::string& err) {
    for (size_t i = 0; i < list.size(); ++i) {
        const Json& j = list[i];
        ItemDef it;
        it.id = j["id"].str_or("");
        it.name = j["name"].str_or(it.id);
        it.info = j["info"].str_or("");
        it.material = material;
        if (it.id.empty() || out.find_item(it.id) >= 0) {
            err = "item '" + it.id + "': missing or duplicate id";
            return false;
        }
        int r = index_of(kRarityNames, j["rarity"].str_or("common"));
        if (r < 0) {
            err = it.id + ": rarity must be common, uncommon, rare or epic";
            return false;
        }
        it.rarity = Rarity(r);
        read_effect(j["effect"], it.effect);
        it.icon = j["icon"].int_or(-1);
        out.items.push_back(std::move(it));
    }
    return true;
}
}  // namespace

const char* stage_name(Stage s) { return kStageNames[int(s)]; }
const char* stage_label(Stage s) { return kStageLabels[int(s)]; }
const char* rarity_name(Rarity r) { return kRarityNames[int(r)]; }
const char* minigame_name(MiniGame g) { return kGameNames[int(g)]; }

Care default_care(Stage s) {
    //                 hunger  strength  poop    sleep    wake    weight  hearts
    switch (s) {
        case Stage::Egg:           return {0, 0, 0, 0, 0, 0, 1};
        case Stage::Baby1:         return {3 * 60, 3 * 60, 3 * 60, 20 * 60, 9 * 60, 5, 1};
        case Stage::Baby2:         return {30 * 60, 30 * 60, 60 * 60, 20 * 60, 9 * 60, 10, 1};
        case Stage::Child:         return {48 * 60, 48 * 60, 120 * 60, 21 * 60, 8 * 60, 20, 2};
        case Stage::Adult:         return {59 * 60, 59 * 60, 120 * 60, 22 * 60, 8 * 60, 30, 3};
        case Stage::Perfect:       return {66 * 60, 66 * 60, 120 * 60, 23 * 60, 9 * 60, 40, 4};
        case Stage::Ultimate:      return {75 * 60, 75 * 60, 120 * 60, 23 * 60, 9 * 60, 50, 5};
        case Stage::SuperUltimate: return {85 * 60, 85 * 60, 150 * 60, 23 * 60, 9 * 60, 60, 5};
    }
    return {};
}

Stats default_stats(Stage s) {
    switch (s) {
        case Stage::Egg:           return {1, 1, 1, 1};
        case Stage::Baby1:         return {20, 5, 4, 5};
        case Stage::Baby2:         return {30, 8, 6, 6};
        case Stage::Child:         return {45, 12, 9, 9};
        case Stage::Adult:         return {70, 18, 14, 12};
        case Stage::Perfect:       return {100, 26, 20, 16};
        case Stage::Ultimate:      return {140, 36, 28, 20};
        case Stage::SuperUltimate: return {180, 46, 36, 25};
    }
    return {};
}

bool parse_stage(const std::string& s, Stage& out) {
    int i = index_of(kStageNames, s);
    if (i < 0 && (s == "infant" || s == "baby")) i = int(Stage::Baby1);  // names from older packs
    if (i >= 0) out = Stage(i);
    return i >= 0;
}

int Pack::find_species(const std::string& id) const {
    for (size_t i = 0; i < species.size(); ++i)
        if (species[i].id == id) return int(i);
    return -1;
}

int Pack::find_item(const std::string& id) const {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].id == id) return int(i);
    return -1;
}

int Pack::find_move(const std::string& id) const {
    for (size_t i = 0; i < moves.size(); ++i)
        if (moves[i].id == id) return int(i);
    return -1;
}

bool load_pack(Platform& platform, const std::string& dir, Pack& out, std::string& err) {
    out = Pack{};
    std::string text;
    if (!platform.read_file(dir + "/pack.json", text)) {
        err = "cannot read " + dir + "/pack.json";
        return false;
    }
    Json root;
    if (!Json::parse(text, root, err)) return false;
    if (root["format"].int_or(0) != 1) {
        err = "pack.json: unsupported format (expected 1)";
        return false;
    }

    out.id = root["id"].str_or("");
    if (out.id.empty()) {
        err = "pack.json: missing \"id\"";
        return false;
    }
    out.name = root["name"].str_or(out.id);
    out.version = root["version"].str_or("");
    out.author = root["author"].str_or("");
    out.license = root["license"].str_or("");

    const Json& theme = root["theme"];
    read_theme_color(theme, "bg_top", out.theme.bg_top);
    read_theme_color(theme, "bg_bottom", out.theme.bg_bottom);
    read_theme_color(theme, "ui", out.theme.ui);
    read_theme_color(theme, "ui_text", out.theme.ui_text);
    read_theme_color(theme, "accent", out.theme.accent);

    // Items and moves first: everything else refers to them by id.
    if (!load_items(root["materials"], true, out, err) || !load_items(root["items"], false, out, err)) return false;
    if (!root["item_icons"].is_null()) {
        if (!load_sprite_file(platform, dir, root["item_icons"].str_or(""), out.item_icons, err)) return false;
        for (const ItemDef& it : out.items)
            if (it.icon >= out.item_icons.frames) {
                err = it.id + ": icon " + std::to_string(it.icon) + " is past the end of item_icons";
                return false;
            }
    }

    const Json& moves = root["moves"];
    for (size_t i = 0; i < moves.size(); ++i) {
        Move m;
        m.id = moves[i]["id"].str_or("");
        m.name = moves[i]["name"].str_or(m.id);
        m.power = moves[i]["power"].int_or(10);
        int g = index_of(kGameNames, moves[i]["game"].str_or("timing"));
        if (m.id.empty() || g < 0) {
            err = "move '" + m.id + "': missing id or unknown game (timing, mash, sequence, dice)";
            return false;
        }
        m.game = MiniGame(g);
        out.moves.push_back(std::move(m));
    }
    auto move_ref = [&](const std::string& id, const std::string& where, int& idx) {
        idx = out.find_move(id);
        if (idx < 0) err = where + ": unknown move '" + id + "'";
        return idx >= 0;
    };

    // "attributes": { "a": { "beats": ["b"] }, ... }
    const Json& attrs = root["attributes"];
    for (size_t i = 0; i < attrs.size(); ++i) {
        std::vector<std::string> beats;
        for (size_t b = 0; b < attrs[i]["beats"].size(); ++b) beats.push_back(attrs[i]["beats"][b].str_or(""));
        out.attributes.push_back({attrs.key(i), std::move(beats)});
    }

    // Pass 1: species and sprites. Evolution targets are resolved after,
    // because a rule can point at a species defined later in the file.
    const Json& species = root["species"];
    for (size_t i = 0; i < species.size(); ++i) {
        const Json& js = species[i];
        Species s;
        s.id = js["id"].str_or("");
        s.name = js["name"].str_or(s.id);
        if (s.id.empty()) {
            err = "species #" + std::to_string(i) + " has no id";
            return false;
        }
        if (out.find_species(s.id) >= 0) {
            err = "duplicate species id '" + s.id + "'";
            return false;
        }
        if (!parse_stage(js["stage"].str_or(""), s.stage)) {
            err = s.id + ": unknown stage '" + js["stage"].str_or("") + "'";
            return false;
        }
        s.care = default_care(s.stage);
        if (!read_care(js["care"], s.care, err)) {
            err = s.id + ": " + err;
            return false;
        }
        s.stats = default_stats(s.stage);
        s.family = js["family"].str_or("");
        s.attribute = js["attribute"].str_or("");
        s.enemy = js["enemy"].bool_or(true);
        read_stats(js["stats"], s.stats);
        const Json& learns = js["learns"];
        for (size_t l = 0; l < learns.size(); ++l) {
            LearnRule rule;
            rule.level = learns[l]["level"].int_or(1);
            if (!move_ref(learns[l]["move"].str_or(""), s.id, rule.move)) return false;
            s.learns.push_back(rule);
        }
        if (!load_sprite_file(platform, dir, js["sprite"].str_or(""), s.sprite, err, js)) return false;
        out.species.push_back(std::move(s));
    }

    for (size_t i = 0; i < species.size(); ++i) {
        const Json& rules = species[i]["evolve"];
        for (size_t r = 0; r < rules.size(); ++r) {
            EvolveRule rule;
            std::string to = rules[r]["to"].str_or("");
            rule.to = out.find_species(to);
            if (rule.to < 0) {
                err = out.species[i].id + ": evolves to unknown species '" + to + "'";
                return false;
            }
            const Json& jr = rules[r];
            double hours = jr["hours_in_stage"].num_or(-1);
            rule.after_seconds = uint32_t(hours >= 0 ? hours * 3600.0 : jr["after_minutes"].num_or(0) * 60.0);
            rule.max_care_mistakes = jr["max_care_mistakes"].int_or(-1);
            rule.min_level = jr["min_level"].int_or(0);
            rule.min_effort = jr["min_effort"].int_or(0);
            rule.min_quest_floor = jr["min_quest_floor"].int_or(0);
            out.species[i].evolve.push_back(rule);
        }
    }

    const Json& rosters = root["rosters"];
    for (size_t i = 0; i < rosters.size(); ++i) {
        Roster ro;
        ro.id = rosters[i]["id"].str_or("");
        ro.name = rosters[i]["name"].str_or(ro.id);
        const Json& eggs = rosters[i]["eggs"];
        for (size_t e = 0; e < eggs.size(); ++e) {
            int idx = out.find_species(eggs[e].str_or(""));
            if (idx < 0) {
                err = "roster " + ro.id + ": unknown egg '" + eggs[e].str_or("") + "'";
                return false;
            }
            ro.eggs.push_back(idx);
        }
        out.rosters.push_back(std::move(ro));
    }

    auto read_drops = [&](const Json& list, const std::string& where, std::vector<Drop>& drops) {
        for (size_t d = 0; d < list.size(); ++d) {
            Drop drop;
            if (!item_ref(out, list[d]["item"].str_or(""), where, drop.item, err)) return false;
            drop.chance = float(list[d]["chance"].num_or(0));
            drops.push_back(drop);
        }
        return true;
    };

    // Shared drop tables: "families": { "beast": { "drops": [...] }, ... }
    const Json& families = root["families"];
    for (size_t i = 0; i < families.size(); ++i) {
        std::vector<Drop> drops;
        if (!read_drops(families[i]["drops"], "family " + families.key(i), drops)) return false;
        out.family_drops.push_back({families.key(i), std::move(drops)});
    }
    auto family_table = [&](const std::string& family) -> const std::vector<Drop>* {
        for (const auto& [name, drops] : out.family_drops)
            if (name == family) return &drops;
        return nullptr;
    };

    // Which creature stages appear on which floors.
    const Json& tiers = root["quest"]["tiers"];
    if (tiers.is_null()) {
        out.tiers = {{Stage::Child, 1, 9},     {Stage::Adult, 10, 19},    {Stage::Perfect, 20, 29},
                     {Stage::Ultimate, 30, 49}, {Stage::SuperUltimate, 40, 0}};
    } else {
        for (size_t i = 0; i < tiers.size(); ++i) {
            EnemyTier tier;
            if (!parse_stage(tiers[i]["stage"].str_or(""), tier.stage)) {
                err = "quest.tiers: unknown stage '" + tiers[i]["stage"].str_or("") + "'";
                return false;
            }
            tier.min_floor = tiers[i]["min_floor"].int_or(1);
            tier.max_floor = tiers[i]["max_floor"].int_or(0);
            out.tiers.push_back(tier);
        }
    }

    const Json& enemies = root["enemies"];
    for (size_t i = 0; i < enemies.size(); ++i) {
        const Json& je = enemies[i];
        Enemy e;
        e.id = je["id"].str_or("");
        e.name = je["name"].str_or(e.id);
        e.family = je["family"].str_or("");
        e.attribute = je["attribute"].str_or("");
        e.boss = je["boss"].bool_or(false);
        e.min_floor = je["min_floor"].int_or(1);
        e.max_floor = je["max_floor"].int_or(0);
        e.stats = default_stats(Stage::Baby1);
        read_stats(je["stats"], e.stats);
        for (size_t m = 0; m < je["moves"].size(); ++m) {
            int idx;
            if (!move_ref(je["moves"][m].str_or(""), e.id, idx)) return false;
            e.moves.push_back(idx);
        }
        if (e.moves.empty()) {
            err = e.id + ": enemies need at least one move";
            return false;
        }
        if (!read_drops(je["drops"], e.id, e.drops)) return false;
        if (e.drops.empty())
            if (const auto* table = family_table(e.family)) e.drops = *table;
        if (!load_sprite_file(platform, dir, je["sprite"].str_or(""), e.sprite, err, je)) return false;
        out.enemies.push_back(std::move(e));
    }

    // Every creature whose stage has a tier also fights in quests, using its
    // own art, stats and moves, and its family's drops.
    if (root["quest"]["species_enemies"].bool_or(true)) {
        for (size_t s = 0; s < out.species.size(); ++s) {
            const Species& sp = out.species[s];
            if (!sp.enemy) continue;
            for (const EnemyTier& tier : out.tiers) {
                if (tier.stage != sp.stage) continue;
                Enemy e;
                e.id = sp.id;
                e.name = sp.name;
                for (char& c : e.name) c = char(toupper(static_cast<unsigned char>(c)));
                e.family = sp.family;
                e.attribute = sp.attribute;
                e.sprite = sp.sprite;
                e.stats = sp.stats;
                e.species = int(s);
                e.min_floor = tier.min_floor;
                e.max_floor = tier.max_floor;
                for (const LearnRule& r : sp.learns)
                    if (std::find(e.moves.begin(), e.moves.end(), r.move) == e.moves.end()) e.moves.push_back(r.move);
                if (e.moves.empty() && !out.moves.empty()) e.moves.push_back(0);
                if (e.moves.empty()) continue;  // a pack with no moves at all
                if (const auto* table = family_table(e.family)) e.drops = *table;
                out.enemies.push_back(std::move(e));
            }
        }
    }

    const Json& crops = root["crops"];
    for (size_t i = 0; i < crops.size(); ++i) {
        const Json& jc = crops[i];
        Crop c;
        c.name = jc["name"].str_or("");
        if (!item_ref(out, jc["seed"].str_or(""), "crop " + c.name, c.seed, err) ||
            !item_ref(out, jc["yields"].str_or(""), "crop " + c.name, c.yields, err))
            return false;
        c.count = jc["count"].int_or(1);
        c.grow_s = uint32_t(jc["minutes"].num_or(30) * 60.0);
        if (!load_sprite_file(platform, dir, jc["sprite"].str_or(""), c.sprite, err, jc)) return false;
        if (c.sprite.frames != 4) {
            err = "crop " + c.name + ": sprite needs exactly 4 frames";
            return false;
        }
        out.crops.push_back(std::move(c));
    }

    // {"item_id": count, ...} -> (index, count) pairs
    auto amounts = [&](const Json& obj, const std::string& where, std::vector<std::pair<int, int>>& list) {
        for (size_t n = 0; n < obj.size(); ++n) {
            int idx;
            if (!item_ref(out, obj.key(n), where, idx, err)) return false;
            list.push_back({idx, obj[n].int_or(1)});
        }
        return true;
    };

    const Json& recipes = root["recipes"];
    for (size_t i = 0; i < recipes.size(); ++i) {
        Recipe r;
        if (!item_ref(out, recipes[i]["makes"].str_or(""), "recipe", r.makes, err)) return false;
        r.count = recipes[i]["count"].int_or(1);
        if (!amounts(recipes[i]["needs"], "recipe " + out.items[r.makes].id, r.needs)) return false;
        out.recipes.push_back(std::move(r));
    }

    if (!amounts(root["start_items"], "start_items", out.start_items)) return false;

    // Attribute names must be ones the pack defines (when it defines any).
    if (!out.attributes.empty()) {
        auto known = [&](const std::string& a) {
            if (a.empty()) return true;
            for (const auto& [name, beats] : out.attributes)
                if (name == a) return true;
            return false;
        };
        for (const auto& [name, beats] : out.attributes)
            for (const std::string& b : beats)
                if (!known(b)) return err = "attribute " + name + " beats unknown attribute '" + b + "'", false;
        for (const Species& s : out.species)
            if (!known(s.attribute)) return err = s.id + ": unknown attribute '" + s.attribute + "'", false;
        for (const Enemy& e : out.enemies)
            if (!known(e.attribute)) return err = e.id + ": unknown attribute '" + e.attribute + "'", false;
    }
    out.lifespan_s = uint32_t(root["lifespan_days"].num_or(0) * 86400.0);

    if (out.rosters.empty() || out.rosters[0].eggs.empty()) {
        err = "pack needs at least one roster with an egg";
        return false;
    }
    return true;
}

}  // namespace vpet
