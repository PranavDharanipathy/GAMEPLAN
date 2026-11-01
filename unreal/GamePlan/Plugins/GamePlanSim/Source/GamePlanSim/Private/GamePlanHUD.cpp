#include "GamePlanHUD.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "GamePlanMatchActor.h"

void AGamePlanHUD::DrawHUD()
{
    Super::DrawHUD();
    AGamePlanMatchActor* M = nullptr;
    for (TActorIterator<AGamePlanMatchActor> It(GetWorld()); It; ++It) { M = *It; break; }
    if (!M || !Canvas) return;
    const float W = Canvas->ClipX;
    const FLinearColor Red(0.78f, 0.17f, 0.15f), Blue(0.12f, 0.37f, 0.75f), Fg(0.95f, 0.95f, 0.93f);
    DrawRect(FLinearColor(0, 0, 0, 0.55f), W / 2 - 170, 12, 340, 54);
    DrawText(FString::FromInt(M->Score(true)), Red, W / 2 - 150, 22, nullptr, 2.0f);
    DrawText(M->TimerText(), Fg, W / 2 - 38, 18, nullptr, 2.2f);
    DrawText(FString::FromInt(M->Score(false)), Blue, W / 2 + 110, 22, nullptr, 2.0f);
    DrawText(M->PhaseText(), Fg, W / 2 - 60, 70, nullptr, 1.0f);
    DrawText(M->ConnectionText(), M->ConnectionText() == TEXT("Connected") ? Fg : FLinearColor(1, 0.7f, 0.2f), 20, 20, nullptr, 1.1f);
    if (M->IsFinished() && bShowPostMatchNotice)
    {
        DrawRect(FLinearColor(0, 0, 0, 0.6f), W - 380, Canvas->ClipY - 70, 360, 50);
        DrawText(TEXT("Match report and replay ready  -  Tab: open  |  Esc: dismiss"), Fg, W - 370, Canvas->ClipY - 55, nullptr, 0.9f);
    }
}
