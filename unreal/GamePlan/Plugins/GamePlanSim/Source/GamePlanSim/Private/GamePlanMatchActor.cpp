#include "GamePlanMatchActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DriverStationPawn.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "GamePlanCoords.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

#include "gameplan/analysis.hpp"
#include "gameplan/match.hpp"

static UStaticMesh* Shape(const TCHAR* Path) { return LoadObject<UStaticMesh>(nullptr, Path); }

AGamePlanMatchActor::AGamePlanMatchActor()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AGamePlanMatchActor::BeginPlay()
{
    Super::BeginPlay();
    const std::string Root = GP_SOURCE_ROOT;
    try
    {
        gp::MatchSetup Setup = gp::MatchSetup::load(gp::resolvePath(TCHAR_TO_UTF8(*MatchSetupPath), Root));
        Setup.world.competitionMode = ControlMode == EGamePlanControlMode::Competition;
        if (Setup.world.competitionMode) TimeScale = 1.f;   // competition play is always real time
        Setup.recordDir = TCHAR_TO_UTF8(*RecordDir);
        Setup.raw.set("record", Setup.recordDir);
        Runner = std::make_unique<gp::MatchRunner>(Setup);
        std::string Err;
        if (!Runner->begin(Err)) { LastError = UTF8_TO_TCHAR(Err.c_str()); UE_LOG(LogTemp, Error, TEXT("GamePlan: %s"), *LastError); Runner.reset(); return; }
    }
    catch (const std::exception& E)
    {
        LastError = UTF8_TO_TCHAR(E.what());
        UE_LOG(LogTemp, Error, TEXT("GamePlan: %s"), *LastError);
        return;
    }
    BuildField();
    // Driver eyes in the chosen ALLIANCE AREA spot, facing the field; independent of the robot.
    const gp::SeasonDef& S = Runner->world().season();
    const gp::AABB2& Area = S.allianceArea[Driver.bRedAlliance ? 0 : 1];
    const double Y = FMath::Lerp(Area.yMin, Area.yMax, (double)Driver.StandPosition);
    const double X = Driver.bRedAlliance ? -S.half - Driver.DistanceFromWallM : S.half + Driver.DistanceFromWallM;
    DriverPawn = GetWorld()->SpawnActor<ADriverStationPawn>();
    DriverPawn->FieldOfViewDeg = Driver.FieldOfViewDeg;
    DriverPawn->PlaceDriver(GamePlanCoords::ToUE(gp::Vec2(X, Y), 0.0), Driver.EyeHeightM, Driver.bRedAlliance ? 0.f : 180.f);
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0)) PC->Possess(DriverPawn);
    if (!UGameplayStatics::GetPlayerController(this, 1)) UGameplayStatics::CreatePlayer(this, 1, true);   // second FTC gamepad
}

void AGamePlanMatchActor::BuildField()
{
    UStaticMesh* Cube = Shape(TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Cyl = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UStaticMesh* Sphere = Shape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    const gp::SeasonDef& S = Runner->world().season();
    auto AddBox = [&](const FVector& C, const FVector& SizeCm, const FRotator& R = FRotator::ZeroRotator) {
        UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
        M->SetStaticMesh(Cube);
        M->SetupAttachment(RootComponent);
        M->SetWorldLocationAndRotation(C, R);
        M->SetWorldScale3D(SizeCm / 100.0);
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // core owns all physics
        M->RegisterComponent();
        return M;
    };
    const double H = S.half * 200.0;
    AddBox(FVector(0, 0, -0.75), FVector(H, H, 1.5));                                  // tiles
    for (int Side = 0; Side < 4; ++Side) {                                              // perimeter
        const bool AlongX = Side < 2;
        const double Off = (Side % 2 ? 1 : -1) * (S.half * 100.0 + 1.27);
        AddBox(AlongX ? FVector(0, Off, S.wallHeight * 50) : FVector(Off, 0, S.wallHeight * 50),
               AlongX ? FVector(H + 5, 2.54, S.wallHeight * 100) : FVector(2.54, H + 5, S.wallHeight * 100));
    }
    for (auto& Leg : S.legs) {                                                          // HIVE frame
        const FVector A = GamePlanCoords::ToUE(Leg.base), B = GamePlanCoords::ToUE(Leg.apex);
        UStaticMeshComponent* M = AddBox((A + B) / 2, FVector(3.8, 3.8, (B - A).Size()), (B - A).Rotation() + FRotator(-90, 0, 0));
        M->SetStaticMesh(Cyl);
    }
    for (auto& F : S.flowers) {                                                         // FLOWERS
        UStaticMeshComponent* M = AddBox(GamePlanCoords::ToUE(F.axis, S.flowerTopH / 2), FVector(S.flowerOuterR * 200, S.flowerOuterR * 200, S.flowerTopH * 100));
        M->SetStaticMesh(Cyl);
    }
    for (int Hv = 0; Hv < 2; ++Hv) for (int C = 0; C < 2; ++C)                          // CELLS (posed every frame)
        HiveCells.Add(AddBox(FVector::ZeroVector, FVector(50.8, 35.6, 30.5)));
    for (auto& Slot : Runner->world().robots()) {
        const gp::RobotConfig& RC = Slot.robot->cfg;
        RobotBodies.Add(AddBox(FVector::ZeroVector, FVector(RC.length * 100, RC.width * 100, RC.height * 100)));
        // CAD-derived visual meshes replace this envelope when the robot description provides them.
    }
    auto MakeISM = [&](float DiameterM) {
        UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
        I->SetStaticMesh(Sphere);
        I->SetupAttachment(RootComponent);
        I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        I->RegisterComponent();
        return I;
    };
    Pollen = MakeISM(S.pollenD);
    RedNectar = MakeISM(S.nectarD);
    BlueNectar = MakeISM(S.nectarD);
}

void AGamePlanMatchActor::Tick(float Dt)
{
    Super::Tick(Dt);
    if (!Runner || bFinished) return;
    // Live gamepads: local players 0/1 are FTC gamepad1/gamepad2 of the user's robot.
    gp::GamepadState G1, G2;
    Input.Sample(UGameplayStatics::GetPlayerController(this, 0), G1);
    Input.Sample(UGameplayStatics::GetPlayerController(this, 1), G2);
    Runner->setLiveGamepads(UserRobotIndex, G1, G2);
    float LY, LP;
    Input.SampleLook(UGameplayStatics::GetPlayerController(this, 0), LY, LP);
    if (DriverPawn) DriverPawn->AddLook(LY, LP, Dt);
    std::string Err;
    // Clamp a single frame's catch-up so a hitch slows the simulation instead of exploding it.
    const bool More = Runner->advance(FMath::Min(Dt * TimeScale, 0.1f), Err);
    if (!Err.empty()) { LastError = UTF8_TO_TCHAR(Err.c_str()); UE_LOG(LogTemp, Error, TEXT("GamePlan: %s"), *LastError); }
    SyncVisuals();
    if (!More)
    {
        Runner->finish();
        const std::string Dir = Runner->setup().recordDir;
        gp::writeMatchReport(Dir, Dir + "/report.html");
        LastReport = UTF8_TO_TCHAR((Dir + "/report.html").c_str());
        bFinished = true;   // the HUD shows a small dismissible "report and replay available" notice
    }
}

void AGamePlanMatchActor::SyncVisuals()
{
    gp::World& W = Runner->world();
    for (int32 i = 0; i < RobotBodies.Num(); ++i) {
        const gp::Robot& R = *W.robots()[i].robot;
        RobotBodies[i]->SetWorldLocationAndRotation(GamePlanCoords::ToUE(R.pos, R.cfg.height / 2),
            FRotator(FMath::RadiansToDegrees(R.pitch), -FMath::RadiansToDegrees(R.heading), FMath::RadiansToDegrees(R.roll)));
    }
    Pollen->ClearInstances(); RedNectar->ClearInstances(); BlueNectar->ClearInstances();
    for (const gp::Element& E : W.elements()) {
        if (E.state == gp::ElemState::HumanArea || E.state == gp::ElemState::OutOfField) continue;
        const FTransform T(FRotator::ZeroRotator, GamePlanCoords::ToUE(E.p), FVector(E.radius * 2));
        (E.type == gp::ElemType::Pollen ? Pollen : (E.color == gp::Alliance::Red ? RedNectar : BlueNectar))->AddInstance(T, true);
    }
    const gp::SeasonDef& S = W.season();
    for (int Hv = 0; Hv < 2; ++Hv) for (int C = 0; C < 2; ++C) {
        const gp::CellDef& Cd = S.hives[Hv].cells[C];
        const gp::Vec3 P = W.tagPosition(Cd.tags[0]);   // cell pose follows the same rigid HIVE rotation as its tags
        HiveCells[Hv * 2 + C]->SetWorldLocation(GamePlanCoords::ToUE(P) + FVector(0, 0, 15));
    }
}

FString AGamePlanMatchActor::TimerText() const { return Runner ? UTF8_TO_TCHAR(Runner->world().rules().timerText(Runner->world().time()).c_str()) : TEXT("--:--"); }
FString AGamePlanMatchActor::PhaseText() const
{
    if (!Runner) return LastError.IsEmpty() ? TEXT("") : TEXT("ERROR");
    const FString P = UTF8_TO_TCHAR(gp::toString(Runner->world().rules().phase()));
    return Runner->setup().world.competitionMode ? P : P + TEXT(" (practice)");
}
int32 AGamePlanMatchActor::Score(bool bRed) const { return Runner ? Runner->world().rules().score(bRed ? gp::Alliance::Red : gp::Alliance::Blue).total() : 0; }
FString AGamePlanMatchActor::ConnectionText() const
{
    if (!Runner) return TEXT("Disconnected");
    const gp::Robot& R = *Runner->world().robots()[UserRobotIndex].robot;
    if (!R.rcAlive) return TEXT("Disconnected");
    if (R.disabled) return TEXT("Reconnecting");
    for (const auto& H : R.elec.hubs) if (H.inReset) return TEXT("Reconnecting");
    return TEXT("Connected");
}

void AGamePlanMatchActor::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Runner && !bFinished) Runner->finish();
    Runner.reset();
    Super::EndPlay(Reason);
}
