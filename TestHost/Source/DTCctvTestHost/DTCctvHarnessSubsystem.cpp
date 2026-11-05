#include "DTCctvHarnessSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "DTCctvStream.h"
#include "DTCctvView.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDTCctvHarness, Log, All);

namespace
{
	FString FrameAgeHistogramString(const FDTCctvStreamStats& Stats)
	{
		FString Result;
		for (int32 Bin = 0; Bin < Stats.FrameAgeHistogram.Num(); ++Bin)
		{
			if (Stats.FrameAgeHistogram[Bin] > 0)
			{
				Result += FString::Printf(TEXT("%d:%lld;"), Bin, Stats.FrameAgeHistogram[Bin]);
			}
		}
		return Result;
	}

	void AddCounters(FDTCctvStreamStats& Total, const FDTCctvStreamStats& Stats)
	{
		Total.PacketsReceived += Stats.PacketsReceived;
		Total.FramesDecoded += Stats.FramesDecoded;
		Total.FramesDisplayed += Stats.FramesDisplayed;
		Total.FramesSuperseded += Stats.FramesSuperseded;
		Total.PacketsDropped += Stats.PacketsDropped;
		Total.DecodeErrors += Stats.DecodeErrors;
		Total.Reconnects += Stats.Reconnects;
		Total.PeakQueuedFrames = FMath::Max(Total.PeakQueuedFrames, Stats.PeakQueuedFrames);
		Total.FrameAgeSamples += Stats.FrameAgeSamples;
		Total.FrameAgeTotalMs += Stats.FrameAgeTotalMs;
		Total.FrameAgeMaxMs = FMath::Max(Total.FrameAgeMaxMs, Stats.FrameAgeMaxMs);
		Total.FrameAgeLatestMs = FMath::Max(Total.FrameAgeLatestMs, Stats.FrameAgeLatestMs);
		if (Total.FrameAgeHistogram.Num() < Stats.FrameAgeHistogram.Num())
		{
			Total.FrameAgeHistogram.SetNumZeroed(Stats.FrameAgeHistogram.Num());
		}
		for (int32 Bin = 0; Bin < Stats.FrameAgeHistogram.Num(); ++Bin)
		{
			Total.FrameAgeHistogram[Bin] += Stats.FrameAgeHistogram[Bin];
		}
		Total.FrameAgeMeanMs = Total.FrameAgeSamples > 0 ? Total.FrameAgeTotalMs / Total.FrameAgeSamples : 0.0;
		const int64 Target = FMath::CeilToInt64(Total.FrameAgeSamples * 0.95);
		int64 Cumulative = 0;
		for (int32 Bin = 0; Target > 0 && Bin < Total.FrameAgeHistogram.Num(); ++Bin)
		{
			Cumulative += Total.FrameAgeHistogram[Bin];
			if (Cumulative >= Target)
			{
				Total.FrameAgeP95Ms = Bin == 1001 ? Total.FrameAgeMaxMs : Bin;
				break;
			}
		}
	}
}

void UDTCctvHarnessSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const TCHAR* CommandLine = FCommandLine::Get();
	FString UrlEnvironment;
	if (FParse::Value(CommandLine, TEXT("-CctvUrlEnv="), UrlEnvironment))
	{
		Url = FPlatformMisc::GetEnvironmentVariable(*UrlEnvironment);
		bEnabled = !Url.IsEmpty();
	}
	else
	{
		bEnabled = FParse::Value(CommandLine, TEXT("-CctvUrl="), Url);
	}
	if (!bEnabled)
	{
		return;
	}
	FParse::Value(CommandLine, TEXT("-CctvCount="), Count);
	FParse::Value(CommandLine, TEXT("-CctvSeconds="), DurationSeconds);
	FParse::Value(CommandLine, TEXT("-CctvCsv="), CsvPath);
	FParse::Value(CommandLine, TEXT("-CctvShot="), ShotPath);
	FParse::Value(CommandLine, TEXT("-CctvSubUrl="), SubUrl);
	FParse::Value(CommandLine, TEXT("-CctvResizeScenario="), ResizeScenario);
	FParse::Value(CommandLine, TEXT("-CctvResizePeriod="), ResizePeriod);
	FParse::Value(CommandLine, TEXT("-CctvResizeAffected="), ResizeAffected);
	FParse::Value(CommandLine, TEXT("-CctvShotEvery="), ShotEvery);
	FParse::Value(CommandLine, TEXT("-CctvCloseOpenCycles="), CloseOpenCycles);
	FParse::Value(CommandLine, TEXT("-CctvCloseOpenInterval="), CloseOpenInterval);
	FParse::Value(CommandLine, TEXT("-CctvCloseHold="), CloseHoldSeconds);
	FParse::Value(CommandLine, TEXT("-CctvStallAt="), StallAtSeconds);
	FParse::Value(CommandLine, TEXT("-CctvStallSeconds="), StallSeconds);
	StallSeconds = FMath::Clamp(StallSeconds, 0.0, 10.0);
	bSoftware = FParse::Param(CommandLine, TEXT("CctvSoftware"));
	Count = FMath::Clamp(Count, 1, 64);
	ResizeAffected = FMath::Clamp(ResizeAffected, 1, Count);
	DurationSeconds = FMath::Max(1.0, DurationSeconds);
	ResizePeriod = FMath::Max(8.0, ResizePeriod);
	CloseOpenInterval = FMath::Max(1.0, CloseOpenInterval);
	CloseHoldSeconds = FMath::Max(0.5, CloseHoldSeconds);
	CloseOpenCycles = FMath::Max(0, CloseOpenCycles);
	NextCycleSeconds = CloseOpenInterval;
	NextShotSeconds = 10.0;
	ResizeScenario.ToLowerInline();
	if (!ResizeScenario.IsEmpty() && ResizeScenario != TEXT("drag") && ResizeScenario != TEXT("extremes") &&
		ResizeScenario != TEXT("subset") && ResizeScenario != TEXT("disconnected"))
	{
		UE_LOG(LogDTCctvHarness, Error, TEXT("알 수 없는 크기 변경 시나리오입니다."));
		bEnabled = false;
		return;
	}
	if (!CsvPath.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(CsvPath), true);
		ChannelCsvPath = FPaths::ChangeExtension(CsvPath, TEXT("channels.csv"));
		EventCsvPath = FPaths::ChangeExtension(CsvPath, TEXT("events.csv"));
	}
}

void UDTCctvHarnessSubsystem::BuildViews()
{
	UWorld* World = GetGameInstance()->GetWorld();
	if (World == nullptr || World->GetFirstPlayerController() == nullptr)
	{
		return;
	}

	RootWidget = CreateWidget<UDTCctvHarnessRootWidget>(World->GetFirstPlayerController());
	if (RootWidget == nullptr)
	{
		return;
	}
	Canvas = RootWidget->WidgetTree->ConstructWidget<UCanvasPanel>();
	RootWidget->WidgetTree->RootWidget = Canvas;

	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UDTCctvView* View = RootWidget->WidgetTree->ConstructWidget<UDTCctvView>();
		View->Config.Url = Url;
		View->Config.SubUrl = SubUrl;
		View->Config.DecodeMode = bSoftware ? EDTCctvDecodeMode::SoftwareOnly : EDTCctvDecodeMode::Auto;
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(View);
		Slot->SetPosition(FVector2D((Index % Columns) * 480.0, (Index / Columns) * 270.0));
		Slot->SetSize(FVector2D(480.0, 270.0));
		Views.Add(View);
	}
	RootWidget->AddToViewport();
	ClosedTotals.SetNum(Count);
	RequestedSizes.SetNum(Count);

	bBuilt = true;
	StartSeconds = FPlatformTime::Seconds();
	WindowStartSeconds = StartSeconds;
	if (!CsvPath.IsEmpty())
	{
		FFileHelper::SaveStringToFile(
			TEXT("t,frame_fps,max_frame_ms,used_mb,playing,decode_fps_avg,display_fps_avg,decoded,displayed,superseded,dropped,errors,reconnects,queued,output_w,decoder,codec,src_w,src_h,queued_frames,peak_queued_frames,render_in_flight,frame_age_samples,frame_age_total_ms,frame_age_mean_ms,frame_age_p95_ms,frame_age_max_ms,frame_age_latest_ms,frame_age_hist\n"),
			*CsvPath);
		FFileHelper::SaveStringToFile(TEXT("t,channel,state,decode_fps,display_fps,decoded,displayed,dropped,errors,reconnects,output_w,output_h,src_w,src_h,view_w,view_h,cycle,closed,decoder,codec,queued_frames,peak_queued_frames,render_in_flight,frame_age_samples,frame_age_total_ms,frame_age_mean_ms,frame_age_p95_ms,frame_age_max_ms,frame_age_latest_ms,frame_age_hist\n"), *ChannelCsvPath);
		FFileHelper::SaveStringToFile(TEXT("t,event,cycle\n"), *EventCsvPath);
	}
	WriteEvent(TEXT("start"), 0.0);
}

void UDTCctvHarnessSubsystem::Deinitialize()
{
	bEnabled = false;
	for (UDTCctvView* View : Views)
	{
		View->Stop();
	}
	if (RootWidget != nullptr)
	{
		RootWidget->RemoveFromParent();
	}
	Views.Reset();
	Canvas = nullptr;
	RootWidget = nullptr;
	Super::Deinitialize();
}

FDTCctvStreamStats UDTCctvHarnessSubsystem::GetLifetimeStats(int32 Index) const
{
	const UDTCctvStream* Stream = Views[Index]->GetStream();
	FDTCctvStreamStats Stats = !bCycleClosed && Stream != nullptr ? Stream->GetStats() : FDTCctvStreamStats();
	AddCounters(Stats, ClosedTotals[Index]);
	return Stats;
}

void UDTCctvHarnessSubsystem::WriteEvent(const FString& Event, double Elapsed)
{
	UE_LOG(LogDTCctvHarness, Log, TEXT("시나리오: %s, %.3f초, 반복 %d"), *Event, Elapsed, CompletedCycles);
	if (!EventCsvPath.IsEmpty())
	{
		const FString Line = FString::Printf(TEXT("%.3f,%s,%d\n"), Elapsed, *Event, CompletedCycles);
		FFileHelper::SaveStringToFile(Line, *EventCsvPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
	}
}

void UDTCctvHarnessSubsystem::UpdateScenario(double Elapsed)
{
	if (!bCycleClosed && CompletedCycles < CloseOpenCycles && Elapsed >= NextCycleSeconds)
	{
		for (int32 Index = 0; Index < Views.Num(); ++Index)
		{
			if (UDTCctvStream* Stream = Views[Index]->GetStream())
			{
				AddCounters(ClosedTotals[Index], Stream->GetStats());
				Stream->Close();
			}
		}
		bCycleClosed = true;
		ReopenSeconds = Elapsed + CloseHoldSeconds;
		WriteEvent(TEXT("close"), Elapsed);
	}
	if (bCycleClosed && Elapsed >= ReopenSeconds)
	{
		for (UDTCctvView* View : Views)
		{
			if (UDTCctvStream* Stream = View->GetStream())
			{
				Stream->Open(View->Config);
			}
		}
		bCycleClosed = false;
		++CompletedCycles;
		NextCycleSeconds = Elapsed + CloseOpenInterval;
		WriteEvent(TEXT("open"), Elapsed);
	}

	FVector2D Area = Canvas->GetCachedGeometry().GetLocalSize();
	if (Area.X <= 0.0 || Area.Y <= 0.0)
	{
		Area = FVector2D(1920.0, 1080.0);
	}
	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
	const int32 Rows = FMath::DivideAndRoundUp(Count, Columns);
	const FVector2D Cell(Area.X / Columns, Area.Y / Rows);
	const double Phase = FMath::Fmod(Elapsed, ResizePeriod) / ResizePeriod;
	const int32 ScenarioPhase = FMath::FloorToInt(Elapsed * 2.0 / ResizePeriod);
	if (!ResizeScenario.IsEmpty() && ScenarioPhase != LastScenarioPhase)
	{
		LastScenarioPhase = ScenarioPhase;
		WriteEvent(ResizeScenario + FString::Printf(TEXT("-%d"), ScenarioPhase), Elapsed);
	}
	for (int32 Index = 0; Index < Views.Num(); ++Index)
	{
		FVector2D Size = Cell;
		FVector2D Position((Index % Columns) * Cell.X, (Index / Columns) * Cell.Y);
		const bool bAffected = Index < ResizeAffected && !ResizeScenario.IsEmpty();
		if (bAffected)
		{
			Position = FVector2D::ZeroVector;
			if (ResizeScenario == TEXT("drag") || ResizeScenario == TEXT("disconnected"))
			{
				const double Alpha = 0.5 - 0.5 * FMath::Cos(Phase * 2.0 * PI);
				Size = FVector2D(32.0 + (Area.X - 32.0) * Alpha, 18.0 + (Area.Y - 18.0) * Alpha);
			}
			else if (ResizeScenario == TEXT("extremes"))
			{
				Size = Phase < 0.5 ? FVector2D(32.0, 18.0) : Area;
			}
			else if (ResizeScenario == TEXT("subset"))
			{
				Size = Phase < 0.5 ? Cell : Area * 0.8;
			}
		}
		UCanvasPanelSlot* Slot = CastChecked<UCanvasPanelSlot>(Views[Index]->Slot);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
		Slot->SetZOrder(bAffected ? 1 + Index : 0);
		RequestedSizes[Index] = Size;
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
	FString ChannelLines;
	int32 QueuedFrames = 0, PeakFrames = 0, RenderCommands = 0;
	FDTCctvStreamStats FrameAgeTotals;
	for (int32 Index = 0; Index < Views.Num(); ++Index)
	{
		const UDTCctvView* View = Views[Index];
		const UDTCctvStream* Stream = View->GetStream();
		if (Stream == nullptr)
		{
			continue;
		}
		const FDTCctvStreamStats Stats = GetLifetimeStats(Index);
		ChannelLines += FString::Printf(TEXT("%.3f,%d,%d,%.3f,%.3f,%lld,%lld,%lld,%lld,%d,%d,%d,%d,%d,%.1f,%.1f,%d,%d,%s,%s,%d,%d,%d,%lld,%.3f,%.3f,%.3f,%.3f,%.3f,%s\n"),
			Now - StartSeconds, Index, static_cast<int32>(Stats.State), Stats.DecodeFps, Stats.DisplayFps,
			Stats.FramesDecoded, Stats.FramesDisplayed, Stats.PacketsDropped, Stats.DecodeErrors, Stats.Reconnects,
			Stats.OutputSize.X, Stats.OutputSize.Y, Stats.SourceSize.X, Stats.SourceSize.Y,
			RequestedSizes[Index].X, RequestedSizes[Index].Y, CompletedCycles, bCycleClosed ? 1 : 0, *Stats.DecoderName, *Stats.CodecName,
			Stats.QueuedFrames, Stats.PeakQueuedFrames, Stats.RenderCommandsInFlight,
			Stats.FrameAgeSamples, Stats.FrameAgeTotalMs, Stats.FrameAgeMeanMs, Stats.FrameAgeP95Ms, Stats.FrameAgeMaxMs, Stats.FrameAgeLatestMs,
			*FrameAgeHistogramString(Stats));
		AddCounters(FrameAgeTotals, Stats);
		QueuedFrames += Stats.QueuedFrames;
		PeakFrames = FMath::Max(PeakFrames, Stats.PeakQueuedFrames);
		RenderCommands += Stats.RenderCommandsInFlight;
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
	const FString Line = FString::Printf(TEXT("%.1f,%.1f,%.1f,%llu,%d,%.1f,%.1f,%lld,%lld,%lld,%lld,%lld,%d,%d,%d,%s,%s,%d,%d,%d,%d,%d,%lld,%.3f,%.3f,%.3f,%.3f,%.3f,%s\n"),
		Now - StartSeconds, FramesThisWindow / Window, MaxFrameMs, UsedMb, Playing,
		DecodeFps / ViewCount, DisplayFps / ViewCount, Decoded, Displayed, Superseded, Dropped, Errors, Reconnects, Queued,
		OutputWidth, *Decoder, *Codec, SourceW, SourceH, QueuedFrames, PeakFrames, RenderCommands,
		FrameAgeTotals.FrameAgeSamples, FrameAgeTotals.FrameAgeTotalMs, FrameAgeTotals.FrameAgeMeanMs, FrameAgeTotals.FrameAgeP95Ms, FrameAgeTotals.FrameAgeMaxMs, FrameAgeTotals.FrameAgeLatestMs,
		*FrameAgeHistogramString(FrameAgeTotals));
	UE_LOG(LogDTCctvHarness, Log, TEXT("%s"), *Line.TrimEnd());
	if (!CsvPath.IsEmpty())
	{
		FFileHelper::SaveStringToFile(Line, *CsvPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
		FFileHelper::SaveStringToFile(ChannelLines, *ChannelCsvPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
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

	double Elapsed = FPlatformTime::Seconds() - StartSeconds;
	if (!bStallDone && StallAtSeconds > 0.0 && Elapsed >= StallAtSeconds)
	{
		bStallDone = true;
		WriteEvent(TEXT("stall-begin"), Elapsed);
		// 시험 전용으로 게임 틱만 멈춘다. 수신·디코딩 스레드는 계속 진행한다.
		FPlatformProcess::SleepNoStats(StallSeconds);
		Elapsed = FPlatformTime::Seconds() - StartSeconds;
		WriteEvent(TEXT("stall-end"), Elapsed);
	}
	if (!bFrameAgeWarmupReset && Elapsed >= 10.0)
	{
		for (int32 Index = 0; Index < Views.Num(); ++Index)
		{
			if (UDTCctvStream* Stream = Views[Index]->GetStream())
			{
				Stream->ResetFrameAgeStats();
			}
			ClosedTotals[Index].FrameAgeSamples = 0;
			ClosedTotals[Index].FrameAgeTotalMs = ClosedTotals[Index].FrameAgeMeanMs = ClosedTotals[Index].FrameAgeP95Ms = ClosedTotals[Index].FrameAgeMaxMs = 0.0;
			ClosedTotals[Index].FrameAgeHistogram.Reset();
		}
		bFrameAgeWarmupReset = true;
		WriteEvent(TEXT("frame-age-warmup-reset"), Elapsed);
	}
	UpdateScenario(Elapsed);
	WriteSample(DeltaTime);
	if (ShotEvery > 0.0 && !ShotPath.IsEmpty() && Elapsed >= NextShotSeconds && FApp::CanEverRender())
	{
		const FString SequencePath = FPaths::ChangeExtension(ShotPath, TEXT("")) + FString::Printf(TEXT("-%04d.png"), ShotIndex++);
		FScreenshotRequest::RequestScreenshot(SequencePath, true, false);
		NextShotSeconds = Elapsed + FMath::Max(0.1, ShotEvery);
	}
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
