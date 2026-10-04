// Geometry helpers: shoelace area, DLT homography (Gaussian elimination).
#include "omniscan/geometry.h"
#include <cmath>

namespace omniscan {

float quad_area(const Quad& q) noexcept {
    // Shoelace.
    float s = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const Point& a = q.p[i];
        const Point& b = q.p[(i + 1) % 4];
        s += a.x * b.y - b.x * a.y;
    }
    return std::fabs(s) * 0.5f;
}

Rect quad_bounds(const Quad& q) noexcept {
    Rect r{q.p[0].x, q.p[0].y, 0, 0};
    float x1 = q.p[0].x, y1 = q.p[0].y, x2 = x1, y2 = y1;
    for (int i = 1; i < 4; ++i) {
        if (q.p[i].x < x1) x1 = q.p[i].x;
        if (q.p[i].y < y1) y1 = q.p[i].y;
        if (q.p[i].x > x2) x2 = q.p[i].x;
        if (q.p[i].y > y2) y2 = q.p[i].y;
    }
    r.x = x1; r.y = y1; r.w = x2 - x1; r.h = y2 - y1;
    return r;
}

bool quad_is_degenerate(const Quad& q, float eps) noexcept {
    return quad_area(q) <= eps;
}

bool order_quad_corners(const Point in[4], Quad& out) noexcept {
    // Centroid angle sort, then rotate so top-left (min y, then min x) is first.
    float cx = 0, cy = 0;
    for (int i = 0; i < 4; ++i) { cx += in[i].x; cy += in[i].y; }
    cx *= 0.25f; cy *= 0.25f;
    // Simple insertion sort by atan2.
    Point s[4] = {in[0], in[1], in[2], in[3]};
    float a[4];
    for (int i = 0; i < 4; ++i) a[i] = std::atan2(s[i].y - cy, s[i].x - cx);
    for (int i = 1; i < 4; ++i) {
        Point ps = s[i]; float as = a[i]; int j = i - 1;
        while (j >= 0 && a[j] > as) { s[j+1] = s[j]; a[j+1] = a[j]; --j; }
        s[j+1] = ps; a[j+1] = as;
    }
    // s is now CCW starting at some angle; find TL.
    int tl = 0;
    for (int i = 1; i < 4; ++i) {
        if (s[i].y < s[tl].y || (s[i].y == s[tl].y && s[i].x < s[tl].x)) tl = i;
    }
    // atan2 order is CCW in standard orientation but y-down flips it to CW;
    // enforce clockwise TL,TR,BR,BL by picking direction with positive area.
    Quad cw(s[tl], s[(tl+1)%4], s[(tl+2)%4], s[(tl+3)%4]);
    // In y-down coords, this sequence is clockwise; verify area sign via shoelace.
    float signed_area = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const Point& p0 = cw.p[i];
        const Point& p1 = cw.p[(i+1)%4];
        signed_area += (p1.x - p0.x) * (p1.y + p0.y);
    }
    // signed_area > 0 means clockwise in y-down. If CCW, swap TR<->BL.
    if (signed_area < 0) {
        Quad ccw(cw.p[0], cw.p[3], cw.p[2], cw.p[1]);
        cw = ccw;
    }
    if (quad_is_degenerate(cw)) return false;
    out = cw;
    return true;
}

// 8x8 solve via Gaussian elimination with partial pivot (double precision).
static bool solve8(double A[8][9]) noexcept {
    for (int col = 0; col < 8; ++col) {
        int piv = col;
        double best = std::fabs(A[col][col]);
        for (int r = col + 1; r < 8; ++r) {
            double v = std::fabs(A[r][col]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-12) return false;
        if (piv != col) {
            for (int c = col; c < 9; ++c) {
                double t = A[col][c]; A[col][c] = A[piv][c]; A[piv][c] = t;
            }
        }
        double d = A[col][col];
        for (int c = col; c < 9; ++c) A[col][c] /= d;
        for (int r = 0; r < 8; ++r) {
            if (r == col) continue;
            double f = A[r][col];
            if (f != 0.0) {
                for (int c = col; c < 9; ++c) A[r][c] -= f * A[col][c];
            }
        }
    }
    return true;
}

bool quad_homography(const Quad& src, const Quad& dst, Matrix3x3& H) noexcept {
    if (quad_is_degenerate(src) || quad_is_degenerate(dst)) return false;
    double A[8][9] = {};
    for (int i = 0; i < 4; ++i) {
        double x = src.p[i].x, y = src.p[i].y;
        double u = dst.p[i].x, v = dst.p[i].y;
        int r = 2 * i;
        A[r][0] = x; A[r][1] = y; A[r][2] = 1;
        A[r][6] = -u * x; A[r][7] = -u * y; A[r][8] = u;
        A[r+1][3] = x; A[r+1][4] = y; A[r+1][5] = 1;
        A[r+1][6] = -v * x; A[r+1][7] = -v * y; A[r+1][8] = v;
    }
    if (!solve8(A)) return false;
    for (int i = 0; i < 8; ++i) H.m[i] = static_cast<float>(A[i][8]);
    H.m[8] = 1.0f;
    return true;
}

bool apply_homography(const Matrix3x3& H, Point pt, Point& out) noexcept {
    double x = pt.x, y = pt.y;
    double w = H.m[6]*x + H.m[7]*y + H.m[8];
    if (std::fabs(w) < 1e-9) return false;
    out.x = static_cast<float>((H.m[0]*x + H.m[1]*y + H.m[2]) / w);
    out.y = static_cast<float>((H.m[3]*x + H.m[4]*y + H.m[5]) / w);
    return true;
}

} // namespace omniscan
