#include "GamePlanInput.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

void FGamePlanInput::Stick(float X, float Y, const FGamePlanStickCalibration& C, float& OX, float& OY) const
{
    X -= C.CenterX; Y -= C.CenterY;
    const float R = FMath::Sqrt(X * X + Y * Y);
    if (R < C.DeadZone) { OX = OY = 0.f; return; }
    const float Scaled = FMath::Pow(FMath::Clamp((R - C.DeadZone) / (1.f - C.DeadZone), 0.f, 1.f), C.Exponent);
    OX = X / R * Scaled;
    OY = Y / R * Scaled * (C.InvertY ? -1.f : 1.f);
}

bool FGamePlanInput::Sample(APlayerController* PC, gp::GamepadState& G) const
{
    G = gp::GamepadState();
    if (!PC) return false;
    float lx, ly, rx, ry;
    Stick(PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX), PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY), Left, lx, ly);
    Stick(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX), PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY), Right, rx, ry);
    G.lx = lx; G.ly = ly; G.rx = rx; G.ry = ry;
    auto Trig = [&](const FKey& K) { float v = PC->GetInputAnalogKeyState(K); return v < TriggerDeadZone ? 0.f : v; };
    G.lt = Trig(EKeys::Gamepad_LeftTriggerAxis);
    G.rt = Trig(EKeys::Gamepad_RightTriggerAxis);
    auto Btn = [&](const FKey& K, uint32 Bit) { if (PC->IsInputKeyDown(K)) G.buttons |= Bit; };
    Btn(EKeys::Gamepad_FaceButton_Bottom, gp::BTN_A); Btn(EKeys::Gamepad_FaceButton_Right, gp::BTN_B);
    Btn(EKeys::Gamepad_FaceButton_Left, gp::BTN_X); Btn(EKeys::Gamepad_FaceButton_Top, gp::BTN_Y);
    Btn(EKeys::Gamepad_LeftShoulder, gp::BTN_LB); Btn(EKeys::Gamepad_RightShoulder, gp::BTN_RB);
    Btn(EKeys::Gamepad_DPad_Up, gp::BTN_DUP); Btn(EKeys::Gamepad_DPad_Down, gp::BTN_DDOWN);
    Btn(EKeys::Gamepad_DPad_Left, gp::BTN_DLEFT); Btn(EKeys::Gamepad_DPad_Right, gp::BTN_DRIGHT);
    Btn(EKeys::Gamepad_Special_Left, gp::BTN_BACK); Btn(EKeys::Gamepad_Special_Right, gp::BTN_START);
    Btn(EKeys::Gamepad_LeftThumbstick, gp::BTN_LSTICK); Btn(EKeys::Gamepad_RightThumbstick, gp::BTN_RSTICK);
    if (KeyboardFallback)
    {
        if (PC->IsInputKeyDown(EKeys::W)) G.ly = -1.f;
        if (PC->IsInputKeyDown(EKeys::S)) G.ly = 1.f;
        if (PC->IsInputKeyDown(EKeys::A)) G.lx = -1.f;
        if (PC->IsInputKeyDown(EKeys::D)) G.lx = 1.f;
        if (PC->IsInputKeyDown(EKeys::Q)) G.rx = -1.f;
        if (PC->IsInputKeyDown(EKeys::E)) G.rx = 1.f;
        if (PC->IsInputKeyDown(EKeys::SpaceBar)) G.buttons |= gp::BTN_A;
        if (PC->IsInputKeyDown(EKeys::LeftShift)) G.rt = 1.f;
    }
    return true;
}

void FGamePlanInput::SampleLook(APlayerController* PC, float& Yaw, float& Pitch) const
{
    float mx = 0, my = 0;
    if (PC) PC->GetInputMouseDelta(mx, my);
    Yaw = FMath::Clamp(mx * 0.5f, -1.f, 1.f);
    Pitch = FMath::Clamp(my * 0.5f, -1.f, 1.f);
}
