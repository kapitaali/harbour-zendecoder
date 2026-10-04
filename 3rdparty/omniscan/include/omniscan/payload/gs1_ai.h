#pragma once
// GS1 Application Identifier element strings (M7). Refs: GS1 General
// Specifications (AI table: fixed/variable lengths). Supports the
// parenthesized human-readable form "(01)GTIN(10)BATCH..." and the raw
// scanner form with FNC1 (0x1D) separators. Pure, deterministic.
#include <map>
#include <string>
#include "omniscan/export.h"

namespace omniscan {
namespace payload {

// Parse into out with keys "payload"="gs1" and "gs1.<AI>" per element.
// Unknown AIs split fine in "(..)" form; raw form stops at unknown AIs.
// Returns false (out unchanged) unless at least one element parses.
OMNISCAN_API bool parse_gs1(const std::string& text, std::map<std::string, std::string>& out);

// GTIN-14 Mod-10 check (weights 3/1 from the right). False on bad input.
OMNISCAN_API bool validate_gtin14(const std::string& gtin14);

}  // namespace payload
}  // namespace omniscan
