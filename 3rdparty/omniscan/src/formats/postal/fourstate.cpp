// Shared 4-state postal helpers: bar segmentation by uniform pitch,
// band detection, per-bar state classification, 180-degree rotation.
#include "fourstate.h"
#include <algorithm>

namespace omniscan {
namespace postal {

bool extract_postal(const BinaryImage& bin, int y, int min_bars, double tol,
                    std::vector<PBar>& bars, double& qleft, double& qright) {
    bars.clear();
    qleft = qright = 0;
    if (bin.empty() || y < 0 || y >= bin.height || min_bars < 1) return false;
    const uint8_t* row = bin.bits.data() + y * bin.stride;
    const int W = bin.width;
    // Collect ink runs (bars) and the gaps between them.
    std::vector<PBar> all;
    std::vector<int> gaps;
    int x = 0;
    while (x < W && row[x] != 0) ++x;  // leading quiet
    while (x < W) {
        int x0 = x;
        while (x < W && row[x] == 0) ++x;
        int g0 = x;  // end of ink
        while (x < W && row[x] != 0) ++x;
        if (x >= W) {
            all.push_back({x0, g0});  // trailing quiet trimmed
            break;
        }
        all.push_back({x0, g0});
        gaps.push_back(x - g0);
    }
    if (static_cast<int>(all.size()) < min_bars) return false;
    // Uniformity: every bar and gap within tol of the median.
    auto median_of = [](std::vector<int> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    std::vector<int> bw, gw;
    try {
        for (auto& b : all) bw.push_back(b.x1 - b.x0);
    } catch (...) {
        return false;
    }
    int bmed = median_of(bw);
    if (bmed <= 0) return false;
    for (int w : bw) {
        double r = w / static_cast<double>(bmed);
        if (r < 1.0 - tol || r > 1.0 + tol) return false;
    }
    int gmed = gaps.empty() ? bmed : median_of(gaps);
    if (gmed <= 0) return false;
    for (int g : gaps) {
        double r = g / static_cast<double>(gmed);
        if (r < 1.0 - tol || r > 1.0 + tol) return false;
    }
    double pitch = bmed + gmed;
    // Quiet margins in pitches (leading quiet already skipped).
    int trail = W - all.back().x1;
    qleft = all.front().x0 / pitch;
    qright = trail / pitch;
    bars = std::move(all);
    return true;
}

bool classify_postal(const BinaryImage& bin, const std::vector<PBar>& bars,
                     int y, std::vector<Bar4>& states, int& top, int& bot) {
    states.clear();
    top = bot = 0;
    if (bin.empty() || bars.empty() || y < 0 || y >= bin.height) return false;
    // Per-bar ink extents via the bar's center column.
    std::vector<int> tops, bots;
    try {
        tops.reserve(bars.size());
        bots.reserve(bars.size());
        for (const PBar& b : bars) {
            int xc = (b.x0 + b.x1) / 2;
            if (xc < 0 || xc >= bin.width) return false;
            // Contiguous ink around the scan row (bars are solid; this
            // bounds the extent to the bar plus anything touching it).
            int t = y;
            while (t - 1 >= 0 && bin.bits[(t - 1) * bin.stride + xc] == 0) --t;
            int e = y;
            while (e + 1 < bin.height &&
                   bin.bits[(e + 1) * bin.stride + xc] == 0)
                ++e;
            tops.push_back(t);
            bots.push_back(e);
        }
    } catch (...) {
        return false;
    }
    top = *std::min_element(tops.begin(), tops.end());
    bot = *std::max_element(bots.begin(), bots.end());
    int h = bot - top + 1;
    if (h < 8) return false;  // too small to resolve thirds
    try {
        for (size_t i = 0; i < bars.size(); ++i) {
            bool te = (tops[i] - top) < h * 0.2;
            bool be = (bot - bots[i]) < h * 0.2;
            states.push_back(te ? (be ? Bar4::Full : Bar4::Asc)
                                : (be ? Bar4::Desc : Bar4::Track));
        }
    } catch (...) {
        return false;
    }
    return true;
}

std::vector<Bar4> rotate180(const std::vector<Bar4>& bars) {
    std::vector<Bar4> out;
    try {
        out.reserve(bars.size());
        for (auto it = bars.rbegin(); it != bars.rend(); ++it) {
            Bar4 b = *it;
            if (b == Bar4::Asc)
                out.push_back(Bar4::Desc);
            else if (b == Bar4::Desc)
                out.push_back(Bar4::Asc);
            else
                out.push_back(b);
        }
    } catch (...) {
        out.clear();
    }
    return out;
}

}  // namespace postal
}  // namespace omniscan
