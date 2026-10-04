// Decode dispatcher: validates input, routes to backends, caps max_symbols.
// No exceptions escape; no global mutable state (registry is immutable).
#include "omniscan/decode.h"
#include "omniscan/binarizer.h"
#include "omniscan/payload.h"
#include "omniscan/version.h"
#include "../backend_zxing/zxing_adapter.h"
#include "../formats/linear/native_linear.h"
#include "../formats/postal/native_postal.h"
#include "despeckle.h"
#include <algorithm>
#include <exception>

namespace omniscan {

namespace {

void fill_parsed(const Options& opt, std::vector<Result>& out) noexcept {
    if (!opt.return_parsed) return;
    bool want_swiss =
        (opt.enabled_symbologies & symbology_bit(Symbology::SwissQR)) != 0;
    try {
        for (auto& r : out) payload::sniff_into(r.text, r.parsed);
        // Swiss QR bills relabel from QRCode, but only when the caller
        // asked for SwissQR (otherwise the mask contract would leak).
        if (want_swiss) {
            for (auto& r : out) {
                auto it = r.parsed.find("payload");
                if (it != r.parsed.end() && it->second == "swissqr")
                    r.symbology = Symbology::SwissQR;
            }
        }
        // Enabling SwissQR alone implies the QR backend; drop non-bill QRs
        // so the symbology filter contract holds.
        bool want_qr =
            (opt.enabled_symbologies & symbology_bit(Symbology::QRCode)) != 0;
        if (want_swiss && !want_qr) {
            std::vector<Result> kept;
            for (auto& r : out)
                if (r.symbology == Symbology::SwissQR) kept.push_back(r);
            out.swap(kept);
        }
    } catch (...) {
    }
}

// Native backend: M3 linear Tier-2 + M4a postal codecs. More native formats
// register here in later milestones.
struct NativeBackend final : backend_zxing::Backend {
    const char* name() const noexcept override { return "native"; }
    const char* version() const noexcept override { return OMNISCAN_VERSION_STRING; }
    bool available() const noexcept override { return true; }
    SymMask handled() const noexcept override {
        return symbology_bit(Symbology::MSI) |
               symbology_bit(Symbology::Plessey) |
               symbology_bit(Symbology::Telepen) |
               symbology_bit(Symbology::Pharmacode) |
               symbology_bit(Symbology::DeutschePost) |
               symbology_bit(Symbology::RM4SCC) |
               symbology_bit(Symbology::JapanPost) |
               symbology_bit(Symbology::AustraliaPost) |
               symbology_bit(Symbology::USPSIMb) |
               symbology_bit(Symbology::KIX);
    }
    DecodeStatus decode(const ImageView& img, const Options& opt,
                        std::vector<Result>& out) const noexcept override {
        Options sub = opt;
        sub.enabled_symbologies &= handled();
        if (sub.enabled_symbologies == 0)
            return DecodeStatus::UnsupportedSymbology;
        // One linear+postal pass over `v`. `out` is only touched on the
        // error/empty paths so a retry can safely call this again.
        auto pass = [&](const ImageView& v) -> DecodeStatus {
            std::vector<Result> tmp;
            DecodeStatus a = DecodeStatus::NoBarcodeFound;
            DecodeStatus b = DecodeStatus::NoBarcodeFound;
            try {
                a = linear::decode_native_linear(v, sub, tmp);
            } catch (...) {
                a = DecodeStatus::BackendError;
            }
            try {
                std::vector<Result> tmp2;
                b = postal::decode_native_postal(v, sub, tmp2);
                for (auto& r : tmp2) tmp.push_back(std::move(r));
            } catch (...) {
                b = DecodeStatus::BackendError;
            }
            try {
                out.clear();
                for (auto& r : tmp) {
                    if (static_cast<int>(out.size()) >= sub.max_symbols) break;
                    out.push_back(std::move(r));
                }
            } catch (...) {
                return DecodeStatus::BackendError;
            }
            if (!out.empty()) return DecodeStatus::Ok;
            // Sub-dispatchers report UnsupportedSymbology for bits outside
            // their own set; combined, "nothing found" dominates.
            if (a == DecodeStatus::BackendError || b == DecodeStatus::BackendError)
                return DecodeStatus::BackendError;
            return DecodeStatus::NoBarcodeFound;
        };

        DecodeStatus st = pass(img);
        if (st != DecodeStatus::NoBarcodeFound || !opt.try_harder) {
            if (st == DecodeStatus::Ok) fill_parsed(opt, out);
            return st;
        }

        // try_harder despeckle retry. Salt-and-pepper noise corrupts *every*
        // scan line (one flipped pixel destroys that line's run quantization),
        // so more scan lines -- the existing try_harder lever -- cannot help;
        // removing the impulses does. Measured 14/50 -> 49/50 on the native
        // noise variants with no regressions and no new false positives, but
        // it costs a full extra pass, so it runs only when the raw pass
        // already came up empty. Default settings are byte-identical.
        Image clean = despeckle::median3(img);
        if (clean.empty()) return st;  // too small to filter: keep the miss
        DecodeStatus st2 = pass(clean.view());
        if (st2 == DecodeStatus::Ok) {
            fill_parsed(opt, out);
            return st2;
        }
        out.clear();
        if (st2 == DecodeStatus::BackendError) return st2;
        return st;
    }
};

const backend_zxing::Backend& native_backend() noexcept {
    static const NativeBackend b{};
    return b;
}

bool view_sane(const ImageView& v) noexcept {
    if (!v.valid()) return false;
    if (v.width > 16384 || v.height > 16384) return false;
    return true;
}

} // namespace

bool backend_available(const char* name) noexcept {
    if (!name) return false;
    char n[16]; size_t i = 0;
    for (; name[i] && i + 1 < sizeof n; ++i) {
        char c = name[i];
        n[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    }
    n[i] = '\0';
    auto eq = [](const char* a, const char* b) {
        size_t k = 0;
        while (a[k] && b[k] && a[k] == b[k]) ++k;
        return a[k] == b[k];
    };
    if (eq(n, "native")) return native_backend().available();
    if (eq(n, "zxing")) return backend_zxing::instance().available();
    return false;
}

const char* backend_version(const char* name) noexcept {
    if (!name) return "";
    char n[16]; size_t i = 0;
    for (; name[i] && i + 1 < sizeof n; ++i) {
        char c = name[i];
        n[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    }
    n[i] = '\0';
    auto eq = [](const char* a, const char* b) {
        size_t k = 0;
        while (a[k] && b[k] && a[k] == b[k]) ++k;
        return a[k] == b[k];
    };
    if (eq(n, "native")) return native_backend().version();
    if (eq(n, "zxing")) return backend_zxing::instance().version();
    return "";
}

std::vector<Result> decode(const ImageView& img, const Options& opt) {
    std::vector<Result> out;
    (void)decode_into(img, opt, out); // status detail available via decode_into
    return out;
}

DecodeStatus decode_into(const ImageView& img, const Options& opt,
                         std::vector<Result>& out) noexcept {
    try {
        out.clear();
    } catch (...) {
        return DecodeStatus::BackendError;
    }
    if (!view_sane(img)) return DecodeStatus::InvalidImage;
    if (opt.max_symbols < 1) return DecodeStatus::InvalidArgument;
    if (opt.enabled_symbologies == 0) return DecodeStatus::UnsupportedSymbology;

    const backend_zxing::Backend* backends[2] = {&backend_zxing::instance(), &native_backend()};
    SymMask remaining = opt.enabled_symbologies;
    // SwissQR is a QR payload schema: querying it implies the QR backend.
    if (remaining & symbology_bit(Symbology::SwissQR))
        remaining |= symbology_bit(Symbology::QRCode);
    bool any_backend = false;
    DecodeStatus worst = DecodeStatus::NoBarcodeFound;

    for (const backend_zxing::Backend* b : backends) {
        if (!b || !b->available()) continue;
        SymMask wants = remaining & b->handled();
        if (!wants) continue;
        any_backend = true;
        Options sub = opt;
        sub.enabled_symbologies = wants;
        std::vector<Result> tmp;
        DecodeStatus st;
        try {
            st = b->decode(img, sub, tmp);
        } catch (...) {
            st = DecodeStatus::BackendError;
        }
        if (st == DecodeStatus::Ok && !tmp.empty()) {
            try {
                for (auto& r : tmp) {
                    if (static_cast<int>(out.size()) >= opt.max_symbols) break;
                    out.push_back(std::move(r));
                }
            } catch (...) {
                return DecodeStatus::BackendError;
            }
            if (static_cast<int>(out.size()) >= opt.max_symbols) {
                try { out.resize(opt.max_symbols); } catch (...) {}
                fill_parsed(opt, out);
                return DecodeStatus::Ok;
            }
            worst = DecodeStatus::Ok;
        } else if (st == DecodeStatus::Ok) {
            if (worst == DecodeStatus::NoBarcodeFound) worst = DecodeStatus::Ok;
            // backend succeeded with zero symbols: keep scanning others
            if (worst == DecodeStatus::NoBarcodeFound) worst = DecodeStatus::Ok;
        } else if (st != DecodeStatus::NoBarcodeFound) {
            worst = st; // surface real errors
        }
        remaining &= ~b->handled();
        if (remaining == 0) break;
    }

    if (!out.empty()) {
        fill_parsed(opt, out);
        return DecodeStatus::Ok;
    }
    if (!any_backend) {
        // Requested set needs a backend that is compiled out.
        // (Native handles 0 at M1; zxing handles Grade-1 when ON.)
        return DecodeStatus::BackendNotAvailable;
    }
    if (worst == DecodeStatus::Ok) return DecodeStatus::NoBarcodeFound;
    return worst;
}

} // namespace omniscan
