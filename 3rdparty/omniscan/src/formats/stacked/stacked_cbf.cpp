// Codablock F value-level decode: framed row values -> payload text.
// Row framing (bands, dividers, scanline, Code-128 run parse) lives in
// native_stacked.cpp; this file owns the acceptance frame and the
// data/fill/K split.  The executable spec it follows 1:1 is
// attic/cbf_decode_check.py (curated 116/116 + random 120/120 + sweep
// 425/425 + negatives 20/20 against the zint oracle); see
// docs/formats/codablock_f.md for the per-claim evidence.
#include "stacked_cbf.h"

#include <algorithm>

namespace omniscan {
namespace stackedcbf {

namespace {

// Code set the walk state can hold (anchor comes from the set column).
enum class Sub { A, B, C };

inline bool control_in(Sub s, int c) {
    if (s == Sub::C) return c == 100 || c == 101;  // 98/99 are data "98"/"99"
    return c >= 98 && c <= 101;                    // shift + both switches
}

inline Sub step(Sub s, int c) {
    if (s == Sub::C) return c == 100 ? Sub::B : Sub::A;  // 100->B, 101->A
    if (c == 99) return Sub::C;
    if (c == 100) return Sub::B;
    return Sub::A;  // 101
}

inline Sub other(Sub s) { return s == Sub::A ? Sub::B : Sub::A; }

// Append one data cell's text.  FNC cells produce no text in every
// state: 96/97 = FNC3/FNC2 in A and B, 102 = FNC1 everywhere.  In Code
// C, 96/97 are ordinary digit pairs (oracle: input "9697" -> cells
// 96, 97, decodes back), only 102 is an FNC there.  Returns false only
// on allocation failure; c is in [0,102] (frame + walk guarantee).
bool append_cell(Sub s, int c, std::string& text) {
    try {
        if (s == Sub::C) {
            if (c <= 99) {
                text.push_back(static_cast<char>('0' + c / 10));
                text.push_back(static_cast<char>('0' + c % 10));
            }
            return true;  // 102 = FNC1: no text
        }
        if (s == Sub::A) {
            if (c >= 96) return true;  // FNC3/FNC2/FNC1: no text
            text.push_back(static_cast<char>(c < 64 ? 32 + c : c - 64));
            return true;
        }
        if (c >= 96) return true;  // B: FNC2/FNC3/FNC1: no text
        text.push_back(static_cast<char>(32 + c));
        return true;
    } catch (...) {
        return false;
    }
}

// F-independent frame acceptance, checked once before the split
// (docs/formats/codablock_f.md, split policy step 0):
//   - kMinRows..kMaxRows rows, uniform width, S slots in
//     [kMinSlots, kMaxSlots] (the zint geometry bounds C in [9, 67]),
//   - start code in {103,104,105} (observed always 103; the row reader
//     accepts any Code-128 start, so the core does too),
//   - set in {98,99,100} and rowid == rowid_expect (the upside-down
//     pin: with rows reversed the k=0 rowid never matches for any
//     set -- a rot180 scan must refuse, not decode to wrong text),
//   - every slot cell in [0,102], including the dropped K pair that
//     the split walk never examines.
// The row check is NOT re-checked here: the core receives
// check-stripped rows and the row reader already verified it.
bool frame_ok(const std::vector<std::vector<int>>& rows) {
    const size_t R = rows.size();
    if (R < static_cast<size_t>(kMinRows) ||
        R > static_cast<size_t>(kMaxRows)) {
        return false;
    }
    const size_t width = rows[0].size();
    if (width < 3 + static_cast<size_t>(kMinSlots) ||
        width > 3 + static_cast<size_t>(kMaxSlots)) {
        return false;
    }
    for (size_t k = 0; k < R; ++k) {
        const std::vector<int>& row = rows[k];
        if (row.size() != width) return false;
        if (row[0] < 103 || row[0] > 105) return false;
        const int set_col = row[1];
        if (set_col != 98 && set_col != 99 && set_col != 100) return false;
        if (row[2] != rowid_expect(static_cast<int>(R),
                                   static_cast<int>(k), set_col)) {
            return false;
        }
        for (size_t i = 3; i < row.size(); ++i) {
            if (row[i] < 0 || row[i] > 102) return false;
        }
    }
    return true;
}

// Walk the data cells [0, D) in flat order, split across rows.  Each
// row re-anchors its state from its set column (no state carry across
// boundaries -- zint relies on this: switches can land on a row end and
// evaporate).  A set cell (98 in A/B) shifts exactly one following cell.
// Fills end_state with each row's final state (the fill-entry rule
// consults it) and text with the payload so far.  Returns false on an
// out-of-range set/cell, an allocation failure, or when the final data
// cell is a control (switch or dangling shift) -- the anti-F-1 rule.
bool walk_rows(const std::vector<std::vector<int>>& rows, size_t D, size_t S,
               std::string& text, std::vector<Sub>& end_state) {
    text.clear();
    end_state.clear();
    bool last_data = false;
    try {
        end_state.reserve(rows.size());
        for (size_t r = 0; r < rows.size(); ++r) {
            const std::vector<int>& row = rows[r];
            Sub st;
            if (row[1] == 98) {
                st = Sub::A;
            } else if (row[1] == 99) {
                st = Sub::C;
            } else if (row[1] == 100) {
                st = Sub::B;
            } else {
                return false;  // unreachable after frame_ok
            }
            bool sh = false;
            const size_t lo = r * S;
            const size_t hi = std::min(D, (r + 1) * S);
            for (size_t idx = lo; idx < hi; ++idx) {
                const int c = row[3 + (idx - lo)];
                if (c > 102) return false;
                const Sub e = (sh && st != Sub::C) ? other(st) : st;
                if (c == 98 && e != Sub::C) {
                    sh = true;  // one-cell Shift A<->B (re-armed if repeated)
                    last_data = false;
                    continue;
                }
                if (control_in(e, c)) {
                    st = step(e, c);
                    sh = false;
                    last_data = false;
                    continue;
                }
                if (!append_cell(e, c, text)) return false;
                sh = false;
                last_data = true;
            }
            end_state.push_back(st);
        }
    } catch (...) {
        return false;
    }
    return last_data;
}

}  // namespace

bool decode_cbf_values(const std::vector<std::vector<int>>& rows,
                       std::string& text) {
    text.clear();
    if (!frame_ok(rows)) return false;
    const size_t R = rows.size();
    const size_t S = rows[0].size() - 3;
    const size_t region = R * S - 2;  // K occupies the last 2 slots
    int count = 0;
    std::string best;
    try {
        // Enumerate the fill count F; D = data cells >= 1.  A candidate
        // must satisfy the data walk, data-less-row set, and the fill
        // pattern; accept only a unique F (else refuse ambiguous).
        for (size_t F = 0; F <= region; ++F) {
            const size_t D = region - F;
            if (D < 1) continue;
            std::string candidate;
            std::vector<Sub> end_state;
            if (!walk_rows(rows, D, S, candidate, end_state)) continue;
            // Rows with no data cells under this D must carry set 100.
            bool ok = true;
            for (size_t r = 0; r < R && ok; ++r) {
                if (std::min(D, (r + 1) * S) <= r * S && rows[r][1] != 100) {
                    ok = false;
                }
            }
            if (!ok) continue;
            // Fill cells (idx D .. D+F-1): per-row restart; a row's
            // first fill is 100 iff its data ended in Code C else 99,
            // then alternating 99/100 within the row.  Set columns are
            // already frame-checked.
            int prev_row = -1;
            int prev_v = -1;
            for (size_t idx = D; idx < D + F && ok; ++idx) {
                const size_t r = idx / S;
                const size_t slot = idx % S;
                const int v = rows[r][3 + slot];
                int want;
                if (static_cast<int>(r) != prev_row) {
                    want = (end_state[r] == Sub::C) ? 100 : 99;
                } else {
                    want = (prev_v == 100) ? 99 : 100;
                }
                if (v != want) {
                    ok = false;
                    break;
                }
                prev_row = static_cast<int>(r);
                prev_v = v;
            }
            if (!ok) continue;
            if (++count > 1) return false;  // ambiguous F: refuse
            best = std::move(candidate);
        }
    } catch (...) {
        return false;
    }
    if (count != 1) return false;
    text = std::move(best);
    return true;
}

}  // namespace stackedcbf
}  // namespace omniscan
