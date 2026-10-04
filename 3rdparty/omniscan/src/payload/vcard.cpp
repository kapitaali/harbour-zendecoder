// vCard 3.0/4.0 field extraction (M7). Unfolds continuation lines, strips
// parameters, unescapes values, joins repeats. No exceptions escape.
#include "omniscan/payload/vcard.h"
#include <vector>

namespace omniscan {
namespace payload {
namespace {

std::string upper_of(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    return s;
}

std::string unescape_vc(const std::string& v) {
    std::string out;
    try {
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '\\' && i + 1 < v.size()) {
                char n = v[i + 1];
                if (n == 'n' || n == 'N')
                    out.push_back('\n');
                else
                    out.push_back(n);  // \, \; \\ all collapse correctly
                ++i;
            } else {
                out.push_back(v[i]);
            }
        }
    } catch (...) {
    }
    return out;
}

bool known_field(const std::string& name) {
    static const char* kKnown[] = {"FN", "N",  "NICKNAME", "ORG",  "TITLE",
                                   "ROLE", "TEL", "EMAIL", "ADR",  "URL",
                                   "NOTE", "BDAY", "ADR",  "IMPP", "X-SOCIAL"};
    for (const char* k : kKnown)
        if (name == k) return true;
    return false;
}

}  // namespace

bool parse_vcard(const std::string& text, std::map<std::string, std::string>& out) {
    std::map<std::string, std::string> tmp;
    if (text.empty()) return false;
    try {
        // Normalize newlines, unfold continuations.
        std::string norm;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r') continue;
            norm.push_back(text[i]);
        }
        std::vector<std::string> lines;
        std::string cur;
        for (size_t i = 0; i <= norm.size(); ++i) {
            bool eol = (i == norm.size()) || norm[i] == '\n';
            if (eol) {
                if (!cur.empty() && (cur[0] == ' ' || cur[0] == '\t') &&
                    !lines.empty()) {
                    lines.back() += cur.substr(1);
                } else if (!cur.empty()) {
                    lines.push_back(cur);
                }
                cur.clear();
            } else {
                cur.push_back(norm[i]);
            }
        }
        if (lines.size() < 3) return false;
        if (upper_of(lines.front()).substr(0, 11) != "BEGIN:VCARD") return false;
        if (upper_of(lines.back()).substr(0, 9) != "END:VCARD") return false;
        bool saw_version = false, saw_field = false;
        for (const std::string& ln : lines) {
            size_t c = ln.find(':');
            if (c == std::string::npos || c == 0) continue;
            std::string head = ln.substr(0, c);
            std::string value = ln.substr(c + 1);
            size_t sc = head.find(';');
            std::string name = upper_of(head.substr(0, sc));
            if (name == "BEGIN" || name == "END") continue;
            if (name == "VERSION") {
                saw_version = true;
                continue;
            }
            if (!known_field(name)) continue;
            std::string key = "vcard." + name;
            std::string val = unescape_vc(value);
            auto it = tmp.find(key);
            if (it == tmp.end())
                tmp[key] = val;
            else
                it->second += "\n" + val;
            saw_field = true;
        }
        if (!saw_version || !saw_field) return false;
        out["payload"] = "vcard";
        for (auto& kv : tmp) out[kv.first] = kv.second;
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace payload
}  // namespace omniscan
