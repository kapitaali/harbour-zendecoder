#pragma once
// MaxiCode value-level codec (internal): codewords <-> text via the
// derived code sets, state machine, NS numeric packing and ECI.
//
// Derived black-box from zint 2.16.0.9 `--verbose` dumps; see
// docs/formats/maxicode.md for the per-claim evidence and sample counts
// (nothing here reproduces a paid spec).  Deterministic, no I/O; callers
// own try/catch per project discipline.
#include <cstdint>
#include <string>
#include <vector>
#include "omniscan/export.h"

namespace omniscan {
namespace maxicode {

// Decode the data codewords cws[start..end) to bytes/text.
//   state machine: 59/60/61/62 = one-shot shift to B/C/D/E; 63 = A<->B
//   latch; 60,60 / 61,61 / 62,62 = latch-C/D/E; C/D runs exit via 58;
//   33 = pad (A/B/C/D), 28 = pad (E); 27 = ECI lead; 31 = NS.
// Returns false on an unknown codeword in the active set.
// `mode` selects pad semantics (E-state pads with 28).
OMNISCAN_API bool decode_data(const std::vector<int>& cws, int start,
                              int end, std::string& text);

// Encode bytes to codewords (test-only use; not an oracle-faithful
// optimizer — it emits valid codewords the decoder reads back, which is
// all a round-trip encoder must do, per the Codablock F precedent).
OMNISCAN_API bool encode_data(const std::string& text, std::vector<int>& cws);

// NS numeric packing: 9 decimal digits -> codeword 31 + 5 codewords
// (30 bits). Exposed for tests.
OMNISCAN_API bool ns_pack(const std::string& digits9,
                          std::vector<int>& out5);
OMNISCAN_API bool ns_unpack(const std::vector<int>& in5, std::string& digits9);

// ECI value -> codewords (after the lead 27). Empty for value 0.
OMNISCAN_API std::vector<int> eci_encode(int value);
// Decode an ECI value from the codewords after the 27 lead; returns the
// number of codewords consumed (0 on error).
OMNISCAN_API int eci_decode(const std::vector<int>& cws, int start,
                            int& value);

}  // namespace maxicode
}  // namespace omniscan
