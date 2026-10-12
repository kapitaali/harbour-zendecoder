#pragma once
/// @file
/// Grayscale image buffers + non-owning views + ROI.
///
/// Deterministic, no global state, thread-safe on distinct images.
#include <cstddef>
#include <cstdint>
#include <vector>
#include "omniscan/export.h"

namespace omniscan {

/// Non-owning grayscale view. Data is row-major, `stride` bytes per row
/// (stride >= width). Lifetime belongs to the caller.
struct ImageView {
    const uint8_t* data = nullptr; ///< first pixel; not owned
    int width = 0;                 ///< pixels per row
    int height = 0;                ///< rows
    int stride = 0;                ///< bytes per row

    constexpr ImageView() noexcept = default;
    /// Construct from raw pointer + dimensions (no copy).
    constexpr ImageView(const uint8_t* d, int w, int h, int s) noexcept
        : data(d), width(w), height(h), stride(s) {}

    /// True when data is non-null and dimensions/stride are usable.
    bool valid() const noexcept {
        return data != nullptr && width > 0 && height > 0 && stride >= width;
    }
    /// Pixel at (x, y). No bounds check — caller must keep x/y in range.
    uint8_t at(int x, int y) const noexcept { return data[y * stride + x]; }
};

/// Owned grayscale image (row-major, padded to a stable stride).
class OMNISCAN_API Image {
public:
    /// Empty image.
    Image() = default;
    /// `w` x `h` image, zero-filled. Returns an empty image for
    /// non-positive dimensions or absurd sizes (>16384 per axis or
    /// >256 MP — deterministic failure, no throw).
    Image(int w, int h);

    Image(const Image&) = default;
    Image(Image&&) noexcept = default;
    Image& operator=(const Image&) = default;
    Image& operator=(Image&&) noexcept = default;

    /// Deep copy; empty on invalid input.
    static Image copy_from(const ImageView& v);

    int width() const noexcept { return w_; }   ///< pixels per row
    int height() const noexcept { return h_; }  ///< rows
    int stride() const noexcept { return stride_; } ///< bytes per row
    bool empty() const noexcept { return pixels_.empty(); } ///< no pixels

    /// Row @p y (mutable). No bounds check.
    uint8_t* row(int y) noexcept { return pixels_.data() + y * stride_; }
    /// Row @p y (const). No bounds check.
    const uint8_t* row(int y) const noexcept { return pixels_.data() + y * stride_; }

    /// Non-owning view over the whole image (empty view when empty()).
    ImageView view() const noexcept {
        if (empty()) return {};
        return {pixels_.data(), w_, h_, stride_};
    }

    /// Crop ROI (clamped). Returns empty image if ROI is empty/invalid.
    Image crop(int x, int y, int w, int h) const;

    /// Transpose (swap axes). Used for 90-degree-rotated code retry paths.
    Image transposed() const;

    /// Rotate clockwise as viewed by `degrees` about the image center,
    /// same-size output, bilinear interpolation, `fill` outside. Exact
    /// for multiples of 90 degrees (trig snapped); rotate(θ) followed
    /// by rotate(-θ) is near-identity (interpolation loss only).
    Image rotated(double degrees, uint8_t fill = 255) const;

private:
    int w_ = 0, h_ = 0, stride_ = 0;
    std::vector<uint8_t> pixels_;
};

/// Clamp a rectangle to image bounds. Returns false if the result is empty.
OMNISCAN_API bool clamp_roi(const ImageView& img, int& x, int& y, int& w,
                      int& h) noexcept;

} // namespace omniscan
