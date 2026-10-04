#pragma once
// Australia Post 4-state codec support (M4b). FCCs, N/C tables, filler rule
// and RS parameters cross-checked against zint backend/auspost.c (BSD-3,
// cited not copied; itself citing the AusPost Aug-2012 tech spec).
// GF(64)/0x43 arithmetic verified entry-for-entry against zint's published
// logt_0x43/alog_0x43 tables (see PROGRESS.md). Independent implementation.
#include <cstdint>
#include <string>
#include <vector>
#include "fourstate.h"

namespace omniscan {
namespace postal {

// 6-bit RS ECC over triples (GF(64) poly 0x43, 4 symbols, first root 1).
void auspost_rs_ecc(const uint8_t* triples, int ntriples, uint8_t ecc[4]);

// Full decode. Text = DPID + customer (FCC excluded; reported separately).
// False unless frame, FCC/length, RS and customer split all validate.
bool parse_auspost(const std::vector<Bar4>& bars, std::string& text,
                   std::string& fcc, std::string& kind);

}  // namespace postal
}  // namespace omniscan
