// USPS Intelligent Mail decoder (M4b). See imb.h / usps_imb.md.
#include "imb.h"
#include <cstdint>
#include "imb_tables.h"

namespace omniscan {
namespace postal {
namespace {

// Portable 128-bit bignum (4x32 limbs, little-endian). All ops by small ints.
struct Big4 {
    uint32_t w[4] = {0, 0, 0, 0};
};

void big_mul_small(Big4& a, uint32_t m) {
    uint64_t carry = 0;
    for (int i = 0; i < 4; ++i) {
        uint64_t v = (uint64_t)a.w[i] * m + carry;
        a.w[i] = (uint32_t)v;
        carry = v >> 32;
    }
}

void big_add_small(Big4& a, uint32_t v) {
    uint64_t s = (uint64_t)a.w[0] + v;
    a.w[0] = (uint32_t)s;
    uint64_t carry = s >> 32;
    for (int i = 1; i < 4 && carry; ++i) {
        s = (uint64_t)a.w[i] + carry;
        a.w[i] = (uint32_t)s;
        carry = s >> 32;
    }
}

uint32_t big_divmod_small(Big4& a, uint32_t d) {
    uint64_t rem = 0;
    for (int i = 3; i >= 0; --i) {
        uint64_t v = (rem << 32) | a.w[i];
        a.w[i] = (uint32_t)(v / d);
        rem = v % d;
    }
    return (uint32_t)rem;
}

bool big_is_zero(const Big4& a) {
    return (a.w[0] | a.w[1] | a.w[2] | a.w[3]) == 0;
}

// CRC-11, MSB-first over 102 bits (poly 0x0F35, init 0x07FF), per USPS-B-3200.
unsigned imb_crc11(const Big4& acc) {
    // Bits MSB-first: bit 101 down to bit 0.
    uint16_t crc = 0x07FF;
    for (int i = 101; i >= 0; --i) {
        unsigned bit = (acc.w[i / 32] >> (i % 32)) & 1u;
        bool fb = (((crc >> 10) & 1u) ^ bit) != 0;
        crc = (uint16_t)(((crc << 1) & 0x7FF) ^ (fb ? 0x735u : 0u));
    }
    return crc;
}

// Character -> codeword reverse map (built once). -1 = not a member.
const int16_t* imb_reverse() {
    static int16_t rev[8192];
    static bool done = false;
    if (!done) {
        for (int i = 0; i < 8192; ++i) rev[i] = -1;
        for (int c = 0; c < 1287; ++c) rev[kImbI[c]] = (int16_t)c;
        for (int c = 0; c < 78; ++c) rev[kImbII[c]] = (int16_t)(1287 + c);
        done = true;
    }
    return rev;
}

}  // namespace

bool parse_imb(const std::vector<Bar4>& bars, std::string& text) {
    text.clear();
    if (bars.size() != 65) return false;
    // Two 65-bit fields from bar states (see usps_imb.md for the mapping).
    // compA = 1 iff Full/Desc; compB = 1 iff Full/Asc.
    unsigned compA[65], compB[65];
    for (int i = 0; i < 65; ++i) {
        Bar4 s = bars[i];
        compA[i] = (s == Bar4::Full || s == Bar4::Desc) ? 1u : 0u;
        compB[i] = (s == Bar4::Full || s == Bar4::Asc) ? 1u : 0u;
    }
    const int16_t* rev = imb_reverse();
    unsigned codeword[10];
    unsigned crc = 0;
    for (int i = 0; i < 10; ++i) {
        unsigned v = 0;
        for (int j = 0; j < 13; ++j) {
            int pos = (int)kImbIV[13 * i + j] - 1;  // 0..129
            if (pos < 0 || pos > 129) return false;
            unsigned bit = (pos < 65) ? compA[pos] : compB[pos - 65];
            v |= bit << j;
        }
        int cw = (v < 8192) ? rev[v] : -1;
        if (cw >= 0) {
            codeword[i] = (unsigned)cw;  // as transmitted
        } else if (v < 8192 && rev[0x1FFF - v] >= 0) {
            codeword[i] = (unsigned)rev[0x1FFF - v];  // complemented
            crc |= 1u << i;                           // CRC bits 0..9
        } else {
            return false;  // neither member: bar error
        }
    }
    // Position 9 (J, doubled to <= 1270) always comes from Table I.
    // Position 0 may come from Table II: natural c0 tops out ~658, so the
    // +659 offset for CRC bit 10 can push it past 1286 (e.g. max routing).
    if (codeword[9] > 1286) return false;
    if (codeword[9] & 1u) return false;  // J is doubled on encode
    codeword[9] /= 2;
    if (codeword[9] > 635) return false;
    // CRC bit 10 via the +659 offset (natural range tops out ~658).
    if (codeword[0] >= 659) {
        crc |= 1u << 10;
        codeword[0] -= 659;
    }
    // Rebuild the accumulator.
    Big4 acc;
    big_add_small(acc, codeword[0]);
    for (int j = 1; j <= 8; ++j) {
        big_mul_small(acc, 1365);
        big_add_small(acc, codeword[j]);
    }
    big_mul_small(acc, 636);
    big_add_small(acc, codeword[9]);
    // Verify CRC-11 over the 102-bit field.
    if (imb_crc11(acc) != crc) return false;
    // Unpack fields: 18x ÷10, ÷5, ÷10 (copy first: divmod mutates).
    Big4 f = acc;
    char t[20];
    for (int i = 19; i >= 2; --i) t[i] = (char)('0' + big_divmod_small(f, 10));
    char t1 = (char)('0' + big_divmod_small(f, 5));
    char t0 = (char)('0' + big_divmod_small(f, 10));
    std::string tracking;
    tracking.push_back(t0);
    tracking.push_back(t1);
    for (int i = 2; i < 20; ++i) tracking.push_back(t[i]);
    // Routing by disjoint ranges (see usps_imb.md). R' fits 12 digits.
    std::string routing;
    {
        Big4 g = f;
        uint64_t rval = 0, mult = 1;
        for (int k = 0; k < 12; ++k) {
            uint32_t d = big_divmod_small(g, 10);
            rval += (uint64_t)d * mult;
            mult *= 10;
        }
        if (!big_is_zero(g)) return false;  // R' > 999999999999
        if (rval == 0) {
            routing = "";
        } else if (rval <= 100000ULL) {
            std::string digits = std::to_string(rval - 1);
            if (digits.size() > 5) return false;
            routing = std::string(5 - digits.size(), '0') + digits;
        } else if (rval <= 1000100000ULL) {
            std::string digits = std::to_string(rval - 100001ULL);
            if (digits.size() > 9) return false;
            routing = std::string(9 - digits.size(), '0') + digits;
        } else if (rval <= 101000100000ULL) {
            std::string digits = std::to_string(rval - 1000100001ULL);
            if (digits.size() > 11) return false;
            routing = std::string(11 - digits.size(), '0') + digits;
        } else {
            return false;
        }
    }
    try {
        text = tracking;
        if (!routing.empty()) {
            text.push_back('-');
            text += routing;
        }
    } catch (...) {
        return false;
    }
    return text.size() == 20 || text.size() == 26 || text.size() == 30 ||
           text.size() == 32;
}

}  // namespace postal
}  // namespace omniscan
