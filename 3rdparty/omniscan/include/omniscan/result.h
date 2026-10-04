#pragma once
// Unified decode result + status codes. No exceptions across the API.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "omniscan/geometry.h"
#include "omniscan/symbology.h"
#include "omniscan/export.h"

namespace omniscan {

enum class DecodeStatus : int {
    Ok = 0,
    NoBarcodeFound = 1,     // valid call, zero symbols (out is empty)
    InvalidImage = -1,      // null/empty view, bad stride/dims
    InvalidArgument = -2,   // bad options, null out-params
    UnsupportedSymbology = -3,
    BackendNotAvailable = -4, // requested symbologies need a backend that is off
    BackendError = -5,      // backend failed internally (never throws)
};

OMNISCAN_API const char* to_string(DecodeStatus s) noexcept;
inline bool succeeded(DecodeStatus s) noexcept {
    return s == DecodeStatus::Ok || s == DecodeStatus::NoBarcodeFound;
}

struct Result {
    Symbology symbology = Symbology::Unknown;
    std::string text;               // decoded text (UTF-8 where applicable)
    std::vector<uint8_t> bytes;     // raw bytes for binary payloads (may be empty)
    Quad location{};                // corners in image space (may be degenerate if unknown)
    std::optional<Symbology> linked_symbology; // for GS1 Composite etc.
    float confidence = 0.0f;        // 0..1
    std::map<std::string, std::string> parsed; // GS1 AIs, vCard fields, etc.
};

} // namespace omniscan
