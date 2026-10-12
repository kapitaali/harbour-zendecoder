#pragma once
/**
@file
Swiss QR bill payload schema (SIX Payment Standards) over QR (M5).
    Validates the SPC/0200 bill structure, IBAN, amount/currency and
    QRR/SCOR/NON references. Keys "payload"="swissqr" plus "swissqr.*".
    Pure, deterministic.
*/
#include <map>
#include <string>
#include "omniscan/export.h"

namespace omniscan {
namespace payload {

/// Parse a Swiss QR (SPC) bill payload. @return true when the text is Swiss QR (parsed merged).
OMNISCAN_API bool parse_swissqr(const std::string& text, std::map<std::string, std::string>& out);
/// IBAN mod-97 check (ISO 13616). False on bad input.
OMNISCAN_API bool validate_iban(const std::string& iban);

}  // namespace payload
}  // namespace omniscan
