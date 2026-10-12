// Code 49 native value layer: inverse pattern lookup (tabulated font)
// + codeword-stream decode (row checks, last-row rules, Alphabet).
// Contract: docs/formats/code49.md. Every constant comes from the doc
// or the generated tables header; nothing here is assumed. Landed
// un-dispatched (no image pipeline yet): reachable only through the
// exported value API used by tests and fuzz.
#include "stacked_code49.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "code49_tables.h"

namespace omniscan {
namespace code49 {
namespace {

// Generated inverse table lives in omniscan::stacked::c49.
namespace c49t = ::omniscan::stacked::c49;

bool inv_lookup(const int* runs, int R, int slot, int& a, int& b) {
    std::uint64_t runs32 = 0;
    for (int i = 0; i < 8; ++i) {
        if (runs[i] < 1 || runs[i] > 15) return false;
        runs32 |= static_cast<std::uint64_t>(runs[i]) << (4 * i);
    }
    const std::uint64_t key =
        (runs32 << 9) | (static_cast<std::uint64_t>(R) << 5) |
        static_cast<std::uint64_t>(slot);
    std::size_t lo = 0, hi = c49t::kInvCount;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (c49t::kInv[mid].key < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < c49t::kInvCount && c49t::kInv[lo].key == key) {
        a = static_cast<int>(c49t::kInv[lo].ab >> 16);
        b = static_cast<int>(c49t::kInv[lo].ab & 0xFFFF);
        return true;
    }
    return false;
}

int row_sum7(const std::vector<int>& cws, int r) {
    int s = 0;
    for (int i = 0; i < 7; ++i) s += cws[static_cast<size_t>(r) * 8 + i];
    return s % 49;
}

// ---- row parsing (image side) ----
// Twins of the MicroPDF417 scanline helpers (kept local per format
// convention): rows start AND end dark here, and the width is the
// fixed 70 modules (no variant search).

// Runs of module bits [from, from+len), must start dark. Returns the
// run count (<= maxr) or -1.
int slice_runs(const uint8_t* bits, int from, int len, int* runs, int maxr) {
    if (bits[from] != 1) return -1;
    int n = 0, cur = 1;
    for (int i = 1; i < len; ++i) {
        if (bits[from + i] == bits[from + i - 1]) {
            ++cur;
        } else {
            if (n >= maxr) return -1;
            runs[n++] = cur;
            cur = 1;
        }
    }
    if (n >= maxr) return -1;
    runs[n++] = cur;
    return n;
}

// Majority of 3 scanlines at the row center (same noise arithmetic
// as the stacked convention: isolated flips survive only 2/3).
void sample_line(const BinaryImage& bin, int x0, int x1, int yc,
                 std::vector<uint8_t>& line) {
    const int n = x1 - x0 + 1;
    line.assign(static_cast<size_t>(n), 0);
    for (int j = 0; j < n; ++j) {
        int dark = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            int y = yc + dy;
            if (y < 0) y = 0;
            if (y >= bin.height) y = bin.height - 1;
            if (bin.bits[static_cast<size_t>(y) * bin.stride + x0 + j] == 0)
                ++dark;
        }
        if (dark >= 2) line[static_cast<size_t>(j)] = 1;
    }
}

// Sampled line [a..b] (dark at both ends by the row anchors) ->
// snapped module bits of length M (= 70). Boundaries snap via
// round(cumulative_px / module_px); the per-row module width
// re-derives from the first-to-last dark pixel span, so anchor
// jitter never accumulates.
bool line_to_modules(const std::vector<uint8_t>& line, int a, int b, int M,
                     std::vector<uint8_t>& bits) {
    const int span = b - a + 1;
    const double m = static_cast<double>(span) / M;
    if (m < 1.5) return false;
    bits.assign(static_cast<size_t>(M), 0);
    double cum = 0;
    int pos = 0;
    int color = 1;  // first pixel (a) is dark
    for (int i = a; i <= b;) {
        int j = i;
        while (j <= b && line[static_cast<size_t>(j)] ==
                             line[static_cast<size_t>(i)])
            ++j;
        cum += static_cast<double>(j - i);
        const int end = static_cast<int>(std::lround(cum / m));
        const int w = end - pos;
        if (w < 1 || pos + w > M) return false;
        for (int k = 0; k < w; ++k)
            bits[static_cast<size_t>(pos++)] = static_cast<uint8_t>(color);
        color = color ? 0 : 1;
        i = j;
    }
    (void)color;
    return pos == M;
}

// One row of module bits -> 8 codeword values. All per-row validation
// layers live here: dark anchors, 35-run framing ([1,1]/[4]), every
// 8-window summing to 16, inverse table per (R,slot).
bool parse_row(const uint8_t* bits, int R, int r, int* cws8) {
    if (bits[0] != 1 || bits[69] != 1) return false;
    int runs[40];
    int n = slice_runs(bits, 0, 70, runs, 40);
    if (n != 35 || runs[0] != 1 || runs[1] != 1 || runs[34] != 4)
        return false;
    for (int k = 0; k < 4; ++k) {
        int wsum = 0;
        for (int i = 0; i < 8; ++i) wsum += runs[2 + 8 * k + i];
        if (wsum != 16) return false;
        int a = -1, b = -1;
        if (!c49_pattern_lookup(runs + 2 + 8 * k, R, 4 * r + k, a, b))
            return false;
        cws8[2 * k] = a;
        cws8[2 * k + 1] = b;
    }
    return true;
}

}  // namespace

bool try_decode_c49(const BinaryImage& bin, std::vector<Result>& out) {
    try {
        const int W = bin.width, H = bin.height;
        if (W < 40 || H < 16) return false;
        // Boundary groups: rows carrying a >=0.8W dark run (borders +
        // dividers are solid full-width ink; data rows peak at a few
        // modules). Bands = interiors between boundary groups.
        std::vector<std::pair<int, int>> bounds;
        for (int y = 0; y < H; ++y) {
            const uint8_t* row =
                bin.bits.data() + static_cast<size_t>(y) * bin.stride;
            int run = 0, best = 0;
            for (int x = 0; x < W; ++x) {
                if (row[x] == 0) {
                    if (++run > best) best = run;
                } else {
                    run = 0;
                }
            }
            if (best * 5 >= W * 4) {
                if (!bounds.empty() && bounds.back().second == y - 1)
                    bounds.back().second = y;
                else
                    bounds.emplace_back(y, y);
            }
        }
        std::vector<std::pair<int, int>> bands;
        for (size_t i = 0; i + 1 < bounds.size(); ++i) {
            const int y0 = bounds[i].second + 1, y1 = bounds[i + 1].first - 1;
            if (y1 >= y0) bands.emplace_back(y0, y1);
        }
        const int R = static_cast<int>(bands.size());
        if (R < 2 || R > 8) return false;
        std::vector<int> flat;
        flat.reserve(static_cast<size_t>(8 * R));
        for (size_t bi = 0; bi < bands.size(); ++bi) {
            const int y0 = bands[bi].first, y1 = bands[bi].second;
            const int hb = y1 - y0 + 1;
            if (hb < 8) return false;
            // x anchors: columns dark on >=50% of the band's rows --
            // both symbol edges are full-height dark (module 0 and the
            // stop bar), so stray rows above/below cannot anchor.
            const int thr = (hb + 1) / 2;
            std::vector<int> colink(static_cast<size_t>(W), 0);
            for (int y = y0; y <= y1; ++y) {
                const uint8_t* row =
                    bin.bits.data() + static_cast<size_t>(y) * bin.stride;
                for (int x = 0; x < W; ++x)
                    if (row[x] == 0) ++colink[static_cast<size_t>(x)];
            }
            int x0 = -1, x1 = -1;
            for (int x = 0; x < W; ++x) {
                if (colink[static_cast<size_t>(x)] >= thr) {
                    if (x0 < 0) x0 = x;
                    x1 = x;
                }
            }
            if (x0 < 0) return false;
            const int wb = x1 - x0 + 1;
            const double mg = static_cast<double>(wb) / 70;
            if (mg < 1.5) return false;
            // Row height: 9 modules interior, 9.5 edge (measured);
            // accept 7.5..11 modules of slack for bleed/crop.
            const double hm = hb / mg;
            if (hm < 7.5 || hm > 11.0) return false;
            std::vector<uint8_t> line, bits;
            sample_line(bin, x0, x1, (y0 + y1) / 2, line);
            int a = -1, b = -1;
            for (int j = 0; j < static_cast<int>(line.size()); ++j) {
                if (line[static_cast<size_t>(j)]) {
                    if (a < 0) a = j;
                    b = j;
                }
            }
            if (a < 0 || a > static_cast<int>(mg) ||
                static_cast<int>(line.size()) - 1 - b >
                    static_cast<int>(mg))
                return false;
            if (!line_to_modules(line, a, b, 70, bits)) return false;
            int cws8[8];
            if (!parse_row(bits.data(), R, static_cast<int>(bi), cws8))
                return false;
            for (int k = 0; k < 8; ++k) flat.push_back(cws8[k]);
        }
        std::string text;
        if (!decode_c49_stream(flat, R, text)) return false;
        if (text.empty()) return false;
        int gx0 = W, gx1 = -1, gy0 = H, gy1 = -1;
        for (const auto& band : bands) {
            gy0 = std::min(gy0, band.first);
            gy1 = std::max(gy1, band.second);
        }
        for (int y = gy0; y <= gy1; ++y) {
            const uint8_t* row =
                bin.bits.data() + static_cast<size_t>(y) * bin.stride;
            for (int x = 0; x < W; ++x)
                if (row[x] == 0) {
                    gx0 = std::min(gx0, x);
                    gx1 = std::max(gx1, x);
                }
        }
        Result res;
        res.symbology = Symbology::Code49;
        res.text = text;
        res.confidence = 1.0f;
        res.location =
            Quad(Point{static_cast<float>(gx0), static_cast<float>(gy0)},
                 Point{static_cast<float>(gx1 + 1), static_cast<float>(gy0)},
                 Point{static_cast<float>(gx1 + 1),
                       static_cast<float>(gy1 + 1)},
                 Point{static_cast<float>(gx0),
                       static_cast<float>(gy1 + 1)});
        out.push_back(std::move(res));
        return true;
    } catch (...) {
        return false;
    }
}

bool c49_pattern_lookup(const int* runs8, int R, int slot, int& a,
                        int& b) {
    if (runs8 == nullptr) return false;
    if (R < 2 || R > 8 || slot < 0 || slot >= 4 * R) return false;
    return inv_lookup(runs8, R, slot, a, b);
}

bool decode_c49_stream(const std::vector<int>& cws, int rows,
                       std::string& text) {
    text.clear();
    if (rows < 2 || rows > 8) return false;
    if (static_cast<int>(cws.size()) != 8 * rows) return false;
    for (int v : cws)
        if (v < 0 || v > 48) return false;
    // Row checks: every non-last row ends with sum(c0..c6) mod 49.
    for (int r = 0; r + 1 < rows; ++r) {
        const int c7 = cws[static_cast<size_t>(r) * 8 + 7];
        if (c7 != row_sum7(cws, r)) return false;
    }
    // Last row: F + C6 rule (S in {0,4,5}; S==2 digit-packed refuses).
    const size_t lo = static_cast<size_t>(rows - 1) * 8;
    int f = 0;
    for (int i = 0; i < 7; ++i) f += cws[lo + i];
    if (cws[lo + 7] != f % 49) return false;
    const int c6 = cws[lo + 6];
    const int S = c6 - 7 * (rows - 2);
    if (S != 0 && S != 4 && S != 5) return false;
    // Data assembly: non-last c0..c6 concatenated, trailing 48s
    // stripped (48 is never a data value); plus R<=6 tail data from
    // last-row c0,c1 ((48,48)->none, (x,48)->[x], (x,y)->[x,y]).
    // R>=7 last-row c0,c1 are computed, never appended.
    std::vector<int> data;
    data.reserve(static_cast<size_t>(rows) * 7);
    for (int r = 0; r + 1 < rows; ++r)
        for (int i = 0; i < 7; ++i)
            data.push_back(cws[static_cast<size_t>(r) * 8 + i]);
    while (!data.empty() && data.back() == 48) data.pop_back();
    if (rows <= 6) {
        const int t0 = cws[lo], t1 = cws[lo + 1];
        if (t0 == 48 && t1 == 48) {
        } else if (t1 == 48) {
            data.push_back(t0);
        } else {
            data.push_back(t0);
            data.push_back(t1);
        }
    }
    if (data.empty()) return false;
    // Alphabet: pos0 in set S, rest start in set 0; 43/44 + value;
    // 48 mid-data, dangling shifts and unmapped values refuse.
    const char* setS = (S == 0) ? kC49S0 : (S == 4) ? kC49S4 : kC49S5;
    const int v0 = data[0];
    if (v0 < 0 || v0 > 42 || setS[v0] == '\0') return false;
    text.push_back(setS[v0]);
    for (size_t i = 1; i < data.size();) {
        const int v = data[i];
        if (v == 43 || v == 44) {
            if (i + 1 >= data.size()) return false;
            const int w = data[i + 1];
            const char* tbl = (v == 43) ? kC49S4 : kC49S5;
            if (w < 0 || w > 42 || tbl[w] == '\0') return false;
            text.push_back(tbl[w]);
            i += 2;
        } else {
            if (v < 0 || v > 42 || kC49S0[v] == '\0') return false;
            text.push_back(kC49S0[v]);
            i += 1;
        }
    }
    return true;
}

}  // namespace code49
}  // namespace omniscan
