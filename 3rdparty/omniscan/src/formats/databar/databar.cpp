// Native GS1 DataBar Omnidirectional (RSS-14) decoder.
//
// Algorithm derived black-box and verified byte-for-byte against the zint
// oracle (attic/dbar_encoder.py, 3000/3000 fresh payloads); see
// docs/formats/databar.md for the derivation and sample counts. Facts only
// in this file: no third-party implementation source consulted, no paid
// spec text reproduced.
//
// Layout (96 modules incl. a leading 1-module space; extract_runs trims
// the leading light run, so the codec sees the 95 scanned modules):
//   idx 0      left guard bar (1)          [leading space trimmed]
//   idx 1-16   char1 (16,4)  L->R
//   idx 17-31  left finder (15)            indexed by c_left
//   idx 32-46  char2 (15,4)  R->L
//   idx 47-61  char4 (15,4)  L->R
//   idx 62-76  right finder (15)           indexed by c_right
//   idx 77-92  char3 (16,4)  R->L
//   idx 93     right guard space (1)
//   idx 94     right guard bar (1)
#include "databar.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "../linear/linear_scan.h"

namespace omniscan {
namespace databar {
namespace {

// ---- Character value tables -------------------------------------------------
// lo, hi, gsum, t_odd, t_even, odd_mods, even_mods, odd_widest, even_widest,
// odd_need_single, even_need_single.
struct Group {
    int lo, hi, gsum, t_odd, t_even;
    int odd_mods, even_mods, odd_widest, even_widest;
    int odd_need1, even_need1;
};

constexpr Group kOut16[5] = {
    {0, 160, 0, 161, 1, 12, 4, 8, 1, 0, 1},
    {161, 960, 161, 80, 10, 10, 6, 6, 3, 0, 1},
    {961, 2014, 961, 31, 34, 8, 8, 4, 5, 0, 1},
    {2015, 2714, 2015, 10, 70, 6, 10, 3, 6, 0, 1},
    {2715, 2840, 2715, 1, 126, 4, 12, 1, 8, 0, 1},
};
constexpr Group kIn15[4] = {
    {0, 335, 0, 4, 84, 5, 10, 2, 7, 1, 0},
    {336, 1035, 336, 20, 35, 7, 8, 4, 5, 1, 0},
    {1036, 1515, 1036, 48, 10, 9, 6, 6, 3, 1, 0},
    {1516, 1596, 1516, 81, 1, 11, 4, 8, 1, 1, 0},
};

// ---- Subset width enumeration ----------------------------------------------
// Lexicographically ascending 4-tuples of element widths in [1, widest]
// summing to `mods`, optionally requiring a single-module element.
struct SubsetTable {
    int mods, widest, need1;
    std::vector<std::array<int, 4>> tuples;
};

const std::array<SubsetTable, 18>& subset_tables() {
    static const std::array<SubsetTable, 18> tables = [] {
        // Every (module-count, widest, need-single) bucket the group tables
        // can request: 16-odd (need1=0), 16-even (need1=1), 15-odd
        // (need1=1), 15-even (need1=0). (mods, widest, need1) is unique.
        const int specs[18][3] = {
            {12, 8, 0}, {10, 6, 0}, {8, 4, 0}, {6, 3, 0}, {4, 1, 0},
            {12, 8, 1}, {10, 6, 1}, {8, 5, 1}, {6, 3, 1}, {4, 1, 1},
            {11, 8, 1}, {9, 6, 1}, {7, 4, 1}, {5, 2, 1},
            {10, 7, 0}, {8, 5, 0}, {6, 3, 0}, {4, 1, 0},
        };
        std::array<SubsetTable, 18> t{};
        for (int i = 0; i < 18; ++i) {
            t[i].mods = specs[i][0];
            t[i].widest = specs[i][1];
            t[i].need1 = specs[i][2];
            for (int a = 1; a <= specs[i][1]; ++a)
                for (int b = 1; b <= specs[i][1]; ++b)
                    for (int c = 1; c <= specs[i][1]; ++c)
                        for (int d = 1; d <= specs[i][1]; ++d) {
                            if (a + b + c + d != specs[i][0]) continue;
                            if (specs[i][2] &&
                                a != 1 && b != 1 && c != 1 && d != 1)
                                continue;
                            t[i].tuples.push_back({a, b, c, d});
                        }
            std::sort(t[i].tuples.begin(), t[i].tuples.end());
        }
        return t;
    }();
    return tables;
}

int subset_index(int mods, int widest, int need1, const int w[4]) {
    for (const SubsetTable& st : subset_tables()) {
        if (st.mods != mods || st.widest != widest || st.need1 != need1)
            continue;
        const std::array<int, 4> key = {w[0], w[1], w[2], w[3]};
        for (size_t i = 0; i < st.tuples.size(); ++i)
            if (st.tuples[i] == key) return static_cast<int>(i);
        return -1;
    }
    return -1;
}

// ---- Finder templates -------------------------------------------------------
constexpr const char* kLeftFinder[9] = {
    "000111111110010", "000111110000010", "000111000000010",
    "000100000000010", "001111111000010", "001111100000010",
    "001110000000010", "011111000000010", "011100000000010",
};
constexpr const char* kRightFinder[9] = {
    "101100000000111", "101111100000111", "101111111000111",
    "101111111110111", "101111000000011", "101111110000011",
    "101111111100011", "101111111000001", "101111111110001",
};

// ---- Helpers ----------------------------------------------------------------
bool run_widths(const std::vector<int>& mods, int a, int b, bool reverse,
                int w[8]) {
    std::vector<int> seg(mods.begin() + a, mods.begin() + b);
    if (reverse) std::reverse(seg.begin(), seg.end());
    if (seg.empty()) return false;
    int n = 0, cur = seg[0], cnt = 0;
    for (int v : seg) {
        if (v == cur) {
            ++cnt;
        } else {
            if (n >= 8) return false;
            w[n++] = cnt;
            cur = v;
            cnt = 1;
        }
    }
    if (n >= 8) return false;
    w[n++] = cnt;
    return n == 8;
}

bool finder_lookup(const std::vector<int>& mods, int a,
                   const char* const tmpl[9], int& out) {
    for (int v = 0; v < 9; ++v) {
        bool ok = true;
        for (int k = 0; k < 15; ++k)
            if (mods[a + k] != (tmpl[v][k] - '0')) {
                ok = false;
                break;
            }
        if (ok) {
            out = v;
            return true;
        }
    }
    return false;
}

int element_checksum(const int w1[8], const int w2[8], const int w3[8],
                     const int w4[8]) {
    const int* chars[4] = {w1, w2, w3, w4};
    long long s = 0;
    long long weight = 1;
    for (int ci = 0; ci < 4; ++ci) {
        for (int mi = 0; mi < 8; ++mi) {
            s += weight * chars[ci][mi];
            weight = (weight * 3) % 79;
        }
    }
    return static_cast<int>(s % 79);
}

int gtin_check_digit(const std::string& d13) {
    int w = 3, s = 0;
    for (char ch : d13) {
        s += (ch - '0') * w;
        w = 4 - w;
    }
    return (10 - (s % 10)) % 10;
}

bool decode_char(const int w[8], const Group* groups, int ngroups,
                 bool outside, int& value) {
    int odd[4] = {w[0], w[2], w[4], w[6]};
    int even[4] = {w[1], w[3], w[5], w[7]};
    int odd_mods = 0, even_mods = 0;
    for (int i = 0; i < 4; ++i) {
        odd_mods += odd[i];
        even_mods += even[i];
    }
    for (int g = 0; g < ngroups; ++g) {
        if (groups[g].odd_mods != odd_mods ||
            groups[g].even_mods != even_mods)
            continue;
        int vodd = subset_index(odd_mods, groups[g].odd_widest,
                                groups[g].odd_need1, odd);
        int veven = subset_index(even_mods, groups[g].even_widest,
                                 groups[g].even_need1, even);
        if (vodd < 0 || veven < 0) return false;
        if (vodd >= groups[g].t_odd || veven >= groups[g].t_even) return false;
        if (outside)
            value = groups[g].gsum + vodd * groups[g].t_even + veven;
        else
            value = groups[g].gsum + veven * groups[g].t_odd + vodd;
        return true;
    }
    return false;
}

}  // namespace

bool parse_databar_omn(const std::vector<int>& runs, std::string& text) {
    text.clear();
    if (runs.size() != 45) return false;
    int total = 0;
    for (int r : runs) {
        if (r < 1 || r > 9) return false;
        total += r;
    }
    if (total != 95) return false;
    // Rebuild the 95 scanned modules (bar-first, alternating).
    std::vector<int> mods;
    mods.reserve(95);
    int bit = 1;
    for (int r : runs) {
        for (int k = 0; k < r; ++k) mods.push_back(bit);
        bit ^= 1;
    }
    if (mods.size() != 95) return false;
    // Guards: leading bar, then space/bar at the right end.
    if (mods[0] != 1 || mods[93] != 0 || mods[94] != 1) return false;
    int w1[8], w2[8], w3[8], w4[8];
    if (!run_widths(mods, 1, 17, false, w1)) return false;   // char1 L->R
    if (!run_widths(mods, 32, 47, true, w2)) return false;   // char2 R->L
    if (!run_widths(mods, 47, 62, false, w4)) return false;  // char4 L->R
    if (!run_widths(mods, 77, 93, true, w3)) return false;   // char3 R->L
    int cl = 0, cr = 0;
    if (!finder_lookup(mods, 17, kLeftFinder, cl)) return false;
    if (!finder_lookup(mods, 62, kRightFinder, cr)) return false;
    // Finder pair -> check character (undo the two excluded-pair skips).
    int temp = 9 * cl + cr;
    if (temp >= 72) temp -= 1;
    if (temp >= 8) temp -= 1;
    const int check = temp;
    if (element_checksum(w1, w2, w3, w4) != check) return false;
    int c1 = 0, c2 = 0, c3 = 0, c4 = 0;
    if (!decode_char(w1, kOut16, 5, true, c1)) return false;
    if (!decode_char(w2, kIn15, 4, false, c2)) return false;
    if (!decode_char(w3, kOut16, 5, true, c3)) return false;
    if (!decode_char(w4, kIn15, 4, false, c4)) return false;
    long long vL = 1597LL * c1 + c2;
    long long vR = 1597LL * c3 + c4;
    long long V = 4537077LL * vL + vR;
    if (V < 0 || V > 9999999999999LL) return false;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%013lld", V);
    std::string d13(buf);
    text = d13 + std::to_string(gtin_check_digit(d13));
    return true;
}

// ---- Image dispatcher -------------------------------------------------------
bool native_databar_handled(Symbology s) noexcept {
    return s == Symbology::DataBar;
}

namespace {

bool try_line(const BinaryImage& bin, const linear::ScanLine& sc,
              std::vector<Result>& out, const Options& opt) {
    std::vector<int> runs;
    int rx0 = 0, rx1 = 0;
    int line_len = sc.horizontal ? bin.width : bin.height;
    if (!linear::extract_runs(bin, sc.fixed, 0, line_len, sc.horizontal, 2,
                              runs, rx0, rx1))
        return false;
    // 45 runs / 95 modules, or nothing: cheap gate before quantizing.
    if (runs.size() != 45) return false;
    std::vector<int> q;
    double mod = 0;
    if (!linear::quantize_runs(runs, 9, 0.30, q, mod)) return false;
    std::string text;
    std::vector<int> rev;
    bool ok = parse_databar_omn(q, text);
    if (!ok) {
        try {
            rev.assign(q.rbegin(), q.rend());
        } catch (...) {
            return false;
        }
        ok = parse_databar_omn(rev, text);
    }
    if (!ok) return false;
    for (const Result& r : out)
        if (r.symbology == Symbology::DataBar && r.text == text) return true;
    Result res;
    res.symbology = Symbology::DataBar;
    res.text = text;
    res.confidence = 1.0f;
    int top = 0, bot = 0;
    if (sc.horizontal) {
        linear::vertical_extent(bin, rx0, rx1, sc.fixed, top, bot);
        res.location = Quad(Point{(float)rx0, (float)top},
                            Point{(float)rx1, (float)top},
                            Point{(float)rx1, (float)(bot + 1)},
                            Point{(float)rx0, (float)(bot + 1)});
    } else {
        int l = 0, r = 0;
        linear::horizontal_extent(bin, rx0, rx1, sc.fixed, l, r);
        res.location = Quad(Point{(float)l, (float)rx0},
                            Point{(float)(r + 1), (float)rx0},
                            Point{(float)(r + 1), (float)rx1},
                            Point{(float)l, (float)rx1});
    }
    (void)opt;
    out.push_back(std::move(res));
    return true;
}

}  // namespace

OMNISCAN_API DecodeStatus decode_native_databar(const ImageView& img,
                                                const Options& opt,
                                                std::vector<Result>& out) noexcept {
    try {
        out.clear();
    } catch (...) {
        return DecodeStatus::BackendError;
    }
    if (!img.valid()) return DecodeStatus::InvalidImage;
    if (opt.max_symbols < 1) return DecodeStatus::InvalidArgument;
    if ((opt.enabled_symbologies & symbology_bit(Symbology::DataBar)) == 0)
        return DecodeStatus::UnsupportedSymbology;
    for (int pass = 0; pass < (opt.try_harder ? 2 : 1); ++pass) {
        BinaryImage bin;
        BinarizerParams bp;
        bp.method =
            (pass == 0) ? BinarizerMethod::Otsu : BinarizerMethod::Sauvola;
        if (binarize(img, bp, bin) != DecodeStatus::Ok) continue;
        if (bin.empty() || bin.width < 8 || bin.height < 8) continue;
        std::vector<linear::ScanLine> scans;
        try {
            linear::plan_scans(img, opt.try_harder, scans);
        } catch (...) {
            return DecodeStatus::BackendError;
        }
        bool found = false;
        for (const linear::ScanLine& sc : scans) {
            try {
                if (try_line(bin, sc, out, opt)) {
                    found = true;
                    break;
                }
            } catch (...) {
                return DecodeStatus::BackendError;
            }
        }
        if (found) return DecodeStatus::Ok;
    }
    return out.empty() ? DecodeStatus::NoBarcodeFound : DecodeStatus::Ok;
}

}  // namespace databar
}  // namespace omniscan
