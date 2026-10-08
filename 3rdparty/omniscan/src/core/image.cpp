// Owned image + ROI helpers.
#include "omniscan/image.h"

#include <cmath>

namespace omniscan {

Image::Image(int w, int h) {
    if (w <= 0 || h <= 0) return;
    // Guard against overflow / absurd allocations (deterministic failure).
    if (w > 16384 || h > 16384) return;
    size_t need = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (need > (256u * 1024u * 1024u)) return; // 256 MP cap
    w_ = w; h_ = h; stride_ = w;
    pixels_.assign(need, 0);
}

Image Image::copy_from(const ImageView& v) {
    Image img;
    if (!v.valid()) return img;
    if (v.width > 16384 || v.height > 16384) return img;
    img.w_ = v.width; img.h_ = v.height; img.stride_ = v.width;
    img.pixels_.resize(static_cast<size_t>(v.width) * v.height);
    for (int y = 0; y < v.height; ++y) {
        const uint8_t* src = v.data + y * v.stride;
        uint8_t* dst = img.pixels_.data() + y * img.stride_;
        for (int x = 0; x < v.width; ++x) dst[x] = src[x];
    }
    return img;
}

Image Image::crop(int x, int y, int w, int h) const {
    Image out;
    if (empty() || w <= 0 || h <= 0) return out;
    if (!clamp_roi(view(), x, y, w, h)) return out;
    out = Image(w, h);
    if (out.empty()) return Image{};
    for (int r = 0; r < h; ++r) {
        const uint8_t* src = row(y + r) + x;
        uint8_t* dst = out.row(r);
        for (int c = 0; c < w; ++c) dst[c] = src[c];
    }
    return out;
}

Image Image::transposed() const {
    Image out;
    if (empty()) return out;
    out = Image(h_, w_);
    if (out.empty()) return Image{};
    for (int y = 0; y < h_; ++y) {
        const uint8_t* src = row(y);
        for (int x = 0; x < w_; ++x) out.row(x)[y] = src[x];
    }
    return out;
}

namespace {

double norm_trig(double v) {
    if (v > -1e-9 && v < 1e-9) return 0.0;
    if (v > 1.0 - 1e-9) return 1.0;
    if (v < -1.0 + 1e-9) return -1.0;
    return v;
}

}  // namespace

Image Image::rotated(double degrees, uint8_t fill) const {
    Image out;
    if (empty()) return out;
    out = Image(w_, h_);
    if (out.empty()) return Image{};
    const double rad = degrees * 3.141592653589793 / 180.0;
    const double c = norm_trig(std::cos(rad));
    const double s = norm_trig(std::sin(rad));
    const double cx = (w_ - 1) / 2.0;
    const double cy = (h_ - 1) / 2.0;
    for (int y = 0; y < h_; ++y) {
        const double dy = y - cy;
        uint8_t* dst = out.row(y);
        for (int x = 0; x < w_; ++x) {
            const double dx = x - cx;
            // Inverse map of a viewed-clockwise rotation.
            const double sx = cx + dx * c + dy * s;
            const double sy = cy - dx * s + dy * c;
            const int x0 = static_cast<int>(std::floor(sx));
            const int y0 = static_cast<int>(std::floor(sy));
            const double fx = sx - x0;
            const double fy = sy - y0;
            auto px = [&](int xx, int yy) -> double {
                if (xx < 0 || yy < 0 || xx >= w_ || yy >= h_)
                    return fill;
                return row(yy)[xx];
            };
            const double v = px(x0, y0) * (1 - fx) * (1 - fy) +
                             px(x0 + 1, y0) * fx * (1 - fy) +
                             px(x0, y0 + 1) * (1 - fx) * fy +
                             px(x0 + 1, y0 + 1) * fx * fy;
            dst[x] = static_cast<uint8_t>(v + 0.5);
        }
    }
    return out;
}

bool clamp_roi(const ImageView& img, int& x, int& y, int& w, int& h) noexcept {
    if (!img.valid() || w <= 0 || h <= 0) return false;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= img.width || y >= img.height) return false;
    if (x + w > img.width) w = img.width - x;
    if (y + h > img.height) h = img.height - y;
    return w > 0 && h > 0;
}

} // namespace omniscan
