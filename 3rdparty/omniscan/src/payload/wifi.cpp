// Wi-Fi QR format (M7). Single-pass unescape parser. No exceptions escape.
#include "omniscan/payload/wifi.h"
#include <utility>
#include <vector>

namespace omniscan {
namespace payload {
namespace {

// Split s on sep chars that are NOT backslash-escaped. Backslashes are
// preserved here; decoding happens once per field below.
std::vector<std::string> split_raw(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            cur.push_back(s[i]);
            cur.push_back(s[i + 1]);
            ++i;
        } else if (s[i] == sep) {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(s[i]);
        }
    }
    parts.push_back(cur);
    return parts;
}

std::string unescape_once(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            out.push_back(s[i + 1]);
            ++i;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

}  // namespace

bool parse_wifi(const std::string& text, std::map<std::string, std::string>& out) {
    std::map<std::string, std::string> tmp;
    const std::string pre = "WIFI:";
    if (text.size() < pre.size() + 3) return false;
    for (size_t i = 0; i < pre.size(); ++i)
        if (text[i] != pre[i]) return false;
    if (text[text.size() - 2] != ';' || text[text.size() - 1] != ';')
        return false;
    try {
        std::string body = text.substr(pre.size(), text.size() - pre.size() - 2);
        bool any = false;
        for (const std::string& field : split_raw(body, ';')) {
            if (field.empty()) continue;
            // First unescaped ':' separates key and value.
            size_t c = std::string::npos;
            for (size_t i = 0; i < field.size(); ++i) {
                if (field[i] == '\\' && i + 1 < field.size()) {
                    ++i;
                } else if (field[i] == ':') {
                    c = i;
                    break;
                }
            }
            if (c == std::string::npos || c == 0) return false;
            std::string key = "wifi." + unescape_once(field.substr(0, c));
            std::string val = unescape_once(field.substr(c + 1));
            if (key == "wifi.") return false;
            tmp[key] = val;
            any = true;
        }
        if (!any) return false;
        auto it = tmp.find("wifi.S");
        if (it == tmp.end() || it->second.empty()) return false;
        if (tmp.find("wifi.T") == tmp.end()) tmp["wifi.T"] = "nopass";
        out["payload"] = "wifi";
        for (auto& kv : tmp) out[kv.first] = kv.second;
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace payload
}  // namespace omniscan
