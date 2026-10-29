// Cryo storage: two backup slots. Frozen pets don't age, get hungry or
// evolve; their timers resume exactly where they stopped when thawed.
#include "scenes.h"

namespace vpet {
namespace {

constexpr int kPanelH = 26;

class CryoScene : public Scene {
public:
    using Scene::Scene;

    void on_press(Button b) override {
        if (confirming_) {
            if (b == BTN_B) {
                std::string msg;
                if (game_.cryo(slot_)) game_.cryo_swap(slot_, msg);
                else game_.cryo_store(slot_, msg);
                game_.toast(msg);
            }
            confirming_ = false;
            return;
        }
        if (b == BTN_A) slot_ = (slot_ + 1) % kCryoSlots;
        else if (b == BTN_B) confirming_ = true;
        else game_.pop();
    }

    void draw(Framebuffer& fb) override {
        const Pack& pack = game_.pack();
        const Theme& t = pack.theme;
        uint64_t now = game_.now();
        draw_title(fb, t, "CRYO STORAGE");

        Color ice = rgb(0x6c, 0xc8, 0xff);
        auto panel = [&](int y, const char* label, const Pet* pet, bool selected, bool frozen) {
            Color border = selected ? t.accent : mix(t.ui, t.ui_text, 60);
            fb.panel(3, y, kScreenW - 6, kPanelH, frozen ? mix(t.ui, ice, 40) : mix(t.ui, t.ui_text, 18), border);
            fb.draw_text(7, y + 3, label, selected ? t.accent : mix(t.ui_text, t.ui, 80));
            if (!pet) {
                fb.draw_text(40, y + 12, "EMPTY", mix(t.ui_text, t.ui, 100));
                return;
            }
            const Species& s = pack.species[pet->species];
            fb.draw_sprite(s.sprite, 0, kScreenW - 26, y + 5, 1);
            fb.draw_text(7, y + 11, s.name, t.ui_text);
            std::string info = std::string(stage_name(s.stage)) + " LV" + std::to_string(pet->level);
            if (frozen) info += "  " + format_duration(now > pet->frozen_ms ? now - pet->frozen_ms : 0);
            for (char& c : info) c = char(toupper(static_cast<unsigned char>(c)));
            fb.draw_text(7, y + 18, info, frozen ? ice : mix(t.ui_text, t.ui, 70));
        };

        panel(14, "ACTIVE", &game_.pet(), false, false);
        for (int i = 0; i < kCryoSlots; ++i) {
            const std::optional<Pet>& p = game_.cryo(i);
            panel(14 + (kPanelH + 3) * (i + 1), i == 0 ? "SLOT 1" : "SLOT 2", p ? &*p : nullptr, i == slot_, bool(p));
        }

        if (confirming_) {
            const char* q = game_.cryo(slot_) ? "SWAP WITH ACTIVE?" : "FREEZE + NEW EGG?";
            fb.panel(8, 50, kScreenW - 16, 22, t.ui, t.accent);
            fb.draw_text_centered(54, q, t.ui_text);
            fb.draw_text_centered(63, "B: YES  C: NO", t.accent);
        }
        draw_footer(fb, t, "A: SLOT B: CHOOSE C: BACK");
    }

private:
    int slot_ = 0;
    bool confirming_ = false;
};

}  // namespace

std::unique_ptr<Scene> make_cryo_scene(Game& game) { return std::make_unique<CryoScene>(game); }

}  // namespace vpet
