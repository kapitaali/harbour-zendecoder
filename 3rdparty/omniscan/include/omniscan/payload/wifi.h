#pragma once
/**
@file
Wi-Fi QR format (de-facto zxing standard) (M7). Backslash escapes.
    Keys "payload"="wifi" plus "wifi.T/S/P/H" (missing T means "nopass").
    Pure, deterministic.
*/
#include <map>
#include <string>
#include "omniscan/export.h"

namespace omniscan {
namespace payload {

/// Parse a Wi-Fi QR format string. @return true when the text is Wi-Fi (parsed merged).
OMNISCAN_API bool parse_wifi(const std::string& text, std::map<std::string, std::string>& out);

}  // namespace payload
}  // namespace omniscan
