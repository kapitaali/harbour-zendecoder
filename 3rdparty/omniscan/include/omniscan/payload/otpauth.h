#pragma once
/**
@file
otpauth Key URI Format, totp/hotp (M7). Validates scheme, type, base32
    secret, algorithm/digits/counter rules. Keys "payload"="otpauth" plus
    "otp.*" entries. Pure, deterministic.
*/
#include <map>
#include <string>
#include "omniscan/export.h"

namespace omniscan {
namespace payload {

/// Parse an otpauth Key URI (totp/hotp). @return true when the text is otpauth (parsed merged).
OMNISCAN_API bool parse_otpauth(const std::string& text, std::map<std::string, std::string>& out);

}  // namespace payload
}  // namespace omniscan
