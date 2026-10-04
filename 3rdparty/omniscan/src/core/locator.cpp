// M1 locator stub: honest no-op. Real finder-pattern detectors land with
// native Tier-2 decoders (M3+) — this must not produce false positives.
#include "omniscan/locator.h"

namespace omniscan {

DecodeStatus find_candidates(const BinaryImage& bin,
                             std::vector<Candidate>& out) noexcept {
    try {
        out.clear();
    } catch (...) {
        return DecodeStatus::BackendError;
    }
    if (bin.empty() || bin.width <= 0 || bin.height <= 0) return DecodeStatus::InvalidImage;
    return DecodeStatus::Ok; // zero candidates at M1
}

} // namespace omniscan
