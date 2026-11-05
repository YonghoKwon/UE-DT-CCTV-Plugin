#pragma once

#include "CoreMinimal.h"
#include "DTCctvFFmpegInclude.h"
#include "DTCctvPacketQueue.h"
#include "DTCctvTypes.h"
#include "DTCctvVideoFrame.h"
#include "HAL/CriticalSection.h"
#include <atomic>

class FEvent;
class FRunnableThread;

struct FDTCctvSessionParams
{
	FString Url;
	EDTCctvTransport Transport = EDTCctvTransport::Tcp;
	EDTCctvDecodeMode DecodeMode = EDTCctvDecodeMode::Auto;
	float ConnectTimeoutSeconds = 5.f;
	float ReadTimeoutSeconds = 5.f;
	float MinReconnectDelaySeconds = 0.5f;
	float MaxReconnectDelaySeconds = 2.f;
	int32 MaxConcurrentConnects = 8;
	int32 MaxQueuedPackets = 120;
	int32 MaxHardwareDecodeStreams = 4;
	int32 SoftwareDecodeThreads = 2;
};

/**
 * RTSP 연결 하나의 수명을 담당한다. 읽기 스레드가 연결과 복구를 맡고 디코딩 스레드가 프레임을 만든다.
 * 호출자를 막지 않도록 Retire로 넘기면 스레드가 끝난 뒤 정리된다.
 */
class FDTCctvSession
{
public:
	explicit FDTCctvSession(const FDTCctvSessionParams& InParams);
	~FDTCctvSession();

	void Start();
	void RequestStop();
	bool IsFinished() const { return bFinished.load(); }

	/** 표시 대기 중인 가장 오래된 프레임을 가져간다. 없으면 nullptr. */
	FDTCctvVideoFramePtr TakeFrame();

	EDTCctvState GetState() const { return State.load(); }
	bool HasDecodedFrame() const { return FramesDecoded.load() > 0; }
	FIntPoint GetSourceSize() const;

	/** 표시에 필요한 크기를 알린다. 원본의 절반 이하이면 업로드 전에 줄여 GPU 부하를 낮춘다. */
	void SetDesiredOutputSize(FIntPoint Size);

	/** 세션에서 알 수 있는 통계를 채운다. 화면 관련 값은 UDTCctvStream이 채운다. */
	void FillStats(FDTCctvStreamStats& Stats) const;

	/** 세션을 넘겨받아 스레드가 끝나면 해제한다. 게임 스레드에서 호출한다. */
	static void Retire(TSharedPtr<FDTCctvSession, ESPMode::ThreadSafe> Session);
	/** 모듈 종료 시 남은 세션을 모두 기다려 해제한다. */
	static void ShutdownRetired();

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FDTCctvHwFallbackTest;
	friend class FDTCctvFrameBurstTest;
#endif
	class FWorker;

	static int InterruptCallback(void* Opaque);
	static AVPixelFormat GetFormatCallback(AVCodecContext* Context, const AVPixelFormat* Formats);

	void ReaderMain();
	bool OpenInput();
	bool OpenDecoder();
	bool TryOpenDecoder(const AVCodec* Codec, AVHWDeviceType HwType);
	void ReadLoop();
	void CloseConnection();

	void DecoderMain();
	void OutputFrame(AVFrame* Decoded);

	void ExtendDeadline(double Seconds);

	FDTCctvSessionParams Params;

	AVFormatContext* FormatContext = nullptr;
	AVCodecContext* CodecContext = nullptr;
	SwsContext* ScaleContext = nullptr;
	int32 VideoStreamIndex = -1;
	int32 HwPixelFormat = -1;
	bool bForceSoftware = false;
	bool bHoldsHwSlot = false;

	FDTCctvPacketQueue Queue;

	FEvent* StopEvent = nullptr;
	TUniquePtr<FWorker> ReaderWorker;
	TUniquePtr<FRunnableThread> ReaderThread;
	TUniquePtr<FWorker> DecoderWorker;
	TUniquePtr<FRunnableThread> DecoderThread;

	std::atomic<bool> bStop{ false };
	std::atomic<bool> bFinished{ false };
	std::atomic<bool> bHwFailed{ false };
	std::atomic<double> Deadline{ 0.0 };
	std::atomic<double> LastFrameSeconds{ 0.0 };
	std::atomic<double> FirstFrameSeconds{ 0.0 };
	std::atomic<EDTCctvState> State{ EDTCctvState::Idle };
	std::atomic<int64> PacketsReceived{ 0 };
	std::atomic<int64> FramesDecoded{ 0 };
	std::atomic<int64> FramesSuperseded{ 0 };
	std::atomic<int64> DecodeErrors{ 0 };
	std::atomic<int32> Reconnects{ 0 };

	mutable FCriticalSection Lock;
	FIntPoint SourceSize = FIntPoint::ZeroValue;
	FIntPoint DesiredOutputSize = FIntPoint::ZeroValue;
	TArray<FDTCctvVideoFramePtr, TInlineAllocator<8>> PendingFrames;
	int32 PeakQueuedFrames = 0;
	FString CodecName;
	FString DecoderName;
};
