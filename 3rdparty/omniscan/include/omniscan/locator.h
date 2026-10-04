#pragma once
// Finder-pattern / bar-pattern detection (format-agnostic front end).
// M1: API + honest stub (no false positives). Real detectors arrive with
// native Tier-2 decoders (M3+) and via the zxing backend (M2).
#include <vector>
#include "omniscan/binarizer.h"
#include "omniscan/geometry.h"
#include "omniscan/result.h"
#include "omniscan/symbology.h"
#include "omniscan/export.h"

namespace omniscan {

struct Candidate {
    Quad quad{};
    Symbology hint = Symbology::Unknown;
    float score = 0.0f; // 0..1
};

// Detect candidate regions in a binarized image. M1 returns zero candidates
// (stub). Never reports failure for "nothing found": out is empty + Ok.
OMNISCAN_API DecodeStatus find_candidates(const BinaryImage& bin,
                                     std::vector<Candidate>& out) noexcept;

} // namespace omniscan
