#include "vpet/game.h"
#include <algorithm>
#include <cctype>
#include "scenes.h"
#include "vpet/json.h"

namespace vpet {

namespace {
constexpr uint64_t kAutosaveMs = 60'000;
constexpr uint64_t kToastMs = 1500;
constexpr uint64_t kFlashMs = 900;
}

bool Game::init(const std::string& root) {
    now_ = platform_.now_ms();
    rng_ ^= uint32_t(now_) | 1;

    // One pack, or a folder of packs.
    std::string text;
    pack_dirs_.clear();
    pack_names_.clear();
    if (platform_.read_file(root + "/pack.json", text)) {
        pack_dirs_.push_back(root);
    } else {
        for (const std::string& name : platform_.list_dirs(root))
            if (platform_.read_file(root + "/" + name + "/pack.json", text)) pack_dirs_.push_back(root + "/" + name);
    }
    if (pack_dirs_.empty()) {
        error_ = "no packs found in " + root;
        return false;
    }
    for (const std::string& dir : pack_dirs_) {
        Json j;
        std::string err;
        platform_.read_file(dir + "/pack.json", text);
        std::string folder = dir.substr(dir.find_last_of('/') + 1);
        pack_names_.push_back(Json::parse(text, j, err) ? j["name"].str_or(folder) : folder);
    }

    // Reopen the pack used last time.
    int choice = 0;
    std::string settings;
    if (platform_.load_save("settings", settings)) {
        size_t at = settings.find("pack ");
        std::string last = at == std::string::npos ? "" : settings.substr(at + 5, settings.find('\n', at) - at - 5);
        for (size_t i = 0; i < pack_dirs_.size(); ++i)
            if (pack_dirs_[i] == last) choice = int(i);
    }
    if (open_pack(choice)) return true;
    if (choice != 0 && open_pack(0)) return true;  // the last one broke; fall back
    return false;
}

bool Game::open_pack(int index) {
    Pack pack;
    std::string err;
    if (!load_pack(platform_, pack_dirs_[index], pack, err)) {
        if (pack_.species.empty()) error_ = err;  // nothing loaded yet: show it
        else toast("PACK ERROR");
        return false;
    }
    if (!pack_.species.empty()) save();  // the pack being left
    pack_ = std::move(pack);
    pack_index_ = index;
    error_.clear();

    std::string saved;
    save_ = Save{};
    if (!(platform_.load_save(pack_.id, saved) && read_save(pack_, saved, save_))) {
        save_ = Save{};
        save_.home = new_home(pack_);
        new_egg();
    }
    last_save_ = now_;
    platform_.write_save("settings", "pack " + pack_dirs_[index] + "\n");

    stack_.clear();
    pending_pops_ = 0;
    push(make_home_scene(*this));
    return true;
}

int Game::roster_index() const {
    for (size_t i = 0; i < pack_.rosters.size(); ++i)
        if (pack_.rosters[i].id == save_.roster) return int(i);
    return 0;
}

void Game::set_roster(int index) { save_.roster = pack_.rosters[size_t(index) % pack_.rosters.size()].id; }

void Game::handle_settings_hotkey(uint8_t held) {
    bool combo = (held & BTN_A) && (held & BTN_C);
    if (!combo) {
        combo_since_ = 0;
        combo_used_ = false;
        return;
    }
    if (!combo_since_) combo_since_ = now_;
    bool in_settings = !stack_.empty() && stack_.back()->is_settings();
    if (!combo_used_ && !in_settings && now_ - combo_since_ >= kSettingsHoldMs) {
        combo_used_ = true;
        push(make_settings_scene(*this));
    }
}

void Game::frame() {
    now_ = platform_.now_ms();
    uint8_t held = platform_.buttons();
    uint8_t pressed = held & ~prev_buttons_;
    prev_buttons_ = held;

    if (!error_.empty()) {
        draw_error();
        platform_.present(fb_);
        return;
    }

    // The first frame after loading a save replays all the time it was closed.
    utc_offset_s_ = platform_.utc_offset_s();
    uint32_t ev = simulate(pack_, save_.active, now_, utc_offset_s_);
    if (ev & EV_DIED) {
        // Old age. Home, garden and cryo are kept; a new egg arrives.
        std::string name = species().name;
        for (char& c : name) c = char(toupper(static_cast<unsigned char>(c)));
        new_egg();
        toast(name + " PASSED ON");
    } else if (ev & EV_MISTAKE) toast("CARE MISTAKE!");
    else if (ev & EV_HATCHED) toast("HATCHED!");
    else if (ev & EV_EVOLVED) toast("EVOLVED!");
    else if (ev & EV_SICK) toast("GOT SICK!");
    if (ev & (EV_HATCHED | EV_EVOLVED)) flash_start_ = now_;

    if (!stack_.empty()) {
        Scene& top = *stack_.back();
        for (Button b : {BTN_A, BTN_B, BTN_C})
            if (pressed & b) top.on_press(b);
    }
    bool popped = pending_pops_ > 0;
    for (; pending_pops_ > 0 && stack_.size() > 1; --pending_pops_) stack_.pop_back();
    pending_pops_ = 0;
    if (pending_pack_ >= 0) {  // applied here, never from inside a scene
        int index = pending_pack_;
        pending_pack_ = -1;
        if (index != pack_index_ && open_pack(index)) toast(pack_names_[index]);
    }
    handle_settings_hotkey(held);

    if (!stack_.empty()) {
        stack_.back()->update(now_);
        stack_.back()->draw(fb_);
    }

    const Theme& t = pack_.theme;

    // Holding A+C: a bar fills toward opening settings.
    if (combo_since_ && !combo_used_ && now_ - combo_since_ > 300) {
        int w = int((now_ - combo_since_) * 80 / kSettingsHoldMs);
        fb_.panel(18, 14, 84, 17, t.ui, t.accent);
        fb_.draw_text_centered(17, "SETTINGS", t.ui_text);
        fb_.fill_rect(20, 25, std::min(w, 80), 3, t.accent);
    }

    // Hatching and evolving: three white flashes.
    if (flash_start_ && now_ - flash_start_ < kFlashMs && (now_ - flash_start_) / 150 % 2 == 0)
        fb_.dither(rgb(0xff, 0xff, 0xff), 11);

    if (now_ < toast_until_) {
        // Pops in by dropping a few pixels, on a rounded panel with a shadow.
        uint64_t age = now_ + kToastMs - toast_until_;
        int y = 52 - (age < 120 ? int(120 - age) / 20 : 0);
        int w = Framebuffer::text_width(toast_) + 12;
        int x = (kScreenW - w) / 2;
        fb_.fill_rect(x + 2, y + 13, w - 2, 1, mix(t.ui, rgb(0, 0, 0), 120));
        fb_.fill_rect(x + w, y + 2, 1, 11, mix(t.ui, rgb(0, 0, 0), 120));
        fb_.panel(x, y, w, 13, t.ui, t.accent);
        fb_.draw_text_centered(y + 4, toast_, t.ui_text);
    }

    platform_.present(fb_);

    if (pressed || popped || (ev & ~EV_POOPED) || now_ - last_save_ >= kAutosaveMs) save();
}

void Game::save() {
    if (!error_.empty()) return;
    platform_.write_save(pack_.id, write_save(pack_, save_));
    last_save_ = now_;
}

void Game::new_egg() {
    const Roster& r = pack_.rosters[size_t(roster_index())];
    save_.active = new_pet(pack_, r.eggs[rand() % r.eggs.size()], now_);
}

bool Game::cryo_store(int slot, std::string& msg) {
    if (save_.cryo[slot]) return msg = "SLOT IS FULL", false;
    if (species().stage == Stage::Egg) return msg = "EGGS CAN'T BE FROZEN", false;
    if (save_.active.sick) return msg = "CURE IT FIRST", false;
    freeze(save_.active, now_);
    save_.cryo[slot] = save_.active;
    new_egg();
    msg = "FROZEN. NEW EGG!";
    save();
    return true;
}

bool Game::cryo_swap(int slot, std::string& msg) {
    if (!save_.cryo[slot]) return msg = "SLOT IS EMPTY", false;
    if (species().stage == Stage::Egg) return msg = "EGGS CAN'T BE FROZEN", false;
    if (save_.active.sick) return msg = "CURE IT FIRST", false;
    Pet stored = *save_.cryo[slot];
    freeze(save_.active, now_);
    save_.cryo[slot] = save_.active;
    thaw(stored, now_);
    save_.active = stored;
    msg = "SWAPPED!";
    save();
    return true;
}

void Game::push(std::unique_ptr<Scene> scene) { stack_.push_back(std::move(scene)); }

void Game::pop() { ++pending_pops_; }

void Game::toast(const std::string& msg) {
    toast_ = msg;
    toast_until_ = now_ + kToastMs;
}

uint32_t Game::rand() {
    // xorshift32
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void Game::draw_error() {
    fb_.clear(rgb(0x5d, 0x27, 0x5d));
    fb_.draw_text(4, 4, "PACK ERROR", rgb(0xff, 0xcd, 0x75));
    // Wrap at 28 characters per line (120px / 4px advance, minus margins).
    int y = 14;
    for (size_t i = 0; i < error_.size() && y < kScreenH - 6; i += 28, y += 7)
        fb_.draw_text(4, y, error_.substr(i, 28), rgb(0xf4, 0xf4, 0xf4));
}

}  // namespace vpet
