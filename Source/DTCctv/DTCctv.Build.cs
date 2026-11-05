using UnrealBuildTool;

public class DTCctv : ModuleRules
{
	public DTCctv(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UMG", "DeveloperSettings", "SlateCore", "Slate" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "DTCctvRender", "DTCctvFFmpeg" });
	}
}
