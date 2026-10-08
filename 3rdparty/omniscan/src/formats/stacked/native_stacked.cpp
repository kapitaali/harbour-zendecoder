// Native stacked dispatcher: Code 16K (M3-tier-2-stacked) and Codablock F.
// Code 16K: segment row bands by separator/border bars, decode each row's
// five Code128-width codewords (space-first polarity convention — codeword
// patterns start on space runs where standalone Code128 starts on bars;
// the widths are identical, verified 26/26 rows against verbose ground
// truth), validate guards, stitch by geometry, decode values to text.
// See docs/formats/code_16k.md. Codablock F: boundary groups by the
// >= width/4 divider line, band centers through parse_code128_row, frame
// plus unique-F split in decode_cbf_values. See
// docs/formats/codablock_f.md.
#include "native_stacked.h"

#include <utility>
#include <vector>

#include "omniscan/binarizer.h"
#include "stacked_16k.h"
#include "stacked_cbf.h"
#include "../linear/codecs.h"
#include "../linear/linear_scan.h"
#include "../linear/code128_table.h"

namespace omniscan {
namespace stacked {

bool native_stacked_handled(Symbology s) noexcept {
    return s == Symbology::Code16K || s == Symbology::CodablockF;
}

namespace {

// Match one 6-run group against the Code128 width table. Polarity-blind
// on purpose (see file header): the twin loop in code128.cpp shares the
// contract (bare widths in, value out) but not the code, because the two
// call sites document opposite polarity conventions. Returns 0..106/-1.
//
// Value 106 (STOP) has no 6-run data pattern, yet mod-107 checks land on
// it ~1/107 of the time each — and the oracle renders those checks with
// reverse-stop widths ([2,1,1,1,3,3], observed, spec-unknown). Accepting
// it here converts ~2% of valid symbols from misses to reads. Safety:
// 106 can never be first/data/pad (encoders never emit it there), so a
// damage-formed 106 anywhere but a check slot refuses downstream (subset
// mapping rejects >95/>99; first-decomposition rejects out-of-range
// specs), and a damage-formed 106 IN a check slot fails the check it is
// part of. The checks arbitrate every path.
int match_cell(const int* r) {
    for (int v = 0; v < 107; ++v) {
        const int* t = linear::kCode128Runs[v];
        if (r[0] == t[0] && r[1] == t[1] && r[2] == t[2] && r[3] == t[3] &&
            r[4] == t[4] && r[5] == t[5])
            return v;
    }
    static const int kReverseStop[6] = {2, 1, 1, 1, 3, 3};
    bool is_revstop = true;
    for (int k = 0; k < 6; ++k)
        if (r[k] != kReverseStop[k]) is_revstop = false;
    if (is_revstop) return 106;
    return -1;
}

// Decode one band's center row into five values. row_idx is 0-based from
// the top and drives the period-8 guard table — valid because bands only
// exist between two separators, so band 0 always sits under the top
// border (a cropped symbol with no top border refuses at segmentation,
// never misindexes here). Strict framing: exactly 39 runs (5 guard +
// 30 codeword + 4 guard); guard-L validated; guard-R stripped
// unvalidated (rule open); codewords at offset 5. Damage splits/merges
// runs and refuses here, never downstream.
bool read_band_row(const BinaryImage& bin, int y, int row_idx,
                   int out_vals[5]) {
    std::vector<int> runs;
    int rx0 = 0, rx1 = 0;
    if (!linear::extract_runs(bin, y, 2, bin.width - 2, true, 2, runs, rx0,
                              rx1))
        return false;
    if (runs.size() != 39) return false;
    std::vector<int> q;
    double mod = 0;
    if (!linear::quantize_runs(runs, 4, 0.30, q, mod)) return false;
    const int* g = stacked16k::kGuardL[row_idx & 7];
    for (int k = 0; k < 5; ++k)
        if (q[k] != g[k]) return false;
    for (int j = 0; j < 5; ++j) {
        int cell[6];
        for (int k = 0; k < 6; ++k) cell[k] = q[5 + 6 * j + k];
        int v = match_cell(cell);
        if (v < 0) return false;
        out_vals[j] = v;
    }
    return true;
}

// One Code 16K attempt on a binarized image. True + one Result on
// success; false (try the next format/pass) on any segmentation, row,
// guard, or value failure.
bool try_decode_16k(const BinaryImage& bin, std::vector<Result>& out) {
    // Band segmentation: separator/border bars are single ink runs
    // spanning >=80% of the width (observed, all symbols). Data
    // bands lie strictly between consecutive separator groups; the
    // image margins before the first / after the last group are
    // quiet, not bands. A symbol cropped at the image edge loses
    // its outer border there and refuses below — never misindexes
    // the guard table above (band 0 is always row 1).
    std::vector<std::pair<int, int>> bands;
    try {
        std::vector<int> seps;
        for (int y = 0; y < bin.height; ++y) {
            const uint8_t* row = bin.bits.data() +
                                 static_cast<size_t>(y) * bin.stride;
            int x = 0, inksegs = 0, longest = 0;
            while (x < bin.width) {
                bool ink = (row[x] == 0);
                int x0 = x;
                while (x < bin.width && (row[x] == 0) == ink) ++x;
                if (ink) {
                    ++inksegs;
                    if (x - x0 > longest) longest = x - x0;
                }
            }
            if (inksegs == 1 && longest >= (bin.width * 4) / 5)
                seps.push_back(y);
        }
        size_t i = 0;
        std::vector<std::pair<int, int>> groups;
        while (i < seps.size()) {
            size_t j = i;
            while (j + 1 < seps.size() && seps[j + 1] == seps[j] + 1) ++j;
            groups.emplace_back(seps[i], seps[j]);
            i = j + 1;
        }
        for (size_t b = 0; b + 1 < groups.size(); ++b) {
            int lo = groups[b].second + 1;
            int hi = groups[b + 1].first - 1;
            if (hi - lo + 1 >= 8) bands.emplace_back(lo, hi);
        }
    } catch (...) {
        return false;
    }
    if (bands.size() < 2 || bands.size() > 16) return false;
    // One scan row per band (center); values in geometric order.
    std::vector<std::vector<int>> rows;
    bool band_fail = false;
    try {
        rows.reserve(bands.size());
        for (size_t b = 0; b < bands.size(); ++b) {
            int y = (bands[b].first + bands[b].second) / 2;
            int vals[5] = {0, 0, 0, 0, 0};
            if (!read_band_row(bin, y, static_cast<int>(b), vals)) {
                band_fail = true;
                break;
            }
            rows.emplace_back(vals, vals + 5);
        }
    } catch (...) {
        return false;
    }
    if (band_fail) return false;
    std::string text;
    bool ok = false;
    try {
        ok = stacked16k::decode_16k_values(rows, text);
    } catch (...) {
        ok = false;
    }
    if (!ok) return false;
    // Location quad hugs ink: x from a middle band's row extent,
    // y across all bands.
    try {
        Result res;
        res.symbology = Symbology::Code16K;
        res.text = text;
        res.confidence = 1.0f;
        int my = (bands[bands.size() / 2].first +
                  bands[bands.size() / 2].second) /
                 2;
        std::vector<int> rruns;
        int rx0 = 0, rx1 = 0;
        if (linear::extract_runs(bin, my, 2, bin.width - 2, true, 2, rruns,
                                 rx0, rx1)) {
            int top = bands.front().first, bot = bands.back().second;
            res.location =
                Quad(Point{(float)rx0, (float)top},
                     Point{(float)rx1, (float)top},
                     Point{(float)rx1, (float)(bot + 1)},
                     Point{(float)rx0, (float)(bot + 1)});
        }
        out.push_back(std::move(res));
    } catch (...) {
        return false;
    }
    return true;
}

// One Codablock F attempt on a binarized image. Boundary rows are pixel
// rows whose longest ink run covers >= 1/4 of the width: a divider
// carries the 11-module start, a (C-2)*11-module line, and the
// 11-module stop tail (67..93% ink for C = 9..49), while data rows top
// out at 4-module runs -- ~7x margin, measured on oracle renders in
// attic/cbf_image_check.py. Unlike 16K's single-run 80% rule the
// segment count is ignored on purpose: a CBF divider has 7 ink
// segments (start pattern + line + stop tail). Consecutive boundary
// rows form groups; interiors >= 8 px are the R data bands (R in
// [2, 44]). Each band's center scanline runs bar-first from the first
// to the last ink (rows start and end on bars), quantizes by the
// minimum run (clean renders always contain a 1-module element;
// damage shrinks it and refuses in parse or the row check), then
// parse_code128_row -- forward, then the reverse mirror path -- whose
// mod-103 row check verifies Codablock F rows unchanged (start
// 103 = 0 mod 103). Frame acceptance ({103,104,105} starts,
// set/rowid/cell ranges, uniform width) and the unique-F split both
// live in decode_cbf_values, so upside-down input refuses on rowid
// instead of misreading and rot90/270 dies at segmentation or parse.
bool try_decode_cbf(const BinaryImage& bin, std::vector<Result>& out) {
    try {
        std::vector<int> bnd;
        for (int y = 0; y < bin.height; ++y) {
            const uint8_t* row = bin.bits.data() +
                                 static_cast<size_t>(y) * bin.stride;
            int longest = 0, run0 = -1;
            for (int x = 0; x < bin.width; ++x) {
                if (row[x] == 0) {
                    if (run0 < 0) run0 = x;
                    int len = x - run0 + 1;
                    if (len > longest) longest = len;
                } else {
                    run0 = -1;
                }
            }
            if (longest >= bin.width / 4) bnd.push_back(y);
        }
        std::vector<std::pair<int, int>> groups;
        for (size_t gi = 0; gi < bnd.size(); ++gi) {
            if (!groups.empty() && bnd[gi] == groups.back().second + 1)
                groups.back().second = bnd[gi];
            else
                groups.emplace_back(bnd[gi], bnd[gi]);
        }
        std::vector<std::pair<int, int>> bands;
        for (size_t b = 0; b + 1 < groups.size(); ++b) {
            int lo = groups[b].second + 1;
            int hi = groups[b + 1].first - 1;
            if (hi - lo + 1 >= 8) bands.emplace_back(lo, hi);
        }
        if (bands.size() < 2 || bands.size() > 44) return false;
        std::vector<std::vector<int>> rows;
        rows.reserve(bands.size());
        for (size_t b = 0; b < bands.size(); ++b) {
            int y = (bands[b].first + bands[b].second) / 2;
            std::vector<int> runs;
            int rx0 = 0, rx1 = 0;
            if (!linear::extract_runs(bin, y, 0, bin.width, true, 1, runs,
                                      rx0, rx1))
                return false;
            std::vector<int> q;
            double mod = 0;
            if (!linear::quantize_runs(runs, 4, 0.30, q, mod)) return false;
            std::vector<int> vals;
            if (!linear::parse_code128_row(q, vals)) return false;
            rows.push_back(std::move(vals));
        }
        std::string text;
        if (!stackedcbf::decode_cbf_values(rows, text)) return false;
        Result res;
        res.symbology = Symbology::CodablockF;
        res.text = text;
        res.confidence = 1.0f;
        int my = (bands[bands.size() / 2].first +
                  bands[bands.size() / 2].second) /
                 2;
        std::vector<int> rruns;
        int rx0 = 0, rx1 = 0;
        if (linear::extract_runs(bin, my, 0, bin.width, true, 1, rruns, rx0,
                                 rx1)) {
            int top = bands.front().first, bot = bands.back().second;
            res.location =
                Quad(Point{(float)rx0, (float)top},
                     Point{(float)rx1, (float)top},
                     Point{(float)rx1, (float)(bot + 1)},
                     Point{(float)rx0, (float)(bot + 1)});
        }
        out.push_back(std::move(res));
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace

DecodeStatus decode_native_stacked(const ImageView& img, const Options& opt,
                                   std::vector<Result>& out) noexcept {
    try {
        out.clear();
    } catch (...) {
        return DecodeStatus::BackendError;
    }
    if (!img.valid()) return DecodeStatus::InvalidImage;
    if (opt.max_symbols < 1) return DecodeStatus::InvalidArgument;
    const bool want16k = (opt.enabled_symbologies &
                          symbology_bit(Symbology::Code16K)) != 0;
    const bool wantcbf = (opt.enabled_symbologies &
                          symbology_bit(Symbology::CodablockF)) != 0;
    if (!want16k && !wantcbf) return DecodeStatus::UnsupportedSymbology;
    DecodeStatus st = DecodeStatus::NoBarcodeFound;
    // Binarizer passes mirror the linear/postal dispatchers: Otsu always,
    // Sauvola retry under try_harder.
    for (int pass = 0; pass < (opt.try_harder ? 2 : 1); ++pass) {
        BinaryImage bin;
        BinarizerParams bp;
        bp.method =
            (pass == 0) ? BinarizerMethod::Otsu : BinarizerMethod::Sauvola;
        if (binarize(img, bp, bin) != DecodeStatus::Ok) {
            st = DecodeStatus::BackendError;
            continue;
        }
        if (bin.empty() || bin.width < 8 || bin.height < 8) continue;
        bool found = false;
        try {
            if (want16k && try_decode_16k(bin, out)) found = true;
            if (!found && wantcbf && try_decode_cbf(bin, out)) found = true;
        } catch (...) {
            return DecodeStatus::BackendError;
        }
        if (found) return DecodeStatus::Ok;
    }
    return out.empty() ? st : DecodeStatus::Ok;
}

}  // namespace stacked
}  // namespace omniscan
