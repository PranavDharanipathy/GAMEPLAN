using System.IO;
using UnrealBuildTool;

// Links the platform build of gameplan_core produced by tools/build_unreal_core.py (same CMake
// sources as the headless gp-sim; never a separate physics implementation).
public class GamePlanSim : ModuleRules
{
    public GamePlanSim(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bEnableExceptions = true;   // the core reports configuration errors with exceptions
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Slate", "SlateCore", "UMG", "ProceduralMeshComponent" });
        string Repo = Path.GetFullPath(Path.Combine(PluginDirectory, "..", "..", "..", ".."));
        string Core = Path.Combine(PluginDirectory, "ThirdParty", "gameplan_core");
        PublicIncludePaths.Add(Path.Combine(Repo, "core", "include"));
        PublicDefinitions.Add("GP_SOURCE_ROOT=\"" + Repo.Replace("\\", "/") + "\"");
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(Core, "lib", "Win64", "gameplan_core.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(Core, "lib", "Win64", "zlib.lib"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicAdditionalLibraries.Add(Path.Combine(Core, "lib", "Linux", "libgameplan_core.a"));
            PublicSystemLibraries.Add("z");
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicAdditionalLibraries.Add(Path.Combine(Core, "lib", "Mac", "libgameplan_core.a"));
            PublicSystemLibraries.Add("z");
        }
        PublicDefinitions.Add("GP_HAVE_ZLIB=1");
    }
}
