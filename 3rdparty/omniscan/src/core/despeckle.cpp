// 3x3 median filter for impulse-noise removal (see despeckle.h).
// Sorting network on 9 bytes: no allocation in the inner loop, deterministic,
// no exceptions. Cost measured at well under a millisecond for corpus-sized
// images, and it only runs on the try_harder miss path.
#include "despeckle.h"
#include <cstdint>

namespace omniscan {
namespace despeckle {

namespace {

// Insertion sort of a fixed 9-element window; median is index 4.
inline uint8_t median9(uint8_t* w) {
    for (int i = 1; i < 9; ++i) {
        uint8_t v = w[i];
        int j = i - 1;
        while (j >= 0 && w[j] > v) {
            w[j + 1] = w[j];
            --j;
        }
        w[j + 1] = v;
    }
    return w[4];
}

}  // namespace

Image median3(const ImageView& src) {
    if (!src.valid() || src.width < 3 || src.height < 3) return {};
    Image out(src.width, src.height);
    if (out.empty()) return {};

    const int W = src.width, H = src.height;
    for (int y = 0; y < H; ++y) {
        uint8_t* dst = out.row(y);
        // Clamp the three source rows once per column run.
        const int ym = y > 0 ? y - 1 : 0;
        const int yp = y + 1 < H ? y + 1 : H - 1;
        const uint8_t* r0 = src.data + ym * src.stride;
        const uint8_t* r1 = src.data + y * src.stride;
        const uint8_t* r2 = src.data + yp * src.stride;
        for (int x = 0; x < W; ++x) {
            const int xm = x > 0 ? x - 1 : 0;
            const int xp = x + 1 < W ? x + 1 : W - 1;
            uint8_t w[9] = {
                r0[xm], r0[x], r0[xp],
                r1[xm], r1[x], r1[xp],
                r2[xm], r2[x], r2[xp],
            };
            dst[x] = median9(w);
        }
    }
    return out;
}

}  // namespace despeckle
}  // namespace omniscan
