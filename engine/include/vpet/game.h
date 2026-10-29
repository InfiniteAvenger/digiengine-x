#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "vpet/framebuffer.h"
#include "vpet/pack.h"
#include "vpet/pet.h"
#include "vpet/platform.h"

namespace vpet {

class Game;

class Scene {
public:
    explicit Scene(Game& game) : game_(game) {}
    virtual ~Scene() = default;
    virtual void on_press(Button) {}
    virtual void update(uint64_t /*now_ms*/) {}
    virtual void draw(Framebuffer& fb) = 0;

protected:
    Game& game_;
};

class Game {
public:
    explicit Game(Platform& platform) : platform_(platform) {}

    // Loads the pack and starts the home screen. On failure the error is
    // shown on screen by frame().
    bool init(const std::string& pack_dir);
    // Runs one frame: input, simulation, draw, present. Call 30-60x a second.
    void frame();
    // Writes the pet to its save slot. Also called automatically.
    void save();

    void push(std::unique_ptr<Scene> scene);
    void pop();  // deferred until the current frame's input is handled
    void toast(const std::string& msg);
    // Asks the home screen to show the pet eating `item` (-1 = kibble).
    void play_eat(int item) {
        eat_item_ = item;
        eat_start_ = now_;
    }
    int eat_item() const { return eat_item_; }
    uint64_t eat_start() const { return eat_start_; }

    const Pack& pack() const { return pack_; }
    Pet& pet() { return save_.active; }
    const Pet& pet() const { return save_.active; }
    Home& home() { return save_.home; }
    const Home& home() const { return save_.home; }
    const Species& species() const { return pack_.species[save_.active.species]; }

    // Cryo storage. Storing puts the active pet in an empty slot and starts a
    // new egg; swapping trades the active pet with a stored one. Both return
    // false (with a reason in `msg`) when not allowed.
    const std::optional<Pet>& cryo(int slot) const { return save_.cryo[slot]; }
    bool cryo_store(int slot, std::string& msg);
    bool cryo_swap(int slot, std::string& msg);
    // Replaces the active pet with a fresh egg from the current roster.
    void new_egg();
    uint64_t now() const { return now_; }
    // Minutes after local midnight (0-1439).
    int local_minute() const {
        int64_t s = int64_t(now_ / 1000) + utc_offset_s_;
        return int(((s / 60) % 1440 + 1440) % 1440);
    }
    uint32_t rand();

private:
    void draw_error();

    Platform& platform_;
    Framebuffer fb_;
    Pack pack_;
    Save save_;
    int roster_ = 0;
    std::vector<std::unique_ptr<Scene>> stack_;
    int pending_pops_ = 0;
    std::string error_;
    std::string toast_;
    uint64_t toast_until_ = 0;
    uint64_t now_ = 0;
    uint64_t last_save_ = 0;
    uint64_t flash_start_ = 0, eat_start_ = 0;
    int eat_item_ = -1;
    int32_t utc_offset_s_ = 0;
    uint8_t prev_buttons_ = 0;
    uint32_t rng_ = 0x9e3779b9u;
};

}  // namespace vpet
