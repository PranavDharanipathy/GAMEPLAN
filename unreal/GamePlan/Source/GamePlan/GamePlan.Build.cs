using UnrealBuildTool;
public class GamePlan : ModuleRules
{
    public GamePlan(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Slate", "SlateCore", "UMG", "GamePlanSim" });
    }
}
