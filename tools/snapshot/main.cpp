// Headless runner: plays a scripted session against the demo pack with a fake
// clock, checks the pet's state along the way, and writes chosen frames as
// 24-bit BMPs (scaled 4x). Exit code is the number of failed checks.
//   vpet_snapshot <pack_dir> <out_dir>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
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

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: vpet_snapshot <pack_dir> <out_dir>\n");
        return 2;
    }
    FakePlatform p;
    pack_tool_checks(p, argv[1]);
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
    check(pet().hunger == 1, "kibble adds one hunger heart");
    check(!pet().hunger_call.active(), "feeding answers the hunger call");
    for (int i = 0; i < 4; ++i) {
        press(vpet::BTN_B);
        press(vpet::BTN_B);
    }
    check(pet().hunger == 4, "hunger caps at 4 hearts");
    check(pet().weight == 9, "four kibble add 4g");

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
            for (int i = 0; i < presses; ++i) m.press(vpet::BTN_A, 100 + i * 100, 0);
            m.update(vpet::MinigameTuning::battle().mash_ms);
            return m;
        };
        check(mash(11).success() && !mash(5).success(), "mash: 11 presses succeed, 5 don't");

        // Training uses gentler settings than battles.
        vpet::MinigameTuning easy = vpet::MinigameTuning::training(), hard = vpet::MinigameTuning::battle();
        check(easy.zone_width > hard.zone_width && easy.sweep_ms > hard.sweep_ms && easy.mash_goal < hard.mash_goal &&
                  easy.seq_ms > hard.seq_ms,
              "training is easier: wider gold, slower cursor, fewer presses, more combo time");
        Minigame em;
        em.start(vpet::MiniGame::Mash, 0, 1, easy);
        for (int i = 0; i < easy.mash_goal; ++i) em.press(vpet::BTN_A, 100 + i * 100, 0);
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
    select(2);
    press(vpet::BTN_B);
    run(1000);  // "READY" beat
    shot("03_train_drill");
    // Drill type is random; mashing B beats Speed Bag and ends the others.
    for (int i = 0; i < 24; ++i) press(vpet::BTN_B);
    run(3000);
    check(pet().trainings == 1 && pet().weight == 8, "a drill counts as training and burns 1g");
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

    std::printf("neglect for 40 minutes (catch-up)\n");
    uint16_t mistakes_before = pet().care_mistakes;
    skip(40 * 60 * 1000);
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
    check(vpet::count(pet(), data_seed) == 3, "a new pet starts with 3 data seeds");
    select(6);
    press(vpet::BTN_B);  // open garden, cursor on plot 0
    press(vpet::BTN_B);  // empty plot -> seed picker
    press(vpet::BTN_B);  // plant the first seed type
    check(pet().garden[0].crop == 0 && vpet::count(pet(), data_seed) == 2, "planting uses a seed");
    press(vpet::BTN_A);
    press(vpet::BTN_B);
    press(vpet::BTN_A);  // second seed type: herb
    press(vpet::BTN_B);
    check(pet().garden[1].crop == 1, "planted an herb in plot 2");
    run(500);
    shot("11_garden_planted");
    skip(21 * 60 * 1000);
    run(1000);
    shot("12_garden_ripe");
    for (int i = 0; i < 7; ++i) press(vpet::BTN_A);  // back to plot 0
    press(vpet::BTN_B);
    check(vpet::count(pet(), berry) == 3 && pet().garden[0].crop < 0, "harvest gives 3 berries and clears the plot");
    check(pet().garden[1].crop == 1, "the herb is still growing");
    press(vpet::BTN_C);

    std::printf("cooking\n");
    select(7);
    press(vpet::BTN_B);  // bag
    press(vpet::BTN_A);
    press(vpet::BTN_A);
    press(vpet::BTN_B);  // cook & craft
    shot("13_recipes");
    press(vpet::BTN_B);  // first recipe: berry jam
    check(vpet::count(pet(), jam) == 1 && vpet::count(pet(), berry) == 0, "3 berries cook into berry jam");
    press(vpet::BTN_C);
    press(vpet::BTN_A);  // wrap around to ITEMS
    press(vpet::BTN_B);
    shot("14_items");
    int hunger_before = pet().hunger;
    press(vpet::BTN_B);  // eat the jam
    check(vpet::count(pet(), jam) == 0 && pet().hunger == std::min(4, hunger_before + 2), "berry jam adds 2 hunger");
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
    for (int i = 0; i < 4; ++i) {
        select(1);
        press(vpet::BTN_B);
        press(vpet::BTN_B);  // kibble
    }
    pet().level = 6;  // test shortcut: know all four moves
    check(!pet().sick && pet().hunger == 4, "healthy and fed before questing");
    select(3);
    press(vpet::BTN_B);
    shot("15_quest_lobby");
    press(vpet::BTN_B);  // enter floor 1
    check(pet().hunger == 3, "entering a floor costs a hunger heart");
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
        if (pk.items[i].material) loot += vpet::count(pet(), int(i));
    std::printf("  materials owned: %d\n", loot);

    std::printf("save round-trip of RPG state\n");
    vpet::Pet mid = pet();
    game->save();
    game = std::make_unique<vpet::Game>(p);
    game->init(argv[1]);
    check(pet().level == mid.level && pet().xp == mid.xp && pet().run.best == mid.run.best &&
              pet().inventory == mid.inventory && pet().garden[1].crop == mid.garden[1].crop &&
              pet().garden[1].planted_ms == mid.garden[1].planted_ms,
          "level, run, inventory and garden survive a reload");
    run(500);
    shot("21_home_day");

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
