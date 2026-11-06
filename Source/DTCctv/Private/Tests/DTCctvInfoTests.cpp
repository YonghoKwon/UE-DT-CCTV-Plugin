#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DTCctvInfoUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvInfoFormatTest, "DTCctv.Info.ProfileAndPixelFormat", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvInfoFormatTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("H264 High"), DTCctvProfileName(AV_CODEC_ID_H264, AV_PROFILE_H264_HIGH), FString(TEXT("High")));
	TestEqual(TEXT("HEVC Main10"), DTCctvProfileName(AV_CODEC_ID_HEVC, AV_PROFILE_HEVC_MAIN_10), FString(TEXT("Main 10")));
	TestEqual(TEXT("H264 level"), DTCctvLevelName(AV_CODEC_ID_H264, 41), FString(TEXT("4.1")));
	TestEqual(TEXT("HEVC level"), DTCctvLevelName(AV_CODEC_ID_HEVC, 120), FString(TEXT("4.0")));
	FDTCctvStreamInfo Info;
	DTCctvDescribePixelFormat(AV_PIX_FMT_P010LE, Info);
	TestEqual(TEXT("P010 깊이"), Info.BitDepth, 10);
	TestEqual(TEXT("P010 크로마"), Info.Chroma, FString(TEXT("4:2:0")));
	DTCctvDescribePixelFormat(AV_PIX_FMT_YUVJ422P, Info);
	TestEqual(TEXT("JPEG 깊이"), Info.BitDepth, 8);
	TestEqual(TEXT("JPEG 크로마"), Info.Chroma, FString(TEXT("4:2:2")));
	DTCctvDescribePixelFormat(AV_PIX_FMT_RGB24, Info);
	TestEqual(TEXT("RGB 구분"), Info.Chroma, FString(TEXT("RGB")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvInfoProbeTest, "DTCctv.Info.GopAndBitrate", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvInfoProbeTest::RunTest(const FString& Parameters)
{
	FDTCctvStreamProbe Probe;
	for (int32 Frame = 0; Frame <= 300; ++Frame)
	{
		Probe.Record(100.0 + Frame / 30.0, Frame / 30.0, Frame % 60 == 0, Frame % 3 == 1);
	}
	FDTCctvStreamInfo Info;
	Probe.Fill(Info, 7500000, 110.0);
	TestTrue(TEXT("10초 안정화"), Info.bStabilized);
	TestEqual(TEXT("실측30fps"), Info.MeasuredFps, 30.0);
	TestEqual(TEXT("payload6Mbps"), Info.MeasuredBitrateBps, static_cast<int64>(6000000));
	TestEqual(TEXT("GOP60"), Info.GopFrames, 60);
	TestEqual(TEXT("GOP2초"), Info.GopSeconds, 2.0);
	TestTrue(TEXT("실제B관측"), Info.bObservedBFrames);
	FDTCctvStreamProbe Waiting;
	Waiting.Record(100.0, 0.0, true, false);
	TestFalse(TEXT("10초 전"), Waiting.IsStable(109.999));
	Waiting.Fill(Info, 1, 110.0);
	TestFalse(TEXT("단일 키프레임으로 GOP 추정 안 함"), Info.bMeasuredGop);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvInfoSdpTest, "DTCctv.Info.SdpAndChanges", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvInfoSdpTest::RunTest(const FString& Parameters)
{
	FDTCctvStreamInfo Info;
	DTCctvParseSdpInfo(TEXT("v=0\r\ns=fixture\r\na=tool:local publisher\r\nm=video 0 RTP/AVP 96\r\nm=audio 0 RTP/AVP 97\r\na=rtpmap:97 MPEG4-GENERIC/48000/2\r\nm=video 0 RTP/AVP 98\r\n"), Info);
	TestTrue(TEXT("SDP 전체 관측"), Info.bSdpKnown);
	TestEqual(TEXT("비디오2개"), Info.VideoTracks, 2);
	TestEqual(TEXT("오디오1개"), Info.AudioTracks, 1);
	TestEqual(TEXT("도구 이름"), Info.ServerTool, FString(TEXT("local publisher")));
	TestEqual(TEXT("오디오 인코딩"), Info.AudioCodecs[0], FString(TEXT("MPEG4-GENERIC")));
	FDTCctvStreamInfo Changed = Info;
	Changed.CodecName = TEXT("hevc");
	const FString Difference = DTCctvInfoChanges(Info, Changed);
	TestTrue(TEXT("변경 코덱만 기록"), Difference.Contains(TEXT("코덱=hevc")));
	TestFalse(TEXT("같은 세션명 생략"), Difference.Contains(TEXT("세션=")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvErrorDescriptionTest, "DTCctv.Info.ErrorDescription", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvErrorDescriptionTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("입출력 제한 시간 중단의 실제 원인"), DTCctvDescribeError(AVERROR_EXIT, true).Contains(TEXT("시간 초과")));
	TestFalse(TEXT("명시 중단을 시간 초과로 오인하지 않음"), DTCctvDescribeError(AVERROR_EXIT, false).Contains(TEXT("시간 초과")));
	TestTrue(TEXT("잘못된 요청400과 경로 점검 안내"), DTCctvDescribeError(AVERROR_HTTP_BAD_REQUEST, false).Contains(TEXT("경로")));
	TestTrue(TEXT("인증401 유지"), DTCctvDescribeError(AVERROR_HTTP_UNAUTHORIZED, false).Contains(TEXT("인증 실패(401)")));
	TestTrue(TEXT("404 상태 유지"), DTCctvDescribeError(AVERROR_HTTP_NOT_FOUND, false).Contains(TEXT("404")));
	TestTrue(TEXT("알 수 없는 오류도 한국어·코드 제공"), DTCctvDescribeError(-123456, false).Contains(TEXT("처리 오류(코드 -123456)")));
	return true;
}

#endif
