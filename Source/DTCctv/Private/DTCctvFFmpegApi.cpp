#include "DTCctvFFmpegApi.h"

#include "DTCctvLog.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include <atomic>

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_LINUX
#include <dlfcn.h>
#endif

FDTCctvFFmpegFunctions GDTCctvFFmpegFunctions;

namespace
{
	std::atomic<bool> Ready{ false };
	std::atomic<int32> Owners{ 0 };
	TArray<void*> LibraryHandles;
	FString LoadError;
#if WITH_DEV_AUTOMATION_TESTS
	FString TestFailure;
#endif

	const TCHAR* LibraryName(EDTCctvFFmpegLibrary Library)
	{
#if PLATFORM_WINDOWS
		static const TCHAR* Names[] = { TEXT("avutil-60.dll"), TEXT("swresample-6.dll"), TEXT("swscale-9.dll"), TEXT("avcodec-62.dll"), TEXT("avformat-62.dll") };
#else
		static const TCHAR* Names[] = { TEXT("libavutil.so.60"), TEXT("libswresample.so.6"), TEXT("libswscale.so.9"), TEXT("libavcodec.so.62"), TEXT("libavformat.so.62") };
#endif
		return Names[static_cast<int32>(Library)];
	}

	FDTCctvFFmpegLoadOps NativeOps()
	{
		FDTCctvFFmpegLoadOps Ops;
		Ops.Open = [](const FString& Path, FString& Error) -> void*
		{
			if (!IFileManager::Get().FileExists(*Path))
			{
				Error = TEXT("필수 파일 없음: ") + Path;
				return nullptr;
			}
#if PLATFORM_WINDOWS
			void* Handle = reinterpret_cast<void*>(::LoadLibraryExW(*Path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
			if (Handle == nullptr)
			{
				Error = FString::Printf(TEXT("라이브러리 로드 실패: %s (Windows 오류 %lu)"), *Path, ::GetLastError());
			}
			return Handle;
#elif PLATFORM_LINUX
			void* Handle = dlopen(TCHAR_TO_UTF8(*Path), RTLD_NOW | RTLD_LOCAL);
			if (Handle == nullptr)
			{
				const char* Detail = dlerror();
				Error = FString::Printf(TEXT("라이브러리 로드 실패: %s (%s)"), *Path, Detail != nullptr ? UTF8_TO_TCHAR(Detail) : TEXT("미확인 오류"));
			}
			return Handle;
#else
			Error = TEXT("지원하지 않는 FFmpeg 로드 플랫폼");
			return nullptr;
#endif
		};
		Ops.Resolve = [](void* Handle, const ANSICHAR* Name) -> void*
		{
#if PLATFORM_WINDOWS
			return reinterpret_cast<void*>(::GetProcAddress(reinterpret_cast<HMODULE>(Handle), Name));
#elif PLATFORM_LINUX
			return dlsym(Handle, Name);
#else
			return nullptr;
#endif
		};
		Ops.Close = [](void* Handle)
		{
#if PLATFORM_WINDOWS
			::FreeLibrary(reinterpret_cast<HMODULE>(Handle));
#elif PLATFORM_LINUX
			dlclose(Handle);
#endif
		};
		Ops.DescribeAddress = [](void* Address) -> FString
		{
#if PLATFORM_WINDOWS
			HMODULE Module = nullptr;
			WCHAR Path[32768] = {};
			if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(Address), &Module) && ::GetModuleFileNameW(Module, Path, UE_ARRAY_COUNT(Path)) > 0)
			{
				return FString(Path);
			}
#elif PLATFORM_LINUX
			Dl_info Info = {};
			if (dladdr(Address, &Info) != 0 && Info.dli_fname != nullptr)
			{
				return UTF8_TO_TCHAR(Info.dli_fname);
			}
#endif
			return TEXT("제공 모듈 미확인");
		};
		return Ops;
	}
}

bool DTCctvResolveFFmpeg(FDTCctvFFmpegFunctions& Functions, TArray<void*>& Handles, FString& Error,
	const FString& Directory, const FDTCctvFFmpegLoadOps& Ops)
{
	Functions = FDTCctvFFmpegFunctions();
	Handles.Reset();
	Error.Reset();
	FDTCctvFFmpegFunctions Candidate;
	TArray<void*> Opened;
	auto Fail = [&]()
	{
		for (int32 Index = Opened.Num() - 1; Index >= 0; --Index)
		{
			Ops.Close(Opened[Index]);
		}
		return false;
	};
	for (int32 Index = 0; Index < static_cast<int32>(EDTCctvFFmpegLibrary::Count); ++Index)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(Directory / LibraryName(static_cast<EDTCctvFFmpegLibrary>(Index)));
		void* Handle = Ops.Open(Path, Error);
		if (Handle == nullptr)
		{
			if (Error.IsEmpty())
			{
				Error = TEXT("라이브러리 로드 실패: ") + Path;
			}
			return Fail();
		}
		Opened.Add(Handle);
	}
#define DTCCTV_FFMPEG_FUNCTION(Library, Name) \
	{ \
		void* Address = Ops.Resolve(Opened[static_cast<int32>(EDTCctvFFmpegLibrary::Library)], #Name); \
		if (Address == nullptr) \
		{ \
			Error = FString::Printf(TEXT("필수 함수 없음: %s / %s"), LibraryName(EDTCctvFFmpegLibrary::Library), TEXT(#Name)); \
			return Fail(); \
		} \
		Candidate.Name = reinterpret_cast<decltype(Candidate.Name)>(Address); \
	}
#include "DTCctvFFmpegFunctions.inl"
#undef DTCCTV_FFMPEG_FUNCTION
	Functions = Candidate;
	Handles = MoveTemp(Opened);
	return true;
}

bool DTCctvInitializeFFmpeg()
{
	check(IsInGameThread());
	const FDTCctvFFmpegLoadOps Ops = NativeOps();
#if WITH_EDITOR
	const FString Directory = FPaths::GetPath(FModuleManager::Get().GetModuleFilename(TEXT("DTCctv")));
#else
	const FString Directory = FPlatformProcess::BaseDir();
#endif
	const bool bLoaded = DTCctvResolveFFmpeg(GDTCctvFFmpegFunctions, LibraryHandles, LoadError, Directory, Ops);
	Ready.store(bLoaded, std::memory_order_release);
	if (!bLoaded)
	{
		return false;
	}
#define DTCCTV_FFMPEG_FUNCTION(Library, Name) \
	UE_LOG(LogDTCctv, Log, TEXT("FFmpegBinding function=%s address=%p provider=%s"), TEXT(#Name), \
		reinterpret_cast<void*>(GDTCctvFFmpegFunctions.Name), *Ops.DescribeAddress(reinterpret_cast<void*>(GDTCctvFFmpegFunctions.Name)));
#include "DTCctvFFmpegFunctions.inl"
#undef DTCCTV_FFMPEG_FUNCTION
	UE_LOG(LogDTCctv, Log, TEXT("FFmpegLoader ready=1 directory=%s binding=local"), *Directory);
	return true;
}

bool DTCctvFFmpegReady()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!TestFailure.IsEmpty())
	{
		return false;
	}
#endif
	return Ready.load(std::memory_order_acquire);
}

FString DTCctvFFmpegLoadError()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!TestFailure.IsEmpty())
	{
		return TestFailure;
	}
#endif
	return LoadError.IsEmpty() ? TEXT("FFmpeg 라이브러리가 준비되지 않았습니다.") : LoadError;
}

void DTCctvShutdownFFmpeg()
{
	Ready.store(false, std::memory_order_release);
	if (Owners.load() != 0)
	{
		UE_LOG(LogDTCctv, Warning, TEXT("FFmpegLoader shutdown=process_lifetime owners=%d. 남은 소유자 때문에 라이브러리를 해제하지 않습니다."), Owners.load());
		return;
	}
	const FDTCctvFFmpegLoadOps Ops = NativeOps();
	for (int32 Index = LibraryHandles.Num() - 1; Index >= 0; --Index)
	{
		Ops.Close(LibraryHandles[Index]);
	}
	LibraryHandles.Reset();
	GDTCctvFFmpegFunctions = FDTCctvFFmpegFunctions();
	UE_LOG(LogDTCctv, Log, TEXT("FFmpegLoader shutdown=unloaded owners=0"));
}

void DTCctvRetainFFmpegOwner()
{
	Owners.fetch_add(1);
}

void DTCctvReleaseFFmpegOwner()
{
	Owners.fetch_sub(1);
}

int32 DTCctvFFmpegOwners()
{
	return Owners.load();
}

#if WITH_DEV_AUTOMATION_TESTS
void DTCctvSetFFmpegTestFailure(const FString& Error)
{
	check(IsInGameThread());
	TestFailure = Error;
}
#endif
