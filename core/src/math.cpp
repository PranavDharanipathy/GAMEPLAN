#include "gameplan/math.hpp"

#include <limits>

namespace gp {

bool AABB2::intersectsOBB(const OBB2& b) const {
    OBB2 a;
    a.c = {(xMin + xMax) / 2, (yMin + yMax) / 2};
    a.halfL = (xMax - xMin) / 2;
    a.halfW = (yMax - yMin) / 2;
    Vec2 n, c;
    double d;
    return obbOverlap(a, b, n, d, c);
}

bool obbOverlap(const OBB2& a, const OBB2& b, Vec2& normal, double& depth, Vec2& contact) {
    Vec2 axes[4] = {a.axisX(), a.axisY(), b.axisX(), b.axisY()};
    Vec2 ca[4], cb[4];
    a.corners(ca);
    b.corners(cb);
    depth = std::numeric_limits<double>::max();
    for (auto& ax : axes) {
        double amin = 1e18, amax = -1e18, bmin = 1e18, bmax = -1e18;
        for (int i = 0; i < 4; ++i) {
            double pa = ca[i].dot(ax), pb = cb[i].dot(ax);
            amin = std::min(amin, pa); amax = std::max(amax, pa);
            bmin = std::min(bmin, pb); bmax = std::max(bmax, pb);
        }
        double o = std::min(amax, bmax) - std::max(amin, bmin);
        if (o <= 0) return false;
        if (o < depth) {
            depth = o;
            normal = ax;
            if ((b.c - a.c).dot(ax) < 0) normal = -ax;
        }
    }
    // Contact estimate: deepest corner of each box inside the other, averaged.
    Vec2 sum{0, 0};
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        if (a.contains(cb[i], 1e-9)) { sum += cb[i]; ++n; }
        if (b.contains(ca[i], 1e-9)) { sum += ca[i]; ++n; }
    }
    contact = n ? sum / double(n) : (a.c + b.c) * 0.5;
    return true;
}

double segmentPointDistance(Vec3 a, Vec3 b, Vec3 p, Vec3* closest) {
    Vec3 ab = b - a;
    double t = ab.norm2() > 0 ? clampd((p - a).dot(ab) / ab.norm2(), 0.0, 1.0) : 0.0;
    Vec3 c = a + ab * t;
    if (closest) *closest = c;
    return (p - c).norm();
}

}  // namespace gp
