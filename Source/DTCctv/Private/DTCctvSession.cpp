#include "DTCctvSession.h"

#include "Containers/Ticker.h"
#include "DTCctvHwDevice.h"
#include "DTCctvLog.h"
#include "DTCctvPolicies.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

namespace
{
	/** 닫을 때 TEARDOWN 응답을 기다리는 최대 시간. */
	constexpr double CloseDeadlineSeconds = 1.0;
	/** 프레임이 몰려 도착해도 표시하도록 두는 대기열 길이. 표시가 못 따라가면 오래된 프레임부터 버린다. */
	constexpr int32 MaxPendingFrames = 8;
	constexpr int64 MaxPendingFrameBytes = 24 * 1024 * 1024;

	std::atomic<int32> ActiveHwSessions{ 0 };
	FCriticalSection SdpRegistryLock;
	TMap<void*, FDTCctvSession*> SdpRegistry;

	using FSessionPtr = TSharedPtr<FDTCctvSession, ESPMode::ThreadSafe>;

	double NowSeconds()
	{
		return FPlatformTime::Seconds();
	}

	FString AvErrorString(int32 Error)
	{
		char Buffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		av_strerror(Error, Buffer, sizeof(Buffer));
		return UTF8_TO_TCHAR(Buffer);
	}

	/** 동시 연결 시도 수를 제한해, 일제히 복구할 때 서버에 부하가 몰리지 않게 한다. */
	class FConnectGate
	{
	public:
		FConnectGate(int32 Limit, const std::atomic<bool>& bStop, FEvent* StopEvent)
		{
			while (!bStop.load())
			{
				{
					FScopeLock ScopeLock(&GetLock());
					if (GetActive() < Limit)
					{
						++GetActive();
						bAcquired = true;
						return;
					}
				}
				StopEvent->Wait(50);
			}
		}

		~FConnectGate()
		{
			if (bAcquired)
			{
				FScopeLock ScopeLock(&GetLock());
				--GetActive();
			}
		}

		bool IsAcquired() const { return bAcquired; }

	private:
		static FCriticalSection& GetLock()
		{
			static FCriticalSection Lock;
			return Lock;
		}

		static int32& GetActive()
		{
			static int32 Active = 0;
			return Active;
		}

		bool bAcquired = false;
	};

	bool IsDirectlyRenderable(int32 Format)
	{
		return Format == AV_PIX_FMT_NV12 || Format == AV_PIX_FMT_YUV420P || Format == AV_PIX_FMT_YUVJ420P;
	}

	/** 이 플랫폼에서 시도할 하드웨어 디코딩 순서. */
	TArray<AVHWDeviceType> GetHwCandidates()
	{
		TArray<AVHWDeviceType> Types;
		Types.Add(AV_HWDEVICE_TYPE_CUDA);
#if PLATFORM_WINDOWS
		Types.Add(AV_HWDEVICE_TYPE_D3D11VA);
#elif PLATFORM_LINUX
		Types.Add(AV_HWDEVICE_TYPE_VAAPI);
#endif
		return Types;
	}

	FCriticalSection& GetRetiredLock()
	{
		static FCriticalSection Lock;
		return Lock;
	}

	TArray<FSessionPtr>& GetRetiredSessions()
	{
		static TArray<FSessionPtr> Sessions;
		return Sessions;
	}

	FTSTicker::FDelegateHandle& GetRetireTickerHandle()
	{
		static FTSTicker::FDelegateHandle Handle;
		return Handle;
	}

	bool TickRetiredSessions(float)
	{
		// 해제는 스레드 종료를 기다리므로 잠금 밖에서 한다. 끝난 세션만 고르므로 막히지 않는다.
		TArray<FSessionPtr> Finished;
		{
			FScopeLock ScopeLock(&GetRetiredLock());
			TArray<FSessionPtr>& Sessions = GetRetiredSessions();
			for (int32 Index = Sessions.Num() - 1; Index >= 0; --Index)
			{
				if (Sessions[Index]->IsFinished())
				{
					Finished.Add(Sessions[Index]);
					Sessions.RemoveAtSwap(Index);
				}
			}
		}
		return true;
	}
}

class FDTCctvSession::FWorker : public FRunnable
{
public:
	explicit FWorker(TFunction<void()> InFunction)
		: Function(MoveTemp(InFunction))
	{
	}

	virtual uint32 Run() override
	{
		Function();
		return 0;
	}

private:
	TFunction<void()> Function;
};

FDTCctvSession::FDTCctvSession(const FDTCctvSessionParams& InParams)
	: Params(InParams)
	, Queue(InParams.MaxQueuedPackets)
{
	StopEvent = FPlatformProcess::GetSynchEventFromPool(true);
	FrameSpaceEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

FDTCctvSession::~FDTCctvSession()
{
	RequestStop();
	// FRunnableThread 소멸자가 스레드 종료를 기다린다. 스레드를 먼저 해제해야 Worker를 지울 수 있다.
	ReaderThread.Reset();
	ReaderWorker.Reset();
	FPlatformProcess::ReturnSynchEventToPool(StopEvent);
	FPlatformProcess::ReturnSynchEventToPool(FrameSpaceEvent);
}

void FDTCctvSession::Start()
{
	ReaderWorker = MakeUnique<FWorker>([this]() { ReaderMain(); });
	ReaderThread.Reset(FRunnableThread::Create(ReaderWorker.Get(), TEXT("DTCctvRead")));
	if (!ReaderThread.IsValid())
	{
		ReaderWorker.Reset();
		RequestStop();
		bFinished.store(true);
		DTCctvLogEvent(Params.LogContext, FString::Printf(TEXT("읽기 스레드를 시작하지 못했습니다: %s"), *DTCctvMaskUrlCredentials(Params.Url)), ELogVerbosity::Error);
	}
}

void FDTCctvSession::RequestStop()
{
	bStop.store(true);
	StopEvent->Trigger();
	FrameSpaceEvent->Trigger();
	Queue.Abort();
}

FDTCctvVideoFramePtr FDTCctvSession::TakeFrame(bool bCanCatchUp)
{
	FScopeLock ScopeLock(&Lock);
	const double Now = NowSeconds();
	LastConsumerPollSeconds = Now;
	if (PendingFrames.IsEmpty())
	{
		FrameBacklogSinceSeconds = 0.0;
		bConsumerCanCatchUp.store(bCanCatchUp);
		return nullptr;
	}
	if (PendingFrames.Num() > 1 && FrameBacklogSinceSeconds == 0.0)
	{
		FrameBacklogSinceSeconds = Now;
	}
	bConsumerCanCatchUp.store(bCanCatchUp && (FrameBacklogSinceSeconds == 0.0 || Now - FrameBacklogSinceSeconds < 0.75));
	TArray<double, TInlineAllocator<8>> Ages;
	for (const FDTCctvVideoFramePtr& Pending : PendingFrames)
	{
		Ages.Add(FMath::Max(0.0, Now - Pending->GetDecodedSeconds()));
	}
	const int32 Skip = DTCctvChooseStaleFrameSkip(Ages, FrameBacklogSinceSeconds > 0.0 ? Now - FrameBacklogSinceSeconds : 0.0, bCanCatchUp);
	if (Skip > 0)
	{
		for (int32 Index = 0; Index < Skip; ++Index)
		{
			PendingFrameBytes -= PendingFrames[Index]->GetBufferBytes();
		}
		PendingFrames.RemoveAt(0, Skip, false);
		FramesSuperseded.fetch_add(Skip);
	}
	FDTCctvVideoFramePtr Frame = PendingFrames[0];
	LastConsumerTakeSeconds = Now;
	PendingFrameBytes -= Frame->GetBufferBytes();
	PendingFrames.RemoveAt(0, 1, false);
	FrameSpaceEvent->Trigger();
	if (PendingFrames.IsEmpty())
	{
		FrameBacklogSinceSeconds = 0.0;
	}
	return Frame;
}

void FDTCctvSession::NotifyConsumerUnavailable()
{
	bConsumerCanCatchUp.store(false);
	FrameSpaceEvent->Trigger();
}

void FDTCctvSession::SetDesiredOutputSize(FIntPoint Size)
{
	FScopeLock ScopeLock(&Lock);
	DesiredOutputSize = Size;
}

FIntPoint FDTCctvSession::GetSourceSize() const
{
	FScopeLock ScopeLock(&Lock);
	return SourceSize;
}

void FDTCctvSession::FillStats(FDTCctvStreamStats& Stats) const
{
	Stats.State = State.load();
	Stats.PacketsReceived = PacketsReceived.load();
	Stats.FramesDecoded = FramesDecoded.load();
	Stats.FramesSuperseded = FramesSuperseded.load();
	Queue.FillStats(Stats);
	Stats.DecodeErrors = DecodeErrors.load();
	Stats.Reconnects = Reconnects.load();

	FScopeLock ScopeLock(&Lock);
	Stats.SourceSize = SourceSize;
	Stats.QueuedFrames = PendingFrames.Num();
	Stats.PeakQueuedFrames = PeakQueuedFrames;
	Stats.QueuedFrameBytes = PendingFrameBytes;
	Stats.PeakQueuedFrameBytes = PeakPendingFrameBytes;
	Stats.PreparedFrameSize = PreparedFrameSize;
	Stats.CodecName = CodecName;
	Stats.DecoderName = DecoderName;
	Stats.LastError = LastError;
	Stats.LastErrorCode = LastErrorCode;
	FrameAge.FillStats(Stats);
}

void FDTCctvSession::RecordFrameSubmitted(double DecodedSeconds, double SubmittedSeconds)
{
	FScopeLock ScopeLock(&Lock);
	FrameAge.Record((SubmittedSeconds - DecodedSeconds) * 1000.0);
}

void FDTCctvSession::ResetFrameAgeStats()
{
	FScopeLock ScopeLock(&Lock);
	FrameAge = FDTCctvFrameAgeAccumulator();
}

FDTCctvStreamInfo FDTCctvSession::GetStreamInfo() const
{
	FScopeLock ScopeLock(&Lock);
	return StreamInfo;
}

void FDTCctvSession::CaptureSdpLog(void* Context, const FString& Message)
{
	const int32 Start = Message.Find(TEXT("SDP:"));
	if (Start == INDEX_NONE)
	{
		return;
	}
	FScopeLock RegistryLock(&SdpRegistryLock);
	if (FDTCctvSession* Session = SdpRegistry.FindRef(Context))
	{
		FDTCctvStreamInfo Parsed;
		DTCctvParseSdpInfo(Message.Mid(Start + 4), Parsed);
		FScopeLock ScopeLock(&Session->Lock);
		Session->SdpInfo = MoveTemp(Parsed);
	}
}

void FDTCctvSession::CaptureConnectionInfo()
{
	AVStream* Video = FormatContext->streams[VideoStreamIndex];
	FDTCctvStreamInfo Info;
	{
		FScopeLock ScopeLock(&Lock);
		Info = SdpInfo;
		Info.DecoderName = DecoderName;
	}
	Info.SafeUrl = DTCctvMaskUrlCredentials(Params.Url);
	Info.Transport = Params.Transport == EDTCctvTransport::Tcp ? TEXT("TCP") : TEXT("UDP");
	Info.CodecName = UTF8_TO_TCHAR(avcodec_get_name(Video->codecpar->codec_id));
	Info.Profile = DTCctvProfileName(Video->codecpar->codec_id, CodecContext->profile);
	Info.Level = DTCctvLevelName(Video->codecpar->codec_id, CodecContext->level);
	Info.SourceSize = FIntPoint(Video->codecpar->width, Video->codecpar->height);
	DTCctvDescribePixelFormat(static_cast<AVPixelFormat>(Video->codecpar->format), Info);
	const AVRational Rate = Video->avg_frame_rate.num > 0 && Video->avg_frame_rate.den > 0
		? Video->avg_frame_rate : av_guess_frame_rate(FormatContext, Video, nullptr);
	Info.DeclaredFps = Rate.num > 0 && Rate.den > 0 ? av_q2d(Rate) : 0.0;
	Info.bDeclaredBFrames = Video->codecpar->video_delay > 0 || CodecContext->has_b_frames > 0;
	Info.DecoderReason = DecoderReason;
	Info.ConnectionNumber = ++ConnectionNumber;
	int32 VideoOrdinal = 0;
	for (uint32 Index = 0; Index < FormatContext->nb_streams; ++Index)
	{
		const AVCodecParameters* Parameters = FormatContext->streams[Index]->codecpar;
		if (Parameters->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			if (static_cast<int32>(Index) == VideoStreamIndex)
			{
				Info.SelectedVideoTrack = VideoOrdinal;
				Info.SelectedStreamIndex = VideoStreamIndex;
			}
			++VideoOrdinal;
		}
	}
	if (!Info.bSdpKnown)
	{
		Info.VideoTracks = VideoOrdinal;
	}
	if (Info.SessionName.IsEmpty())
	{
		if (const AVDictionaryEntry* Title = av_dict_get(FormatContext->metadata, "title", nullptr, 0))
		{
			Info.SessionName = DTCctvMaskUrlCredentials(UTF8_TO_TCHAR(Title->value));
		}
	}
	StreamProbe = FDTCctvStreamProbe();
	VideoPayloadBytes.store(0);
	bFrameFormatObserved = false;
	bInfoProbeComplete = false;
	{
		FScopeLock ScopeLock(&Lock);
		StreamInfo = MoveTemp(Info);
		LastError.Reset();
		LastErrorCode = 0;
	}
	InfoRevision.fetch_add(1);
}

void FDTCctvSession::ObserveFrameInfo(AVFrame* Frame, AVFrame* Transferred)
{
	if (FormatContext == nullptr || CodecContext == nullptr || VideoStreamIndex < 0)
	{
		return;
	}
	const FIntPoint Size(Transferred->width, Transferred->height);
	const bool bFormatChanged = bFrameFormatObserved && (InfoFrameSize != Size || InfoFrameFormat != Transferred->format);
	if (bInfoProbeComplete && !bFormatChanged)
	{
		return;
	}
	const double Now = NowSeconds();
	const AVRational TimeBase = FormatContext->streams[VideoStreamIndex]->time_base;
	const double MediaSeconds = Frame->best_effort_timestamp != AV_NOPTS_VALUE
		? Frame->best_effort_timestamp * av_q2d(TimeBase) : -1.0;
	if (!bInfoProbeComplete)
	{
		StreamProbe.Record(Now, MediaSeconds, (Frame->flags & AV_FRAME_FLAG_KEY) != 0, Frame->pict_type == AV_PICTURE_TYPE_B);
	}
	FScopeLock ScopeLock(&Lock);
	const bool bStableUpdate = !StreamInfo.bStabilized && StreamProbe.IsStable(Now);
	const bool bLateGop = StreamInfo.bStabilized && !StreamInfo.bMeasuredGop && StreamProbe.HasGop();
	if (!bFrameFormatObserved || bFormatChanged)
	{
		StreamInfo.SourceSize = Size;
		const char* Format = av_get_pix_fmt_name(static_cast<AVPixelFormat>(Transferred->format));
		StreamInfo.FramePixelFormat = Format != nullptr ? UTF8_TO_TCHAR(Format) : TEXT("미확인");
		if (StreamInfo.PixelFormat.IsEmpty() || bFormatChanged)
		{
			DTCctvDescribePixelFormat(CodecContext->sw_pix_fmt, StreamInfo);
			if (StreamInfo.PixelFormat.IsEmpty())
			{
				DTCctvDescribePixelFormat(static_cast<AVPixelFormat>(Transferred->format), StreamInfo);
			}
		}
		StreamInfo.DecoderName = DecoderName;
		if (bSoftwareFormatFallback.load())
		{
			StreamInfo.DecoderReason = TEXT("HW 형식 초기화 실패 후 유효 SW 형식 선택");
		}
		bFrameFormatObserved = true;
		InfoFrameSize = Size;
		InfoFrameFormat = Transferred->format;
		InfoRevision.fetch_add(1);
	}
	if (bStableUpdate || bLateGop)
	{
		StreamProbe.Fill(StreamInfo, VideoPayloadBytes.load(), Now);
		InfoRevision.fetch_add(1);
	}
	bInfoProbeComplete = StreamInfo.bStabilized && StreamInfo.bMeasuredGop;
}

void FDTCctvSession::SetError(const FString& Stage, int32 Error)
{
	FString Detail = AvErrorString(Error);
	if (Error == AVERROR_HTTP_UNAUTHORIZED) { Detail = TEXT("인증 실패(401)"); }
	else if (Error == AVERROR_HTTP_FORBIDDEN) { Detail = TEXT("접근 거부(403)"); }
	else if (Error == AVERROR_HTTP_NOT_FOUND) { Detail = TEXT("스트림 경로 없음(404)"); }
	else if (Error == AVERROR(ETIMEDOUT)) { Detail = TEXT("연결 또는 수신 시간 초과"); }
	else if (Error == AVERROR(ECONNREFUSED)) { Detail = TEXT("포트 연결 거부"); }
	else if (Error == AVERROR_DECODER_NOT_FOUND) { Detail = TEXT("코덱 미지원"); }
	const FString Message = DTCctvMaskUrlCredentials(Stage + TEXT(": ") + Detail);
	{
		FScopeLock ScopeLock(&Lock);
		if (LastError == Message)
		{
			return;
		}
		LastError = Message;
		LastErrorCode = Error;
	}
	DTCctvLogEvent(Params.LogContext, FString::Printf(TEXT("%s: %s"), *DTCctvMaskUrlCredentials(Params.Url), *Message), ELogVerbosity::Warning);
}

void FDTCctvSession::Retire(TSharedPtr<FDTCctvSession, ESPMode::ThreadSafe> Session)
{
	if (!Session.IsValid())
	{
		return;
	}
	Session->RequestStop();

	FScopeLock ScopeLock(&GetRetiredLock());
	GetRetiredSessions().Add(MoveTemp(Session));
	if (!GetRetireTickerHandle().IsValid())
	{
		GetRetireTickerHandle() = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRetiredSessions), 0.25f);
	}
}

void FDTCctvSession::ShutdownRetired()
{
	if (GetRetireTickerHandle().IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GetRetireTickerHandle());
		GetRetireTickerHandle().Reset();
	}

	TArray<FSessionPtr> Sessions;
	{
		FScopeLock ScopeLock(&GetRetiredLock());
		Sessions = MoveTemp(GetRetiredSessions());
		GetRetiredSessions().Reset();
	}
	// 각 세션의 소멸자가 스레드 종료를 기다린다.
	Sessions.Reset();
}

int FDTCctvSession::InterruptCallback(void* Opaque)
{
	const FDTCctvSession* Session = static_cast<const FDTCctvSession*>(Opaque);
	return (Session->bStop.load() || NowSeconds() > Session->Deadline.load()) ? 1 : 0;
}

AVPixelFormat FDTCctvSession::GetFormatCallback(AVCodecContext* Context, const AVPixelFormat* Formats)
{
	FDTCctvSession* Session = static_cast<FDTCctvSession*>(Context->opaque);
	if (!Session->bSoftwareFormatFallback.load())
	{
		for (const AVPixelFormat* Format = Formats; *Format != AV_PIX_FMT_NONE; ++Format)
		{
			if (static_cast<int32>(*Format) == Session->HwPixelFormat)
			{
				return *Format;
			}
		}
	}
	AVPixelFormat SoftwareFormat = AV_PIX_FMT_NONE;
	for (const AVPixelFormat* Format = Formats; *Format != AV_PIX_FMT_NONE; ++Format)
	{
		const AVPixFmtDescriptor* Descriptor = av_pix_fmt_desc_get(*Format);
		if (Descriptor != nullptr && (Descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL) == 0)
		{
			SoftwareFormat = *Format;
			break;
		}
	}
	if (SoftwareFormat != AV_PIX_FMT_NONE)
	{
		if (!Session->bSoftwareFormatFallback.exchange(true))
		{
			DTCctvLogEvent(Session->Params.LogContext, FString::Printf(TEXT("하드웨어 형식 협상 후 소프트웨어 형식으로 연결을 유지합니다.")), ELogVerbosity::Warning);
		}
		Session->bHwFailed.store(false);
		FScopeLock ScopeLock(&Session->Lock);
		Session->DecoderName = TEXT("software");
	}
	else
	{
		Session->bHwFailed.store(true);
		DTCctvLogEvent(Session->Params.LogContext, FString::Printf(TEXT("디코더에 사용 가능한 소프트웨어 형식이 없어 다시 열어야 합니다.")), ELogVerbosity::Warning);
	}
	return SoftwareFormat;
}

void FDTCctvSession::ExtendDeadline(double Seconds)
{
	Deadline.store(NowSeconds() + Seconds);
}

void FDTCctvSession::ReaderMain()
{
	const FDTCctvLogScope LogScope(Params.LogContext);
	FDTCctvBackoff Backoff(Params.MinReconnectDelaySeconds, Params.MaxReconnectDelaySeconds);
	const FString SafeUrl = DTCctvMaskUrlCredentials(Params.Url);
	bool bFirstAttempt = true;

	while (!bStop.load())
	{
		State.store(bFirstAttempt ? EDTCctvState::Connecting : EDTCctvState::Reconnecting);
		FirstFrameSeconds.store(0.0);
		LastFrameSeconds.store(0.0);
		bHwFailed.store(false);
		bSoftwareFormatFallback.store(false);

		bool bOpened = false;
		{
			const FConnectGate Gate(Params.MaxConcurrentConnects, bStop, StopEvent);
			bOpened = Gate.IsAcquired() && OpenInput() && OpenDecoder();
		}

		if (bOpened && !bStop.load())
		{
			CaptureConnectionInfo();
			Queue.Reset();
			DecoderWorker = MakeUnique<FWorker>([this]() { DecoderMain(); });
			DecoderThread.Reset(FRunnableThread::Create(DecoderWorker.Get(), TEXT("DTCctvDecode")));

			if (DecoderThread.IsValid())
			{
				ReadLoop();
			}
			else
			{
				DTCctvLogEvent(Params.LogContext, FString::Printf(TEXT("디코딩 스레드를 시작하지 못했습니다: %s"), *SafeUrl), ELogVerbosity::Error);
			}

			Queue.Abort();
			DecoderThread.Reset();
			DecoderWorker.Reset();
		}

		const double FirstFrame = FirstFrameSeconds.load();
		const double StableSeconds = FirstFrame > 0.0 ? NowSeconds() - FirstFrame : 0.0;

		CloseConnection();

		if (bStop.load())
		{
			break;
		}

		bFirstAttempt = false;
		Reconnects.fetch_add(1);
		State.store(EDTCctvState::Reconnecting);
		const float Delay = Backoff.Next(StableSeconds, FMath::FRand());
		DTCctvLogEvent(Params.LogContext, FString::Printf(TEXT("%s: %.1f초 후 재연결합니다."), *SafeUrl, Delay), ELogVerbosity::Log);
		StopEvent->Wait(FTimespan::FromSeconds(Delay));
	}

	State.store(EDTCctvState::Idle);
	bFinished.store(true);
}

bool FDTCctvSession::OpenInput()
{
	const FString SafeUrl = DTCctvMaskUrlCredentials(Params.Url);
	ExtendDeadline(Params.ConnectTimeoutSeconds);

	FormatContext = avformat_alloc_context();
	if (FormatContext != nullptr)
	{
		FScopeLock RegistryLock(&SdpRegistryLock);
		RegisteredSdpContext = FormatContext;
		SdpRegistry.Add(FormatContext, this);
		SdpInfo = FDTCctvStreamInfo();
	}
	if (FormatContext == nullptr)
	{
		return false;
	}
	FormatContext->interrupt_callback.callback = &FDTCctvSession::InterruptCallback;
	FormatContext->interrupt_callback.opaque = this;

	AVDictionary* Options = nullptr;
	av_dict_set(&Options, "rtsp_transport", Params.Transport == EDTCctvTransport::Tcp ? "tcp" : "udp", 0);
	av_dict_set_int(&Options, "timeout", static_cast<int64>(Params.ReadTimeoutSeconds * 1000000.0), 0);
	av_dict_set(&Options, "allowed_media_types", "video", 0);
	av_dict_set(&Options, "fflags", "nobuffer+discardcorrupt", 0);
	av_dict_set(&Options, "flags", "low_delay", 0);
	av_dict_set_int(&Options, "max_delay", 200000, 0);
	av_dict_set_int(&Options, "analyzeduration", 1000000, 0);
	av_dict_set_int(&Options, "probesize", 2000000, 0);
	if (Params.Transport == EDTCctvTransport::Udp)
	{
		av_dict_set_int(&Options, "buffer_size", 8 * 1024 * 1024, 0);
	}

	const int32 OpenError = avformat_open_input(&FormatContext, TCHAR_TO_UTF8(*Params.Url), nullptr, &Options);
	av_dict_free(&Options);
	if (OpenError < 0)
	{
		// 실패하면 FFmpeg가 컨텍스트를 해제하고 nullptr로 만든다.
		SetError(TEXT("연결 실패"), OpenError);
		return false;
	}

	const int32 InfoError = avformat_find_stream_info(FormatContext, nullptr);
	if (InfoError < 0)
	{
		SetError(TEXT("스트림 분석 실패"), InfoError);
		return false;
	}

	VideoStreamIndex = av_find_best_stream(FormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (VideoStreamIndex < 0)
	{
		DTCctvLogEvent(Params.LogContext, FString::Printf(TEXT("%s: 비디오 스트림이 없습니다."), *SafeUrl), ELogVerbosity::Warning);
		return false;
	}

	for (uint32 Index = 0; Index < FormatContext->nb_streams; ++Index)
	{
		FormatContext->streams[Index]->discard = static_cast<int32>(Index) == VideoStreamIndex ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
	}
	return true;
}

bool FDTCctvSession::TryOpenDecoder(const AVCodec* Codec, AVHWDeviceType HwType)
{
	AVStream* Stream = FormatContext->streams[VideoStreamIndex];
	const bool bHardware = HwType != AV_HWDEVICE_TYPE_NONE;

	HwPixelFormat = -1;
	if (bHardware)
	{
		for (int32 Index = 0;; ++Index)
		{
			const AVCodecHWConfig* HwConfig = avcodec_get_hw_config(Codec, Index);
			if (HwConfig == nullptr)
			{
				return false;
			}
			if ((HwConfig->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0 && HwConfig->device_type == HwType)
			{
				HwPixelFormat = HwConfig->pix_fmt;
				break;
			}
		}
	}

	AVCodecContext* Context = avcodec_alloc_context3(Codec);
	if (Context == nullptr || avcodec_parameters_to_context(Context, Stream->codecpar) < 0)
	{
		avcodec_free_context(&Context);
		return false;
	}
	Context->pkt_timebase = Stream->time_base;
	// B 프레임이 있으면 순서가 어긋날 수 있어 없을 때만 저지연 출력을 쓴다.
	if (Stream->codecpar->video_delay == 0)
	{
		Context->flags |= AV_CODEC_FLAG_LOW_DELAY;
	}

	if (bHardware)
	{
		Context->hw_device_ctx = DTCctvHwDevice::Acquire(HwType);
		if (Context->hw_device_ctx == nullptr)
		{
			avcodec_free_context(&Context);
			return false;
		}
		Context->opaque = this;
		Context->get_format = &FDTCctvSession::GetFormatCallback;
		Context->thread_count = 1;
	}
	else
	{
		Context->thread_count = FMath::Max(1, Params.SoftwareDecodeThreads);
		Context->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
	}

	if (avcodec_open2(Context, Codec, nullptr) < 0)
	{
		avcodec_free_context(&Context);
		return false;
	}

	CodecContext = Context;
	const FString Decoder = bHardware && !bSoftwareFormatFallback.load() ? UTF8_TO_TCHAR(av_hwdevice_get_type_name(HwType)) : TEXT("software");
	const FString Codecname = UTF8_TO_TCHAR(avcodec_get_name(Stream->codecpar->codec_id));
	{
		FScopeLock ScopeLock(&Lock);
		CodecName = Codecname;
		DecoderName = Decoder;
	}

	const AVRational FrameRate = av_guess_frame_rate(FormatContext, Stream, nullptr);
	NominalFps.store(FrameRate.den != 0 ? FMath::Max(0.0, av_q2d(FrameRate)) : 0.0);

	return true;
}

bool FDTCctvSession::OpenDecoder()
{
	const AVStream* Stream = FormatContext->streams[VideoStreamIndex];
	const AVCodec* Codec = avcodec_find_decoder(Stream->codecpar->codec_id);
	DecoderReason = Params.DecodeMode == EDTCctvDecodeMode::SoftwareOnly ? TEXT("사용자 소프트웨어 지정") : TEXT("이전 HW 장애로 소프트웨어 유지");
	if (Codec == nullptr)
	{
		SetError(TEXT("디코더 선택"), AVERROR_DECODER_NOT_FOUND);
		DTCctvLogEvent(Params.LogContext, FString::Printf(TEXT("%s: 지원하지 않는 코덱입니다: %s"),
			*DTCctvMaskUrlCredentials(Params.Url), UTF8_TO_TCHAR(avcodec_get_name(Stream->codecpar->codec_id))), ELogVerbosity::Warning);
		return false;
	}

	if (Params.DecodeMode == EDTCctvDecodeMode::Auto && !bForceSoftware)
	{
		DecoderReason = TEXT("HW 슬롯 초과");
		if (ActiveHwSessions.fetch_add(1) < Params.MaxHardwareDecodeStreams)
		{
			bHoldsHwSlot = true;
			for (const AVHWDeviceType HwType : GetHwCandidates())
			{
				if (TryOpenDecoder(Codec, HwType))
				{
					DecoderReason = TEXT("지원 HW 장치 선택");
					return true;
				}
			}
			bHoldsHwSlot = false;
			DecoderReason = TEXT("HW 형식 미지원 또는 장치 초기화 실패");
		}
		ActiveHwSessions.fetch_sub(1);
	}
	return TryOpenDecoder(Codec, AV_HWDEVICE_TYPE_NONE);
}

void FDTCctvSession::ReadLoop()
{
	const FString SafeUrl = DTCctvMaskUrlCredentials(Params.Url);
	const double WatchdogStart = NowSeconds();
	double FirstKeyPacketSeconds = 0.0;
	const int64 DroppedAtStart = Queue.GetDroppedPackets();
	AVPacket* Packet = av_packet_alloc();

	while (Packet != nullptr && !bStop.load())
	{
		const double LastFrame = LastFrameSeconds.load();
		const EDTCctvOutputWatchdogAction WatchdogAction = DTCctvEvaluateOutputWatchdog(
			NowSeconds(), WatchdogStart, FirstKeyPacketSeconds, LastFrame, Params.ReadTimeoutSeconds, HwPixelFormat >= 0 && !bSoftwareFormatFallback.load());
		if (WatchdogAction != EDTCctvOutputWatchdogAction::None)
		{
			if (FirstKeyPacketSeconds == 0.0 && LastFrame == 0.0)
			{
				DTCctvLogEvent(Params.LogContext, FString::Printf(TEXT("%s: 30초 안에 키프레임을 받지 못해 디코딩 방식을 유지하고 다시 연결합니다."), *SafeUrl), ELogVerbosity::Warning);
			}
			else
			{
				DTCctvLogEvent(Params.LogContext, FString::Printf(TEXT("%s: 키프레임 수신 후 디코딩 출력 감시 기한을 넘어 다시 연결합니다."), *SafeUrl), ELogVerbosity::Warning);
			}
			if (WatchdogAction == EDTCctvOutputWatchdogAction::RetrySoftware)
			{
				bForceSoftware = true;
			}
			break;
		}
		ExtendDeadline(Params.ReadTimeoutSeconds);
		const int32 ReadResult = av_read_frame(FormatContext, Packet);
		if (ReadResult == AVERROR(EAGAIN))
		{
			FPlatformProcess::Sleep(0.002f);
			continue;
		}
		if (ReadResult < 0)
		{
			if (!bStop.load())
			{
				SetError(TEXT("수신 중단"), ReadResult);
			}
			break;
		}

		if (Packet->stream_index == VideoStreamIndex)
		{
			if (FirstKeyPacketSeconds == 0.0 && (Packet->flags & AV_PKT_FLAG_KEY) != 0)
			{
				FirstKeyPacketSeconds = NowSeconds();
			}
			PacketsReceived.fetch_add(1);
			VideoPayloadBytes.fetch_add(FMath::Max(0, Packet->size));
			Queue.Push(Packet);
		}
		av_packet_unref(Packet);

		if (bHwFailed.load() && FirstKeyPacketSeconds > 0.0)
		{
			DTCctvLogEvent(Params.LogContext, FString::Printf(TEXT("%s: 하드웨어 디코딩 오류로 다시 연결합니다. 키프레임 수신=%d"), *SafeUrl, FirstKeyPacketSeconds > 0.0 ? 1 : 0), ELogVerbosity::Warning);
			bForceSoftware = true;
			break;
		}

		// 하드웨어 디코딩이 입력을 못 따라가면 이 채널은 소프트웨어로 옮긴다.
		if (HwPixelFormat >= 0 && !bSoftwareFormatFallback.load() && Queue.GetDroppedPackets() > DroppedAtStart)
		{
			DTCctvLogEvent(Params.LogContext, FString::Printf(TEXT("%s: 하드웨어 디코딩 과부하로 다시 연결합니다. 키프레임 수신=%d"), *SafeUrl, FirstKeyPacketSeconds > 0.0 ? 1 : 0), ELogVerbosity::Warning);
			if (FirstKeyPacketSeconds > 0.0)
			{
				bForceSoftware = true;
			}
			break;
		}
	}

	av_packet_free(&Packet);
}

void FDTCctvSession::CloseConnection()
{
	avcodec_free_context(&CodecContext);
	if (bHoldsHwSlot)
	{
		bHoldsHwSlot = false;
		ActiveHwSessions.fetch_sub(1);
	}
	sws_freeContext(ScaleContext);
	ScaleContext = nullptr;
	if (FormatContext != nullptr)
	{
		// TEARDOWN을 보낼 짧은 시간만 준다. 중단 요청이 있으면 바로 끝난다.
		ExtendDeadline(CloseDeadlineSeconds);
		avformat_close_input(&FormatContext);
	}
	if (RegisteredSdpContext != nullptr)
	{
		FScopeLock RegistryLock(&SdpRegistryLock);
		if (SdpRegistry.FindRef(RegisteredSdpContext) == this)
		{
			SdpRegistry.Remove(RegisteredSdpContext);
		}
		RegisteredSdpContext = nullptr;
	}
	VideoStreamIndex = -1;
	HwPixelFormat = -1;
	DTCctvHwDevice::ReleaseUnused();
}

void FDTCctvSession::DecoderMain()
{
	const FDTCctvLogScope LogScope(Params.LogContext);
	AVFrame* Decoded = av_frame_alloc();
	AVPacket* Packet = nullptr;
	bool bDiscontinuity = false;

	while (Decoded != nullptr && Queue.Pop(Packet, bDiscontinuity))
	{
		if (bDiscontinuity)
		{
			avcodec_flush_buffers(CodecContext);
		}

		const int32 SendResult = avcodec_send_packet(CodecContext, Packet);
		av_packet_free(&Packet);
		if (SendResult < 0 && SendResult != AVERROR(EAGAIN))
		{
			DecodeErrors.fetch_add(1);
			SetError(TEXT("디코딩 패킷 오류"), SendResult);
		}

		for (;;)
		{
			const int32 ReceiveResult = avcodec_receive_frame(CodecContext, Decoded);
			if (ReceiveResult == AVERROR(EAGAIN) || ReceiveResult == AVERROR_EOF)
			{
				break;
			}
			if (ReceiveResult < 0)
			{
				DecodeErrors.fetch_add(1);
				SetError(TEXT("디코딩 출력 오류"), ReceiveResult);
				break;
			}
			OutputFrame(Decoded);
			av_frame_unref(Decoded);
		}
	}

	av_frame_free(&Decoded);
}

void FDTCctvSession::OutputFrame(AVFrame* Decoded)
{
	const double DecodedSeconds = NowSeconds();
	AVFrame* Output = nullptr;
	if (Decoded->hw_frames_ctx != nullptr)
	{
		Output = av_frame_alloc();
		if (Output == nullptr || av_hwframe_transfer_data(Output, Decoded, 0) < 0 || av_frame_copy_props(Output, Decoded) < 0)
		{
			av_frame_free(&Output);
			bHwFailed.store(true);
			return;
		}
	}
	else
	{
		Output = av_frame_clone(Decoded);
		if (Output == nullptr)
		{
			DecodeErrors.fetch_add(1);
			return;
		}
	}

	ObserveFrameInfo(Decoded, Output);
	const FIntPoint FrameSourceSize(Output->width, Output->height);
	FIntPoint WantedSize;
	{
		FScopeLock ScopeLock(&Lock);
		WantedSize = DesiredOutputSize;
	}
	if (WantedSize.X > 0 && WantedSize.Y > 0)
	{
		WantedSize = DTCctvComputeOutputSize(FrameSourceSize, FVector2D(WantedSize));
	}
	const bool bDownscale = WantedSize.X > 0 && WantedSize.Y > 0 && WantedSize.X <= Output->width / 2 && WantedSize.Y <= Output->height / 2;

	if (bDownscale || !IsDirectlyRenderable(Output->format))
	{
		// 축소가 필요하거나, 10비트·4:2:2처럼 셰이더가 다루지 않는 형식이면 NV12로 변환한다.
		const int32 TargetWidth = bDownscale ? WantedSize.X : Output->width;
		const int32 TargetHeight = bDownscale ? WantedSize.Y : Output->height;
		AVFrame* Converted = av_frame_alloc();
		ScaleContext = sws_getCachedContext(ScaleContext,
			Output->width, Output->height, static_cast<AVPixelFormat>(Output->format),
			TargetWidth, TargetHeight, AV_PIX_FMT_NV12,
			bDownscale ? SWS_BILINEAR : SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
		bool bConverted = false;
		if (Converted != nullptr && ScaleContext != nullptr)
		{
			Converted->format = AV_PIX_FMT_NV12;
			Converted->width = TargetWidth;
			Converted->height = TargetHeight;
			if (av_frame_get_buffer(Converted, 0) >= 0 && av_frame_copy_props(Converted, Output) >= 0)
			{
				bConverted = sws_scale(ScaleContext, Output->data, Output->linesize, 0, Output->height, Converted->data, Converted->linesize) == TargetHeight;
			}
		}
		av_frame_free(&Output);
		if (!bConverted)
		{
			av_frame_free(&Converted);
			DecodeErrors.fetch_add(1);
			return;
		}
		Output = Converted;
	}

	const FDTCctvVideoFramePtr Frame = MakeShared<FDTCctvVideoFrame, ESPMode::ThreadSafe>(Output, FrameSourceSize, DecodedSeconds);
	const double WaitStartedSeconds = NowSeconds();
	for (;;)
	{
		if (bStop.load())
		{
			return;
		}
		{
			FScopeLock ScopeLock(&Lock);
			const int64 FrameBytes = Frame->GetBufferBytes();
			const int64 ByteLimit = FMath::Max(MaxPendingFrameBytes, FrameBytes);
			const bool bFull = PendingFrames.Num() >= MaxPendingFrames || PendingFrameBytes + FrameBytes > ByteLimit;
			if (!bFull || !DTCctvShouldBackpressure(NowSeconds(), LastConsumerPollSeconds, LastConsumerTakeSeconds, WaitStartedSeconds, bConsumerCanCatchUp.load()))
			{
				while (!PendingFrames.IsEmpty() && (PendingFrames.Num() >= MaxPendingFrames || PendingFrameBytes + FrameBytes > ByteLimit))
				{
					FramesSuperseded.fetch_add(1);
					PendingFrameBytes -= PendingFrames[0]->GetBufferBytes();
					PendingFrames.RemoveAt(0, 1, false);
				}
				SourceSize = FrameSourceSize;
				PreparedFrameSize = Frame->GetSize();
				PendingFrames.Add(Frame);
				PendingFrameBytes += FrameBytes;
				PeakPendingFrameBytes = FMath::Max(PeakPendingFrameBytes, PendingFrameBytes);
				PeakQueuedFrames = FMath::Max(PeakQueuedFrames, PendingFrames.Num());
				break;
			}
		}
		// 소비 여유가 있는 짧은 버스트만 디코딩 스레드에서 기다린다. 게임 스레드는 기다리지 않는다.
		FrameSpaceEvent->Wait(5);
	}

	const double Now = NowSeconds();
	LastFrameSeconds.store(Now);
	FramesDecoded.fetch_add(1);
	if (FirstFrameSeconds.load() == 0.0)
	{
		FirstFrameSeconds.store(Now);
		State.store(EDTCctvState::Playing);
	}
}
