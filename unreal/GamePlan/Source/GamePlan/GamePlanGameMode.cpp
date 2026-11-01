#include "GamePlanGameMode.h"
#include "GamePlanHUD.h"
#include "GamePlanMatchActor.h"
#include "Engine/World.h"

AGamePlanGameMode::AGamePlanGameMode() { HUDClass = AGamePlanHUD::StaticClass(); DefaultPawnClass = nullptr; }

void AGamePlanGameMode::StartPlay()
{
    Super::StartPlay();
    // The Drive screen: one match actor configured from the pre-match setup (alliance, driver spot,
    // eye height, AUTO/TELEOP OpModes, competition vs practice).
    GetWorld()->SpawnActor<AGamePlanMatchActor>();
}
