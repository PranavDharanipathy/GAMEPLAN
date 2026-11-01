using UnrealBuildTool;
public class GamePlanTarget : TargetRules
{
    public GamePlanTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("GamePlan");
    }
}
