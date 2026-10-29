// Minimal PNG codec. Decoding includes a small inflate (RFC 1951) based on
// the canonical-Huffman approach of zlib's "puff" reference decoder.
#include "vpet/png.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

namespace vpet {
namespace {

// --- Inflate -----------------------------------------------------------------

struct BitReader {
    const uint8_t* p;
    size_t n, pos = 0;
    uint32_t buf = 0;
    int count = 0;
    bool err = false;

    int bit() {
        if (count == 0) {
            if (pos >= n) {
                err = true;
                return 0;
            }
            buf = p[pos++];
            count = 8;
        }
        int b = int(buf & 1);
        buf >>= 1;
        --count;
        return b;
    }
    uint32_t bits(int k) {
        uint32_t v = 0;
        for (int i = 0; i < k; ++i) v |= uint32_t(bit()) << i;
        return v;
    }
};

struct Huffman {
    uint16_t counts[16];
    uint16_t symbols[320];
};

void build(Huffman& h, const uint8_t* lengths, int n) {
    std::memset(h.counts, 0, sizeof h.counts);
    for (int s = 0; s < n; ++s) h.counts[lengths[s]]++;
    h.counts[0] = 0;
    uint16_t offs[16] = {};
    for (int len = 1; len < 15; ++len) offs[len + 1] = uint16_t(offs[len] + h.counts[len]);
    for (int s = 0; s < n; ++s)
        if (lengths[s]) h.symbols[offs[lengths[s]]++] = uint16_t(s);
}

int decode_symbol(BitReader& br, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= br.bit();
        int count = h.counts[len];
        if (code - count < first) return h.symbols[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
        if (br.err) return -1;
    }
    return -1;
}

const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflate(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    BitReader br{src, n};
    bool last = false;
    while (!last) {
        last = br.bit();
        uint32_t type = br.bits(2);
        if (type == 0) {  // stored
            br.count = 0;  // skip to the byte boundary
            if (br.pos + 4 > n) return false;
            size_t len = size_t(src[br.pos]) | size_t(src[br.pos + 1]) << 8;
            br.pos += 4;
            if (br.pos + len > n) return false;
            out.insert(out.end(), src + br.pos, src + br.pos + len);
            br.pos += len;
            continue;
        }
        if (type == 3) return false;

        Huffman lit, dist;
        if (type == 1) {  // fixed codes
            uint8_t l[288];
            for (int i = 0; i < 288; ++i) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            build(lit, l, 288);
            uint8_t d[30];
            std::memset(d, 5, sizeof d);
            build(dist, d, 30);
        } else {  // dynamic codes
            int hlit = int(br.bits(5)) + 257, hdist = int(br.bits(5)) + 1, hclen = int(br.bits(4)) + 4;
            static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t cl[19] = {};
            for (int i = 0; i < hclen; ++i) cl[kOrder[i]] = uint8_t(br.bits(3));
            Huffman clh;
            build(clh, cl, 19);
            uint8_t lens[320] = {};
            int i = 0;
            while (i < hlit + hdist) {
                int sym = decode_symbol(br, clh);
                if (sym < 0) return false;
                if (sym < 16) {
                    lens[i++] = uint8_t(sym);
                    continue;
                }
                uint8_t val = 0;
                int rep;
                if (sym == 16) {
                    if (i == 0) return false;
                    val = lens[i - 1];
                    rep = 3 + int(br.bits(2));
                } else if (sym == 17) {
                    rep = 3 + int(br.bits(3));
                } else {
                    rep = 11 + int(br.bits(7));
                }
                if (i + rep > hlit + hdist) return false;
                while (rep--) lens[i++] = val;
            }
            build(lit, lens, hlit);
            build(dist, lens + hlit, hdist);
        }

        for (;;) {
            int sym = decode_symbol(br, lit);
            if (sym < 0) return false;
            if (sym < 256) {
                out.push_back(uint8_t(sym));
            } else if (sym == 256) {
                break;
            } else {
                sym -= 257;
                if (sym >= 29) return false;
                size_t len = kLenBase[sym] + br.bits(kLenExtra[sym]);
                int ds = decode_symbol(br, dist);
                if (ds < 0 || ds >= 30) return false;
                size_t d = kDistBase[ds] + br.bits(kDistExtra[ds]);
                if (d > out.size()) return false;
                size_t from = out.size() - d;
                for (size_t k = 0; k < len; ++k) out.push_back(out[from + k]);
            }
        }
        if (br.err) return false;
    }
    return true;
}

// --- Chunk helpers -------------------------------------------------------------

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

int paeth(int a, int b, int c) {
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

}  // namespace

bool decode_png(const std::string& data, Image& out, std::string& err) {
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());
    size_t n = data.size();
    if (n < 8 || std::memcmp(d, kSignature, 8) != 0) return err = "not a PNG file", false;

    int w = 0, h = 0, depth = 0, color = -1;
    std::vector<uint32_t> palette;  // RGBA
    std::vector<uint8_t> idat;
    int trns_gray = -1, trns_r = -1, trns_g = -1, trns_b = -1;

    for (size_t pos = 8; pos + 12 <= n;) {
        uint32_t len = be32(d + pos);
        const uint8_t* type = d + pos + 4;
        const uint8_t* body = d + pos + 8;
        if (pos + 12 + len > n) return err = "truncated PNG chunk", false;
        if (!std::memcmp(type, "IHDR", 4) && len >= 13) {
            w = int(be32(body));
            h = int(be32(body + 4));
            depth = body[8];
            color = body[9];
            if (body[12] != 0) return err = "interlaced PNGs are not supported", false;
        } else if (!std::memcmp(type, "PLTE", 4)) {
            for (uint32_t i = 0; i + 2 < len; i += 3)
                palette.push_back(uint32_t(body[i]) << 24 | uint32_t(body[i + 1]) << 16 | uint32_t(body[i + 2]) << 8 | 0xFF);
        } else if (!std::memcmp(type, "tRNS", 4)) {
            if (color == 3)
                for (uint32_t i = 0; i < len && i < palette.size(); ++i) palette[i] = (palette[i] & 0xFFFFFF00u) | body[i];
            else if (color == 0 && len >= 2)
                trns_gray = body[1];
            else if (color == 2 && len >= 6)
                trns_r = body[1], trns_g = body[3], trns_b = body[5];
        } else if (!std::memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), body, body + len);
        } else if (!std::memcmp(type, "IEND", 4)) {
            break;
        }
        pos += 12 + len;
    }

    int channels = color == 0 ? 1 : color == 2 ? 3 : color == 3 ? 1 : color == 4 ? 2 : color == 6 ? 4 : 0;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || !channels) return err = "unsupported PNG color type", false;
    bool depth_ok = depth == 8 || (color == 3 && (depth == 1 || depth == 2 || depth == 4));
    if (!depth_ok) return err = "PNG bit depth " + std::to_string(depth) + " is not supported (use 8-bit)", false;
    if (color == 3 && palette.empty()) return err = "palette PNG has no palette", false;
    if (idat.size() < 2) return err = "PNG has no image data", false;

    std::vector<uint8_t> raw;
    if (!inflate(idat.data() + 2, idat.size() - 2, raw)) return err = "corrupt PNG image data", false;

    size_t stride = (size_t(w) * channels * depth + 7) / 8;
    size_t bpp = std::max<size_t>(1, size_t(channels) * depth / 8);
    if (raw.size() < (stride + 1) * size_t(h)) return err = "PNG image data is too short", false;

    std::vector<uint8_t> prev(stride, 0), row(stride);
    out.w = w;
    out.h = h;
    out.rgba.assign(size_t(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* line = &raw[size_t(y) * (stride + 1)];
        int filter = line[0];
        for (size_t x = 0; x < stride; ++x) {
            int a = x >= bpp ? row[x - bpp] : 0, b = prev[x], c = x >= bpp ? prev[x - bpp] : 0;
            int v = line[1 + x];
            switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += paeth(a, b, c); break;
                default: return err = "bad PNG row filter", false;
            }
            row[x] = uint8_t(v);
        }
        for (int x = 0; x < w; ++x) {
            uint32_t px;
            if (color == 3) {
                int per = 8 / depth, shift = 8 - depth * (x % per + 1);
                size_t idx = (row[x / per] >> shift) & ((1 << depth) - 1);
                px = idx < palette.size() ? palette[idx] : 0;
            } else {
                const uint8_t* p = &row[size_t(x) * channels];
                uint8_t r = p[0], g = channels >= 3 ? p[1] : p[0], bl = channels >= 3 ? p[2] : p[0];
                uint8_t al = channels == 4 ? p[3] : channels == 2 ? p[1] : 0xFF;
                if (color == 0 && p[0] == trns_gray) al = 0;
                if (color == 2 && r == trns_r && g == trns_g && bl == trns_b) al = 0;
                px = uint32_t(r) << 24 | uint32_t(g) << 16 | uint32_t(bl) << 8 | al;
            }
            out.rgba[size_t(y) * w + x] = px;
        }
        prev.swap(row);
    }
    return true;
}

std::string encode_png(const Image& img) {
    std::string out(reinterpret_cast<const char*>(kSignature), 8);
    auto put32 = [](std::string& s, uint32_t v) {
        for (int i = 3; i >= 0; --i) s.push_back(char(v >> (8 * i)));
    };
    auto chunk = [&](const char* type, const std::string& body) {
        put32(out, uint32_t(body.size()));
        std::string tb = std::string(type, 4) + body;
        out += tb;
        put32(out, crc32(reinterpret_cast<const uint8_t*>(tb.data()), tb.size()));
    };

    std::string ihdr;
    put32(ihdr, uint32_t(img.w));
    put32(ihdr, uint32_t(img.h));
    ihdr += std::string("\x08\x06\x00\x00\x00", 5);  // 8-bit RGBA, no interlace
    chunk("IHDR", ihdr);

    std::string raw;
    for (int y = 0; y < img.h; ++y) {
        raw.push_back(0);  // filter: none
        for (int x = 0; x < img.w; ++x) put32(raw, img.rgba[size_t(y) * img.w + x]);
    }
    // zlib stream of stored deflate blocks
    std::string z = "\x78\x01";
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        size_t len = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + len >= raw.size();
        z.push_back(char(last ? 1 : 0));
        z.push_back(char(len & 0xFF));
        z.push_back(char(len >> 8));
        z.push_back(char(~len & 0xFF));
        z.push_back(char((~len >> 8) & 0xFF));
        z.append(raw, pos, len);
        pos += len;
        if (last) break;
    }
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, b << 16 | a);
    chunk("IDAT", z);
    chunk("IEND", "");
    return out;
}

bool sprite_from_image(const Image& img, int frame_w, int frame_h, Sprite& out, std::string& err) {
    if (frame_h <= 0) frame_h = img.h;
    if (frame_w <= 0) frame_w = frame_h;
    if (frame_w > 120 || frame_h > 120) return err = "frames larger than 120x120 are not supported", false;
    if (img.w % frame_w || img.h % frame_h)
        return err = "image size " + std::to_string(img.w) + "x" + std::to_string(img.h) +
                     " is not a whole number of " + std::to_string(frame_w) + "x" + std::to_string(frame_h) + " frames",
               false;
    out = Sprite{};
    out.w = frame_w;
    out.h = frame_h;
    out.palette.push_back(0);
    std::map<Color, uint8_t> index;
    int cols = img.w / frame_w, rows = img.h / frame_h;
    out.frames = cols * rows;
    for (int f = 0; f < out.frames; ++f) {
        int ox = (f % cols) * frame_w, oy = (f / cols) * frame_h;
        for (int y = 0; y < frame_h; ++y)
            for (int x = 0; x < frame_w; ++x) {
                uint32_t px = img.rgba[size_t(oy + y) * img.w + ox + x];
                if ((px & 0xFF) < 128) {
                    out.pixels.push_back(0);
                    continue;
                }
                Color c = rgb(uint8_t(px >> 24), uint8_t(px >> 16), uint8_t(px >> 8));
                auto it = index.find(c);
                if (it == index.end()) {
                    if (out.palette.size() >= 256) return err = "more than 255 colors", false;
                    it = index.emplace(c, uint8_t(out.palette.size())).first;
                    out.palette.push_back(c);
                }
                out.pixels.push_back(it->second);
            }
    }
    return true;
}

Image image_from_sprite(const Sprite& s) {
    Image img;
    img.w = s.w * s.frames;
    img.h = s.h;
    img.rgba.assign(size_t(img.w) * img.h, 0);
    for (int f = 0; f < s.frames; ++f)
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x) {
                uint8_t idx = s.pixels[(size_t(f) * s.h + y) * s.w + x];
                if (!idx) continue;
                Color c = s.palette[idx];
                uint32_t r = (c >> 11) * 255 / 31, g = ((c >> 5) & 0x3F) * 255 / 63, b = (c & 0x1F) * 255 / 31;
                img.rgba[size_t(y) * img.w + f * s.w + x] = r << 24 | g << 16 | b << 8 | 0xFF;
            }
    return img;
}

bool write_spr(const Sprite& s, std::string& out, std::string& err) {
    // Palette keys: printable ASCII except '.', '#' and space.
    std::string keys;
    for (char c = '!'; c <= '~'; ++c)
        if (c != '.' && c != '#') keys.push_back(c);
    if (s.palette.size() - 1 > keys.size())
        return err = std::to_string(s.palette.size() - 1) + " colors is more than .spr allows (" +
                     std::to_string(keys.size()) + "); keep it as a PNG",
               false;
    out = "size " + std::to_string(s.w) + " " + std::to_string(s.h) + "\npalette\n";
    for (size_t i = 1; i < s.palette.size(); ++i) {
        Color c = s.palette[i];
        char hex[8];
        std::snprintf(hex, sizeof hex, "%02x%02x%02x", (c >> 11) * 255 / 31, ((c >> 5) & 0x3F) * 255 / 63,
                      (c & 0x1F) * 255 / 31);
        out += keys[i - 1];
        out += std::string(" ") + hex + "\n";
    }
    for (int f = 0; f < s.frames; ++f) {
        out += "frame\n";
        for (int y = 0; y < s.h; ++y) {
            for (int x = 0; x < s.w; ++x) {
                uint8_t idx = s.pixels[(size_t(f) * s.h + y) * s.w + x];
                out += idx ? keys[idx - 1] : '.';
            }
            out += '\n';
        }
    }
    return true;
}

}  // namespace vpet
