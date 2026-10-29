#pragma once
#include <string>
#include <vector>

namespace vpet {

struct BakeReport {
    int species = 0, sprites = 0, evolutions = 0, files_copied = 0;
    std::vector<std::string> errors, warnings;
    std::vector<std::string> placeholders;  // ids that got a generated sprite
};

// Builds a pack folder from a modder-friendly source folder:
//
//   <in>/species.csv      one creature per row (required)
//   <in>/evolutions.csv   one evolution rule per row (optional)
//   <in>/base.json        everything else: theme, items, moves, enemies... (optional)
//   <in>/sprites/...      PNG strips or .spr files referenced above
//
// PNG creature sprites are converted to .spr (PNG kept if > 92 colors). Any
// other file base.json refers to is copied as-is. Returns false if anything
// failed validation; every problem is listed in report.errors.
struct BakeOptions {
    // Generate a simple placeholder sprite (a blob tinted by family, sized by
    // stage, with the name's initials) for any creature whose sprite file is
    // missing, so a pack is playable before all its art exists.
    bool placeholders = false;
};

bool bake_pack(const std::string& in_dir, const std::string& out_dir, BakeReport& report, const BakeOptions& options = {});

}  // namespace vpet
