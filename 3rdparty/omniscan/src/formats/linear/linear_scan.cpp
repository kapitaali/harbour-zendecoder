// Shared 1D scanline helpers. See linear_scan.h.
#include "linear_scan.h"
#include <algorithm>
#include <cmath>

namespace omniscan {
namespace linear {

bool extract_runs(const BinaryImage& bin, int fixed, int from, int to,
                  bool horizontal, int min_bars, std::vector<int>& out_runs,
                  int& rx0, int& rx1) {
    out_runs.clear();
    rx0 = rx1 = 0;
    if (bin.empty() || bin.width <= 0 || bin.height <= 0) return false;
    int line_len = horizontal ? bin.width : bin.height;
    int ortho = horizontal ? bin.height : bin.width;
    if (fixed < 0 || fixed >= ortho) return false;
    if (from < 0) from = 0;
    if (to > line_len) to = line_len;
    if (to - from < 8) return false;

    auto pixel = [&](int i) -> uint8_t {
        return horizontal ? bin.bits[fixed * bin.stride + i]
                          : bin.bits[i * bin.stride + fixed];
    };
    // Skip leading quiet (light=1), start at first bar (ink=0).
    int i = from;
    while (i < to && pixel(i) != 0) ++i;
    if (i >= to) return false;
    int start = i;
    int bars = 1;  // i already points at the first bar run
    int cur = 0;   // current run width; in_bar alternates starting true
    bool in_bar = true;
    std::vector<int> runs;
    for (; i < to; ++i) {
        bool is_bar = (pixel(i) == 0);
        if (is_bar == in_bar) {
            ++cur;
        } else {
            runs.push_back(cur);
            cur = 1;
            in_bar = is_bar;
            if (is_bar) ++bars;
        }
    }
    runs.push_back(cur);  // last run
    // Trim trailing quiet: runs must end with a bar.
    if (!in_bar) runs.pop_back();
    if (bars < min_bars || runs.size() < 3) return false;
    // rx extent: from first bar start to end of last bar.
    int end = to - 1;
    while (end > start && pixel(end) != 0) --end;
    rx0 = start;
    rx1 = end + 1;
    out_runs = std::move(runs);
    return true;
}

bool quantize_runs(const std::vector<int>& runs, int max_q, double tol,
                   std::vector<int>& q, double& mod) {
    q.clear();
    mod = 0;
    if (runs.empty() || max_q < 1) return false;
    int m = *std::min_element(runs.begin(), runs.end());
    if (m <= 0) return false;
    mod = static_cast<double>(m);
    try {
        q.reserve(runs.size());
        for (int w : runs) {
            double v = w / mod;
            int qi = static_cast<int>(std::lround(v));
            if (qi < 1 || qi > max_q) return false;
            if (std::fabs(v - qi) > tol) return false;
            q.push_back(qi);
        }
    } catch (...) {
        return false;
    }
    return true;
}

void plan_scans(const ImageView& img, bool try_harder,
                std::vector<ScanLine>& out) {
    out.clear();
    if (!img.valid()) return;
    try {
        out.push_back({true, img.height / 2});
        if (try_harder) {
            // Eighths (rows and columns) cover off-center codes in real photos.
            for (int k = 1; k <= 7; ++k) {
                int y = (img.height * k) / 8;
                bool dup = (y == img.height / 2);
                for (const ScanLine& s : out)
                    if (s.horizontal && s.fixed == y) dup = true;
                if (!dup) out.push_back({true, y});
            }
            for (int k = 1; k <= 7; ++k) {
                int x = (img.width * k) / 8;
                bool dup = false;
                for (const ScanLine& s : out)
                    if (!s.horizontal && s.fixed == x) dup = true;
                if (!dup) out.push_back({false, x});
            }
            return;
        }
        // Default: pair the centre row with the centre column, which is
        // where a symbol rotated 90 degrees lives (a horizontal line runs
        // along such a symbol's bars and reads one long run, so no row can
        // ever decode it). native_linear stops scanning as soon as a line
        // produces a hit, so the column runs ONLY when the row found
        // nothing -- a retry on empty, not an extra source of symbols for
        // images that already decode. Positive results are unchanged.
        out.push_back({false, img.width / 2});
    } catch (...) {
        out.clear();
    }
}

void vertical_extent(const BinaryImage& bin, int x0, int x1, int y, int& top,
                     int& bot) {
    top = bot = y;
    if (bin.empty()) return;
    if (x0 < 0) x0 = 0;
    if (x1 > bin.width) x1 = bin.width;
    if (x0 >= x1) return;
    auto has_ink = [&](int yy) {
        if (yy < 0 || yy >= bin.height) return false;
        const uint8_t* row = bin.bits.data() + yy * bin.stride;
        for (int x = x0; x < x1; ++x)
            if (row[x] == 0) return true;
        return false;
    };
    top = y;
    while (top - 1 >= 0 && has_ink(top - 1)) --top;
    bot = y;
    while (bot + 1 < bin.height && has_ink(bot + 1)) ++bot;
}

void horizontal_extent(const BinaryImage& bin, int y0, int y1, int x, int& l,
                       int& r) {
    l = r = x;
    if (bin.empty()) return;
    if (y0 < 0) y0 = 0;
    if (y1 > bin.height) y1 = bin.height;
    if (y0 >= y1) return;
    auto has_ink = [&](int xx) {
        if (xx < 0 || xx >= bin.width) return false;
        for (int yy = y0; yy < y1; ++yy)
            if (bin.bits[yy * bin.stride + xx] == 0) return true;
        return false;
    };
    l = x;
    while (l - 1 >= 0 && has_ink(l - 1)) --l;
    r = x;
    while (r + 1 < bin.width && has_ink(r + 1)) ++r;
}

}  // namespace linear
}  // namespace omniscan
