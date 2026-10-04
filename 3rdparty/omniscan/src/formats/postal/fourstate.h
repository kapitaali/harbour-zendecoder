#pragma once
// Internal 4-state postal infrastructure + codec declarations (M4).
// Bar states follow zint's convention. All helpers pure/deterministic.
#include <cstdint>
#include <string>
#include <vector>
#include "omniscan/binarizer.h"
#include "omniscan/export.h"
#include "omniscan/image.h"

namespace omniscan {
namespace postal {

enum class Bar4 : uint8_t { Full = 0, Asc = 1, Desc = 2, Track = 3 };

// One vertical ink bar: columns [x0, x1).
struct PBar {
    int x0 = 0;
    int x1 = 0;
};

// Segment uniformly-pitched vertical bars on one row. All bars cross the
// row (true for every 4-state bar at the vertical middle). tol = relative
// uniformity tolerance for bar and gap widths. qleft/qright = quiet margins
// in pitches. Returns false on invalid input, too few bars, or ragged pitch.
bool extract_postal(const BinaryImage& bin, int y, int min_bars, double tol,
                    std::vector<PBar>& bars, double& qleft, double& qright);

// Band top/bottom (global ink extents over the bar columns) + per-bar state.
// Extents grow contiguously from scan row y (bars are solid ink).
bool classify_postal(const BinaryImage& bin, const std::vector<PBar>& bars,
                     int y, std::vector<Bar4>& states, int& top, int& bot);

// 180-degree rotation of a bar sequence: reversed order, asc<->desc swap.
std::vector<Bar4> rotate180(const std::vector<Bar4>& bars);

// RM4SCC/KIX alphabet: index 0-9 -> '0'-'9', 10-35 -> 'A'-'Z'.
// rm4kix_lookup returns -1 when the quad is not a valid character.
int rm4kix_lookup(Bar4 b0, Bar4 b1, Bar4 b2, Bar4 b3);
char rm4kix_char(int idx);  // '?' when bad

// Checksum over data chars (zint CheckCharTopBottom form). -1 on bad input.
int rm4scc_check_idx(const std::string& data);  // 0..35
// Exported: tests verify the reference check vector via this entry point.
OMNISCAN_API char rm4scc_check_char(const std::string& data);  // '?' = error

bool parse_rm4scc(const std::vector<Bar4>& bars, std::string& text,
                  char& check);
bool parse_kix(const std::vector<Bar4>& bars, std::string& text);
bool kix_grammar_ok(const std::string& text);

// ITF-structure Deutsche Post wrapper is fed pixel runs (see below).
// dp_check_valid: 12/14-digit string, 4/9 weights from the left.
bool dp_check_valid(const std::string& full);
bool parse_deutsche_post(const std::vector<int>& pixel_runs, std::string& text,
                         std::string& kind);  // kind: identcode/leitcode

}  // namespace postal
}  // namespace omniscan
