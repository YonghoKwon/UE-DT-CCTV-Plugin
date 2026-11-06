#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DTCctvFFmpegInclude.h"
#include "DTCctvPacketQueue.h"
#include "DTCctvPolicies.h"
#include "DTCctvSession.h"
#include "DTCctvVideoFrame.h"
#include "DTCctvYuvRenderer.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvMaskUrlTest, "DTCctv.Policies.MaskUrl", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvMaskUrlTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("계정 가림"), DTCctvMaskUrlCredentials(TEXT("rtsp://admin:secret@10.0.0.1:554/s1")), FString(TEXT("rtsp://***@10.0.0.1:554/s1")));
	TestEqual(TEXT("비밀번호의 @"), DTCctvMaskUrlCredentials(TEXT("rtsp://admin:p@ss@10.0.0.1/s1")), FString(TEXT("rtsp://***@10.0.0.1/s1")));
	TestEqual(TEXT("계정 없음"), DTCctvMaskUrlCredentials(TEXT("rtsp://10.0.0.1/a@b")), FString(TEXT("rtsp://10.0.0.1/a@b")));
	TestEqual(TEXT("문장 속 URL"), DTCctvMaskUrlCredentials(TEXT("open rtsp://u:p@h/s failed")), FString(TEXT("open rtsp://***@h/s failed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvOutputSizeTest, "DTCctv.Policies.OutputSize", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvOutputSizeTest::RunTest(const FString& Parameters)
{
	const FIntPoint Source(1920, 1080);
	TestTrue(TEXT("크기 정보 없음"), DTCctvComputeOutputSize(Source, FVector2D::ZeroVector) == Source);
	TestTrue(TEXT("확대하지 않음"), DTCctvComputeOutputSize(Source, FVector2D(3840, 2160)) == Source);
	TestTrue(TEXT("절반"), DTCctvComputeOutputSize(Source, FVector2D(960, 540)) == FIntPoint(960, 540));
	TestTrue(TEXT("비율 유지"), DTCctvComputeOutputSize(Source, FVector2D(960, 1000)) == FIntPoint(960, 540));
	TestTrue(TEXT("원본 없음"), DTCctvComputeOutputSize(FIntPoint::ZeroValue, FVector2D(960, 540)) == FIntPoint::ZeroValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvBackoffTest, "DTCctv.Policies.Backoff", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvBackoffTest::RunTest(const FString& Parameters)
{
	FDTCctvBackoff Backoff(0.5f, 4.f);
	TestEqual(TEXT("1회"), Backoff.Next(0.0, 0.f), 0.5f);
	TestEqual(TEXT("2회"), Backoff.Next(0.0, 0.f), 1.f);
	TestEqual(TEXT("3회"), Backoff.Next(0.0, 0.f), 2.f);
	TestEqual(TEXT("4회"), Backoff.Next(0.0, 0.f), 4.f);
	TestEqual(TEXT("상한"), Backoff.Next(0.0, 0.f), 4.f);
	TestEqual(TEXT("지터"), Backoff.Next(0.0, 1.f), 5.f);
	TestEqual(TEXT("안정 후 초기화"), Backoff.Next(FDTCctvBackoff::StableResetSeconds, 0.f), 0.5f);
	return true;
}

namespace
{
	void PushPacket(FDTCctvPacketQueue& Queue, bool bKeyFrame)
	{
		AVPacket* Packet = av_packet_alloc();
		av_new_packet(Packet, 8);
		Packet->flags = bKeyFrame ? AV_PKT_FLAG_KEY : 0;
		Queue.Push(Packet);
		av_packet_free(&Packet);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvPacketQueueTest, "DTCctv.Policies.PacketQueue", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvPacketQueueTest::RunTest(const FString& Parameters)
{
	FDTCctvPacketQueue Queue(3);
	PushPacket(Queue, true);
	PushPacket(Queue, false);
	PushPacket(Queue, false);
	TestEqual(TEXT("상한까지 보관"), Queue.Num(), 3);

	// 상한 초과: 큐를 비우고 키프레임이 아닌 패킷도 버린다.
	PushPacket(Queue, false);
	TestEqual(TEXT("비워짐"), Queue.Num(), 0);
	TestEqual(TEXT("버린 수"), Queue.GetDroppedPackets(), static_cast<int64>(4));

	PushPacket(Queue, false);
	TestEqual(TEXT("키프레임 전까지 버림"), Queue.Num(), 0);

	PushPacket(Queue, true);
	TestEqual(TEXT("키프레임부터 재개"), Queue.Num(), 1);

	AVPacket* Packet = nullptr;
	bool bDiscontinuity = false;
	TestTrue(TEXT("꺼내기"), Queue.Pop(Packet, bDiscontinuity));
	TestTrue(TEXT("불연속 알림"), bDiscontinuity);
	TestTrue(TEXT("키프레임"), (Packet->flags & AV_PKT_FLAG_KEY) != 0);
	av_packet_free(&Packet);

	Queue.Abort();
	TestFalse(TEXT("중단 후 대기 안 함"), Queue.Pop(Packet, bDiscontinuity));

	Queue.Reset();
	PushPacket(Queue, true);
	TestEqual(TEXT("재시작"), Queue.Num(), 1);
	return true;
}

namespace
{
	FVector3f Apply(const FDTCctvColorTransform& Transform, float Y, float Cb, float Cr)
	{
		const auto Dot = [Y, Cb, Cr](const FVector4f& Row)
		{
			return Row.X * Y + Row.Y * Cb + Row.Z * Cr + Row.W;
		};
		return FVector3f(Dot(Transform.RowR), Dot(Transform.RowG), Dot(Transform.RowB));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvColorTest, "DTCctv.Policies.ColorTransform", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvColorTest::RunTest(const FString& Parameters)
{
	const float Tolerance = 0.01f;

	const FDTCctvColorTransform Limited709 = DTCctvMakeColorTransform(EDTCctvYuvMatrix::Bt709, false);
	FVector3f Rgb = Apply(Limited709, 16.f / 255.f, 128.f / 255.f, 128.f / 255.f);
	TestTrue(TEXT("제한 범위 검정"), Rgb.Equals(FVector3f::ZeroVector, Tolerance));
	Rgb = Apply(Limited709, 235.f / 255.f, 128.f / 255.f, 128.f / 255.f);
	TestTrue(TEXT("제한 범위 흰색"), Rgb.Equals(FVector3f::OneVector, Tolerance));
	// BT.709 제한 범위 순수 빨강: Y=63, Cb=102, Cr=240
	Rgb = Apply(Limited709, 63.f / 255.f, 102.f / 255.f, 240.f / 255.f);
	TestTrue(TEXT("BT.709 빨강"), Rgb.Equals(FVector3f(1.f, 0.f, 0.f), 0.02f));

	const FDTCctvColorTransform Full601 = DTCctvMakeColorTransform(EDTCctvYuvMatrix::Bt601, true);
	Rgb = Apply(Full601, 1.f, 128.f / 255.f, 128.f / 255.f);
	TestTrue(TEXT("전체 범위 흰색"), Rgb.Equals(FVector3f::OneVector, Tolerance));
	// BT.601 전체 범위 순수 파랑: Y=29, Cb=255, Cr=107
	Rgb = Apply(Full601, 29.f / 255.f, 1.f, 107.f / 255.f);
	TestTrue(TEXT("BT.601 파랑"), Rgb.Equals(FVector3f(0.f, 0.f, 1.f), 0.02f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvFrameMetadataTest, "DTCctv.Policies.FrameMetadata", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvFrameMetadataTest::RunTest(const FString& Parameters)
{
	AVFrame* Frame = av_frame_alloc();
	if (!TestNotNull(TEXT("프레임 할당"), Frame))
	{
		return false;
	}
	Frame->format = AV_PIX_FMT_YUV420P;
	Frame->width = 480;
	Frame->height = 270;
	Frame->colorspace = AVCOL_SPC_UNSPECIFIED;
	const FDTCctvVideoFrame VideoFrame(Frame, FIntPoint(1920, 1080), 123.0);
	TestEqual(TEXT("CPU 변환 전 디코딩 완료 시각 보존"), VideoFrame.GetDecodedSeconds(), 123.0);
	TestTrue(TEXT("축소 프레임도 원본 크기 보존"), VideoFrame.GetSourceSize() == FIntPoint(1920, 1080));
	const FDTCctvColorTransform Expected = DTCctvMakeColorTransform(EDTCctvYuvMatrix::Bt709, false);
	TestTrue(TEXT("색 공간 미지정 HD 원본은 축소 후에도 BT.709"), VideoFrame.GetPlanes().Transform.RowR.Equals(Expected.RowR, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvOutputWatchdogTest, "DTCctv.Policies.OutputWatchdog", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvOutputWatchdogTest::RunTest(const FString& Parameters)
{
	using FAction = EDTCctvOutputWatchdogAction;
	for (const bool bHardware : { false, true })
	{
		TestTrue(TEXT("키프레임 전 10초에는 재연결하지 않음"), DTCctvEvaluateOutputWatchdog(110.0, 100.0, 0.0, 0.0, 5.0, bHardware) == FAction::None);
		TestTrue(TEXT("키프레임 전 30초 직전 대기"), DTCctvEvaluateOutputWatchdog(129.999, 100.0, 0.0, 0.0, 5.0, bHardware) == FAction::None);
		TestTrue(TEXT("키프레임 전 30초에는 HW도 일반 재연결"), DTCctvEvaluateOutputWatchdog(130.0, 100.0, 0.0, 0.0, 5.0, bHardware) == FAction::Reconnect);
		TestTrue(TEXT("키프레임 없이 오래 지나도 SW 강제 없음"), DTCctvEvaluateOutputWatchdog(160.0, 100.0, 0.0, 0.0, 5.0, bHardware) == FAction::Reconnect);
	}
	TestTrue(TEXT("늦은 키프레임 도착 뒤 새로 10초 대기"), DTCctvEvaluateOutputWatchdog(138.999, 100.0, 129.0, 0.0, 5.0, true) == FAction::None);
	TestTrue(TEXT("키프레임 뒤 10초 무출력이면 HW 폴백"), DTCctvEvaluateOutputWatchdog(139.0, 100.0, 129.0, 0.0, 5.0, true) == FAction::RetrySoftware);
	TestTrue(TEXT("SW 무출력은 일반 재연결"), DTCctvEvaluateOutputWatchdog(139.0, 100.0, 129.0, 0.0, 5.0, false) == FAction::Reconnect);
	TestTrue(TEXT("ReadTimeout이 10초보다 길면 그만큼 대기"), DTCctvEvaluateOutputWatchdog(148.999, 100.0, 129.0, 0.0, 20.0, true) == FAction::None);
	TestTrue(TEXT("긴 ReadTimeout 경계에서 HW 폴백"), DTCctvEvaluateOutputWatchdog(149.0, 100.0, 129.0, 0.0, 20.0, true) == FAction::RetrySoftware);
	TestTrue(TEXT("재생 뒤에는 마지막 출력부터 읽기 기한 적용"), DTCctvEvaluateOutputWatchdog(154.999, 100.0, 129.0, 150.0, 5.0, true) == FAction::None);
	TestTrue(TEXT("재생 뒤 출력 중단은 HW 폴백"), DTCctvEvaluateOutputWatchdog(155.0, 100.0, 129.0, 150.0, 5.0, true) == FAction::RetrySoftware);
	TestTrue(TEXT("재생 뒤 SW 출력 중단은 일반 재연결"), DTCctvEvaluateOutputWatchdog(155.0, 100.0, 129.0, 150.0, 5.0, false) == FAction::Reconnect);
	TestTrue(TEXT("읽기 기한이 길어도 키프레임 대기는 30초"), DTCctvEvaluateOutputWatchdog(130.0, 100.0, 0.0, 0.0, 60.0, true) == FAction::Reconnect);
	TestTrue(TEXT("키프레임 표식 전에는 출력 이력이 있어도 SW 강제 없음"), DTCctvEvaluateOutputWatchdog(155.0, 100.0, 0.0, 150.0, 5.0, true) == FAction::Reconnect);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvHwFallbackTest, "DTCctv.Policies.HwFormatFallback", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvHwFallbackTest::RunTest(const FString& Parameters)
{
	FDTCctvSession Session{ FDTCctvSessionParams() };
	AVCodecContext* Context = avcodec_alloc_context3(nullptr);
	if (!TestNotNull(TEXT("코덱 문맥 할당"), Context))
	{
		return false;
	}
	Context->opaque = &Session;
	Session.HwPixelFormat = AV_PIX_FMT_CUDA;
	const AVPixelFormat HardwareFormats[] = { AV_PIX_FMT_CUDA, AV_PIX_FMT_NV12, AV_PIX_FMT_NONE };
	TestTrue(TEXT("지원되는 하드웨어 형식 선택"), FDTCctvSession::GetFormatCallback(Context, HardwareFormats) == AV_PIX_FMT_CUDA);
	TestFalse(TEXT("정상 협상은 오류 아님"), Session.bHwFailed.load());
	const AVPixelFormat SoftwareFormats[] = { AV_PIX_FMT_NV12, AV_PIX_FMT_NONE };
	TestTrue(TEXT("하드웨어 형식 제거 후 소프트웨어 선택"), FDTCctvSession::GetFormatCallback(Context, SoftwareFormats) == AV_PIX_FMT_NV12);
	const AVPixelFormat PlanarFormats[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
	TestTrue(TEXT("I420 단독 폴백 선택"), FDTCctvSession::GetFormatCallback(Context, PlanarFormats) == AV_PIX_FMT_YUV420P);
	TestFalse(TEXT("사용 가능한 소프트웨어 형식은 재접속 오류가 아님"), Session.bHwFailed.load());
	TestTrue(TEXT("소프트웨어 협상 상태를 유지"), Session.bSoftwareFormatFallback.load());
	TestTrue(TEXT("같은 연결에서는 실패한 하드웨어 협상을 반복하지 않음"), FDTCctvSession::GetFormatCallback(Context, HardwareFormats) == AV_PIX_FMT_NV12);
	TestEqual(TEXT("정상 소프트웨어 폴백은 재접속하지 않음"), Session.Reconnects.load(), 0);
	FDTCctvStreamStats Stats;
	Session.FillStats(Stats);
	TestEqual(TEXT("실제 폴백을 디코더 통계에 표시"), Stats.DecoderName, FString(TEXT("software")));
	const AVPixelFormat EmptyFormats[] = { AV_PIX_FMT_NONE };
	TestTrue(TEXT("사용 가능한 형식이 없으면 실패 반환"), FDTCctvSession::GetFormatCallback(Context, EmptyFormats) == AV_PIX_FMT_NONE);
	TestTrue(TEXT("진짜 협상 실패는 읽기 스레드에 전달"), Session.bHwFailed.load());
	avcodec_free_context(&Context);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvFrameBurstTest, "DTCctv.Policies.FrameBurstBuffer", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvFrameBurstTest::RunTest(const FString& Parameters)
{
	FDTCctvSession Session{ FDTCctvSessionParams() };
	AVFrame* Frame = av_frame_alloc();
	if (!TestNotNull(TEXT("프레임 할당"), Frame))
	{
		return false;
	}
	Frame->format = AV_PIX_FMT_YUV420P;
	Frame->width = 16;
	Frame->height = 16;
	if (av_frame_get_buffer(Frame, 32) < 0)
	{
		av_frame_free(&Frame);
		return false;
	}
	for (int32 Index = 0; Index < 8; ++Index)
	{
		if (av_frame_make_writable(Frame) < 0)
		{
			av_frame_free(&Frame);
			return false;
		}
		Frame->data[0][0] = static_cast<uint8>(Index);
		Session.OutputFrame(Frame);
	}
	FDTCctvStreamStats Stats;
	Session.FillStats(Stats);
	TestEqual(TEXT("짧은 지연의 8프레임을 순서대로 보존"), Stats.QueuedFrames, 8);
	TestEqual(TEXT("버스트 대체 없음"), Stats.FramesSuperseded, int64(0));
	const int64 BufferBytes = Stats.QueuedFrameBytes;
	TestTrue(TEXT("대기 프레임 메모리 계측"), BufferBytes > 0);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		FDTCctvVideoFramePtr Output = Session.TakeFrame();
		if (!TestTrue(TEXT("대기 프레임 존재"), Output.IsValid()))
		{
			av_frame_free(&Frame);
			return false;
		}
		TestEqual(TEXT("FIFO 프레임 내용"), Output->GetPlanes().Data[0][0], static_cast<uint8>(Index));
	}
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Session.OutputFrame(Frame);
	}
	Session.FillStats(Stats);
	TestEqual(TEXT("프레임 수 상한 유지"), Stats.QueuedFrames, 8);
	TestEqual(TEXT("상한 초과는 기존 이름으로 집계"), Stats.FramesSuperseded, int64(1));
	// FFmpeg의 writable 복사는 처음 할당과 버퍼 패딩 크기가 다를 수 있다.
	TestEqual(TEXT("대체 후8개 실제 버퍼 크기를 정확히 집계"), Stats.QueuedFrameBytes, int64(8) * static_cast<int64>(Frame->buf[0]->size));
	av_frame_free(&Frame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvFrameAgeTest, "DTCctv.Policies.FrameAge", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvFrameAgeTest::RunTest(const FString& Parameters)
{
	FDTCctvFrameAgeAccumulator Age;
	for (int32 Index = 0; Index < 19; ++Index)
	{
		Age.Record(0.1);
	}
	Age.Record(100.0);
	FDTCctvStreamStats Stats;
	Age.FillStats(Stats);
	TestEqual(TEXT("모든 프레임 집계"), Stats.FrameAgeSamples, int64(20));
	TestEqual(TEXT("p95는 보수적인 구간 상한"), Stats.FrameAgeP95Ms, 1.0);
	TestEqual(TEXT("최대 지연 보존"), Stats.FrameAgeMaxMs, 100.0);
	TestTrue(TEXT("평균은 실제 합으로 계산"), FMath::IsNearlyEqual(Stats.FrameAgeMeanMs, 5.095, 0.00001));
	FDTCctvFrameAgeAccumulator Overflow;
	Overflow.Record(2000.0);
	FDTCctvStreamStats Other;
	Overflow.FillStats(Other);
	TestEqual(TEXT("1초 초과를 작은 값으로 포화시키지 않음"), Other.FrameAgeP95Ms, 2000.0);
	DTCctvMergeFrameAgeStats(Stats, Other);
	TestEqual(TEXT("세션 합산 후 표본 수"), Stats.FrameAgeSamples, int64(21));
	TestEqual(TEXT("합산 p95 재계산"), Stats.FrameAgeP95Ms, 100.0);
	DTCctvClearFrameAgeStats(Stats);
	TestEqual(TEXT("워밍업 제외 후 표본 초기화"), Stats.FrameAgeSamples, int64(0));
	TestTrue(TEXT("히스토그램 초기화"), Stats.FrameAgeHistogram.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvLatencyPolicyTest, "DTCctv.Policies.LatencyBound", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvLatencyPolicyTest::RunTest(const FString& Parameters)
{
	const double Burst[] = { 0.055, 0.040, 0.025, 0.010 };
	TestEqual(TEXT("짧은 버스트 순서 보존"), DTCctvChooseStaleFrameSkip(Burst, 0.02), 0);
	const double Persistent[] = { 0.085, 0.068, 0.051, 0.034, 0.017 };
	TestEqual(TEXT("지속 대기는40ms 안쪽까지 따라잡음"), DTCctvChooseStaleFrameSkip(Persistent, 0.1), 3);
	const double Stalled[] = { 0.132, 0.115, 0.098, 0.081, 0.064, 0.047, 0.030, 0.013 };
	TestEqual(TEXT("정지 뒤에는 지속 시간 전에도 age 상한 적용"), DTCctvChooseStaleFrameSkip(Stalled, 0.0), 6);
	const double Fresh[] = { 0.025, 0.008 };
	TestEqual(TEXT("오래 지속돼도 신선한 프레임은 보존"), DTCctvChooseStaleFrameSkip(Fresh, 1.0), 0);
	const double Only[] = { 2.0 };
	TestEqual(TEXT("새 프레임 없는 경우 마지막 표본을 숨기지 않음"), DTCctvChooseStaleFrameSkip(Only, 1.0), 0);
	const double Boundary[] = { 0.040, 0.024 };
	TestEqual(TEXT("40ms 경계 보존"), DTCctvChooseStaleFrameSkip(Boundary, 1.0), 0);
	TestTrue(TEXT("144/60은 따라잡을 여유 있음"), DTCctvCanCatchUp(1.0 / 144.0, 60.0));
	TestTrue(TEXT("60/30은 따라잡을 여유 있음"), DTCctvCanCatchUp(1.0 / 60.0, 30.0));
	TestFalse(TEXT("60/60은 지속 대기를 해소할 여유 없음"), DTCctvCanCatchUp(1.0 / 60.0, 60.0));
	TestFalse(TEXT("30/60은 건너뛰기가 필요"), DTCctvCanCatchUp(1.0 / 30.0, 60.0));
	TestEqual(TEXT("따라잡는 중인 버스트는 보존"), DTCctvChooseStaleFrameSkip(Stalled, 0.2, true), 0);
	TestEqual(TEXT("여유 추정이 틀려도 유예는 유한함"), DTCctvChooseStaleFrameSkip(Stalled, 0.75, true), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvPreparedSizeTest, "DTCctv.Policies.InitialPreparedSize", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvPreparedSizeTest::RunTest(const FString& Parameters)
{
	FDTCctvSession Session{ FDTCctvSessionParams() };
	Session.SetDesiredOutputSize(FIntPoint(480, 270));
	AVFrame* Frame = av_frame_alloc();
	if (!TestNotNull(TEXT("원본 프레임 할당"), Frame))
	{
		return false;
	}
	Frame->format = AV_PIX_FMT_YUV420P;
	Frame->width = 1920;
	Frame->height = 1440;
	if (av_frame_get_buffer(Frame, 32) < 0)
	{
		av_frame_free(&Frame);
		return false;
	}
	Session.OutputFrame(Frame);
	FDTCctvVideoFramePtr Output = Session.TakeFrame();
	if (TestTrue(TEXT("첫 출력 존재"), Output.IsValid()))
	{
		TestEqual(TEXT("4:3 원본 비율을 유지한 첫 프레임"), Output->GetSize(), FIntPoint(360, 270));
		TestEqual(TEXT("축소 전 원본 메타데이터 보존"), Output->GetSourceSize(), FIntPoint(1920, 1440));
		TestTrue(TEXT("원본8장 임시 할당 대신 작은 버퍼 사용"), Output->GetBufferBytes() < 1024 * 1024);
	}
	FDTCctvStreamStats Stats;
	Session.FillStats(Stats);
	TestEqual(TEXT("소비 후 대기 버퍼 해제"), Stats.QueuedFrameBytes, int64(0));
	av_frame_free(&Frame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvFrameMemoryTest, "DTCctv.Policies.FrameMemoryCap", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvFrameMemoryTest::RunTest(const FString& Parameters)
{
	for (const FIntPoint Size : { FIntPoint(1920, 1080), FIntPoint(3840, 2160) })
	{
		FDTCctvSession Session{ FDTCctvSessionParams() };
		AVFrame* Frame = av_frame_alloc();
		if (!TestNotNull(TEXT("메모리 상한 시험 프레임"), Frame))
		{
			return false;
		}
		Frame->format = AV_PIX_FMT_YUV420P;
		Frame->width = Size.X;
		Frame->height = Size.Y;
		if (av_frame_get_buffer(Frame, 32) < 0)
		{
			av_frame_free(&Frame);
			return false;
		}
		const int32 ExpectedFrames = Size.X == 1920 ? 8 : 2;
		for (int32 Index = 0; Index <= ExpectedFrames; ++Index)
		{
			Session.OutputFrame(Frame);
		}
		FDTCctvStreamStats Stats;
		Session.FillStats(Stats);
		TestEqual(TEXT("큰 프레임은 바이트 예산으로 먼저 제한"), Stats.QueuedFrames, ExpectedFrames);
		TestTrue(TEXT("HD·4K 대기 버퍼24MiB 상한 유지"), Stats.QueuedFrameBytes <= 24 * 1024 * 1024);
		TestEqual(TEXT("예산 초과도 기존 대체 집계에 포함"), Stats.FramesSuperseded, int64(1));
		av_frame_free(&Frame);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvFrameBackpressureTest, "DTCctv.Policies.FrameBackpressure", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvFrameBackpressureTest::RunTest(const FString& Parameters)
{
	FDTCctvSession Session{ FDTCctvSessionParams() };
	AVFrame* Prime = av_frame_alloc();
	if (!TestNotNull(TEXT("실제 소비를 시작할 프레임"), Prime))
	{
		return false;
	}
	Prime->format = AV_PIX_FMT_YUV420P;
	Prime->width = Prime->height = 16;
	if (av_frame_get_buffer(Prime, 32) < 0)
	{
		av_frame_free(&Prime);
		return false;
	}
	Session.OutputFrame(Prime);
	Session.TakeFrame(true);
	av_frame_free(&Prime);
	TFuture<bool> Producer = Async(EAsyncExecution::Thread, [&Session]()
	{
		AVFrame* Frame = av_frame_alloc();
		if (Frame == nullptr)
		{
			return false;
		}
		Frame->format = AV_PIX_FMT_YUV420P;
		Frame->width = Frame->height = 16;
		bool bOk = av_frame_get_buffer(Frame, 32) >= 0;
		for (int32 Index = 0; bOk && Index < 16; ++Index)
		{
			bOk = av_frame_make_writable(Frame) >= 0;
			if (bOk)
			{
				Frame->data[0][0] = static_cast<uint8>(Index);
				Session.OutputFrame(Frame);
			}
		}
		av_frame_free(&Frame);
		return bOk;
	});
	const double Deadline = FPlatformTime::Seconds() + 3.0;
	FDTCctvStreamStats Stats;
	do
	{
		Session.FillStats(Stats);
		FPlatformProcess::SleepNoStats(0.001f);
	} while (Stats.QueuedFrames < 8 && FPlatformTime::Seconds() < Deadline);
	TestEqual(TEXT("생산 버스트도 대기열은8장"), Stats.QueuedFrames, 8);
	int32 Received = 0;
	while (Received < 16 && FPlatformTime::Seconds() < Deadline)
	{
		if (FDTCctvVideoFramePtr Frame = Session.TakeFrame(true))
		{
			TestEqual(TEXT("생산 대기 후에도 FIFO 내용 보존"), Frame->GetPlanes().Data[0][0], static_cast<uint8>(Received++));
		}
		else
		{
			FPlatformProcess::SleepNoStats(0.001f);
		}
	}
	Session.RequestStop();
	const bool bProducerOk = Producer.Get();
	Session.FillStats(Stats);
	TestTrue(TEXT("생산 스레드 정상 종료"), bProducerOk);
	TestEqual(TEXT("16개 버스트 모두 소비"), Received, 16);
	TestEqual(TEXT("소비 여유가 있으면 버스트 대체 없음"), Stats.FramesSuperseded, int64(0));
	TestEqual(TEXT("소비 뒤 대기 메모리 없음"), Stats.QueuedFrameBytes, int64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvConsumerLeaseTest, "DTCctv.Policies.ConsumerLease", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvConsumerLeaseTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("정상 소비와 짧은 생산 버스트"), DTCctvShouldBackpressure(10.0, 9.99, 9.98, 9.9, true));
	TestTrue(TEXT("0.3초 정지 버스트의 보존 여유"), DTCctvShouldBackpressure(10.0, 9.69, 9.69, 9.8, true));
	TestFalse(TEXT("GT 호출이 멈추면 상태값이참이어도 대기안함"), DTCctvShouldBackpressure(10.0, 9.64, 9.64, 9.9, true));
	TestFalse(TEXT("빈호출만 있고 실제소비가없으면 대기안함"), DTCctvShouldBackpressure(10.0, 9.99, 9.2, 9.9, true));
	TestFalse(TEXT("소비를 시작하기 전에는 대기안함"), DTCctvShouldBackpressure(10.0, 9.99, 0.0, 9.9, true));
	TestFalse(TEXT("렌더정체알림은 즉시 대기해제"), DTCctvShouldBackpressure(10.0, 9.99, 9.98, 9.9, false));
	TestFalse(TEXT("프레임별 대기기한도 유지"), DTCctvShouldBackpressure(10.0, 9.99, 9.98, 9.2, true));
	TestFalse(TEXT("역행한 시각은 활성증거가 아님"), DTCctvShouldBackpressure(10.0, 10.1, 9.98, 9.9, true));
	return true;
}

#endif
