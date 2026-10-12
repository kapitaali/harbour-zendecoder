// Grid Matrix native image level (Tier-2). Ports attic/gm_img2.py
// (localize + frame-checkerboard version detect) and the validated
// full-stream decode (Python e2e 104/80 + v6 D72/130/157 full-stream
// check + emitted-tables file check "Hello Grid Matrix!" EXACT).
// Contract: docs/formats/grid_matrix.md steps 2/7-9/11/13/15-17.
// Scope (v1, documented): flush renders (no quiet zone, step-1 note),
// integer module scale, axis-aligned 0/180 (90/270 safely refused),
// default EC levels (tables default-only; others safely refused via RS),
// single + full-stream 2-block (any D; pads read via table positions).
// Real clutter / arbitrary rotation / fractional scale are follow-up
// (refused safely here). False (nothing appended) otherwise;
// never throws.
#include "stacked_gridmatrix.h"

#include <string>
#include <vector>

#include "gridmatrix_tables.h"
#include "omniscan/binarizer.h"
#include "omniscan/geometry.h"
#include "omniscan/result.h"

namespace omniscan {
namespace gridmatrix {
namespace {

// Dark-pixel bbox (BinaryImage 1=light, 0=dark ink). False if
// empty/tiny/highly non-square.
inline bool localize(const BinaryImage& bin, int& x0, int& y0, int& x1,
                     int& y1) {
    x0 = bin.width;
    y0 = bin.height;
    x1 = y1 = -1;
    for (int y = 0; y < bin.height; ++y)
        for (int x = 0; x < bin.width; ++x)
            if (!bin.at(x, y)) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < x0) return false;
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w < 12 || h < 12) return false;
    const int d = w > h ? w - h : h - w;
    if (d > 2 && d * 20 > w) return false;
    return true;
}

// Frame module center pixel.
inline int frame_px(const BinaryImage& bin, int x0, int y0, int s, int br,
                    int bc, int i, int j) {
    return bin.at(x0 + (bc * 6 + j) * s + s / 2,
                  y0 + (br * 6 + i) * s + s / 2);
}

// Version detect: M in {3,5..27} with integer s=W/(6M) where every macro
// frame is uniform (alternation hardening is follow-up; uniformity +
// integer size gates M uniquely on clean renders, proven by test-duplicate
// stage checks). Returns version or 0.
inline int detect_version(const BinaryImage& bin, int x0, int y0, int x1,
                          int y1) {
    const int W = x1 - x0 + 1, H = y1 - y0 + 1;
    if (W != H) return 0;
    for (int M = 3; M <= 27; M += 2) {
        const int G = 6 * M;
        if (W % G != 0) continue;
        const int s = W / G;
        if (s < 1) continue;
        bool ok = true;
        for (int br = 0; br < M && ok; ++br)
            for (int bc = 0; bc < M && ok; ++bc) {
                int v = -1;
                for (int e = 0; e < 6 && ok; ++e) {
                    const int f[4] = {
                        frame_px(bin, x0, y0, s, br, bc, 0, e),
                        frame_px(bin, x0, y0, s, br, bc, 5, e),
                        frame_px(bin, x0, y0, s, br, bc, e, 0),
                        frame_px(bin, x0, y0, s, br, bc, e, 5)};
                    for (int t = 0; t < 4 && ok; ++t) {
                        if (v < 0)
                            v = f[t];
                        else if (f[t] != v)
                            ok = false;
                    }
                }
            }
        if (ok) return (M - 1) / 2;
    }
    return 0;
}

// Sample module grid (6M x 6M, 1=dark) at integer scale s.
inline void sample_grid(const BinaryImage& bin, int x0, int y0, int M,
                        int s, std::vector<uint8_t>& grid) {
    const int G = 6 * M;
    grid.assign(static_cast<size_t>(G) * G, 0);
    for (int r = 0; r < G; ++r)
        for (int c = 0; c < G; ++c)
            grid[static_cast<size_t>(r) * G + c] =
                bin.at(x0 + c * s + s / 2, y0 + r * s + s / 2) ? 0 : 1;
}

// Read one codeword (7 bits MSB-first) from grid via table entry
// ((br,bc), ks[7] MSB-first; (-1,-1,-1) slots read 0). Grid stride G.
inline int read_cw(const std::vector<uint8_t>& grid, int G,
                   const int8_t (*ks)[3]) {
    int v = 0;
    for (int j = 0; j < 7; ++j) {
        int bit = 0;
        if (ks[j][0] >= 0) {
            const int ii = ks[j][2] / 4, jj = ks[j][2] % 4;
            bit = grid[static_cast<size_t>(ks[j][0] * 6 + 1 + ii) * G +
                       (ks[j][1] * 6 + 1 + jj)];
        }
        v = (v << 1) | bit;
    }
    return v;
}

}  // namespace

bool try_decode_gridmatrix(const BinaryImage& bin,
                           std::vector<Result>& out) {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!localize(bin, x0, y0, x1, y1)) return false;
    const int vers = detect_version(bin, x0, y0, x1, y1);
    if (vers < 1 || vers > 13) return false;
    const GMVersion* T = gm_version(vers);
    if (!T) return false;
    const int M = 2 * vers + 1;
    if (T->m != M) return false;
    const int W = x1 - x0 + 1, G = 6 * M;
    if (W % G != 0) return false;
    const int s = W / G;
    if (s < 1) return false;
    const int H = T->h, K = T->k;
    if (H <= 0 || K <= 0) return false;
    std::vector<uint8_t> grid0;
    sample_grid(bin, x0, y0, M, s, grid0);
    std::vector<uint8_t> grid180(grid0.size(), 0);
    for (int r = 0; r < G; ++r)
        for (int c = 0; c < G; ++c)
            grid180[static_cast<size_t>(r) * G + c] =
                grid0[static_cast<size_t>(G - 1 - r) * G + (G - 1 - c)];
    const int c0 = (M - 1) / 2;
    for (int rot = 0; rot < 2; ++rot) {
        const std::vector<uint8_t>& g = (rot == 0) ? grid0 : grid180;
        auto cw0 = [&]() -> int {
            int v = 0;
            for (int j = 0; j < 7; ++j) {
                const int kk = 9 + j;
                const int ii = kk / 4, jj = kk % 4;
                v = (v << 1) |
                    g[static_cast<size_t>(c0 * 6 + 1 + ii) * G +
                      (c0 * 6 + 1 + jj)];
            }
            return v;
        };
        // The core in fill-index order: cw0, data 1..H-1, EC1. That is
        // exactly N(v) codewords.
        std::vector<int> s1;
        s1.reserve(static_cast<size_t>(H + K));
        s1.push_back(cw0());
        for (int c = 1; c < H; ++c) {
            if (c - 1 >= T->ndata) {
                s1.push_back(0);
                continue;
            }
            s1.push_back(read_cw(g, G, T->data[c - 1]));
        }
        for (int i = 0; i < K; ++i) s1.push_back(read_cw(g, G, T->ec1[i]));
        // Single block, ALL EC LEVELS (step 21b). s1 is the core read in
        // fill-index order (cw0, data 1..H-1, EC1), which is exactly
        // [data(h_s) | EC1(k_s)] for EVERY level, because the fill order is
        // level-independent and h_s + k_s == N(v) always. So the same block
        // is handed to decode_gm_stream, which tries each level's exact
        // (h, k) and keeps the longest parse. No per-level tables are needed
        // and no h search remains.
        std::vector<int> low1(s1.rbegin(), s1.rend());
        std::string text;
        int best_cons = -1;
        // NOTE on why this is NOT "first success wins", and not "fewest RS
        // corrections". A WRONG level can RS-validate with nerr=0 and still
        // parse: measured on the corpus, a v5 2-BLOCK symbol decodes
        // single-block at k=24 with nerr=0 and yields the first 123 chars of
        // the true 235-char payload, because the parse stops at a spurious
        // END marker inside EC data. Error count therefore does not
        // discriminate. Only "how much of the stream did the parse use" does,
        // since a truncated false positive necessarily consumed less than the
        // real decode -- so single and 2-block candidates compete on the same
        // scale and the fullest wins.
        int cons_single = 0;
        if (decode_gm_stream(s1, vers, text, cons_single) && !text.empty())
            best_cons = cons_single;
        // 2-block fallback (v4+; full B-head read, any D). Block 1 is
        // RS-corrected here at the DEFAULT level only -- 2-block at a
        // non-default level is a different (h, 2k) split and stays refused.
        if (T->nec2 != 0) {
            int n1 = 0;
            if (gm_rs_decode(low1, K, n1)) {
                std::vector<int> b1full(low1.rbegin(), low1.rend());
                std::vector<int> s2;
                s2.reserve(static_cast<size_t>(H + K));
                for (int c = H; c < 2 * H; ++c) {
                    if (c - 1 >= T->ndata) {
                        s2.push_back(0);
                        continue;
                    }
                    s2.push_back(read_cw(g, G, T->data[c - 1]));
                }
                for (int i = 0; i < K; ++i)
                    s2.push_back(read_cw(g, G, T->ec2[i]));
                std::vector<int> low2(s2.rbegin(), s2.rend());
                int n2 = 0;
                if (gm_rs_decode(low2, K, n2)) {
                    std::vector<int> b2(low2.rbegin(), low2.rend());
                    std::string bits;
                    int cons2 = 0;
                    bits.reserve(static_cast<size_t>(2 * H) * 7);
                    for (int i = 0; i < H; ++i) {
                        const int v = b1full[i];
                        for (int j = 6; j >= 0; --j)
                            bits.push_back(((v >> j) & 1) ? '1' : '0');
                    }
                    for (int i = 0; i < H; ++i) {
                        const int v = b2[i];
            for (int j = 6; j >= 0; --j)
                bits.push_back(((v >> j) & 1) ? '1' : '0');
        }
                    std::string t2;
                    int wok2 = gm_walk_bits(bits, t2, cons2);
                    if (wok2 && !t2.empty() && cons2 > best_cons) {
                        text = std::move(t2);
                        best_cons = cons2;
                    }
                }
            }
        }
        if (best_cons >= 0) {
            Result res;
            res.symbology = Symbology::GridMatrix;
            res.text = text;
            res.confidence = 1.0f;
            res.location =
                Quad(Point{static_cast<float>(x0), static_cast<float>(y0)},
                     Point{static_cast<float>(x1 + 1), static_cast<float>(y0)},
                     Point{static_cast<float>(x1 + 1),
                           static_cast<float>(y1 + 1)},
                     Point{static_cast<float>(x0),
                           static_cast<float>(y1 + 1)});
            out.push_back(std::move(res));
            return true;
        }
    }
    return false;
}

}  // namespace gridmatrix
}  // namespace omniscan
