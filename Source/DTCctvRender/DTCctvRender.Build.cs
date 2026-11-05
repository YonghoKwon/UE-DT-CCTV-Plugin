using UnrealBuildTool;

public class DTCctvRender : ModuleRules
{
	public DTCctvRender(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// PostConfigInit에 로드되어 셰이더 경로를 등록한다. 이 모듈에는 UObject를 두지 않는다.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "RHI" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "Projects" });
	}
}
