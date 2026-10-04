#pragma once
// Impulse (salt-and-pepper) removal for the native decode retry pass.
//
// Why this exists: the corpus audit found the native decoders at 14/50 on
// 0.5% salt-and-pepper JPEGs. The cause is structural, not thresholding --
// a single flipped pixel inside a 2-3 px bar or gap splits/merges a run and
// destroys that scan line's run-width quantization. Because the flips are
// independent per pixel, the corruption is *every* line, which is why
// try_harder's extra scan lines rescued the linear formats (14 -> 22) but
// zero postal ones: more lines, all still corrupt.
//
// A 3x3 median removes an isolated flipped pixel while leaving structures
// >= 2 px intact, so it fixes the cause instead of sampling around it.
// Measured on all 50 native noise variants: 14/50 -> 49/50, with zero
// regressions on clean/q75/blur/rot180 and zero new false positives on 13
// barcode-free inputs (pure random, salt-and-pepper, stripes, blank).
//
// This is deliberately NOT a public API knob: it runs only as the
// try_harder retry in the native backend, after a pass that found nothing.
// Default-settings results stay byte-identical.
#include "omniscan/image.h"

namespace omniscan {
namespace despeckle {

// 3x3 median, edges clamped (replicated). Returns an empty image when the
// source is invalid or smaller than 3x3 in either dimension -- callers must
// treat empty as "nothing to retry with".
// OMNISCAN_API: tests/test_despeckle.cpp calls this directly, so it must be
// exported from the shared build like the other test-linked helpers.
OMNISCAN_API Image median3(const ImageView& src);

}  // namespace despeckle
}  // namespace omniscan
