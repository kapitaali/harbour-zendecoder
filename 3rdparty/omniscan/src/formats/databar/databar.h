#pragma once
// Native GS1 DataBar (RSS-14) Omnidirectional decoder — internal.
// Pure module-level parser + image dispatcher. Deterministic, no I/O,
// suitable for libFuzzer. See docs/formats/databar.md for the derived
// algorithm (check character, width enumeration, finder templates).
#include <string>
#include <vector>
#include "omniscan/export.h"
#include "omniscan/image.h"
#include "omniscan/options.h"
#include "omniscan/result.h"

namespace omniscan {
namespace databar {

bool native_databar_handled(Symbology s) noexcept;

// Pure codec: `runs` is a left-to-right, bar-first, alternating run-width
// vector in module units (45 runs, 95 modules) as produced by
// linear::extract_runs + linear::quantize_runs on an Omni symbol. On
// success emits the 14-digit GTIN (13 encoded digits + implied mod-10
// check digit), matching the backend's text. Returns false on any
// structural, finder, or mod-79 check failure.
// Exported for test linkage (shared builds hide everything else); not part
// of the supported API surface.
OMNISCAN_API bool parse_databar_omn(const std::vector<int>& runs,
                                    std::string& text);

// Exported for the same reason as parse_databar_omn: tests link the shared
// library and drive the image path directly (the top-level decode_into()
// registration is a shared-file edit owned by integration).
OMNISCAN_API DecodeStatus decode_native_databar(const ImageView& img,
                                                const Options& opt,
                                                std::vector<Result>& out) noexcept;

}  // namespace databar
}  // namespace omniscan
