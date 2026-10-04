#pragma once
// Stable C ABI. Pure C, no exceptions, no C++ types.
// Ownership: omniscan_decode() allocates *out (array of `n` results, or NULL
// when n==0). Free with omniscan_results_free(). All returned char* borrow
// from the container — do not free them, do not use after free.
#include <stddef.h>
#include <stdint.h>

#include "omniscan/export.h"

#ifdef __cplusplus
extern "C" {
#endif

// Export control is shared with the C++ API (see omniscan/export.h):
// OMNISCAN_STATIC for static builds, OMNISCAN_BUILDING_CAPI while building.
#define OMNISCAN_CAPI OMNISCAN_API

typedef struct omniscan_result_set omniscan_result_t;

// Returns 0 on success (even when *n==0), negative DecodeStatus code on error.
// sym_mask: low-32-bit mask over Symbology values 0..31 (Grade-1). Pass
// 0xFFFFFFFFu for "all Grade-1". max_symbols<=0 means 1.
OMNISCAN_CAPI int omniscan_decode(const uint8_t* gray, int w, int h, int stride,
                                  uint32_t sym_mask,
                                  omniscan_result_t** out, size_t* n);

// Borrowed accessors (NULL-safe: return ""/0 on bad index/handle).
OMNISCAN_CAPI const char* omniscan_result_text(const omniscan_result_t* r, size_t i);
OMNISCAN_CAPI const char* omniscan_result_symbology(const omniscan_result_t* r, size_t i);
OMNISCAN_CAPI int omniscan_result_symbology_id(const omniscan_result_t* r, size_t i);
OMNISCAN_CAPI float omniscan_result_confidence(const omniscan_result_t* r, size_t i);
// Corner points (image space). Returns 0 on success, -1 on bad index/handle.
OMNISCAN_CAPI int omniscan_result_quad(const omniscan_result_t* r, size_t i,
                                       float* x0, float* y0, float* x1, float* y1,
                                       float* x2, float* y2, float* x3, float* y3);
OMNISCAN_CAPI const char* omniscan_version(void);
OMNISCAN_CAPI void omniscan_results_free(omniscan_result_t* r);

#ifdef __cplusplus
} // extern "C"
#endif
