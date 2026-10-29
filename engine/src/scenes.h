#pragma once
#include <cctype>
#include <memory>
#include <string>
#include <vector>
#include "vpet/game.h"

namespace vpet {

std::unique_ptr<Scene> make_home_scene(Game& game);
std::unique_ptr<Scene> make_status_scene(Game& game);
std::unique_ptr<Scene> make_feed_scene(Game& game);
std::unique_ptr<Scene> make_train_scene(Game& game);
std::unique_ptr<Scene> make_garden_scene(Game& game);
std::unique_ptr<Scene> make_bag_scene(Game& game);
std::unique_ptr<Scene> make_quest_scene(Game& game);
std::unique_ptr<Scene> make_cryo_scene(Game& game);
std::unique_ptr<Scene> make_settings_scene(Game& game);
std::unique_ptr<Scene> make_moves_scene(Game& game);

// Shared drawing helpers for full-screen menus.
std::string format_duration(uint64_t ms);
void draw_title(Framebuffer& fb, const Theme& t, const std::string& title);
// Four hearts showing `halves` half hearts (0-8).
void draw_hearts(Framebuffer& fb, int x, int y, int halves);
void draw_footer(Framebuffer& fb, const Theme& t, const std::string& text);
// Horizontal bar, value/max filled.
void draw_bar(Framebuffer& fb, int x, int y, int w, int h, int value, int max, Color fill, Color back);
Color rarity_color(Rarity r);
// Draws an item's 8x8 icon if the pack has one.
void draw_item_icon(Framebuffer& fb, const Pack& pack, int item, int x, int y);
// Splits text into lines of at most `width` characters at spaces.
std::vector<std::string> wrap_text(const std::string& text, size_t width);

// Sky, sun/moon, clouds, hills and ground between rows top..bottom.
void draw_scenery(Framebuffer& fb, const Theme& t, int top, int horizon, int bottom, uint64_t now_ms,
                  int local_minute, bool dark);

}  // namespace vpet
