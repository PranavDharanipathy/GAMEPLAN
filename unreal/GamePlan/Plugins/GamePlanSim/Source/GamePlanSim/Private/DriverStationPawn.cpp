#include "DriverStationPawn.h"
#include "Camera/CameraComponent.h"

ADriverStationPawn::ADriverStationPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("DriverEyes"));
    RootComponent = Camera;
    Camera->bUsePawnControlRotation = false;
}

void ADriverStationPawn::PlaceDriver(const FVector& StandPointUE, float EyeHeightM, float FacingYawDeg)
{
    SetActorLocation(FVector(StandPointUE.X, StandPointUE.Y, EyeHeightM * 100.f));
    BaseYaw = Yaw = FacingYawDeg;
    Camera->SetFieldOfView(FieldOfViewDeg);
    Camera->SetWorldRotation(FRotator(Pitch, Yaw, 0.f));
}

void ADriverStationPawn::AddLook(float YawAxis, float PitchAxis, float Dt)
{
    Yaw = FMath::Clamp(Yaw + YawAxis * LookSensitivity * Dt, BaseYaw - MaxYawFromFieldDeg, BaseYaw + MaxYawFromFieldDeg);
    Pitch = FMath::Clamp(Pitch + PitchAxis * LookSensitivity * Dt, MinPitchDeg, MaxPitchDeg);
}

void ADriverStationPawn::Tick(float Dt)
{
    Super::Tick(Dt);
    Camera->SetFieldOfView(FieldOfViewDeg);
    Camera->SetWorldRotation(FRotator(Pitch, Yaw, 0.f));
}
