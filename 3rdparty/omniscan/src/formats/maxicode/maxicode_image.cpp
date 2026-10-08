// MaxiCode image pipeline: hex-grid sampling, codeword assembly via the
// black-box module map, GF(2) erasure recovery of any unsampled bits,
// EC verification, value decode. See docs/formats/maxicode.md.
// Deterministic, no I/O.
#include "maxicode_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "maxicode.h"
#include "maxicode_map.h"
#include "maxicode_tables.h"
#include "omniscan/binarizer.h"

namespace omniscan {
namespace maxicode {
namespace {

int gf64_mul(int a, int b) {
    int p = 0;
    while (b) {
        if (b & 1) p ^= a;
        b >>= 1;
        a <<= 1;
        if (a & 0x40) a ^= 0x43;
    }
    return p & 0x3F;
}

bool is_dark(const BinaryImage& bin, int x, int y) {
    if (x < 0 || y < 0 || x >= bin.width || y >= bin.height) return false;
    return bin.bits[static_cast<size_t>(y) * bin.stride + x] == 0;
}

// GF(64) inverse (poly 0x43): inv[0] = 0, a·inv[a] = 1 otherwise.
// Pinned by unit test; lets single-error searches solve the value
// directly instead of trying all 63.
int gf64_inv(int a) {
    static const int* inv = [] {
        static int v[64] = {0};
        for (int x = 1; x < 64; ++x)
            for (int y = 1; y < 64; ++y)
                if (gf64_mul(x, y) == 1) { v[x] = y; break; }
        return v;
    }();
    if (a < 0 || a > 63) return 0;
    return inv[a];
}

// Which (codeword, bit) pairs the map can sample. With the corrected
// lattice (x = 4 + 10*col) only cw1 bits 2 and 3 are unsampled; before
// the correction ~33 right-edge bits were, and every symbol paid for it
// in erasure budget. Derived from the generated table, so a map change
// re-arms the recovery below automatically.
bool bit_sampled(int module, int bit) {
    static const std::vector<std::vector<bool>> t = [] {
        std::vector<std::vector<bool>> v(145, std::vector<bool>(6, false));
        for (int r = 0; r < 33; ++r)
            for (int j = 0; j < 30; ++j) {
                CellMap cm = kCellMap[r][j];
                if (cm.module == 0) continue;
                v[cm.module][cm.bit] = true;
            }
        return v;
    }();
    if (module < 1 || module > 144 || bit < 0 || bit > 5) return false;
    return t[module][bit];
}

// Secondary parity of a full codeword vector for one interleave block
// (0 = even, 1 = odd), computed with the fitted GF(64) matrices.
void compute_parity(const std::vector<int>& cw, int mode, int block,
                    std::vector<int>& out) {
    const int ndata = (mode == 5) ? 68 : 84;
    const int half = (mode == 5) ? 28 : 20;
    out.assign(half, 0);
    if (mode == 5) {
        std::vector<int> d(34, 0);
        for (int k = 0; k < ndata; ++k)
            if ((k % 2) == block) d[k / 2] = cw[20 + k];
        for (int i = 0; i < 28; ++i) {
            int acc = 0;
            for (int j = 0; j < 34; ++j) acc ^= gf64_mul(kM5[i][j], d[j]);
            out[i] = acc;
        }
    } else {
        const int (*m)[42] = (block == 0) ? kMe : kMo;
        std::vector<int> d(42, 0);
        for (int k = 0; k < ndata; ++k)
            if ((k % 2) == block) d[k / 2] = cw[20 + k];
        for (int i = 0; i < 20; ++i) {
            int acc = 0;
            for (int j = 0; j < 42; ++j) acc ^= gf64_mul(m[i][j], d[j]);
            out[i] = acc;
        }
    }
}

// One fitted-matrix entry of the secondary code: EEC shares one matrix
// across blocks, SEC has one per block (they are numerically identical,
// but the code does not assume that).
int sec_mat(int mode, int block, int i, int j) {
    if (mode == 5) return kM5[i][j];
    return (block == 0) ? kMe[i][j] : kMo[i][j];
}

// Syndrome of one secondary interleave block: s = M·d_recv XOR p_recv.
// Data symbol j of this block lives at cws[20 + 2*j + block], parity
// symbol i at cws[ecbase + 2*i + block] (see compute_parity).
void block_syndrome(const std::vector<int>& cws, int mode, int block,
                    std::vector<int>& s) {
    const bool eec = (mode == 5);
    const int np = eec ? 28 : 20;
    const int ecbase = eec ? 88 : 104;
    std::vector<int> par;
    compute_parity(cws, mode, block, par);
    s.assign(np, 0);
    for (int i = 0; i < np; ++i) s[i] = par[i] ^ cws[ecbase + 2 * i + block];
}

// Single-symbol correction for one interleave block. Returns true when
// the block is clean, when a single data-symbol error was found and
// fixed (the fix is applied to cws), or when the syndrome proves a
// parity-only error (cws untouched: the data is good). Anything heavier
// returns false. Safety case in docs/formats/maxicode.md §Image
// pipeline (attic/maxi_corr.py): unique single-error solutions, and no
// weight-3/4 column dependencies, so heavier damage is always rejected,
// never misaccepted.
bool correct_block(std::vector<int>& cws, int mode, int block) {
    const bool eec = (mode == 5);
    const int nd = eec ? 34 : 42;
    const int np = eec ? 28 : 20;
    std::vector<int> s;
    block_syndrome(cws, mode, block, s);
    bool clean = true;
    for (int v : s) clean = clean && (v == 0);
    if (clean) return true;
    for (int j = 0; j < nd; ++j) {
        for (int e = 1; e < 64; ++e) {
            bool hit = true;
            for (int i = 0; i < np; ++i) {
                if (gf64_mul(sec_mat(mode, block, i, j), e) != s[i]) {
                    hit = false;
                    break;
                }
            }
            if (hit) {
                cws[20 + 2 * j + block] ^= e;
                // The fix must explain the syndrome exactly; recompute
                // rather than trust the search.
                block_syndrome(cws, mode, block, s);
                for (int v : s)
                    if (v != 0) return false;
                return true;
            }
        }
    }
    // Single parity-symbol error: exactly one nonzero syndrome symbol. A
    // data error can never present this way — every column of every
    // fitted matrix has full weight — so the data is proven good.
    int nz = 0;
    for (int v : s) nz += (v != 0);
    return nz == 1;
}

// True EC verification: recompute both parity blocks from the recovered
// data codewords and compare with the sampled parity codewords. Solving
// the erasure system alone is NOT evidence — an all-zero noise image
// satisfies the linear system trivially, so the result must be checked
// against the transmitted parity.
bool verify_secondary(const std::vector<int>& cws, int mode) {
    const int npar = (mode == 5) ? 56 : 40;
    const int ecbase = (mode == 5) ? 88 : 104;
    for (int block = 0; block < 2; ++block) {
        std::vector<int> par;
        compute_parity(cws, mode, block, par);
        for (int i = 0; i < static_cast<int>(par.size()); ++i) {
            int pcw = 2 * i + block;
            if (pcw >= npar) break;
            if (par[i] != cws[ecbase + pcw]) return false;
        }
    }
    return true;
}

// GF(2) erasure recovery. GF(64) multiply-by-constant is GF(2)-linear, so
// each parity BIT is a GF(2)-linear function of the message bits. All
// unsampled bits (message and parity alike) are unknowns; the parity
// equations give the system, solved by Gauss-Jordan over GF(2).
bool recover_erasures(std::vector<int>& cws, int mode) {
    const int ndata = (mode == 5) ? 68 : 84;
    const int npar = (mode == 5) ? 56 : 40;
    const int ecbase = (mode == 5) ? 88 : 104;
    std::vector<std::pair<int, int>> unk;  // (codeword index 0-based, bit)
    for (int c = 20; c < 20 + ndata; ++c)
        for (int b = 0; b < 6; ++b)
            if (!bit_sampled(c + 1, b)) unk.push_back({c, b});
    for (int c = ecbase; c < ecbase + npar; ++c)
        for (int b = 0; b < 6; ++b)
            if (!bit_sampled(c + 1, b)) unk.push_back({c, b});
    if (unk.empty()) return verify_or_correct(cws, mode);
    int u = static_cast<int>(unk.size());
    int neq = npar * 6;
    if (u > neq) return false;

    // Parity of a full codeword vector: even block (0) / odd block (1).
    auto parity_of = [&](const std::vector<int>& cw, int block,
                         std::vector<int>& out) {
        compute_parity(cw, mode, block, out);
    };

    std::vector<int> obs(npar);
    for (int k = 0; k < npar; ++k) obs[k] = cws[ecbase + k];

    std::vector<int> zeroed(cws.begin(), cws.end());
    for (auto& pr : unk) zeroed[pr.first] &= ~(1 << (5 - pr.second));
    std::vector<int> pE, pO;
    parity_of(zeroed, 0, pE);
    parity_of(zeroed, 1, pO);
    auto base_par = [&](int pcw) {
        return (pcw % 2 == 0) ? pE[pcw / 2] : pO[pcw / 2];
    };

    // Variables: every unknown (message OR parity) bit. Equation e=(pcw,
    // pbit): sum_t x_t * (A e_t)[bit]  XOR  (parity-unknown at that exact
    // position)  =  sampled_parity_bit XOR base_bit.
    std::vector<std::vector<int>> A(neq, std::vector<int>(u + 1, 0));
    for (int e = 0; e < neq; ++e) {
        int pcw = e / 6, pbit = e % 6;
        int base = base_par(pcw);
        int observed = (obs[pcw] >> (5 - pbit)) & 1;
        A[e][u] = observed ^ ((base >> (5 - pbit)) & 1);
        // Parity-unknown column: identity at its own position.
        for (int t = 0; t < u; ++t) {
            int c = unk[t].first, b = unk[t].second;
            if (c >= ecbase && c < ecbase + npar) {
                int pos = (c - ecbase) * 6 + b;
                A[e][t] = (pos == e) ? 1 : 0;
            }
        }
    }
    for (int t = 0; t < u; ++t) {
        int c = unk[t].first;
        if (c >= ecbase && c < ecbase + npar) continue;  // parity var: done
        std::vector<int> m2 = zeroed;
        m2[c] ^= 1 << (5 - unk[t].second);
        std::vector<int> qE, qO;
        parity_of(m2, 0, qE);
        parity_of(m2, 1, qO);
        for (int e = 0; e < neq; ++e) {
            int pcw = e / 6, pbit = e % 6;
            int q = (pcw % 2 == 0) ? qE[pcw / 2] : qO[pcw / 2];
            int b = base_par(pcw);
            A[e][t] = ((q >> (5 - pbit)) & 1) ^ ((b >> (5 - pbit)) & 1);
        }
    }
    // Gauss-Jordan over GF(2).
    int row = 0;
    std::vector<int> piv(u, -1);
    for (int c = 0; c < u && row < neq; ++c) {
        int sel = -1;
        for (int r = row; r < neq; ++r)
            if (A[r][c]) { sel = r; break; }
        if (sel < 0) continue;
        std::swap(A[row], A[sel]);
        for (int r = 0; r < neq; ++r) {
            if (r == row || !A[r][c]) continue;
            for (int t = c; t <= u; ++t) A[r][t] ^= A[row][t];
        }
        piv[c] = row;
        ++row;
    }
    for (int c = 0; c < u; ++c)
        if (piv[c] < 0) return false;  // underdetermined: cannot recover
    for (int c = 0; c < u; ++c)
        if (A[piv[c]][u]) cws[unk[c].first] |= 1 << (5 - unk[c].second);
    // The system may be consistent for garbage (e.g. an all-zero region):
    // only a full parity re-check makes the recovery trustworthy.
    return verify_or_correct(cws, mode);
}

// Modes 2/3 (structured primary): the primary 10 codewords carry fixed
// The SECONDARY message: a single code-set stream over cws 20..103
// (docs/formats/maxicode.md §structured primary). The whole region is
// passed through: pads are 33s in A/B state, but a 33 in latched-C/D
// state is data (220/252), and only the state machine knows which is
// which — truncating at a bare 33 here would cut latched data.
bool decode_secondary(const std::vector<int>& cws, std::string& text) {
    text.clear();
    std::vector<int> data(cws.begin() + 20, cws.begin() + 104);
    if (data.empty()) return false;
    return decode_data(data, 0, static_cast<int>(data.size()), text);
}

// ---------------------------------------------------------------- primary
// In modes 2/3 the first 10 codewords are a base-64 integer (cw_j has
// weight 64^j) carrying the structured primary — postal code, country,
// service level — NOT code-set data. The model below was fitted black-box
// from oracle codewords (attic/maxi_decode.py, 131 random primaries +
// edge cases) and is documented in docs/formats/maxicode.md.
//
//   mode 2: val = 2 + 16*plen*64^5            (length, decimal * 16)
//                + 16*(postal as decimal int) (at 64^0)
//                + 16*country                 (at 64^6)
//                + 4*service                  (at 64^8)
//   mode 3: val = 3 + thermometer: slot 1..6 holds 8 when slot <= 6-plen
//                else 12
//                + sum_k 16*e_k*64^(5-k)      (postal, one char per slot)
//                + 16*country (64^6) + 4*service (64^8)
//          d_k = (ascii-48) mod 64 of postal char k, e_k = d_k when
//          d_k <= 10 else d_k - 64. The -64 is invisible for digits; it
//          surfaces as a -1024 borrow into the next base-64 slot for
//          letters/punctuation (measured: 'A' encodes as e = -47).
//
// The field separator written into the result is the FOUR literal
// characters "<GS>", the transcoder convention pinned by
// tests/data/backend/README.md (ASCII 29 is transcoded, never emitted).

constexpr int64_t kP64[11] = {1,
                              64,
                              4096,
                              262144,
                              16777216,
                              1073741824,
                              68719476736,
                              4398046511104,
                              281474976710656,
                              18014398509481984LL,
                              1152921504606846976LL};

int64_t mode3_thermo(int eff) {
    int64_t v = 0;
    for (int s = 1; s <= 6; ++s) {
        int t = (s <= 6 - eff) ? 8 : 12;
        v += static_cast<int64_t>(t) * kP64[s];
    }
    return v;
}

// Base-64 postal digit -> character. Derived from the oracle charset:
// ascii 32, 34-36, 38-58, 65-90 (everything else is rejected upstream).
bool primary_char(int d, char& c) {
    int a = ((d + 16) % 64) + 32;
    if (!(a == 32 || a == 34 || a == 35 || a == 36 ||
          (a >= 38 && a <= 58) || (a >= 65 && a <= 90))) {
        return false;
    }
    c = static_cast<char>(a);
    return true;
}

// Mode 3: recover the postal string from W = thermometer + 16*X.
//
// Only slots below the postal length are encoded — the rest is thermometer
// fill — so the recovered digit at every position k >= eff MUST be 0.
// Larger eff values always re-appear as the postal padded with trailing
// spaces (a plen-p postal and the same postal plus one space encode to
// byte-identical codewords: measured, attic/maxi_decode.py), so the
// canonical answer is the smallest self-consistent eff.
bool mode3_postal(int64_t W, std::string& postal) {
    for (int eff = 1; eff <= 6; ++eff) {
        int64_t R = W - mode3_thermo(eff);
        if (R % 16 != 0) continue;
        int64_t rem = R / 16;
        int d[6];
        for (int i = 0; i < 6; ++i) {
            int64_t r = rem % 64;
            if (r < 0) r += 64;
            int di = static_cast<int>(r);
            int64_t e = (di <= 10) ? di : di - 64;
            rem = (rem - e) / 64;  // exact by construction
            d[5 - i] = di;         // i = 0 is the least significant slot
        }
        if (rem != 0) continue;
        bool tail = true;
        for (int k = eff; k < 6; ++k)
            if (d[k] != 0) { tail = false; break; }
        if (!tail) continue;
        std::string s;
        for (int k = 0; k < eff; ++k) {
            char c;
            if (!primary_char(d[k], c)) { s.clear(); break; }
            s.push_back(c);
        }
        if (s.size() != static_cast<size_t>(eff)) continue;
        postal = s;
        return true;
    }
    return false;
}

// Modes 2/3: read postal / country / service out of cws[0..9] and format
// them as "postal<GS>country<GS>service".
bool decode_primary_fields(const std::vector<int>& cws, std::string& out) {
    int64_t val = 0;
    for (int j = 9; j >= 0; --j) val = val * 64 + cws[j];
    int mode = static_cast<int>(val % 16);
    if (mode != 2 && mode != 3) return false;
    int64_t base = val - mode;

    // country/service only reach slots 6..9, so they come off the top of
    // the integer first; each candidate window is at most a couple wide
    // because the postal+thermometer term stays below 64^7.
    int64_t svc0 = base / kP64[7] / 256;
    const int64_t svc_c[3] = {svc0, svc0 - 1, svc0 + 1};
    for (int64_t svc : svc_c) {
        if (svc < 0 || svc > 999) continue;
        int64_t V = base - 4 * svc * kP64[8];
        if (V < 0) continue;
        int64_t h = V / kP64[7];
        const int64_t cdiv_c[3] = {h - 1, h, h + 1};
        for (int64_t cdiv : cdiv_c) {
            if (cdiv < 0 || cdiv > 249) continue;
            for (int cmod = 0; cmod < 4; ++cmod) {
                int64_t country = cdiv * 4 + cmod;
                if (country > 999) continue;
                int64_t W = V - 16 * country * kP64[6];
                std::string postal;
                if (mode == 2) {
                    if (W < 0 || W % 16 != 0) continue;
                    int64_t Y = W / 16;  // plen*64^5 + postal decimal
                    int64_t plen = Y / kP64[5];
                    int64_t pi = Y % kP64[5];
                    if (plen < 1 || plen > 9) continue;
                    int64_t lim = 1;
                    for (int i = 0; i < plen; ++i) lim *= 10;
                    if (pi >= lim) continue;
                    postal = std::to_string(pi);
                    postal.insert(postal.begin(),
                                  static_cast<size_t>(plen) - postal.size(),
                                  '0');
                } else if (!mode3_postal(W, postal)) {
                    continue;
                }
                char f[4];
                std::snprintf(f, sizeof f, "%03d", static_cast<int>(country));
                out = postal + "<GS>" + f;
                std::snprintf(f, sizeof f, "%03d", static_cast<int>(svc));
                out += "<GS>";
                out += f;
                return true;
            }
        }
    }
    return false;
}

// Single-error fix for the primary block under the cw0 hypothesis
// already stored in cws[0] (position 0 is the hypothesis itself, never
// searched: an "error at 0" just means a wrong hypothesis, which another
// value catches). Data positions 1..9 are solved directly — column j is
// full weight (measured: min column weight 10/10), so syndrome row 0
// defines the value uniquely — and a unit syndrome is a proven
// parity-only error. Applies the fix to cws; on failure cws[1..19] is
// unchanged. See docs/formats/maxicode.md §Image pipeline.
bool fix_primary(std::vector<int>& cws) {
    int s[10];
    auto syndrome = [&] {
        for (int i = 0; i < 10; ++i) {
            int acc = cws[10 + i];
            for (int j = 0; j < 10; ++j) acc ^= gf64_mul(kMp[i][j], cws[j]);
            s[i] = acc;
        }
    };
    syndrome();
    bool clean = true;
    for (int v : s) clean = clean && (v == 0);
    if (clean) return true;
    for (int j = 1; j < 10; ++j) {
        int m0 = kMp[0][j];
        if (m0 == 0) continue;  // measured never (see above); skip if so
        int e = gf64_mul(s[0], gf64_inv(m0));
        if (e == 0) continue;  // s[0] == 0: position j is innocent
        bool hit = true;
        for (int i = 1; i < 10; ++i) {
            if (gf64_mul(kMp[i][j], e) != s[i]) { hit = false; break; }
        }
        if (hit) {
            cws[j] ^= e;
            syndrome();
            for (int v : s)
                if (v != 0) { cws[j] ^= e; return false; }  // undo
            return true;
        }
    }
    // Single parity-symbol error: exactly one nonzero syndrome symbol. A
    // data error can never present this way (full-weight columns), so the
    // data is proven good and cws stays untouched.
    int nz = 0;
    for (int v : s) nz += (v != 0);
    return nz == 1;
}

}  // namespace

bool verify_or_correct(std::vector<int>& cws, int mode) {
    if (verify_secondary(cws, mode)) return true;
    std::vector<int> save = cws;
    for (int block = 0; block < 2; ++block) {
        if (!correct_block(cws, mode, block)) {
            cws = save;
            return false;
        }
    }
    return true;
}

// Measured hexagon footprint shared by the upright and rotated samplers
// (docs/formats/maxicode.md §Hex geometry).
const int kHexFoot[10][2] = {
    {-1, 1}, {-2, 2}, {-3, 3}, {-4, 4}, {-4, 4},
    {-4, 4}, {-4, 4}, {-3, 3}, {-2, 2}, {-1, 1}};

// Vote one cell at a rotated position. (cx, cy) is the image position of
// the cell centre; (cos_t, sin_t) is clockwise-as-viewed; s is image px
// per symbol unit. Pixels are nearest reads; membership is tested in
// symbol frame (row dy = -4..+5, half-widths kHexFoot). At zero rotation
// with integer centre and s = 1 this reads exactly the upright
// footprint. Returns the voted bit, or -1 when fully off-image.
int rotated_cell_bit(const BinaryImage& bin, double cx, double cy,
                     double cos_t, double sin_t, double s) {
    int dark = 0, n = 0;
    const int r = static_cast<int>(std::ceil(6.0 * s)) + 1;
    for (int oy = -r; oy <= r; ++oy) {
        for (int ox = -r; ox <= r; ++ox) {
            const double u = (ox * cos_t + oy * sin_t) / s;
            const double v = (-ox * sin_t + oy * cos_t) / s;
            if (v < -4.5 || v >= 5.5) continue;
            const int dy = static_cast<int>(std::floor(v + 0.5));
            if (dy < -4 || dy > 5) continue;
            if (u < kHexFoot[dy + 4][0] - 0.5 ||
                u > kHexFoot[dy + 4][1] + 0.5)
                continue;
            const int x = static_cast<int>(std::floor(cx + ox + 0.5));
            const int y = static_cast<int>(std::floor(cy + oy + 0.5));
            if (x < 0 || y < 0 || x >= bin.width || y >= bin.height)
                continue;
            ++n;
            if (bin.bits[static_cast<size_t>(y) * bin.stride + x] == 0)
                ++dark;
        }
    }
    if (n == 0) return -1;
    return dark * 2 > n ? 1 : 0;
}

// Sample codewords through the rotated grid. (ccx, ccy) is the image
// position of symbol (sym_cx, sym_cy) (normally the bullseye (144.5,
// 149) at the ring center, or (149.5, 149) at the bbox center); s is
// image px per symbol unit. When max_mod is 20 only the primary is
// sampled (scorer path); 144 samples everything. When sparse, a 3x3
// center vote replaces the footprint vote. Returns false when any
// needed cell is off-image.
bool sample_tilted(const BinaryImage& bin, double cos_t, double sin_t,
                   double ccx, double ccy, double s, std::vector<int>& cws,
                   int max_mod, bool sparse, double sym_cx = 149.5,
                   double sym_cy = 149.0) {
    cws.assign(144, 0);
    for (int r = 0; r < 33; ++r) {
        for (int j = 0; j < 30; ++j) {
            CellMap cm = kCellMap[r][j];
            if (cm.module == 0 || cm.module > max_mod) continue;
            const double px0 = 4 + 10 * j + (r % 2 ? 5 : 0) - sym_cx;
            const double py0 = 9 * r + 4 - sym_cy;
            const double cx = ccx + s * (px0 * cos_t - py0 * sin_t);
            const double cy = ccy + s * (px0 * sin_t + py0 * cos_t);
            int bit;
            if (!sparse) {
                bit = rotated_cell_bit(bin, cx, cy, cos_t, sin_t, s);
                if (bit < 0) return false;
            } else {
                // Scorer path: 3x3 center vote (all taps interior —
                // rows -1..1 have half-width 4). Same majority rule,
                // ~7x fewer reads; the full vote re-checks hits.
                int dark = 0, n = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const double tx =
                            cx + s * (ox * cos_t - oy * sin_t);
                        const double ty =
                            cy + s * (ox * sin_t + oy * cos_t);
                        const int x = static_cast<int>(
                            std::floor(tx + 0.5));
                        const int y = static_cast<int>(
                            std::floor(ty + 0.5));
                        if (x < 0 || y < 0 || x >= bin.width ||
                            y >= bin.height)
                            continue;
                        ++n;
                        if (bin.bits[static_cast<size_t>(y) * bin.stride +
                                     x] == 0)
                            ++dark;
                    }
                }
                if (n == 0) return false;
                bit = dark * 2 > n ? 1 : 0;
            }
            cws[cm.module - 1] |= bit << (5 - cm.bit);
        }
    }
    return true;
}

// All cw0 values a real primary can carry: modes 4/5/6 plus the
// structured 2/3 values (mode + 16*postal-digit). Anything else is
// damage or garbage.
const int kRealCw0[11] = {2, 3, 4, 5, 6, 18, 19, 34, 35, 50, 51};

// 180-degree rotation of a binary image (pixel-exact: rotating twice
// is the identity, so no interpolation exists or is needed — the
// unchanged sampler then reads the exact upright codewords).
BinaryImage rot180_copy(const BinaryImage& bin) {
    BinaryImage out;
    out.width = bin.width;
    out.height = bin.height;
    out.stride = bin.width;
    out.bits.assign(static_cast<size_t>(bin.width) * bin.height, 1);
    for (int y = 0; y < bin.height; ++y) {
        for (int x = 0; x < bin.width; ++x) {
            if (bin.bits[static_cast<size_t>(y) * bin.stride + x] == 0)
                out.bits[static_cast<size_t>(bin.height - 1 - y) *
                             bin.width +
                         (bin.width - 1 - x)] = 0;
        }
    }
    return out;
}

// Dark-pixel bounding box (inclusive). Returns false when there is no
// dark pixel at all.
bool content_bbox(const BinaryImage& bin, int& x0, int& y0, int& x1,
                  int& y1) {
    x0 = bin.width;
    y0 = bin.height;
    x1 = -1;
    y1 = -1;
    for (int y = 0; y < bin.height; ++y) {
        for (int x = 0; x < bin.width; ++x) {
            if (bin.bits[static_cast<size_t>(y) * bin.stride + x] == 0) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    return x1 >= x0;
}

BinaryImage crop_copy(const BinaryImage& bin, int x0, int y0, int x1,
                      int y1) {
    BinaryImage out;
    out.width = x1 - x0 + 1;
    out.height = y1 - y0 + 1;
    out.stride = out.width;
    out.bits.assign(static_cast<size_t>(out.width) * out.height, 1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (bin.bits[static_cast<size_t>(y) * bin.stride + x] == 0)
                out.bits[static_cast<size_t>(y - y0) * out.stride +
                         (x - x0)] = 0;
        }
    }
    return out;
}

// Located decode: the frame as-is (identical to decode_image),
// rotated 180 degrees, then the dark-pixel bounding box (quiet zone)
// as-is and rotated — first success wins. No sampler changes: rotation
// is pixel-exact and cropping is exact when the bbox is (the sampler
// tolerates +/-2 px either way). Every attempt ends in the same EC
// verify; see docs/formats/maxicode.md §Image pipeline.
// Assembled-codeword decode: mode recovery (with primary fix),
// secondary recovery/correction, value decode. Shared by the
// upright sampler (decode_image) and the rotated sampler
// (decode_tilted).
bool decode_cws(std::vector<int>& cws, std::string& text) {
    // Modes 4/5/6 put a constant mode in cw0 (an unsampled module) — recover
    // it by trying the valid modes and keeping the one whose primary EC
    // matches. Modes 2/3 instead carry postal data in cw0..2, so cw0 is
    // not constant there; try the 64 possible cw0 values (the primary EC
    // has 10 parity codewords = 60 bits, so a wrong guess cannot pass).
    std::vector<int> p10(cws.begin(), cws.begin() + 10);
    int best_mode = 0, best_mism = 99;
    for (int m : {2, 3, 4, 5, 6}) {
        p10[0] = m;
        int mism = 0;
        for (int i = 0; i < 10; ++i) {
            int acc = 0;
            for (int j = 0; j < 10; ++j) acc ^= gf64_mul(kMp[i][j], p10[j]);
            if (acc != cws[10 + i]) ++mism;
        }
        if (mism < best_mism) { best_mism = mism; best_mode = m; }
    }
    // Only modes 4/5/6 have a CONSTANT mode codeword in cw0. Modes 2/3
    // carry postal data there, so the trial above can hit zero mismatches
    // at best_mode 2/3 purely by coincidence: mode 2 gives cw0 = 2 whenever
    // 16*postal mod 64 == 0 (postal_int % 4 == 0, 1 in 4 symbols) and mode 3
    // gives cw0 = 3 whenever the last postal char has 16*e_5 mod 64 == 0.
    // Taking the branch for those would run the structured primary through
    // the code-set decoder (measured: `123456000000` -> `P"G A SECONDARY
    // PAYLOAD`, primary lost). So accept the fast path only for mode >= 4;
    // everything else falls through to the cw0 brute force, which finds
    // v = 2 or 3 on its second/third iteration.
    int mode = 0;  // fast-path mode (4/5/6), else brute-force/2-3 path below
    if (best_mism == 0 && best_mode >= 4) {
        cws[0] = best_mode;
        mode = best_mode;
    } else {
        // Layer 2: single-error fix under a 4/5/6 hypothesis. First accept
        // wins — hypotheses' valid sets are at distance 11, so an
        // acceptance is unambiguous (docs/formats/maxicode.md §Image
        // pipeline). cws[0] keeps the hypothesis; fix_primary only touches
        // cws[1..19] and restores them on failure.
        for (int m : {4, 5, 6}) {
            cws[0] = m;
            if (fix_primary(cws)) { mode = m; break; }
        }
    }
    if (mode == 0) {
        // Not a 4/5/6 symbol: brute-force cw0 (modes 2/3 structured
        // primary) against the primary EC. v = 0 is excluded: a blank /
        // all-zero region samples to the zero vector, and the zero
        // message has zero parity, so v = 0 would match it "perfectly"
        // (measured: it turned blank + despeckle images into false
        // positives). No real primary has cw0 = 0 — mode 2 gives
        // cw0 in {2,18,34,50} and mode 3 in {3,19,35,51} (mode constant
        // + 16*postal-digit at 64^0; docs/formats/maxicode.md §structured
        // primary), and the 4/5/6 path already matched above.
        bool found = false;
        for (int v = 1; v < 64 && !found; ++v) {
            p10[0] = v;
            int mism = 0;
            for (int i = 0; i < 10; ++i) {
                int acc = 0;
                for (int j = 0; j < 10; ++j)
                    acc ^= gf64_mul(kMp[i][j], p10[j]);
                if (acc != cws[10 + i]) { mism = 1; break; }
            }
            if (mism == 0) { cws[0] = v; found = true; }
        }
        if (!found) {
            // Layer 4: single-error fix under the remaining hypotheses
            // ({4,5,6} already failed layer 2, so they are skipped).
            // decode_primary_fields below gates the mode to 2/3.
            for (int v = 1; v < 64 && !found; ++v) {
                if (v == 4 || v == 5 || v == 6) continue;
                cws[0] = v;
                if (fix_primary(cws)) found = true;
            }
        }
        if (!found) return false;
        // Modes 2/3: the mode is NOT in the symbol; the secondary message
        // uses the same SEC layout as mode 4 (84 data + 40 EC), so recover
        // the same unsampled bits before decoding.
        if (!recover_erasures(cws, 4)) return false;
        std::string secondary;
        if (!decode_secondary(cws, secondary)) return false;
        std::string primary;
        if (!decode_primary_fields(cws, primary)) return false;
        text = primary + "<GS>" + secondary;
        return true;
    }
    mode = cws[0];
    // Modes 4/6: SEC, secondary data cws 20..103. Mode 5: EEC, cws 20..87.
    // The whole secondary region is passed through: a bare-33 truncation
    // here would cut 220/252 data in latched-C/D state; the state machine
    // in decode_data finds the A/B-state pads itself.
    int sec_end = (mode == 5) ? 88 : 104;
    if (!recover_erasures(cws, mode)) return false;
    std::vector<int> data(cws.begin() + 1, cws.begin() + 10);
    for (int k = 20; k < sec_end; ++k) data.push_back(cws[k]);
    if (!decode_data(data, 0, static_cast<int>(data.size()), text))
        return false;
    return true;
}

bool decode_located(const BinaryImage& bin, std::string& text) {
    text.clear();
    if (decode_image(bin, text)) return true;
    if (bin.empty()) return false;
    BinaryImage flip = rot180_copy(bin);
    if (decode_image(flip, text)) return true;
    int x0, y0, x1, y1;
    if (content_bbox(bin, x0, y0, x1, y1) &&
        (x0 > 0 || y0 > 0 || x1 < bin.width - 1 || y1 < bin.height - 1)) {
        BinaryImage crop = crop_copy(bin, x0, y0, x1, y1);
        if (decode_image(crop, text)) return true;
        BinaryImage cflip = rot180_copy(crop);
        if (decode_image(cflip, text)) return true;
    }
    text.clear();
    return false;
}

// Full decode through the rotated grid: sample all 144 codewords at
// (theta_deg clockwise-as-viewed) about (ccx, ccy) with s image px per
// symbol unit, then run the shared codeword decode. Returns false when
// any needed cell is off-image or EC/valiation fails.
bool decode_tilted(const BinaryImage& bin, double theta_deg, double ccx,
                   double ccy, double s, std::string& text, double sym_cx,
                   double sym_cy) {
    text.clear();
    if (s <= 0.0) return false;
    const double rad = theta_deg * 3.141592653589793 / 180.0;
    const double cos_t = std::cos(rad);
    const double sin_t = std::sin(rad);
    std::vector<int> cws;
    if (!sample_tilted(bin, cos_t, sin_t, ccx, ccy, s, cws, 144, false,
                       sym_cx, sym_cy))
        return false;
    return decode_cws(cws, text);
}

// Tilted-photo decode (try_harder path): dark-bbox center and
// per-angle scale, 1-degree scan with the primary-fix scorer, full
// decode at hit angles. See docs/formats/maxicode.md §Image pipeline.
namespace {

// An enclosed light component: centroid and max radius from it.
struct Enc {
    double cx, cy, r;
};

// Light components (4-connectivity) that never touch the border.
//
// Two phases, because the first component is always the exterior and
// it is by far the largest: phase 1 floods ONLY from border light
// pixels and records a single visited bit per pixel (no pixel list, no
// centroid), phase 2 labels what phase 1 never reached -- which is by
// construction exactly the set of enclosed regions. Measured at
// 17 ms/Mpx either way, so this is a fallback, never a first attempt.
void enclosed_regions(const BinaryImage& bin, std::vector<Enc>& out) {
    const int w = bin.width, h = bin.height;
    if (w <= 2 || h <= 2) return;
    std::vector<unsigned char> seen(static_cast<size_t>(w) * h, 0);
    auto light = [&](int x, int y) {
        return bin.bits[static_cast<size_t>(y) * bin.stride + x] != 0;
    };
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    std::vector<int> stk, comp;
    auto seed = [&](int x, int y) {
        const size_t i = static_cast<size_t>(y) * w + x;
        if (!seen[i] && light(x, y)) {
            seen[i] = 1;
            stk.push_back(x * h + y);
        }
    };
    for (int x = 0; x < w; ++x) {
        seed(x, 0);
        seed(x, h - 1);
    }
    for (int y = 0; y < h; ++y) {
        seed(0, y);
        seed(w - 1, y);
    }
    while (!stk.empty()) {
        const int p = stk.back();
        stk.pop_back();
        const int x = p / h, y = p % h;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const size_t ni = static_cast<size_t>(ny) * w + nx;
            if (seen[ni] || !light(nx, ny)) continue;
            seen[ni] = 1;
            stk.push_back(nx * h + ny);
        }
    }
    for (int y0 = 0; y0 < h; ++y0) {
        for (int x0 = 0; x0 < w; ++x0) {
            const size_t i0 = static_cast<size_t>(y0) * w + x0;
            if (seen[i0] || !light(x0, y0)) continue;
            stk.clear();
            comp.clear();
            stk.push_back(x0 * h + y0);
            seen[i0] = 1;
            while (!stk.empty()) {
                const int p = stk.back();
                stk.pop_back();
                comp.push_back(p);
                const int x = p / h, y = p % h;
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const size_t ni = static_cast<size_t>(ny) * w + nx;
                    if (seen[ni] || !light(nx, ny)) continue;
                    seen[ni] = 1;
                    stk.push_back(nx * h + ny);
                }
            }
            if (comp.size() < 8) continue;
            double sx = 0.0, sy = 0.0;
            for (int p : comp) {
                sx += p / h;
                sy += p % h;
            }
            const double cx = sx / comp.size(), cy = sy / comp.size();
            double rmax = 0.0;
            for (int p : comp) {
                const double d = std::hypot(p / h - cx, p % h - cy);
                if (d > rmax) rmax = d;
            }
            out.push_back({cx, cy, rmax});
        }
    }
}

// The bullseye's three rings are complete circles, so the white inside
// them -- centre disc r <= 6.1, annuli 14.1-22.0 and 30.1-38.1 -- is
// white no flood fill from the border can reach. Cluster the enclosed
// components whose centroids agree (|ci-cj| <= 0.3*min(ri,rj), which
// pairs the concentric set and rejects an unrelated blob of any size);
// the largest cluster IS the bullseye. Emits up to `maxn` candidate
// centres, largest cluster first, each the component-wise median of its
// members. Measured on 216 cluttered positives: found 216/216, centre
// error mean 0.39 / max 0.50 symbol units against the tolerance of 3.
// On 240 negatives: 0 candidates.
void cluster_seeds(const std::vector<Enc>& e, std::vector<double>& out,
                   int maxn) {
    const int n = static_cast<int>(e.size());
    if (n < 2) return;
    std::vector<int> taken(static_cast<size_t>(n), 0);
    for (int round = 0; round < maxn; ++round) {
        std::vector<int> best;
        for (int i = 0; i < n; ++i) {
            if (taken[i]) continue;
            std::vector<int> c;
            for (int j = 0; j < n; ++j) {
                if (taken[j]) continue;
                const double d =
                    std::hypot(e[i].cx - e[j].cx, e[i].cy - e[j].cy);
                if (d <= 0.3 * std::min(e[i].r, e[j].r)) c.push_back(j);
            }
            if (static_cast<int>(c.size()) > static_cast<int>(best.size()))
                best = c;
        }
        if (static_cast<int>(best.size()) < 2) return;
        std::vector<double> xs, ys;
        for (int j : best) {
            xs.push_back(e[j].cx);
            ys.push_back(e[j].cy);
            taken[j] = 1;
        }
        std::sort(xs.begin(), xs.end());
        std::sort(ys.begin(), ys.end());
        out.push_back(xs[xs.size() / 2]);
        out.push_back(ys[ys.size() / 2]);
    }
}

}  // namespace

// ---- Affine fallback (perspective slice) -------------------------------
// A foreshortened symbol's rings are ellipses, not circles, and past
// ~2-3% distortion no uniform (theta, s, centre) hypothesis decodes
// anything (docs/formats/maxicode.md §Image pipeline, "Perspective:
// measured"). This recovers the affine map from the rings alone —
// (R/mid)^2 = u'Fu linearises the ellipse fit (F = (AA')^-1), the
// centre linearises the same way, both solved by 3x3 linear LS with
// Hartley-style normalization — then scans symbol rotation with EC as
// the arbiter. Runs only after the uniform path fails, so clean images
// never reach it; EC verification is the acceptance gate, as before.

// Symmetric 3x3 solve by Gauss-Jordan. False when singular.
static bool aff_solve3(double N[3][3], double y[3], double x[3]) {
    double A[3][4];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) A[i][j] = N[i][j];
        A[i][3] = y[i];
    }
    for (int i = 0; i < 3; ++i) {
        int p = i;
        for (int r = i + 1; r < 3; ++r)
            if (std::fabs(A[r][i]) > std::fabs(A[p][i])) p = r;
        if (std::fabs(A[p][i]) < 1e-12) return false;
        for (int c = i; c < 4; ++c) std::swap(A[i][c], A[p][c]);
        for (int r = 0; r < 3; ++r) {
            if (r == i) continue;
            const double f = A[r][i] / A[i][i];
            for (int c = i; c < 4; ++c) A[r][c] -= f * A[i][c];
        }
    }
    for (int i = 0; i < 3; ++i) x[i] = A[i][3] / A[i][i];
    return true;
}

// Symmetric 2x2 eigendecomposition: eigenvalues l1 >= l2 (not ordered
// here — the caller sorts) and the l1 eigenvector angle in degrees.
static void aff_eig2(double a, double b, double c, double& l1, double& l2,
                     double& ang) {
    const double tr = a + c, det = a * c - b * b;
    const double disc = std::sqrt(std::max(0.0, tr * tr / 4.0 - det));
    l1 = tr / 2.0 + disc;
    l2 = tr / 2.0 - disc;
    double v1x, v1y;
    if (std::fabs(b) > 1e-12) {
        v1x = b;
        v1y = l1 - a;
    } else {
        v1x = (a >= c) ? 1.0 : 0.0;
        v1y = (a >= c) ? 0.0 : 1.0;
    }
    ang = std::atan2(v1y, v1x) * 180.0 / 3.141592653589793;
}

struct AffPt {
    double x, y;  // image px
    double R;     // ring mid-radius in symbol units (42.1 only)
    int ray;
};

// Ring-mid points about (cx, cy): 128 rays at half-pixel steps; the
// midpoint of crossing pair (5,6) sits on the 42.1-unit circle with
// opposite-parity endpoints, so threshold bias cancels (the stage-2
// trick). Only the outer ring: it has the longest lever arm, no
// payload contact beyond it is possible from inside, and mixing radii
// with different resampling biases stalls the centre iteration in a
// ~2.5px orbit (measured, attic/maxi_perspfit.cpp). Returns the
// usable-ray count; rays with fewer than 6 crossings (off-symbol
// seeds, image edge) are dropped.
static int aff_collect(const BinaryImage& bin, double cx, double cy,
                       std::vector<AffPt>& out) {
    out.clear();
    int usable = 0;
    for (int k = 0; k < 128; ++k) {
        const double phi = k * 3.141592653589793 / 64.0;
        const double dx = std::cos(phi), dy = std::sin(phi);
        double tr[8];
        int nt = 0;
        double prev = -1.0;
        for (double r = 0.0; r < 4000.0; r += 0.5) {
            const int x = static_cast<int>(std::floor(cx + r * dx + 0.5));
            const int y = static_cast<int>(std::floor(cy + r * dy + 0.5));
            if (x < 0 || y < 0 || x >= bin.width || y >= bin.height) break;
            const double v =
                bin.bits[static_cast<size_t>(y) * bin.stride + x] == 0 ? 1.0
                                                                       : 0.0;
            if (prev >= 0.0 && v != prev) {
                if (nt < 8) tr[nt++] = r;
            }
            prev = v;
        }
        if (nt < 6) continue;
        ++usable;
        const double m2 = (tr[4] + tr[5]) / 2.0;
        out.push_back({cx + m2 * dx, cy + m2 * dy, 42.1, k});
    }
    return usable;
}

// Fit (R/mid)^2 = u'Fu about (cx,cy): 3-unknown linear LS for
// F = (AA')^-1. Fitting u'Eu instead fits the reciprocal ellipse --
// exact for uniform, silently wrong under distortion.
static bool aff_fit_F(const std::vector<AffPt>& pts, double cx, double cy,
                      double F[3]) {
    double N[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    double y[3] = {0, 0, 0};
    for (size_t i = 0; i < pts.size(); ++i) {
        const double dx = pts[i].x - cx, dy = pts[i].y - cy;
        const double rr = dx * dx + dy * dy;
        if (rr < 1e-9) continue;
        const double rho = std::sqrt(rr);
        const double ux = dx / rho, uy = dy / rho;
        const double lhs =
            (pts[i].R / rho) * (pts[i].R / rho);
        const double a[3] = {ux * ux, 2 * ux * uy, uy * uy};
        for (int r = 0; r < 3; ++r) {
            y[r] += a[r] * lhs;
            for (int c = 0; c < 3; ++c) N[r][c] += a[r] * a[c];
        }
    }
    return aff_solve3(N, y, F);
}

// With F fixed, solve for the centre: (p-c)'F(p-c) = R^2 linearises to
// u'Hu - R^2 = 2q'u - d with q = Hd. Shifted to the centroid and scaled
// by the RMS radius (Hartley-style): absolute image coordinates make
// the unnormalized system singular to working precision and the
// iteration wanders instead of contracting.
static bool aff_fit_centre(const std::vector<AffPt>& pts, const double F[3],
                           double& ccx, double& ccy) {
    double mx = 0, my = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        mx += pts[i].x;
        my += pts[i].y;
    }
    if (pts.empty()) return false;
    mx /= pts.size();
    my /= pts.size();
    double s0 = 0;
    for (size_t i = 0; i < pts.size(); ++i)
        s0 += (pts[i].x - mx) * (pts[i].x - mx) +
              (pts[i].y - my) * (pts[i].y - my);
    s0 = std::sqrt(s0 / pts.size());
    if (s0 < 1e-9) return false;
    const double s02 = s0 * s0;
    const double H[3] = {s02 * F[0], s02 * F[1], s02 * F[2]};
    double N[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    double y[3] = {0, 0, 0};
    for (size_t i = 0; i < pts.size(); ++i) {
        const double ux = (pts[i].x - mx) / s0, uy = (pts[i].y - my) / s0;
        const double uhu =
            H[0] * ux * ux + 2 * H[1] * ux * uy + H[2] * uy * uy;
        const double lhs = uhu - pts[i].R * pts[i].R;
        const double a[3] = {2 * ux, 2 * uy, -1.0};
        for (int r = 0; r < 3; ++r) {
            y[r] += a[r] * lhs;
            for (int c = 0; c < 3; ++c) N[r][c] += a[r] * a[c];
        }
    }
    double x[3];
    if (!aff_solve3(N, y, x)) return false;
    const double hdet = H[0] * H[2] - H[1] * H[1];
    if (std::fabs(hdet) < 1e-12) return false;
    const double dx = (H[2] * x[0] - H[1] * x[1]) / hdet;
    const double dy = (-H[1] * x[0] + H[0] * x[1]) / hdet;
    ccx = mx + s0 * dx;
    ccy = my + s0 * dy;
    return true;
}

// Sample codewords through an affine grid: cell centres through A,
// footprint taps through its polar rotation at the mean singular
// scale (the sheared footprint's majority vote is insensitive to the
// residual shape error). Otherwise identical to sample_tilted,
// including the off-image rule.
static bool sample_affine(const BinaryImage& bin, double a11, double a12,
                          double a21, double a22, double fcos, double fsin,
                          double ccx, double ccy, double sm,
                          std::vector<int>& cws, int max_mod, bool sparse,
                          double sym_cx = 144.5, double sym_cy = 149.0) {
    cws.assign(144, 0);
    for (int r = 0; r < 33; ++r) {
        for (int j = 0; j < 30; ++j) {
            CellMap cm = kCellMap[r][j];
            if (cm.module == 0 || cm.module > max_mod) continue;
            const double px0 = 4 + 10 * j + (r % 2 ? 5 : 0) - sym_cx;
            const double py0 = 9 * r + 4 - sym_cy;
            const double cx = ccx + a11 * px0 + a12 * py0;
            const double cy = ccy + a21 * px0 + a22 * py0;
            int bit;
            if (!sparse) {
                bit = rotated_cell_bit(bin, cx, cy, fcos, fsin, sm);
                if (bit < 0) return false;
            } else {
                int dark = 0, n = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const double tx =
                            cx + sm * (ox * fcos - oy * fsin);
                        const double ty =
                            cy + sm * (ox * fsin + oy * fcos);
                        const int x = static_cast<int>(
                            std::floor(tx + 0.5));
                        const int y = static_cast<int>(
                            std::floor(ty + 0.5));
                        if (x < 0 || y < 0 || x >= bin.width ||
                            y >= bin.height)
                            continue;
                        ++n;
                        if (bin.bits[static_cast<size_t>(y) * bin.stride +
                                      x] == 0)
                            ++dark;
                    }
                }
                if (n == 0) return false;
                bit = dark * 2 > n ? 1 : 0;
            }
            cws[cm.module - 1] |= bit << (5 - cm.bit);
        }
    }
    return true;
}

// Affine decode from an explicit seed: fit the rings, gate the fit,
// scan symbol rotation with the primary-fix scorer, full decode at
// hits. The fit needs no scale estimate and no ring consensus, so this
// also runs when stage 2 refuses (its span windows assume circles).
static bool try_affine_seed(const BinaryImage& bin, double ccx, double ccy,
                            std::string& text) {
    text.clear();
    // Fixed rounds, no convergence loop: F about the seed, one centre
    // solve, refit F about the new centre, trim, final F. Iterating to
    // convergence stalls instead of contracting on real rows (measured:
    // a ~2.5px orbit from 16px-off seeds at s4), so the gate on the
    // trimmed residual arbitrates quality, not convergence.
    std::vector<AffPt> pts;
    if (aff_collect(bin, ccx, ccy, pts) < 64) {
        return false;
    }
    double F[3] = {0, 0, 0};
    if (!aff_fit_F(pts, ccx, ccy, F)) return false;
    double cx = ccx, cy = ccy;
    if (!aff_fit_centre(pts, F, cx, cy)) return false;
    pts.clear();
    if (aff_collect(bin, cx, cy, pts) < 64) {
        return false;
    }
    if (!aff_fit_F(pts, cx, cy, F)) return false;
    // Robust refit: drop the worst eighth of rays (payload-merged
    // crossings survive the count gate but not the shape), then gate
    // the trimmed residual. Measured: good fits <= 0.09, garbage >=
    // 0.92 (attic/maxi_perspfit.cpp).
    pts.clear();
    if (aff_collect(bin, cx, cy, pts) < 64) {
        return false;
    }
    if (!aff_fit_F(pts, cx, cy, F)) return false;
    {
        double rres[128] = {0};
        for (size_t i = 0; i < pts.size(); ++i) {
            const int k = pts[i].ray;
            if (k < 0 || k >= 128) continue;
            const double dx = pts[i].x - cx, dy = pts[i].y - cy;
            const double rho = std::sqrt(dx * dx + dy * dy);
            if (rho < 1e-9) continue;
            const double ux = dx / rho, uy = dy / rho;
            const double q =
                F[0] * ux * ux + 2 * F[1] * ux * uy + F[2] * uy * uy;
            if (q <= 0.0) continue;
            const double model = pts[i].R / std::sqrt(q);
            const double d = std::fabs(rho - model) / model;
            if (d > rres[k]) rres[k] = d;
        }
        int order[128];
        for (int i = 0; i < 128; ++i) order[i] = i;
        std::sort(order, order + 128,
                  [&](int a, int b) { return rres[a] < rres[b]; });
        bool keep[128] = {false};
        for (int i = 0; i < 112; ++i) keep[order[i]] = true;
        std::vector<AffPt> tpts;
        for (size_t i = 0; i < pts.size(); ++i)
            if (pts[i].ray >= 0 && pts[i].ray < 128 &&
                keep[pts[i].ray])
                tpts.push_back(pts[i]);
        if (tpts.size() < 96) return false;
        if (!aff_fit_F(tpts, cx, cy, F)) return false;
        double worst = 0;
        for (size_t i = 0; i < tpts.size(); ++i) {
            const double dx = tpts[i].x - cx, dy = tpts[i].y - cy;
            const double rho = std::sqrt(dx * dx + dy * dy);
            if (rho < 1e-9) continue;
            const double ux = dx / rho, uy = dy / rho;
            const double q =
                F[0] * ux * ux + 2 * F[1] * ux * uy + F[2] * uy * uy;
            if (q <= 0.0) continue;
            const double model = tpts[i].R / std::sqrt(q);
            const double d = std::fabs(rho - model) / model;
            if (d > worst) worst = d;
        }
        if (worst >= 0.15) {
            return false;
        }
    }
    // F^-1 = AA': eigendecomposition gives the ellipse orientation U
    // and the singular scales P. The symbol rotation is unknown, so
    // A(theta) = U P R(theta); its polar rotation is U R(theta).
    double fi[3];
    {
        const double det = F[0] * F[2] - F[1] * F[1];
        if (std::fabs(det) < 1e-12) return false;
        fi[0] = F[2] / det;
        fi[1] = -F[1] / det;
        fi[2] = F[0] / det;
    }
    double pi1, pi2, phiU;
    aff_eig2(fi[0], fi[1], fi[2], pi1, pi2, phiU);
    if (pi1 <= 0.0 || pi2 <= 0.0) return false;
    const double p1 = std::sqrt(pi1), p2 = std::sqrt(pi2);
    if (p1 / p2 > 4.0) return false;  // wilder than any measured row
    const double s_mean = std::sqrt(p1 * p2);
    if (!(s_mean > 0.0)) return false;
    const double ucr = std::cos(phiU * 3.141592653589793 / 180.0);
    const double usr = std::sin(phiU * 3.141592653589793 / 180.0);
    const double rad = 3.141592653589793 / 180.0;
    const double ratios[21] = {1.00, 1.01, 0.99, 1.02, 0.98, 1.03, 0.97,
                               1.04, 0.96, 1.05, 0.95, 1.06, 0.94, 1.07,
                               0.93, 1.08, 0.92, 1.09, 0.91, 1.10, 0.90};
    for (int ri = 0; ri < 21; ++ri) {
        const double q1 = ratios[ri] * p1, q2 = ratios[ri] * p2;
        const double sm = ratios[ri] * s_mean;
        for (int d = 0; d < 360; ++d) {
            const double co = std::cos(d * rad);
            const double si = std::sin(d * rad);
            // A = U P R: P R first, then U.
            const double b11 = q1 * co, b12 = -q1 * si;
            const double b21 = q2 * si, b22 = q2 * co;
            const double a11 = ucr * b11 + -usr * b21;
            const double a12 = ucr * b12 + -usr * b22;
            const double a21 = usr * b11 + ucr * b21;
            const double a22 = usr * b12 + ucr * b22;
            const double fc = std::cos((phiU + d) * rad);
            const double fs = std::sin((phiU + d) * rad);
            std::vector<int> p10;
            if (!sample_affine(bin, a11, a12, a21, a22, fc, fs, cx, cy,
                               sm, p10, 20, true, 144.5, 149.0))
                continue;
            bool hit = false;
            for (int v = 0; v < 11 && !hit; ++v) {
                std::vector<int> tmp(p10.begin(), p10.begin() + 20);
                tmp[0] = kRealCw0[v];
                if (fix_primary(tmp)) hit = true;
            }
            if (!hit) continue;
            std::vector<int> cws;
            if (!sample_affine(bin, a11, a12, a21, a22, fc, fs, cx, cy,
                               sm, cws, 144, false, 144.5, 149.0))
                continue;
            if (decode_cws(cws, text)) {
                return true;
            }
        }
    }
    text.clear();
    return false;
}

// Stages 2-4 and the scan, from an explicit seed. `decode_photo` calls
// this twice at most: once with the dark-pixel bbox centre (the path
// every clean image already takes, unchanged), once with the enclosed
// white cluster when that fails. Nothing here assumes the seed came
// from a bbox -- stage 2's ray windows tolerate any origin inside the
// rings and stage 3 refines the centre to well under a pixel. Every
// uniform-path failure (no spans, no consensus, gate, scan) falls
// through to the affine fit, which needs no scale estimate and no ring
// consensus; clean images never reach it.
static bool decode_from_seed(const BinaryImage& bin, double ccx, double ccy,
                             std::string& text) {
    // Scale from the SPAN between the first and fourth ring-edge
    // crossings along 32 rays. The six edges sit at 6.1, 14.1, 22.0,
    // 30.1, 38.1 and 46.1 symbol units, and the first four crossings
    // seen from an origin anywhere inside the bullseye are a window of
    // four consecutive edges: 6.1..30.1, 14.1..38.1 or 22.0..46.1. All
    // three windows are 24 units wide, so no assumption about which
    // band the origin landed in is needed. Both endpoints are detected
    // by the same threshold rule, so the one-pixel edge bias cancels —
    // unlike per-band widths, where binarization widens dark bands and
    // narrows light ones (measured 9 px / 8 px for a true 8 px scale,
    // i.e. a median that runs 6% high and drags the ring template off
    // the bullseye). Rays that leave the image before the fourth
    // crossing are dropped.
    std::vector<double> spans;
    for (int k = 0; k < 32; ++k) {
        const double phi = k * 3.141592653589793 / 16.0;
        const double dx = std::cos(phi), dy = std::sin(phi);
        std::vector<double> tr;
        double prev = -1.0;
        for (double r = 0.0;; r += 1.0) {
            const int x =
                static_cast<int>(std::floor(ccx + r * dx + 0.5));
            const int y =
                static_cast<int>(std::floor(ccy + r * dy + 0.5));
            if (x < 0 || y < 0 || x >= bin.width || y >= bin.height)
                break;
            const double v =
                bin.bits[static_cast<size_t>(y) * bin.stride + x] == 0
                    ? 1.0
                    : 0.0;
            if (prev >= 0.0 && v != prev) {
                tr.push_back(r);
                if (tr.size() == 4) break;
            }
            prev = v;
        }
        if (tr.size() < 4) continue;
        // Two overlapping 16-unit windows, each bounded by edges of the
        // same parity (both light->dark or both dark->light), so the
        // threshold bias that shifts a light->dark crossing by a pixel
        // cancels exactly; summed they give a 32-unit baseline.
        const double span = (tr[2] - tr[0]) + (tr[3] - tr[1]);
        if (span > 0.0) spans.push_back(span);
    }
    if (spans.size() < 16) return try_affine_seed(bin, ccx, ccy, text);
    std::sort(spans.begin(), spans.end());
    const double med = spans[spans.size() / 2];
    int close = 0;
    for (double d : spans) {
        if (std::fabs(d - med) <= 0.25 * med) ++close;
    }
    // No ring consensus (foreshortened spans disagree, or no bullseye):
    // the uniform scan below assumes circles, so skip it and let the
    // affine fit arbitrate instead.
    if (close * 3 < (int)spans.size() * 2)
        return try_affine_seed(bin, ccx, ccy, text);
    const double s_est = med / 32.0;
    if (!(s_est > 0.0)) return try_affine_seed(bin, ccx, ccy, text);
    // Bullseye centre: centroid of the dark annuli. The three rings are
    // complete circles centred on the bullseye, so the centroid of the
    // dark pixels inside them is the ring centre to well under a pixel
    // (>= 3900 ring px even at s = 1) — independent of payload and of
    // rotation, unlike the ink-bbox centre, which drifts with whichever
    // edge cells happen to be dark.
    {
        // Radii shrink each pass: 60 and 55 always cover the whole ring
        // set from an ink-bbox centre (a few units out), 47 and 46.5
        // then trim the mask to the rings so payload cells cannot pull
        // the mean (the nearest mapped cell is r = 51.5; everything
        // inside that is a bullseye-blocked constant cell). Skipping
        // the wide passes and going straight to 46.5 clips the far
        // side of the rings and lands ~5 px off — too far to decode.
        const double kRadii[4] = {60.0, 55.0, 47.0, 46.5};
        for (int pass = 0; pass < 4; ++pass) {
            const double R = kRadii[pass] * s_est;
            const int R2 = static_cast<int>(R * R);
            const int ex = static_cast<int>(R) + 1;
            double sx = 0.0, sy = 0.0;
            int n = 0;
            for (int oy = -ex; oy <= ex; ++oy) {
                for (int ox = -ex; ox <= ex; ++ox) {
                    if (ox * ox + oy * oy > R2) continue;
                    const int x = static_cast<int>(std::floor(ccx + ox + 0.5));
                    const int y = static_cast<int>(std::floor(ccy + oy + 0.5));
                    if (x < 0 || y < 0 || x >= bin.width || y >= bin.height)
                        continue;
                    if (bin.bits[static_cast<size_t>(y) * bin.stride + x] == 0) {
                        sx += x;
                        sy += y;
                        ++n;
                    }
                }
            }
            if (n < 64) break;  // not a bullseye; keep the bbox centre
            ccx = sx / n;
            ccy = sy / n;
        }
    }
    // Bullseye gate: the rings are complete circles, so the dark
    // fraction sampled on the three ring mid-radii must be high and the
    // dark fraction on the light radii must be low. This is far more
    // tolerant of a few-percent scale error than a template match (a
    // 6% radius error still lands inside a band 8 units wide, where it
    // drags a whole-ring normalized cross correlation down to ~0.65)
    // and it costs ~400 pixel reads instead of ~10^6. Rejected images
    // never reach the 21 x 360 scale/angle scan, so this stays a
    // time-saver only; EC verification is the real acceptance gate.
    {
        const double kDark[3] = {10.1, 26.0, 42.1};
        const double kLight[3] = {2.0, 18.0, 34.0};
        const int kSamp = 64;
        double score = 0.0;
        for (int g = 0; g < 6; ++g) {
            const bool want_dark = (g < 3);
            const double ru =
                (want_dark ? kDark[g] : kLight[g - 3]) * s_est;
            int dark = 0;
            for (int a = 0; a < kSamp; ++a) {
                const double th = a * 3.141592653589793 * 2.0 / kSamp;
                const int x = static_cast<int>(
                    std::floor(ccx + ru * std::cos(th) + 0.5));
                const int y = static_cast<int>(
                    std::floor(ccy + ru * std::sin(th) + 0.5));
                if (x < 0 || y < 0 || x >= bin.width || y >= bin.height)
                    continue;
                if (bin.bits[static_cast<size_t>(y) * bin.stride + x] == 0)
                    ++dark;
            }
            const double frac = (double)dark / kSamp;
            score += want_dark ? frac : (1.0 - frac);
        }
        score /= 6.0;
        // A foreshortened bullseye fails the circle gate by design
        // (radii vary with angle), so it falls through to the affine
        // fit instead of refusing here.
        if (score < 0.75) return try_affine_seed(bin, ccx, ccy, text);
    }
    const double rcx = ccx, rcy = ccy;
    const double rad = 3.141592653589793 / 180.0;
    // Scale bracket: the span median quantizes to whole pixels and
    // moves a few percent with the binarization threshold, so search
    // outward from the estimate; the exact scorer arbitrates each
    // combination.
    const double ratios[21] = {1.00, 1.01, 0.99, 1.02, 0.98, 1.03, 0.97,
                               1.04, 0.96, 1.05, 0.95, 1.06, 0.94, 1.07,
                               0.93, 1.08, 0.92, 1.09, 0.91, 1.10, 0.90};
    for (int ri = 0; ri < 21; ++ri) {
        const double sm = s_est * ratios[ri];
        for (int d = 0; d < 360; ++d) {
            const double theta = d;
            const double co = std::cos(theta * rad);
            const double si = std::sin(theta * rad);
            std::vector<int> p10;
            if (!sample_tilted(bin, co, si, rcx, rcy, sm, p10, 20, true,
                               144.5, 149.0))
                continue;
            bool hit = false;
            for (int v = 0; v < 11 && !hit; ++v) {
                std::vector<int> tmp(p10.begin(), p10.begin() + 20);
                tmp[0] = kRealCw0[v];
                if (fix_primary(tmp)) hit = true;
            }
            if (hit && decode_tilted(bin, theta, rcx, rcy, sm, text,
                                     144.5, 149.0))
                return true;
        }
    }
    // The uniform scan assumes circles; a foreshortened symbol exhausts
    // it without a hit. The affine fit gets the last word.
    return try_affine_seed(bin, ccx, ccy, text);
}

// Tilted-photo decode. Two seeds, in order:
//   1. the dark-pixel bbox centre -- byte-for-byte the path every clean
//      image took before the clutter slice existed;
//   2. the enclosed-white cluster (see cluster_seeds), which does not
//      care what else is dark in the frame and therefore survives
//      clutter, where the bbox centre is wherever the background happens
//      to reach.
// Stage 2's consensus gate and EC arbitrate every candidate, so the
// second seed can only add decodes, never accept garbage. See
// docs/formats/maxicode.md §Image pipeline ("Clutter: measured").
bool decode_photo(const BinaryImage& bin, std::string& text) {
    text.clear();
    if (bin.width < 32 || bin.height < 32) return false;
    int x0, y0, x1, y1;
    if (content_bbox(bin, x0, y0, x1, y1) &&
        decode_from_seed(bin, (x0 + x1) / 2.0, (y0 + y1) / 2.0, text))
        return true;
    text.clear();
    std::vector<Enc> enc;
    enclosed_regions(bin, enc);
    std::vector<double> seeds;
    cluster_seeds(enc, seeds, 3);
    for (size_t i = 0; i + 1 < seeds.size(); i += 2)
        if (decode_from_seed(bin, seeds[i], seeds[i + 1], text)) return true;
    text.clear();
    return false;
}

// Sample the hex grid, assemble 144 codewords, verify EC, decode.
bool decode_image(const BinaryImage& bin, std::string& text) {
    text.clear();
    if (bin.width < 32 || bin.height < 32) return false;
    // zint renders no border: the whole image IS the symbol, 299x298 at
    // scale 1 (attic/maxi_cal2.py). Grid cell centre: x = 4 + 10j + (5 if
    // odd row), y = 4 + 9r (integer centres, attic/maxi_geo.py). The x
    // origin is 4, not 14: 30 columns must fit on 299 px, and the old
    // x = 14 + 10j lattice left the leftmost column (33 cells) outside
    // the map entirely -- zxing-cpp refuses a symbol missing it, and the
    // decoder had to recover those bits by erasure recovery. Re-derived
    // at x0 = 4 (attic/maxi_mapx.py, 829/829 old entries unchanged at
    // index +1; see docs/formats/maxicode.md §Hex geometry).
    //
    // PER-AXIS scale: zint's --scale is NOT uniform (measured: 299x298,
    // 598x564, 1495x1428 -- attic/maxi_cal*.py), so x uses W/299 and y
    // uses H/298. A single width-derived scale put scale-2/5 renders off
    // by ~1px per row (32px+ accumulated) and they all failed. Verified
    // 862/862 mapped cells at scales 1, 2 and 5 (see
    // docs/formats/maxicode.md §image pipeline).
    double sx = static_cast<double>(bin.width) / 299.0;
    double sy = static_cast<double>(bin.height) / 298.0;
    if (sx <= 0.0 || sy <= 0.0) return false;
    std::vector<int> cws(144, 0);
    // Majority vote over the cell's MEASURED hexagon footprint rather than
    // a single centre pixel: rows dy = -4..+5 about y = 9r+4 with
    // half-widths 1,2,3,4,4,4,4,3,2,1 (docs/formats/maxicode.md
    // §Hex geometry). The footprint is cell-private — horizontal neighbours
    // sit at cx +/- 10*sx with half-width 4*sx, and vertical neighbours
    // start exactly on the shared row dy = +5 — so a vote still reads THIS
    // cell. Measured (attic/maxi_noise.cpp, one <noisy> <ref> pair per
    // image): on the 0.5%-flip corpus noise variant centre sampling
    // corrupts 43 codewords over 18 images and all 18 reject, the vote
    // corrupts 0; blur is 0 either way.
    for (int r = 0; r < 33; ++r) {
        for (int j = 0; j < 30; ++j) {
            CellMap cm = kCellMap[r][j];
            if (cm.module == 0) continue;
            int px = 4 + 10 * j + (r % 2 ? 5 : 0);
            int py = 9 * r + 4;
            int cx = static_cast<int>(px * sx);
            int cy = static_cast<int>(py * sy);
            int dark = 0, n = 0;
            for (int dy = -4; dy <= 5; ++dy) {
                // Band [cy + int(dy*sy), cy + int((dy+1)*sy)) so the rows
                // tile the footprint with no gaps and no double-counting at
                // any sy >= 1.
                int y0 = cy + static_cast<int>(dy * sy);
                int y1 = cy + static_cast<int>((dy + 1) * sy);
                int x0 = cx + static_cast<int>(kHexFoot[dy + 4][0] * sx);
                int x1 = cx + static_cast<int>(kHexFoot[dy + 4][1] * sx);
                for (int y = y0; y < y1; ++y) {
                    if (y < 0 || y >= bin.height) continue;
                    for (int x = x0; x <= x1; ++x) {
                        if (x < 0 || x >= bin.width) continue;
                        ++n;
                        if (bin.bits[static_cast<size_t>(y) * bin.stride + x] ==
                            0)
                            ++dark;
                    }
                }
            }
            int bit;
            if (n > 0) {
                bit = dark * 2 > n ? 1 : 0;
            } else {
                // Footprint collapsed (sub-module sampling): fall back to the
                // centre pixel, which is what the first version always used.
                bit = is_dark(bin, cx, cy) ? 1 : 0;
            }
            cws[cm.module - 1] |= bit << (5 - cm.bit);
        }
    }
    return decode_cws(cws, text);
}

}  // namespace maxicode
}  // namespace omniscan
