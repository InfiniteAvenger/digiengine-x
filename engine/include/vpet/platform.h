#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "vpet/framebuffer.h"

namespace vpet {

enum Button : uint8_t { BTN_A = 1, BTN_B = 2, BTN_C = 4 };

// Everything the engine needs from the outside world. One implementation per
// target (SDL for PC/Android, ESP-IDF for the handheld).
class Platform {
public:
    virtual ~Platform() = default;

    // Wall-clock time in milliseconds since the Unix epoch.
    virtual uint64_t now_ms() = 0;
    // Local time minus UTC, in seconds (for bedtimes).
    virtual int32_t utc_offset_s() = 0;
    // Bitmask of currently held buttons.
    virtual uint8_t buttons() = 0;
    virtual void present(const Framebuffer& fb) = 0;
    // Plays a square-wave tone until the next call; 0 = silence. The engine
    // sequences notes itself, so a platform only needs this one switch.
    virtual void set_tone(uint16_t /*freq_hz*/) {}
    // Read-only content such as packs.
    virtual bool read_file(const std::string& path, std::string& out) = 0;
    // Names of the folders inside `path` (for finding installed packs).
    // Platforms with a single built-in pack can leave this empty.
    virtual std::vector<std::string> list_dirs(const std::string& /*path*/) { return {}; }
    // Save slots, stored wherever the platform keeps user data.
    virtual bool load_save(const std::string& slot, std::string& out) = 0;
    virtual bool write_save(const std::string& slot, const std::string& data) = 0;
};

}  // namespace vpet
