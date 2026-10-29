// 2x4 garden. Crops grow in real time from their planting timestamp, so they
// keep growing while the game is closed.
#include "scenes.h"
#include "vpet/rpg.h"

namespace vpet {
namespace {

constexpr int kCols = 4;
constexpr int kTile = 26;
constexpr int kGridX = (kScreenW - kCols * (kTile + 2)) / 2 + 1;
constexpr int kGridY = 16;
constexpr int kPanelY = 78;

class GardenScene : public Scene {
public:
    using Scene::Scene;

    void on_press(Button b) override {
        if (picking_) return on_press_picking(b);
        if (b == BTN_A) cursor_ = (cursor_ + 1) % kGardenPlots;
        else if (b == BTN_C) game_.pop();
        else act();
    }

    void draw(Framebuffer& fb) override {
        const Theme& t = game_.pack().theme;
        const Pack& pack = game_.pack();
        const Pet& pet = game_.pet();
        uint64_t now = game_.now();

        draw_title(fb, t, "GARDEN");
        Color grass = mix(t.bg_bottom, t.ui, 60);
        fb.fill_rect(0, 11, kScreenW, kPanelY - 11, grass);
        Color soil = rgb(0x6b, 0x44, 0x2a), furrow = rgb(0x52, 0x33, 0x20);

        for (int i = 0; i < kGardenPlots; ++i) {
            int x = kGridX + (i % kCols) * (kTile + 2), y = kGridY + (i / kCols) * (kTile + 4);
            fb.fill_rect(x, y, kTile, kTile, soil);
            for (int fy = y + 4; fy < y + kTile; fy += 5) fb.fill_rect(x + 2, fy, kTile - 4, 1, furrow);
            const Plot& plot = pet.garden[i];
            int stage = crop_stage(pack, plot, now);
            if (stage >= 0) fb.draw_sprite(pack.crops[plot.crop].sprite, stage, x + 1, y + 1, 2);
            if (stage == kCropRipe && (now / 400) % 2) fb.draw_text(x + kTile - 4, y + 1, "!", t.accent);
            if (i == cursor_) {
                fb.rect(x - 1, y - 1, kTile + 2, kTile + 2, t.accent);
                fb.rect(x - 2, y - 2, kTile + 4, kTile + 4, t.ui);
            }
        }

        fb.fill_rect(0, kPanelY, kScreenW, kScreenH - kPanelY, t.ui);
        if (picking_) {
            const Crop& c = pack.crops[seeds_[pick_]];
            fb.draw_text(6, kPanelY + 4, "PLANT WHICH SEED?", t.accent);
            draw_item_icon(fb, pack, c.seed, 5, kPanelY + 11);
            fb.draw_text(15, kPanelY + 13, pack.items[c.seed].name + " X" + std::to_string(count(pet, c.seed)),
                         t.ui_text);
            fb.draw_text(6, kPanelY + 21, "GROWS " + pack.items[c.yields].name + " IN " +
                                              format_duration(uint64_t(c.grow_s) * 1000),
                         mix(t.ui_text, t.ui, 90));
            draw_footer(fb, t, "A: NEXT B: PLANT C: CANCEL");
            return;
        }

        const Plot& plot = pet.garden[cursor_];
        int stage = crop_stage(pack, plot, now);
        if (stage < 0) {
            fb.draw_text(6, kPanelY + 4, "EMPTY PLOT", t.accent);
            fb.draw_text(6, kPanelY + 13, available_seeds().empty() ? "NO SEEDS - FIND SOME" : "B: PLANT A SEED",
                         t.ui_text);
        } else {
            const Crop& c = pack.crops[plot.crop];
            fb.draw_text(6, kPanelY + 4, c.name, t.accent);
            if (stage == kCropRipe) {
                fb.draw_text(6, kPanelY + 13, "RIPE! B: HARVEST", t.ui_text);
            } else {
                fb.draw_text(6, kPanelY + 13, format_duration(crop_ms_left(pack, plot, now)) + " LEFT", t.ui_text);
                uint64_t total = uint64_t(c.grow_s) * 1000, left = crop_ms_left(pack, plot, now);
                draw_bar(fb, 6, kPanelY + 21, kScreenW - 12, 3, int((total - left) / 1000), int(total / 1000),
                         t.accent, mix(t.ui, t.ui_text, 40));
            }
        }
        draw_footer(fb, t, "A: MOVE B: ACT C: BACK");
    }

private:
    std::vector<int> available_seeds() const {
        std::vector<int> crops;
        const Pack& pack = game_.pack();
        for (size_t c = 0; c < pack.crops.size(); ++c)
            if (count(game_.pet(), pack.crops[c].seed) > 0) crops.push_back(int(c));
        return crops;
    }

    void act() {
        const Pack& pack = game_.pack();
        Pet& pet = game_.pet();
        const Plot& plot = pet.garden[cursor_];
        if (plot.crop < 0) {
            seeds_ = available_seeds();
            if (seeds_.empty()) return game_.toast("NO SEEDS");
            picking_ = true;
            pick_ = 0;
            return;
        }
        std::string name = pack.items[pack.crops[plot.crop].yields].name;
        int got = harvest(pack, pet, cursor_, game_.now(), game_.rand());
        game_.toast(got ? "+" + std::to_string(got) + " " + name : "NOT RIPE YET");
    }

    void on_press_picking(Button b) {
        if (b == BTN_A) {
            pick_ = (pick_ + 1) % int(seeds_.size());
        } else if (b == BTN_B) {
            if (plant(game_.pack(), game_.pet(), cursor_, seeds_[pick_], game_.now())) game_.toast("PLANTED!");
            picking_ = false;
        } else {
            picking_ = false;
        }
    }

    int cursor_ = 0;
    bool picking_ = false;
    std::vector<int> seeds_;  // crop indices the player has seeds for
    int pick_ = 0;
};

}  // namespace

std::unique_ptr<Scene> make_garden_scene(Game& game) { return std::make_unique<GardenScene>(game); }

}  // namespace vpet
