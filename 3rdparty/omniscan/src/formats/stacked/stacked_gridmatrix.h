#pragma once
// Grid Matrix native value layer (Tier-2): RS codec + modes walker +
// codeword-stream decode. Contract: docs/formats/grid_matrix.md steps
// 4-6/10 (RS codec fitted black-box, modes grammar + N-separator
// correction, values), steps 12/15 (K table, H-rule). Landed
// un-dispatched (Code49 precedent): no behavior change until the image
// pipeline slice (step 19), which adds tables/positions/dispatch. The
// value path is exposed for tests and the fuzz target (OMNISCAN_API:
// shared build uses -fvisibility=hidden). Supports ALL EC levels at
// value level (K table 13x5 exact + H-rule +-2 search gated by RS);
// image level (step 19) scopes to default levels (EC positions
// default-only). U/L punct sfx 45..50 untwin to [/]^_` (zint twins);
// M punct is literal (M values lack punct, so M sfx are legitimate).
// False on anything invalid; never throws.
#include <cstdint>
#include <string>
#include <vector>

#include "omniscan/binarizer.h"
#include "omniscan/export.h"
#include "omniscan/result.h"
#include <vector>

namespace omniscan {
namespace gridmatrix {

// Per-level EC parameters (N(v), K(v,level), default level) live in
// gridmatrix_levels.h. The old kGMK[14][6] and kGMFrac[6] tables that used
// to be here are GONE, deliberately:
//
//   - kGMFrac drove an EC-fraction estimate K(1-f)/f with a +/-5/6 search
//     window over h, because h could not be pinned. It is superseded by the
//     exact rule h(v,level) = N(v) - K(v,level).
//   - kGMK's v1 row was {0,7,7,7,7,9}: it mapped E1-E3 onto E4's k, i.e. it
//     asserted three levels that do not exist at v1. A dead table that is
//     also wrong is a standing hazard, so it was deleted rather than left
//     for someone to reuse.

// RS decode (attic/gm_rs.py rs_decode: syndromes -> Berlekamp-Massey
// -> Chien -> Forney; b=1 so magnitude = Omega(X^-1)/Lambda'(X^-1)).
// Corrects up to floor(k/2) errors in low-first cw (in/out). Returns
// false (untouched) when syndromes are dirty beyond repair;
// 0-error input returns unchanged (0 false corrections, step 10).
OMNISCAN_API bool gm_rs_decode(std::vector<int>& cw, int k, int& nerr);

// Modes walker (attic/gm_walk.py walk, steps 5-6 + N-separator fix):
// L1 bitstring -> payload text (modes U/L/M/N/B, N separators
// space/+/-/./, by k buffer offset; punct sfx45..50 LITERAL -- sfx are
// overloaded (twins share values with ./:;<=), so twins decode
// twin-mapped (documented encoder ambiguity)). Sets consumed = bit
// position of the end marker on success (0 on failure); callers
// prefer the longest parse across tries (correct = fullest; wrong-split
// miscorrections truncate early -- step-18 fix).
OMNISCAN_API bool gm_walk_bits(const std::string& bits, std::string& text,
                               int& consumed);
// False on any refusal path (value 28, escape-peek2 11, M cmds
// 0001/0101, B->B, N 1019/slot-5, bad lengths, EOF); never throws.
OMNISCAN_API bool gm_walk_bits(const std::string& bits, std::string& text);

// Value level: ordered codeword stream (high-first data+EC, single
// H+K or 2-block (H+K)+(H'+K) with H'<=H) + version 1..13 -> payload.
// Tries EC levels (K table) x H-rule +-2 x single/2-block, gated by RS
// (syndromes zero) + walk success (RS guarantees integrity; pads/end
// tail need not be consumed, step-17 fix). False on anything invalid
// (incl. D>2H large-D and non-default... no, all levels tried here;
// image level scopes separately); never throws.
OMNISCAN_API bool decode_gm_stream(const std::vector<int>& cws, int version,
                                   std::string& text);
// As above, also reporting how many bits of the data stream the winning parse
// consumed. Callers that can offer an ALTERNATIVE layout for the same cells
// (the image path can offer a 2-block split) must compare this: a wrong
// layout can RS-validate with nerr=0 and still parse, so error count alone
// does not discriminate. The longest consumed parse is the one that used the
// stream, and a truncated false positive necessarily consumed less.
OMNISCAN_API bool decode_gm_stream(const std::vector<int>& cws, int version,
                                   std::string& text, int& consumed);

// Image level: one binarized pass (localize -> version detect ->
// sample modules -> 0/180 orientation retry -> table-driven codeword
// read (default EC levels only; others safely refused) -> RS correct
// (single first, then full-stream 2-block) -> walk -> Result). True +
// exactly one Result appended on success; false (nothing appended)
// otherwise. Step-19 image slice; value level (above) landed step 18.
bool try_decode_gridmatrix(const BinaryImage& bin,
                           std::vector<Result>& out);

// Pipeline stages exposed for tests (internal, no API macro).
// Localize dark-pixel bbox (false if empty/tiny/non-square).
bool gm_localize(const BinaryImage& bin, int& x0, int& y0, int& x1, int& y1);
// Version detect via frame checkerboard (1..13 or 0).
int gm_detect_version(const BinaryImage& bin, int x0, int y0, int x1, int y1);
// Sample module grid (6M x 6M, 1=dark) at integer scale s (false if bad).
bool gm_sample_grid(const BinaryImage& bin, int x0, int y0, int m, int s,
                    std::vector<uint8_t>& grid);

}  // namespace gridmatrix
}  // namespace omniscan
