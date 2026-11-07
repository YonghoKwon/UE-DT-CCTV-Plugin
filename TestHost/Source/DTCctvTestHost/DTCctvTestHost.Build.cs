using UnrealBuildTool;

public class DTCctvTestHost : ModuleRules
{
	public DTCctvTestHost(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UMG", "Slate", "SlateCore", "DTCctv" });
		PrivateDependencyModuleNames.Add("RHI");
		PrivateDependencyModuleNames.Add("RenderCore");
		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "JsonUtilities" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Media", "MediaAssets", "MediaUtils" });
	}
}
