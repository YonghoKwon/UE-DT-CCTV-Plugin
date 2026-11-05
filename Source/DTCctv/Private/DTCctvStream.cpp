#include "DTCctvStream.h"

#include "DTCctvLog.h"
#include "DTCctvPolicies.h"
#include "DTCctvSession.h"
#include "DTCctvSettings.h"
#include "DTCctvYuvRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace
{
	/** 창 크기를 끄는 동안 렌더 타깃을 반복해서 다시 만들지 않기 위한 대기 시간. */
	constexpr double ResizeHoldSeconds = 0.25;
	/** 경계 근처에서 메인/서브가 반복 전환되지 않기 위한 대기 시간. */
	constexpr double SubSwitchHoldSeconds = 2.0;
	constexpr double ResizeThreshold = 0.1;
	constexpr double StatsWindowSeconds = 1.0;
	constexpr int32 MaxRenderCommandsInFlight = 3;

	FDTCctvSessionParams MakeParams(const FDTCctvStreamConfig& Config, const FString& Url)
	{
		const UDTCctvSettings* Settings = GetDefault<UDTCctvSettings>();
		FDTCctvSessionParams Params;
		Params.Url = Url;
		Params.Transport = Config.Transport;
		Params.DecodeMode = Config.DecodeMode;
		Params.ConnectTimeoutSeconds = Config.ConnectTimeoutSeconds;
		Params.ReadTimeoutSeconds = Config.ReadTimeoutSeconds;
		Params.MinReconnectDelaySeconds = Config.MinReconnectDelaySeconds;
		Params.MaxReconnectDelaySeconds = Config.MaxReconnectDelaySeconds;
		Params.MaxConcurrentConnects = Settings->MaxConcurrentConnects;
		Params.MaxQueuedPackets = Settings->MaxQueuedPackets;
		Params.MaxHardwareDecodeStreams = Settings->MaxHardwareDecodeStreams;
		Params.SoftwareDecodeThreads = Settings->SoftwareDecodeThreads;
		return Params;
	}

	void AddSessionTotals(FDTCctvStreamStats& Total, const FDTCctvSession& Session)
	{
		FDTCctvStreamStats Stats;
		Session.FillStats(Stats);
		Total.PacketsReceived += Stats.PacketsReceived;
		Total.FramesDecoded += Stats.FramesDecoded;
		Total.FramesSuperseded += Stats.FramesSuperseded;
		Total.PacketsDropped += Stats.PacketsDropped;
		Total.DecodeErrors += Stats.DecodeErrors;
		Total.Reconnects += Stats.Reconnects;
		Total.PacketKeyWaits += Stats.PacketKeyWaits;
		Total.PacketKeyWaitSeconds += Stats.PacketKeyWaitSeconds;
		Total.PeakQueuedPackets = FMath::Max(Total.PeakQueuedPackets, Stats.PeakQueuedPackets);
		Total.PeakQueuedFrames = FMath::Max(Total.PeakQueuedFrames, Stats.PeakQueuedFrames);
		Total.PeakQueuedFrameBytes = FMath::Max(Total.PeakQueuedFrameBytes, Stats.PeakQueuedFrameBytes);
		DTCctvMergeFrameAgeStats(Total, Stats);
	}
}

void UDTCctvStream::Open(const FDTCctvStreamConfig& InConfig)
{
	Close();

	Config = InConfig;
	Accumulated = FDTCctvStreamStats();
	FramesDisplayed = 0;
	StatsWindowStart = 0.0;
	SmoothedTickSeconds = 0.0;
	StatsWindowDecoded = 0;
	StatsWindowDisplayed = 0;
	DecodeFps = 0.f;
	DisplayFps = 0.f;
	SourceSize = FIntPoint::ZeroValue;
	OutputSize = FIntPoint::ZeroValue;
	ResizeWantedSince = 0.0;
	SubSwitchWantedSince = 0.0;
	bManualSubStream = false;
	bUsingSubStream = false;

	if (Config.Url.IsEmpty())
	{
		UE_LOG(LogDTCctv, Warning, TEXT("스트림 주소가 비어 있습니다."));
		return;
	}

	CreateTextureIfNeeded();
	StartSession(false);
}

void UDTCctvStream::Close()
{
	if (Session.IsValid())
	{
		AddSessionTotals(Accumulated, *Session);
		FDTCctvSession::Retire(MoveTemp(Session));
		Session.Reset();
	}
	if (PendingSession.IsValid())
	{
		FDTCctvSession::Retire(MoveTemp(PendingSession));
		PendingSession.Reset();
	}
	if (LastState != EDTCctvState::Idle)
	{
		LastState = EDTCctvState::Idle;
		OnStateChanged.Broadcast(this, LastState);
	}
}

bool UDTCctvStream::IsOpen() const
{
	return Session.IsValid();
}

UTexture* UDTCctvStream::GetTexture() const
{
	return Texture;
}

void UDTCctvStream::StartSession(bool bSubStream)
{
	const FString& Url = bSubStream ? Config.SubUrl : Config.Url;
	TSharedPtr<FDTCctvSession, ESPMode::ThreadSafe> NewSession =
		MakeShared<FDTCctvSession, ESPMode::ThreadSafe>(MakeParams(Config, Url));
	if (ViewSize.X > 0.0 && ViewSize.Y > 0.0)
	{
		// 알고 있는 표시 크기를 먼저 전달해 첫 프레임의 원본 크기 임시 할당을 줄인다.
		NewSession->SetDesiredOutputSize(FIntPoint(FMath::Max(2, FMath::CeilToInt(ViewSize.X)), FMath::Max(2, FMath::CeilToInt(ViewSize.Y))));
	}
	NewSession->Start();

	if (Session.IsValid())
	{
		// 새 스트림의 첫 프레임이 나올 때까지 기존 영상을 계속 보여 준다.
		if (PendingSession.IsValid())
		{
			FDTCctvSession::Retire(MoveTemp(PendingSession));
		}
		PendingSession = NewSession;
	}
	else
	{
		Session = NewSession;
	}
	bUsingSubStream = bSubStream;
}

void UDTCctvStream::CreateTextureIfNeeded()
{
	if (Texture == nullptr)
	{
		Texture = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
		// 셰이더가 선형 값을 쓰면 sRGB 타깃이 감마를 적용한다. Slate와 머티리얼 모두 같은 색으로 읽는다.
		Texture->RenderTargetFormat = RTF_RGBA8_SRGB;
		Texture->ClearColor = FLinearColor::Black;
		Texture->bAutoGenerateMips = false;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->Filter = TF_Bilinear;
		Texture->InitAutoFormat(16, 9);
		Texture->UpdateResourceImmediate(true);
	}
	if (!Renderer.IsValid())
	{
		Renderer = MakeShared<FDTCctvYuvRenderer, ESPMode::ThreadSafe>();
		RenderInFlight = MakeShared<std::atomic<int32>, ESPMode::ThreadSafe>(0);
	}
}

void UDTCctvStream::SetUseSubStream(bool bUseSubStream)
{
	bManualSubStream = true;
	if (!Session.IsValid() || bUseSubStream == bUsingSubStream || (bUseSubStream && Config.SubUrl.IsEmpty()))
	{
		return;
	}
	StartSession(bUseSubStream);
}

void UDTCctvStream::ReportViewSize(FVector2D InViewSize)
{
	ViewSize = InViewSize;
}

void UDTCctvStream::UpdateSubStreamSelection()
{
	if (bManualSubStream || Config.SubUrl.IsEmpty() || ViewSize.X <= 0.0 || PendingSession.IsValid())
	{
		return;
	}

	const int32 MaxWidth = GetDefault<UDTCctvSettings>()->SubStreamMaxWidth;
	const bool bWantSub = ViewSize.X < MaxWidth;
	if (bWantSub == bUsingSubStream)
	{
		SubSwitchWantedSince = 0.0;
		return;
	}

	const double Now = FPlatformTime::Seconds();
	if (SubSwitchWantedSince == 0.0)
	{
		SubSwitchWantedSince = Now;
	}
	else if (Now - SubSwitchWantedSince >= SubSwitchHoldSeconds)
	{
		SubSwitchWantedSince = 0.0;
		StartSession(bWantSub);
	}
}

void UDTCctvStream::UpdateOutputSize(FIntPoint FrameSize)
{
	const bool bSourceChanged = FrameSize != SourceSize;
	SourceSize = FrameSize;

	const FIntPoint Wanted = DTCctvComputeOutputSize(FrameSize, ViewSize);
	bool bApply = bSourceChanged || OutputSize.X == 0;
	if (!bApply && Wanted != OutputSize)
	{
		const double Change = FMath::Abs(static_cast<double>(Wanted.X - OutputSize.X)) / OutputSize.X;
		if (Change >= ResizeThreshold)
		{
			const double Now = FPlatformTime::Seconds();
			if (ResizeWantedSince == 0.0)
			{
				ResizeWantedSince = Now;
			}
			bApply = Now - ResizeWantedSince >= ResizeHoldSeconds;
		}
		else
		{
			ResizeWantedSince = 0.0;
		}
	}

	if (bApply)
	{
		ResizeWantedSince = 0.0;
		if (Wanted != OutputSize)
		{
			OutputSize = Wanted;
			Texture->ResizeTarget(OutputSize.X, OutputSize.Y);
		}
	}
}

void UDTCctvStream::RenderLatestFrame()
{
	// 렌더 스레드는 게임 스레드보다 보통 한 프레임 늦으므로 1건 대기는 정상이다. 그 이상 밀릴 때만 건너뛴다.
	if (!Session.IsValid())
	{
		return;
	}
	if (Texture == nullptr || RenderInFlight->load() >= MaxRenderCommandsInFlight)
	{
		Session->NotifyConsumerUnavailable();
		return;
	}

	double SourceFps = Session->GetNominalFps();
	if (DecodeFps > 0.0)
	{
		SourceFps = SourceFps > 0.0 ? FMath::Min(SourceFps, static_cast<double>(DecodeFps)) : DecodeFps;
	}
	const bool bCanCatchUp = DTCctvCanCatchUp(SmoothedTickSeconds, SourceFps);
	FDTCctvVideoFramePtr Frame = Session->TakeFrame(bCanCatchUp);
	if (!Frame.IsValid())
	{
		return;
	}

	// -nullrhi처럼 렌더링이 없는 실행에서는 디코딩만 하고 텍스처를 건드리지 않는다.
	if (!FApp::CanEverRender())
	{
		return;
	}

	UpdateOutputSize(Frame->GetSourceSize());
	Session->SetDesiredOutputSize(OutputSize);
	if (PendingSession.IsValid())
	{
		PendingSession->SetDesiredOutputSize(OutputSize);
	}

	FTextureRenderTargetResource* Resource = Texture->GameThread_GetRenderTargetResource();
	if (Resource == nullptr)
	{
		Session->NotifyConsumerUnavailable();
		return;
	}

	RenderInFlight->fetch_add(1);
	++FramesDisplayed;
	Session->RecordFrameSubmitted(Frame->GetDecodedSeconds(), FPlatformTime::Seconds());

	ENQUEUE_RENDER_COMMAND(DTCctvRenderFrame)(
		[Frame, Renderer = Renderer, InFlight = RenderInFlight, Resource](FRHICommandListImmediate& RHICmdList)
		{
			const FDTCctvYuvPlanes Planes = Frame->GetPlanes();
			Renderer->Render(RHICmdList, Planes, Resource->GetRenderTargetTexture());
			InFlight->fetch_sub(1);
		});
}

void UDTCctvStream::UpdateStats()
{
	const double Now = FPlatformTime::Seconds();
	if (StatsWindowStart == 0.0)
	{
		StatsWindowStart = Now;
		return;
	}
	const double Elapsed = Now - StatsWindowStart;
	if (Elapsed < StatsWindowSeconds)
	{
		return;
	}

	const FDTCctvStreamStats Stats = GetStats();
	DecodeFps = static_cast<float>((Stats.FramesDecoded - StatsWindowDecoded) / Elapsed);
	DisplayFps = static_cast<float>((FramesDisplayed - StatsWindowDisplayed) / Elapsed);
	StatsWindowDecoded = Stats.FramesDecoded;
	StatsWindowDisplayed = FramesDisplayed;
	StatsWindowStart = Now;
}

void UDTCctvStream::Tick(float DeltaTime)
{
	const double TickSeconds = FMath::Clamp(static_cast<double>(DeltaTime), 1.0 / 240.0, 0.05);
	SmoothedTickSeconds = SmoothedTickSeconds > 0.0 ? SmoothedTickSeconds * 0.95 + TickSeconds * 0.05 : TickSeconds;
	if (!Session.IsValid())
	{
		return;
	}

	if (PendingSession.IsValid() && PendingSession->HasDecodedFrame())
	{
		AddSessionTotals(Accumulated, *Session);
		FDTCctvSession::Retire(MoveTemp(Session));
		Session = MoveTemp(PendingSession);
		PendingSession.Reset();
	}

	const EDTCctvState NewState = Session->GetState();
	if (NewState != LastState)
	{
		LastState = NewState;
		OnStateChanged.Broadcast(this, LastState);
		// 콜백에서 Close가 호출되었을 수 있다.
		if (!Session.IsValid())
		{
			return;
		}
	}

	UpdateSubStreamSelection();
	RenderLatestFrame();
	UpdateStats();
}

FDTCctvStreamStats UDTCctvStream::GetStats() const
{
	FDTCctvStreamStats Stats;
	if (Session.IsValid())
	{
		Session->FillStats(Stats);
	}
	Stats.PacketsReceived += Accumulated.PacketsReceived;
	Stats.FramesDecoded += Accumulated.FramesDecoded;
	Stats.FramesSuperseded += Accumulated.FramesSuperseded;
	Stats.PacketsDropped += Accumulated.PacketsDropped;
	Stats.DecodeErrors += Accumulated.DecodeErrors;
	Stats.Reconnects += Accumulated.Reconnects;
	Stats.PacketKeyWaits += Accumulated.PacketKeyWaits;
	Stats.PacketKeyWaitSeconds += Accumulated.PacketKeyWaitSeconds;
	Stats.PeakQueuedPackets = FMath::Max(Stats.PeakQueuedPackets, Accumulated.PeakQueuedPackets);
	Stats.PeakQueuedFrames = FMath::Max(Stats.PeakQueuedFrames, Accumulated.PeakQueuedFrames);
	Stats.PeakQueuedFrameBytes = FMath::Max(Stats.PeakQueuedFrameBytes, Accumulated.PeakQueuedFrameBytes);
	DTCctvMergeFrameAgeStats(Stats, Accumulated);
	Stats.RenderCommandsInFlight = RenderInFlight.IsValid() ? RenderInFlight->load() : 0;
	Stats.PlaneTextureRebuilds = Renderer.IsValid() ? Renderer->GetPlaneTextureRebuilds() : 0;
	Stats.FramesDisplayed = FramesDisplayed;
	Stats.OutputSize = OutputSize;
	Stats.DecodeFps = DecodeFps;
	Stats.DisplayFps = DisplayFps;
	return Stats;
}

void UDTCctvStream::ResetFrameAgeStats()
{
	DTCctvClearFrameAgeStats(Accumulated);
	if (Session.IsValid())
	{
		Session->ResetFrameAgeStats();
	}
	if (PendingSession.IsValid())
	{
		PendingSession->ResetFrameAgeStats();
	}
}

TStatId UDTCctvStream::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDTCctvStream, STATGROUP_Tickables);
}

void UDTCctvStream::BeginDestroy()
{
	if (Session.IsValid())
	{
		FDTCctvSession::Retire(MoveTemp(Session));
		Session.Reset();
	}
	if (PendingSession.IsValid())
	{
		FDTCctvSession::Retire(MoveTemp(PendingSession));
		PendingSession.Reset();
	}
	Super::BeginDestroy();
}
