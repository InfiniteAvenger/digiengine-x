// Bag: items, materials, and recipes for cooking and crafting. Built on a
// generic scrolling list driven by A (next), B (choose), C (back).
#include <functional>
#include "scenes.h"
#include "vpet/rpg.h"

namespace vpet {
namespace {

struct Row {
    std::string label, right;
    Color color;
    int item = -1;  // shows this item's icon
};

class ListScene : public Scene {
public:
    using RowsFn = std::function<std::vector<Row>()>;
    using TextFn = std::function<std::string(int)>;
    using PickFn = std::function<void(int)>;

    ListScene(Game& game, std::string title, RowsFn rows, TextFn detail, PickFn pick, std::string empty)
        : Scene(game), title_(std::move(title)), rows_(std::move(rows)), detail_(std::move(detail)),
          pick_(std::move(pick)), empty_(std::move(empty)) {}

    void on_press(Button b) override {
        int n = int(rows_().size());
        if (b == BTN_C) game_.pop();
        else if (n == 0) return;
        else if (b == BTN_A) cursor_ = (cursor_ + 1) % n;
        else pick_(cursor_);
    }

    void draw(Framebuffer& fb) override {
        const Theme& t = game_.pack().theme;
        std::vector<Row> rows = rows_();
        int n = int(rows.size());
        if (cursor_ >= n) cursor_ = n ? n - 1 : 0;

        draw_title(fb, t, title_);
        if (n == 0) {
            fb.draw_text_centered(44, empty_, mix(t.ui_text, t.ui, 90));
            draw_footer(fb, t, "C: BACK");
            return;
        }

        constexpr int kVisible = 7;
        int first = cursor_ < kVisible ? 0 : cursor_ - kVisible + 1;
        for (int i = first; i < n && i < first + kVisible; ++i) {
            int y = 15 + (i - first) * 10;
            bool sel = i == cursor_;
            if (sel) fb.fill_rect(2, y - 2, kScreenW - 4, 10, mix(t.ui, t.accent, 70));
            int tx = 6;
            if (rows[i].item >= 0) {
                draw_item_icon(fb, game_.pack(), rows[i].item, 4, y - 2);
                tx = 15;
            }
            fb.draw_text(tx, y, rows[i].label, sel ? t.accent : rows[i].color);
            fb.draw_text(kScreenW - 6 - Framebuffer::text_width(rows[i].right), y, rows[i].right, t.ui_text);
        }
        if (n > kVisible) {  // scroll indicator
            int h = 70 * kVisible / n;
            fb.fill_rect(kScreenW - 2, 13 + (70 - h) * first / (n - kVisible), 1, h, t.accent);
        }

        fb.fill_rect(0, 85, kScreenW, 1, mix(t.ui, t.ui_text, 40));
        std::vector<std::string> lines = wrap_text(detail_(cursor_), 28);
        for (size_t l = 0; l < lines.size() && l < 2; ++l)
            fb.draw_text(4, 89 + int(l) * 8, lines[l], mix(t.ui_text, t.ui, 50));
        draw_footer(fb, t, "A: NEXT B: CHOOSE C: BACK");
    }

private:
    std::string title_;
    RowsFn rows_;
    TextFn detail_;
    PickFn pick_;
    std::string empty_;
    int cursor_ = 0;
};

// Indices of owned items of one kind, in pack order.
std::vector<int> owned(const Game& game, bool materials) {
    std::vector<int> ids;
    const Pack& pack = game.pack();
    for (size_t i = 0; i < pack.items.size(); ++i)
        if (pack.items[i].material == materials && count(game.home(), int(i)) > 0)
            ids.push_back(int(i));
    return ids;
}

std::unique_ptr<Scene> make_item_list(Game& g, bool materials) {
    auto rows = [&g, materials] {
        std::vector<Row> out;
        for (int i : owned(g, materials)) {
            const ItemDef& it = g.pack().items[i];
            out.push_back({it.name, "X" + std::to_string(count(g.home(), i)), rarity_color(it.rarity), i});
        }
        return out;
    };
    auto detail = [&g, materials](int row) {
        std::vector<int> ids = owned(g, materials);
        if (row >= int(ids.size())) return std::string();
        const ItemDef& it = g.pack().items[ids[row]];
        std::string where = it.material ? "" : it.effect.in_quest() ? " (QUEST)" : " (HOME)";
        return std::string(rarity_name(it.rarity)) + where + ": " + it.info;
    };
    auto pick = [&g, materials](int row) {
        std::vector<int> ids = owned(g, materials);
        if (row >= int(ids.size())) return;
        std::string msg;
        int item = ids[row];
        const Effect& e = g.pack().items[item].effect;
        if (use_item_at_home(g.pack(), g.pet(), g.home(), item, msg) && (e.hunger || e.strength)) {
            g.pop();  // back to the home screen to watch it eat
            g.pop();
            g.play_eat(item);
        } else {
            g.toast(msg);
        }
    };
    return std::make_unique<ListScene>(g, materials ? "MATERIALS" : "ITEMS", rows, detail, pick,
                                       materials ? "NO MATERIALS YET" : "NO ITEMS YET");
}

std::unique_ptr<Scene> make_recipe_list(Game& g) {
    auto rows = [&g] {
        std::vector<Row> out;
        const Pack& pack = g.pack();
        for (size_t r = 0; r < pack.recipes.size(); ++r) {
            const ItemDef& it = pack.items[pack.recipes[r].makes];
            bool ok = can_craft(pack, g.home(), int(r));
            out.push_back({it.name, ok ? "OK" : "-", ok ? rarity_color(it.rarity) : rgb(0x6a, 0x6e, 0x80),
                           pack.recipes[r].makes});
        }
        return out;
    };
    auto detail = [&g](int row) {
        const Pack& pack = g.pack();
        std::string s = "NEEDS";
        for (auto [item, n] : pack.recipes[row].needs)
            s += " " + pack.items[item].name + " " + std::to_string(count(g.home(), item)) + "/" + std::to_string(n);
        return s;
    };
    auto pick = [&g](int row) {
        const Pack& pack = g.pack();
        if (craft(pack, g.home(), row)) g.toast("MADE " + pack.items[pack.recipes[row].makes].name + "!");
        else g.toast("MISSING ITEMS");
    };
    return std::make_unique<ListScene>(g, "COOK & CRAFT", rows, detail, pick, "NO RECIPES");
}

}  // namespace

std::unique_ptr<Scene> make_bag_scene(Game& game) {
    Game* g = &game;
    auto rows = [g] {
        auto kinds = [g](bool m) { return std::to_string(owned(*g, m).size()); };
        Color c = g->pack().theme.ui_text;
        return std::vector<Row>{{"ITEMS", kinds(false), c}, {"MATERIALS", kinds(true), c}, {"COOK & CRAFT", "", c}};
    };
    auto detail = [](int row) {
        static const char* const kText[] = {"FOOD, CHIPS AND QUEST GEAR", "DROPS AND CROPS FOR RECIPES",
                                            "TURN MATERIALS INTO ITEMS"};
        return std::string(kText[row]);
    };
    auto pick = [g](int row) {
        if (row == 2) g->push(make_recipe_list(*g));
        else g->push(make_item_list(*g, row == 1));
    };
    return std::make_unique<ListScene>(game, "BAG", rows, detail, pick, "");
}

}  // namespace vpet
