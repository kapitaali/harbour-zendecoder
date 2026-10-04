// otpauth:// Key URIs (M7). Percent-decoding, base32 secret validation,
// totp/hotp rule sets. No exceptions escape.
#include "omniscan/payload/otpauth.h"

namespace omniscan {
namespace payload {
namespace {

std::string lower_of(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}

std::string upper_of(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    return s;
}

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Percent-decode (%XX) with '+' -> space. Returns false on bad escapes.
bool url_decode(const std::string& in, std::string& out) {
    out.clear();
    try {
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '%') {
                if (i + 2 >= in.size()) return false;
                int h = hexval(in[i + 1]), l = hexval(in[i + 2]);
                if (h < 0 || l < 0) return false;
                out.push_back((char)(h * 16 + l));
                i += 2;
            } else if (in[i] == '+') {
                out.push_back(' ');
            } else {
                out.push_back(in[i]);
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool valid_secret(const std::string& s) {
    if (s.size() < 8) return false;
    for (char c : s) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7') || c == '=';
        if (!ok) return false;
    }
    return true;
}

bool parse_ulong(const std::string& s, unsigned long& v) {
    if (s.empty()) return false;
    v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (unsigned long)(c - '0');
        if (v > 1000000000UL) return false;
    }
    return true;
}

}  // namespace

bool parse_otpauth(const std::string& text, std::map<std::string, std::string>& out) {
    std::map<std::string, std::string> tmp;
    const std::string pre = "otpauth://";
    if (text.size() <= pre.size()) return false;
    for (size_t i = 0; i < pre.size(); ++i) {
        char a = text[i], b = pre[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (a != b) return false;
    }
    try {
        std::string rest = text.substr(pre.size());
        size_t slash = rest.find('/');
        std::string type =
            lower_of(slash == std::string::npos ? rest : rest.substr(0, slash));
        if (type != "totp" && type != "hotp") return false;
        std::string label_enc, query;
        if (slash == std::string::npos) {
            label_enc = "";
        } else {
            std::string after = rest.substr(slash + 1);
            size_t q = after.find('?');
            label_enc = (q == std::string::npos) ? after : after.substr(0, q);
            query = (q == std::string::npos) ? "" : after.substr(q + 1);
        }
        std::string label;
        if (!url_decode(label_enc, label) || label.empty()) return false;
        // Split issuer prefix off the label.
        std::string account = label, issuer;
        size_t colon = label.find(':');
        if (colon != std::string::npos) {
            issuer = label.substr(0, colon);
            account = label.substr(colon + 1);
        }
        // Query params (lowercased keys).
        std::map<std::string, std::string> qm;
        if (!query.empty()) {
            size_t i = 0;
            while (i <= query.size()) {
                size_t e = query.find('&', i);
                std::string pair = (e == std::string::npos)
                                       ? query.substr(i)
                                       : query.substr(i, e - i);
                size_t eq = pair.find('=');
                std::string k = lower_of(eq == std::string::npos
                                             ? pair
                                             : pair.substr(0, eq));
                std::string vraw = (eq == std::string::npos)
                                       ? ""
                                       : pair.substr(eq + 1);
                std::string v;
                if (!url_decode(vraw, v)) return false;
                qm[k] = v;
                if (e == std::string::npos) break;
                i = e + 1;
            }
        }
        auto get = [&](const char* k) -> std::string {
            auto it = qm.find(k);
            return it == qm.end() ? "" : it->second;
        };
        // Secret (required): strip spaces, uppercase, validate.
        std::string secret;
        for (char c : get("secret"))
            if (c != ' ' && c != '\t') secret.push_back(c);
        secret = upper_of(secret);
        if (!valid_secret(secret)) return false;
        if (!get("issuer").empty()) issuer = get("issuer");
        std::string algo =
            get("algorithm").empty() ? "SHA1" : upper_of(get("algorithm"));
        if (algo != "SHA1" && algo != "SHA256" && algo != "SHA512")
            return false;
        std::string digits = get("digits").empty() ? "6" : get("digits");
        if (digits != "6" && digits != "8") return false;
        tmp["payload"] = "otpauth";
        tmp["otp.type"] = type;
        tmp["otp.label"] = label;
        tmp["otp.account"] = account;
        if (!issuer.empty()) tmp["otp.issuer"] = issuer;
        tmp["otp.secret"] = secret;
        tmp["otp.algorithm"] = algo;
        tmp["otp.digits"] = digits;
        if (type == "totp") {
            std::string period = get("period").empty() ? "30" : get("period");
            unsigned long pv = 0;
            if (!parse_ulong(period, pv) || pv == 0) return false;
            tmp["otp.period"] = period;
        } else {
            unsigned long cv = 0;
            if (!parse_ulong(get("counter"), cv)) return false;
            tmp["otp.counter"] = get("counter");
        }
        out.insert(tmp.begin(), tmp.end());
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace payload
}  // namespace omniscan
