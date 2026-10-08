#pragma once
// Code 16K value-level codec (stacked Tier-2). Pure codeword math shared
// by the native decoder (src/formats/stacked/stacked_16k.cpp) and the
// test-only encoder (tests/encoders/stacked_encoders.h).
//
// Derived from black-box observation of zint 2.16.0.9 vectors; see
// docs/formats/code_16k.md for the per-claim evidence. Nothing here
// reproduces a paid spec. Deterministic, no I/O; throwing only via
// std::vector growth (callers own the try/catch, per project discipline).
#include <cstddef>
#include <string>
#include <vector>
#include "omniscan/export.h"

namespace omniscan {
namespace stacked16k {

// Left guard bars by absolute row index mod 8 (row 1 = index 0).
// Observed constant across symbol totals 2..15; row 1's entry doubles
// as the upside-down detector (a reversed last row never matches it).
// Right guards follow a murkier rule — stripped unvalidated, see the doc.
inline constexpr int kGuardL[8][5] = {
    {3, 2, 1, 1, 1},  // row 1 mod 8
    {2, 2, 2, 1, 1},  // row 2
    {2, 1, 2, 2, 1},  // row 3
    {1, 4, 1, 1, 1},  // row 4
    {1, 1, 3, 2, 1},  // row 5
    {1, 2, 3, 1, 1},  // row 6
    {1, 1, 1, 4, 1},  // row 7
    {3, 1, 1, 2, 1},  // row 8 (mod 0)
};

// Rows for ndata data codewords: smallest r in [2,16] with
// ndata <= 5r-3 (first + checks overhead). 0 = impossible (> 77).
// Boundaries verified exact against the oracle (7/8, 12/13, 22,
// 27, 28, 32->7 rows, 77->16 rows; 78 refused).
inline int rows_for_data(size_t ndata) {
    for (int r = 2; r <= 16; ++r)
        if (ndata <= static_cast<size_t>(5 * r - 3)) return r;
    return 0;
}

// First codeword, encoder side (explicit switches only, so never
// implicit): 7*(rows-2) + initial subset index (B=1, C=2; A never
// emitted — control inputs are refused by the test encoder).
// Returns false for bad rows/subset.
inline bool first_codeword(int rows, int base_subset, int& first) {
    first = 0;
    if (rows < 2 || rows > 16) return false;
    if (base_subset < 1 || base_subset > 2) return false;
    first = 7 * (rows - 2) + base_subset;
    return true;
}

// Check computation (solved exactly: 72/72 oracle vectors, N = 10..30).
// body = ALL codewords except the two checks (first, data AND pads all
// included); ntotal = body.size() + 2. Casts precede multiplies so even
// fuzz-sized bodies cannot overflow long (realistic maxima land ~6e7).
inline void check_values(const std::vector<int>& body, int& c1, int& c2) {
    int ntotal = static_cast<int>(body.size()) + 2;
    long s1 = 0, s2 = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        s1 += static_cast<long>(body[i]) * (static_cast<long>(i) + 2L);
        s2 += static_cast<long>(body[i]) *
              (2L * ntotal - 1L + static_cast<long>(i) * ntotal);
    }
    c1 = static_cast<int>(s1 % 107);
    c2 = static_cast<int>(s2 % 107);
}

// Full value-level decode: per-row 5 codeword values (0..106),
// top-to-bottom, into payload text. Validates row count (2..16),
// first-codeword decomposition (unique (base, spec) or refuse),
// both mod-107 checks, pad stripping (trailing 103s — data can never
// be 103), and subset tracking with the single row-1-implicit carve-out.
// Refuses initial-A and any A-entry (text mapping unvalidated), shift
// usage, and FNCs in data positions — silence over confident-wrong text
// for a checkless interpretation layer (the checks cover values only).
// Exported for test linkage (shared builds hide everything else); not
// part of the supported API surface.
OMNISCAN_API bool decode_16k_values(
    const std::vector<std::vector<int>>& rows, std::string& text);

}  // namespace stacked16k
}  // namespace omniscan
