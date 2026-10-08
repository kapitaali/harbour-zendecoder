#pragma once
// Native linear Tier-2 codecs (M3). Pure module-level parsers: input is a
// quantized run-width vector alternating bar,space,bar,... starting and
// ending with a bar. Deterministic, no I/O, suitable for libFuzzer.
#include <string>
#include <vector>
#include "omniscan/export.h"
#include "omniscan/options.h"

namespace omniscan {
namespace linear {

// MSI: runs use quanta {1,2}. Emits data digits (checks stripped).
// `only` restricts accepted check schemes (Auto = documented preference).
bool parse_msi(const std::vector<int>& runs, std::string& digits,
               std::string& check_scheme, MsiCheck only = MsiCheck::Auto);

// Plessey: bar quanta {1,3}, space quanta {1,2}. Emits uppercase hex.
bool parse_plessey(const std::vector<int>& runs, std::string& hex);

// Telepen: pair quanta {1,3}. Emits ASCII payload (start/stop/check removed).
bool parse_telepen(const std::vector<int>& runs, std::string& text);

// Pharmacode one-track: bar_runs widths in px + uniform space module.
// Emits decimal value. Pure w.r.t. its inputs.
bool parse_pharmacode(const std::vector<int>& bar_runs, double space_mod,
                      std::string& decimal);

// Pharmacode two-track: bar HEIGHT digits left-to-right, each 1
// (bottom-half), 2 (top-half) or 3 (full-height). Value is bijective
// base 3, MSD leftmost: sum d_i * 3^i right-to-left. 2..16 bars,
// range 4..64570080. Pure w.r.t. its inputs (height classification from
// pixel extents happens in native_linear, not here).
bool parse_pharma2(const std::vector<int>& digits, std::string& decimal);

// Luhn / Mod-11 primitives shared by encoder (tests) and decoder.
// Mod-11 wraps: 7 = IBM (common), 9 = NCR (per zint backend/plessey.c).
// A check value of 10 is unencodable as a single digit; zint emits "10"
// (two chars) — the decoder accepts that expansion.
int msi_luhn_check(const std::string& data);           // -1 on bad input
bool msi_luhn_valid(const std::string& full);
int msi_mod11_check(const std::string& data, int wrap = 7);  // -1/10 special
bool msi_mod11_valid(const std::string& full, int wrap = 7);

// Plessey CRC-8 (poly 0xE9, init 0, MSB-first, no reflection/xorout).
// Data bits in print order (LSB-first per nibble, nibbles in order).
unsigned plessey_crc8(const char* bits, int nbits);

// Telepen helpers. Check rule per AIM Europe USS Telepen (1991) via zint:
// check = (127 - (sum of data byte values) % 127) % 127 (start excluded).
// Exported: tests link the installed/shared library and cross-check the
// checksum model through this entry point.
OMNISCAN_API int telepen_check_value(const std::string& data);  // -1 = bad
bool telepen_even_parity(unsigned byte);

// ITF (Interleaved 2 of 5) structure parser on PIXEL runs (ratio-based
// narrow/wide split from the start pattern; ratio 1:2..1:3 per DP specs).
// Emits even-length digit strings (2..16 digits). Deutsche Post lengths
// (12/14) are claimed by the postal dispatcher, not here.
bool parse_itf(const std::vector<int>& pixel_runs, std::string& digits);

// Code 128 ROW reader (stacking infrastructure — NOT a standalone
// decoder; Symbology::Code128 stays backend-routed and this parser is
// never registered in try_fn/order). Input is a QUANTIZED run vector
// alternating bar,space,... starting and ending with a bar (quantize
// with quantize_runs(q, 4, 0.30)); matching is exact on quantized
// modules, damage misquantizes into silence. Emits the symbol VALUE
// sequence [start, data...] with the mod-103 check verified and
// stripped and the stop consumed. Tries forward, then reversed runs
// (reverse-stop path), always reporting print order. Values 0..102
// data, 103/104/105 Start A/B/C (subset interpretation is the caller's;
// see docs/formats/code128_row.md). Exported for test linkage (shared
// builds hide everything else); not part of the supported API surface.
OMNISCAN_API bool parse_code128_row(const std::vector<int>& q,
                                    std::vector<int>& values);

}  // namespace linear
}  // namespace omniscan
