#pragma once
// Code 49 native value layer (Tier-2 stacked): codeword-stream decode.
// Contract: docs/formats/code49.md (Alphabet, Rows, Font/Tabulation).
// Landed un-dispatched (Code128-row precedent): no behavior change
// until the image pipeline slice. The value path is exposed for
// tests and the fuzz target (OMNISCAN_API: the shared build compiles
// with -fvisibility=hidden — same pattern as decode_16k_values).
#include <string>
#include <vector>

#include "omniscan/binarizer.h"
#include "omniscan/export.h"
#include "omniscan/result.h"

namespace omniscan {
namespace code49 {

// Value level: flat codeword stream (8R values: rows of 7 data +
// check, last row [T0,T1,G1..G4,C6,F]) + row count -> payload text.
// Verifies every non-last row check and the last-row F (both mod-49
// sums), the C6 rule (S+7(R-2), S in {0,4,5} — S==2 digit-packed
// mode refuses honestly), then decodes the Alphabet (pos0 in set S,
// rest set-0/shifts; G1..G4 taken as read, F covers the row).
// False on anything invalid; never throws.
OMNISCAN_API bool decode_c49_stream(const std::vector<int>& cws, int rows,
                                    std::string& text);

// Pattern level: one 8-run window at (R,slot) -> codeword pair via
// the generated inverse table (bijective: 0 collisions measured).
// False on miss; never throws.
OMNISCAN_API bool c49_pattern_lookup(const int* runs8, int R, int slot,
                                     int& a, int& b);

// Image level: one binarized pass (boundary groups -> bands ->
// per-row module quantization -> window/inverse validation -> stream
// decode). True + exactly one Result appended on success; false
// (nothing appended) otherwise.
bool try_decode_c49(const BinaryImage& bin, std::vector<Result>& out);

// ---------------------------------------------------------------------------
// Alphabet value -> char (docs §Alphabet); '\0' = no char in this set.
// Set 0 (bare): 0-9 digits, 10-35 A-Z, 36 '-', 37 '.', 38 space,
// 39 '$', 40 '/', 41 '+', 42 '%'.
inline constexpr char kC49S0[43] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',  // 0..9
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y',
    'Z',  // 10..35
    '-', '.', ' ', '$', '/', '+', '%',  // 36..42
};
// Set 4 (via shift 43): !(6) "(7) #(8) &(9) '(0) ((36) )(37) *(39)
// ,(40) :(41).
inline constexpr char kC49S4[43] = {
    '\'', '\0', '\0', '\0', '\0', '\0', '!', '"', '#',
    '&',  // 0..9 (0='\'', 6..9)
    '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
    '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0',
    '\0', '\0', '\0', '\0',  // 10..35 (none)
    '(', ')', '\0', '*', ',', ':', '\0',  // 36..42 (36,37,39,40,41)
};
// Set 5 (via shift 44): ;(1) <(2) =(3) >(4) ?(5) @(6) [(7) \(8) ](9)
// ^(0) _(36) `(37) a-z(10-35) {(39) |(40) }(41) ~(42).
inline constexpr char kC49S5[43] = {
    '^', ';', '<', '=', '>', '?', '@', '[', '\\',
    ']',  // 0..9
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y',
    'z',  // 10..35
    '_', '`', '\0', '{', '|', '}', '~',  // 36..42 (36,37,39,40,41,42)
};

}  // namespace code49
}  // namespace omniscan
