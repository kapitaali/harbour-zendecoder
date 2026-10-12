// Grid Matrix native value layer (Tier-2). Ports attic/gm_rs.py
// (rs_decode, Omega j=0 fix included) and attic/gm_walk.py (walk,
// steps 5-6 + N-separator fix + U/L sfx45..50 untwin; M punct literal).
// Contract: docs/formats/grid_matrix.md. False on anything invalid;
// never throws (contract; callers cap input sizes).
#include "stacked_gridmatrix.h"

#include <string>
#include <vector>

#include "gridmatrix_levels.h"

namespace omniscan {
namespace gridmatrix {
namespace {

// GF(2^7), poly x^7+x^3+1 (0x89), alpha=2 (docs step 4).
struct GF {
    int exp[127] = {0};
    int log[128] = {0};
    GF() {
        int x = 1;
        for (int i = 0; i < 127; ++i) {
            exp[i] = x;
            log[x] = i;
            x <<= 1;
            if (x & 0x80) x ^= 0x89;
        }
    }
};
inline const GF& gf() {
    static const GF t;
    return t;
}
inline int gmul(int a, int b) {
    if (a == 0 || b == 0) return 0;
    return gf().exp[(gf().log[a] + gf().log[b]) % 127];
}
inline int ginv(int a) {  // caller ensures a != 0
    return gf().exp[(127 - gf().log[a]) % 127];
}

// ---- walker tables (attic/gm_walk.py, steps 5-6) ----
inline char ul_char(int st, int v) {  // st 0=U,1=L; v 0..26
    if (v < 26) return static_cast<char>((st == 0 ? 'A' : 'a') + v);
    return ' ';
}
inline char mch_char(int v) {  // v 0..62
    if (v < 10) return static_cast<char>('0' + v);
    if (v < 36) return static_cast<char>('A' + (v - 10));
    if (v < 62) return static_cast<char>('a' + (v - 36));
    return ' ';
}
// PUNCT[32] (sfx 32..63): k0..14=33..47, k15..21=58..64,
// k22..27=91..96, k28..31=123..126. NOTE sfx45..50 are overloaded
// (twins [/]^_` share values with ./:;<=); walker outputs literal
// PUNCT (twins decode twin-mapped -- documented encoder ambiguity).
inline const char kPunct[33] =
    "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
inline char sep_char(int slot) {  // 0..4; 5 refused by caller
    static const char s[5] = {' ', '+', '-', '.', ','};
    return s[slot];
}

}  // namespace

bool gm_rs_decode(std::vector<int>& cw, int k, int& nerr) {
    nerr = 0;
    const int n = static_cast<int>(cw.size());
    if (n <= 0 || k <= 0 || k >= n) return false;
    for (int v : cw)
        if (v < 0 || v > 127) return false;
    // Syndromes S_j = cw(a^{1+j}) (root offset b=1).
    std::vector<int> S(static_cast<size_t>(k), 0);
    for (int j = 0; j < k; ++j) {
        const int a = gf().exp[(1 + j) % 127];
        int acc = 0;
        for (int i = n - 1; i >= 0; --i) acc = gmul(acc, a) ^ cw[i];
        S[j] = acc;
    }
    bool clean = true;
    for (int s : S)
        if (s != 0) {
            clean = false;
            break;
        }
    if (clean) return true;  // untouched, 0 false corrections
    // Berlekamp-Massey -> Lambda (low-first, lam[0]=1).
    std::vector<int> lam(1, 1), prev(1, 1);
    int L = 0, m = 1, bval = 1;
    for (int i = 0; i < k; ++i) {
        int delta = S[i];
        for (int j = 1; j <= L && j < (int)lam.size(); ++j)
            delta ^= gmul(lam[j], S[i - j]);
        if (delta == 0) {
            ++m;
            continue;
        }
        const int coef = gmul(delta, ginv(bval));
        std::vector<int> oldlam = lam;
        if ((int)prev.size() + m > (int)lam.size())
            lam.resize(prev.size() + m, 0);
        for (size_t kk = 0; kk < prev.size(); ++kk)
            lam[kk + m] ^= gmul(coef, prev[kk]);
        if (2 * L <= i) {
            L = i + 1 - L;
            prev = oldlam;
            bval = delta;
            m = 1;
        } else {
            ++m;
        }
    }
    while (lam.size() > 1 && lam.back() == 0) lam.pop_back();
    // Chien: pos p errs iff Lam(a^-p)==0.
    std::vector<int> errpos;
    for (int p = 0; p < n; ++p) {
        const int x = gf().exp[(127 - (p % 127)) % 127];
        int acc = 0, xp = 1;
        for (size_t kk = 0; kk < lam.size(); ++kk) {
            if (lam[kk]) acc ^= gmul(lam[kk], xp);
            xp = gmul(xp, x);
        }
        if (acc == 0) errpos.push_back(p);
    }
    if ((int)errpos.size() != L || L == 0) return false;
    // Omega = S*Lam mod x^k (j=0..deg, Lam[0]=1 term included: step-10 fix).
    std::vector<int> omega(static_cast<size_t>(k), 0);
    for (int i = 0; i < k; ++i) {
        int acc = 0;
        for (size_t j = 0; j < lam.size(); ++j)
            if (i - (int)j >= 0) acc ^= gmul(lam[j], S[i - j]);
        omega[i] = acc;
    }
    std::vector<int> lder(lam.size(), 0);  // formal derivative (char 2)
    for (size_t kk = 1; kk < lam.size(); ++kk)
        if (kk % 2 == 1) lder[kk - 1] = lam[kk];
    for (int p : errpos) {
        const int X = gf().exp[p % 127];
        const int Xi = ginv(X);
        int oacc = 0, xp = 1;
        for (size_t kk = 0; kk < omega.size(); ++kk) {
            if (omega[kk]) oacc ^= gmul(omega[kk], xp);
            xp = gmul(xp, Xi);
        }
        int dacc = 0;
        xp = 1;
        for (size_t kk = 0; kk < lder.size(); ++kk) {
            if (lder[kk]) dacc ^= gmul(lder[kk], xp);
            xp = gmul(xp, Xi);
        }
        if (dacc == 0) return false;
        cw[p] ^= gmul(oacc, ginv(dacc));  // b=1: X^{1-b}=1
    }
    for (int j = 0; j < k; ++j) {  // re-syndrome must be clean now
        const int a = gf().exp[(1 + j) % 127];
        int acc = 0;
        for (int i = n - 1; i >= 0; --i) acc = gmul(acc, a) ^ cw[i];
        if (acc != 0) return false;
    }
    nerr = static_cast<int>(errpos.size());
    return true;
}

bool gm_walk_bits(const std::string& bits, std::string& text,
                    int& consumed) {
    text.clear();
    consumed = 0;
    const int n = static_cast<int>(bits.size());
    for (char c : bits)
        if (c != '0' && c != '1') return false;
    int i = 0;
    auto take = [&](int kk, int& out) -> bool {
        if (i + kk > n) return false;
        int v = 0;
        for (int j = 0; j < kk; ++j) v = (v << 1) + (bits[i + j] - '0');
        i += kk;
        out = v;
        return true;
    };
    auto peek = [&](int kk) -> std::string {
        if (i + kk > n) return std::string();
        return bits.substr(static_cast<size_t>(i), static_cast<size_t>(kk));
    };
    if (n < 4) return false;
    int st = -1;  // 0=U 1=L 2=M 3=N 4=B
    int tail = -1;
    const std::string t4 = peek(4);
    if (t4 == "0100") {
        st = 0;
        i += 4;
    } else if (t4 == "0011") {
        st = 1;
        i += 4;
    } else if (t4 == "0101") {
        st = 2;
        i += 4;
    } else if (t4 == "0111") {
        st = 4;
        i += 4;
    } else if (t4 == "0010") {
        st = 3;
        i += 4;
        if (!take(2, tail) || tail == 3) return false;
    } else {
        return false;
    }
    auto enter_b = [&]() -> bool {
        int cnt = 0;
        if (!take(9, cnt)) return false;
        cnt += 1;
        if (cnt > 512) return false;
        if (i + 8 * cnt > n) return false;
        for (int t = 0; t < cnt; ++t) {
            int v = 0;
            for (int j = 0; j < 8; ++j) v = (v << 1) + (bits[i + j] - '0');
            i += 8;
            text.push_back(static_cast<char>(v));
        }
        return true;
    };
    if (st == 4 && !enter_b()) return false;
    while (true) {
        if (st == 0 || st == 1) {
            int v = 0;
            if (!take(5, v)) return false;
            if (v <= 26) {
                text.push_back(ul_char(st, v));
            } else if (v == 27) {
                consumed = i;
                return true;
            } else if (v == 28) {
                return false;
            } else if (v == 29) {
                if (!take(2, tail)) return false;
                st = 3;
            } else if (v == 30) {
                st = (st == 0) ? 1 : 0;
            } else {
                int p = 0;
                if (!take(2, p)) return false;
                if (p == 0) {
                    st = 2;
                } else if (p == 1) {
                    int sfx = 0;
                    if (!take(6, sfx) || sfx < 32 || sfx > 63) return false;
                    // Literal PUNCT (no untwin): sfx45..50 are OVERLOADED
                    // (zint emits twins [/]^_` AND legitimate ./:;<= with
                    // identical values -- indistinguishable). Literal is
                    // correct for common punct; twins decode twin-mapped
                    // (documented encoder ambiguity, not decoder error).
                    text.push_back(kPunct[sfx - 32]);
                } else if (p == 2) {
                    if (!enter_b()) return false;
                    st = 4;
                } else {
                    return false;
                }
            }
        } else if (st == 2) {
            int v = 0;
            if (!take(6, v)) return false;
            if (v <= 62) {
                text.push_back(mch_char(v));
            } else {
                int c = 0;
                if (!take(4, c)) return false;
                if (c == 0) {
                    consumed = i;
                    return true;
                } else if (c == 2) {
                    if (!take(2, tail)) return false;
                    st = 3;
                } else if (c == 3) {
                    st = 1;
                } else if (c == 4) {
                    st = 0;
                } else if (c == 6) {
                    int sfx = 0;
                    if (!take(6, sfx) || sfx < 32 || sfx > 63) return false;
                    text.push_back(kPunct[sfx - 32]);  // literal (no twin)
                } else if (c == 7) {
                    if (!enter_b()) return false;
                    st = 4;
                } else {
                    return false;
                }
            }
        } else if (st == 3) {
            struct W {
                bool dig;
                int v;
            };
            std::vector<W> words;
            int next_st = -1;  // 4=END 0=L 1=U? use 10+L/U/M, 4=B, 9=END
            bool enter_b_pending = false;
            while (true) {
                int w = 0;
                if (!take(10, w)) return false;
                if (w <= 999) {
                    words.push_back({true, w});
                } else if (w <= 1017) {
                    const int slot = (w - 1000) / 3, kk = (w - 1000) % 3;
                    if (slot == 5) return false;
                    words.push_back({false, slot * 10 + kk});
                } else if (w == 1018) {
                    next_st = 9;
                    break;
                } else if (w == 1020) {
                    next_st = 1;
                    break;
                } else if (w == 1021) {
                    next_st = 0;
                    break;
                } else if (w == 1022) {
                    next_st = 2;
                    break;
                } else if (w == 1023) {
                    // DEFER the byte block. `words` holds digit and
                    // separator words read EARLIER in this N segment, and
                    // they are flushed to `text` only after this loop. If
                    // enter_b() ran here its bytes would land ahead of them
                    // and silently reorder the payload: '0123456789 .,;- '
                    // came out as '9 .,;- 012345678' (N("012345678") then
                    // B("9 .,;- ")). U/L/M never buffer, so only this path
                    // needs deferring. Found by the forced-level corpus
                    // generator, not by reasoning.
                    enter_b_pending = true;
                    next_st = 4;
                    break;
                } else {
                    return false;
                }
            }
            bool has_d = false;
            for (auto& ww : words)
                if (ww.dig) {
                    has_d = true;
                    break;
                }
            if (!words.empty() && !has_d) return false;
            if (has_d && tail < 0) return false;
            int last_d = -1;
            for (int j = 0; j < (int)words.size(); ++j)
                if (words[j].dig) last_d = j;
            std::string digits;
            std::vector<std::pair<int, char>> seps;
            int nseen = 0;
            for (int j = 0; j < (int)words.size(); ++j) {
                if (!words[j].dig) {
                    const int slot = words[j].v / 10, kk = words[j].v % 10;
                    seps.emplace_back(nseen + kk, sep_char(slot));
                } else {
                    const int val = words[j].v;
                    std::string d;
                    if (j == last_d) {
                        const int wl = 3 - tail;
                        if (wl <= 0 || wl > 3) return false;
                        int div = 1;
                        for (int t = 0; t < tail; ++t) div *= 10;
                        int dd = val / div;
                        d = std::to_string(dd);
                        while ((int)d.size() < wl) d = "0" + d;
                        if ((int)d.size() != wl) return false;
                    } else {
                        char b3[4];
                        b3[0] = static_cast<char>('0' + (val / 100) % 10);
                        b3[1] = static_cast<char>('0' + (val / 10) % 10);
                        b3[2] = static_cast<char>('0' + val % 10);
                        b3[3] = 0;
                        d = b3;
                    }
                    digits += d;
                    nseen += (int)d.size();
                }
            }
            size_t si = 0;
            for (size_t idx = 0; idx < digits.size(); ++idx) {
                while (si < seps.size() && seps[si].first == (int)idx) {
                    text.push_back(seps[si].second);
                    ++si;
                }
                text.push_back(digits[idx]);
            }
            while (si < seps.size()) {
                if (seps[si].first == (int)digits.size()) {
                    text.push_back(seps[si].second);
                    ++si;
                } else {
                    return false;
                }
            }
            // Buffered digits and separators are in `text` now, so the
            // deferred byte block can go after them in the payload's real
            // order (see w == 1023 above).
            if (enter_b_pending && !enter_b()) return false;
            if (next_st == 9) {
                consumed = i;
                return true;
            }
            st = next_st;  // L/U/M values follow with NO tag; B body read
        } else {  // st == 4 (B body read at entry; peek tag/END)
            const std::string tag = peek(4);
            if (tag.size() < 4) return false;
            if (tag == "0000") {
                i += 4;
                consumed = i;
                return true;
            } else if (tag == "0100") {
                i += 4;
                st = 0;
            } else if (tag == "0011") {
                i += 4;
                st = 1;
            } else if (tag == "0101") {
                i += 4;
                st = 2;
            } else if (tag == "0010") {
                i += 4;
                if (!take(2, tail) || tail == 3) return false;
                st = 3;
            } else {
                return false;
            }
        }
    }
}

bool decode_gm_stream(const std::vector<int>& cws, int version,
                      std::string& text, int& consumed) {
    text.clear();
    consumed = 0;
    if (version < 1 || version > 13) return false;
    const int Lc = static_cast<int>(cws.size());
    if (Lc <= 0) return false;
    for (int v : cws)
        if (v < 0 || v > 127) return false;
    auto try_blocks = [&](int h, int k, std::string& t, int& cons) -> bool {
        // single-block: the input is exactly ONE full codeword block, so
        // Lc == h + k == N(v). The image path always supplies a whole block
        // (stacked_gridmatrix_image.cpp reserves and fills H+K), and h is
        // exact now, so there is no length-derived h to search for.
        //
        // A SHORTER input (Lc < h+k) is refused rather than accepted with
        // D = Lc - k: a self-consistent [D|K] block with D != h cannot be a
        // real symbol, because a real encoder always fills the data region
        // to h before computing EC. Its EC therefore differs from any such
        // block's EC, so accepting it would only ever accept a synthetic
        // vector -- and it widens the set of RS-miscorrectable candidates.
        if (Lc == h + k) {
            const int D = h;
            std::vector<int> low(cws.rbegin(), cws.rend());
            int nerr = 0;
            if (!gm_rs_decode(low, k, nerr)) return false;
            // low[0]=x^0 (last EC) .. low[Lc-1]=top (data[0]); rebuild
            // high-first data[0..D-1] = low[Lc-1-i].
            std::string bits;
            bits.reserve(static_cast<size_t>(D) * 7);
            for (int i = 0; i < D; ++i) {
                // low index of high-first data[i] is (Lc-1-i)
                const int v = low[Lc - 1 - i];
                for (int j = 6; j >= 0; --j)
                    bits.push_back(((v >> j) & 1) ? '1' : '0');
            }
            return gm_walk_bits(bits, t, cons);
        }
        // 2-block: Lc == D+2K with H < D <= 2H (D exact from length).
        const int D = Lc - 2 * k;
        if (D <= h || D > 2 * h) return false;
        // block1 = cws[0:H+K], block2 = cws[H+K : Lc]
        std::vector<int> lo1(cws.begin(), cws.begin() + h + k);
        std::vector<int> lo2(cws.begin() + h + k, cws.end());
        // to low-first
        std::vector<int> l1(lo1.rbegin(), lo1.rend());
        std::vector<int> l2(lo2.rbegin(), lo2.rend());
        int n1 = 0, n2 = 0;
        if (!gm_rs_decode(l1, k, n1) || !gm_rs_decode(l2, k, n2))
            return false;
        std::string bits;
        bits.reserve(static_cast<size_t>(D) * 7);
        for (int i = 0; i < h; ++i) {
            const int v = l1[h + k - 1 - i];
            for (int j = 6; j >= 0; --j)
                bits.push_back(((v >> j) & 1) ? '1' : '0');
        }
        const int c = D - h;
        for (int i = 0; i < c; ++i) {
            const int v = l2[c + k - 1 - i];
            for (int j = 6; j >= 0; --j)
                bits.push_back(((v >> j) & 1) ? '1' : '0');
        }
        return gm_walk_bits(bits, t, cons);
    };
    // Descending K (s=5..1) + longest-parse selection.
    //
    // The level is now EXACT: h(v,level) = N(v) - K(v,level) (step 21b). Each
    // level contributes exactly ONE (h,k) pair, so there is no h search window
    // and no EC-fraction guess. The old code swept 11 candidate h values per
    // level around the EC-fraction estimate because h could not be pinned;
    // kGMN/kGMKLevel below are the entire difference.
    //
    // Longest-parse selection is kept and still load-bearing: a wrong block
    // split can RS-miscorrect and truncate early in garbage, so among the
    // levels whose roots hold we keep the LONGEST consumed parse (correct =
    // fullest; truncated false positives lose). RS gates strongly first.
    std::string best;
    int best_cons = -1;
    for (int s = 5; s >= 1; --s) {
        const int k = gm_ec_codewords(version, s);
        if (k <= 0) continue;
        const int h = gm_data_codewords(version, s);   // exact, no window
        if (h <= 0) continue;
        std::string t;
        int cons = 0;
        if (try_blocks(h, k, t, cons)) {
            if (cons > best_cons) {
                best = t;
                best_cons = cons;
            }
        }
    }
    consumed = best_cons;
    if (best_cons < 0) {
        text.clear();
        return false;
    }
    text = best;
    return true;
}

bool decode_gm_stream(const std::vector<int>& cws, int version,
                      std::string& text) {
    int consumed = 0;
    return decode_gm_stream(cws, version, text, consumed);
}

}  // namespace gridmatrix
}  // namespace omniscan
