// Native MaxiCode dispatcher: binarize (Otsu, Sauvola retry under
// try_harder -- mirrors the linear/postal/stacked dispatchers) and hand
// the binary image to the T4 hex-grid pipeline. See
// docs/formats/maxicode.md §Image pipeline.
#include "native_maxicode.h"

#include "maxicode_image.h"
#include "omniscan/binarizer.h"

namespace omniscan {
namespace maxicode_native {

bool native_maxicode_handled(Symbology s) noexcept {
    return s == Symbology::MaxiCode;
}

DecodeStatus decode_native_maxicode(const ImageView& img, const Options& opt,
                                    std::vector<Result>& out) noexcept {
    try {
        out.clear();
    } catch (...) {
        return DecodeStatus::BackendError;
    }
    if (!img.valid()) return DecodeStatus::InvalidImage;
    if (opt.max_symbols < 1) return DecodeStatus::InvalidArgument;
    if ((opt.enabled_symbologies & symbology_bit(Symbology::MaxiCode)) == 0)
        return DecodeStatus::UnsupportedSymbology;
    DecodeStatus st = DecodeStatus::NoBarcodeFound;
    for (int pass = 0; pass < (opt.try_harder ? 2 : 1); ++pass) {
        BinaryImage bin;
        BinarizerParams bp;
        bp.method =
            (pass == 0) ? BinarizerMethod::Otsu : BinarizerMethod::Sauvola;
        if (binarize(img, bp, bin) != DecodeStatus::Ok) {
            st = DecodeStatus::BackendError;
            continue;
        }
        if (bin.empty() || bin.width < 32 || bin.height < 32) continue;
        std::string text;
        bool found = false;
        try {
            // Located decode: frame as-is (identical to the old
            // decode_image call for full-bleed input), then 180-degree
            // rotation and quiet-zone-bbox retries. See
            // docs/formats/maxicode.md §Image pipeline.
            found = maxicode::decode_located(bin, text);
        } catch (...) {
            return DecodeStatus::BackendError;
        }
        if (!found) continue;
        try {
            Result res;
            res.symbology = Symbology::MaxiCode;
            res.text = text;
            res.confidence = 1.0f;
            // Location: the sampled grid's bounding box (the pipeline
            // treats the whole image as symbol, zint renders no border).
            res.location = Quad(Point{0.0f, 0.0f},
                                Point{(float)bin.width, 0.0f},
                                Point{(float)bin.width, (float)bin.height},
                                Point{0.0f, (float)bin.height});
            out.push_back(std::move(res));
        } catch (...) {
            return DecodeStatus::BackendError;
        }
        return DecodeStatus::Ok;
    }
    // Tilted-photo search (try_harder only): rotation/scale/position
    // search over the rotated grid with EC verification as arbiter.
    // Default path is untouched (measured bench-neutral); this stage
    // costs ~0.1-0.2s per photo worst case. See
    // docs/formats/maxicode.md §Image pipeline.
    if (opt.try_harder && out.empty()) {
        BinaryImage bin;
        BinarizerParams bp;  // Otsu default: deterministic photo input
        if (binarize(img, bp, bin) == DecodeStatus::Ok && !bin.empty() &&
            bin.width >= 32 && bin.height >= 32) {
            std::string text;
            bool found = false;
            try {
                found = maxicode::decode_photo(bin, text);
            } catch (...) {
                return DecodeStatus::BackendError;
            }
            if (found) {
                try {
                    Result res;
                    res.symbology = Symbology::MaxiCode;
                    res.text = text;
                    res.confidence = 1.0f;
                    res.location = Quad(
                        Point{0.0f, 0.0f}, Point{(float)bin.width, 0.0f},
                        Point{(float)bin.width, (float)bin.height},
                        Point{0.0f, (float)bin.height});
                    out.push_back(std::move(res));
                } catch (...) {
                    return DecodeStatus::BackendError;
                }
                return DecodeStatus::Ok;
            }
        }
    }
    return out.empty() ? st : DecodeStatus::Ok;
}

}  // namespace maxicode_native
}  // namespace omniscan
