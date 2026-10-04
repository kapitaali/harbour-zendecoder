// ITF (Interleaved 2 of 5) structure parser (M4a). Pixel runs alternate
// bar,space,... starting+ending with a bar. Narrow reference comes from the
// 4-run start pattern (self-calibrating); wide = 1.6..3.4x narrow.
// Digit patterns: standard ITF table (cross-checked against zxing-cpp).
#include "codecs.h"

namespace omniscan {
namespace linear {
namespace {

// 5 elements, exactly 2 wide. Index = digit.
const int kITF[10][5] = {
    {1, 1, 2, 2, 1}, {2, 1, 1, 1, 2}, {1, 2, 1, 1, 2}, {2, 2, 1, 1, 1},
    {1, 1, 2, 1, 2}, {2, 1, 2, 1, 1}, {1, 2, 2, 1, 1}, {1, 1, 1, 2, 2},
    {2, 1, 1, 2, 1}, {1, 2, 1, 2, 1},
};

int itf_digit(const int* e /*classified 1=narrow 2=wide, len 5*/) {
    int wide = 0;
    for (int k = 0; k < 5; ++k) {
        if (e[k] != 1 && e[k] != 2) return -1;
        if (e[k] == 2) ++wide;
    }
    if (wide != 2) return -1;
    for (int d = 0; d < 10; ++d) {
        bool match = true;
        for (int k = 0; k < 5 && match; ++k)
            if (e[k] != kITF[d][k]) match = false;
        if (match) return d;
    }
    return -1;
}

}  // namespace

bool parse_itf(const std::vector<int>& runs, std::string& digits) {
    digits.clear();
    int n = static_cast<int>(runs.size());
    // start(4) + k digit-pairs (10 runs each) + stop(3); 2..16 digits.
    if (n < 4 + 10 + 3 || ((n - 7) % 10) != 0) return false;
    int groups = (n - 7) / 10;
    if (groups < 1 || groups > 8) return false;
    // Narrow reference from the start pattern (4 narrow elements).
    double ref = 0;
    for (int k = 0; k < 4; ++k) ref += runs[k];
    ref /= 4.0;
    if (!(ref > 0)) return false;
    for (int k = 0; k < 4; ++k) {
        double r = runs[k] / ref;
        if (r < 0.75 || r > 1.35) return false;
    }
    auto cls = [&](int w) -> int {
        double r = w / ref;
        if (r >= 0.6 && r < 1.6) return 1;
        if (r >= 1.6 && r <= 3.4) return 2;
        return -1;
    };
    try {
        for (int g = 0; g < groups; ++g) {
            int eb[5], es[5];
            for (int k = 0; k < 5; ++k) {
                eb[k] = cls(runs[4 + 10 * g + 2 * k]);
                es[k] = cls(runs[4 + 10 * g + 2 * k + 1]);
                if (eb[k] < 0 || es[k] < 0) return false;
            }
            int d1 = itf_digit(eb), d2 = itf_digit(es);
            if (d1 < 0 || d2 < 0) return false;
            digits.push_back(static_cast<char>('0' + d1));
            digits.push_back(static_cast<char>('0' + d2));
        }
    } catch (...) {
        return false;
    }
    // Stop: wide bar, narrow space, narrow bar, then exact end.
    int s0 = cls(runs[n - 3]), s1 = cls(runs[n - 2]), s2 = cls(runs[n - 1]);
    if (s0 != 2 || s1 != 1 || s2 != 1) return false;
    return true;
}

}  // namespace linear
}  // namespace omniscan
