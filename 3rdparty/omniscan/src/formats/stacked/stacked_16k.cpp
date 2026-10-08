// Code 16K value-level decode: per-row 5 codeword values (top to
// bottom) into payload text. Row framing (bands, guards, run splitting)
// lives in native_stacked.cpp; this file owns codeword semantics only:
// first-codeword decomposition, mod-107 checks, pads, subset tracking.
// See docs/formats/code_16k.md for the per-claim oracle evidence.
#include "stacked_16k.h"

namespace omniscan {
namespace stacked16k {

namespace {

// Subset state for text tracking. Latches follow the Code 128 table
// (values mean different things per current subset); anything the text
// mapping cannot prove safe refuses the whole symbol — checks cover
// values, never interpretations, so a wrong map would corrupt silently.
enum class Subset { A, B, C };

bool append_b(int v, std::string& text) {
    if (v < 0 || v > 95) return false;
    try {
        text.push_back(static_cast<char>(32 + v));
    } catch (...) {
        return false;
    }
    return true;
}

bool append_c(int v, std::string& text) {
    if (v < 0 || v > 99) return false;
    try {
        text.push_back(static_cast<char>('0' + v / 10));
        text.push_back(static_cast<char>('0' + v % 10));
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace

bool decode_16k_values(const std::vector<std::vector<int>>& rows,
                       std::string& text) {
    text.clear();
    size_t n = rows.size();
    if (n < 2 || n > 16) return false;
    std::vector<int> flat;
    try {
        flat.reserve(n * 5);
        for (const auto& r : rows) {
            if (r.size() != 5) return false;
            for (int v : r) {
                if (v < 0 || v > 106) return false;
                flat.push_back(v);
            }
        }
        if (flat.size() < 4) return false;  // first + data + 2 checks min
    } catch (...) {
        return false;
    }
    // Checks cover everything except themselves (first, data AND pads —
    // fitted that way over 72 oracle vectors).
    int c1 = flat[flat.size() - 2], c2 = flat[flat.size() - 1];
    std::vector<int> body;
    try {
        body.assign(flat.begin(), flat.end() - 2);
    } catch (...) {
        return false;
    }
    int e1 = 0, e2 = 0;
    check_values(body, e1, e2);
    if (e1 != c1 || e2 != c2) return false;
    // First-codeword decomposition: X = first - 7*(rows-2) splits
    // uniquely into (base, spec) with base in {0,1,2} (A/B/C initial
    // subset), spec 0 (nothing implied) or 3..6 (implicit B->C entry at
    // data position spec-2; row-1 geometry bounds it there). Implicit
    // from any other base is rejected: unobserved, and the general
    // formula would otherwise admit phantom readings.
    int first = body[0];
    int X = first - 7 * (static_cast<int>(n) - 2);
    int base = -1, spec = -1, count = 0;
    for (int b = 0; b <= 2; ++b) {
        for (int s = 0; s <= 6; ++s) {
            if (s != 0 && (s < 3 || b != 1)) continue;
            if (b + s == X) {
                base = b;
                spec = s;
                ++count;
            }
        }
    }
    if (count != 1) return false;  // ambiguous or impossible: refuse
    if (base == 0) return false;   // initial-A text mapping unvalidated
    // Pads: trailing 103s after the checks are off. Data can never be
    // 103 (Start-A-only value, never emitted as data), so stripping is
    // exact — but never strip the first codeword itself.
    size_t end = body.size();
    while (end > 1 && body[end - 1] == 103) --end;
    if (end <= 1) return false;  // first only, no data: degenerate
    // Subset walk over data positions (1-based past `first`).
    Subset st = (base == 2) ? Subset::C : Subset::B;
    int implicit_at = (spec == 0) ? -1 : spec - 2;
    try {
        for (size_t i = 1; i < end; ++i) {
            int v = body[i];
            int datapos = static_cast<int>(i);  // 1-based: body[0]=first
            if (implicit_at >= 0 && datapos == implicit_at) {
                if (st != Subset::B) return false;  // inconsistent input
                st = Subset::C;  // no codeword consumed: the value at
                                 // this position already IS a C-pair
            }
            if (st == Subset::B) {
                if (v == 99) {
                    st = Subset::C;
                    continue;
                }
                if (v == 96 || v == 97) continue;  // FNC3/FNC2: instructions,
                                                   // not message content
                if (v == 98 || v == 100 || v == 101 || v == 102)
                    return false;  // Shift / FNC4 / latch-A / FNC1:
                                   // unmapped, refuse rather than corrupt
                if (!append_b(v, text)) return false;
            } else {  // Subset::C
                if (v == 100) {
                    st = Subset::B;
                    continue;
                }
                if (v == 101 || v == 102)
                    return false;  // latch-A / FNC1 in C: refuse
                if (!append_c(v, text)) return false;  // 0..99 incl 96-99
            }
        }
    } catch (...) {
        return false;
    }
    if (text.empty()) return false;
    return true;
}

}  // namespace stacked16k
}  // namespace omniscan
