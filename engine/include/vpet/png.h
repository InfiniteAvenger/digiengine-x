#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "vpet/sprite.h"

namespace vpet {

// 8-bit RGBA image, row-major, one 0xRRGGBBAA value per pixel.
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> rgba;
};

// Decodes a PNG (non-interlaced; gray, RGB, palette, gray+alpha or RGBA;
// bit depth 8, or 1/2/4/8 for palette images). Self-contained: no zlib.
bool decode_png(const std::string& data, Image& out, std::string& err);

// Encodes RGBA as an uncompressed (stored-deflate) PNG. Simple and valid;
// used by the baker and tests, not for shipping large images.
std::string encode_png(const Image& img);

// Cuts an image into frames (row-major grid of frame_w x frame_h cells; a
// horizontal strip is a 1-row grid) and converts it to an indexed sprite.
// Pixels with alpha < 128 become transparent. At most 255 colors.
// frame_w/frame_h of 0 mean "square frames as tall as the image".
bool sprite_from_image(const Image& img, int frame_w, int frame_h, Sprite& out, std::string& err);

// The reverse, for exporting .spr art to PNG: frames side by side.
Image image_from_sprite(const Sprite& s);

// Writes a sprite in the .spr text format.
bool write_spr(const Sprite& s, std::string& out, std::string& err);

}  // namespace vpet
