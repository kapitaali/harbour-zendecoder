// Native postal dispatcher (M4a): RM4SCC + KIX. Binarizes once (Otsu;
// Sauvola retry when try_harder), scans rows, extracts uniformly-pitched
// 4-state bars, tries forward + 180-degree-rotated readings.
#include "native_postal.h"
#include "auspost.h"
#include "fourstate.h"
#include "imb.h"
#include "japan_post.h"

namespace omniscan {
namespace postal {
namespace {

struct Hit {
    Symbology sym = Symbology::Unknown;
    std::string text;
    Quad quad{};
};

}  // namespace

// Band detection: rows rich in ink runs (barcode bands) come first.
// 4-state bars only coexist on middle rows, so fixed scan rows can miss
// top/bottom-placed codes in real photos. Deterministic.
void find_bands(const BinaryImage& bin, std::vector<int>& rows) {
    rows.clear();
    if (bin.empty()) return;
    try {
        int H = bin.height, W = bin.width;
        std::vector<char> hot(H, 0);
        for (int y = 0; y < H; ++y) {
            const uint8_t* row = bin.bits.data() + y * bin.stride;
            int runs = 0;
            bool ink = false;
            for (int x = 0; x < W; ++x) {
                bool b = (row[x] == 0);
                if (b && !ink) ++runs;
                ink = b;
            }
            hot[y] = (runs >= 10);
        }
        // Contiguous hot segments of height >= 8 (classify needs thirds).
        for (int y = 0; y < H;) {
            if (!hot[y]) {
                ++y;
                continue;
            }
            int y0 = y;
            while (y < H && hot[y]) ++y;
            if (y - y0 >= 8) rows.push_back((y0 + y) / 2);
        }
    } catch (...) {
        rows.clear();
    }
}

// Location quad from scan-axis extent [rx0,rx1) and orthogonal band
// [top,bot]. swapped = transposed retry for 90-degree-rotated codes:
// axes are exchanged back to original image space.

// Transpose a binarized image in place of binarizing a transposed
// grayscale one. This is EXACTLY equivalent -- Otsu's threshold comes from
// the grayscale histogram, which transposing cannot change, and the
// per-pixel decision is axis-symmetric; Sauvola's window clamps per axis,
// so it commutes with a transpose too -- but it costs one byte pass
// instead of a grayscale copy plus a histogram pass plus a threshold
// pass. The distinction matters because this runs on the DEFAULT miss
// path for every image that is not a postal code, so it is paid far more
// often than it is useful.
//
// Returns false (leaving `out` empty) on allocation failure, which the
// caller treats as "no transposed retry" rather than an error.
bool transpose_bin(const BinaryImage& in, BinaryImage& out) {
    out = BinaryImage{};
    if (in.empty() || in.width <= 0 || in.height <= 0) return false;
    out.width = in.height;
    out.height = in.width;
    out.stride = in.height;
    try {
        out.bits.assign(static_cast<size_t>(out.stride) * out.height, 1);
    } catch (...) {
        out = BinaryImage{};
        return false;
    }
    for (int y = 0; y < in.height; ++y) {
        const uint8_t* src =
            in.bits.data() + static_cast<size_t>(y) * in.stride;
        uint8_t* dst = out.bits.data() + static_cast<size_t>(y);
        for (int x = 0; x < in.width; ++x)
            dst[static_cast<size_t>(x) * out.stride] = src[x];
    }
    return true;
}

Quad make_quad(int rx0, int rx1, int top, int bot, bool swapped) noexcept {
    float x0 = (float)rx0, x1 = (float)rx1;
    float y0 = (float)top, y1 = (float)(bot + 1);
    if (!swapped)
        return Quad(Point{x0, y0}, Point{x1, y0}, Point{x1, y1}, Point{x0, y1});
    return Quad(Point{y0, x0}, Point{y1, x0}, Point{y1, x1}, Point{y0, x1});
}

bool native_postal_handled(Symbology s) noexcept {
    return s == Symbology::RM4SCC || s == Symbology::KIX ||
           s == Symbology::JapanPost || s == Symbology::AustraliaPost ||
           s == Symbology::USPSIMb;
}

DecodeStatus decode_native_postal(const ImageView& img, const Options& opt,
                                  std::vector<Result>& out) noexcept {
    try {
        out.clear();
    } catch (...) {
        return DecodeStatus::BackendError;
    }
    if (!img.valid()) return DecodeStatus::InvalidImage;
    if (opt.max_symbols < 1) return DecodeStatus::InvalidArgument;
    bool want_rm = (opt.enabled_symbologies & symbology_bit(Symbology::RM4SCC)) != 0;
    bool want_kix = (opt.enabled_symbologies & symbology_bit(Symbology::KIX)) != 0;
    bool want_jp =
        (opt.enabled_symbologies & symbology_bit(Symbology::JapanPost)) != 0;
    bool want_au =
        (opt.enabled_symbologies & symbology_bit(Symbology::AustraliaPost)) !=
        0;
    bool want_imb =
        (opt.enabled_symbologies & symbology_bit(Symbology::USPSIMb)) != 0;
    if (!want_rm && !want_kix && !want_jp && !want_au && !want_imb)
        return DecodeStatus::UnsupportedSymbology;

    DecodeStatus st = DecodeStatus::NoBarcodeFound;
    // One orientation pass over a binarized image. swapped = transposed
    // retry for 90-degree-rotated codes. Returns Ok (stop), BackendError
    // (stop), or NoBarcodeFound (keep going).
    auto scan_bin = [&](const BinaryImage& bin, bool swapped) -> DecodeStatus {
        // Scan rows: content bands first (all modes), eighths supplement
        // when try_harder (bands can miss severely broken prints).
        std::vector<int> rows;
        try {
            find_bands(bin, rows);
        } catch (...) {
            rows.clear();
        }
        if (opt.try_harder) {
            for (int k = 1; k <= 7; ++k) {
                int y = (bin.height * k) / 8;
                bool dup = false;
                for (int r : rows)
                    if (r >= y - 2 && r <= y + 2) dup = true;
                if (!dup) {
                    try {
                        rows.push_back(y);
                    } catch (...) {
                    }
                }
            }
        } else if (rows.empty()) {
            try {
                rows.push_back(bin.height / 2);
            } catch (...) {
            }
        }
        for (size_t ri = 0; ri < rows.size(); ++ri) {
            int y = rows[ri];
            std::vector<PBar> bars;
            double ql = 0, qr = 0;
            if (!extract_postal(bin, y, 6, 0.35, bars, ql, qr)) continue;
            if (ql < 1.5 || qr < 1.5) continue;  // quiet zones required
            std::vector<Bar4> states;
            int top = 0, bot = 0;
            if (!classify_postal(bin, bars, y, states, top, bot)) continue;
            std::vector<Bar4> rot = rotate180(states);
            // First-valid wins per symbology (forward preferred): a rotated
            // misreading validates by chance only ~1/36 (RM4SCC check).
            if (want_rm) {
                std::string text;
                char check = '?';
                bool ok = false;
                try {
                    ok = parse_rm4scc(states, text, check);
                    if (!ok) ok = parse_rm4scc(rot, text, check);
                } catch (...) {
                    ok = false;
                }
                if (ok) {
                    bool dup = false;
                    for (const Result& r : out)
                        if (r.symbology == Symbology::RM4SCC &&
                            r.text == text) {
                            dup = true;
                            break;
                        }
                    if (!dup) {
                        Result res;
                        res.symbology = Symbology::RM4SCC;
                        res.text = text;
                        res.confidence = 1.0f;
                        if (opt.return_parsed) {
                            res.parsed["check"] = std::string(1, check);
                        }
                        res.location = make_quad(bars.front().x0, bars.back().x1,
                                                   top, bot, swapped);
                        try {
                            out.push_back(std::move(res));
                        } catch (...) {
                            return DecodeStatus::BackendError;
                        }
                        st = DecodeStatus::Ok;
                        if (static_cast<int>(out.size()) >= opt.max_symbols)
                            return DecodeStatus::Ok;
                    }
                }
            }
            if (want_kix) {
                std::string text;
                bool ok = false;
                try {
                    ok = parse_kix(states, text);
                    if (!ok) ok = parse_kix(rot, text);
                } catch (...) {
                    ok = false;
                }
                if (ok) {
                    bool dup = false;
                    for (const Result& r : out)
                        if (r.symbology == Symbology::KIX && r.text == text) {
                            dup = true;
                            break;
                        }
                    if (!dup) {
                        Result res;
                        res.symbology = Symbology::KIX;
                        res.text = text;
                        res.confidence = 1.0f;
                        res.location = make_quad(bars.front().x0, bars.back().x1,
                                                   top, bot, swapped);
                        try {
                            out.push_back(std::move(res));
                        } catch (...) {
                            return DecodeStatus::BackendError;
                        }
                        st = DecodeStatus::Ok;
                        if (static_cast<int>(out.size()) >= opt.max_symbols)
                            return DecodeStatus::Ok;
                    }
                }
            }
            if (want_jp) {
                std::string text;
                bool ok = false;
                try {
                    ok = parse_japanpost(states, text);
                    if (!ok) ok = parse_japanpost(rot, text);
                } catch (...) {
                    ok = false;
                }
                if (ok) {
                    bool dup = false;
                    for (const Result& r : out)
                        if (r.symbology == Symbology::JapanPost &&
                            r.text == text) {
                            dup = true;
                            break;
                        }
                    if (!dup) {
                        Result res;
                        res.symbology = Symbology::JapanPost;
                        res.text = text;
                        res.confidence = 1.0f;
                        res.location = make_quad(bars.front().x0, bars.back().x1,
                                                   top, bot, swapped);
                        try {
                            out.push_back(std::move(res));
                        } catch (...) {
                            return DecodeStatus::BackendError;
                        }
                        st = DecodeStatus::Ok;
                        if (static_cast<int>(out.size()) >= opt.max_symbols)
                            return DecodeStatus::Ok;
                    }
                }
            }
            if (want_au) {
                std::string text, fcc, kind;
                bool ok = false;
                try {
                    ok = parse_auspost(states, text, fcc, kind);
                    if (!ok) ok = parse_auspost(rot, text, fcc, kind);
                } catch (...) {
                    ok = false;
                }
                if (ok) {
                    bool dup = false;
                    for (const Result& r : out)
                        if (r.symbology == Symbology::AustraliaPost &&
                            r.text == text) {
                            dup = true;
                            break;
                        }
                    if (!dup) {
                        Result res;
                        res.symbology = Symbology::AustraliaPost;
                        res.text = text;
                        res.confidence = 1.0f;
                        if (opt.return_parsed) {
                            res.parsed["fcc"] = fcc;
                            res.parsed["kind"] = kind;
                        }
                        res.location = make_quad(bars.front().x0, bars.back().x1,
                                                   top, bot, swapped);
                        try {
                            out.push_back(std::move(res));
                        } catch (...) {
                            return DecodeStatus::BackendError;
                        }
                        st = DecodeStatus::Ok;
                        if (static_cast<int>(out.size()) >= opt.max_symbols)
                            return DecodeStatus::Ok;
                    }
                }
            }
            if (want_imb) {
                std::string text;
                bool ok = false;
                try {
                    ok = parse_imb(states, text);
                    if (!ok) ok = parse_imb(rot, text);
                } catch (...) {
                    ok = false;
                }
                if (ok) {
                    bool dup = false;
                    for (const Result& r : out)
                        if (r.symbology == Symbology::USPSIMb &&
                            r.text == text) {
                            dup = true;
                            break;
                        }
                    if (!dup) {
                        Result res;
                        res.symbology = Symbology::USPSIMb;
                        res.text = text;
                        res.confidence = 1.0f;
                        res.location = make_quad(bars.front().x0, bars.back().x1,
                                                   top, bot, swapped);
                        try {
                            out.push_back(std::move(res));
                        } catch (...) {
                            return DecodeStatus::BackendError;
                        }
                        st = DecodeStatus::Ok;
                        if (static_cast<int>(out.size()) >= opt.max_symbols)
                            return DecodeStatus::Ok;
                    }
                }
            }
        }
        if (!out.empty()) return DecodeStatus::Ok;
        return DecodeStatus::NoBarcodeFound;
    };
    for (int pass = 0; pass < (opt.try_harder ? 2 : 1); ++pass) {
        BinaryImage bin;
        BinarizerParams bp;
        bp.method = (pass == 0) ? BinarizerMethod::Otsu : BinarizerMethod::Sauvola;
        if (binarize(img, bp, bin) != DecodeStatus::Ok) {
            st = DecodeStatus::BackendError;
            continue;
        }
        DecodeStatus r = scan_bin(bin, false);
        if (r != DecodeStatus::NoBarcodeFound) return r;
        // Transposed retry for 90-degree-rotated codes. Reached only when
        // the row scan found nothing, so images that already decode are
        // unaffected -- the same retry-on-empty shape as the despeckle
        // pass, and the same one try_harder has always used here. It used
        // to be gated on try_harder, which meant a rotated postal symbol
        // was invisible at default settings; orientation is not something
        // a caller can know about their image, so it cannot be opt-in.
        //
        // Do NOT "optimise" this by skipping the transpose when find_bands
        // returned no bands above -- a rotated symbol is precisely the
        // case where it returns none. Rotating turns the vertical bars
        // into horizontal stripes, so every row crosses exactly one of
        // them and never the >=10 ink runs a row needs to be "hot". An
        // empty band list is the NORMAL state of the symbol we are looking
        // for, not evidence that the image is empty. That is also why the
        // default path above falls back to the centre row: it always has
        // to try, and it always fails, and only then is this worth doing.
        //
        // Transposing the binarized image (rather than the grayscale one,
        // which is what this did originally) is what keeps that cost
        // payable on every miss: one byte pass instead of a copy plus a
        // histogram plus a threshold. Measured on the 401-image corpus,
        // the barcode-free row went 0.136 ms -> 0.225 ms when this was a
        // re-binarize and 0.136 ms -> 0.162 ms as a transpose.
        BinaryImage tbin;
        if (transpose_bin(bin, tbin)) {
            DecodeStatus r2 = scan_bin(tbin, true);
            if (r2 != DecodeStatus::NoBarcodeFound) return r2;
        }
    }
    return out.empty() ? st : DecodeStatus::Ok;
}

}  // namespace postal
}  // namespace omniscan
