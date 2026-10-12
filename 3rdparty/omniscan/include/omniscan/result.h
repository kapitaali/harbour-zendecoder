#pragma once
/// @file
/// Unified decode result + status codes. No exceptions across the API.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "omniscan/geometry.h"
#include "omniscan/symbology.h"
#include "omniscan/export.h"

namespace omniscan {

/// Status returned by every entry point (never an exception).
enum class DecodeStatus : int {
    Ok = 0,                        ///< one or more symbols found
    NoBarcodeFound = 1,            ///< valid call, zero symbols (out is empty)
    InvalidImage = -1,             ///< null/empty view, bad stride/dims
    InvalidArgument = -2,          ///< bad options, null out-params
    UnsupportedSymbology = -3,     ///< mask names something this build cannot claim
    BackendNotAvailable = -4,      ///< requested symbologies need a backend that is off
    BackendError = -5,             ///< backend failed internally (never throws)
};

/// Name matching the enumerator spelling, never null ("Unknown" for
/// out-of-range values). Used by tools/logs.
OMNISCAN_API const char* to_string(DecodeStatus s) noexcept;

/// True for Ok and NoBarcodeFound: the call itself was valid.
inline bool succeeded(DecodeStatus s) noexcept {
    return s == DecodeStatus::Ok || s == DecodeStatus::NoBarcodeFound;
}

/// One decoded symbol.
struct Result {
    /// Which symbology was decoded (Unknown when unassigned).
    Symbology symbology = Symbology::Unknown;
    /// Decoded text (UTF-8 where applicable).
    std::string text;
    /// Raw bytes for binary payloads (may be empty).
    std::vector<uint8_t> bytes;
    /// Corners in image space (may be degenerate if unknown).
    Quad location{};
    /// For GS1 Composite etc.: the linked symbol's symbology.
    std::optional<Symbology> linked_symbology;
    /// 0..1.
    float confidence = 0.0f;
    /// GS1 AIs, vCard fields, etc. (filled when Options::return_parsed).
    std::map<std::string, std::string> parsed;
};

} // namespace omniscan
