#pragma once
/**
@file
Vehicle Identification Number, 17 chars, ISO 3779 (M7). Claims only
    check-valid numbers. Keys "payload"="vin" plus "vin.*" entries.
    Pure, deterministic.
*/
#include <map>
#include <string>
#include "omniscan/export.h"

namespace omniscan {
namespace payload {

/// Parse a 17-char ISO 3779 VIN. @return true only for check-valid VINs (parsed merged).
OMNISCAN_API bool parse_vin(const std::string& text, std::map<std::string, std::string>& out);
/// ISO 3779 check-digit + format validation. False on bad input.
OMNISCAN_API bool validate_vin(const std::string& text);

}  // namespace payload
}  // namespace omniscan
