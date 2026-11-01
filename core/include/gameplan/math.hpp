#pragma once
#include <algorithm>
#include <cmath>

namespace gp {

constexpr double kPi = 3.14159265358979323846;
constexpr double kG = 9.80665;
constexpr double kInch = 0.0254;

inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double sgn(double v) { return (v > 0) - (v < 0); }
inline double wrapAngle(double a) {
    a = std::fmod(a + kPi, 2 * kPi);
    if (a < 0) a += 2 * kPi;
    return a - kPi;
}
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

struct Vec2 {
    double x = 0, y = 0;
    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(double s) { x *= s; y *= s; return *this; }
    double dot(Vec2 o) const { return x * o.x + y * o.y; }
    double cross(Vec2 o) const { return x * o.y - y * o.x; }
    double norm() const { return std::sqrt(x * x + y * y); }
    double norm2() const { return x * x + y * y; }
    Vec2 normalized() const { double n = norm(); return n > 1e-12 ? *this / n : Vec2{0, 0}; }
    Vec2 rotated(double a) const { double c = std::cos(a), s = std::sin(a); return {c * x - s * y, s * x + c * y}; }
    Vec2 perp() const { return {-y, x}; }
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    double dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(Vec3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double norm() const { return std::sqrt(x * x + y * y + z * z); }
    double norm2() const { return x * x + y * y + z * z; }
    Vec3 normalized() const { double n = norm(); return n > 1e-12 ? *this / n : Vec3{0, 0, 0}; }
    Vec2 xy() const { return {x, y}; }
};

// Pose of a planar body (field frame) with small pitch/roll for tipping.
struct Pose2 {
    double x = 0, y = 0, heading = 0;  // heading: CCW from +x
};

// Oriented rectangle (robot footprint) in the field plane.
struct OBB2 {
    Vec2 c;
    double halfL = 0, halfW = 0;  // along local x (forward), local y (left)
    double angle = 0;
    Vec2 axisX() const { return {std::cos(angle), std::sin(angle)}; }
    Vec2 axisY() const { return {-std::sin(angle), std::cos(angle)}; }
    void corners(Vec2 out[4]) const {
        Vec2 ax = axisX() * halfL, ay = axisY() * halfW;
        out[0] = c + ax + ay; out[1] = c - ax + ay; out[2] = c - ax - ay; out[3] = c + ax - ay;
    }
    Vec2 toLocal(Vec2 p) const { Vec2 d = p - c; return {d.dot(axisX()), d.dot(axisY())}; }
    Vec2 toWorld(Vec2 l) const { return c + axisX() * l.x + axisY() * l.y; }
    bool contains(Vec2 p, double margin = 0) const {
        Vec2 l = toLocal(p);
        return std::fabs(l.x) <= halfL + margin && std::fabs(l.y) <= halfW + margin;
    }
    // closest point on/in box to p (world frame)
    Vec2 closest(Vec2 p) const {
        Vec2 l = toLocal(p);
        l.x = clampd(l.x, -halfL, halfL);
        l.y = clampd(l.y, -halfW, halfW);
        return toWorld(l);
    }
};

struct AABB2 {
    double xMin = 0, xMax = 0, yMin = 0, yMax = 0;
    bool contains(Vec2 p) const { return p.x >= xMin && p.x <= xMax && p.y >= yMin && p.y <= yMax; }
    bool intersectsCircle(Vec2 p, double r) const {
        double cx = clampd(p.x, xMin, xMax), cy = clampd(p.y, yMin, yMax);
        return (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) <= r * r;
    }
    bool intersectsOBB(const OBB2& b) const;
};

// Separating-axis test with minimum translation vector. Returns true if overlapping;
// normal points from a to b.
bool obbOverlap(const OBB2& a, const OBB2& b, Vec2& normal, double& depth, Vec2& contact);

// Distance from point to segment (3D).
double segmentPointDistance(Vec3 a, Vec3 b, Vec3 p, Vec3* closest = nullptr);

}  // namespace gp
