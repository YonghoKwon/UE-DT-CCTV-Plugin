#pragma once

#include "DTCctvFFmpegInclude.h"

enum class EDTCctvFFmpegLibrary : uint8
{
	AvUtil,
	SwResample,
	SwScale,
	AvCodec,
	AvFormat,
	Count
};

struct FDTCctvFFmpegFunctions
{
#define DTCCTV_FFMPEG_FUNCTION(Library, Name) decltype(&::Name) Name = nullptr;
#include "DTCctvFFmpegFunctions.inl"
#undef DTCCTV_FFMPEG_FUNCTION
};

struct FDTCctvFFmpegLoadOps
{
	TFunction<void*(const FString&, FString&)> Open;
	TFunction<void*(void*, const ANSICHAR*)> Resolve;
	TFunction<void(void*)> Close;
	TFunction<FString(void*)> DescribeAddress;
};

extern FDTCctvFFmpegFunctions GDTCctvFFmpegFunctions;

FORCEINLINE const FDTCctvFFmpegFunctions& DTCctvFFmpeg()
{
	return GDTCctvFFmpegFunctions;
}

bool DTCctvResolveFFmpeg(FDTCctvFFmpegFunctions& Functions, TArray<void*>& Handles, FString& Error,
	const FString& Directory, const FDTCctvFFmpegLoadOps& Ops);
bool DTCctvInitializeFFmpeg();
bool DTCctvFFmpegReady();
FString DTCctvFFmpegLoadError();
void DTCctvShutdownFFmpeg();
void DTCctvRetainFFmpegOwner();
void DTCctvReleaseFFmpegOwner();
int32 DTCctvFFmpegOwners();

#if WITH_DEV_AUTOMATION_TESTS
void DTCctvSetFFmpegTestFailure(const FString& Error);
#endif
