// VIN check + fields (M7). Transliteration/weights per ISO 3779 / 49 CFR
// 565; 30-year year cycle reported as both candidates (deterministic).
// No region table (kept out deliberately — see docs/payload.md).
// No exceptions escape.
#include "omniscan/payload/vin.h"
#include <string>

namespace omniscan {
namespace payload {
namespace {

int transliterate(char c) {
    switch (c) {
        case 'A':
            return 1;
        case 'B':
            return 2;
        case 'C':
            return 3;
        case 'D':
            return 4;
        case 'E':
            return 5;
        case 'F':
            return 6;
        case 'G':
            return 7;
        case 'H':
            return 8;
        case 'J':
            return 1;
        case 'K':
            return 2;
        case 'L':
            return 3;
        case 'M':
            return 4;
        case 'N':
            return 5;
        case 'P':
            return 7;
        case 'R':
            return 9;
        case 'S':
            return 2;
        case 'T':
            return 3;
        case 'U':
            return 4;
        case 'V':
            return 5;
        case 'W':
            return 6;
        case 'X':
            return 7;
        case 'Y':
            return 8;
        case 'Z':
            return 9;
        default:
            return (c >= '0' && c <= '9') ? c - '0' : -1;
    }
}

const int kWeights[17] = {8, 7, 6, 5, 4, 3, 2, 10, 0,
                          9, 8, 7, 6, 5,  4,  3,  2};

const char kYears[] = "ABCDEFGHJKLMNPRSTVWXY123456789";

}  // namespace

bool validate_vin(const std::string& text) {
    if (text.size() != 17) return false;
    int sum = 0;
    for (int i = 0; i < 17; ++i) {
        char c = text[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c == 'I' || c == 'O' || c == 'Q') return false;
        int v = transliterate(c);
        if (v < 0) return false;
        sum += v * kWeights[i];
    }
    int check = sum % 11;
    char want = (check == 10) ? 'X' : (char)('0' + check);
    char got = text[8];
    if (got >= 'a' && got <= 'z') got = (char)(got - 32);
    return got == want;
}

bool parse_vin(const std::string& text, std::map<std::string, std::string>& out) {
    if (!validate_vin(text)) return false;
    try {
        std::string v = text;
        for (char& c : v)
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        // Serial: last 5 must be numeric.
        for (int i = 12; i < 17; ++i)
            if (v[i] < '0' || v[i] > '9') return false;
        int year_idx = -1;
        for (int i = 0; kYears[i]; ++i)
            if (kYears[i] == v[9]) year_idx = i;
        if (year_idx < 0) return false;
        out["payload"] = "vin";
        out["vin.wmi"] = v.substr(0, 3);
        out["vin.year_code"] = std::string(1, v[9]);
        out["vin.year_a"] = std::to_string(1980 + year_idx);
        out["vin.year_b"] = std::to_string(2010 + year_idx);
        out["vin.plant"] = std::string(1, v[10]);
        out["vin.serial"] = v.substr(11, 6);
        out["vin.check"] = std::string(1, v[8]);
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace payload
}  // namespace omniscan
