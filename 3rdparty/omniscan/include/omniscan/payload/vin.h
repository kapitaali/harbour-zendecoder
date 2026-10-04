#pragma once
// Vehicle Identification Number, 17 chars, ISO 3779 (M7). Claims only
// check-valid numbers. Keys "payload"="vin" plus "vin.*" entries.
// Pure, deterministic.
#include <map>
#include <string>
#include "omniscan/export.h"

namespace omniscan {
namespace payload {

OMNISCAN_API bool parse_vin(const std::string& text, std::map<std::string, std::string>& out);
OMNISCAN_API bool validate_vin(const std::string& text);

}  // namespace payload
}  // namespace omniscan
