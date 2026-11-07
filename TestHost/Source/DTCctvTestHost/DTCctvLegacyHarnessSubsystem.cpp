#include "DTCctvLegacyHarnessSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "DTCctvHarnessSubsystem.h"
#include "DTCctvStreamInfo.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "MediaPlayer.h"
#include "MediaPlayerFacade.h"
#include "MediaSampleQueue.h"
#include "MediaTexture.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogDTCctvLegacyHarness, Log, All);

struct FDTCctvLegacyChannel
{
	TSharedPtr<FMediaTextureSampleQueue, ESPMode::ThreadSafe> Samples;
	bool bOpenAccepted = false;
	bool bHasSample = false;
	int64 Frames = 0;
	int64 TimestampChanges = 0;
	int64 LastTicks = 0;
	FIntPoint Dimensions = FIntPoint::ZeroValue;
};

void UDTCctvLegacyHarnessSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TCHAR* Command = FCommandLine::Get();
	bLegacyOnly = FParse::Param(Command, TEXT("CctvLegacyOnly"));
	FParse::Value(Command, TEXT("CctvSeconds="), DurationSeconds);
	FParse::Value(Command, TEXT("CctvLegacyCount="), Count);
	Count = FMath::Clamp(Count, 0, 4);
	bEnabled = Count > 0;
	if (!bEnabled)
	{
		return;
	}
	FString Environment;
	if (FParse::Value(Command, TEXT("CctvUrlEnv="), Environment))
	{
		Url = FPlatformMisc::GetEnvironmentVariable(*Environment);
	}
	else
	{
		FParse::Value(Command, TEXT("CctvUrl="), Url);
	}
	FString MainCsv;
	FParse::Value(Command, TEXT("CctvCsv="), MainCsv);
	CsvPath = FPaths::GetPath(MainCsv) / TEXT("legacy.samples.csv");
}

void UDTCctvLegacyHarnessSubsystem::StartPlayers()
{
	if (FApp::CanEverRender())
	{
		RootWidget = CreateWidget<UDTCctvHarnessRootWidget>(GetGameInstance());
	}
	UCanvasPanel* Canvas = RootWidget != nullptr ? RootWidget->WidgetTree->ConstructWidget<UCanvasPanel>() : nullptr;
	if (Canvas != nullptr)
	{
		RootWidget->WidgetTree->RootWidget = Canvas;
		RootWidget->AddToViewport(10);
	}
	FFileHelper::SaveStringToFile(TEXT("t,channel,player,is_playing,has_error,playback_seconds,frames,timestamp_changes,last_sample_seconds,width,height,open_accepted,phase\n"), *CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UMediaPlayer* Player = NewObject<UMediaPlayer>(this);
		Players.Add(Player);
		Player->PlayOnOpen = true;
		Player->SetDesiredPlayerName(FName(TEXT("FFMPEGMedia")));
		Player->SetLooping(true);
		TSharedPtr<FDTCctvLegacyChannel> Channel = MakeShared<FDTCctvLegacyChannel>();
		Channel->Samples = MakeShared<FMediaTextureSampleQueue, ESPMode::ThreadSafe>(8);
		Player->GetPlayerFacade()->AddVideoSampleSink(Channel->Samples.ToSharedRef());
		Channels.Add(Channel);
		if (Canvas != nullptr)
		{
			UMediaTexture* Texture = NewObject<UMediaTexture>(this);
			Textures.Add(Texture);
			Texture->SetMediaPlayer(Player);
			Texture->UpdateResource();
			UImage* Image = RootWidget->WidgetTree->ConstructWidget<UImage>();
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2f(480.f, 270.f);
			Image->SetBrush(Brush);
			UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Image);
			Slot->SetAnchors(FAnchors(Index / 4.f, 0.75f, (Index + 1) / 4.f, 1.f));
			if (bLegacyOnly)
			{
				Slot->SetAnchors(FAnchors((Index % 2) / 2.f, (Index / 2) / 2.f, (Index % 2 + 1) / 2.f, (Index / 2 + 1) / 2.f));
			}
			Slot->SetOffsets(FMargin(0.f));
		}
		Channel->bOpenAccepted = Player->OpenUrl(Url);
		UE_LOG(LogDTCctvLegacyHarness, Log, TEXT("기존 플레이어 채널=%d 요청=%d 주소=%s"), Index, Channel->bOpenAccepted, *DTCctvMaskUrlCredentials(Url));
	}
	StartSeconds = FPlatformTime::Seconds();
	bStarted = true;
}

void UDTCctvLegacyHarnessSubsystem::WriteSamples(double Elapsed, bool bShutdown)
{
	for (int32 Index = 0; Index < Channels.Num(); ++Index)
	{
		const FDTCctvLegacyChannel& Channel = *Channels[Index];
		const UMediaPlayer* Player = Players[Index];
		const FString Line = FString::Printf(TEXT("%.3f,%d,%s,%d,%d,%.6f,%lld,%lld,%.6f,%d,%d,%d,%s\n"),
			Elapsed, Index, *Player->GetPlayerName().ToString(), Player->IsPlaying(), Player->HasError(), Player->GetTime().GetTotalSeconds(),
			Channel.Frames, Channel.TimestampChanges, FTimespan(Channel.LastTicks).GetTotalSeconds(), Channel.Dimensions.X, Channel.Dimensions.Y, Channel.bOpenAccepted, bShutdown ? TEXT("shutdown") : TEXT("running"));
		FFileHelper::SaveStringToFile(Line, *CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}
}

void UDTCctvLegacyHarnessSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!bStarted)
	{
		if (World != nullptr && World->HasBegunPlay() && World->GetFirstPlayerController() != nullptr)
		{
			StartPlayers();
		}
		return;
	}
	for (const TSharedPtr<FDTCctvLegacyChannel>& Channel : Channels)
	{
		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;
		while (Channel->Samples->Dequeue(Sample))
		{
			const int64 Ticks = Sample->GetTime().Time.GetTicks();
			Channel->TimestampChanges += Channel->bHasSample && Ticks != Channel->LastTicks ? 1 : 0;
			Channel->bHasSample = true;
			Channel->LastTicks = Ticks;
			Channel->Dimensions = Sample->GetDim();
			++Channel->Frames;
		}
	}
	const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
	if (Elapsed - LastSampleSeconds >= 1.0)
	{
		WriteSamples(Elapsed);
		LastSampleSeconds = Elapsed;
	}
	if (bLegacyOnly && Elapsed >= DurationSeconds)
	{
		WriteSamples(Elapsed);
		bFinalSampleWritten = true;
		bEnabled = false;
		FPlatformMisc::RequestExit(false);
	}
}

void UDTCctvLegacyHarnessSubsystem::Deinitialize()
{
	if (bStarted && !bFinalSampleWritten)
	{
		WriteSamples(FPlatformTime::Seconds() - StartSeconds, true);
	}
	bEnabled = false;
	for (UMediaPlayer* Player : Players)
	{
		Player->Close();
	}
	if (RootWidget != nullptr)
	{
		RootWidget->RemoveFromParent();
	}
	Channels.Reset();
	Players.Reset();
	Textures.Reset();
	RootWidget = nullptr;
	Super::Deinitialize();
}
