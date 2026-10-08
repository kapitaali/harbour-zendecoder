// MaxiCode value-level codec: code sets A-E, state machine, NS, ECI.
// Derived black-box from zint 2.16.0.9 `--verbose` dumps; see
// docs/formats/maxicode.md for the derivation and verification counts.
// Nothing here reproduces a paid spec.
#include "maxicode.h"

#include <array>
#include <cstdio>

#include "maxicode_tables.h"

namespace omniscan {
namespace maxicode {
namespace {

// Reverse lookup: byte -> (set, codeword) for encoding. Built from the
// generated forward tables (first match wins; sets A/B share lower-case
// values with different bytes, so the table is per-set already).
int find_in_set(int set, int byte) {
    const int* t = kSets[set];
    for (int cw = 0; cw < 64; ++cw)
        if (t[cw] == byte) return cw;
    return -1;
}

constexpr int kPadABCD = 33;
constexpr int kPadE = 28;

}  // namespace

bool ns_pack(const std::string& digits9, std::vector<int>& out5) {
    out5.clear();
    if (digits9.size() != 9) return false;
    long long v = 0;
    for (char c : digits9) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    if (v >= 1000000000LL) return false;
    // 30-bit big-endian, 6 bits per codeword, MSB first.
    for (int i = 4; i >= 0; --i) out5.push_back((int)((v >> (6 * i)) & 63));
    return true;
}

bool ns_unpack(const std::vector<int>& in5, std::string& digits9) {
    digits9.clear();
    if (in5.size() != 5) return false;
    long long v = 0;
    for (int c : in5) {
        if (c < 0 || c > 63) return false;
        v = (v << 6) | c;
    }
    if (v >= 1000000000LL) return false;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%09lld", v);
    digits9 = buf;
    return true;
}

std::vector<int> eci_encode(int value) {
    std::vector<int> out;
    if (value <= 0 || value > 999999) return out;  // 0 = no-op; >max invalid
    if (value < 32) {
        out.push_back(value);
    } else if (value < 1024) {
        out.push_back(32 + (value >> 6));
        out.push_back(value & 63);
    } else if (value < 32768) {
        out.push_back(48 + (value >> 12));
        out.push_back((value >> 6) & 63);
        out.push_back(value & 63);
    } else {
        out.push_back(56 + (value >> 18));
        out.push_back((value >> 12) & 63);
        out.push_back((value >> 6) & 63);
        out.push_back(value & 63);
    }
    return out;
}

int eci_decode(const std::vector<int>& cws, int start, int& value) {
    value = 0;
    if (start < 0 || start >= (int)cws.size()) return 0;
    int b0 = cws[start];
    int n, hi;
    if (b0 < 32) {
        value = b0;
        return 1;
    } else if (b0 < 48) {
        n = 2;
        hi = b0 - 32;
    } else if (b0 < 56) {
        n = 3;
        hi = b0 - 48;
    } else {
        n = 4;
        hi = b0 - 56;
    }
    if (start + n > (int)cws.size()) return 0;
    long long v = hi;
    for (int i = 1; i < n; ++i) {
        int c = cws[start + i];
        if (c < 0 || c > 63) return 0;
        v = (v << 6) | c;
    }
    value = (int)v;
    return n;
}

bool decode_data(const std::vector<int>& cws, int start, int end,
                 std::string& text) {
    text.clear();
    if (start < 0 || end > (int)cws.size() || start > end) return false;
    int set = 0;           // current set: 0=A 1=B 2=C 3=D 4=E
    bool run_latched = false;
    for (int i = start; i < end; ++i) {
        int c = cws[i];
        if (c < 0 || c > 63) return false;
        // Pads end the data region. cw 33 is a pad only in states A/B:
        // in latched-C/D state it is DATA (220/252 — the oracle emits
        // one-shot 60,33 / 61,33 and C/D latches containing 33, always
        // exiting with 58 before padding, so a 33 read while latched can
        // only be data). In E, pads are 28s.
        if (((set == 0 || set == 1) && c == kPadABCD) ||
            (set == 4 && c == kPadE))
            break;
        // ECI lead.
        if (c == 27) {
            int val = 0;
            int n = eci_decode(cws, i + 1, val);
            if (n == 0) return false;
            i += n;
            continue;
        }
        // NS numeric pack (emitted in set A).
        if (c == 31 && set == 0) {
            if (i + 5 >= end) return false;
            std::vector<int> five(cws.begin() + i + 1, cws.begin() + i + 6);
            std::string d;
            if (!ns_unpack(five, d)) return false;
            text += d;
            i += 5;
            continue;
        }
        // Multi-char shift to set A (emitted in set B): 56 + 2 set-A
        // values, 57 + 3 set-A values. Measured: `ab12cd` -> [63,1,2,56,
        // 49,50,3,4]; `ab123cd` -> [63,1,2,57,49,50,51,3,4]; and letters
        // occur too (`Kg~WW...` -> [...,56,23,23,...] = 'W','W'), so the
        // values are general set-A codewords, not digits.
        if ((c == 56 || c == 57) && set == 1) {
            int n = (c == 56) ? 2 : 3;
            if (i + n >= end) return false;
            for (int k = 1; k <= n; ++k) {
                int nc = cws[i + k];
                if (nc < 0 || nc > 63) return false;
                int byte = kSets[0][nc];
                if (byte < 0) return false;
                text.push_back((char)byte);
            }
            i += n;
            continue;
        }
        // A<->B latch: exit any C/D/E run first (run latches always
        // return to A — measured: a D-latch opened from B still exits to
        // A), then toggle. A 63 inside a run therefore always lands on B;
        // unlatched behavior is unchanged (A->B, B->A).
        if (c == 63) {
            if (run_latched) {
                set = 0;
                run_latched = false;
            }
            set = (set == 0) ? 1 : 0;
            continue;
        }
        // 59 = one-shot A<->B, but ONLY when unlatched: inside a C/D/E
        // run latch, 59 is DATA (space — measured in-latch in all three
        // sets, including doubled 59,59 = two spaces). 60/61/62 always
        // shift, from any state (one-shot operands reach into latched
        // sets: high_mix).
        if (c == 59 && !run_latched) {
            int target = (set == 0) ? 1 : 0;
            if (i + 1 >= end) return false;
            int nc = cws[i + 1];
            if (nc < 0 || nc > 63) return false;
            int byte = kSets[target][nc];
            if (byte < 0) return false;
            text.push_back((char)byte);
            ++i;
            continue;
        }
        if (c == 60 || c == 61 || c == 62) {
            int target = c - 58;
            if (i + 1 < end && cws[i + 1] == c) {
                run_latched = true;
                set = target;
                ++i;
                continue;
            }
            if (i + 1 >= end) return false;
            int nc = cws[i + 1];
            if (nc < 0 || nc > 63) return false;
            int byte = kSets[target][nc];
            if (byte < 0) return false;
            text.push_back((char)byte);
            ++i;
            continue;
        }
        // C/D/E run exit: run latches always return to A (measured: a
        // D-latch opened from B still exits to A — the continuation reads
        // in A, not in the latch base).
        if (c == 58 && run_latched) {
            set = 0;
            run_latched = false;
            continue;
        }
        int byte = kSets[set][c];
        if (byte < 0) return false;
        text.push_back((char)byte);
    }
    return true;
}

bool encode_data(const std::string& text, std::vector<int>& cws) {
    cws.clear();
    // State-tracking encoder: it emits valid codewords the decoder reads
    // back (not an oracle-faithful density optimizer — the same contract
    // the Codablock F test encoder uses). `cur` stays in {A,B}; C/D/E are
    // reached by one-shot shifts, which revert, so they never become the
    // current set.
    int cur = 0;  // 0=A, 1=B
    for (unsigned char b : text) {
        int k = find_in_set(cur, (int)b);
        if (k >= 0) {
            cws.push_back(k);
            continue;
        }
        int other = cur ^ 1;
        k = find_in_set(other, (int)b);
        if (k >= 0) {
            cws.push_back(63);  // A<->B latch
            cws.push_back(k);
            cur = other;
            continue;
        }
        // One-shot to C/D/E (reverts after one char).
        bool done = false;
        for (int s = 2; s <= 4 && !done; ++s) {
            int q = find_in_set(s, (int)b);
            if (q >= 0) {
                cws.push_back(58 + s);  // 60/61/62
                cws.push_back(q);
                done = true;
            }
        }
        if (!done) return false;
    }
    return true;
}

}  // namespace maxicode
}  // namespace omniscan
