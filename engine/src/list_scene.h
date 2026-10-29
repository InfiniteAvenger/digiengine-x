#pragma once
// A scrolling list screen driven by A (next), B (choose), C (back). Rows are
// rebuilt every frame, so counts and states stay current.
#include <functional>
#include "scenes.h"

namespace vpet {

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

    ListScene(Game& game, std::string title, RowsFn rows, TextFn detail, PickFn pick, std::string empty,
              bool settings = false)
        : Scene(game), title_(std::move(title)), rows_(std::move(rows)), detail_(std::move(detail)),
          pick_(std::move(pick)), empty_(std::move(empty)), settings_(settings) {}

    bool is_settings() const override { return settings_; }

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
    bool settings_ = false;
    int cursor_ = 0;
};

}  // namespace vpet
