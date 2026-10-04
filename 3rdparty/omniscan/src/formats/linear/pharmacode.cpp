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

}  // namespace linear
}  // namespace omniscan
