// MicroPDF417 native decode: image detector (docs step 6) -> row parse
// (steps 1-3: RAP tables, 17-module codewords, discriminator/cluster
// rule) -> cross-row RAP assignment checks (step 2) -> RS syndrome
// check over GF(929) (step 5) -> mode decode (step 4). Every constant
// comes from the generated tables header or a documented measurement;
// nothing here is assumed. Refusals are layered cheap-to-expensive so
// non-symbol input (text pages, other formats, noise) dies early, and
// upside-down/rotated input refuses at the RAP/cluster/syndrome layers.
#include "stacked_micropdf417.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "micropdf417_tables.h"

namespace omniscan {
namespace micropdf417 {
namespace {

// Generated tables live in omniscan::stacked::mp417.
namespace mp = ::omniscan::stacked::mp417;

// ---- variant lookup (docs step 1) ----
int variant_ecc(int c, int r) noexcept {
    for (const auto& v : mp::kVariants)
        if (v.cols == c && v.rows == r) return v.ecc;
    return -1;
}

// ---- RS syndrome check (docs step 5) ----
// The generator has roots 3^1..3^k, so every valid full codeword
// polynomial c(x) (c[0] = highest power, data region then EC) must
// annihilate at each root. Horner from c[0].
bool ec_ok(const std::vector<int>& cw, int k) noexcept {
    long long p3 = 1;
    for (int j = 1; j <= k; ++j) {
        p3 = (p3 * 3) % 929;
        long long s = 0;
        for (int v : cw) s = (s * p3 + v) % 929;
        if (s != 0) return false;
    }
    return true;
}

// ---- RS error correction (docs: Error correction) ----
// Bounded errors-only decode over GF(929): corrects up to
// t = floor(k/2) symbol errors, refuses (output untouched) on
// anything else. Runs ONLY when ec_ok() fails, so clean symbols
// never pay it. Port of the verified attic/mp_rs_correct.py
// prototype (bars: 1700 exact-correction trials, 5000 t+1 trials
// with zero misdecodes); the prototype's two bugs are avoided
// here by construction (full formal derivative — GF(929) is not
// characteristic 2 — and direct power evaluation).
int gf_pow929(int a, int e) noexcept {
    long long r = 1, b = ((a % 929) + 929) % 929;
    while (e > 0) {
        if (e & 1) r = (r * b) % 929;
        b = (b * b) % 929;
        e >>= 1;
    }
    return static_cast<int>(r);
}
int gf_inv929(int a) noexcept {
    a %= 929;
    if (a < 0) a += 929;
    if (a == 0) return -1;
    return gf_pow929(a, 927);
}

bool rs_correct(const std::vector<int>& rx, int k,
                std::vector<int>& fixed) {
    const int n = static_cast<int>(rx.size());
    const int t = k / 2;
    if (t < 1 || n <= 0 || k <= 0) return false;
    // 1. syndromes S_1..S_k (same Horner orientation as ec_ok).
    std::vector<int> syn(static_cast<size_t>(k), 0);
    {
        long long p3 = 1;
        for (int j = 1; j <= k; ++j) {
            p3 = (p3 * 3) % 929;
            long long s = 0;
            for (int v : rx) s = (s * p3 + v) % 929;
            syn[static_cast<size_t>(j - 1)] = static_cast<int>(s);
        }
    }
    bool clean = true;
    for (int s : syn)
        if (s != 0) {
            clean = false;
            break;
        }
    if (clean) {
        fixed = rx;
        return true;
    }
    // 2. Berlekamp-Massey over the first 2t syndromes.
    const int m2t = 2 * t;
    std::vector<int> C(1, 1), B(1, 1);
    int L = 0, m = 1, b = 1;
    for (int N = 0; N < m2t; ++N) {
        long long d = syn[static_cast<size_t>(N)];
        for (int i = 1; i <= L; ++i) {
            if (i < static_cast<int>(C.size()))
                d = (d + static_cast<long long>(C[static_cast<size_t>(i)]) *
                           syn[static_cast<size_t>(N - i)]) %
                    929;
        }
        const int dd = static_cast<int>(d);
        if (dd == 0) {
            ++m;
            continue;
        }
        const std::vector<int> T = C;
        const size_t need = static_cast<size_t>(m) + B.size();
        if (C.size() < need) C.resize(need, 0);
        const int binv = gf_inv929(b);
        if (binv < 0) return false;
        const int db =
            static_cast<int>(static_cast<long long>(dd) * binv % 929);
        for (size_t i = 0; i < B.size(); ++i) {
            const size_t at = static_cast<size_t>(m) + i;
            int c = static_cast<int>(
                (C[at] - static_cast<long long>(db) * B[i]) % 929);
            if (c < 0) c += 929;
            C[at] = c;
        }
        if (2 * L <= N) {
            L = N + 1 - L;
            B = T;
            b = dd;
            m = 1;
        } else {
            ++m;
        }
    }
    if (L < 1 || L > t) return false;
    if (static_cast<int>(C.size()) <= L)
        C.resize(static_cast<size_t>(L) + 1, 0);
    // 3. Chien search: positions i with C(a^(-(n-1-i))) == 0.
    std::vector<int> locs;
    locs.reserve(static_cast<size_t>(L));
    for (int i = 0; i < n; ++i) {
        const int exp = (n - 1 - i) % 928;
        const int xinv = gf_pow929(3, (928 - exp) % 928);
        long long v = 0;
        for (int ci = L; ci >= 0; --ci)
            v = (v * xinv + C[static_cast<size_t>(ci)]) % 929;
        if (v == 0) locs.push_back(i);
    }
    if (static_cast<int>(locs.size()) != L) return false;
    // 4. Omega + Forney (FC=1: Y = -Om(xinv)/Lam'(xinv), full
    // formal derivative — every ci>=1 term survives in GF(929)).
    std::vector<int> om(static_cast<size_t>(m2t), 0);
    for (int i = 0; i < m2t && i < static_cast<int>(C.size()); ++i) {
        if (C[static_cast<size_t>(i)] == 0) continue;
        for (int j = 0; j + i < m2t; ++j)
            om[static_cast<size_t>(i + j)] = static_cast<int>(
                (om[static_cast<size_t>(i + j)] +
                 static_cast<long long>(C[static_cast<size_t>(i)]) *
                     syn[static_cast<size_t>(j)]) %
                929);
    }
    fixed = rx;
    for (int i : locs) {
        const int exp = (n - 1 - i) % 928;
        const int xinv = gf_pow929(3, (928 - exp) % 928);
        long long num = 0;
        for (int ci = m2t - 1; ci >= 0; --ci)
            num = (num * xinv + om[static_cast<size_t>(ci)]) % 929;
        long long den = 0;
        for (int ci = 1; ci <= L; ++ci) {
            if (C[static_cast<size_t>(ci)] == 0) continue;
            den = (den + static_cast<long long>(C[static_cast<size_t>(ci)]) *
                             ci % 929 * gf_pow929(xinv, ci - 1)) %
                  929;
        }
        const int dinv = gf_inv929(static_cast<int>(den));
        if (dinv < 0) return false;
        const int mag = static_cast<int>((929 - num % 929) % 929 *
                                         static_cast<long long>(dinv) % 929);
        int nv = (fixed[static_cast<size_t>(i)] - mag) % 929;
        if (nv < 0) nv += 929;
        fixed[static_cast<size_t>(i)] = nv;
    }
    // 5. Re-verify all k syndromes on the corrected stream.
    return ec_ok(fixed, k);
}

// ---- mode decode (docs step 4; direct port of attic/mp_modes.py) ----
bool decode_modes(const std::vector<int>& cws, std::string& out) {
    out.clear();
    int mode = -1;  // -1 none, 0 text, 1 numeric, 2 byte
    char sub = 'A';
    char ovr = 0;  // one-shot next-half submode (0 = none)
    bool shift_byte = false;
    std::vector<int> num, byts;
    int byt_latch = 901;

    // Numeric segment: base-900 group(s) with a 10^n length prefix.
    // Chunks of max 44 digits, each prefixed '1', consecutive under
    // one 902 latch (docs: 44-digit rule — zint 44+1/44+44/44+44+1
    // observations). Every full 44-digit chunk is EXACTLY 15
    // codewords (10^44 <= v < 2*10^44 < 900^15), and every final
    // chunk (<=44 digits) fits 15 — so the parse is deterministic
    // with no backtracking: while more than 15 codewords remain the
    // next group is 15 cws decoding to '1'+44 digits, and the final
    // <=15 cws decode to '1'+1..44 digits (the old single-group
    // rule, unchanged). Decimal conversion is repeated division of
    // the base-900 digit vector by 10 -- no bignum needed.
    auto to_dec = [&](size_t from, size_t count) {
        std::vector<int> d(num.begin() + from, num.begin() + from + count);
        std::string dec;
        for (;;) {
            size_t z = 0;
            while (z + 1 < d.size() && d[z] == 0) ++z;
            if (z) d.erase(d.begin(), d.begin() + z);
            if (d.size() == 1 && d[0] == 0) break;
            long long rem = 0;
            for (int& x : d) {
                const long long cur = rem * 900 + x;
                x = static_cast<int>(cur / 10);
                rem = cur % 10;
            }
            dec.push_back(static_cast<char>('0' + rem));
        }
        std::reverse(dec.begin(), dec.end());
        return dec;
    };
    auto flush_num = [&]() -> bool {
        if (num.empty()) return false;
        size_t pos = 0;
        while (num.size() - pos > 15) {
            const std::string g = to_dec(pos, 15);
            if (g.size() != 45 || g[0] != '1') return false;
            out.append(g, 1, 44);
            pos += 15;
        }
        const std::string last = to_dec(pos, num.size() - pos);
        if (last.size() < 2 || last.size() > 45 || last[0] != '1')
            return false;  // n<1, overlong tail, or >=2*10^n
        out.append(last, 1, std::string::npos);
        return true;
    };

    // One 5-codeword group -> 6 bytes big-endian (base 900, < 256^6).
    auto grp6 = [&](const int* g) -> bool {
        long long v = 0;
        for (int i = 0; i < 5; ++i) v = v * 900 + g[i];
        if (v >= (1LL << 48)) return false;
        char b[6];
        for (int i = 5; i >= 0; --i) {
            b[i] = static_cast<char>(v & 0xFF);
            v >>= 8;
        }
        out.append(b, 6);
        return true;
    };

    // Byte segment: 901 peels groups while len>5 (raw tail 1..5),
    // 924 while len>=5 (exact groups). Tail codewords are raw bytes.
    auto flush_byt = [&]() -> bool {
        std::vector<int> seg = byts;
        const bool exact = (byt_latch != 901);
        while (exact ? seg.size() >= 5 : seg.size() > 5) {
            if (!grp6(seg.data())) return false;
            seg.erase(seg.begin(), seg.begin() + 5);
        }
        for (int c : seg) {
            if (c > 255) return false;
            out.push_back(static_cast<char>(c));
        }
        return true;
    };

    // One text half: switch cells first, then the submode table.
    auto half = [&](int v) -> bool {
        const char eff = ovr ? ovr : sub;
        ovr = 0;
        if (eff == 'P') {
            if (v == 29) {  // latch exit to Upper
                sub = 'A';
                return true;
            }
            if (v < 0 || v > 28 || kPu[v] == '\0') return false;
            out.push_back(kPu[v]);
            return true;
        }
        // eff in {A, L, M}:
        if (v == 29) {  // one-shot Punct (next half only)
            ovr = 'P';
            return true;
        }
        if (v == 27) {  // L: one-shot Upper; A/M: latch Lower
            if (eff == 'L') ovr = 'A';
            else sub = 'L';
            return true;
        }
        if (v == 28) {  // M: latch Upper; A/L: latch Mixed
            if (eff == 'M') sub = 'A';
            else sub = 'M';
            return true;
        }
        if (v == 25 && eff == 'M') {  // latch Punct (persistent)
            sub = 'P';
            return true;
        }
        const char* tbl = (eff == 'A') ? kUp : (eff == 'L') ? kLo : kMi;
        if (v < 0 || v > 26 || tbl[v] == '\0') return false;
        out.push_back(tbl[v]);
        return true;
    };

    auto flush_cur = [&]() -> bool {
        if (mode == 1) {
            if (!flush_num()) return false;
            num.clear();
        } else if (mode == 2) {
            if (!flush_byt()) return false;
            byts.clear();
        }
        return true;
    };

    for (int cw : cws) {
        if (cw < 0 || cw > 928) return false;
        if (shift_byte) {  // 913 one-shot byte consumes the next codeword
            if (cw > 255) return false;
            out.push_back(static_cast<char>(cw));
            shift_byte = false;
            continue;
        }
        if (cw >= 900) {
            ovr = 0;
            if (cw == 900) {
                if (mode == 0) return true;  // pad in text mode ends the data
                if (!flush_cur()) return false;
                mode = 0;
                sub = 'A';
            } else if (cw == 901 || cw == 924) {
                if (!flush_cur()) return false;
                mode = 2;
                byt_latch = cw;
            } else if (cw == 902) {
                if (!flush_cur()) return false;
                mode = 1;
            } else if (cw == 913) {
                if (!flush_cur()) return false;
                shift_byte = true;
            } else {
                return false;  // reserved 903..912 / 914..923 / 925..928
            }
            continue;
        }
        if (mode < 0) return false;  // data before latch
        if (mode == 0) {
            if (!half(cw / 30)) return false;
            if (!half(cw % 30)) return false;
        } else if (mode == 1) {
            num.push_back(cw);
        } else {
            byts.push_back(cw);
        }
    }
    if (mode == 1) {
        if (!flush_num()) return false;
    } else if (mode == 2) {
        if (!flush_byt()) return false;
    }
    if (shift_byte) return false;  // dangling byte shift at end
    return true;
}

// ---- row parsing (image side) ----

// Runs of module bits [from, from+len), must start dark. Returns the
// run count (<= maxr) or -1.
int slice_runs(const uint8_t* bits, int from, int len, int* runs, int maxr) {
    if (bits[from] != 1) return -1;
    int n = 0, cur = 1;
    for (int i = 1; i < len; ++i) {
        if (bits[from + i] == bits[from + i - 1]) {
            ++cur;
        } else {
            if (n >= maxr) return -1;
            runs[n++] = cur;
            cur = 1;
        }
    }
    if (n >= maxr) return -1;
    runs[n++] = cur;
    return n;
}

// RAP: 6 runs -> packed key -> linear search; number is 1..52.
int rap_lookup(const std::uint32_t* tab, const int* runs, int n) {
    if (n != 6) return -1;
    const std::uint32_t key = mp::pack_runs(runs, n);
    for (int i = 0; i < 52; ++i)
        if (tab[i] == key) return i + 1;
    return -1;
}

// Codeword: 8 runs; the discriminator (x0-x2+x4-x6) mod 9 must equal
// the row's RAP-derived cluster (docs step 2, verified 451/451),
// then the packed key hits the 929-value table for that cluster.
int cod_lookup(int cluster_disc, const int* runs, int n) {
    if (n != 8) return -1;
    int d = (runs[0] - runs[2] + runs[4] - runs[6]) % 9;
    if (d < 0) d += 9;
    if (d != cluster_disc) return -1;
    const std::uint32_t key = mp::pack_runs(runs, 8);
    const auto* arr = mp::kCodLookup[d / 3];
    int lo = 0, hi = 929;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (arr[mid].key < key) lo = mid + 1;
        else hi = mid;
    }
    if (lo < 929 && arr[lo].key == key) return arr[lo].val;
    return -1;
}

struct RowVals {
    int left = -1, center = -1, right = -1;
    std::vector<int> cws;
};

// One row of module bits -> RAP numbers + codeword values. All the
// per-row validation layers (anchors, tables, cluster) live here.
bool parse_row(const uint8_t* bits, int M, int c, RowVals& row) {
    if (bits[0] != 1 || bits[M - 1] != 1) return false;  // step-6 anchors
    int runs[20];
    int n = slice_runs(bits, 0, 10, runs, 20);
    const int left = (n < 0) ? -1 : rap_lookup(mp::kRapL, runs, n);
    if (left < 0) return false;
    const int disc = ((left - 1) % 3) * 3;  // cluster from left RAP (step 2)
    // Data column offsets: center RAP sits after c/2 columns on 3-4 col
    // (1|center|2 for 3 col, 2|center|2 for 4 col), absent on 1-2 col.
    int offs[4];
    int noff = 0;
    if (c >= 3) {
        const int s = c / 2;
        for (int k = 0; k < s; ++k) offs[noff++] = 10 + 17 * k;
        for (int k = 0; k < c - s; ++k)
            offs[noff++] = 10 + 17 * s + 10 + 17 * k;
    } else {
        for (int k = 0; k < c; ++k) offs[noff++] = 10 + 17 * k;
    }
    row.cws.clear();
    row.cws.reserve(static_cast<size_t>(c));
    for (int k = 0; k < c; ++k) {
        n = slice_runs(bits, offs[k], 17, runs, 20);
        const int v = (n < 0) ? -1 : cod_lookup(disc, runs, n);
        if (v < 0) return false;
        row.cws.push_back(v);
    }
    if (c >= 3) {
        n = slice_runs(bits, 10 + 17 * (c / 2), 10, runs, 20);
        row.center = (n < 0) ? -1 : rap_lookup(mp::kRapC, runs, n);
        if (row.center < 0) return false;
    }
    n = slice_runs(bits, M - 11, 10, runs, 20);
    row.right = (n < 0) ? -1 : rap_lookup(mp::kRapL, runs, n);
    if (row.right < 0) return false;
    row.left = left;
    return true;
}

// Cross-row RAP assignment (docs step 2, measured 34/34): lefts run
// consecutively (mod 52, 1-based) and center/right follow the one-d
// rule for the row count.
bool rows_valid(const std::vector<RowVals>& rows, int c, int r) {
    const int d = (c >= 3) ? rap_center_offset(r) : rap_right_offset(r);
    if (d < 0) return false;
    int expect = rows[0].left;
    for (int i = 0; i < r; ++i) {
        if (rows[i].left != expect) return false;
        expect = expect % 52 + 1;
        if (c >= 3) {
            const int center = (rows[i].left - 1 + d) % 52 + 1;
            if (rows[i].center != center) return false;
            const int right = (center - 1 + d) % 52 + 1;
            if (rows[i].right != right) return false;
        } else {
            const int right = (rows[i].left - 1 + d) % 52 + 1;
            if (rows[i].right != right) return false;
        }
    }
    return true;
}

// Majority of 3 scanlines at the row center: noise survives only a
// 2/3 vote (approx. 0.0075% per pixel at the 0.5% corpus level), and
// blur shifts horizontal row boundaries, not the vertical run edges,
// so the row center is the safest vertical sample.
void sample_line(const BinaryImage& bin, int x0, int x1, int yc,
                 std::vector<uint8_t>& line) {
    const int n = x1 - x0 + 1;
    line.assign(static_cast<size_t>(n), 0);
    for (int j = 0; j < n; ++j) {
        int dark = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            int y = yc + dy;
            if (y < 0) y = 0;
            if (y >= bin.height) y = bin.height - 1;
            if (bin.bits[static_cast<size_t>(y) * bin.stride + x0 + j] == 0)
                ++dark;
        }
        if (dark >= 2) line[static_cast<size_t>(j)] = 1;
    }
}

// Sampled line [a..b] (dark at both ends by the step-6 anchors) ->
// snapped module bits of length M. Boundaries snap via
// round(cumulative_px / module_px) so +/-1px edge jitter rounds away;
// the per-row module width re-derives from the span between the first
// and last dark pixel (exactly M modules), so per-row anchor jitter
// never accumulates.
bool line_to_modules(const std::vector<uint8_t>& line, int a, int b, int M,
                     std::vector<uint8_t>& bits) {
    const int span = b - a + 1;
    const double m = static_cast<double>(span) / M;
    if (m < 1.5) return false;
    bits.assign(static_cast<size_t>(M), 0);
    double cum = 0;
    int pos = 0;
    int color = 1;  // first pixel (a) is dark
    for (int i = a; i <= b;) {
        int j = i;
        while (j <= b && line[static_cast<size_t>(j)] ==
                             line[static_cast<size_t>(i)])
            ++j;
        cum += static_cast<double>(j - i);
        const int end = static_cast<int>(std::lround(cum / m));
        const int w = end - pos;
        if (w < 1 || pos + w > M) return false;
        for (int k = 0; k < w; ++k)
            bits[static_cast<size_t>(pos++)] = static_cast<uint8_t>(color);
        color = color ? 0 : 1;
        i = j;
    }
    (void)color;
    return pos == M;
}

}  // namespace

bool decode_mp417_stream(const std::vector<int>& cws, int cols, int rows,
                         std::string& text) {
    text.clear();
    if (cols < 1 || cols > 4 || rows < 4 || rows > 44) return false;
    const int ecc = variant_ecc(cols, rows);
    if (ecc < 0) return false;
    const int total = cols * rows;
    if (static_cast<int>(cws.size()) != total) return false;
    for (int v : cws)
        if (v < 0 || v > 928) return false;
    if (!ec_ok(cws, ecc)) {
        // Bounded correction: up to t = floor(k/2) symbol errors
        // (docs: Error correction). Refusal leaves the stream
        // untouched, so anything uncorrectable refuses exactly as
        // before; a corrected stream still faces the mode decoder.
        std::vector<int> fixed;
        if (!rs_correct(cws, ecc, fixed)) return false;
        const std::vector<int> data(fixed.begin(),
                                    fixed.begin() + (total - ecc));
        return decode_modes(data, text);
    }
    const std::vector<int> data(cws.begin(), cws.begin() + (total - ecc));
    return decode_modes(data, text);
}

bool try_decode_mp417(const BinaryImage& bin, std::vector<Result>& out) {
    try {
        const int W = bin.width, H = bin.height;
        if (W < 40 || H < 16) return false;
        // Bands: rows carrying a >=3px dark run. Every symbol row has
        // a codeword (8 runs summing to 17 modules -> some run >= 3
        // modules >= 3px), so a symbol band never splits; isolated
        // noise runs of >=3px are far below the corpus noise level and
        // die at the module/variant checks below.
        std::vector<std::pair<int, int>> bands;
        bands.reserve(16);
        for (int y = 0; y < H; ++y) {
            const uint8_t* row =
                bin.bits.data() + static_cast<size_t>(y) * bin.stride;
            int run = 0, best = 0;
            for (int x = 0; x < W; ++x) {
                if (row[x] == 0) {
                    if (++run > best) best = run;
                } else {
                    run = 0;
                }
            }
            if (best >= 3) {
                if (!bands.empty() && bands.back().second == y - 1)
                    bands.back().second = y;
                else
                    bands.emplace_back(y, y);
            }
        }
        for (const auto& band : bands) {
            const int y0 = band.first, y1 = band.second;
            const int hb = y1 - y0 + 1;
            if (hb < 8) continue;
            // x anchors: columns dark on >=50% of the band's rows --
            // both symbol edges are full-height dark (module 0 = left
            // RAP start, module M-1 = stop bar), so noise rows above/
            // below or a few dark pixels anywhere else cannot anchor.
            const int thr = (hb + 1) / 2;
            std::vector<int> colink(static_cast<size_t>(W), 0);
            for (int y = y0; y <= y1; ++y) {
                const uint8_t* row =
                    bin.bits.data() + static_cast<size_t>(y) * bin.stride;
                for (int x = 0; x < W; ++x)
                    if (row[x] == 0) ++colink[static_cast<size_t>(x)];
            }
            int x0 = -1, x1 = -1;
            for (int x = 0; x < W; ++x) {
                if (colink[static_cast<size_t>(x)] >= thr) {
                    if (x0 < 0) x0 = x;
                    x1 = x;
                }
            }
            if (x0 < 0) continue;
            const int wb = x1 - x0 + 1;
            for (int c = 1; c <= 4; ++c) {
                const int M = mp::kRowModules[c];
                const double mg = static_cast<double>(wb) / M;
                if (mg < 1.5) continue;
                const double rc = hb / (2 * mg);
                const int r = static_cast<int>(std::lround(rc));
                // band edges can sit +/-2px out (JPEG bleed, stray
                // noise rows); 1/mg rows = 2px of slack.
                if (r < 4 || r > 44 || std::fabs(rc - r) > 1.0 / mg)
                    continue;
                if (variant_ecc(c, r) < 0) continue;
                std::vector<RowVals> rows(static_cast<size_t>(r));
                std::vector<uint8_t> line, bits;
                bool ok = true;
                for (int i = 0; i < r && ok; ++i) {
                    const int yc = static_cast<int>(
                        std::lround(y0 + (2 * i + 1) * mg));
                    sample_line(bin, x0, x1, yc, line);
                    // per-row horizontal anchors: first/last dark pixel
                    // within the band window (edges may sit +/-1px off
                    // the 50% anchor under JPEG bleed).
                    int a = -1, b = -1;
                    for (int j = 0; j < static_cast<int>(line.size()); ++j) {
                        if (line[static_cast<size_t>(j)]) {
                            if (a < 0) a = j;
                            b = j;
                        }
                    }
                    if (a < 0 || a > static_cast<int>(mg) ||
                        static_cast<int>(line.size()) - 1 - b >
                            static_cast<int>(mg)) {
                        ok = false;
                        break;
                    }
                    if (!line_to_modules(line, a, b, M, bits)) {
                        ok = false;
                        break;
                    }
                    if (!parse_row(bits.data(), M, c, rows[static_cast<size_t>(i)]))
                        ok = false;
                }
                if (!ok) continue;
                if (!rows_valid(rows, c, r)) continue;
                std::vector<int> flat;
                flat.reserve(static_cast<size_t>(c) * r);
                for (int i = 0; i < r; ++i)
                    flat.insert(flat.end(), rows[static_cast<size_t>(i)].cws.begin(),
                                rows[static_cast<size_t>(i)].cws.end());
                std::string text;
                if (!decode_mp417_stream(flat, c, r, text)) continue;
                if (text.empty()) continue;  // no empty-payload symbols
                Result res;
                res.symbology = Symbology::MicroPDF417;
                res.text = text;
                res.confidence = 1.0f;
                res.location =
                    Quad(Point{static_cast<float>(x0), static_cast<float>(y0)},
                         Point{static_cast<float>(x1 + 1),
                               static_cast<float>(y0)},
                         Point{static_cast<float>(x1 + 1),
                               static_cast<float>(y1 + 1)},
                         Point{static_cast<float>(x0),
                               static_cast<float>(y1 + 1)});
                out.push_back(std::move(res));
                return true;
            }
        }
    } catch (...) {
        return false;
    }
    return false;
}

}  // namespace micropdf417
}  // namespace omniscan
