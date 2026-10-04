// Swiss QR bill schema validation (M5 payload). Element layout per SIX
// QR-bill Implementation Guidelines (v2.x): SPC, 0200, coding, IBAN,
// creditor 7, ultimate-creditor 7, amount, currency, ultimate-debtor 7,
// ref-type, reference, message, EPD trailer, optional bill information.
// QR-reference mod-10 recursive per the ISR lineage; SCOR/IBAN mod-97 per
// ISO 13616/11649. No exceptions escape.
#include "omniscan/payload/swissqr.h"
#include <vector>

namespace omniscan {
namespace payload {
namespace {

bool iban_mod97_ok(const std::string& iban) {
    // Rearrange + expand + mod 97 == 1. Letters A=10..Z=35.
    std::string digits;
    try {
        std::string rear = iban.substr(4) + iban.substr(0, 4);
        for (char c : rear) {
            if (c >= '0' && c <= '9') {
                digits.push_back(c);
            } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                char u = (c >= 'a') ? (char)(c - 32) : c;
                std::string v = std::to_string(u - 'A' + 10);
                digits += v;
            } else {
                return false;
            }
        }
        int rem = 0;
        for (char c : digits) rem = (rem * 10 + (c - '0')) % 97;
        return rem == 1;
    } catch (...) {
        return false;
    }
}

// Mod-10 recursive (ISR/QR-reference lineage). Table + procedure as used
// by the orange ISR slips.
bool qr_ref_ok(const std::string& ref) {
    static const int kTab[10] = {0, 9, 4, 6, 8, 2, 7, 1, 3, 5};
    if (ref.size() != 27) return false;
    for (char c : ref)
        if (c < '0' || c > '9') return false;
    int carry = 0;
    for (int i = 0; i < 26; ++i) carry = kTab[(carry + (ref[i] - '0')) % 10];
    return (10 - carry) % 10 == (ref[26] - '0');
}

bool scor_ok(const std::string& ref) {
    if (ref.size() < 5 || ref.size() > 25) return false;
    if (ref[0] != 'R' || ref[1] != 'F') return false;
    for (size_t i = 2; i < ref.size(); ++i) {
        char c = ref[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
        if (!ok) return false;
    }
    return iban_mod97_ok(ref);
}

bool valid_amount(const std::string& a) {
    if (a.empty()) return true;  // open amount
    size_t dot = a.find('.');
    std::string whole = (dot == std::string::npos) ? a : a.substr(0, dot);
    std::string frac = (dot == std::string::npos) ? "" : a.substr(dot + 1);
    if (whole.empty() || whole.size() > 9) return false;
    for (char c : whole)
        if (c < '0' || c > '9') return false;
    if (!frac.empty() && (frac.size() > 2)) return false;
    for (char c : frac)
        if (c < '0' || c > '9') return false;
    if (dot != std::string::npos && frac.empty()) return false;
    return true;
}

bool valid_country(const std::string& c) {
    if (c.size() != 2) return false;
    return ((c[0] >= 'A' && c[0] <= 'Z') || (c[0] >= 'a' && c[0] <= 'z')) &&
           ((c[1] >= 'A' && c[1] <= 'Z') || (c[1] >= 'a' && c[1] <= 'z'));
}

// Address block of 7 (type + 6 fields) at el[pos..]: all-empty (unused) or
// type S|K with non-empty name. Lengths enforced by the caller table.
bool valid_address(const std::vector<std::string>& el, size_t pos,
                   bool required, std::string& name) {
    static const size_t kMax[6] = {70, 70, 16, 16, 35, 2};
    bool all_empty = true;
    for (size_t k = 0; k < 7; ++k)
        if (!el[pos + k].empty()) all_empty = false;
    if (all_empty) return !required;
    const std::string& type = el[pos];
    if (type != "S" && type != "K") return false;
    if (el[pos + 1].empty()) return false;  // name required when used
    for (size_t k = 0; k < 6; ++k)
        if (el[pos + 1 + k].size() > kMax[k]) return false;
    if (!el[pos + 6].empty() && !valid_country(el[pos + 6])) return false;
    name = el[pos + 1];
    return true;
}

}  // namespace

bool validate_iban(const std::string& iban) {
    if (iban.size() < 15 || iban.size() > 32) return false;
    for (char c : iban) {
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z');
        if (!ok) return false;
    }
    return iban_mod97_ok(iban);
}

bool parse_swissqr(const std::string& text, std::map<std::string, std::string>& out) {
    std::map<std::string, std::string> tmp;
    try {
        // Split lines (CRLF/LF/CR all accepted).
        std::vector<std::string> el;
        std::string cur;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r') continue;
            if (text[i] == '\n') {
                el.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(text[i]);
            }
        }
        el.push_back(cur);
        while (!el.empty() && el.back().empty()) el.pop_back();
        if (el.size() != 31 && el.size() != 32) return false;
        if (el[0] != "SPC" || el[1] != "0200" || el[2] != "1") return false;
        const std::string& iban = el[3];
        if (iban.size() != 21) return false;
        if ((iban[0] != 'C' || iban[1] != 'H') &&
            (iban[0] != 'L' || iban[1] != 'I'))
            return false;
        if (!iban_mod97_ok(iban)) return false;
        // QR-IID range 30000-31999 (positions 5-9 of the IBAN).
        bool qr_iban = iban.substr(4, 5) >= "30000" && iban.substr(4, 5) <= "31999";
        std::string cred_name;
        if (!valid_address(el, 4, true, cred_name)) return false;
        if (el[8].empty() || el[9].empty() || el[10].empty()) return false;
        if (!valid_country(el[10])) return false;
        std::string ucred;
        if (!valid_address(el, 11, false, ucred)) return false;
        if (!valid_amount(el[18])) return false;
        if (el[19] != "CHF" && el[19] != "EUR") return false;
        std::string udebt;
        if (!valid_address(el, 20, false, udebt)) return false;
        const std::string& rt = el[27];
        const std::string& ref = el[28];
        if (rt == "QRR") {
            if (!qr_iban || !qr_ref_ok(ref)) return false;
        } else if (rt == "SCOR") {
            if (qr_iban || !scor_ok(ref)) return false;
        } else if (rt == "NON") {
            if (!ref.empty()) return false;
        } else {
            return false;
        }
        if (el[29].size() > 140) return false;
        std::string billinfo;
        if (el.size() == 32) {
            if (el[30] != "EPD") return false;
            if (el[31].size() > 140) return false;
            billinfo = el[31];
        } else {
            if (el[30] != "EPD") return false;
        }
        tmp["payload"] = "swissqr";
        tmp["swissqr.iban"] = iban;
        if (!el[18].empty()) tmp["swissqr.amount"] = el[18];
        tmp["swissqr.currency"] = el[19];
        tmp["swissqr.ref_type"] = rt;
        if (!ref.empty()) tmp["swissqr.reference"] = ref;
        tmp["swissqr.creditor"] = cred_name;
        if (!udebt.empty()) tmp["swissqr.debtor"] = udebt;
        if (!el[29].empty()) tmp["swissqr.message"] = el[29];
        if (!billinfo.empty()) tmp["swissqr.billinfo"] = billinfo;
        out.insert(tmp.begin(), tmp.end());
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace payload
}  // namespace omniscan
