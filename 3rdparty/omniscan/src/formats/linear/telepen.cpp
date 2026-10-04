// Telepen decoder (M3, ASCII mode). See docs/formats/telepen.md.
// Pairs (bar,space) quanta {1,3}: (1,1)->"1", (3,1)->"00", (3,3)->"010",
// (1,3)->block boundary (opens "01" / closes "10").
#include "codecs.h"

namespace omniscan {
namespace linear {

bool telepen_even_parity(unsigned byte) {
    unsigned v = byte & 0xFF;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1) == 0;
}

int telepen_check_value(const std::string& data) {
    int sum = 0;  // AIM: start '_' excluded; raw 7-bit data values only
    for (unsigned char c : data) {
        if (c > 127) return -1;
        sum += c;
    }
    return (127 - (sum % 127)) % 127;
}

bool parse_telepen_even(const std::vector<int>& runs, std::string& text);

bool parse_telepen(const std::vector<int>& runs, std::string& text) {
    text.clear();
    // The symbol's trailing space merges with the trailing quiet zone, so
    // extraction drops it: an odd run count means the final pair lacks its
    // space. Complete it with 1, else 3 (validations below disambiguate).
    std::vector<int> full = runs;
    if ((full.size() % 2) == 1) full.push_back(0);  // placeholder
    for (int fill = 1; fill <= 3; fill += 2) {
        if ((runs.size() % 2) == 1) full.back() = fill;
        if (parse_telepen_even(full, text)) return true;
        if ((runs.size() % 2) == 0) break;
    }
    text.clear();
    return false;
}

bool parse_telepen_even(const std::vector<int>& runs, std::string& text) {
    text.clear();
    int n = static_cast<int>(runs.size());
    if (n < 2 || (n % 2) != 0) return false;
    // Reassemble the bit stream.
    std::string bits;
    bits.reserve(n);
    bool in_block = false;
    for (int i = 0; i < n; i += 2) {
        int b = runs[i], s = runs[i + 1];
        if (b == 1 && s == 1) {
            bits.push_back('1');
        } else if (b == 3 && s == 1) {
            bits += "00";
        } else if (b == 3 && s == 3) {
            bits += "010";
        } else if (b == 1 && s == 3) {
            bits += in_block ? "10" : "01";
            in_block = !in_block;
        } else {
            return false;
        }
    }
    if (in_block || bits.size() < 24 || bits.size() % 8 != 0) return false;
    // Bytes LSB-first.
    std::string bytes;
    bytes.reserve(bits.size() / 8);
    for (size_t i = 0; i < bits.size(); i += 8) {
        unsigned v = 0;
        for (int k = 0; k < 8; ++k)
            if (bits[i + k] == '1') v |= (1u << k);
        if (!telepen_even_parity(v)) return false;
        bytes.push_back(static_cast<char>(v));
    }
    // Frame: '_' + data(>=1) + check + 0xFA.
    if (bytes.size() < 3) return false;
    if (static_cast<unsigned char>(bytes.front()) != 0x5F) return false;
    if (static_cast<unsigned char>(bytes.back()) != 0xFA) return false;
    std::string data = bytes.substr(1, bytes.size() - 3);
    if (data.empty()) return false;
    // NB: data bytes legitimately carry the even-parity MSB; parity was
    // verified above, payload values come from bit 0..6 (see data7 below).
    unsigned char check = static_cast<unsigned char>(bytes[bytes.size() - 2]);
    // 7-bit payload values for the checksum.
    std::string data7;
    for (unsigned char c : data) data7.push_back(static_cast<char>(c & 0x7F));
    int want = telepen_check_value(data7);
    if (want < 0 || (check & 0x7F) != want) return false;
    text = data7;
    return true;
}

}  // namespace linear
}  // namespace omniscan
