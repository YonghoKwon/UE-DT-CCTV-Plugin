using UnrealBuildTool;

public class DTCctvTestHostTarget : TargetRules
{
	public DTCctvTestHostTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_3;
		ExtraModuleNames.Add("DTCctvTestHost");
	}
}
