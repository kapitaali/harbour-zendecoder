#pragma once
/**
@file
MECARD (docomo) compact contact format (M7). Keys "payload"="mecard"
    plus "mecard.<KEY>". Pure, deterministic.
*/
#include <map>
#include <string>
#include "omniscan/export.h"

namespace omniscan {
namespace payload {

/// Parse a MECARD string. @return true when the text is MECARD (parsed merged).
OMNISCAN_API bool parse_mecard(const std::string& text, std::map<std::string, std::string>& out);

}  // namespace payload
}  // namespace omniscan
