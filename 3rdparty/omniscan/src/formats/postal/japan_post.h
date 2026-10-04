#pragma once
// Japan Post 4-state ("Kasutama") codec support (M4b). Tables and check math
// cross-checked against zint backend/postal.c (BSD-3, cited not copied;
// itself citing the Japan Post manual) and Wikipedia's Kasutama description.
// Bar states follow the shared Bar4 convention (0=Full, 1=Asc, 2=Desc,
// 3=Track). Independent implementation.
#include <string>
#include <vector>
#include "fourstate.h"
#include "omniscan/export.h"

namespace omniscan {
namespace postal {

// KASUTSET index of a char ("1234567890-abcdefgh"), -1 when invalid.
int japanpost_char_index(char c);

// Expand an input string (digits, '-', A-Z, case-insensitive) to the
// 20-char intermediate (letters become a/b/c + digit pairs). False on bad
// charset, empty input, or expansion overflow past 20 chars.
bool japanpost_expand_input(const std::string& input, std::string& inter);

// Check char for a 20-char intermediate ('?' on error).
// Exported: tests cross-check the mod-19 table via this entry point.
OMNISCAN_API char japanpost_check_for(const std::string& inter20);

// Full decode: exactly 67 bars (guards + 21 groups of 3). Emits the
// original input (pads stripped, pairs decoded). False otherwise.
bool parse_japanpost(const std::vector<Bar4>& bars, std::string& text);

}  // namespace postal
}  // namespace omniscan
