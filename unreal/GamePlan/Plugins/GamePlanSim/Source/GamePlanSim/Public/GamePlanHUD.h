// Driver-facing match HUD: timer, phase, alliance, score, connection status. Deliberately NO
// electrical telemetry, current, voltage or warnings (those live in the post-match workspace).
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GamePlanHUD.generated.h"

UCLASS()
class GAMEPLANSIM_API AGamePlanHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    UPROPERTY(EditAnywhere) bool bShowPostMatchNotice = true;
};
