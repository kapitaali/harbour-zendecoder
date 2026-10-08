#pragma once
// Native stacked dispatcher (internal): Code 16K and Codablock F over
// segmented rows. Code 49 arrives here later; the dispatcher shape already
// accommodates it (one more row-format branch).
#include <vector>
#include "omniscan/image.h"
#include "omniscan/options.h"
#include "omniscan/result.h"

namespace omniscan {
namespace stacked {

bool native_stacked_handled(Symbology s) noexcept;
DecodeStatus decode_native_stacked(const ImageView& img, const Options& opt,
                                   std::vector<Result>& out) noexcept;

}  // namespace stacked
}  // namespace omniscan
