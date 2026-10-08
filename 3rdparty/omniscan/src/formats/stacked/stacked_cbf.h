#pragma once
// Codablock F value-level codec (stacked Tier-2). Pure codeword/frame
// math shared by the native decoder (src/formats/stacked/stacked_cbf.cpp)
// and the test-only encoder (tests/encoders/stacked_encoders.h).
//
// Row contract: rows[r] = [start, set, rowid, slots...] -- exactly what
// parse_code128_row emits (start kept, the mod-103 row check verified
// and stripped, the stop consumed).  The check formula is provably the
// same arithmetic (start 103 = 0 mod 103), so the row reader validates
// Codablock F rows unchanged; the core therefore never re-checks it.
// S = rows[r].size() - 3 slots per row; the last row's final 2 slots
// are the K pair (dropped: formula unverified, see the doc).
//
// Derived from black-box observation of zint 2.16.0.9 vectors; see
// docs/formats/codablock_f.md for the per-claim evidence (nothing here
// reproduces a paid spec).  Deterministic, no I/O; throwing only via
// std::vector/std::string growth (callers own the try/catch, per
// project discipline).
#include <cstddef>
#include <string>
#include <vector>
#include "omniscan/export.h"

namespace omniscan {
namespace stackedcbf {

// Hard format bounds, closed by attic/cbf_geometry_clamp.py (19/19):
// zint stops at 44 rows and 67 columns for every payload (the 44-row
// cap can never request more because data cells <= 2726 = 44*62-2),
// so these are format limits, not sampling artifacts.  Slot count
// S = C - 5 in [4, 62].
inline constexpr int kMinRows = 2;
inline constexpr int kMaxRows = 44;
inline constexpr int kMinSlots = 4;
inline constexpr int kMaxSlots = 62;

// Row ID rule (docs/formats/codablock_f.md section "Row ID"): verified
// for every set at every k = 0..43 -- the full legal range under
// kMaxRows -- over 677 rendered rows, 0 mismatches (cbf_rid_tall.py +
// cbf_rid_latec.py).  Returns -1 when set_col is not 98/99/100.
inline int rowid_expect(int rows, int k, int set_col) {
    if (set_col == 98 || set_col == 100) {
        if (k == 0) return (62 + rows) % 96;
        return k <= 5 ? 10 + k : 20 + k;
    }
    if (set_col == 99) return k == 0 ? rows - 2 : 42 + k;
    return -1;
}

// Frame acceptance + payload split.  Refuses (returns false, text
// cleared) on: frame violations (bounds, start, set, rowid, cell
// ranges -- this is what pins upside-down input, which would otherwise
// decode to wrong text), row-check failures already handled upstream
// by the row reader, no unique fill count F (0 or >1 candidates), a
// trailing control cell, or data-less rows not carrying set 100.
// text is only written on success.
OMNISCAN_API bool decode_cbf_values(
    const std::vector<std::vector<int>>& rows, std::string& text);

}  // namespace stackedcbf
}  // namespace omniscan
