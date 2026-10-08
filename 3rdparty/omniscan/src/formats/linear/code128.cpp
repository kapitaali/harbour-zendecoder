// Native Code 128 ROW reader (M3 stacking infrastructure).
//
// This is deliberately NOT a standalone decoder: Symbology::Code128 stays
// backend-routed, and this parser is never registered in try_fn/order.
// It exists for one purpose — reading the Code-128-based rows of stacked
// formats (Code 16K, Codablock F) during stitching, where the row frame
// (which rows, which direction) is established by the stacking layer, not
// by standalone detection. A standalone native Code 128 would duplicate
// backend coverage and create row-fragment crosstalk of its own; see
// docs/formats/code128_row.md.
//
// Contract: input is a QUANTIZED run vector alternating bar,space,...,
// starting and ending with a bar (same convention as the other codecs.h
// parsers; quantize with quantize_runs(q, 4, 0.30)). Matching is exact
// on quantized modules — damage misquantizes into silence, and the
// mod-103 check arbitrates everything accepted.
#include "codecs.h"

#include <cstdio>

#include "code128_table.h"

namespace omniscan {
namespace linear {

namespace {

// Exact table lookup over one 6-run symbol. Returns the value 0..106 or -1.
int match6(const int* r) {
    for (int v = 0; v < 107; ++v) {
        const int* t = kCode128Runs[v];
        if (r[0] == t[0] && r[1] == t[1] && r[2] == t[2] && r[3] == t[3] &&
            r[4] == t[4] && r[5] == t[5])
            return v;
    }
    return -1;
}

// Weighted mod-103 check (Wikipedia 'Code 128', worked example PJJ123C
// -> 54, also asserted in tests/test_linear_m3.cpp): start counts with
// weight 1, first data symbol weight 1, then 2, 3, ...
// values[0] = start; values[1..] = data WITHOUT the check symbol.
// Returns the expected check value, or -1 on empty input.
long code128_check_value(const std::vector<int>& values) {
    if (values.empty()) return -1;
    long sum = values[0];  // start, weight 1
    for (size_t i = 1; i < values.size(); ++i) {
        // Bounded inputs (103 values max per row in practice, each < 107):
        // no overflow in any realistic or fuzzed size (vectors are capped
        // by the caller; values themselves are table lookups < 107).
        sum += static_cast<long>(values[i]) * static_cast<long>(i);
    }
    return sum % 103;
}

}  // namespace

bool parse_code128_row(const std::vector<int>& q, std::vector<int>& values) {
    values.clear();
    // Longest sane row is a few dozen symbols; cap far above that so a
    // pathological input cannot turn the mod-103 accumulation (or the
    // fuzz harness) into a slow loop. 1600 runs = 265 symbols.
    if (q.size() < 19 || q.size() > 1600) return false;
    // Forward: [start][data*][check][stop-pattern(7)].
    do {
        if ((q.size() - 7) % 6 != 0) break;
        size_t nsym = (q.size() - 7) / 6;  // start + data + check
        if (nsym < 2) break;               // start + check at least
        int start = match6(q.data());
        if (start < 103 || start > 105) break;
        std::vector<int> v;
        try {
            v.reserve(nsym);
            v.push_back(start);
        } catch (...) {
            return false;
        }
        bool ok = true;
        for (size_t s = 1; s < nsym; ++s) {
            int val = match6(q.data() + s * 6);
            if (val < 0 || val > 102) {
                ok = false;
                break;
            }
            try {
                v.push_back(val);
            } catch (...) {
                return false;
            }
        }
        if (!ok) break;
        // Trailing stop pattern: 7 runs, 13 modules.
        const int* st = q.data() + nsym * 6;
        for (int k = 0; k < 7; ++k)
            if (st[k] != kCode128Stop[k]) {
                ok = false;
                break;
            }
        if (!ok) break;
        // Check is the last data symbol: verify, then strip it.
        int check = v.back();
        v.pop_back();
        if (code128_check_value(v) != check) break;
        try {
            values = v;
        } catch (...) {
            return false;
        }
        return true;
    } while (false);
    // Reverse: [reverse-stop(6)][finalbar(2)][check, data..., start with
    // each 6-run group in reverse]. The article defines it in scan order
    // (right-to-left temporal): reverse-stop first, THEN the 2-module
    // bar — matching q[0..5] against the table and q[6] against 2, not
    // the other way around. Each data group is re-reversed before
    // lookup; the value list is un-reversed at the end so callers always
    // see print order.
    do {
        if (q.size() < 19 || (q.size() - 7) % 6 != 0) break;
        bool stop_ok = true;
        for (int k = 0; k < 6; ++k)
            if (q[k] != kCode128ReverseStop[k]) stop_ok = false;
        if (!stop_ok || q[6] != 2) break;
        {
            size_t nsym = (q.size() - 7) / 6;  // check + data + start
            if (nsym < 2) break;
            std::vector<int> rev;
            try {
                rev.reserve(nsym);
            } catch (...) {
                return false;
            }
            bool ok = true;
            for (size_t s = 0; s < nsym; ++s) {
                int g[6];
                for (int k = 0; k < 6; ++k) g[k] = q[7 + s * 6 + (5 - k)];
                int val = match6(g);
                if (val < 0) {
                    ok = false;
                    break;
                }
                try {
                    rev.push_back(val);
                } catch (...) {
                    return false;
                }
            }
            if (!ok) break;
            // rev holds [check, data..., start] (reverse print order).
            int start = rev.back();
            if (start < 103 || start > 105) break;
            rev.pop_back();
            std::vector<int> v;
            try {
                v.reserve(rev.size());
                v.push_back(start);
                for (size_t i = rev.size(); i-- > 0;) v.push_back(rev[i]);
            } catch (...) {
                return false;
            }
            // v = [start, data..., check]: verify and strip.
            int check = v.back();
            v.pop_back();
            if (code128_check_value(v) != check) break;
            try {
                values = v;
            } catch (...) {
                return false;
            }
            return true;
        }
    } while (false);
    return false;
}

}  // namespace linear
}  // namespace omniscan
