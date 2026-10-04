// Australia Post 4-state decoder (M4b). See auspost.h / australia_post.md.
#include "auspost.h"

namespace omniscan {
namespace postal {
namespace {

// N-table digit -> 2 bars; C-table via GDSET index -> 3 bars.
const int kAusN[10][2] = {
    {0, 0}, {0, 1}, {0, 2}, {1, 0}, {1, 1},
    {1, 2}, {2, 0}, {2, 1}, {2, 2}, {3, 0},
};

const int kAusC[64][3] = {
    {2, 2, 2}, {3, 0, 0}, {3, 0, 1}, {3, 0, 2}, {3, 1, 0}, {3, 1, 1},
    {3, 1, 2}, {3, 2, 0}, {3, 2, 1}, {3, 2, 2}, {0, 0, 0}, {0, 0, 1},
    {0, 0, 2}, {0, 1, 0}, {0, 1, 1}, {0, 1, 2}, {0, 2, 0}, {0, 2, 1},
    {0, 2, 2}, {1, 0, 0}, {1, 0, 1}, {1, 0, 2}, {1, 1, 0}, {1, 1, 1},
    {1, 1, 2}, {1, 2, 0}, {1, 2, 1}, {1, 2, 2}, {2, 0, 0}, {2, 0, 1},
    {2, 0, 2}, {2, 1, 0}, {2, 1, 1}, {2, 1, 2}, {2, 2, 0}, {2, 2, 1},
    {0, 2, 3}, {0, 3, 0}, {0, 3, 1}, {0, 3, 2}, {0, 3, 3}, {1, 0, 3},
    {1, 1, 3}, {1, 2, 3}, {1, 3, 0}, {1, 3, 1}, {1, 3, 2}, {1, 3, 3},
    {2, 0, 3}, {2, 1, 3}, {2, 2, 3}, {2, 3, 0}, {2, 3, 1}, {2, 3, 2},
    {2, 3, 3}, {3, 0, 3}, {3, 1, 3}, {3, 2, 3}, {3, 3, 0}, {3, 3, 1},
    {3, 3, 2}, {3, 3, 3}, {0, 0, 3}, {0, 1, 3},
};

const char kGDSET[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz #";

// GF(64), primitive poly x^6+x+1 (0x43). Table-free.
unsigned gf_mul(unsigned a, unsigned b) {
    unsigned p = 0;
    a &= 0x3F;
    b &= 0x3F;
    while (b) {
        if (b & 1) p ^= a;
        a <<= 1;
        if (a & 0x40) a ^= 0x43;
        b >>= 1;
    }
    return p & 0x3F;
}

unsigned gf_exp(int e) {
    unsigned r = 1;
    e %= 63;
    if (e < 0) e += 63;
    for (int i = 0; i < e; ++i) r = gf_mul(r, 2);
    return r;
}

int gf_log(unsigned c) {
    c &= 0x3F;
    if (c == 0) return 0;  // matches calloc'd table behavior
    unsigned r = 1;
    for (int e = 0; e < 63; ++e) {
        if (r == c) return e;
        r = gf_mul(r, 2);
    }
    return 0;  // unreachable for c != 0
}

// N-pair (2 bars) -> digit, -1 when invalid.
int aus_nval(int b0, int b1) {
    for (int d = 0; d < 10; ++d)
        if (kAusN[d][0] == b0 && kAusN[d][1] == b1) return d;
    return -1;
}

// C-triple (3 bars) -> GDSET index. NOTE: AusCTable is NOT straight base-4
// count (unlike AusBarTable), so this must search; all 64 combos are valid.
int aus_cval(int b0, int b1, int b2) {
    for (int i = 0; i < 64; ++i)
        if (kAusC[i][0] == b0 && kAusC[i][1] == b1 && kAusC[i][2] == b2)
            return i;
    return -1;
}

}  // namespace

void auspost_rs_ecc(const uint8_t* triples, int ntriples, uint8_t ecc[4]) {
    // Generator for nsym=4, first root alpha^1 (mirrors zint's init_code).
    unsigned rspoly[5] = {1, 0, 0, 0, 0};
    int index = 1;
    for (int i = 1; i <= 4; ++i) {
        unsigned cur[5] = {0, 0, 0, 0, 0};
        unsigned ai = gf_exp(index);
        for (int k = 0; k <= i; ++k) {
            unsigned lo = (k > 0) ? rspoly[k - 1] : 0;
            unsigned hi = (k < i) ? gf_mul(rspoly[k], ai) : 0;
            cur[k] = lo ^ hi;
        }
        for (int k = 0; k <= i; ++k) rspoly[k] = cur[k];
        ++index;
    }
    int logpoly[5];
    bool zero = false;
    for (int k = 0; k <= 4; ++k) {
        logpoly[k] = gf_log(rspoly[k]);
        if (rspoly[k] == 0) zero = true;
    }
    unsigned res[4] = {0, 0, 0, 0};
    for (int i = 0; i < ntriples; ++i) {
        unsigned m = res[3] ^ (triples[i] & 0x3F);
        if (m) {
            int lm = gf_log(m);
            for (int k = 3; k > 0; --k) {
                unsigned sub =
                    (zero && rspoly[k] == 0) ? 0 : gf_exp(lm + logpoly[k]);
                res[k] = res[k - 1] ^ sub;
            }
            res[0] = gf_exp(lm + logpoly[0]);
        } else {
            res[3] = res[2];
            res[2] = res[1];
            res[1] = res[0];
            res[0] = 0;
        }
    }
    // zint reverses the result before output.
    for (int k = 0; k < 4; ++k) ecc[k] = static_cast<uint8_t>(res[3 - k]);
}

bool parse_auspost(const std::vector<Bar4>& bars, std::string& text,
                   std::string& fcc, std::string& kind) {
    text.clear();
    fcc.clear();
    kind.clear();
    int n = static_cast<int>(bars.size());
    if (n != 37 && n != 52 && n != 67) return false;
    auto st = [&](int i) { return static_cast<int>(bars[i]); };
    // Guards: [Asc,Track] both ends.
    if (st(0) != 1 || st(1) != 3 || st(n - 2) != 1 || st(n - 1) != 3)
        return false;
    // FCC: two N-digits.
    int f0 = aus_nval(st(2), st(3)), f1 = aus_nval(st(4), st(5));
    if (f0 < 0 || f1 < 0) return false;
    char fcs[3] = {static_cast<char>('0' + f0), static_cast<char>('0' + f1),
                   '\0'};
    std::string fccstr = fcs;
    if (fccstr == "00")
        kind = "null";
    else if (fccstr == "11")
        kind = "standard";
    else if (fccstr == "45")
        kind = "reply";
    else if (fccstr == "59")
        kind = "customer2";
    else if (fccstr == "62")
        kind = "customer3";
    else if (fccstr == "87")
        kind = "route";
    else if (fccstr == "92")
        kind = "redirect";
    else
        return false;
    // Length/FCC consistency.
    bool len_ok = (n == 37 && (fccstr == "00" || fccstr == "11" ||
                              fccstr == "45" || fccstr == "87" ||
                              fccstr == "92")) ||
                  (n == 52 && fccstr == "59") || (n == 67 && fccstr == "62");
    if (!len_ok) return false;
    // DPID: 8 N-digits.
    std::string dpid;
    try {
        for (int k = 0; k < 8; ++k) {
            int d = aus_nval(st(6 + 2 * k), st(6 + 2 * k + 1));
            if (d < 0) return false;
            dpid.push_back(static_cast<char>('0' + d));
        }
    } catch (...) {
        return false;
    }
    int preRS = n - 12 - 2;  // 23 / 38 / 53
    // Reed-Solomon verification over triples from bar 2.
    uint8_t triples[18];
    int ntriples = (preRS - 2) / 3;  // 7 / 12 / 17
    for (int t = 0; t < ntriples; ++t) {
        triples[t] = static_cast<uint8_t>((st(2 + 3 * t) << 4) |
                                          (st(2 + 3 * t + 1) << 2) |
                                          st(2 + 3 * t + 2));
    }
    uint8_t ecc[4];
    auspost_rs_ecc(triples, ntriples, ecc);
    for (int k = 0; k < 4; ++k) {
        int got = (st(preRS + 3 * k) << 4) | (st(preRS + 3 * k + 1) << 2) |
                  st(preRS + 3 * k + 2);
        if (got != ecc[k]) return false;
    }
    // Customer region: bars [22, preRS).
    std::string cust;
    int r0 = 22, rlen = preRS - 22;  // 1 / 16 / 31
    if (n == 37) {
        if (rlen != 1 || st(r0) != 3) return false;  // lone filler tracker
    } else {
        // N-case: greedy digit pairs, remainder all trackers, >= 1 digit.
        std::string digits;
        int k = 0;
        while (k + 2 <= rlen) {
            int d = aus_nval(st(r0 + k), st(r0 + k + 1));
            if (d < 0) break;
            digits.push_back(static_cast<char>('0' + d));
            k += 2;
        }
        bool rest_track = true;
        for (int i = k; i < rlen; ++i)
            if (st(r0 + i) != 3) rest_track = false;
        if (!digits.empty() && rest_track) {
            cust = digits;
        } else {
            // C-case: strip trailing trackers, require 3-bar groups.
            int core = rlen;
            while (core > 0 && st(r0 + core - 1) == 3) --core;
            if (core == 0 || core % 3 != 0) return false;
            try {
                for (int i = 0; i < core; i += 3) {
                    int idx = aus_cval(st(r0 + i), st(r0 + i + 1),
                                       st(r0 + i + 2));
                    if (idx < 0) return false;
                    cust.push_back(kGDSET[idx]);
                }
            } catch (...) {
                return false;
            }
        }
    }
    text = dpid + cust;
    fcc = fccstr;
    return true;
}

}  // namespace postal
}  // namespace omniscan
