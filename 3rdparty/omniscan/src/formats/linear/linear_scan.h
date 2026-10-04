#pragma once
// Internal 1D scanline infrastructure (not public API). All helpers are pure
// w.r.t. their inputs, deterministic, and never throw across boundaries.
#include <cstddef>
#include <vector>
#include "omniscan/binarizer.h"
#include "omniscan/image.h"

namespace omniscan {
namespace linear {

// Extract alternating runs along one line of a binarized image, starting and
// ending with a bar (ink = 0-bits). Leading/trailing quiet (1-bits) skipped.
// horizontal: line is row `fixed`, x in [from, to). Else column.
// min_bars: minimum number of bar runs required. rx0/rx1: ink extent.
// Returns false on invalid input or too few bars.
bool extract_runs(const BinaryImage& bin, int fixed, int from, int to,
                  bool horizontal, int min_bars, std::vector<int>& out_runs,
                  int& rx0, int& rx1);

// Quantize run widths to integer modules. Module = minimum run width (clean
// prints). Each run must satisfy |w/mod - round| <= tol and 1 <= q <= max_q.
bool quantize_runs(const std::vector<int>& runs, int max_q, double tol,
                   std::vector<int>& q, double& mod);

// Lines to attempt: center row first; the center column follows it at
// default settings as the 90-degree retry, and try_harder adds the eighth
// lines in both orientations (for off-center codes). native_linear stops
// at the first line that produces a hit when not try_harder, so only the
// first planned line normally runs.
struct ScanLine {
    bool horizontal = true;
    int fixed = 0;  // row y (horizontal) or column x (vertical)
};
void plan_scans(const ImageView& img, bool try_harder,
                std::vector<ScanLine>& out);

// Ink extent of columns [x0,x1) around row y (clamped). Used for quads.
void vertical_extent(const BinaryImage& bin, int x0, int x1, int y, int& top,
                     int& bot);
// Ink extent of rows [y0,y1) around column x (for vertical scans).
void horizontal_extent(const BinaryImage& bin, int y0, int y1, int x, int& l,
                       int& r);

}  // namespace linear
}  // namespace omniscan
