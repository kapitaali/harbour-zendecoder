// Binarizers: Otsu (global), mean-adaptive and Sauvola (integral-image based).
// Deterministic; invalid input -> InvalidImage; never throws across the API.
#include "omniscan/binarizer.h"
#include <cmath>
#include <cstring>

namespace omniscan {

int otsu_threshold(const ImageView& img) noexcept {
    if (!img.valid()) return -1;
    uint64_t hist[256];
    std::memset(hist, 0, sizeof hist);
    for (int y = 0; y < img.height; ++y) {
        const uint8_t* row = img.data + y * img.stride;
        for (int x = 0; x < img.width; ++x) hist[row[x]]++;
    }
    const double total = static_cast<double>(img.width) * img.height;
    double sum = 0;
    for (int i = 0; i < 256; ++i) sum += i * hist[i];
    double sumB = 0, wB = 0, best = -1;
    int thresh = 0;
    for (int t = 0; t < 256; ++t) {
        wB += hist[t];
        if (wB == 0) continue;
        double wF = total - wB;
        if (wF == 0) break;
        sumB += t * hist[t];
        double mB = sumB / wB, mF = (sum - sumB) / wF;
        double between = wB * wF * (mB - mF) * (mB - mF);
        if (between > best) { best = between; thresh = t; }
    }
    return thresh;
}

static int make_odd_at_least3(int w) noexcept {
    if (w < 3) w = 3;
    return (w % 2 == 0) ? w + 1 : w;
}

DecodeStatus binarize(const ImageView& img, const BinarizerParams& params,
                      BinaryImage& out) noexcept {
    out = BinaryImage{};
    if (!img.valid()) return DecodeStatus::InvalidImage;
    if (img.width > 16384 || img.height > 16384) return DecodeStatus::InvalidImage;
    try {
        out.width = img.width; out.height = img.height; out.stride = img.width;
        out.bits.assign(static_cast<size_t>(img.width) * img.height, 0);
    } catch (...) {
        out = BinaryImage{};
        return DecodeStatus::BackendError; // allocation failure
    }

    if (params.method == BinarizerMethod::Otsu) {
        int t = otsu_threshold(img);
        if (t < 0) { out = BinaryImage{}; return DecodeStatus::InvalidImage; }
        for (int y = 0; y < img.height; ++y) {
            const uint8_t* src = img.data + y * img.stride;
            uint8_t* dst = out.bits.data() + y * out.stride;
            for (int x = 0; x < img.width; ++x)
                dst[x] = (src[x] > t) ? 1 : 0;
        }
        return DecodeStatus::Ok;
    }

    // Local methods via integral images (sum + sumsq).
    const int W = img.width, H = img.height;
    std::vector<double> integ, integ2;
    try {
        integ.assign(static_cast<size_t>(W + 1) * (H + 1), 0.0);
        integ2.assign(static_cast<size_t>(W + 1) * (H + 1), 0.0);
    } catch (...) {
        out = BinaryImage{};
        return DecodeStatus::BackendError;
    }
    auto I = [&](int x, int y) -> double& { return integ[static_cast<size_t>(y) * (W + 1) + x]; };
    for (int y = 0; y < H; ++y) {
        double rowsum = 0, rowsum2 = 0;
        const uint8_t* src = img.data + y * img.stride;
        for (int x = 0; x < W; ++x) {
            double v = src[x];
            rowsum += v; rowsum2 += v * v;
            I(x + 1, y + 1) = I(x + 1, y) + rowsum;
            integ2[static_cast<size_t>(y + 1) * (W + 1) + x + 1] =
                integ2[static_cast<size_t>(y) * (W + 1) + x + 1] + rowsum2;
        }
    }
    auto rect_sum = [&](const std::vector<double>& II, int x0, int y0, int x1, int y1) {
        // [x0,x1) x [y0,y1)
        return II[static_cast<size_t>(y1) * (W + 1) + x1]
             - II[static_cast<size_t>(y0) * (W + 1) + x1]
             - II[static_cast<size_t>(y1) * (W + 1) + x0]
             + II[static_cast<size_t>(y0) * (W + 1) + x0];
    };
    int win = make_odd_at_least3(params.window);
    int r = win / 2;
    double k = params.sauvola_k, R = params.sauvola_R;
    if (!(R > 0)) R = 128.0;

    for (int y = 0; y < H; ++y) {
        const uint8_t* src = img.data + y * img.stride;
        uint8_t* dst = out.bits.data() + y * out.stride;
        for (int x = 0; x < W; ++x) {
            int x0 = x - r < 0 ? 0 : x - r, y0 = y - r < 0 ? 0 : y - r;
            int x1 = x + r + 1 > W ? W : x + r + 1, y1 = y + r + 1 > H ? H : y + r + 1;
            double n = static_cast<double>(x1 - x0) * (y1 - y0);
            double s = rect_sum(integ, x0, y0, x1, y1);
            double mean = s / n;
            uint8_t bit;
            if (params.method == BinarizerMethod::MeanAdaptive) {
                bit = (src[x] > mean - params.offset_C) ? 1 : 0;
            } else { // Sauvola
                double s2 = rect_sum(integ2, x0, y0, x1, y1);
                double var = s2 / n - mean * mean;
                if (var < 0) var = 0;
                double stddev = std::sqrt(var);
                double t = mean * (1.0 + k * (stddev / R - 1.0));
                bit = (src[x] > t) ? 1 : 0;
            }
            dst[x] = bit;
        }
    }
    return DecodeStatus::Ok;
}

} // namespace omniscan
