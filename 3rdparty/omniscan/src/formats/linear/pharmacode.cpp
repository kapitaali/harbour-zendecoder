// Pharmacode one-track decoder (M3). See docs/formats/pharmacode.md.
// Bars right-to-left: narrow adds 2^n, wide adds 2*2^n. Space widths supply
// the module reference (uniform separators); bar ratio <1.5 narrow,
// 1.5..2.6 wide.
#include "codecs.h"
#include <cstdio>

namespace omniscan {
namespace linear {

bool parse_pharmacode(const std::vector<int>& bar_runs, double space_mod,
                      std::string& decimal) {
    decimal.clear();
    if (bar_runs.size() < 2 || bar_runs.size() > 16) return false;
    if (!(space_mod > 0)) return false;
    long value = 0;
    // bar_runs are left-to-right; position n=0 is the rightmost bar.
    int nbars = static_cast<int>(bar_runs.size());
    for (int i = 0; i < nbars; ++i) {
        double r = bar_runs[nbars - 1 - i] / space_mod;  // i=0 rightmost
        int d;
        if (r >= 0.65 && r < 1.5)
            d = 1;
        else if (r >= 1.5 && r <= 2.6)
            d = 2;
        else
            return false;
        value += static_cast<long>(d) * (1L << i);
        if (value > 131070) return false;
    }
    if (value < 3) return false;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%ld", value);
    try {
        decimal = buf;
    } catch (...) {
        return false;
    }
    return true;
}

// Two-track: bijective base 3, the exact analogue of one-track's
// bijective base 2 above (digits {1,2} over powers of 2, range
// 3..131070 = 2*(2^16-1)). Here digits {1,2,3} over powers of 3,
// range 4..64570080 = 3*(3^16-1)/2. Derivation from the oracle and the
// validation sweep live in docs/tier3-feasibility.md section 5.3; the
// per-format page (docs/formats/pharmacode.md) states the rule.
bool parse_pharma2(const std::vector<int>& digits, std::string& decimal) {
    decimal.clear();
    if (digits.size() < 2 || digits.size() > 16) return false;
    long value = 0;
    // digits are left-to-right, MSD first: value = value*3 + d.
    // Prefix values grow monotonically (digits >= 1), so the range cap
    // can be enforced incrementally with no overflow risk (max ~64M).
    int nbars = static_cast<int>(digits.size());
    for (int j = 0; j < nbars; ++j) {
        int d = digits[j];
        if (d < 1 || d > 3) return false;
        value = value * 3L + d;
        if (value > 64570080L) return false;
    }
    if (value < 4) return false;  // >= 2 bars implies >= 1+3; explicit
    char buf[16];
    std::snprintf(buf, sizeof buf, "%ld", value);
    try {
        decimal = buf;
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace linear
}  // namespace omniscan
