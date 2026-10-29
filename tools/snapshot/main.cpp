// Headless runner: plays a scripted session against the demo pack with a fake
// clock, checks the pet's state along the way, and writes chosen frames as
// 24-bit BMPs (scaled 4x). Exit code is the number of failed checks.
//   vpet_snapshot <pack_dir> <out_dir>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include "vpet/game.h"
#include "baker.h"
#include "vpet/minigame.h"
#include "vpet/png.h"
#include "vpet/rpg.h"

namespace {

class FakePlatform : public vpet::Platform {
public:
    uint64_t clock = 1'700'000'000'000ull;  // 22:13 UTC
    int32_t offset = -10 * 3600;            // so local time starts at 12:13
    uint8_t held = 0;
    vpet::Framebuffer last;
    std::map<std::string, std::string> saves;

    uint64_t now_ms() override { return clock; }
    int32_t utc_offset_s() override { return offset; }
    uint8_t buttons() override { return held; }
    void present(const vpet::Framebuffer& fb) override { last = fb; }
    bool read_file(const std::string& path, std::string& out) override {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }
    bool load_save(const std::string& slot, std::string& out) override {
        auto it = saves.find(slot);
        if (it == saves.end()) return false;
        out = it->second;
        return true;
    }
    bool write_save(const std::string& slot, const std::string& data) override {
        saves[slot] = data;
        return true;
    }
    std::vector<std::string> list_dirs(const std::string& path) override {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(path, ec))
            if (e.is_directory()) names.push_back(e.path().filename().string());
        std::sort(names.begin(), names.end());
        return names;
    }
};

void write_bmp(const std::string& path, const vpet::Framebuffer& fb, int scale = 4) {
    const int w = vpet::kScreenW * scale, h = vpet::kScreenH * scale;
    const int row = w * 3;  // multiple of 4 because w is
    const uint32_t size = 54 + uint32_t(row * h);
    unsigned char hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, uint32_t v) { for (int i = 0; i < 4; ++i) hdr[off + i] = uint8_t(v >> (8 * i)); };
    put32(2, size);
    put32(10, 54);
    put32(14, 40);
    put32(18, uint32_t(w));
    put32(22, uint32_t(h));
    hdr[26] = 1;
    hdr[28] = 24;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<char*>(hdr), 54);
    std::string line(size_t(row), '\0');
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            vpet::Color c = fb.pixels()[(y / scale) * vpet::kScreenW + x / scale];
            line[size_t(x * 3 + 0)] = char((c & 0x1F) << 3);
            line[size_t(x * 3 + 1)] = char(((c >> 5) & 0x3F) << 2);
            line[size_t(x * 3 + 2)] = char((c >> 11) << 3);
        }
        f.write(line.data(), row);
    }
}

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}

bool same_pixels(const vpet::Sprite& a, const vpet::Sprite& b) {
    if (a.w != b.w || a.h != b.h || a.frames != b.frames || a.pixels.size() != b.pixels.size()) return false;
    for (size_t i = 0; i < a.pixels.size(); ++i) {
        bool ta = a.pixels[i] == 0, tb = b.pixels[i] == 0;
        if (ta != tb || (!ta && a.palette[a.pixels[i]] != b.palette[b.pixels[i]])) return false;
    }
    return true;
}

void write_file(const std::string& path, const std::string& data) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << data;
}

// PNG codec, PNG sprites in packs, and the baker.
void pack_tool_checks(FakePlatform& p, const std::string& demo_dir) {
    std::printf("png and baker\n");
    std::string text, err;
    vpet::Sprite blip;
    p.read_file(demo_dir + "/sprites/blip.spr", text);
    vpet::parse_sprite(text, blip, err);

    vpet::Image img = vpet::image_from_sprite(blip), back;
    vpet::Sprite again;
    check(vpet::decode_png(vpet::encode_png(img), back, err) && vpet::sprite_from_image(back, 16, 16, again, err) &&
              same_pixels(blip, again),
          "png: sprite -> png -> sprite round-trips exactly");

    std::string compressed;
    vpet::Image cimg;
    vpet::Sprite cspr;
    check(p.read_file("tools/snapshot/fixtures/blip_compressed.png", compressed) &&
              vpet::decode_png(compressed, cimg, err) && vpet::sprite_from_image(cimg, 0, 0, cspr, err) &&
              same_pixels(blip, cspr),
          "png: decodes a deflate-compressed PNG saved by another encoder");

    std::string spr_text;
    vpet::Sprite reparsed;
    check(vpet::write_spr(blip, spr_text, err) && vpet::parse_sprite(spr_text, reparsed, err) && same_pixels(blip, reparsed),
          "spr: writing and re-reading a sprite is lossless");

    // A pack whose creature sprites are PNG strips, loaded directly.
    std::string dir = "build/test_packs/png_direct";
    std::string pack_json;
    p.read_file(demo_dir + "/pack.json", pack_json);
    auto replace = [&](const std::string& from, const std::string& to) {
        size_t at = pack_json.find(from);
        if (at != std::string::npos) pack_json.replace(at, from.size(), to);
    };
    replace("sprites/blip.spr", "sprites/blip.png");
    write_file(dir + "/pack.json", pack_json);
    for (const char* f : {"pixegg.spr", "fangbit.spr", "cogbot.spr", "vinelet.spr", "glitch_king.spr", "items.spr",
                          "sprocket.spr", "mudlump.spr", "voltwing.spr",
                          "crop_berry.spr", "crop_herb.spr", "crop_glow.spr"}) {
        p.read_file(demo_dir + "/sprites/" + f, text);
        write_file(dir + "/sprites/" + f, text);
    }
    write_file(dir + "/sprites/blip.png", vpet::encode_png(img));
    vpet::Pack png_pack;
    bool loaded = vpet::load_pack(p, dir, png_pack, err);
    int bi = png_pack.find_species("blip");
    check(loaded && bi >= 0 && same_pixels(png_pack.species[bi].sprite, blip), "pack: loads a PNG sprite strip directly");

    // The baker turns the CC0 sample folder into a loadable pack.
    vpet::BakeReport rep;
    std::filesystem::remove_all("build/test_packs/baked");
    bool baked = vpet::bake_pack("tools/baker/sample", "build/test_packs/baked", rep);
    vpet::Pack baked_pack;
    bool baked_loads = baked && vpet::load_pack(p, "build/test_packs/baked", baked_pack, err);
    if (!baked_loads) std::printf("  bake/load error: %s\n", (rep.errors.empty() ? err : rep.errors[0]).c_str());
    check(baked_loads && baked_pack.species.size() == 4 && rep.evolutions == 3, "baker: sample folder bakes into a pack that loads");
    int sb = baked_loads ? baked_pack.find_species("sample_blob") : -1;
    check(sb >= 0 && same_pixels(baked_pack.species[sb].sprite, blip), "baker: converted sprites keep their pixels");

    // Broken input is reported, not silently accepted.
    write_file("build/test_packs/broken_src/species.csv",
               "id,name,stage,sprite\nbad1,Bad,notastage,sprites/x.png\nbad2,Bad,egg,sprites/missing.png\n");
    write_file("build/test_packs/broken_src/evolutions.csv", "from,to\nbad2,nobody\n");
    vpet::BakeReport bad;
    bool bad_ok = vpet::bake_pack("build/test_packs/broken_src", "build/test_packs/broken_out", bad);
    check(!bad_ok && bad.errors.size() >= 3, "baker: reports bad stages, missing sprites and unknown species");
}

// Branching evolution rules, driven directly through simulate().
void evolution_checks(FakePlatform& p, const std::string& demo_dir) {
    std::printf("branching evolution\n");
    vpet::Pack pack;
    std::string err;
    vpet::load_pack(p, demo_dir, pack, err);
    int blip = pack.find_species("blip"), sprocket = pack.find_species("sprocket"),
        mudlump = pack.find_species("mudlump"), voltwing = pack.find_species("voltwing");
    const uint64_t t0 = p.clock;
    const uint64_t min = 60'000;
    auto fresh = [&](int species) {
        vpet::Pet pet = vpet::new_pet(pack, species, t0);
        pet.strength = uint8_t(vpet::max_hearts(pack, pet));  // full: no calls -> no surprise mistakes
        pet.hunger = uint8_t(pet.strength * 2);
        return pet;
    };

    vpet::Pet good = fresh(blip);
    good.effort = 1;
    vpet::simulate(pack, good, t0 + 14 * min, p.offset);
    check(good.species == blip, "evolution waits for its time in stage");
    vpet::simulate(pack, good, t0 + 16 * min, p.offset);
    check(good.species == sprocket, "no mistakes + effort takes the first branch");
    check(good.effort == 0 && good.stage_mistakes == 0, "per-form counters reset on evolving");

    vpet::Pet sloppy = fresh(blip);
    sloppy.effort = 3;
    sloppy.stage_mistakes = 1;
    vpet::simulate(pack, sloppy, t0 + 16 * min, p.offset);
    check(sloppy.species == mudlump, "a care mistake falls through to the fallback branch");

    vpet::Pet lazy = fresh(blip);
    vpet::simulate(pack, lazy, t0 + 16 * min, p.offset);
    check(lazy.species == mudlump, "no training also falls through to the fallback");

    vpet::Pet child = fresh(sprocket);
    child.level = 5;
    child.run.best = 4;
    vpet::simulate(pack, child, t0 + 25 * 60 * min, p.offset);
    check(child.species == sprocket, "a quest-floor condition holds evolution back");
    child.run.best = 5;
    vpet::simulate(pack, child, t0 + 25 * 60 * min + min, p.offset);
    check(child.species == voltwing && pack.species[child.species].stage == vpet::Stage::Adult,
          "meeting level and floor unlocks the adult form");

    vpet::Stage legacy;
    check(vpet::parse_stage("baby", legacy) && legacy == vpet::Stage::Infant && vpet::kStageCount == 6,
          "6 stages; old 'baby' packs still load as infant");
}

// Creatures as tiered quest enemies, family drop tables, boss floors.
void enemy_checks(FakePlatform& p, const std::string& demo_dir) {
    std::printf("tiered enemies and family drops\n");
    vpet::Pack pack;
    std::string err;
    vpet::load_pack(p, demo_dir, pack, err);
    auto names_on = [&](int floor, bool* any_boss = nullptr) {
        std::set<std::string> names;
        for (uint32_t r = 0; r < 500; ++r) {
            vpet::EnemyPick pick = vpet::pick_enemy(pack, floor, r * 2654435761u);
            names.insert(pack.enemies[pick.index].name);
            if (any_boss && pick.boss) *any_boss = true;
        }
        return names;
    };
    auto find = [&](const std::string& id) {
        for (const vpet::Enemy& e : pack.enemies)
            if (e.id == id) return &e;
        return static_cast<const vpet::Enemy*>(nullptr);
    };

    bool boss3 = false;
    auto f3 = names_on(3, &boss3);
    check(f3.count("SPROCKET") && f3.count("MUDLUMP") && f3.count("FANGBIT") && !f3.count("VOLTWING") && !boss3,
          "floors 1-9: child creatures and early enemies, no adults or bosses");
    check(!find("blip") && !find("pixegg"), "eggs and infants never appear as enemies");
    auto f15 = names_on(15);
    check(f15.count("VOLTWING") && !f15.count("FANGBIT") && !f15.count("SPROCKET"),
          "floors 10-19: adult creatures; enemies past their max floor drop out");
    auto f25 = names_on(25);
    check(f25.size() == 1 && f25.count("VOLTWING"), "past every tier, the deepest tier keeps appearing");
    bool boss10 = false;
    auto f10 = names_on(10, &boss10);
    check(boss10 && f10.size() == 1 && f10.count("GLITCH KING"), "every 10th floor is the pack's boss");

    const vpet::Enemy *fang = find("fangbit"), *mud = find("mudlump"), *king = find("glitch_king"), *volt = find("voltwing");
    const vpet::Drop& beast_first = pack.family_drops[0].second[0];
    check(fang && fang->drops.size() == 4 && fang->drops[0].item == beast_first.item,
          "an enemy without its own drops uses its family table");
    check(mud && mud->drops.size() == 4 && mud->drops[0].item == pack.find_item("meat"),
          "creature enemies use their family table");
    check(king && king->drops[0].item == pack.find_item("core_shard") && king->drops[0].chance == 0.5f,
          "explicit drops override the family table");
    check(volt && vpet::enemy_stats(*volt, 10, false).hp == 60 && vpet::enemy_stats(*volt, 12, false).hp == 78 &&
              vpet::enemy_stats(*volt, 12, true).hp == 117,
          "enemies scale from the start of their tier; bosses get +50% HP");
}

// Cryo storage, lifespan and the Forever Chip, through the real Game object.
void cryo_checks(const FakePlatform& base, const std::string& demo_dir) {
    std::printf("cryo storage, lifespan, forever partner\n");
    FakePlatform q;
    q.clock = base.clock;
    q.offset = base.offset;
    auto g = std::make_unique<vpet::Game>(q);
    g->init(demo_dir);
    auto advance = [&](uint64_t ms) {
        q.clock += ms;
        g->frame();
    };
    vpet::Pack pack;  // own copy: g is replaced when the save is reloaded below
    std::string load_err;
    vpet::load_pack(q, demo_dir, pack, load_err);
    const uint64_t day = 24ull * 3600 * 1000;
    int blip = pack.find_species("blip");
    std::string msg;

    advance(31'000);  // hatch
    vpet::Pet before = g->pet();
    check(before.species == blip, "cryo setup: the egg hatched");
    check(g->cryo_store(0, msg) && g->cryo(0) && g->cryo(0)->species == blip &&
              pack.species[g->pet().species].stage == vpet::Stage::Egg,
          "storing freezes the active pet and a new egg arrives");
    check(!g->cryo_store(1, msg), "an egg can't be frozen");

    advance(3 * day);  // the new egg hatches and grows; the frozen pet must not
    uint64_t frozen_for = q.clock - g->cryo(0)->frozen_ms;
    check(g->cryo_swap(0, msg), "swapping a stored pet back in");
    const vpet::Pet& back = g->pet();
    check(back.species == blip && back.hunger == before.hunger && back.poop == before.poop &&
              back.care_mistakes == before.care_mistakes,
          "a frozen pet doesn't evolve, get hungry, poop or make mistakes");
    check(back.born_ms == before.born_ms + frozen_for && back.stage_ms == before.stage_ms + frozen_for,
          "age and form timers resume where they paused");
    check(g->cryo(0) && g->cryo(0)->frozen_ms == q.clock, "the other pet is now the one frozen");

    vpet::Pet stored = *g->cryo(0);
    g->save();
    g = std::make_unique<vpet::Game>(q);
    g->init(demo_dir);
    check(g->cryo(0) && g->cryo(0)->species == stored.species && g->cryo(0)->frozen_ms == stored.frozen_ms &&
              g->pet().species == blip && !g->cryo(1),
          "cryo slots survive a save and reload");

    // Lifespan: the demo pack's creatures live 30 days.
    vpet::Pet old = vpet::new_pet(pack, blip, q.clock);
    old.born_ms = q.clock - 30 * day + 5'000;
    uint32_t ev = vpet::simulate(pack, old, q.clock + 10'000, q.offset);
    check(ev & vpet::EV_DIED, "a pet reaching its lifespan passes on");
    vpet::Pet immortal = vpet::new_pet(pack, blip, q.clock);
    immortal.born_ms = q.clock - 300 * day;
    immortal.forever = true;
    check(!(vpet::simulate(pack, immortal, q.clock + 10'000, q.offset) & vpet::EV_DIED) &&
              !vpet::lifespan_left_ms(pack, immortal, q.clock),
          "a Forever Partner never passes on");

    int chip = pack.find_item("forever_chip");
    vpet::add_item(g->home(), chip, 1);
    g->pet().asleep = false;
    bool used = vpet::use_item_at_home(pack, g->pet(), g->home(), chip, msg);
    check(used && g->pet().forever && vpet::count(g->home(), chip) == 0, "the Forever Chip makes the pet a Forever Partner");
    vpet::add_item(g->home(), chip, 1);
    check(!vpet::use_item_at_home(pack, g->pet(), g->home(), chip, msg), "a second chip is refused");

    // In the game: the active pet passes on, home is kept, a new egg arrives.
    g->pet().forever = false;
    std::vector<uint16_t> inv = g->home().inventory;
    g->pet().born_ms = q.clock - 30 * day + 1'000;
    advance(3'000);
    check(pack.species[g->pet().species].stage == vpet::Stage::Egg && g->home().inventory == inv && g->cryo(0),
          "after passing on: a new egg, and the bag and cryo slots are kept");
}

// Settings hotkey, switching packs, versions, move swapping, old saves.
void settings_checks(const FakePlatform& base, const std::string& demo_dir) {
    std::printf("settings, packs and moves\n");
    namespace fs = std::filesystem;

    // A packs folder holding the demo and the baked CC0 sample.
    std::string root = "build/test_packs/multi";
    fs::remove_all(root);
    fs::create_directories(root);
    fs::copy(demo_dir, root + "/demo", fs::copy_options::recursive);
    vpet::BakeReport rep;
    vpet::bake_pack("tools/baker/sample", root + "/baker_sample", rep);

    FakePlatform q;
    q.clock = base.clock;
    q.offset = base.offset;
    auto g = std::make_unique<vpet::Game>(q);
    auto run = [&](uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 33) {
            q.clock += 33;
            g->frame();
        }
    };
    check(g->init(root) && g->pack_names().size() == 2, "finds every pack folder in packs/");
    check(g->pack().id == "baker_sample", "with no history, opens the first pack");

    // Hotkey: hold A+C. 2.5 s is not enough; 3 s opens settings.
    q.held = vpet::BTN_A | vpet::BTN_C;
    run(2500);
    bool early = g->settings_open();
    run(600);
    bool opened = g->settings_open();
    q.held = 0;
    run(100);
    check(!early && opened, "holding A+C for 3 seconds opens settings, not sooner");
    q.held = vpet::BTN_C;
    run(66);
    q.held = 0;
    run(66);
    check(!g->settings_open(), "C closes settings");

    // Switch packs; each keeps its own save.
    run(31'000);  // hatch the sample pack's egg
    std::string sample_species = g->pack().species[g->pet().species].id;
    g->request_pack(1);
    run(66);
    check(g->pack().id == "demo" && g->pack_index() == 1, "switching packs opens the other pack");
    g->request_pack(0);
    run(66);
    check(g->pack().id == "baker_sample" && g->pack().species[g->pet().species].id == sample_species,
          "switching back restores that pack's own pet");
    g->request_pack(1);
    run(66);
    g = std::make_unique<vpet::Game>(q);
    g->init(root);
    check(g->pack().id == "demo", "the last pack used opens next time");

    // Versions: the chosen roster is saved.
    g->set_roster(0);
    g->save();
    g = std::make_unique<vpet::Game>(q);
    g->init(root);
    check(g->roster_index() == 0 && g->pack().rosters[0].id == "demo_v1", "the chosen version is remembered");

    // Move swapping on a level-6 pet that knows four moves.
    const vpet::Pack& pack = g->pack();
    vpet::Pet pet = vpet::new_pet(pack, pack.find_species("blip"), q.clock);
    pet.level = 6;
    int tackle = pack.find_move("tackle"), bubble = pack.find_move("bubble"), zap = pack.find_move("zap_code"),
        lucky = pack.find_move("lucky_roll");
    std::string msg;
    check(vpet::equipped_moves(pack, pet) == std::vector<int>{bubble, zap, lucky}, "by default the newest 3 moves are equipped");
    check(!vpet::toggle_move(pack, pet, tackle, msg), "a 4th move can't be equipped");
    vpet::toggle_move(pack, pet, lucky, msg);
    vpet::toggle_move(pack, pet, tackle, msg);
    check(vpet::equipped_moves(pack, pet) == std::vector<int>{bubble, zap, tackle}, "swapping a move out and another in");
    vpet::toggle_move(pack, pet, bubble, msg);
    vpet::toggle_move(pack, pet, zap, msg);
    check(!vpet::toggle_move(pack, pet, tackle, msg) && vpet::equipped_moves(pack, pet).size() == 1,
          "at least one move stays equipped");

    vpet::Save s;
    s.active = pet;
    s.home = vpet::new_home(pack);
    vpet::Save back;
    check(vpet::read_save(pack, vpet::write_save(pack, s), back) && back.active.moves == pet.moves,
          "equipped moves survive a save");

    // A save from before half hearts stored whole hearts.
    std::string legacy = "format 1\nspecies voltwing\nhunger 3\n";  // an adult: 3 hearts
    vpet::Save old;
    check(vpet::read_save(pack, legacy, old) && old.active.hunger == 6, "old whole-heart hunger loads as half hearts");
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // keep output if a check crashes
    if (argc < 3) {
        std::fprintf(stderr, "usage: vpet_snapshot <pack_dir> <out_dir>\n");
        return 2;
    }
    FakePlatform p;
    pack_tool_checks(p, argv[1]);
    evolution_checks(p, argv[1]);
    enemy_checks(p, argv[1]);
    cryo_checks(p, argv[1]);
    settings_checks(p, argv[1]);
    auto game = std::make_unique<vpet::Game>(p);
    if (!game->init(argv[1])) return 1;
    std::string out = argv[2];
    const vpet::Pack& pack = game->pack();
    auto pet = [&]() -> vpet::Pet& { return game->pet(); };

    auto run = [&](uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 33) {
            p.clock += 33;
            game->frame();
        }
    };
    // Jump the clock forward in one go, like reopening the app later.
    auto skip = [&](uint64_t ms) {
        p.clock += ms;
        game->frame();
    };
    auto press = [&](uint8_t b) {
        p.held = b;
        run(66);
        p.held = 0;
        run(66);
    };
    auto select = [&](int item) {
        press(vpet::BTN_C);
        for (int i = 0; i <= item; ++i) press(vpet::BTN_A);
    };
    auto shot = [&](const char* name) {
        write_bmp(out + "/" + name + ".bmp", p.last);
        std::printf("wrote %s\n", name);
    };
    auto local_minute = [&] { return int(((int64_t(p.clock / 1000) + p.offset) / 60) % 1440); };

    std::printf("hatching\n");
    run(500);
    skip(31'000);
    run(200);
    check(pack.species[pet().species].id == "blip", "egg hatched into blip");
    check(pet().hunger == 0 && pet().hunger_call.active(), "hatchling is hungry and calling");
    run(700);
    shot("01_hungry_call");

    std::printf("feeding\n");
    select(1);
    press(vpet::BTN_B);
    shot("02_feed_menu");
    press(vpet::BTN_B);
    run(500);
    shot("02b_eating");
    check(pet().hunger == 1, "kibble adds half a heart");
    check(!pet().hunger_call.active(), "feeding answers the hunger call");
    for (int i = 0; i < 8; ++i) {  // 7 more fill it; the 8th is refused
        press(vpet::BTN_B);
        press(vpet::BTN_B);
    }
    check(pet().hunger == vpet::max_hearts(game->pack(), pet()) * 2, "hunger caps at the form's hearts (1 for an infant)");
    check(pet().weight == 7, "each kibble eaten adds 1g; refused ones don't");

    std::printf("minigame scoring\n");
    {
        using vpet::Minigame;
        // Timing: the cursor sweeps 0->100 over kSweepMs, so we can aim it.
        auto timing_at = [](uint32_t seed, int target_pct) {
            Minigame m;
            m.start(vpet::MiniGame::Timing, 0, seed);
            uint64_t t = uint64_t(target_pct) * vpet::MinigameTuning::battle().sweep_ms / 100;
            m.update(t);
            m.press(vpet::BTN_B, t, 0);
            return m;
        };
        Minigame probe;
        probe.start(vpet::MiniGame::Timing, 0, 12345);
        int zone = probe.zone_start();
        check(timing_at(12345, zone + vpet::MinigameTuning::battle().zone_width / 2).mult() == 1.5f, "timing: center of the gold is perfect");
        check(timing_at(12345, zone + 1).success() && timing_at(12345, zone + 1).mult() < 1.5f,
              "timing: edge of the gold is a hit");
        check(!timing_at(12345, zone > 50 ? 2 : 98).success(), "timing: far from the gold misses");
        bool moved = false, in_bounds = true;
        for (uint32_t s = 1; s < 200; ++s) {
            Minigame m;
            m.start(vpet::MiniGame::Timing, 0, s * 2654435761u);
            moved |= m.zone_start() != zone;
            in_bounds &= m.zone_start() >= 0 && m.zone_start() + vpet::MinigameTuning::battle().zone_width <= 100;
        }
        check(moved && in_bounds, "timing: the gold zone moves around and stays on the bar");

        auto mash = [](int presses) {
            Minigame m;
            m.start(vpet::MiniGame::Mash, 0, 1);
            for (int i = 0; i < presses; ++i) m.press(i % 2 ? vpet::BTN_B : vpet::BTN_A, 100 + i * 100, 0);
            m.update(vpet::MinigameTuning::battle().mash_ms);
            return m;
        };
        check(mash(11).success() && !mash(5).success(), "mash: 11 alternating presses succeed, 5 don't");
        Minigame one;
        one.start(vpet::MiniGame::Mash, 0, 1);
        for (int i = 0; i < 30; ++i) one.press(vpet::BTN_A, 100 + i * 50, 0);
        one.update(vpet::MinigameTuning::battle().mash_ms);
        check(!one.success(), "mash: hammering one button doesn't count");
        bool repeats = false;
        for (uint32_t s = 1; s < 2000; ++s) {
            Minigame c;
            c.start(vpet::MiniGame::Sequence, 0, s * 2654435761u);
            for (int i = 1; i < Minigame::kSeqLen; ++i) repeats |= c.sequence(i) == c.sequence(i - 1);
        }
        check(!repeats, "sequence: never the same button twice in a row");

        // Training uses gentler settings than battles.
        vpet::MinigameTuning easy = vpet::MinigameTuning::training(), hard = vpet::MinigameTuning::battle();
        check(easy.zone_width > hard.zone_width && easy.sweep_ms > hard.sweep_ms && easy.mash_goal < hard.mash_goal &&
                  easy.seq_ms > hard.seq_ms,
              "training is easier: wider gold, slower cursor, fewer presses, more combo time");
        Minigame em;
        em.start(vpet::MiniGame::Mash, 0, 1, easy);
        for (int i = 0; i < easy.mash_goal; ++i) em.press(i % 2 ? vpet::BTN_B : vpet::BTN_A, 100 + i * 100, 0);
        em.update(easy.mash_ms);
        check(em.success(), "training mash: hitting the lower goal succeeds");

        Minigame seq;
        seq.start(vpet::MiniGame::Sequence, 0, 0xBEEF1234u);
        for (int i = 0; i < Minigame::kSeqLen; ++i) seq.press(vpet::Button(1 << seq.sequence(i)), 100, 0);
        check(seq.done() && seq.mult() == 1.5f, "sequence: the full combo is perfect");
        Minigame wrong;
        wrong.start(vpet::MiniGame::Sequence, 0, 0xBEEF1234u);
        wrong.press(vpet::Button(1 << ((wrong.sequence(0) + 1) % 3)), 100, 0);
        check(wrong.done() && !wrong.success(), "sequence: a wrong first button fails");
    }

    std::printf("training\n");
    uint16_t weight_before_training = pet().weight;
    select(2);
    press(vpet::BTN_B);
    run(1000);  // "READY" beat
    shot("03_train_drill");
    // Drill type is random; mashing B beats Speed Bag and ends the others.
    for (int i = 0; i < 24; ++i) press(vpet::BTN_B);
    run(3000);
    check(pet().trainings == 1 && pet().weight == weight_before_training - 1, "a drill counts as training and burns 1g");
    check(pet().strength <= 1, "strength rises by at most one heart");

    std::printf("status\n");
    select(0);
    press(vpet::BTN_B);
    shot("04_status_p1");
    press(vpet::BTN_A);
    shot("05_status_p2");
    press(vpet::BTN_A);
    shot("06_status_p3");
    press(vpet::BTN_C);

    std::printf("neglect for 80 minutes (catch-up)\n");
    uint16_t mistakes_before = pet().care_mistakes;
    skip(80 * 60 * 1000);
    run(1000);
    check(pet().hunger == 0 && pet().strength == 0, "needs drained while away");
    check(pet().care_mistakes == mistakes_before + 2, "two ignored calls = two care mistakes");
    check(pet().poop == 4 && pet().sick, "four poops made it sick");
    shot("07_neglected");

    std::printf("recovery\n");
    select(4);
    press(vpet::BTN_B);
    check(pet().poop == 0, "clean removes all poop");
    run(400);
    shot("07b_cleaning_wave");
    run(800);
    shot("07c_cleaning_cheer");
    run(1000);  // buttons are ignored until the animation ends
    select(1);
    press(vpet::BTN_B);
    press(vpet::BTN_A);
    press(vpet::BTN_B);
    check(!pet().sick, "medicine cures sickness");

    std::printf("bedtime\n");
    int to_bed = (21 * 60 - local_minute() + 1440) % 1440;
    skip(uint64_t(to_bed + 1) * 60 * 1000);
    run(1000);
    check(pet().asleep, "asleep after 21:00");
    check(pet().lights_call.active(), "lights left on starts a call");
    shot("08_asleep_lights_on");
    select(5);
    press(vpet::BTN_B);
    press(vpet::BTN_C);
    run(1000);
    check(!pet().lights_on && !pet().lights_call.active(), "lights off answers the call");
    shot("09_asleep_dark");

    std::printf("save, quit, reopen next morning\n");
    vpet::Pet before = pet();
    game->save();
    game = std::make_unique<vpet::Game>(p);
    game->init(argv[1]);
    check(pet().care_mistakes == before.care_mistakes && pet().weight == before.weight, "save round-trips");
    skip(11ull * 3600 * 1000);  // 08:01
    run(1000);
    check(!pet().asleep && pet().lights_on, "woke up with lights on");
    check(pet().care_mistakes == before.care_mistakes, "no care mistakes while asleep");
    check(pet().hunger_call.active(), "wakes up hungry and calling");
    skip(11ull * 60 * 1000);
    run(1000);
    check(pet().care_mistakes == before.care_mistakes + 2, "ignoring the morning calls costs two mistakes");
    shot("10_next_morning");

    // Menu slots: 0 status 1 feed 2 train 3 quest 4 clean 5 lights 6 garden 7 bag
    const vpet::Pack& pk = game->pack();
    int data_seed = pk.find_item("data_seed"), berry = pk.find_item("berry"), jam = pk.find_item("berry_jam");

    std::printf("garden\n");
    check(vpet::count(game->home(), data_seed) == 3, "a new pet starts with 3 data seeds");
    select(6);
    press(vpet::BTN_B);  // open garden, cursor on plot 0
    press(vpet::BTN_B);  // empty plot -> seed picker
    press(vpet::BTN_B);  // plant the first seed type
    check(game->home().garden[0].crop == 0 && vpet::count(game->home(), data_seed) == 2, "planting uses a seed");
    press(vpet::BTN_A);
    press(vpet::BTN_B);
    press(vpet::BTN_A);  // second seed type: herb
    press(vpet::BTN_B);
    check(game->home().garden[1].crop == 1, "planted an herb in plot 2");
    run(500);
    shot("11_garden_planted");
    skip(21 * 60 * 1000);
    run(1000);
    shot("12_garden_ripe");
    for (int i = 0; i < 7; ++i) press(vpet::BTN_A);  // back to plot 0
    press(vpet::BTN_B);
    check(vpet::count(game->home(), berry) == 3 && game->home().garden[0].crop < 0, "harvest gives 3 berries and clears the plot");
    check(game->home().garden[1].crop == 1, "the herb is still growing");
    press(vpet::BTN_C);

    std::printf("cooking\n");
    select(7);
    press(vpet::BTN_B);  // bag
    press(vpet::BTN_A);
    press(vpet::BTN_A);
    press(vpet::BTN_B);  // cook & craft
    shot("13_recipes");
    press(vpet::BTN_B);  // first recipe: berry jam
    check(vpet::count(game->home(), jam) == 1 && vpet::count(game->home(), berry) == 0, "3 berries cook into berry jam");
    press(vpet::BTN_C);
    press(vpet::BTN_A);  // wrap around to ITEMS
    press(vpet::BTN_B);
    shot("14_items");
    int hunger_before = pet().hunger;
    press(vpet::BTN_B);  // eat the jam
    check(vpet::count(game->home(), jam) == 0 && pet().hunger == std::min(vpet::max_hearts(game->pack(), pet()) * 2, hunger_before + 4),
          "berry jam adds 2 hearts (4 halves)");
    press(vpet::BTN_C);
    press(vpet::BTN_C);

    std::printf("quest\n");
    select(4);
    press(vpet::BTN_B);  // clean
    run(2200);           // wait out the cleaning animation
    select(1);
    press(vpet::BTN_B);
    press(vpet::BTN_A);
    press(vpet::BTN_B);  // medicine
    for (int i = 0; i < 8; ++i) {
        select(1);
        press(vpet::BTN_B);
        press(vpet::BTN_B);  // kibble
    }
    pet().level = 6;  // test shortcut: know all four moves
    check(!pet().sick && pet().hunger == vpet::max_hearts(game->pack(), pet()) * 2, "healthy and fed before questing");
    select(3);
    press(vpet::BTN_B);
    shot("15_quest_lobby");
    press(vpet::BTN_B);  // enter floor 1
    check(pet().hunger == vpet::max_hearts(game->pack(), pet()) * 2 - 1, "entering a floor costs half a hunger heart");
    run(3000);           // intro messages auto-advance
    shot("16_battle_choose");
    press(vpet::BTN_B);  // BUBBLE: mash minigame
    run(300);
    shot("17_mash");
    for (int i = 0; i < 16; ++i) press(i % 2 ? vpet::BTN_A : vpet::BTN_B);
    run(8000);
    press(vpet::BTN_A);  // ZAP CODE: sequence
    press(vpet::BTN_B);
    run(200);
    shot("18_sequence");
    run(9000);
    press(vpet::BTN_A);
    press(vpet::BTN_A);  // LUCKY ROLL: dice
    press(vpet::BTN_B);
    press(vpet::BTN_B);
    run(300);
    shot("19_dice");

    // Keep pressing B (attack / next floor) until the run ends or hunger runs out.
    int start_xp = pet().xp, start_level = pet().level;
    for (int i = 0; i < 400; ++i) {
        press(i % 4 == 3 ? vpet::BTN_A : vpet::BTN_B);  // an occasional A moves the cursor like a player would
        run(500);
    }
    shot("20_after_quest");
    std::printf("  run floor %d, best %d, level %d, xp %u, hp %d, hunger %d, sick %d\n", pet().run.floor,
                pet().run.best, pet().level, unsigned(pet().xp), pet().run.hp, pet().hunger, int(pet().sick));
    check(pet().run.best >= 1 || pet().sick, "the run either cleared floors or ended in a faint");
    check(pet().run.best == 0 || pet().level > start_level || int(pet().xp) > start_xp, "clearing floors gives XP");
    int loot = 0;
    for (size_t i = 0; i < pk.items.size(); ++i)
        if (pk.items[i].material) loot += vpet::count(game->home(), int(i));
    std::printf("  materials owned: %d\n", loot);

    std::printf("save round-trip of RPG state\n");
    vpet::Pet mid = pet();
    vpet::Home mid_home = game->home();
    game->save();
    game = std::make_unique<vpet::Game>(p);
    game->init(argv[1]);
    check(pet().level == mid.level && pet().xp == mid.xp && pet().run.best == mid.run.best &&
              game->home().inventory == mid_home.inventory && game->home().garden[1].crop == mid_home.garden[1].crop &&
              game->home().garden[1].planted_ms == mid_home.garden[1].planted_ms,
          "level, run, inventory and garden survive a reload");
    run(500);
    shot("21_home_day");
    select(8);
    run(400);
    shot("23_menu_cryo_selected");
    press(vpet::BTN_B);
    shot("24_cryo_screen");
    press(vpet::BTN_B);
    shot("25_cryo_confirm");
    press(vpet::BTN_C);
    press(vpet::BTN_C);
    p.held = vpet::BTN_A | vpet::BTN_C;
    run(1500);
    shot("26_settings_hold");
    run(1700);
    p.held = 0;
    run(100);
    shot("27_settings");
    press(vpet::BTN_A);
    press(vpet::BTN_A);
    press(vpet::BTN_B);
    shot("28_moves");
    press(vpet::BTN_C);
    press(vpet::BTN_C);

    // The timing bar on its own, mid-sweep, to check the gold zone art.
    {
        vpet::Framebuffer fb;
        const vpet::Theme& th = game->pack().theme;
        fb.clear(th.ui);
        vpet::Minigame m;
        m.start(vpet::MiniGame::Timing, 0, 777);
        m.update(vpet::MinigameTuning::battle().sweep_ms * 30 / 100);
        m.draw(fb, th, 20, 0);
        m.start(vpet::MiniGame::Timing, 0, 4242, vpet::MinigameTuning::training());
        m.update(vpet::MinigameTuning::battle().sweep_ms * 70 / 100);
        m.draw(fb, th, 60, 0);
        write_bmp(out + "/22_timing_bars.bmp", fb);
    }

    std::printf("%d check(s) failed\n", failures);
    return failures;
}
