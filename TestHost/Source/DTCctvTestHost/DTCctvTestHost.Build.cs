using UnrealBuildTool;

public class DTCctvTestHost : ModuleRules
{
	public DTCctvTestHost(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UMG", "Slate", "SlateCore", "DTCctv" });
		PrivateDependencyModuleNames.Add("RHI");
	}
}
