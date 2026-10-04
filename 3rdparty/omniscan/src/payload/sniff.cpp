// Payload sniffing: explicit markers, then VIN-by-check, then raw GS1.
// Merges without overwriting existing keys (decoder-set keys win).
#include "omniscan/payload.h"

namespace omniscan {
namespace payload {

bool sniff_into(const std::string& text, std::map<std::string, std::string>& parsed) {
    if (text.empty()) return false;
    std::map<std::string, std::string> tmp;
    bool claimed = false;
    if (parse_swissqr(text, tmp))
        claimed = true;
    else if (parse_otpauth(text, tmp))
        claimed = true;
    else if (parse_wifi(text, tmp))
        claimed = true;
    else if (parse_mecard(text, tmp))
        claimed = true;
    else if (parse_vcard(text, tmp))
        claimed = true;
    else if (!text.empty() && text[0] == '(' && parse_gs1(text, tmp))
        claimed = true;
    else if (parse_vin(text, tmp))
        claimed = true;
    else if (parse_gs1(text, tmp))
        claimed = true;
    if (!claimed) return false;
    try {
        for (auto& kv : tmp)
            if (parsed.find(kv.first) == parsed.end()) parsed[kv.first] = kv.second;
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace payload
}  // namespace omniscan
