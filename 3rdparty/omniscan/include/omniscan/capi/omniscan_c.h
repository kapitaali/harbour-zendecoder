#pragma once
/// @file
/// Stable C ABI. Pure C, no exceptions, no C++ types.
///
/// Ownership: omniscan_decode() allocates *out (array of `n` results, or
/// NULL when n==0). Free with omniscan_results_free(). All returned char*
/// borrow from the container — do not free them, do not use after free.
#include <stddef.h>
#include <stdint.h>

#include "omniscan/export.h"

#ifdef __cplusplus
extern "C" {
#endif

// Export control is shared with the C++ API (see omniscan/export.h):
// OMNISCAN_STATIC for static builds, OMNISCAN_BUILDING_CAPI while building.
#define OMNISCAN_CAPI OMNISCAN_API

/// Opaque result container (an array of `*n` results).
typedef struct omniscan_result_set omniscan_result_t;

/// Decode one 8-bit grayscale buffer.
///
/// @param gray      pixel data, row-major, @p h rows of @p stride bytes
/// @param w         width in pixels (>0)
/// @param h         height in pixels (>0)
/// @param stride    bytes per row (>= w)
/// @param sym_mask  low-32-bit mask over Symbology values 0..31 (Grade-1).
///                  Pass 0xFFFFFFFFu for "all Grade-1".
/// @param out       receives the allocated result set (NULL on failure/empty)
/// @param n         receives the number of results (0 => *out is NULL)
/// @return 0 on success (even when *n==0), negative DecodeStatus code on error.
///         max_symbols<=0 is treated as 1 (via the C++ Options default).
OMNISCAN_CAPI int omniscan_decode(const uint8_t* gray, int w, int h, int stride,
                                  uint32_t sym_mask,
                                  omniscan_result_t** out, size_t* n);

/// Decoded text of result @p i (UTF-8, NUL-terminated, borrowed).
/// NULL-safe: returns "" on bad index/handle.
OMNISCAN_CAPI const char* omniscan_result_text(const omniscan_result_t* r, size_t i);

/// Symbology name of result @p i (borrowed). NULL-safe: "" on bad index/handle.
OMNISCAN_CAPI const char* omniscan_result_symbology(const omniscan_result_t* r, size_t i);

/// Symbology id of result @p i (Symbology value; 255 = Unknown on bad
/// index/handle).
OMNISCAN_CAPI int omniscan_result_symbology_id(const omniscan_result_t* r, size_t i);

/// Confidence of result @p i in 0..1 (0.0f on bad index/handle).
OMNISCAN_CAPI float omniscan_result_confidence(const omniscan_result_t* r, size_t i);

/// Corner points of result @p i (image space, clockwise TL,TR,BR,BL)
/// written to x0..y3. Returns 0 on success, -1 on bad index/handle.
OMNISCAN_CAPI int omniscan_result_quad(const omniscan_result_t* r, size_t i,
                                       float* x0, float* y0, float* x1, float* y1,
                                       float* x2, float* y2, float* x3, float* y3);

/// Library version string (matches OMNISCAN_VERSION_STRING). Never NULL.
OMNISCAN_CAPI const char* omniscan_version(void);

/// Free a result set from omniscan_decode(). NULL-safe.
OMNISCAN_CAPI void omniscan_results_free(omniscan_result_t* r);

#ifdef __cplusplus
} // extern "C"
#endif
