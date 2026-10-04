#pragma once
// Native postal dispatcher (internal): RM4SCC + KIX over 4-state bars.
// Deutsche Post lives in the linear dispatcher (ITF structure).
#include <vector>
#include "omniscan/image.h"
#include "omniscan/options.h"
#include "omniscan/result.h"

namespace omniscan {
namespace postal {

bool native_postal_handled(Symbology s) noexcept;
DecodeStatus decode_native_postal(const ImageView& img, const Options& opt,
                                  std::vector<Result>& out) noexcept;

}  // namespace postal
}  // namespace omniscan
