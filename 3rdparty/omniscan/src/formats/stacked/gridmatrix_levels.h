// Grid Matrix per-level EC parameters.
// SPDX-License-Identifier: Apache-2.0
//
// N(v)    = total data+EC codewords at version v.
// K(v,level) = EC codewords at (version, level).
//
// EXACT H rule (docs/formats/grid_matrix.md step 21b, proven on all 49
// (v,level) cells with 0 mismatches, then re-verified on 60 oracle fixtures
// with 0 mismatches):
//
//     h(v, level) = N(v) - K(v, level)
//
// This replaces the earlier EC-fraction approximation (kGMFrac + a +/-5/6
// search window over h). The approximation needed an 11-value h search per
// level and could not be pinned; the per-level K table below is the entire
// difference. There is no window and no search.
//
// v1 supports ONLY levels 4 and 5: E1-E3 FLOOR to E4. A k of 0 marks a level
// that is not available at that version.
//
// Generated from attic/gm_levels3.py (oracle-fitted, steps 18-21b).
#pragma once

namespace omniscan {
namespace gridmatrix {

// kGMN[v] -> total codewords (data + EC) at version v.
inline constexpr int kGMN[14] = {0, 18, 50, 98, 81, 121, 113, 113,
                                 116, 121, 126, 118, 125, 122};

// kGMKLevel[v][level] -> EC codewords. 0 => level unavailable at v.
inline constexpr int kGMKLevel[14][6] = {
    {0, 0, 0, 0, 0, 0},        // index 0 (versions are 1..13)
    {0, 0, 0, 0, 7, 9},        // v1   h: -,-,-,11,9   (E1-E3 floor to E4)
    {0, 10, 10, 15, 20, 25},   // v2   h: 40,40,35,30,25
    {0, 9, 19, 29, 39, 49},    // v3   h: 89,79,69,59,49
    {0, 8, 16, 24, 32, 41},    // v4   h: 73,65,57,49,40
    {0, 12, 24, 36, 48, 61},   // v5   h: 109,97,85,73,60
    {0, 11, 23, 34, 45, 57},   // v6   h: 102,90,79,68,56
    {0, 12, 23, 34, 45, 57},   // v7   h: 101,90,79,68,56
    {0, 12, 23, 35, 47, 58},   // v8   h: 104,93,81,69,58
    {0, 12, 24, 36, 48, 61},   // v9   h: 109,97,85,73,60
    {0, 13, 26, 38, 51, 63},   // v10  h: 113,100,88,75,63
    {0, 12, 24, 36, 47, 59},   // v11  h: 106,94,82,71,59
    {0, 13, 25, 38, 50, 63},   // v12  h: 112,100,87,75,62
    {0, 13, 25, 37, 49, 61},   // v13  h: 109,97,85,73,61
};

// kGMDefSec[v] -> the DEFAULT EC level for v (what the oracle picks with no
// explicit --secure).
inline constexpr int kGMDefSec[14] = {0, 5, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};

// Total codewords at (version, level), or 0 if the level is unavailable.
inline int gm_total_codewords(int v, int level) noexcept {
    if (v < 1 || v > 13 || level < 1 || level > 5) return 0;
    const int k = kGMKLevel[v][level];
    return k ? kGMN[v] : 0;
}

// Data codewords h(v, level) = N(v) - K(v, level), or 0 if unavailable.
inline int gm_data_codewords(int v, int level) noexcept {
    if (v < 1 || v > 13 || level < 1 || level > 5) return 0;
    const int k = kGMKLevel[v][level];
    return k ? kGMN[v] - k : 0;
}

// EC codewords K(v, level), or 0 if unavailable.
inline int gm_ec_codewords(int v, int level) noexcept {
    if (v < 1 || v > 13 || level < 1 || level > 5) return 0;
    return kGMKLevel[v][level];
}

}  // namespace gridmatrix
}  // namespace omniscan
