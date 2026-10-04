// GS1 AI element strings (M7). Table: fixed (F) or variable-max (V) data
// lengths per the GS1 General Specifications. Unknown AIs split explicitly
// in "(..)" form; raw form stops at unknown AIs. No exceptions escape.
#include "omniscan/payload/gs1_ai.h"

namespace omniscan {
namespace payload {
namespace {

// len > 0 = fixed length; len < 0 = variable with max -len. 0 = unused.
struct AiInfo {
    const char* ai;
    int len;
};

const AiInfo kTable[] = {
    {"00", 18}, {"01", 14}, {"02", 14}, {"10", -20}, {"11", 6}, {"12", 6},
    {"13", 6}, {"15", 6}, {"16", 6}, {"17", 6}, {"20", 2}, {"21", -20},
    {"30", -8}, {"37", -8}, {"400", -30}, {"401", -30}, {"410", 13},
    {"411", 13}, {"412", 13}, {"413", 13}, {"414", 13}, {"415", 13},
    {"420", -20}, {"422", 3}, {"424", 3}, {"8004", -30}, {"8006", 18},
};

// Exact AI lookup (handles 3100-3169 / 3200-3299 families). Returns len or 0.
int ai_len(const std::string& ai) {
    for (const AiInfo& e : kTable) {
        if (ai == e.ai) return e.len;
    }
    // Decimal-place families: 310n-316n, 320n-329n are 6 fixed.
    if (ai.size() == 4 && ai[3] >= '0' && ai[3] <= '9') {
        std::string pre = ai.substr(0, 3);
        if ((pre >= "310" && pre <= "316") || (pre >= "320" && pre <= "329"))
            return 6;
    }
    return 0;
}

bool is_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

bool valid_ymd(const std::string& s) {
    if (s.size() != 6 || !is_digits(s)) return false;
    int mm = (s[2] - '0') * 10 + (s[3] - '0');
    int dd = (s[4] - '0') * 10 + (s[5] - '0');
    return mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31;
}

bool valid_value(const std::string& ai, const std::string& v) {
    int len = ai_len(ai);
    if (len > 0) {
        if ((int)v.size() != len) return false;
    } else if (len < 0) {
        if (v.empty() || (int)v.size() > -len) return false;
    } else {
        return true;  // unknown AI: structure already checked by caller
    }
    bool numeric_ai = (ai == "00" || ai == "01" || ai == "02" ||
                       (ai >= "11" && ai <= "17") || ai == "20" ||
                       ai == "30" || ai == "37" ||
                       (ai.size() == 4 && ai[0] == '3'));
    if (numeric_ai && !is_digits(v)) return false;
    if ((ai >= "11" && ai <= "17") && !valid_ymd(v)) return false;
    return true;
}

}  // namespace

bool validate_gtin14(const std::string& gtin14) {
    if (gtin14.size() != 14 || !is_digits(gtin14)) return false;
    int sum = 0;
    for (int i = 0; i < 14; ++i) {
        int d = gtin14[13 - i] - '0';
        sum += (i % 2 == 0) ? d : 3 * d;
    }
    return sum % 10 == 0;
}

bool parse_gs1(const std::string& text, std::map<std::string, std::string>& out) {
    std::map<std::string, std::string> tmp;
    bool ok = false;
    if (!text.empty() && text[0] == '(') {
        // Parenthesized form: strictly "(AI)value(AI)value...".
        size_t i = 0;
        while (i < text.size()) {
            if (text[i] != '(') break;
            size_t c = text.find(')', i + 1);
            if (c == std::string::npos) break;
            std::string ai = text.substr(i + 1, c - i - 1);
            if (ai.size() < 2 || ai.size() > 4 || !is_digits(ai)) break;
            size_t v0 = c + 1;
            size_t n = text.find('(', v0);
            std::string v = text.substr(v0, n == std::string::npos
                                                 ? n
                                                 : n - v0);
            if (v.empty() || v.find(')') != std::string::npos) break;
            if (!valid_value(ai, v)) break;
            try {
                tmp["gs1." + ai] = v;
            } catch (...) {
                return false;
            }
            ok = true;
            if (n == std::string::npos) {
                i = text.size();
            } else {
                i = n;
            }
        }
        if (!ok || i != text.size()) return false;
    } else {
        // Raw form: AIs back to back, variable fields FNC1-terminated.
        size_t i = 0;
        while (i < text.size()) {
            std::string ai;
            int len = 0;
            for (int w = 4; w >= 2; --w) {
                if (i + w > text.size()) continue;
                std::string cand = text.substr(i, w);
                if (!is_digits(cand)) continue;
                int l = ai_len(cand);
                if (l != 0 || w == 2) {
                    // Accept known AIs; 2-digit unknown AIs cannot be
                    // lengthed in raw form -> stop.
                    if (l == 0) break;
                    ai = cand;
                    len = l;
                    break;
                }
            }
            if (ai.empty()) break;
            i += ai.size();
            std::string v;
            if (len > 0) {
                if (i + len > text.size()) break;
                v = text.substr(i, len);
                i += len;
            } else {
                size_t e = text.find('\x1D', i);
                if (e == std::string::npos) {
                    v = text.substr(i);
                    i = text.size();
                } else {
                    v = text.substr(i, e - i);
                    i = e + 1;
                }
                if ((int)v.size() > -len) break;
            }
            if (!valid_value(ai, v)) break;
            try {
                tmp["gs1." + ai] = v;
            } catch (...) {
                return false;
            }
            ok = true;
        }
        if (!ok || i != text.size()) return false;
    }
    try {
        out["payload"] = "gs1";
        for (auto& kv : tmp) out[kv.first] = kv.second;
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace payload
}  // namespace omniscan
