// GamePlan field frame (metres, right-handed, +x toward the blue wall, +y away from the audience,
// +z up) <-> Unreal (centimetres, left-handed, +X forward, +Y right, +Z up).
#pragma once
#include "CoreMinimal.h"
#include <cmath>
#include "gameplan/math.hpp"

namespace GamePlanCoords
{
    inline FVector ToUE(const gp::Vec3& p) { return FVector(p.x * 100.0, -p.y * 100.0, p.z * 100.0); }
    inline FVector ToUE(const gp::Vec2& p, double z) { return FVector(p.x * 100.0, -p.y * 100.0, z * 100.0); }
    inline FRotator HeadingToUE(double headingRad) { return FRotator(0.0, -FMath::RadiansToDegrees(headingRad), 0.0); }
    inline gp::Vec3 FromUE(const FVector& v) { return gp::Vec3(v.X / 100.0, -v.Y / 100.0, v.Z / 100.0); }
    inline FVector ToUEDir(const gp::Vec3& d) { return FVector(d.x, -d.y, d.z); }
    // Orientation: mirroring y (right- to left-handed) maps quaternion (w, x, y, z) to (w, -x, y, -z).
    inline FQuat ToUE(const double q[4]) { return FQuat(-q[1], q[2], -q[3], q[0]); }
    inline FQuat ToUE(const gp::Vec3& axis, double angleRad) {
        const double s = std::sin(angleRad / 2);
        const double q[4] = {std::cos(angleRad / 2), axis.x * s, axis.y * s, axis.z * s};
        return ToUE(q);
    }
}
