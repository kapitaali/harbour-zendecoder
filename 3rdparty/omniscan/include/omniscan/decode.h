#pragma once
/// @file
/// Top-level decode entry points (dispatcher over registered backends).
///
/// Thread-safe on different images. Deterministic. No exceptions escape.
#include <vector>
#include "omniscan/image.h"
#include "omniscan/options.h"
#include "omniscan/result.h"
#include "omniscan/export.h"

namespace omniscan {

/// Allocating convenience wrapper. Returns results (possibly empty).
///
/// On invalid input returns empty vector; use decode_into() for status
/// detail.
OMNISCAN_API std::vector<Result> decode(const ImageView& img,
                                   const Options& opt = {});

/// Hot-path entry: fills `out` (cleared first).
///
/// @return
///   Ok               — one or more symbols found
///   NoBarcodeFound   — valid call, nothing found (out empty)
///   InvalidImage/InvalidArgument — bad input (out empty)
///   BackendNotAvailable — requested symbologies need a compiled-out backend
///   BackendError     — backend failure (out may be partial, never throws)
OMNISCAN_API DecodeStatus decode_into(const ImageView& img, const Options& opt,
                                 std::vector<Result>& out) noexcept;

/// Backend capability query (for tools/UI).
/// @param name  "zxing" or "native", case-insensitive, null-safe.
/// @return true when the named backend is compiled in.
OMNISCAN_API bool backend_available(const char* name) noexcept;

/// Backend version string (for tools/UI).
/// @param name  "zxing" or "native", case-insensitive, null-safe.
/// @return version string, never null; "" for null/unknown names.
OMNISCAN_API const char* backend_version(const char* name) noexcept;

} // namespace omniscan
