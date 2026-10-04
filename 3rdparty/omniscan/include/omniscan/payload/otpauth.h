#pragma once
// otpauth Key URI Format, totp/hotp (M7). Validates scheme, type, base32
// secret, algorithm/digits/counter rules. Keys "payload"="otpauth" plus
// "otp.*" entries. Pure, deterministic.
#include <map>
#include <string>
#include "omniscan/export.h"

namespace omniscan {
namespace payload {

OMNISCAN_API bool parse_otpauth(const std::string& text, std::map<std::string, std::string>& out);

}  // namespace payload
}  // namespace omniscan
