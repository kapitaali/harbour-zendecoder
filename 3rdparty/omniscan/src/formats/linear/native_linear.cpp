// Native linear Tier-2 dispatcher (M3): MSI, Plessey, Telepen, Pharmacode.
// Binarizes once (Otsu; try_harder retries failures with Sauvola), scans
// lines in both directions (except Pharmacode: direction-ambiguous by design,
// forward only), dedupes, respects max_symbols.
#include "native_linear.h"
#include <algorithm>
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
                    // ---- Pharmacode false-positive guards ----
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
                                if ((i % 2) == 0) {  // runs start on a bar
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
                    }
                    // Dedupe.
                    bool dup = false;
                    for (const Result& r : out)
                        if (r.symbology == hit.sym && r.text == hit.text) {
                            dup = true;
                            break;
                        }
                    if (dup) continue;
                    // Location quad from ink extent + orthogonal extent.
                    Result res;
                    res.symbology = hit.sym;
                    res.text = hit.text;
                    res.confidence = 1.0f;
                    if (opt.return_parsed && !hit.aux.empty() &&
                        !hit.aux_key.empty())
                        res.parsed[hit.aux_key] = hit.aux;
                    try {
                        if (sc.horizontal) {
                            int top = 0, bot = 0;
                            vertical_extent(bin, rx0, rx1, sc.fixed, top, bot);
                            res.location = Quad(
                                Point{(float)rx0, (float)top},
                                Point{(float)rx1, (float)top},
                                Point{(float)rx1, (float)(bot + 1)},
                                Point{(float)rx0, (float)(bot + 1)});
                        } else {
                            int l = 0, r = 0;
                            horizontal_extent(bin, rx0, rx1, sc.fixed, l, r);
                            res.location = Quad(
                                Point{(float)l, (float)rx0},
                                Point{(float)(r + 1), (float)rx0},
                                Point{(float)(r + 1), (float)rx1},
                                Point{(float)l, (float)rx1});
                        }
                        out.push_back(std::move(res));
                    } catch (...) {
                        return DecodeStatus::BackendError;
                    }
                    st = DecodeStatus::Ok;
                    if (static_cast<int>(out.size()) >= opt.max_symbols)
                        return DecodeStatus::Ok;
                }
            }
            // Default plans centre row THEN centre column. Once the row has
            // produced a hit, stop: the column is the 90-degree retry for
            // symbols the row could not see, not an additional source of
            // symbols for images that already decoded. Without this the
            // column would run on every upright barcode and could append a
            // second, spurious result. try_harder plans a single list that
            // already mixes both orientations, and keeps its existing
            // behaviour of scanning every line in it.
            if (!opt.try_harder && !out.empty()) break;
        }
        if (!out.empty()) return DecodeStatus::Ok;
    }
    return out.empty() ? st : DecodeStatus::Ok;
}

}  // namespace linear
}  // namespace omniscan
