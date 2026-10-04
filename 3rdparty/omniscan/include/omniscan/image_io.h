#pragma once
// Minimal image I/O: PGM (P5) + PPM (P6) natively, JPEG/PNG/BMP/GIF via the
// vendored header-only stb_image (public domain, compiled into the library).
// Sailfish/Qt callers may keep using QImage and pass gray pixels directly.
#include <string>
#include "omniscan/image.h"
#include "omniscan/result.h"
#include "omniscan/export.h"

namespace omniscan {

OMNISCAN_API DecodeStatus load_pgm(const std::string& path, Image& out);
OMNISCAN_API DecodeStatus save_pgm(const std::string& path,
                                  const ImageView& img);
OMNISCAN_API DecodeStatus load_ppm_as_gray(const std::string& path, Image& out);

// Dispatch by magic bytes: P5 -> load_pgm, P6 -> load_ppm_as_gray,
// else UnsupportedSymbology? No — returns InvalidImage with a clear cause.
// (Uses InvalidImage since the failure is input-side, not a missing decoder.)
OMNISCAN_API DecodeStatus load_image(const std::string& path, Image& out);

} // namespace omniscan
