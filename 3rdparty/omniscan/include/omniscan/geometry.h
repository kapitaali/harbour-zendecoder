#pragma once
// 2D geometry: points, quads, homography helpers.
// All float-based, image space: origin top-left, +x right, +y down.
#include <array>
#include "omniscan/export.h"

namespace omniscan {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
    constexpr Point() noexcept = default;
    constexpr Point(float xx, float yy) noexcept : x(xx), y(yy) {}
};

struct Quad {
    // Order: p[0]=top-left, p[1]=top-right, p[2]=bottom-right, p[3]=bottom-left
    // (clockwise). Degenerate quads have zero area.
    std::array<Point, 4> p{};
    constexpr Quad() noexcept = default;
    constexpr Quad(Point tl, Point tr, Point br, Point bl) noexcept : p{tl, tr, br, bl} {}
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};

struct Matrix3x3 {
    // Row-major homography.
    std::array<float, 9> m{1,0,0, 0,1,0, 0,0,1};
};

OMNISCAN_API float quad_area(const Quad& q) noexcept;
OMNISCAN_API Rect quad_bounds(const Quad& q) noexcept;
OMNISCAN_API bool quad_is_degenerate(const Quad& q, float eps = 1e-3f) noexcept;

// Reorder 4 unordered points into clockwise TL,TR,BR,BL. Returns false if degenerate.
OMNISCAN_API bool order_quad_corners(const Point in[4], Quad& out) noexcept;

// Solve homography mapping src -> dst (4-point DLT). Returns false if singular.
OMNISCAN_API bool quad_homography(const Quad& src, const Quad& dst, Matrix3x3& H) noexcept;

// Apply homography to a point. Returns false when w ~ 0.
OMNISCAN_API bool apply_homography(const Matrix3x3& H, Point pt, Point& out) noexcept;

} // namespace omniscan
