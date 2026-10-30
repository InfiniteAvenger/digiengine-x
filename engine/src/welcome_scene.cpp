// First launch: a title screen over a starry sky where the player picks the
// pack to play. A shows the next pack, B starts, C adds one from a zip (where
// the platform can). Shown once; packs can be
// switched later in settings.
#include "font.h"
#include "scenes.h"

namespace vpet {
namespace {

constexpr Color kSkyTop = rgb(0x12, 0x16, 0x30);
constexpr Color kSkyBottom = rgb(0x2c, 0x22, 0x52);
constexpr Color kText = rgb(0xf4, 0xf4, 0xf4);
constexpr Color kAccent = rgb(0xff, 0x9a, 0x4d);
constexpr Color kCard = rgb(0x1c, 0x1e, 0x36);

// The 3x5 font drawn `scale` times bigger, with a drop shadow.
void big_text(Framebuffer& fb, int x, int y, const std::string& text, int scale, Color c) {
    for (int pass = 0; pass < 2; ++pass) {
        Color col = pass == 0 ? rgb(0x08, 0x08, 0x14) : c;
        int off = pass == 0 ? 1 : 0;
        for (size_t i = 0; i < text.size(); ++i)
            if (const char* g = glyph_for(text[i]))
                for (int gy = 0; gy < 5; ++gy)
                    for (int gx = 0; gx < 3; ++gx)
                        if (g[gy * 3 + gx] == '#')
                            fb.fill_rect(x + int(i) * 4 * scale + gx * scale + off, y + gy * scale + off, scale, scale,
                                         col);
    }
}

class WelcomeScene : public Scene {
public:
    explicit WelcomeScene(Game& game) : Scene(game), index_(game.pack_index()) {}
    bool is_settings() const override { return true; }  // a setup screen: A+C doesn't stack settings on it

    void on_press(Button b) override {
        int n = int(game_.pack_names().size());
        if (b == BTN_C && game_.platform().can_import_packs()) {
            game_.push(make_import_scene(game_));  // the new pack opens when it's added
        } else if (b == BTN_A && n > 1) {
            index_ = (index_ + 1) % n;
        } else if (b == BTN_B) {
            game_.sfx(Game::Sfx::Clear);
            if (index_ == game_.pack_index()) {
                game_.new_egg();  // the egg's timers start now, not at launch
                game_.save();
                game_.pop();
            } else {
                game_.request_pack(index_);  // opens it fresh at home
            }
        }
    }

    void draw(Framebuffer& fb) override {
        uint64_t now = game_.now();
        for (int y = 0; y < kScreenH; ++y) fb.fill_rect(0, y, kScreenW, 1, mix(kSkyTop, kSkyBottom, y * 256 / kScreenH));
        // Twinkling stars at fixed spots.
        for (int i = 0; i < 26; ++i) {
            uint32_t h = uint32_t(i) * 2654435761u;
            int x = int(h % kScreenW), y = int((h >> 8) % 104);
            bool bright = (now / 400 + uint64_t(i) * 7) % 9 == 0;
            fb.set(x, y, bright ? kText : mix(kSkyTop, kText, 110));
        }

        big_text(fb, 14, 8, "DIGIENGINE", 2, kText);
        big_text(fb, 96, 5, "X", 3, kAccent);
        fb.draw_text_centered(26, "HOLD A+C FOR SETTINGS", mix(kText, kSkyTop, 110));

        const Pack& pack = preview();
        fb.panel(6, 36, kScreenW - 12, 58, kCard, mix(kCard, kAccent, 90));
        // The pack's first egg, bobbing over a shadow.
        if (!pack.rosters.empty() && !pack.rosters[0].eggs.empty()) {
            const Sprite& s = pack.species[size_t(pack.rosters[0].eggs[0])].sprite;
            if (s.valid()) {
                int bob = (now / 450) % 2 ? -1 : 0;
                int scale = s.w > 20 ? 1 : 2;  // big sprites at their own size
                fb.fill_rect(16, 82, 28, 3, mix(kCard, rgb(0, 0, 0), 120));
                fb.draw_sprite(s, int(now / 600 % uint64_t(s.frames)), 30 - s.w * scale / 2, 82 - s.h * scale + bob,
                               scale);
            }
        }
        std::string name = upper(game_.pack_names()[size_t(index_)]);
        std::vector<std::string> lines = wrap_text(name, 14);
        int y = 44;
        for (size_t l = 0; l < lines.size() && l < 2; ++l, y += 8) fb.draw_text(52, y, lines[l], kAccent);
        y += 3;
        if (!pack.author.empty()) {
            std::vector<std::string> by = wrap_text("BY " + upper(pack.author), 14);
            fb.draw_text(52, y, by[0], mix(kText, kCard, 70));
            y += 8;
        }
        fb.draw_text(52, y, std::to_string(pack.species.size()) + " CREATURES", mix(kText, kCard, 70));

        int n = int(game_.pack_names().size());
        if (n > 1) {  // one dot per pack
            int x0 = kScreenW / 2 - (n * 6 - 2) / 2;
            for (int i = 0; i < n; ++i)
                fb.fill_rect(x0 + i * 6, 99, 4, 4, i == index_ ? kAccent : mix(kSkyBottom, kText, 70));
        }
        Theme t;
        t.ui = kCard;
        t.ui_text = kText;
        t.accent = kAccent;
        bool add = game_.platform().can_import_packs();
        draw_footer(fb, t, n > 1 ? (add ? "A: NEXT B: START C: ADD" : "A: NEXT PACK B: START")
                                 : (add ? "B: START C: ADD PACK" : "B: START"));
    }

private:
    static std::string upper(std::string s) {
        for (char& c : s) c = char(toupper(static_cast<unsigned char>(c)));
        return s;
    }

    // The highlighted pack: the open one, or a copy loaded just for the preview.
    const Pack& preview() {
        if (index_ == game_.pack_index()) return game_.pack();
        if (preview_index_ != index_) {
            preview_ = Pack{};
            std::string err;
            load_pack(game_.platform(), game_.pack_dirs()[size_t(index_)], preview_, err);
            preview_index_ = index_;
        }
        return preview_;
    }

    int index_;
    Pack preview_;
    int preview_index_ = -1;
};

}  // namespace

std::unique_ptr<Scene> make_welcome_scene(Game& game) { return std::make_unique<WelcomeScene>(game); }

}  // namespace vpet
