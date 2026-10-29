#include "vpet/game.h"
#include <cctype>
#include "scenes.h"

namespace vpet {

namespace {
constexpr uint64_t kAutosaveMs = 60'000;
constexpr uint64_t kToastMs = 1500;
constexpr uint64_t kFlashMs = 900;
}

bool Game::init(const std::string& pack_dir) {
    now_ = platform_.now_ms();
    rng_ ^= uint32_t(now_) | 1;
    if (!load_pack(platform_, pack_dir, pack_, error_)) return false;

    std::string saved;
    if (!(platform_.load_save(pack_.id, saved) && read_save(pack_, saved, save_))) {
        save_ = Save{};
        save_.home = new_home(pack_);
        new_egg();
    }
    last_save_ = now_;

    stack_.clear();
    push(make_home_scene(*this));
    return true;
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

    if (!stack_.empty()) {
        stack_.back()->update(now_);
        stack_.back()->draw(fb_);
    }

    const Theme& t = pack_.theme;

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
    const Roster& r = pack_.rosters[size_t(roster_) < pack_.rosters.size() ? roster_ : 0];
    save_.active = new_pet(pack_, r.eggs[rand() % r.eggs.size()], now_);
}

bool Game::cryo_store(int slot, std::string& msg) {
    if (save_.cryo[slot]) return msg = "SLOT IS FULL", false;
    if (species().stage == Stage::Egg) return msg = "EGGS CAN'T BE FROZEN", false;
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
