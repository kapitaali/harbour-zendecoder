#pragma once
#include <cstdint>
#include "omniscan/symbology.h"

namespace omniscan {

// MSI check-scheme selector. Auto keeps the documented preference order
// (exact "10"-expansions, then 1010, 1110, 10, 11, NCR variants); any other
// value restricts accepted readings to that scheme alone.
enum class MsiCheck {
    Auto = 0,
    Mod10 = 10,
    Mod1010 = 1010,
    Mod11 = 11,
    Mod1110 = 1110,
    Mod11Ncr = 111,
    Mod1110Ncr = 1111,
};

struct Options {
    SymMask enabled_symbologies = kMaskAll; // bitmask over Symbology values
    bool try_harder = false;  // extra binarizers/scan lines + despeckle retry (slower, more recalls)
    bool return_parsed = true; // fill Result::parsed (GS1 AI, vCard, ...)
    int max_symbols = 1;      // cap on returned symbols (>=1)
    MsiCheck msi_check = MsiCheck::Auto; // MSI check-scheme restriction

    constexpr Options() noexcept = default;
};

// C-ABI-compatible 32-bit view: low 32 bits of the mask (Grade-1 set).
// Bit i corresponds to Symbology value i for i in 0..31.
constexpr uint32_t kCapiMaskAll32 = 0xFFFFFFFFu;

} // namespace omniscan
