#include "icons.h"
#include "scenes.h"

namespace vpet {
namespace {

struct MenuItem {
    const char* name;
};

// Order matches the frames of icons().menu.
enum MenuId { M_STATUS, M_FEED, M_TRAIN, M_QUEST, M_CLEAN, M_LIGHTS, M_GARDEN, M_BAG, M_CRYO, kMenuCount };
constexpr int kSlotW = 13;

constexpr MenuItem kMenu[kMenuCount] = {
    {"STATUS"}, {"FEED"}, {"TRAIN"}, {"QUEST"}, {"CLEAN"}, {"LIGHTS"}, {"GARDEN"}, {"BAG"}, {"CRYO STORAGE"},
};

// Screen layout (120x120)
constexpr int kTopBarH = 12;
constexpr int kBottomBarY = 106;
constexpr int kGroundY = 82;
constexpr int kPetScale = 2;
constexpr int kPetSize = 16 * kPetScale;
constexpr int kPoopAreaX = 84;  // poop piles up to the right of this

class HomeScene : public Scene {
public:
    explicit HomeScene(Game& game) : Scene(game) {}

    void on_press(Button b) override {
        if (cleaning()) return;  // let the wave finish
        if (b == BTN_A) selected_ = (selected_ + 1) % kMenuCount;
        else if (b == BTN_C) selected_ = -1;
        else if (b == BTN_B && selected_ >= 0) activate(MenuId(selected_));
    }

    void update(uint64_t now) override {
        if (eating()) {  // stand still facing the food, just left of the pet
            dir_ = -1;
            if (x_ < kEatMinX) x_ = kEatMinX;
            return;
        }
        if (now < next_step_) return;
        const Pet& pet = game_.pet();
        bool still = game_.species().stage == Stage::Egg || pet.asleep;
        next_step_ = now + (still ? 900 : 600);
        frame_ ^= 1;
        if (still) return;
        if (game_.rand() % 4 == 0) dir_ = -dir_;
        x_ += dir_ * 3;
        const int min_x = 4;
        const int max_x = (pet.poop ? kPoopAreaX - 2 : kScreenW - 4) - kPetSize;
        if (x_ < min_x) x_ = min_x, dir_ = 1;
        if (x_ > max_x) x_ = max_x, dir_ = -1;
    }

    void draw(Framebuffer& fb) override {
        const Theme& t = game_.pack().theme;
        const Species& s = game_.species();
        const Pet& pet = game_.pet();
        const Icons& ic = icons();
        bool egg = s.stage == Stage::Egg;

        uint64_t now = game_.now();
        draw_scenery(fb, t, kTopBarH, kGroundY, kBottomBarY, now, game_.local_minute(), !pet.lights_on);

        // While cleaning, the old poop stays until the wave reaches it.
        uint64_t ct = cleaning() ? now - clean_start_ : 0;
        int wave_x = cleaning() && ct < kWaveMs ? -12 + int(ct * (kScreenW + 24) / kWaveMs) : -100;
        int poop = cleaning() ? (ct < kWaveMs ? clean_poop_ : 0) : pet.poop;
        for (int i = 0; i < poop; ++i) {
            int px = kPoopAreaX + 18 - (i % 2) * 16;
            int py = kGroundY + 4 - 16 * (1 + i / 2);
            if (wave_x < px) fb.draw_sprite(ic.poop, 0, px, py, 2);
        }

        int pet_x = egg ? (kScreenW - kPetSize) / 2 : x_;
        int pet_y = kGroundY - s.sprite.h * kPetScale + 4;
        int pet_frame = pet.asleep ? 0 : frame_;
        bool cheering = cleaning() && ct >= kWaveMs;
        if (cheering) {  // happy hops after the wave
            bool up = (ct / 150) % 2;
            pet_y -= up ? 4 : 0;
            pet_frame = up;
        }
        if (eating()) {
            uint64_t et = now - game_.eat_start();
            pet_frame = int((et / 160) % 2);  // chewing
            int bites = int(et * 4 / kEatMs);   // 0 whole .. 3 gone
            const Sprite& food = game_.eat_item() < 0 ? ic.kibble : game_.pack().item_icons;
            int frame = game_.eat_item() < 0 ? 0 : game_.pack().items[game_.eat_item()].icon;
            if (bites < 3 && food.valid() && frame >= 0)
                draw_bitten(fb, food, frame, pet_x - 18, kGroundY - 12, 8 - bites * 3);
        }
        // Sprites face left; flip when walking right.
        fb.draw_sprite(s.sprite, pet_frame, pet_x, pet_y, kPetScale, dir_ > 0 && !egg);
        if (cheering) {
            static const int kSparkle[][2] = {{-6, 2}, {34, 0}, {-2, 20}, {36, 18}, {14, -8}};
            for (int i = 0; i < 5; ++i) {
                if (!((ct / 120 + uint64_t(i)) % 3)) continue;
                int sx = pet_x + kSparkle[i][0], sy = pet_y + kSparkle[i][1];
                Color edge = mix(t.accent, rgb(0xef, 0x7d, 0x57), 110);
                fb.fill_rect(sx - 2, sy, 5, 1, edge);  // 5px plus with a bright center
                fb.fill_rect(sx, sy - 2, 1, 5, edge);
                fb.set(sx, sy, rgb(0xff, 0xff, 0xff));
            }
        }
        if (wave_x > -100) draw_wave(fb, wave_x, now);
        if (pet.asleep) {
            int zy = pet_y + 6 - frame_ * 3;
            fb.draw_text(pet_x + kPetSize - 2, zy, "Z", t.ui_text);
            fb.draw_text(pet_x + kPetSize + 3, zy - 6, "Z", t.ui_text);
        }

        if (pet.sick) fb.draw_sprite(ic.sick, 0, kScreenW - 18, kTopBarH + 3);
        if (pet.needs_attention() && frame_) fb.draw_sprite(ic.attention, 0, kScreenW - 9, kTopBarH + 3);

        fb.fill_rect(0, 0, kScreenW, kTopBarH, t.ui);
        for (int i = 0; i < kMenuCount; ++i) {
            int x = i * kSlotW + 2;  // 9 slots of 13px fill the 120px bar
            bool sel = i == selected_;
            if (sel) fb.fill_rect(x, 0, 13, kTopBarH, t.accent);
            int bob = sel && (now / 300) % 2 ? 1 : 0;  // the chosen icon bobs
            fb.draw_mask(ic.menu, i, x + 2, 2 - bob, sel ? t.ui : mix(t.ui_text, t.ui, 70));
        }
        fb.fill_rect(0, kTopBarH - 1, kScreenW, 1, mix(t.ui, t.accent, 60));

        fb.fill_rect(0, kBottomBarY, kScreenW, kScreenH - kBottomBarY, t.ui);
        fb.fill_rect(0, kBottomBarY, kScreenW, 1, mix(t.ui, t.accent, 60));
        if (selected_ >= 0) {
            fb.draw_text_centered(kBottomBarY + 5, kMenu[selected_].name, t.accent);
        } else {
            // Name on the left, level and hunger on the right.
            fb.draw_text(4, kBottomBarY + 5, s.name, t.ui_text);
            if (!egg) {
                std::string lv = "LV" + std::to_string(pet.level);
                fb.draw_text(kScreenW - 40 - Framebuffer::text_width(lv), kBottomBarY + 5, lv, t.accent);
                draw_hearts(fb, kScreenW - 37, kBottomBarY + 4, pet.hunger);
            }
        }
    }

private:
    // Shared refusals for actions that need an awake, hatched pet.
    bool can_act(bool allow_sick = true) {
        const Pet& pet = game_.pet();
        if (game_.species().stage == Stage::Egg) return game_.toast("STILL AN EGG"), false;
        if (pet.asleep) return game_.toast("ZZZ..."), false;
        if (!allow_sick && pet.sick) return game_.toast("TOO SICK"), false;
        return true;
    }

    void activate(MenuId id) {
        Pet& pet = game_.pet();
        switch (id) {
            case M_STATUS: game_.push(make_status_scene(game_)); break;
            case M_FEED:
                if (can_act()) game_.push(make_feed_scene(game_));
                break;
            case M_TRAIN:
                if (can_act(false)) game_.push(make_train_scene(game_));
                break;
            case M_CLEAN:
                if (!pet.poop) {
                    game_.toast("ALREADY CLEAN");
                    break;
                }
                // The rules clear the poop now; the wave just shows it.
                clean_poop_ = pet.poop;
                clean_up(pet);
                clean_start_ = game_.now();
                break;
            case M_LIGHTS:
                toggle_lights(pet);
                game_.toast(pet.lights_on ? "LIGHTS ON" : "LIGHTS OFF");
                break;
            case M_QUEST:
                if (!can_act(false)) break;
                if (pet.hunger == 0) game_.toast("TOO HUNGRY");
                else game_.push(make_quest_scene(game_));
                break;
            case M_GARDEN: game_.push(make_garden_scene(game_)); break;
            case M_BAG: game_.push(make_bag_scene(game_)); break;
            case M_CRYO: game_.push(make_cryo_scene(game_)); break;
            default: break;
        }
    }

    // --- Cleaning: a wave of water sweeps left to right, washing the poop
    // away, then the pet hops happily among sparkles.
    static constexpr uint64_t kWaveMs = 1100, kCheerMs = 900;

    bool cleaning() const { return clean_start_ && game_.now() - clean_start_ < kWaveMs + kCheerMs; }

    static void draw_wave(Framebuffer& fb, int x, uint64_t now) {
        const Color water = rgb(0x41, 0xa6, 0xf6), deep = rgb(0x2a, 0x6c, 0xc4), foam = rgb(0xf4, 0xf4, 0xf4);
        for (int y = kTopBarH; y < kBottomBarY; ++y) {
            // A rippling front edge: offset steps back and forth down the column.
            int wob = int((uint64_t(y) + now / 50) / 3 % 4);
            int fx = x - (wob == 3 ? 1 : wob);
            fb.fill_rect(fx - 14, y, 6, 1, deep);
            fb.fill_rect(fx - 8, y, 6, 1, water);
            fb.fill_rect(fx - 2, y, 3, 1, foam);
        }
        for (int k = 0; k < 6; ++k) {  // spray thrown ahead of the wave
            int dy = kTopBarH + int((uint64_t(k) * 17 + now / 20) % uint64_t(kBottomBarY - kTopBarH));
            fb.set(x + 3 + (k % 3) * 2, dy, foam);
        }
    }

    uint64_t clean_start_ = 0;
    int clean_poop_ = 0;

    // --- Eating: the food sits left of the pet and disappears in 3 bites.
    static constexpr uint64_t kEatMs = 1400;
    static constexpr int kEatMinX = 22;  // keeps the food on screen

    bool eating() const { return game_.eat_start() && game_.now() - game_.eat_start() < kEatMs; }

    // Draws a sprite frame at 2x showing only its leftmost `cols` columns.
    static void draw_bitten(Framebuffer& fb, const Sprite& s, int frame, int x, int y, int cols) {
        const uint8_t* src = &s.pixels[size_t(frame) * s.w * s.h];
        for (int sy = 0; sy < s.h; ++sy)
            for (int sx = 0; sx < cols && sx < s.w; ++sx)
                if (uint8_t idx = src[sy * s.w + sx]) fb.fill_rect(x + sx * 2, y + sy * 2, 2, 2, s.palette[idx]);
    }

    int selected_ = -1;
    int x_ = (kScreenW - kPetSize) / 2;
    int dir_ = -1;
    int frame_ = 0;
    uint64_t next_step_ = 0;
};

}  // namespace

std::unique_ptr<Scene> make_home_scene(Game& game) { return std::make_unique<HomeScene>(game); }

}  // namespace vpet
