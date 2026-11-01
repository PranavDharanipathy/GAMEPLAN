#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GamePlanGameMode.generated.h"

UCLASS()
class GAMEPLAN_API AGamePlanGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AGamePlanGameMode();
    virtual void StartPlay() override;
};
