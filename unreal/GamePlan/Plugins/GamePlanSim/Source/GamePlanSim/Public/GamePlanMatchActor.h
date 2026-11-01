// Hosts one GamePlan match inside Unreal: the core MatchRunner is the single source of truth for
// physics, electrical, rules and the FTC runtime; this actor only renders its state and feeds live
// controller input. Simulation time advances on the core's fixed step, independent of frame rate.
#pragma once
#include <memory>
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GamePlanInput.h"
#include "GamePlanMatchActor.generated.h"

namespace gp { class MatchRunner; }
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class ADriverStationPawn;

UENUM(BlueprintType)
enum class EGamePlanControlMode : uint8 { Competition, Practice };

USTRUCT(BlueprintType)
struct FGamePlanDriverSetup
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRedAlliance = true;
    /** 0..1 along the ALLIANCE AREA width (audience end = 0). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float StandPosition = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float DistanceFromWallM = 0.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float EyeHeightM = 1.60f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float FieldOfViewDeg = 80.f;
};

UCLASS()
class GAMEPLANSIM_API AGamePlanMatchActor : public AActor
{
    GENERATED_BODY()
public:
    AGamePlanMatchActor();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;

    /** Match description (robots, packages, AUTO/TELEOP OpModes, AI, uncertainty) - same file as gp-sim uses. */
    UPROPERTY(EditAnywhere, Category = "GamePlan") FString MatchSetupPath = TEXT("examples/matches/fixture-runtime.match.json");
    UPROPERTY(EditAnywhere, Category = "GamePlan") int32 UserRobotIndex = 0;
    UPROPERTY(EditAnywhere, Category = "GamePlan") EGamePlanControlMode ControlMode = EGamePlanControlMode::Competition;
    UPROPERTY(EditAnywhere, Category = "GamePlan") FGamePlanDriverSetup Driver;
    UPROPERTY(EditAnywhere, Category = "GamePlan") float TimeScale = 1.f;   // >1 only allowed outside competition play
    UPROPERTY(EditAnywhere, Category = "GamePlan") FString RecordDir = TEXT("out/matches/live");

    // ---- read by the HUD (driver-facing information only) ----
    FString TimerText() const;
    FString PhaseText() const;
    int32 Score(bool bRed) const;
    FString ConnectionText() const;   // Connected / Reconnecting / Disconnected - never electrical data
    bool IsFinished() const { return bFinished; }
    FString ReportPath() const { return LastReport; }

private:
    void BuildField();
    void SyncVisuals();
    std::unique_ptr<gp::MatchRunner> Runner;
    FGamePlanInput Input;
    UPROPERTY() ADriverStationPawn* DriverPawn = nullptr;
    UPROPERTY() UInstancedStaticMeshComponent* Pollen = nullptr;
    UPROPERTY() UInstancedStaticMeshComponent* RedNectar = nullptr;
    UPROPERTY() UInstancedStaticMeshComponent* BlueNectar = nullptr;
    UPROPERTY() TArray<UStaticMeshComponent*> RobotBodies;
    UPROPERTY() TArray<UStaticMeshComponent*> HiveCells;
    bool bFinished = false;
    FString LastReport, LastError;
};
