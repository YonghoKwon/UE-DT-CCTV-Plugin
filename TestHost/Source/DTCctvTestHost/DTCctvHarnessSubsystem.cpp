#include "DTCctvHarnessSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "DTCctvStream.h"
#include "DTCctvView.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDTCctvHarness, Log, All);

void UDTCctvHarnessSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const TCHAR* CommandLine = FCommandLine::Get();
	bEnabled = FParse::Value(CommandLine, TEXT("-CctvUrl="), Url);
	if (!bEnabled)
	{
		return;
	}
	FParse::Value(CommandLine, TEXT("-CctvCount="), Count);
	FParse::Value(CommandLine, TEXT("-CctvSeconds="), DurationSeconds);
	FParse::Value(CommandLine, TEXT("-CctvCsv="), CsvPath);
	FParse::Value(CommandLine, TEXT("-CctvShot="), ShotPath);
	bSoftware = FParse::Param(CommandLine, TEXT("CctvSoftware"));
}

void UDTCctvHarnessSubsystem::BuildViews()
{
	UWorld* World = GetGameInstance()->GetWorld();
	if (World == nullptr || World->GetFirstPlayerController() == nullptr)
	{
		return;
	}

	UUserWidget* Root = CreateWidget<UDTCctvHarnessRootWidget>(World->GetFirstPlayerController());
	UUniformGridPanel* Grid = Root->WidgetTree->ConstructWidget<UUniformGridPanel>();
	Root->WidgetTree->RootWidget = Grid;

	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UDTCctvView* View = Root->WidgetTree->ConstructWidget<UDTCctvView>();
		View->Config.Url = Url;
		View->Config.DecodeMode = bSoftware ? EDTCctvDecodeMode::SoftwareOnly : EDTCctvDecodeMode::Auto;
		UUniformGridSlot* GridSlot = Grid->AddChildToUniformGrid(View, Index / Columns, Index % Columns);
		GridSlot->SetHorizontalAlignment(HAlign_Fill);
		GridSlot->SetVerticalAlignment(VAlign_Fill);
		Views.Add(View);
	}
	Root->AddToViewport();

	bBuilt = true;
	StartSeconds = FPlatformTime::Seconds();
	WindowStartSeconds = StartSeconds;
	if (!CsvPath.IsEmpty())
	{
		FFileHelper::SaveStringToFile(
			TEXT("t,frame_fps,max_frame_ms,used_mb,playing,decode_fps_avg,display_fps_avg,decoded,displayed,superseded,dropped,errors,reconnects,queued,output_w,decoder,codec,src_w,src_h\n"),
			*CsvPath);
	}
}

void UDTCctvHarnessSubsystem::WriteSample(float DeltaTime)
{
	++FramesThisWindow;
	MaxFrameMs = FMath::Max(MaxFrameMs, DeltaTime * 1000.0);

	const double Now = FPlatformTime::Seconds();
	const double Window = Now - WindowStartSeconds;
	if (Window < 1.0)
	{
		return;
	}

	int32 Playing = 0;
	float DecodeFps = 0.f;
	float DisplayFps = 0.f;
	int64 Decoded = 0, Displayed = 0, Superseded = 0, Dropped = 0, Errors = 0;
	int32 Reconnects = 0, Queued = 0, OutputWidth = 0, SourceW = 0, SourceH = 0;
	FString Decoder, Codec;
	for (const UDTCctvView* View : Views)
	{
		const UDTCctvStream* Stream = View->GetStream();
		if (Stream == nullptr)
		{
			continue;
		}
		const FDTCctvStreamStats Stats = Stream->GetStats();
		Playing += Stats.State == EDTCctvState::Playing ? 1 : 0;
		DecodeFps += Stats.DecodeFps;
		DisplayFps += Stats.DisplayFps;
		Decoded += Stats.FramesDecoded;
		Displayed += Stats.FramesDisplayed;
		Superseded += Stats.FramesSuperseded;
		Dropped += Stats.PacketsDropped;
		Errors += Stats.DecodeErrors;
		Reconnects += Stats.Reconnects;
		Queued += Stats.QueuedPackets;
		OutputWidth = Stats.OutputSize.X;
		SourceW = Stats.SourceSize.X;
		SourceH = Stats.SourceSize.Y;
		Decoder = Stats.DecoderName;
		Codec = Stats.CodecName;
	}

	const int32 ViewCount = FMath::Max(1, Views.Num());
	const uint64 UsedMb = FPlatformMemory::GetStats().UsedPhysical / (1024 * 1024);
	const FString Line = FString::Printf(TEXT("%.1f,%.1f,%.1f,%llu,%d,%.1f,%.1f,%lld,%lld,%lld,%lld,%lld,%d,%d,%d,%s,%s,%d,%d\n"),
		Now - StartSeconds, FramesThisWindow / Window, MaxFrameMs, UsedMb, Playing,
		DecodeFps / ViewCount, DisplayFps / ViewCount, Decoded, Displayed, Superseded, Dropped, Errors, Reconnects, Queued,
		OutputWidth, *Decoder, *Codec, SourceW, SourceH);
	UE_LOG(LogDTCctvHarness, Log, TEXT("%s"), *Line.TrimEnd());
	if (!CsvPath.IsEmpty())
	{
		FFileHelper::SaveStringToFile(Line, *CsvPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
	}

	FramesThisWindow = 0;
	MaxFrameMs = 0.0;
	WindowStartSeconds = Now;
}

void UDTCctvHarnessSubsystem::Tick(float DeltaTime)
{
	if (!bBuilt)
	{
		BuildViews();
		return;
	}

	WriteSample(DeltaTime);

	const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
	if (!bShotTaken && !ShotPath.IsEmpty() && Elapsed >= DurationSeconds - 2.0)
	{
		bShotTaken = true;
		FScreenshotRequest::RequestScreenshot(ShotPath, true, false);
	}
	if (Elapsed >= DurationSeconds)
	{
		bEnabled = false;
		FPlatformMisc::RequestExit(false);
	}
}
