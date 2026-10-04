#pragma once
// vCard 3.0/4.0 (RFC 6350) field extraction (M7). Keys "payload"="vcard"
// plus "vcard.<FIELD>" (uppercased names, params stripped, repeats joined
// with '\n', standard backslash unescapes). Pure, deterministic.
#include <map>
#include <string>
#include "omniscan/export.h"

namespace omniscan {
namespace payload {

OMNISCAN_API bool parse_vcard(const std::string& text, std::map<std::string, std::string>& out);

}  // namespace payload
}  // namespace omniscan
