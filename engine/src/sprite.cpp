#include "vpet/sprite.h"
#include <sstream>

namespace vpet {

bool parse_hex_color(const std::string& hex, Color& out) {
    if (hex.size() != 6) return false;
    unsigned v = 0;
    for (char ch : hex) {
        v <<= 4;
        if (ch >= '0' && ch <= '9') v |= unsigned(ch - '0');
        else if (ch >= 'a' && ch <= 'f') v |= unsigned(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') v |= unsigned(ch - 'A' + 10);
        else return false;
    }
    out = rgb(uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v));
    return true;
}

bool parse_sprite(const std::string& text, Sprite& out, std::string& err) {
    out = Sprite{};
    out.palette.push_back(0);  // index 0: transparent
    int16_t index_of[128];
    for (auto& i : index_of) i = -1;
    index_of[int('.')] = 0;

    enum class Mode { Header, Palette, Rows } mode = Mode::Header;
    int rows_left = 0;
    int line_no = 0;
    std::istringstream in(text);
    std::string line;

    auto fail = [&](const std::string& msg) {
        err = "line " + std::to_string(line_no) + ": " + msg;
        return false;
    };

    while (std::getline(in, line)) {
        ++line_no;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();

        if (mode == Mode::Rows) {
            if (line.empty()) continue;
            if (int(line.size()) != out.w) return fail("row must be " + std::to_string(out.w) + " characters");
            for (char ch : line) {
                int idx = (unsigned char)ch < 128 ? index_of[int(ch)] : -1;
                if (idx < 0) return fail(std::string("color '") + ch + "' is not in the palette");
                out.pixels.push_back(uint8_t(idx));
            }
            if (--rows_left == 0) mode = Mode::Header;
            continue;
        }

        if (line.empty() || line[0] == '#') continue;
        std::istringstream words(line);
        std::string word;
        words >> word;

        if (word == "size") {
            words >> out.w >> out.h;
            if (out.w <= 0 || out.h <= 0 || out.w > 120 || out.h > 120) return fail("bad size");
            mode = Mode::Header;
        } else if (word == "palette") {
            mode = Mode::Palette;
        } else if (word == "frame") {
            if (out.w == 0) return fail("'size' must come before the first frame");
            ++out.frames;
            rows_left = out.h;
            mode = Mode::Rows;
        } else if (mode == Mode::Palette && word.size() == 1) {
            char key = word[0];
            std::string hex;
            words >> hex;
            if (key == '.' || key == '#' || (unsigned char)key >= 128) return fail("'.' and '#' cannot be palette keys");
            if (out.palette.size() >= 255) return fail("too many palette colors");
            Color c;
            if (!parse_hex_color(hex, c)) return fail("bad color '" + hex + "'");
            index_of[int(key)] = int16_t(out.palette.size());
            out.palette.push_back(c);
        } else {
            return fail("unknown directive '" + word + "'");
        }
    }

    if (mode == Mode::Rows) return fail("file ended in the middle of a frame");
    if (out.frames == 0) return fail("no frames");
    return true;
}

}  // namespace vpet
