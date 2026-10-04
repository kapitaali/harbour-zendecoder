// Plessey Code decoder (M3). See docs/formats/plessey.md.
// Bars quanta {1,3}, spaces {1,2,3}. Pairs: (3,1)->1, (1,2|3)->0.
#include "codecs.h"

namespace omniscan {
namespace linear {

unsigned plessey_crc8(const char* bits, int nbits) {
    unsigned reg = 0;
    for (int i = 0; i < nbits; ++i) {
        int b = (bits[i] == '1') ? 1 : 0;
        int msb = (reg >> 7) & 1;
        reg = ((reg << 1) | b) & 0xFF;
        if (msb) reg ^= 0xE9;  // poly x^8+x^7+x^6+x^5+x^3+1, minus x^8 term
    }
    return reg;
}

namespace {

// Bit value of one (bar,space) pair. 0-bit = narrow bar + wide space where
// the space may be 2 (real dimension ratio) or 3 (zint's symmetric 1:3).
// Returns -1 when invalid.
int plessey_bit(int b, int s) {
    if (b == 3 && s == 1) return 1;
    if (b == 1 && (s == 2 || s == 3)) return 0;
    return -1;
}

}  // namespace

bool parse_plessey(const std::vector<int>& runs, std::string& hex) {
    hex.clear();
    int n = static_cast<int>(runs.size());
    if (n < 8 + 8 + 16 + 9) return false;  // start + 1 digit + crc + stop
    // Start bits 1101.
    static const int kStart[4] = {1, 1, 0, 1};
    for (int k = 0; k < 4; ++k) {
        int bit = plessey_bit(runs[2 * k], runs[2 * k + 1]);
        if (bit != kStart[k]) return false;
    }
    // Try data lengths: bits after start are hex digits (LSB-first per
    // digit); the 8 bits following the data are the CRC; then the 9-run stop.
    int max_pairs = (n - 8) / 2;  // pairs available after start
    for (int ndigits = 1; ndigits + 2 <= max_pairs / 4; ++ndigits) {
        int data_pairs = 4 * ndigits;
        int crc_pos = 8 + 2 * data_pairs;  // run index of CRC start
        if (crc_pos + 16 + 9 > n) break;
        // Collect data bits (print order).
        std::string dbits;
        dbits.reserve(4 * ndigits);
        bool ok = true;
        for (int k = 0; k < data_pairs && ok; ++k) {
            int bit = plessey_bit(runs[8 + 2 * k], runs[8 + 2 * k + 1]);
            if (bit < 0)
                ok = false;
            else
                dbits.push_back(bit ? '1' : '0');
        }
        if (!ok) break;  // longer k only adds pairs; first bad pair sticks
        // CRC bits.
        std::string cbits;
        for (int k = 0; k < 8; ++k) {
            int bit = plessey_bit(runs[crc_pos + 2 * k],
                                  runs[crc_pos + 2 * k + 1]);
            if (bit < 0) {
                ok = false;
                break;
            }
            cbits.push_back(bit ? '1' : '0');
        }
        if (!ok) break;
        // CRC bits in print order: low nibble LSB-first, then high nibble
        // LSB-first (matches the encoder in tests/encoders/linear_encoders.h).
        unsigned reconstructed = 0;
        for (int k = 0; k < 4; ++k)
            if (cbits[k] == '1') reconstructed |= (1u << k);
        for (int k = 0; k < 4; ++k)
            if (cbits[4 + k] == '1') reconstructed |= (1u << (k + 4));
        if (reconstructed !=
            plessey_crc8(dbits.data(), static_cast<int>(dbits.size())))
            continue;
        // Stop: literal 9-run pattern (termination + reverse-start, widths
        // per zint backend/plessey.c), then exact end of ink.
        static const int kStop[9] = {3, 3, 1, 3, 1, 1, 3, 1, 3};
        int stop_idx = crc_pos + 16;
        if (stop_idx + 9 != n) continue;
        bool stop_ok = true;
        for (int k = 0; k < 9 && stop_ok; ++k)
            if (runs[stop_idx + k] != kStop[k]) stop_ok = false;
        if (!stop_ok) continue;
        // Emit hex (nibbles LSB-first in dbits).
        try {
            for (int d = 0; d < ndigits; ++d) {
                int v = 0;
                for (int k = 0; k < 4; ++k)
                    if (dbits[4 * d + k] == '1') v |= (1 << k);
                hex.push_back("0123456789ABCDEF"[v]);
            }
        } catch (...) {
            return false;
        }
        return true;
    }
    return false;
}

}  // namespace linear
}  // namespace omniscan
