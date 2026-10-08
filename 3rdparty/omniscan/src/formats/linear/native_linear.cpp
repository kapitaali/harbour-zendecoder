// Native linear Tier-2 dispatcher (M3): MSI, Plessey, Telepen, Pharmacode.
// Binarizes once (Otsu; try_harder retries failures with Sauvola), scans
// lines in both directions (except Pharmacode: direction-ambiguous by design,
// forward only), dedupes, respects max_symbols.
#include "native_linear.h"
#include <algorithm>
#include <cmath>
#include "codecs.h"
#include "linear_scan.h"
#include "../postal/fourstate.h"

namespace omniscan {
namespace linear {
namespace {

// Minimum quiet zone, in module widths, required on BOTH sides before a
// Pharmacode hit is accepted. See the rationale at the guard site: 4 is
// measured to sit between the worst noise case (2) and the worst genuine
// symbol (6). Only Pharmacode uses this -- it is the one native linear
// format with no check character.
constexpr int kPharmacodeQuietModules = 4;
// Minimum perpendicular extent of EVERY bar, in module widths, before a
// Pharmacode hit is accepted. Genuine symbols measure 21.3-32.0 (generated
// corpus, scales 2-4, both 90 orientations, padded frames); noise that
// survived the quiet guard measured 1.0 and 3.0. See the guard site for the
// full rationale, including why this is measured per bar rather than across
// the whole symbol.
constexpr long kPharmacodeBarModules = 12;

struct Hit {
    Symbology sym = Symbology::Unknown;
    std::string text;
    Quad quad{};
    std::string aux;      // annotation value (check scheme / DP kind)
    std::string aux_key;  // annotation key ("check", "kind")
};

// One-track Pharmacode candidate held for the end-of-pass emission
// decision (see below): guards already passed, geometry kept so the
// quad can be built if it emits. Stashes are per binarizer pass.
struct PharmaStash {
    Hit hit;
    int rx0 = 0, rx1 = 0;  // row-run span (meaningful if horizontal)
    int fixed = 0;         // scan line coordinate
    bool horizontal = true;
};

bool try_msi(const std::vector<int>& q, const Options& opt, Hit& hit) {
    std::vector<int> qq;
    double mod = 0;
    if (!quantize_runs(q, 2, 0.30, qq, mod)) return false;
    std::string digits, scheme;
    if (!parse_msi(qq, digits, scheme, opt.msi_check)) return false;
    hit.sym = Symbology::MSI;
    hit.text = digits;
    hit.aux = scheme;
    hit.aux_key = "check";
    return true;
}

bool try_plessey(const std::vector<int>& runs, const Options&, Hit& hit) {
    std::vector<int> q;
    double mod = 0;
    if (!quantize_runs(runs, 3, 0.30, q, mod)) return false;
    // Bars {1,3}, spaces {1,2,3} (space 3 = zint's symmetric 1:3).
    for (size_t i = 0; i < q.size(); ++i) {
        if ((i % 2) == 0) {
            if (q[i] != 1 && q[i] != 3) return false;
        } else {
            if (q[i] != 1 && q[i] != 2 && q[i] != 3) return false;
        }
    }
    std::string hex;
    if (!parse_plessey(q, hex)) return false;
    hit.sym = Symbology::Plessey;
    hit.text = hex;
    return true;
}

bool try_telepen(const std::vector<int>& runs, const Options&, Hit& hit) {
    std::vector<int> q;
    double mod = 0;
    if (!quantize_runs(runs, 3, 0.30, q, mod)) return false;
    for (int w : q)
        if (w != 1 && w != 3) return false;
    std::string text;
    if (!parse_telepen(q, text)) return false;
    hit.sym = Symbology::Telepen;
    hit.text = text;
    return true;
}

struct PharmaBar {
    int w;    // bar width px, along the scan line
    int top;  // ink top px, perpendicular axis, full-image coords
    int bot;  // ink bottom px, inclusive
};

// Geometry core shared by the two-track attempt: classifies measured bar
// boxes (left-to-right) against a frame and parses bijective base 3.
// Horizontal scans only at the call site: bar columns are found by a
// vertical ink projection that has no meaning for column scans (a rotated
// two-track symbol is inherently silent — one image row can never cross
// bars stacked in two rows — unlike one-track whose tall bars columns do
// cross; docs/formats/pharmacode.md states this).
//
// The caller supplies boxes (width + full-column ink extents per bar),
// the module, and the image height; ALL policy decisions except the
// value range live here so there is exactly one place that defines what
// "two-track structure" means:
//
// - 2..16 bars; each bar narrow-band (0.65..1.5 modules) AND uniform
//   with its siblings (max/min <= 1.5x). Genuine bars are uniform by
//   print; wide or ragged bars mean damage.
// - Heights quantize against a frame with tolerance max(2px, 1 module):
//   full (spans frame), top-half, bottom-half. Anything else refuses.
// - Short uniform frames (all bars one half) double the frame and pick
//   the in-bounds registration: an all-half pattern is the normal state
//   of values like 4 and 8, never evidence of damage. If both doubled
//   registrations fit (loosely framed symbol), bottom wins — documented
//   heuristic for a provably ambiguous case, FP-measured by the suites.
//   Neither fitting means tight all-full ink: all-3s, decided at
//   emission.
// - all_full is REPORTED, not refused: uniform-full patterns are
//   ratio-identical to one-track all-narrow symbols, and the emission
//   layer gives those to one-track when its guards pass (existing reads
//   must not move) and to two-track otherwise.
// On success decimal holds the value and all_full says whether every
// bar classified full. Range 4..64570080 enforced in parse_pharma2.
bool try_pharma2(const std::vector<PharmaBar>& bars, double mod, int img_h,
                 std::string& decimal, bool& all_full) {
    decimal.clear();
    all_full = false;
    size_t n = bars.size();
    if (n < 2 || n > 16) return false;
    if (!(mod > 0) || img_h <= 0) return false;
    double wmin = -1.0, wmax = -1.0;
    int t0 = -1, b0 = -1;
    for (const PharmaBar& b : bars) {
        if (b.w <= 0 || b.bot < b.top) return false;
        double r = b.w / mod;
        if (r < 0.65 || r > 1.5) return false;  // narrow band only
        if (wmin < 0 || b.w < wmin) wmin = static_cast<double>(b.w);
        if (wmax < 0 || b.w > wmax) wmax = static_cast<double>(b.w);
        if (t0 < 0 || b.top < t0) t0 = b.top;
        if (b0 < 0 || b.bot > b0) b0 = b.bot;
    }
    if (wmax > 1.5 * wmin) return false;  // ragged widths: damage, refuse
    double hf = static_cast<double>(b0) - t0 + 1.0;
    if (hf <= 0) return false;
    const double tol = mod > 2.0 ? mod : 2.0;
    std::vector<int> digits;
    try {
        digits.reserve(n);
    } catch (...) {
        return false;
    }
    // Classify every bar against the frame thirds. Uniform-span bars
    // all land "full" here; what that MEANS is decided below, because a
    // uniform span is where the frame itself is ambiguous: it may be the
    // full symbol (bars genuinely full) or one half of a doubled frame
    // (bars genuinely half, the other half empty by value, not damage).
    // Mixed spans force the full frame — no doubling is involved, so no
    // ruler, no magic threshold, just thirds.
    double mid = (static_cast<double>(t0) + b0) / 2.0;
    all_full = true;
    for (const PharmaBar& b : bars) {
        double t = b.top, e = b.bot;
        bool full = (std::fabs(t - t0) <= tol && std::fabs(e - b0) <= tol);
        bool top =
            (!full && std::fabs(t - t0) <= tol && std::fabs(e - mid) <= tol);
        bool bot = (!full && !top && std::fabs(t - mid) <= tol &&
                    std::fabs(e - b0) <= tol);
        if (!full && !top && !bot) return false;
        if (!full) all_full = false;
        try {
            digits.push_back(full ? 3 : (top ? 2 : 1));
        } catch (...) {
            return false;
        }
    }
    if (!all_full) return parse_pharma2(digits, decimal);
    // Uniform span: try the doubled frame before accepting all-3s. A
    // uniform span at half-module aspect is the normal state of all-half
    // values (4, 8, ...), so the in-bounds registration wins over the
    // full-frame reading whenever exactly one doubled frame fits inside
    // the image. Neither fitting means tight all-full ink (or a symbol
    // at/below the resolution floor): fall back to all-3s and let the
    // emission tiebreak decide. Both fitting (loosely framed symbol)
    // falls back to print physics (ruler) then margin symmetry — see
    // below; both documented heuristics for a provably ambiguous case,
    // FP-measured by the suites.
    bool fit_lo = (static_cast<double>(t0) - hf >= -1.0);
    bool fit_hi = (static_cast<double>(b0) + hf <= img_h);
    int d = 3;
    if (fit_lo != fit_hi)
        d = fit_lo ? 1 : 2;
    else if (!fit_lo && !fit_hi)
        d = 3;  // neither fits: tight all-full ink (or at/below floor)
    else {
        // Both doubled frames fit: the framing is too loose for bounds
        // to decide anything, so fall back to print physics. Full bars
        // run ~10 modules tall at oracle proportions, halves ~5; the
        // geometric mid separates them. (A single threshold for a
        // genuinely ambiguous case — documented heuristic, validated by
        // the oracle sweep including the loose-whitespace q2_ set that
        // forced this branch into existence.)
        if (hf >= 7.5 * mod) {
            d = 3;
        } else {
            // Halves: pick the registration with more balanced margins.
            // Labels tend to center content; the quiet guard already
            // forces *some* margin everywhere, so symmetry is the only
            // remaining signal. Bottom wins exact ties (arbitrary,
            // documented).
            double above_lo = static_cast<double>(t0) - hf;
            double below_lo = static_cast<double>(img_h - 1) - b0;
            double above_hi = static_cast<double>(t0);
            double below_hi = static_cast<double>(img_h - 1) - (b0 + hf);
            double imb_lo = std::fabs(above_lo - below_lo);
            double imb_hi = std::fabs(above_hi - below_hi);
            d = (imb_lo <= imb_hi) ? 1 : 2;
        }
    }
    if (d != 3) all_full = false;  // halves outcome, not a full pattern
    try {
        digits.assign(n, d);
    } catch (...) {
        return false;
    }
    // NOTE: no all_full refusal anywhere here. Uniform-full patterns
    // match two-track geometry, and whether they EMIT as two-track is
    // the emission layer's decision (one-track tiebreak when its guards
    // pass, two-track otherwise) — see the scan loop below.
    return parse_pharma2(digits, decimal);
}

bool try_pharmacode(const std::vector<int>& runs, const Options&, Hit& hit) {
    if (runs.size() < 3) return false;  // >= 2 bars => >= 3 runs
    // Space uniformity supplies the module: min space = module.
    int sp_min = -1;
    for (size_t i = 1; i < runs.size(); i += 2) {
        if (sp_min < 0 || runs[i] < sp_min) sp_min = runs[i];
    }
    if (sp_min <= 0) return false;
    double mod = static_cast<double>(sp_min);
    for (size_t i = 1; i < runs.size(); i += 2) {
        double r = runs[i] / mod;
        if (r < 0.65 || r > 1.5) return false;  // separators must be ~uniform
    }
    std::vector<int> bars;
    try {
        for (size_t i = 0; i < runs.size(); i += 2) bars.push_back(runs[i]);
    } catch (...) {
        return false;
    }
    std::string decimal;
    if (!parse_pharmacode(bars, mod, decimal)) return false;
    hit.sym = Symbology::Pharmacode;
    hit.text = decimal;
    hit.aux = "1";  // one-track; the two-track emission below marks "2"
    hit.aux_key = "tracks";
    return true;
}

using TryFn = bool (*)(const std::vector<int>&, const Options&, Hit&);

bool try_dp(const std::vector<int>& runs, const Options&, Hit& hit) {
    std::string text, kind;
    if (!postal::parse_deutsche_post(runs, text, kind)) return false;
    hit.sym = Symbology::DeutschePost;
    hit.text = text;
    hit.aux = kind;
    hit.aux_key = "kind";
    return true;
}

TryFn try_fn(Symbology s) {
    switch (s) {
        case Symbology::MSI:
            return try_msi;
        case Symbology::Plessey:
            return try_plessey;
        case Symbology::Telepen:
            return try_telepen;
        case Symbology::Pharmacode:
            return try_pharmacode;
        case Symbology::DeutschePost:
            return try_dp;
        default:
            return nullptr;
    }
}

bool wants_reverse(Symbology s) {
    // One-track Pharmacode has no direction marker; the mirror of a valid
    // code is another valid code, so reverse attempts only add phantoms.
    return s != Symbology::Pharmacode;
}

// Shared tail for scan-line hits: dedupe, location quad, append,
// max_symbols cutoff. Extracted (behavior-identical) so the two-track
// decision below emits through the same path instead of duplicating it.
enum class Emit { Emitted, Duplicate, Stop, Error };

Emit emit_hit(std::vector<Result>& out, const Options& opt, const Hit& hit,
              const Quad& quad) noexcept {
    try {
        for (const Result& r : out)
            if (r.symbology == hit.sym && r.text == hit.text)
                return Emit::Duplicate;
        Result res;
        res.symbology = hit.sym;
        res.text = hit.text;
        res.confidence = 1.0f;
        if (opt.return_parsed && !hit.aux.empty() && !hit.aux_key.empty())
            res.parsed[hit.aux_key] = hit.aux;
        res.location = quad;
        out.push_back(std::move(res));
    } catch (...) {
        return Emit::Error;
    }
    if (static_cast<int>(out.size()) >= opt.max_symbols) return Emit::Stop;
    return Emit::Emitted;
}

// Location quad for a row-scan hit over [rx0,rx1): ink extent plus the
// orthogonal extent, mirroring the inline code this refactors. The
// extent calls allocate nothing and clamp their bounds, so they cannot
// throw (kept outside any try for exactly that reason).
Quad row_quad(const BinaryImage& bin, int rx0, int rx1, int fixed) {
    int top = 0, bot = 0;
    vertical_extent(bin, rx0, rx1, fixed, top, bot);
    return Quad(Point{(float)rx0, (float)top}, Point{(float)rx1, (float)top},
                Point{(float)rx1, (float)(bot + 1)},
                Point{(float)rx0, (float)(bot + 1)});
}

Quad col_quad(const BinaryImage& bin, int rx0, int rx1, int fixed) {
    int l = 0, r = 0;
    horizontal_extent(bin, rx0, rx1, fixed, l, r);
    return Quad(Point{(float)l, (float)rx0}, Point{(float)(r + 1), (float)rx0},
                Point{(float)(r + 1), (float)rx1},
                Point{(float)l, (float)rx1});
}

struct Pharma2Span {
    std::string text;
    int x0 = 0, x1 = 0;  // bar span, x1 exclusive (mirrors rx1)
    int top = 0, bot = 0;  // frame extremes (ink), bot inclusive
    bool all_full = false;
};

// Column-projection two-track attempt. Bar columns are found from
// vertical ink counts over the FULL image height — independent of any
// scan row — because no single row crosses bars stacked in two rows,
// so row runs can never discover them all (a row through one half
// misses the other half's bars entirely, and the visible subset's
// widened gaps fail uniformity). This is the entire reason the attempt
// lives here, outside the try_fn loop, instead of inside it: it must
// also run when the row's own runs prove nothing.
//
// Runs on every horizontal scan line with Pharmacode enabled (vertical
// scans see single bars; rotated two-track is inherently silent).
// Cost is one O(W*H) byte pass per pass whenever Pharmacode is
// enabled — inside run-to-run noise on corpus-sized images
// (barcode-free row 0.16–0.20 ms across runs vs 0.136 before; the
// suites, not the clock, are the arbiter here) — and stated in
// docs/formats/pharmacode.md.
bool project_pharma2(const BinaryImage& bin, Pharma2Span& hit) {
    hit = Pharma2Span{};
    int W = bin.width, H = bin.height;
    if (bin.empty() || W < 8 || H < 8) return false;
    std::vector<int> counts;
    try {
        counts.assign(static_cast<size_t>(W), 0);
    } catch (...) {
        return false;
    }
    for (int y = 0; y < H; ++y) {
        const uint8_t* row =
            bin.bits.data() + static_cast<size_t>(y) * bin.stride;
        for (int x = 0; x < W; ++x)
            if (row[x] == 0) ++counts[static_cast<size_t>(x)];
    }
    // Half bars carry ~H/2 ink pixels per column; gaps ~0. The threshold
    // sits far below any genuine bar and far above speckle (independent
    // salt-and-pepper lands ~H/200 per column) — structured patterns
    // that clear it (text strokes, stripes) die on uniformity below.
    int thr = H / 12;
    if (thr < 3) thr = 3;
    struct Seg {
        int a, b;
    };  // half-open [a,b)
    std::vector<Seg> segs;
    try {
        int x = 0;
        while (x < W) {
            if (counts[static_cast<size_t>(x)] < thr) {
                ++x;
                continue;
            }
            int a = x;
            while (x < W && counts[static_cast<size_t>(x)] >= thr) ++x;
            segs.push_back(Seg{a, x});
        }
        if (segs.size() < 2 || segs.size() > 16) return false;
    } catch (...) {
        return false;
    }
    // Module = min gap; gaps and widths share try_pharmacode's bands
    // (ratios, so print scale is irrelevant).
    double gmin = -1.0;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        double g = static_cast<double>(segs[i + 1].a - segs[i].b);
        if (g <= 0) return false;
        if (gmin < 0 || g < gmin) gmin = g;
    }
    if (!(gmin > 0)) return false;
    double wmin = -1.0, wmax = -1.0;
    for (const Seg& s : segs) {
        double w = static_cast<double>(s.b - s.a);
        if (w < 0.65 * gmin || w > 1.5 * gmin) return false;
        if (wmin < 0 || w < wmin) wmin = w;
        if (wmax < 0 || w > wmax) wmax = w;
    }
    if (wmax > 1.5 * wmin) return false;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        double g = static_cast<double>(segs[i + 1].a - segs[i].b);
        if (g < 0.65 * gmin || g > 1.5 * gmin) return false;
    }
    // Quiet at the image edges: full-width projection needs it there,
    // which is also what keeps page-like images (text to the margins)
    // out. Same 4-module policy as the one-track guard.
    double ql = static_cast<double>(segs.front().a);
    double qr = static_cast<double>(W - segs.back().b);
    if (ql < 4.0 * gmin || qr < 4.0 * gmin) return false;
    // Per-bar full-column extents (row-independent, unlike the
    // flood-fill extents in the scan loop).
    std::vector<PharmaBar> boxes;
    try {
        boxes.reserve(segs.size());
    } catch (...) {
        return false;
    }
    for (const Seg& s : segs) {
        int top = -1, bot = -1;
        for (int y = 0; y < H; ++y) {
            const uint8_t* row =
                bin.bits.data() + static_cast<size_t>(y) * bin.stride;
            for (int x = s.a; x < s.b; ++x)
                if (row[x] == 0) {
                    if (top < 0) top = y;
                    bot = y;
                    break;
                }
        }
        if (top < 0) return false;  // hot column with no ink: impossible
        boxes.push_back(PharmaBar{s.b - s.a, top, bot});
    }
    std::string decimal;
    bool all_full = false;
    if (!try_pharma2(boxes, gmin, H, decimal, all_full)) return false;
    int t0 = -1, b0 = -1;
    for (const PharmaBar& b : boxes) {
        if (t0 < 0 || b.top < t0) t0 = b.top;
        if (b0 < 0 || b.bot > b0) b0 = b.bot;
    }
    hit.text = decimal;  // small string; allocation failure propagates to
    hit.x0 = segs.front().a;  // the caller's try/catch (refuses there)
    hit.x1 = segs.back().b;
    hit.top = t0;
    hit.bot = b0;
    hit.all_full = all_full;
    return true;
}

}  // namespace

bool native_linear_handled(Symbology s) noexcept {
    return s == Symbology::MSI || s == Symbology::Plessey ||
           s == Symbology::Telepen || s == Symbology::Pharmacode ||
           s == Symbology::DeutschePost;
}

DecodeStatus decode_native_linear(const ImageView& img, const Options& opt,
                                  std::vector<Result>& out) noexcept {
    try {
        out.clear();
    } catch (...) {
        return DecodeStatus::BackendError;
    }
    if (!img.valid()) return DecodeStatus::InvalidImage;
    if (opt.max_symbols < 1) return DecodeStatus::InvalidArgument;

    Symbology order[5] = {Symbology::MSI, Symbology::Plessey,
                          Symbology::Telepen, Symbology::Pharmacode,
                          Symbology::DeutschePost};
    bool any_enabled = false;
    for (Symbology s : order)
        if (opt.enabled_symbologies & symbology_bit(s)) any_enabled = true;
    if (!any_enabled) return DecodeStatus::UnsupportedSymbology;

    DecodeStatus st = DecodeStatus::NoBarcodeFound;
    // Binarizer passes: Otsu always; Sauvola retry when try_harder.
    for (int pass = 0; pass < (opt.try_harder ? 2 : 1); ++pass) {
        BinaryImage bin;
        BinarizerParams bp;
        bp.method = (pass == 0) ? BinarizerMethod::Otsu : BinarizerMethod::Sauvola;
        if (binarize(img, bp, bin) != DecodeStatus::Ok) {
            st = DecodeStatus::BackendError;
            continue;
        }
        std::vector<ScanLine> scans;
        plan_scans(img, opt.try_harder, scans);
        if (scans.empty()) continue;
        // Two-track projection, ONCE per pass: it is row-independent
        // (the same bar columns are found from every scan line), so
        // per-line repetition would only re-prove the same result at
        // O(W*H) each. Runs before the lines; the emission decision is
        // after them, once the one-track candidates are all stashed.
        Pharma2Span ph2;
        bool two_match = false;
        if ((opt.enabled_symbologies &
             symbology_bit(Symbology::Pharmacode)) != 0) {
            try {
                two_match = project_pharma2(bin, ph2);
            } catch (...) {
                two_match = false;
            }
        }
        std::vector<PharmaStash> stashes;
        for (const ScanLine& sc : scans) {
            int line_len = sc.horizontal ? bin.width : bin.height;
            std::vector<int> runs;
            int rx0 = 0, rx1 = 0;
            if (!extract_runs(bin, sc.fixed, 2, line_len - 2, sc.horizontal,
                              2, runs, rx0, rx1))
                continue;
            // Quiet margins in modules, for the Pharmacode guard below.
            // extract_runs already trimmed the leading/trailing light runs;
            // rx0/rx1 span first bar -> last bar, so the leftovers are quiet.
            int quiet_l = rx0 - 2;
            int quiet_r = (line_len - 2) - rx1;
            int space_min = -1;
            for (size_t i = 1; i < runs.size(); i += 2) {
                if (space_min < 0 || static_cast<int>(runs[i]) < space_min)
                    space_min = static_cast<int>(runs[i]);
            }
            // One-track Pharmacode hits stash into the pass-scoped vector
            // (see below); everything else emits inline through the
            // shared tail.
            for (Symbology s : order) {
                if (!(opt.enabled_symbologies & symbology_bit(s))) continue;
                TryFn fn = try_fn(s);
                if (!fn) continue;
                for (int dir = 0; dir < 2; ++dir) {
                    if (dir == 1 && !wants_reverse(s)) break;
                    std::vector<int> rr = runs;
                    if (dir == 1) {
                        try {
                            std::reverse(rr.begin(), rr.end());
                        } catch (...) {
                            continue;
                        }
                    }
                    Hit hit;
                    bool ok = false;
                    try {
                        ok = fn(rr, opt, hit);
                    } catch (...) {
                        ok = false;
                    }
                    if (!ok) continue;
                    // ---- Pharmacode one-track guards + stash ----
                    //
                    // Pharmacode is the ONLY native linear format with no
                    // check character: any run pattern with uniform
                    // separators and bars in the narrow/wide ratio bands
                    // decodes to some value in 3..131070. try_harder raises
                    // the scan count 1 -> 16, so on random noise each line is
                    // a lottery ticket. Every other native format has a check
                    // character and showed zero false positives on the same
                    // sweep, so this is specific to the missing check digit.
                    //
                    // A/B on 800 pure-random images, default settings never
                    // produce a symbol either way; at try_harder:
                    //
                    //     guards off        747/800 images, 1178 symbols
                    //     quiet zone only    22/800 images,   22 symbols
                    //     both guards         0/800 images,    0 symbols
                    //
                    // try_harder is an amplifier, not a precondition: with
                    // the guards compiled out an 8px checkerboard decodes as
                    // Pharmacode '32767' on the single centre scan line at
                    // DEFAULT settings. Both settings are therefore covered
                    // by tests/test_despeckle.cpp.
                    //
                    // Two independent guards, because neither alone is
                    // sufficient:
                    //
                    // 1. Quiet zone >= 4 modules on BOTH sides. Noise hits
                    //    maxed at 2 modules; genuine symbols measured 9-10
                    //    (generated) with 6 the minimum seen. Cuts 747 -> 22
                    //    on its own.
                    // 2. EVERY bar >= 12 modules tall (measured along the
                    //    perpendicular axis, per bar, not across the whole
                    //    symbol). Genuine symbols read 21.3-32.0 across
                    //    scales 2-4, all four corpus variants, both 90
                    //    orientations and padded small-in-frame; the noise
                    //    that survived guard 1 read 1.0 and 3.0. Per-bar is
                    //    strictly stronger than the whole-symbol envelope
                    //    (same survivors read 14-19 there) because the
                    //    envelope is bounded below by the tallest bar while
                    //    noise blobs are one or two bars of different
                    //    heights. 12 also sits under the reference bar
                    //    height (published dimensions: 0.31" tall over a
                    //    0.02" thin bar = 15.5 modules).
                    //
                    // Neither is a substitute for a check character. Across
                    // five noise families (5200 images x both settings) the
                    // same two guards give 0 false positives, and every
                    // genuine pharmacode in the corpus still decodes at both
                    // settings, rotated and padded.
                    //
                    // A passing one-track hit is STASHED here, not emitted:
                    // the two-track projection after the symbology loop may
                    // replace it (suppression of tall-mixed misreads) or
                    // confirm it (all-full tiebreak). See the emission
                    // decision below.
                    if (hit.sym == Symbology::Pharmacode) {
                        if (space_min <= 0) continue;
                        int qmin = std::min(quiet_l, quiet_r);
                        if (qmin < kPharmacodeQuietModules * space_min)
                            continue;
                        long min_bar = -1;
                        if (sc.horizontal) {
                            int x = rx0;
                            for (size_t i = 0; i < runs.size(); ++i) {
                                int w = static_cast<int>(runs[i]);
                                if ((i % 2) == 0) {
                                    int ta = 0, tb = 0;
                                    vertical_extent(bin, x, x + w, sc.fixed,
                                                    ta, tb);
                                    long h = static_cast<long>(tb) - ta + 1L;
                                    if (min_bar < 0 || h < min_bar)
                                        min_bar = h;
                                }
                                x += w;
                            }
                        } else {
                            int y = rx0;
                            for (size_t i = 0; i < runs.size(); ++i) {
                                int w = static_cast<int>(runs[i]);
                                if ((i % 2) == 0) {
                                    int la = 0, rb = 0;
                                    horizontal_extent(bin, y, y + w, sc.fixed,
                                                      la, rb);
                                    long h = static_cast<long>(rb) - la + 1L;
                                    if (min_bar < 0 || h < min_bar)
                                        min_bar = h;
                                }
                                y += w;
                            }
                        }
                        if (min_bar < 0 ||
                            min_bar < kPharmacodeBarModules *
                                         static_cast<long>(space_min))
                            continue;
                        try {
                            stashes.push_back(PharmaStash{
                                hit, rx0, rx1, sc.fixed, sc.horizontal});
                        } catch (...) {
                            return DecodeStatus::BackendError;
                        }
                        continue;
                    }
                    // Dedupe + emit through the shared tail.
                    Quad quad = sc.horizontal
                                    ? row_quad(bin, rx0, rx1, sc.fixed)
                                    : col_quad(bin, rx0, rx1, sc.fixed);
                    switch (emit_hit(out, opt, hit, quad)) {
                        case Emit::Error:
                            return DecodeStatus::BackendError;
                        case Emit::Stop:
                            return DecodeStatus::Ok;
                        case Emit::Emitted:
                            st = DecodeStatus::Ok;
                            break;
                        case Emit::Duplicate:
                            break;
                    }
                }
            }
            // (Two-track projection ran once per pass, above; the emission
            // decision is below, after the loop.)
            // Default plans centre row THEN centre column. Once the row has
            // produced a hit, stop: the column is the 90-degree retry for
            // symbols the row could not see, not an additional source of
            // symbols for images that already decoded. Without this the
            // column would run on every upright barcode and could append a
            // second, spurious result. try_harder plans a single list that
            // already mixes both orientations, and keeps its existing
            // behaviour of scanning every line in it.
            // A stashed one-track candidate stops the column retry just
            // like an emitted hit: the decision below will emit it (or
            // its two-track replacement). Without this the column would
            // run pointlessly on every pharma hit at default.
            if (!opt.try_harder && (!out.empty() || !stashes.empty())) break;
        }
        // ---- Two-track emission decision (once per pass) ----
        //
        // The projection above is row-independent (same bars from every
        // line), so deciding once per pass keeps the outcome independent
        // of line order — no cross-line doubles, no line-race between
        // the two tracks. Tiebreak table:
        // - two-track match, NOT (all-full AND a guarded one-track
        //   candidate stashed) -> emit two-track. Covers mixed patterns
        //   (suppressing tall-mixed one-track misreads), short all-full
        //   (one-track guards failed), and all-half. Suppression is
        //   global to the pass, so a tall-mixed pattern cannot emit its
        //   bogus one-track value from one line and its two-track value
        //   from another.
        // - two-track match, all-full, stash non-empty -> tall all-full
        //   is ratio-identical to one-track all-narrow (two-track 12 vs
        //   one-track 3): one-track owns it, existing reads must not
        //   move. Emit the stashes (deduped; genuine ones agree).
        // - no two-track match -> emit the stashes (plain one-track
        //   behavior, guards already applied, damage multi-reads
        //   preserved exactly as before).
        if (two_match && !(ph2.all_full && !stashes.empty())) {
            Hit h2;
            h2.sym = Symbology::Pharmacode;
            h2.aux = "2";  // two-track; one-track marks "1" in try_pharmacode
            h2.aux_key = "tracks";
            try {
                h2.text = ph2.text;
            } catch (...) {
                return DecodeStatus::BackendError;
            }
            Quad q2(Point{(float)ph2.x0, (float)ph2.top},
                    Point{(float)ph2.x1, (float)ph2.top},
                    Point{(float)ph2.x1, (float)(ph2.bot + 1)},
                    Point{(float)ph2.x0, (float)(ph2.bot + 1)});
            switch (emit_hit(out, opt, h2, q2)) {
                case Emit::Error:
                    return DecodeStatus::BackendError;
                case Emit::Stop:
                    return DecodeStatus::Ok;
                case Emit::Emitted:
                    st = DecodeStatus::Ok;
                    break;
                case Emit::Duplicate:
                    break;
            }
        } else {
            for (const PharmaStash& ps : stashes) {
                Quad quad = ps.horizontal
                                ? row_quad(bin, ps.rx0, ps.rx1, ps.fixed)
                                : col_quad(bin, ps.rx0, ps.rx1, ps.fixed);
                switch (emit_hit(out, opt, ps.hit, quad)) {
                    case Emit::Error:
                        return DecodeStatus::BackendError;
                    case Emit::Stop:
                        return DecodeStatus::Ok;
                    case Emit::Emitted:
                        st = DecodeStatus::Ok;
                        break;
                    case Emit::Duplicate:
                        break;
                }
            }
        }
        if (!out.empty()) return DecodeStatus::Ok;
    }
    return out.empty() ? st : DecodeStatus::Ok;
}

}  // namespace linear
}  // namespace omniscan
