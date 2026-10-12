#pragma once
/// @file
/// Decode options: symbology mask, effort knobs, MSI check scheme.
#include <cstdint>
#include "omniscan/symbology.h"

namespace omniscan {

/// MSI check-scheme selector.
///
/// Auto keeps the documented preference order (exact "10"-expansions,
/// then 1010, 1110, 10, 11, NCR variants); any other value restricts
/// accepted readings to that scheme alone.
enum class MsiCheck {
    Auto = 0,          ///< accept any scheme, documented preference order
    Mod10 = 10,        ///< exact mod-10 variant only
    Mod1010 = 1010,    ///< exact 1010 variant only
    Mod11 = 11,        ///< exact mod-11 variant only
    Mod1110 = 1110,    ///< exact 1110 variant only
    Mod11Ncr = 111,    ///< exact NCR mod-11 variant only
    Mod1110Ncr = 1111, ///< exact NCR mod-1110 variant only
};

/// Per-call decode options. Copyable, constexpr-defaulted, no globals.
struct Options {
    /// Bitmask over Symbology values (bit i = value i). See kMaskAll.
    SymMask enabled_symbologies = kMaskAll;
    /// Extra binarizers/scan lines + despeckle retry (slower, more recalls).
    bool try_harder = false;
    /// Fill Result::parsed (GS1 AI, vCard, ...).
    bool return_parsed = true;
    /// Cap on returned symbols (>=1; values <1 are rejected).
    int max_symbols = 1;
    /// MSI check-scheme restriction (MsiCheck).
    MsiCheck msi_check = MsiCheck::Auto;

    constexpr Options() noexcept = default;
};

/// C-ABI-compatible 32-bit view: low 32 bits of the mask (Grade-1 set).
/// Bit i corresponds to Symbology value i for i in 0..31.
constexpr uint32_t kCapiMaskAll32 = 0xFFFFFFFFu;

} // namespace omniscan
