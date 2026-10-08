#pragma once
// MaxiCode image pipeline (internal): bullseye location, hex-grid
// sampling, codeword assembly, EC verification, value decode.
#include <string>
#include <vector>
#include "omniscan/binarizer.h"
#include "omniscan/export.h"

namespace omniscan {
namespace maxicode {

// Decode one MaxiCode symbol from a binarized image. Returns false if the
// frame or EC does not validate.
OMNISCAN_API bool decode_image(const BinaryImage& bin, std::string& text);

// Located decode: the frame as-is, rotated 180 degrees, then the
// dark-pixel bounding box (quiet zone) as-is and rotated — first
// success wins. Same EC acceptance as decode_image; see
// docs/formats/maxicode.md §Image pipeline.
OMNISCAN_API bool decode_located(const BinaryImage& bin, std::string& text);

// Tilted-photo decode: recovers centre, scale and angle from the image
// (ink-bbox seed -> ray-span scale -> annulus-centroid ring centre ->
// complete-circle bullseye gate), then scans 21 scale ratios x 360
// angles through the primary-fix scorer and does a full decode at hit
// candidates. try_harder path only; see docs/formats/maxicode.md
// §Image pipeline for the stages, the measured tolerances and the cost.
OMNISCAN_API bool decode_photo(const BinaryImage& bin, std::string& text);

// Full decode through the rotated grid at an explicit angle: (ccx, ccy)
// is the image position of symbol (sym_cx, sym_cy) (bullseye (144.5, 149)
// at the ring center, or (149.5, 149) at the bbox center), s is image px
// per symbol unit, theta_deg clockwise-as-viewed.
// Exposed for tests; production uses decode_photo (scan) instead.
OMNISCAN_API bool decode_tilted(const BinaryImage& bin, double theta_deg,
                                double ccx, double ccy, double s,
                                std::string& text, double sym_cx = 149.5,
                                double sym_cy = 149.0);

// Verify the secondary codewords, correcting at most one symbol error
// per interleave block (a data fix is applied to cws; a proven
// parity-only error accepts without changing cws). Returns false when
// the damage exceeds single-error-per-block. Exact-match fast path
// first, so clean vectors never touch the corrector. See
// docs/formats/maxicode.md §Image pipeline for the safety case.
OMNISCAN_API bool verify_or_correct(std::vector<int>& cws, int mode);

}  // namespace maxicode
}  // namespace omniscan
