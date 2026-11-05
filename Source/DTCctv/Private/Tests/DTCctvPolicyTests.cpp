#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DTCctvFFmpegInclude.h"
#include "DTCctvPacketQueue.h"
#include "DTCctvPolicies.h"
#include "DTCctvSession.h"
#include "DTCctvVideoFrame.h"
#include "DTCctvYuvRenderer.h"

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
	const FDTCctvVideoFrame VideoFrame(Frame, FIntPoint(1920, 1080));
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
	TestTrue(TEXT("읽기 스레드에 하드웨어 실패 전달"), Session.bHwFailed.load());
	FDTCctvStreamStats Stats;
	Session.FillStats(Stats);
	TestEqual(TEXT("실제 폴백을 디코더 통계에 표시"), Stats.DecoderName, FString(TEXT("software")));
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
	TestEqual(TEXT("상한은 유지"), Stats.QueuedFrames, 8);
	TestEqual(TEXT("상한 초과는 기존 이름으로 집계"), Stats.FramesSuperseded, int64(1));
	av_frame_free(&Frame);
	return true;
}

#endif
