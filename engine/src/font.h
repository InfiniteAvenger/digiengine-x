#pragma once

namespace vpet {

// Returns 15 chars (5 rows x 3 columns, '#' = pixel) or nullptr for blanks.
const char* glyph_for(char c);

}  // namespace vpet
