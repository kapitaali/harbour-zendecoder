// MSI / MSI Plessey decoder (M3). See docs/formats/msi.md.
// Run vector: alternating bar,space,... starting+ending with bar, quanta {1,2}.
#include "codecs.h"

namespace omniscan {
namespace linear {
namespace {

// Read npairs (bar,space) pairs at q[pos..] into bits ('1' = wide bar).
// Each pair must sum to 3 modules with bar quantum 1 or 2.
bool pair_bits(const std::vector<int>& q, int pos, int npairs,
               std::string& bits) {
    bits.clear();
    for (int k = 0; k < npairs; ++k) {
        int b = q[pos + 2 * k], s = q[pos + 2 * k + 1];
        if (b + s != 3 || (b != 1 && b != 2)) return false;
        bits.push_back(b == 2 ? '1' : '0');
    }
    return true;
}

}  // namespace

int msi_luhn_check(const std::string& data) {
    if (data.empty()) return -1;
    int sum = 0, pos = 1;
    for (auto it = data.rbegin(); it != data.rend(); ++it, ++pos) {
        if (*it < '0' || *it > '9') return -1;
        int d = *it - '0';
        if (pos % 2 == 1) {  // double rightmost data digit first
            d *= 2;
            if (d > 9) d -= 9;
        }
        sum += d;
    }
    return (10 - (sum % 10)) % 10;
}

bool msi_luhn_valid(const std::string& full) {
    if (full.empty()) return false;
    int sum = 0, pos = 1;
    for (auto it = full.rbegin(); it != full.rend(); ++it, ++pos) {
        if (*it < '0' || *it > '9') return false;
        int d = *it - '0';
        if (pos % 2 == 0) {
            d *= 2;
            if (d > 9) d -= 9;
        }
        sum += d;
    }
    return sum % 10 == 0;
}

int msi_mod11_check(const std::string& data, int wrap) {
    if (data.empty() || (wrap != 7 && wrap != 9)) return -1;
    int sum = 0, w = 2;
    for (auto it = data.rbegin(); it != data.rend(); ++it) {
        if (*it < '0' || *it > '9') return -1;
        sum += (*it - '0') * w;
        if (++w > wrap) w = 2;
    }
    return (11 - (sum % 11)) % 11;  // 10 => two-char "10" expansion (zint)
}

bool msi_mod11_valid(const std::string& full, int wrap) {
    if (full.size() < 2) return false;
    std::string data = full.substr(0, full.size() - 1);
    int c = msi_mod11_check(data, wrap);
    return c >= 0 && c <= 9 && (full.back() - '0') == c;
}

bool parse_msi(const std::vector<int>& runs, std::string& digits,
               std::string& check_scheme, MsiCheck only) {
    digits.clear();
    check_scheme.clear();
    // runs alternate bar,space,... starting with bar. Quanta already {1,2}.
    if (runs.size() < 2 + 8 + 3) return false;  // start + 1 digit + stop
    for (int w : runs)
        if (w != 1 && w != 2) return false;
    int n = static_cast<int>(runs.size());
    int pos = 0;
    // Start: bar2, space1.
    if (runs[0] != 2 || runs[1] != 1) return false;
    pos = 2;
    // Digits: 8 runs (4 pairs) each, pairs summing to 3, then Stop.
    std::string all;
    for (;;) {
        // Stop [1,2,1] terminates exactly at the end of ink.
        if (pos + 3 <= n && runs[pos] == 1 && runs[pos + 1] == 2 &&
            runs[pos + 2] == 1 && pos + 3 == n) {
            pos += 3;
            break;
        }
        if (pos + 8 > n) return false;
        std::string bits;
        if (!pair_bits(runs, pos, 4, bits)) return false;
        int val = (bits[0] - '0') * 8 + (bits[1] - '0') * 4 +
                  (bits[2] - '0') * 2 + (bits[3] - '0');
        if (val > 9) return false;
        all.push_back(static_cast<char>('0' + val));
        pos += 8;
    }
    if (pos != n || all.empty()) return false;

    // Check schemes: gather every validating reading in preference order
    // (exact "10"-expansions, 1010, 1110, 10, 11, NCR), then select by the
    // caller's restriction (Auto keeps the first = documented preference).
    struct Reading {
        std::string digits;
        const char* scheme;
        MsiCheck id;
    };
    std::vector<Reading> cands;
    auto consider = [&](std::string d, const char* s, MsiCheck id) {
        try {
            cands.push_back({std::move(d), s, id});
        } catch (...) {
        }
    };
    // Mod-11 "10" expansion: data + "10" where mod11(data) == 10 (any wrap).
    if (all.size() >= 3 && all[all.size() - 2] == '1' &&
        all[all.size() - 1] == '0') {
        std::string data = all.substr(0, all.size() - 2);
        if (msi_mod11_check(data, 7) == 10)
            consider(data, "mod11", MsiCheck::Mod11);
        else if (msi_mod11_check(data, 9) == 10)
            consider(data, "mod11ncr", MsiCheck::Mod11Ncr);
    }
    // Mod-1110 "10" expansion: data + "10" + c2, c2 = luhn(data+"10").
    if (all.size() >= 4) {
        std::string prefix = all.substr(0, all.size() - 1);
        int c2 = msi_luhn_check(prefix);
        if (c2 >= 0 && all.back() - '0' == c2 && prefix.size() >= 3 &&
            prefix[prefix.size() - 2] == '1' &&
            prefix[prefix.size() - 1] == '0') {
            std::string data = prefix.substr(0, prefix.size() - 2);
            if (msi_mod11_check(data, 7) == 10)
                consider(data, "mod1110", MsiCheck::Mod1110);
            else if (msi_mod11_check(data, 9) == 10)
                consider(data, "mod1110ncr", MsiCheck::Mod1110Ncr);
        }
    }
    // Mod 1010: c1 = luhn(data), c2 = luhn(data+c1).
    if (all.size() >= 3) {
        std::string data = all.substr(0, all.size() - 2);
        int c1 = msi_luhn_check(data);
        if (c1 >= 0 && all[all.size() - 2] - '0' == c1) {
            int c2 = msi_luhn_check(data + static_cast<char>('0' + c1));
            if (c2 >= 0 && all.back() - '0' == c2)
                consider(data, "mod1010", MsiCheck::Mod1010);
        }
        // Mod 1110: c1 = mod11(data), c2 = luhn(data+c1). IBM then NCR.
        for (int wrap = 7; wrap <= 9; wrap += 2) {
            int m1 = msi_mod11_check(data, wrap);
            if (m1 >= 0 && m1 <= 9 && all[all.size() - 2] - '0' == m1) {
                int c2 = msi_luhn_check(data + static_cast<char>('0' + m1));
                if (c2 >= 0 && all.back() - '0' == c2)
                    consider(data, (wrap == 7) ? "mod1110" : "mod1110ncr",
                             (wrap == 7) ? MsiCheck::Mod1110
                                         : MsiCheck::Mod1110Ncr);
            }
        }
    }
    if (msi_luhn_valid(all))
        consider(all.substr(0, all.size() - 1), "mod10", MsiCheck::Mod10);
    if (msi_mod11_valid(all, 7))
        consider(all.substr(0, all.size() - 1), "mod11", MsiCheck::Mod11);
    if (msi_mod11_valid(all, 9))
        consider(all.substr(0, all.size() - 1), "mod11ncr", MsiCheck::Mod11Ncr);
    for (Reading& r : cands) {
        if (only == MsiCheck::Auto || r.id == only) {
            digits = r.digits;
            check_scheme = r.scheme;
            return true;
        }
    }
    return false;
}

}  // namespace linear
}  // namespace omniscan
