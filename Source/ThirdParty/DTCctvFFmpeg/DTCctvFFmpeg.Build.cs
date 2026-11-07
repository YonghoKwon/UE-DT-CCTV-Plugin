using System.IO;
using UnrealBuildTool;

public class DTCctvFFmpeg : ModuleRules
{
	public DTCctvFFmpeg(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string BinDir = Path.Combine(ModuleDirectory, "Win64", "bin");
			// 모듈 바이너리 옆으로 복사해 에디터·패키징 모두 같은 방식으로 로드한다.
			foreach (string Dll in new[] { "avcodec-62", "avformat-62", "avutil-60", "swscale-9", "swresample-6" })
			{
				RuntimeDependencies.Add("$(BinaryOutputDir)/" + Dll + ".dll", Path.Combine(BinDir, Dll + ".dll"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			string LibDir = Path.Combine(ModuleDirectory, "Linux", "lib");
			foreach (string Lib in new[] { "libavcodec.so.62", "libavformat.so.62", "libavutil.so.60", "libswscale.so.9", "libswresample.so.6" })
			{
				string LibPath = Path.Combine(LibDir, Lib);
				RuntimeDependencies.Add("$(BinaryOutputDir)/" + Lib, LibPath);
			}
		}
	}
}
