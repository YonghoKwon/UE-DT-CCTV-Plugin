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
				// UBT는 버전 접미사를 가진 .so를 -l 이름으로 잘못 변환하므로 링커 스크립트를 사용한다.
				PublicAdditionalLibraries.Add(Path.Combine(LibDir, Lib.Substring(0, Lib.LastIndexOf(".so") + 3)));
				RuntimeDependencies.Add("$(BinaryOutputDir)/" + Lib, LibPath);
			}
		}
	}
}
