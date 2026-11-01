// Human driver viewpoint: a person standing in the ALLIANCE AREA. The camera is NEVER attached to
// the robot, never flies, and the robot may leave the view exactly as on a real field.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DriverStationPawn.generated.h"

class UCameraComponent;

UCLASS()
class GAMEPLANSIM_API ADriverStationPawn : public APawn
{
    GENERATED_BODY()
public:
    ADriverStationPawn();
    /** Place the eyes at a standing position (field frame, metres) and height above the floor. */
    void PlaceDriver(const FVector& StandPointUE, float EyeHeightM, float FacingYawDeg);
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditAnywhere, Category = "Driver View") float FieldOfViewDeg = 80.f;
    UPROPERTY(EditAnywhere, Category = "Driver View") float LookSensitivity = 90.f;    // deg/s at full stick
    UPROPERTY(EditAnywhere, Category = "Driver View") float MaxYawFromFieldDeg = 100.f; // natural head-turn limit
    UPROPERTY(EditAnywhere, Category = "Driver View") float MinPitchDeg = -70.f;
    UPROPERTY(EditAnywhere, Category = "Driver View") float MaxPitchDeg = 35.f;
    /** Look input (-1..1), fed by the mouse / a dedicated look stick, never by the robot's position. */
    void AddLook(float YawAxis, float PitchAxis, float DeltaSeconds);

private:
    UPROPERTY(VisibleAnywhere) UCameraComponent* Camera = nullptr;
    float BaseYaw = 0.f, Yaw = 0.f, Pitch = -15.f;
};
