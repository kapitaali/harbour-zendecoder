// Owned image + ROI helpers.
#include "omniscan/image.h"

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
