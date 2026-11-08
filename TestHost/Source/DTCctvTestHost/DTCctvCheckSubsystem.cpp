#include "DTCctvCheckSubsystem.h"

#include "DTCctvCheckWidget.h"
#include "DTCctvStream.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "JsonObjectConverter.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDTCctvCheck, Log, All);

namespace
{
FString CsvText(FString Text)
{
	Text = DTCctvMaskUrlCredentials(Text);
	Text.ReplaceInline(TEXT("\r"), TEXT(" "));
	Text.ReplaceInline(TEXT("\n"), TEXT(" "));
	Text.ReplaceInline(TEXT("\""), TEXT("\"\""));
	return TEXT("\"") + Text + TEXT("\"");
}

FString MarkdownText(FString Text)
{
	Text.ReplaceInline(TEXT("|"), TEXT("/"));
	Text.ReplaceInline(TEXT("\n"), TEXT(" "));
	Text.ReplaceInline(TEXT("\r"), TEXT(" "));
	return DTCctvMaskUrlCredentials(Text);
}
} // namespace

void UDTCctvCheckSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Legacy;
	const TCHAR* Command = FCommandLine::Get();
	if (FParse::Value(Command, TEXT("CctvUrl="), Legacy) || FParse::Value(Command, TEXT("CctvUrlEnv="), Legacy))
	{
		return;
	}
	FString List;
	bAutomatic = FParse::Value(Command, TEXT("CctvCheckList="), List);
	bAutomaticView = FParse::Param(Command, TEXT("CctvView"));
	bDisplayAvailable = FApp::CanEverRender() && !FParse::Param(Command, TEXT("nullrhi"));
	if (!bAutomatic && !bDisplayAvailable)
	{
		return;
	}
	bEnabled = true;
	FParse::Value(Command, TEXT("CctvCheckSeconds="), DurationSeconds);
	if (bAutomatic && !DTCctvLoadCheckList(List, Endpoints, StartupError))
	{
		Message = StartupError;
	}
}

void UDTCctvCheckSubsystem::Deinitialize()
{
	bAutomatic = false;
	if (bViewing)
	{
		StopView();
	}
	if (bRunning)
	{
		FinishCheck(true);
	}
	if (bCapturePending)
	{
		ScreenshotStatus = TEXT("NOT_RUN");
		bCapturePending = false;
		WriteReports();
	}
	CloseStreams();
	if (Panel.IsValid() && GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Panel.ToSharedRef());
	}
	Panel.Reset();
	bEnabled = false;
	Super::Deinitialize();
}

bool UDTCctvCheckSubsystem::AddEndpoint(const FDTCctvCheckEndpoint& Endpoint)
{
	if (IsRunning())
	{
		return false;
	}
	if (Endpoints.Num() >= 16)
	{
		Message = TEXT("주소는 최대 16개입니다.");
		return false;
	}
	if (Endpoint.Url.TrimStartAndEnd().IsEmpty())
	{
		Message = TEXT("RTSP 주소를 입력하세요.");
		return false;
	}
	Channels.Reset();
	Streams.Reset();
	bCompleted = false;
	Endpoints.Add(Endpoint);
	Message = FString::Printf(TEXT("주소 %d개 준비"), Endpoints.Num());
	return true;
}

void UDTCctvCheckSubsystem::RemoveEndpoint(int32 Index)
{
	if (!IsRunning() && Endpoints.IsValidIndex(Index))
	{
		Channels.Reset();
		Streams.Reset();
		bCompleted = false;
		Endpoints.RemoveAt(Index);
		Message = FString::Printf(TEXT("주소 %d개 준비"), Endpoints.Num());
	}
}

bool UDTCctvCheckSubsystem::PrepareReportDirectory()
{
	const FString Identifier = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") +
							   FString::FromInt(FPlatformProcess::GetCurrentProcessId()) + TEXT("-") +
							   FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6);
	ReportDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("CctvCheck") / Identifier);
	return IFileManager::Get().MakeDirectory(*ReportDirectory, true);
}

bool UDTCctvCheckSubsystem::StartCheck(double Seconds)
{
	if (IsRunning())
	{
		return false;
	}
	if (Seconds <= 10.0 || !FMath::IsFinite(Seconds))
	{
		Message = TEXT("점검 시간은 초기 10초보다 커야 합니다.");
		return false;
	}
	if (Endpoints.IsEmpty() && StartupError.IsEmpty())
	{
		Message = TEXT("주소를 먼저 추가하세요.");
		return false;
	}
	if (!PrepareReportDirectory())
	{
		Message = TEXT("보고서 디렉터리를 만들 수 없습니다.");
		return false;
	}
	CloseStreams();
	Channels.Reset();
	Streams.Reset();
	DurationSeconds = Seconds;
	StartSeconds = FPlatformTime::Seconds();
	EndSeconds = StartSeconds;
	WarmupSeconds = 0.0;
	NextCsv = 0.0;
	NextSnapshot = 0.0;
	bWarmupDone = false;
	bWriteFailed = false;
	bCompleted = false;
	bStarted = true;
	ScreenshotStatus = bDisplayAvailable ? TEXT("NOT_RUN") : TEXT("ENV_BLOCKED");
	OpenChannels(true);
	bRunning = true;
	Message = TEXT("점검 중입니다. 초기 10초 뒤의 표시 FPS를 판정합니다.");
	if (Panel.IsValid())
	{
		Panel->Refresh();
	}
	return true;
}

bool UDTCctvCheckSubsystem::StartView()
{
	if (!CanStartView())
	{
		if (!IsRunning())
		{
			Message = TEXT("주소를 먼저 추가하세요.");
		}
		return false;
	}
	if (!StartupError.IsEmpty())
	{
		Message = StartupError;
		return false;
	}
	CloseStreams();
	Channels.Reset();
	Streams.Reset();
	ReportDirectory.Reset();
	bCompleted = false;
	bStarted = true;
	StartSeconds = FPlatformTime::Seconds();
	NextSnapshot = 0.0;
	NextViewLog = 0.0;
	OpenChannels(false);
	bViewing = true;
	Message = TEXT("보기 중: 시간 제한·판정·보고서 없음. 주소 편집·시작은 중지 후 사용할 수 있습니다.");
	UE_LOG(LogDTCctvCheck, Log, TEXT("CctvView started channels=%d. 시간 제한·판정·보고서 없음."), Channels.Num());
	if (Panel.IsValid())
	{
		Panel->Refresh();
	}
	return true;
}

void UDTCctvCheckSubsystem::StopView()
{
	if (!bViewing)
	{
		return;
	}
	UE_LOG(LogDTCctvCheck, Log, TEXT("CctvView stopped elapsed=%.3f channels=%d"), FPlatformTime::Seconds() - StartSeconds, Channels.Num());
	bViewing = false;
	CloseStreams();
	Streams.Reset();
	Channels.Reset();
	Message = TEXT("보기를 중지했습니다. 주소를 편집한 뒤 다시 보기를 누르세요. 보고서는 만들지 않았습니다.");
	if (Panel.IsValid())
	{
		Panel->Refresh();
	}
}

void UDTCctvCheckSubsystem::OpenChannels(bool bWriteCsv)
{

	for (const FDTCctvCheckEndpoint& Endpoint : Endpoints)
	{
		FDTCctvCheckChannel& Channel = Channels.AddDefaulted_GetRef();
		Channel.Endpoint = Endpoint;
		Channel.Observation.bDisplayAvailable = bDisplayAvailable;
		FString Url;
		FString Sub;
		FString Error;
		UDTCctvStream* Stream = nullptr;
		const bool bMainValid = DTCctvCheckComposeUrl(Endpoint.Url, Endpoint.Username, Endpoint.Password, Url, Error);
		const bool bSubValid =
			Endpoint.SubUrl.IsEmpty() || DTCctvCheckComposeUrl(Endpoint.SubUrl, Endpoint.Username, Endpoint.Password, Sub, Error);
		if (bMainValid && bSubValid)
		{
			Stream = NewObject<UDTCctvStream>(this);
			FDTCctvStreamConfig Config;
			Config.Url = Url;
			Config.SubUrl = Sub;
			Config.Transport = Endpoint.Transport;
			Config.DecodeMode = Endpoint.bSoftware || FParse::Param(FCommandLine::Get(), TEXT("CctvSoftware"))
									? EDTCctvDecodeMode::SoftwareOnly
									: EDTCctvDecodeMode::Auto;
			Stream->ReportViewSize(FVector2D(640, 360));
			Stream->Open(Config);
		}
		else
		{
			Channel.Observation.Error = Error;
		}
		Streams.Add(Stream);
		if (bWriteCsv)
		{
			Channel.CsvPath = ReportDirectory / FString::Printf(TEXT("channel-%02d.csv"), Channels.Num());
			const FString Header = TEXT("t,state,decoded,displayed,decode_fps,display_fps,reconnects,dropped,errors,superseded,frame_age_"
									"latest_ms,frame_age_p95_ms,last_error,queued_packets,peak_queued_packets,read_calls,read_wait_seconds,read_wait_max_seconds,read_gap_max_seconds,packet_push_seconds,packet_push_max_seconds\n");
			bWriteFailed |= !FFileHelper::SaveStringToFile(Header, *Channel.CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
	}
}

void UDTCctvCheckSubsystem::CloseStreams()
{
	for (UDTCctvStream* Stream : Streams)
	{
		if (IsValid(Stream))
		{
			Stream->Close();
		}
	}
}

void UDTCctvCheckSubsystem::StopCheck()
{
	if (bViewing)
	{
		StopView();
		return;
	}
	if (bRunning)
	{
		FinishCheck(true);
	}
}

bool UDTCctvCheckSubsystem::SaveAddresses()
{
	if (Endpoints.IsEmpty())
	{
		Message = TEXT("저장할 주소를 먼저 추가하세요.");
		return false;
	}
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("CctvCheck"));
	IFileManager::Get().MakeDirectory(*Folder, true);
	const FString File = Folder / TEXT("addresses.json");
	const bool bSaved = DTCctvSaveCheckList(File, Endpoints);
	Message = bSaved ? TEXT("계정을 제외한 주소 저장 완료. ") + File : TEXT("주소 저장에 실패했습니다.");
	return bSaved;
}

UDTCctvStream* UDTCctvCheckSubsystem::GetChannelStream(int32 Index) const
{
	return Streams.IsValidIndex(Index) ? Streams[Index].Get() : nullptr;
}

FText UDTCctvCheckSubsystem::GetChannelOverlay(int32 Index) const
{
	return Channels.IsValidIndex(Index) ? Channels[Index].Overlay : FText::GetEmpty();
}

FText UDTCctvCheckSubsystem::GetSummaryText() const
{
	return FText::FromString(Message);
}

void UDTCctvCheckSubsystem::ReportTileSize(int32 Index, FVector2D Size)
{
	if (UDTCctvStream* Stream = GetChannelStream(Index))
	{
		Stream->ReportViewSize(Size);
	}
}

FString UDTCctvCheckSubsystem::StateName(EDTCctvState State) const
{
	switch (State)
	{
	case EDTCctvState::Connecting:
		return TEXT("연결 중");
	case EDTCctvState::Playing:
		return TEXT("재생");
	case EDTCctvState::Reconnecting:
		return TEXT("재연결");
	default:
		return TEXT("대기");
	}
}

void UDTCctvCheckSubsystem::SnapshotChannels(double Elapsed, bool bReadStats)
{
	for (int32 Index = 0; Index < Channels.Num(); ++Index)
	{
		FDTCctvCheckChannel& Channel = Channels[Index];
		if (UDTCctvStream* Stream = bReadStats ? GetChannelStream(Index) : nullptr)
		{
			Channel.Observation.Current = Stream->GetStats();
			Channel.Observation.Info = Stream->GetStreamInfo();
			if (!Channel.Observation.Current.LastError.IsEmpty())
			{
				Channel.Observation.Error = Channel.Observation.Current.LastError;
			}
		}
		const FDTCctvStreamStats& Stats = Channel.Observation.Current;
		const FDTCctvStreamInfo& Info = Channel.Observation.Info;
		const FString Verdict = bCompleted ? DTCctvCheckVerdictName(Channel.Result.Verdict) + TEXT(" ") : FString();
		Channel.Overlay = FText::FromString(
			FString::Printf(TEXT("#%02d %s%s | %s %s %dx%d\n원본 %.2f | 디코드 %.2f 표시 %.2f | %s\n재연결 %d 손실 %lld 오류 %lld 대체 "
								 "%lld\nage %.1fms p95 %.1fms\n최근 오류 %s"),
							Index + 1, *Verdict, *StateName(Stats.State), *Info.CodecName, *Info.Profile, Stats.SourceSize.X,
							Stats.SourceSize.Y, Info.DeclaredFps, Stats.DecodeFps, Stats.DisplayFps, *Info.DecoderName, Stats.Reconnects,
							Stats.PacketsDropped, Stats.DecodeErrors, Stats.FramesSuperseded, Stats.FrameAgeLatestMs, Stats.FrameAgeP95Ms,
							*(Channel.Observation.Error.IsEmpty() ? FString(TEXT("없음")) : Channel.Observation.Error.Left(160))));
	}
}

void UDTCctvCheckSubsystem::WriteSamples(double Elapsed)
{
	for (FDTCctvCheckChannel& Channel : Channels)
	{
		const FDTCctvStreamStats& Stats = Channel.Observation.Current;
		const FString Row = FString::Printf(
			TEXT("%.3f,%d,%lld,%lld,%.3f,%.3f,%d,%lld,%lld,%lld,%.3f,%.3f,%s,%d,%d,%lld,%.6f,%.6f,%.6f,%.6f,%.6f\n"), Elapsed, static_cast<int32>(Stats.State),
			Stats.FramesDecoded, Stats.FramesDisplayed, Stats.DecodeFps, Stats.DisplayFps, Stats.Reconnects, Stats.PacketsDropped,
			Stats.DecodeErrors, Stats.FramesSuperseded, Stats.FrameAgeLatestMs, Stats.FrameAgeP95Ms, *CsvText(Channel.Observation.Error),
			Stats.QueuedPackets, Stats.PeakQueuedPackets, Stats.ReadCalls, Stats.ReadWaitSeconds, Stats.ReadWaitMaxSeconds,
			Stats.ReadGapMaxSeconds, Stats.PacketPushSeconds, Stats.PacketPushMaxSeconds);
		bWriteFailed |= !FFileHelper::SaveStringToFile(Row, *Channel.CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
													   &IFileManager::Get(), FILEWRITE_Append);
	}
}

void UDTCctvCheckSubsystem::FinishCheck(bool bInterrupted)
{
	EndSeconds = FPlatformTime::Seconds();
	SnapshotChannels(EndSeconds - StartSeconds);
	WriteSamples(EndSeconds - StartSeconds);
	int32 Pass = 0;
	int32 Warn = 0;
	int32 Fail = 0;
	for (FDTCctvCheckChannel& Channel : Channels)
	{
		Channel.Observation.WindowSeconds = bWarmupDone ? EndSeconds - WarmupSeconds : 0.0;
		Channel.Observation.bInterrupted |= bInterrupted;
		Channel.Result = DTCctvEvaluateCheck(Channel.Observation);
		Pass += Channel.Result.Verdict == EDTCctvCheckVerdict::Pass ? 1 : 0;
		Warn += Channel.Result.Verdict == EDTCctvCheckVerdict::Warn ? 1 : 0;
		Fail += Channel.Result.Verdict == EDTCctvCheckVerdict::Fail ? 1 : 0;
	}
	bRunning = false;
	bCompleted = true;
	Message = FString::Printf(TEXT("점검 완료. PASS %d / WARN %d / FAIL %d. 보고서 %s"), Pass, Warn, Fail, *ReportDirectory);
	if (!StartupError.IsEmpty())
	{
		Message = StartupError;
	}
	SnapshotChannels(EndSeconds - StartSeconds, false);
	if (Panel.IsValid())
	{
		Panel->Refresh();
	}
	bCapturePending = bDisplayAvailable;
	CaptureSince = FPlatformTime::Seconds();
	bCaptureRequested = false;
	if (!bCapturePending)
	{
		FinalizeReport();
	}
}

void UDTCctvCheckSubsystem::FinalizeReport()
{
	bCapturePending = false;
	WriteReports();
	CloseStreams();
	if (bAutomatic)
	{
		uint8 Code = StartupError.IsEmpty() && !bWriteFailed ? 0 : 1;
		for (const FDTCctvCheckChannel& Channel : Channels)
		{
			if (Channel.Result.Verdict == EDTCctvCheckVerdict::Fail)
			{
				Code = 1;
				break;
			}
			if (Code == 0 && Channel.Result.Verdict == EDTCctvCheckVerdict::Warn)
			{
				Code = 2;
			}
		}
		FPlatformMisc::RequestExitWithStatus(false, Code);
	}
}

void UDTCctvCheckSubsystem::WriteReports()
{
	if (ReportDirectory.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	Root->SetStringField(TEXT("tool"), TEXT("DTCctvCheck"));
	Root->SetNumberField(TEXT("process_id"), FPlatformProcess::GetCurrentProcessId());
	Root->SetNumberField(TEXT("duration_seconds"), EndSeconds - StartSeconds);
	Root->SetNumberField(TEXT("warmup_seconds"), 10);
	Root->SetBoolField(TEXT("display_available"), bDisplayAvailable);
	Root->SetStringField(TEXT("screenshot_status"), ScreenshotStatus);
	Root->SetStringField(TEXT("startup_error"), StartupError);
	FString Overall = StartupError.IsEmpty() && !bWriteFailed ? TEXT("PASS") : TEXT("FAIL");
	TArray<TSharedPtr<FJsonValue>> Values;
	FString Markdown = TEXT("# CCTV 연결 점검\n\n초기 10초 이후 FPS를 판정합니다. 표시 FPS와 frame age는 렌더 명령 등록 기준입니다.\n\n| "
							"채널 | 주소 | 판정 | 디코드/표시 FPS | 원인 |\n|---|---|---|---|---|\n");
	for (int32 Index = 0; Index < Channels.Num(); ++Index)
	{
		const FDTCctvCheckChannel& Channel = Channels[Index];
		const FDTCctvCheckObservation& Observation = Channel.Observation;
		const FDTCctvCheckResult& Result = Channel.Result;
		const FString Verdict = DTCctvCheckVerdictName(Result.Verdict);
		if (Verdict == TEXT("FAIL"))
		{
			Overall = TEXT("FAIL");
		}
		else if (Verdict == TEXT("WARN") && Overall == TEXT("PASS"))
		{
			Overall = TEXT("WARN");
		}
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("channel"), Index + 1);
		Object->SetStringField(TEXT("url"), DTCctvCheckSafeUrl(Channel.Endpoint.Url));
		Object->SetStringField(TEXT("sub_url"),
							   Channel.Endpoint.SubUrl.IsEmpty() ? FString() : DTCctvCheckSafeUrl(Channel.Endpoint.SubUrl));
		Object->SetStringField(TEXT("verdict"), Verdict);
		Object->SetNumberField(TEXT("decode_fps"), Result.DecodeFps);
		Object->SetNumberField(TEXT("display_fps"), Result.DisplayFps);
		Object->SetNumberField(TEXT("original_fps"), Observation.Info.DeclaredFps);
		Object->SetNumberField(TEXT("first_frame_seconds"), Observation.FirstFrameSeconds);
		Object->SetNumberField(TEXT("reconnects"), Observation.Current.Reconnects);
		Object->SetNumberField(TEXT("packets_dropped"), Observation.Current.PacketsDropped);
		Object->SetNumberField(TEXT("decode_errors"), Observation.Current.DecodeErrors);
		Object->SetNumberField(TEXT("frames_superseded"), Observation.Current.FramesSuperseded);
		Object->SetNumberField(TEXT("frame_age_p95_ms"), Observation.Current.FrameAgeP95Ms);
		Object->SetStringField(TEXT("last_error"), DTCctvMaskUrlCredentials(Observation.Error));
		Object->SetBoolField(TEXT("interrupted"), Observation.bInterrupted);
		TArray<TSharedPtr<FJsonValue>> Reasons;
		for (const FString& Reason : Result.Reasons)
		{
			Reasons.Add(MakeShared<FJsonValueString>(DTCctvMaskUrlCredentials(Reason)));
		}
		Object->SetArrayField(TEXT("reasons"), Reasons);
		TSharedRef<FJsonObject> Information = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(FDTCctvStreamInfo::StaticStruct(), &Observation.Info, Information, 0, 0);
		Object->SetObjectField(TEXT("stream_info"), Information);
		Values.Add(MakeShared<FJsonValueObject>(Object));
		Markdown += FString::Printf(TEXT("| %d | %s | %s | %.3f / %.3f | %s |\n"), Index + 1,
									*MarkdownText(DTCctvCheckSafeUrl(Channel.Endpoint.Url)), *Verdict, Result.DecodeFps, Result.DisplayFps,
									*MarkdownText(FString::Join(Result.Reasons, TEXT(" / "))));
	}
	Root->SetStringField(TEXT("verdict"), Overall);
	Root->SetArrayField(TEXT("channels"), Values);
	Markdown += TEXT("\n전체 판정 ") + Overall + TEXT("\n\n스크린샷 ") + ScreenshotStatus + TEXT("\n");
	if (!StartupError.IsEmpty())
	{
		Markdown += TEXT("\n") + StartupError + TEXT("\n");
	}
	FString Json;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
	bWriteFailed |=
		!FFileHelper::SaveStringToFile(Json, *(ReportDirectory / TEXT("report.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	bWriteFailed |= !FFileHelper::SaveStringToFile(Markdown, *(ReportDirectory / TEXT("report.md")),
												   FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogDTCctvCheck, Log, TEXT("점검 결과 %s. 보고서 %s"), *Overall, *ReportDirectory);
}

void UDTCctvCheckSubsystem::BuildUI()
{
	if (!Panel.IsValid() && bDisplayAvailable && GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		SAssignNew(Panel, SDTCctvCheckPanel).Checker(this);
		GEngine->GameViewport->AddViewportWidgetContent(Panel.ToSharedRef(), 10000);
	}
	if (Panel.IsValid() && !bInputModeApplied)
	{
		if (APlayerController* Controller = GetGameInstance()->GetFirstLocalPlayerController())
		{
			FInputModeUIOnly Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Controller->SetInputMode(Mode);
			Controller->bShowMouseCursor = true;
			bInputModeApplied = true;
		}
	}
}

void UDTCctvCheckSubsystem::Tick(float DeltaTime)
{
	BuildUI();
	if (bAutomatic && !bStarted)
	{
		if (!(bAutomaticView ? StartView() : StartCheck(DurationSeconds)))
		{
			StartupError = Message;
			bStarted = true;
			bCompleted = true;
			StartSeconds = EndSeconds = FPlatformTime::Seconds();
			if (!bAutomaticView && PrepareReportDirectory())
			{
				WriteReports();
			}
			UE_LOG(LogDTCctvCheck, Error, TEXT("%s"), *StartupError);
			FPlatformMisc::RequestExitWithStatus(false, 1);
		}
	}
	const double Now = FPlatformTime::Seconds();
	if (bViewing)
	{
		const double Elapsed = Now - StartSeconds;
		if (Elapsed >= NextSnapshot)
		{
			SnapshotChannels(Elapsed);
			NextSnapshot = Elapsed + .25;
		}
		if (Elapsed >= NextViewLog)
		{
			for (int32 Index = 0; Index < Channels.Num(); ++Index)
			{
				const FDTCctvStreamStats& Stats = Channels[Index].Observation.Current;
				UE_LOG(LogDTCctvCheck, Log,
					TEXT("CctvViewSample t=%.3f channel=%d state=%d decoded=%lld displayed=%lld decode_fps=%.3f display_fps=%.3f decoder=%s reconnects=%d dropped=%lld errors=%lld"),
					Elapsed, Index + 1, static_cast<int32>(Stats.State), Stats.FramesDecoded, Stats.FramesDisplayed,
					Stats.DecodeFps, Stats.DisplayFps, *Channels[Index].Observation.Info.DecoderName, Stats.Reconnects,
					Stats.PacketsDropped, Stats.DecodeErrors);
			}
			NextViewLog = Elapsed + 5.0;
		}
		return;
	}
	if (bRunning)
	{
		const double Elapsed = Now - StartSeconds;
		for (int32 Index = 0; Index < Channels.Num(); ++Index)
		{
			FDTCctvCheckObservation& Observation = Channels[Index].Observation;
			UDTCctvStream* Stream = GetChannelStream(Index);
			const bool bPlaying = Stream != nullptr && Stream->GetState() == EDTCctvState::Playing;
			if (bPlaying && !Observation.bEverPlayed)
			{
				Observation.bEverPlayed = true;
				Observation.FirstFrameSeconds = Elapsed;
			}
			if (Elapsed >= 10.0 && !bPlaying)
			{
				Observation.bPlayingAfterWarmup = false;
				Observation.bInterrupted |= Observation.bEverPlayed;
			}
		}
		if (Elapsed >= NextSnapshot)
		{
			SnapshotChannels(Elapsed);
			NextSnapshot = Elapsed + .25;
		}
		if (!bWarmupDone && Elapsed >= 10.0)
		{
			SnapshotChannels(Elapsed);
			WarmupSeconds = Now;
			for (int32 Index = 0; Index < Channels.Num(); ++Index)
			{
				Channels[Index].Observation.Warmup = Channels[Index].Observation.Current;
				if (UDTCctvStream* Stream = GetChannelStream(Index))
				{
					Stream->ResetFrameAgeStats();
				}
			}
			bWarmupDone = true;
		}
		if (Elapsed >= NextCsv)
		{
			WriteSamples(Elapsed);
			NextCsv = Elapsed + 1.0;
		}
		if (Elapsed >= DurationSeconds || !StartupError.IsEmpty())
		{
			FinishCheck(false);
		}
	}
	if (bCapturePending)
	{
		const FString Screenshot = ReportDirectory / TEXT("screenshot.png");
		if (!bCaptureRequested && Now - CaptureSince >= .25)
		{
			FScreenshotRequest::RequestScreenshot(Screenshot, true, false);
			bCaptureRequested = true;
		}
		if (bCaptureRequested && IFileManager::Get().FileSize(*Screenshot) > 0)
		{
			ScreenshotStatus = TEXT("PASS");
			FinalizeReport();
		}
		else if (Now - CaptureSince > 5.0)
		{
			ScreenshotStatus = TEXT("ENV_BLOCKED");
			FinalizeReport();
		}
	}
}

const FDTCctvCheckResult* UDTCctvCheckSubsystem::GetChannelResult(int32 Index) const
{
	return Channels.IsValidIndex(Index) ? &Channels[Index].Result : nullptr;
}
