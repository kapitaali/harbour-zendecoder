// MECARD compact contacts (M7). Strict frame, naive splits (values cannot
// contain raw ';' per the format). No exceptions escape.
#include "omniscan/payload/mecard.h"

namespace omniscan {
namespace payload {

bool parse_mecard(const std::string& text, std::map<std::string, std::string>& out) {
    std::map<std::string, std::string> tmp;
    const std::string pre = "MECARD:";
    if (text.size() < pre.size() + 3) return false;
    for (size_t i = 0; i < pre.size(); ++i)
        if (text[i] != pre[i]) return false;
    if (text[text.size() - 2] != ';' || text[text.size() - 1] != ';')
        return false;
    try {
        std::string body = text.substr(pre.size(), text.size() - pre.size() - 2);
        size_t i = 0;
        bool any = false;
        while (i <= body.size()) {
            size_t e = body.find(';', i);
            std::string field =
                (e == std::string::npos) ? body.substr(i) : body.substr(i, e - i);
            if (!field.empty()) {
                size_t c = field.find(':');
                if (c == std::string::npos || c == 0) return false;
                std::string key = "mecard." + field.substr(0, c);
                std::string val = field.substr(c + 1);
                auto it = tmp.find(key);
                if (it == tmp.end())
                    tmp[key] = val;
                else
                    it->second += "\n" + val;
                any = true;
            }
            if (e == std::string::npos) break;
            i = e + 1;
        }
        if (!any) return false;
        out["payload"] = "mecard";
        for (auto& kv : tmp) out[kv.first] = kv.second;
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace payload
}  // namespace omniscan
