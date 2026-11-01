using UnrealBuildTool;
public class GamePlanEditorTarget : TargetRules
{
    public GamePlanEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("GamePlan");
    }
}
