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
			string LibDir = Path.Combine(ModuleDirectory, "Win64", "lib");
			string BinDir = Path.Combine(ModuleDirectory, "Win64", "bin");
			foreach (string Lib in new[] { "avcodec", "avformat", "avutil", "swscale", "swresample" })
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibDir, Lib + ".lib"));
			}
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
				// 고유 링크 이름으로 기존 플러그인의 -lavcodec 등과 검색 결과를 분리한다.
				string LinkFile = "libdtcctv_" + Lib.Substring(3, Lib.IndexOf(".so") - 3) + ".so";
				PublicAdditionalLibraries.Add(Path.Combine(LibDir, LinkFile));
				RuntimeDependencies.Add("$(BinaryOutputDir)/" + Lib, LibPath);
			}
		}
	}
}
