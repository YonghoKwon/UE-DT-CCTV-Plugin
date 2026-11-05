using UnrealBuildTool;

public class DTCctvTestHostEditorTarget : TargetRules
{
	public DTCctvTestHostEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_3;
		ExtraModuleNames.Add("DTCctvTestHost");
	}
}
