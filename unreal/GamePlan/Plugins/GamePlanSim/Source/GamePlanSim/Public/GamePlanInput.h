// Physical gamepads / keyboard -> FTC gamepad semantics (GamepadState), with per-device dead zone,
// response curve and calibration. Up to two FTC gamepads per robot (local players 0 and 1).
#pragma once
#include "CoreMinimal.h"
#include "gameplan/link.hpp"

class APlayerController;

struct FGamePlanStickCalibration
{
    float DeadZone = 0.06f;       // radial dead zone (typical Logitech F310 / Xbox drift)
    float Exponent = 1.0f;        // 1 = linear (FTC SDK passes raw values)
    float CenterX = 0.f, CenterY = 0.f;
    bool InvertY = true;          // FTC: pushing forward gives NEGATIVE left_stick_y
};

class GAMEPLANSIM_API FGamePlanInput
{
public:
    FGamePlanStickCalibration Left, Right;
    float TriggerDeadZone = 0.02f;
    bool KeyboardFallback = true;
    /** Sample one controller. Returns false when the device is disconnected. */
    bool Sample(APlayerController* PC, gp::GamepadState& Out) const;
    /** Look axes for the driver camera (mouse or right-stick-with-modifier, never the robot). */
    void SampleLook(APlayerController* PC, float& Yaw, float& Pitch) const;
private:
    void Stick(float X, float Y, const FGamePlanStickCalibration& C, float& OX, float& OY) const;
};
