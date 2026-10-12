#pragma once
// MicroPDF417 (native, Tier-2 stacked): image detector + value-level
// decode. Contract: docs/formats/micropdf417.md steps 1-6 — variant/
// RAP/codeword tables and the RS codec were derived black-box from
// expired patents + the zint oracle (steps 1-5), and the detector
// facts (row geometry, edge anchors, abutting rows) were measured on
// oracle renders before this file existed (step 6). The image path is
// reached through the native stacked dispatcher; the value path is
// exposed for tests and the fuzz target (OMNISCAN_API: the shared
// build compiles with -fvisibility=hidden, so the test/fuzz link needs
// it exported — same pattern as decode_16k_values). Upside-down/
// rotated symbols refuse by construction (steps 1, 5-6 validation
// layers).
#include <string>
#include <vector>

#include "omniscan/binarizer.h"
#include "omniscan/export.h"
#include "omniscan/result.h"

namespace omniscan {
namespace micropdf417 {

// Value level: full codeword stream (cols*rows values = data region
// incl. 900 pads, then EC) -> payload text. Verifies the (cols, rows)
// variant exists, then either all k RS syndromes over GF(929) are
// zero (roots 3^1..3^k, docs step 5) or the bounded errors-only
// corrector fixes up to t = floor(k/2) symbol errors (docs: Error
// correction; stream untouched + false on anything else), then
// decodes the step-4 modes (900 text/pad, 901/924 byte with the
// len>5 / len>=5 group split, 902 numeric base-900 with the 10^n
// prefix, 913 one-shot byte). False on anything invalid; never
// throws.
OMNISCAN_API bool decode_mp417_stream(const std::vector<int>& cws,
                                      int cols, int rows, std::string& text);

// Image level: one binarized pass (band segmentation -> per-variant
// module quantization -> row parse -> validation layers -> stream
// decode). True + exactly one Result appended on success; false
// (nothing appended) otherwise.
bool try_decode_mp417(const BinaryImage& bin, std::vector<Result>& out);

// ---------------------------------------------------------------------------
// Text submode tables (docs step 4), shared with the test encoder:
// value -> char; '\0' means "switch cell / unused slot" (A/L: 27/28/29,
// M: 25/27/28/29, P: 29 are handled by the half() rules, not here).
inline constexpr char kUp[28] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ ";  // 0..26
inline constexpr char kLo[28] = "abcdefghijklmnopqrstuvwxyz ";  // 0..26
// M: 0..9 digits, 10..24 punctuation, [25] = latch-to-Punct (NUL),
// 26 = space.
inline constexpr char kMi[28] = "0123456789&\r\t,:#-.$/+%*=^\0 ";  // 0..26
// P: 0..28 punctuation, 29 = latch-to-A exit (not a char here).
inline constexpr char kPu[30] = ";<>@[\\]_`~!\r\t,:\n-.$/\"|*()?{}'";

// RAP number offsets (docs step 2, measured 34/34): numbers are
// 1-based mod 52 and lefts run consecutively across rows. For 3-4
// col, center = left + d and right = center + d with ONE d per row
// count; for 1-2 col (no center), right = left + e by row count.
// Returns -1 when the row count is not valid for that column class.
inline int rap_center_offset(int r) noexcept {
    if (r == 4) return 24;
    if (r == 6 || r == 8 || r == 10 || r == 12 || r == 15) return 0;
    if (r == 20) return 16;
    if (r == 26 || r == 32) return 8;
    if (r == 38) return 16;
    if (r == 44) return 24;
    return -1;
}
inline int rap_right_offset(int r) noexcept {
    if (r == 8 || r == 14 || r == 17 || r == 20) return 0;
    if (r == 11 || r == 23 || r == 24 || r == 26 || r == 28) return 8;
    return -1;
}

}  // namespace micropdf417
}  // namespace omniscan
