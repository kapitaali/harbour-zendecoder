#pragma once
/// @file
/// 2D geometry: points, quads, homography helpers.
///
/// All float-based, image space: origin top-left, +x right, +y down.
#include <array>
#include "omniscan/export.h"

namespace omniscan {

/// Pixel-space point.
struct Point {
    float x = 0.0f; ///< horizontal (right positive)
    float y = 0.0f; ///< vertical (down positive)
    constexpr Point() noexcept = default;
    constexpr Point(float xx, float yy) noexcept : x(xx), y(yy) {}
};

/// Four corners (e.g. a detected symbol's bounds).
struct Quad {
    // Order: p[0]=top-left, p[1]=top-right, p[2]=bottom-right, p[3]=bottom-left
    // (clockwise). Degenerate quads have zero area.
    std::array<Point, 4> p{}; ///< TL, TR, BR, BL (clockwise)
    constexpr Quad() noexcept = default;
    constexpr Quad(Point tl, Point tr, Point br, Point bl) noexcept : p{tl, tr, br, bl} {}
};

/// Axis-aligned rectangle.
struct Rect {
    float x = 0, y = 0; ///< top-left corner
    float w = 0, h = 0; ///< size
};

/// 3x3 matrix, row-major homography (homogeneous coordinates).
struct Matrix3x3 {
    // Row-major homography.
    std::array<float, 9> m{1,0,0, 0,1,0, 0,0,1}; ///< row-major, identity default
};

/// Quad area in square pixels (shoelace, always >= 0).
OMNISCAN_API float quad_area(const Quad& q) noexcept;
/// Axis-aligned bounding box of the four corners.
OMNISCAN_API Rect quad_bounds(const Quad& q) noexcept;
/// True when area <= eps (collapsed to a line/point).
OMNISCAN_API bool quad_is_degenerate(const Quad& q, float eps = 1e-3f) noexcept;

/// Reorder 4 unordered points into clockwise TL,TR,BR,BL.
/// Returns false if degenerate.
OMNISCAN_API bool order_quad_corners(const Point in[4], Quad& out) noexcept;

/// Solve the homography mapping @p src -> @p dst (4-point DLT).
/// Returns false if the problem is singular.
OMNISCAN_API bool quad_homography(const Quad& src, const Quad& dst, Matrix3x3& H) noexcept;

/// Apply a homography to a point (dehomogenizes).
/// Returns false when the resulting w is ~0.
OMNISCAN_API bool apply_homography(const Matrix3x3& H, Point pt, Point& out) noexcept;

} // namespace omniscan
