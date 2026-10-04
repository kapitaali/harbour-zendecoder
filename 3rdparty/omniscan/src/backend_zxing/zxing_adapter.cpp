// zxing-cpp adapter: real implementation when OMNISCAN_WITH_ZXING is ON,
// stub otherwise. Single translation unit keeps the OFF build dependency-free.
#include "zxing_adapter.h"

#if defined(OMNISCAN_WITH_ZXING)
// Real backend. zxing-cpp is Apache-2.0; kept as FetchContent dep, never vendored.
// Includes are flat in zxing-cpp v2.x (headers live in core/src/).
#include "BarcodeFormat.h"
#include "ReadBarcode.h"
#include "ReaderOptions.h"
#include "ImageView.h"
#include "Version.h"
#include <algorithm>
#include <cstring>

namespace omniscan {
namespace backend_zxing {
namespace {

using ZXing::BarcodeFormat;
using ZXing::BarcodeFormats;
using ZXing::ByteArray;
using ZXing::ImageFormat;
using ZXing::ReaderOptions;

ZXing::BarcodeFormat to_zx(Symbology s) {
    switch (s) {
        case Symbology::QRCode: return BarcodeFormat::QRCode;
        case Symbology::MicroQRCode: return BarcodeFormat::MicroQRCode;
        case Symbology::RmQR: return BarcodeFormat::RMQRCode;
        case Symbology::DataMatrix: return BarcodeFormat::DataMatrix;
        case Symbology::Aztec: return BarcodeFormat::Aztec;
        case Symbology::PDF417: return BarcodeFormat::PDF417;
        case Symbology::Code128: return BarcodeFormat::Code128;
        case Symbology::Code39: return BarcodeFormat::Code39;
        case Symbology::Code93: return BarcodeFormat::Code93;
        case Symbology::Codabar: return BarcodeFormat::Codabar;
        case Symbology::ITF: return BarcodeFormat::ITF;
        case Symbology::UPCA: return BarcodeFormat::UPCA;
        case Symbology::UPCE: return BarcodeFormat::UPCE;
        case Symbology::EAN8: return BarcodeFormat::EAN8;
        case Symbology::EAN13: return BarcodeFormat::EAN13;
        case Symbology::DataBar: return BarcodeFormat::DataBar;
        case Symbology::DataBarExpanded: return BarcodeFormat::DataBarExpanded;
        case Symbology::MaxiCode: return BarcodeFormat::MaxiCode;
        // v2.3 exposes no MicroPDF417 / DotCode / HanXin / postal formats:
        // those stay native-planned (M3+), never forced through here.
        default: return BarcodeFormat::None;
    }
}

Symbology from_zx(BarcodeFormat f) {
    switch (f) {
        case BarcodeFormat::QRCode: return Symbology::QRCode;
        case BarcodeFormat::MicroQRCode: return Symbology::MicroQRCode;
        case BarcodeFormat::RMQRCode: return Symbology::RmQR;
        case BarcodeFormat::DataMatrix: return Symbology::DataMatrix;
        case BarcodeFormat::Aztec: return Symbology::Aztec;
        case BarcodeFormat::PDF417: return Symbology::PDF417;
        case BarcodeFormat::Code128: return Symbology::Code128;
        case BarcodeFormat::Code39: return Symbology::Code39;
        case BarcodeFormat::Code93: return Symbology::Code93;
        case BarcodeFormat::Codabar: return Symbology::Codabar;
        case BarcodeFormat::ITF: return Symbology::ITF;
        case BarcodeFormat::UPCA: return Symbology::UPCA;
        case BarcodeFormat::UPCE: return Symbology::UPCE;
        case BarcodeFormat::EAN8: return Symbology::EAN8;
        case BarcodeFormat::EAN13: return Symbology::EAN13;
        case BarcodeFormat::DataBar: return Symbology::DataBar;
        case BarcodeFormat::DataBarExpanded: return Symbology::DataBarExpanded;
        case BarcodeFormat::MaxiCode: return Symbology::MaxiCode;
        default: return Symbology::Unknown;
    }
}

struct RealBackend final : Backend {
    const char* name() const noexcept override { return "zxing"; }
    const char* version() const noexcept override { return ZXING_VERSION_STR; }
    bool available() const noexcept override { return true; }
    SymMask handled() const noexcept override {
        SymMask m = 0;
        for (int i = 0; i < 64; ++i) {
            Symbology s = static_cast<Symbology>(i);
            if (to_zx(s) != BarcodeFormat::None) m |= symbology_bit(s);
        }
        return m;
    }
    DecodeStatus decode(const ImageView& img, const Options& opt,
                        std::vector<Result>& out) const noexcept override {
        if (!img.valid()) return DecodeStatus::InvalidImage;
        try {
            out.clear();
            BarcodeFormats formats = BarcodeFormat::None;
            for (int i = 0; i < 64; ++i) {
                Symbology s = static_cast<Symbology>(i);
                if ((opt.enabled_symbologies & symbology_bit(s)) && to_zx(s) != BarcodeFormat::None)
                    formats |= to_zx(s);
            }
            if (formats.empty()) return DecodeStatus::UnsupportedSymbology;
            ReaderOptions hints;
            hints.setFormats(formats);
            hints.setTryHarder(opt.try_harder);
            int maxn = opt.max_symbols < 1 ? 1 : opt.max_symbols;
            if (maxn > 255) maxn = 255;
            hints.setMaxNumberOfSymbols(static_cast<uint8_t>(maxn));
            ZXing::ImageView zimg(img.data, img.width, img.height, ImageFormat::Lum, img.stride);
            auto zres = ZXing::ReadBarcodes(zimg, hints);
            for (auto& z : zres) {
                if (!z.isValid()) continue;
                Result r;
                r.symbology = from_zx(z.format());
                r.text = z.text();
                const ByteArray& b = z.bytes();
                r.bytes.assign(b.begin(), b.end());
                auto pos = z.position();
                // zxing position order: topLeft, topRight, bottomRight, bottomLeft
                r.location.p[0] = Point{static_cast<float>(pos.topLeft().x), static_cast<float>(pos.topLeft().y)};
                r.location.p[1] = Point{static_cast<float>(pos.topRight().x), static_cast<float>(pos.topRight().y)};
                r.location.p[2] = Point{static_cast<float>(pos.bottomRight().x), static_cast<float>(pos.bottomRight().y)};
                r.location.p[3] = Point{static_cast<float>(pos.bottomLeft().x), static_cast<float>(pos.bottomLeft().y)};
                r.confidence = 1.0f;
                out.push_back(std::move(r));
                if (static_cast<int>(out.size()) >= opt.max_symbols) break;
            }
            return out.empty() ? DecodeStatus::NoBarcodeFound : DecodeStatus::Ok;
        } catch (...) {
            return DecodeStatus::BackendError;
        }
    }
};

} // namespace

const Backend& instance() noexcept {
    static const RealBackend b{};
    return b;
}

} // namespace backend_zxing
} // namespace omniscan

#else // OMNISCAN_WITH_ZXING off: honest stub, zero third-party deps.

namespace omniscan {
namespace backend_zxing {
namespace {

struct StubBackend final : Backend {
    const char* name() const noexcept override { return "zxing"; }
    const char* version() const noexcept override { return "not-compiled-in"; }
    bool available() const noexcept override { return false; }
    SymMask handled() const noexcept override { return kMaskGrade1; }
    DecodeStatus decode(const ImageView&, const Options&,
                        std::vector<Result>&) const noexcept override {
        return DecodeStatus::BackendNotAvailable;
    }
};

} // namespace

const Backend& instance() noexcept {
    static const StubBackend b{};
    return b;
}

} // namespace backend_zxing
} // namespace omniscan

#endif
