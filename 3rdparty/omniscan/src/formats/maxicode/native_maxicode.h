#pragma once
// Native MaxiCode dispatcher (internal): binarize, then the T4 hex-grid
// image pipeline (sampling + primary/secondary EC verify + value core).
// See docs/formats/maxicode.md §Image pipeline.
#include <vector>
#include "omniscan/image.h"
#include "omniscan/options.h"
#include "omniscan/result.h"

namespace omniscan {
namespace maxicode_native {

bool native_maxicode_handled(Symbology s) noexcept;
DecodeStatus decode_native_maxicode(const ImageView& img, const Options& opt,
                                    std::vector<Result>& out) noexcept;

}  // namespace maxicode_native
}  // namespace omniscan
