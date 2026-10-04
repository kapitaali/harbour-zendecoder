#pragma once
// USPS Intelligent Mail decoder support (M4b). Character tables live in
// imb_tables.h (machine-extracted from zint, provenance header there).
// Independent implementation of USPS-B-3200 decode per zint's structure.
#include <string>
#include <vector>
#include "fourstate.h"

namespace omniscan {
namespace postal {

// Full decode of 65 4-state bars. Text = 20-digit tracking, plus
// "-" + routing when present (0/5/9/11 digits). False otherwise.
bool parse_imb(const std::vector<Bar4>& bars, std::string& text);

}  // namespace postal
}  // namespace omniscan
