using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;
using EpicGames.Core;

public class DTCctv : ModuleRules
{
	public DTCctv(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UMG", "DeveloperSettings", "SlateCore", "Slate" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "DTCctvRender", "DTCctvFFmpeg" });
		bool WithDTCore = HasDTCore(Target);
		PublicDefinitions.Add("DTCCTV_WITH_DTCORE=" + (WithDTCore ? "1" : "0"));
		if (WithDTCore)
		{
			PrivateDependencyModuleNames.Add("DTCore");
		}
	}

	private bool HasDTCore(ReadOnlyTargetRules Target)
	{
		if (Target.ProjectFile == null)
		{
			return false;
		}
		string ProjectDirectory = Target.ProjectFile.Directory.FullName;
		List<string> Roots = new List<string> { Path.Combine(ProjectDirectory, "Plugins"), Path.Combine(EngineDirectory, "Plugins"), Path.Combine(EngineDirectory, "Plugins", "Marketplace") };
		JsonObject Project = JsonObject.Read(Target.ProjectFile);
		if (Project.TryGetObjectArrayField("Plugins", out JsonObject[] Plugins))
		{
			foreach (JsonObject Plugin in Plugins)
			{
				if (Plugin.GetStringField("Name") == "DTCore" && Plugin.TryGetBoolField("Enabled", out bool Enabled) && !Enabled)
				{
					return false;
				}
			}
		}
		if (Project.TryGetStringArrayField("AdditionalPluginDirectories", out string[] Additional))
		{
			foreach (string Directory in Additional)
			{
				Roots.Add(Path.GetFullPath(Path.Combine(ProjectDirectory, Directory)));
			}
		}

		foreach (string Root in Roots)
		{
			foreach (string Candidate in new[] { Path.Combine(Root, "DTCore"), Root })
			{
				if (File.Exists(Path.Combine(Candidate, "DTCore.uplugin")) && File.Exists(Path.Combine(Candidate, "Source", "DTCore", "Public", "Core", "DxLogSubsystem.h")))
				{
					return true;
				}
			}
		}
		return false;
	}
}
