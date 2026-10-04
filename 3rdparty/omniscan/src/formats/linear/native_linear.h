#pragma once
// Native linear dispatcher (internal). Binarizes once, scans lines, tries
// enabled M3 codecs in both directions, dedupes, respects max_symbols.
#include <vector>
#include "omniscan/image.h"
#include "omniscan/options.h"
#include "omniscan/result.h"

namespace omniscan {
namespace linear {

bool native_linear_handled(Symbology s) noexcept;
DecodeStatus decode_native_linear(const ImageView& img, const Options& opt,
                                  std::vector<Result>& out) noexcept;

}  // namespace linear
}  // namespace omniscan
