#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DTCctvFFmpegInclude.h"
#include "DTCctvPacketQueue.h"
#include "DTCctvPolicies.h"
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

#endif
