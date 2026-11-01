// GamePlan field frame (metres, right-handed, +x toward the blue wall, +y away from the audience,
// +z up) <-> Unreal (centimetres, left-handed, +X forward, +Y right, +Z up).
#pragma once
#include "CoreMinimal.h"
#include "gameplan/math.hpp"

namespace GamePlanCoords
{
    inline FVector ToUE(const gp::Vec3& p) { return FVector(p.x * 100.0, -p.y * 100.0, p.z * 100.0); }
    inline FVector ToUE(const gp::Vec2& p, double z) { return FVector(p.x * 100.0, -p.y * 100.0, z * 100.0); }
    inline FRotator HeadingToUE(double headingRad) { return FRotator(0.0, -FMath::RadiansToDegrees(headingRad), 0.0); }
    inline gp::Vec3 FromUE(const FVector& v) { return gp::Vec3(v.X / 100.0, -v.Y / 100.0, v.Z / 100.0); }
}
