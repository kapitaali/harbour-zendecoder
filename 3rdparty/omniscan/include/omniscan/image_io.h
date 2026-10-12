#pragma once
/// @file
/// Minimal image I/O: PGM (P5) + PPM (P6) natively, JPEG/PNG/BMP/GIF
/// via the vendored header-only stb_image (public domain, compiled into
/// the library). Sailfish/Qt callers may keep using QImage and pass gray
/// pixels directly.
#include <string>
#include "omniscan/image.h"
#include "omniscan/result.h"
#include "omniscan/export.h"

namespace omniscan {

/// Load a binary PGM (P5) file.
OMNISCAN_API DecodeStatus load_pgm(const std::string& path, Image& out);

/// Save a view as binary PGM (P5): writes `width` bytes per row
/// (stride-aware, verbatim grayscale). Invalid view -> InvalidImage,
/// short write -> BackendError.
OMNISCAN_API DecodeStatus save_pgm(const std::string& path,
                                  const ImageView& img);

/// Load a binary PPM (P6) file converted to grayscale.
OMNISCAN_API DecodeStatus load_ppm_as_gray(const std::string& path, Image& out);

/// Dispatch by magic bytes: P5 -> load_pgm, P6 -> load_ppm_as_gray,
/// else JPEG/PNG/BMP/GIF via vendored stb_image (Rec.601 luma).
/// Unreadable/unknown input -> InvalidImage with a clear cause
/// (input-side failure, not a missing decoder).
OMNISCAN_API DecodeStatus load_image(const std::string& path, Image& out);

} // namespace omniscan
