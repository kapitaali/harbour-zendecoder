#pragma once
// Adaptive thresholding. Pure functions, deterministic, no global state.
#include <cstdint>
#include <vector>
#include "omniscan/image.h"
#include "omniscan/result.h"
#include "omniscan/export.h"

namespace omniscan {

enum class BinarizerMethod {
    Otsu,          // global threshold from histogram
    MeanAdaptive,  // local mean in WxW window minus C
    Sauvola        // local mean/stddev, k=0.34, R=128
};

// Packed binary image: 1 byte per pixel (0 or 1), row-major.
struct BinaryImage {
    int width = 0, height = 0, stride = 0;
    std::vector<uint8_t> bits; // size stride*height, values 0/1

    bool empty() const noexcept { return bits.empty(); }
    uint8_t at(int x, int y) const noexcept { return bits[y * stride + x]; }
};

struct BinarizerParams {
    BinarizerMethod method = BinarizerMethod::Otsu;
    int window = 25;       // adaptive window (odd, >=3)
    int offset_C = 5;      // mean-adaptive subtractor
    double sauvola_k = 0.34;
    double sauvola_R = 128.0;
};

// Compute Otsu threshold for a grayscale view (0..255). Returns -1 on invalid view.
OMNISCAN_API int otsu_threshold(const ImageView& img) noexcept;

// Binarize into `out` (resized). Never throws; reports InvalidImage on bad input.
OMNISCAN_API DecodeStatus binarize(const ImageView& img,
                              const BinarizerParams& params,
                              BinaryImage& out) noexcept;

} // namespace omniscan
