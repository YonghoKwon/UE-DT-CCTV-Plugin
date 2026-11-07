#include "DTCctvFFmpegApi.h"
#include "DTCctvStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	struct FLoadProbe
	{
		int32 Opened = 0;
		int32 Resolved = 0;
		TArray<UPTRINT> Closed;
		FString MissingFile;
		FString MissingFunction;

		FDTCctvFFmpegLoadOps Ops()
		{
			FDTCctvFFmpegLoadOps Result;
			Result.Open = [this](const FString& Path, FString& Error) -> void*
			{
				if (!MissingFile.IsEmpty() && Path.Contains(MissingFile))
				{
					Error = TEXT("필수 파일 없음: ") + Path;
					return nullptr;
				}
				return reinterpret_cast<void*>(static_cast<UPTRINT>(++Opened));
			};
			Result.Resolve = [this](void*, const ANSICHAR* Name) -> void*
			{
				++Resolved;
				return MissingFunction == UTF8_TO_TCHAR(Name) ? nullptr : reinterpret_cast<void*>(static_cast<UPTRINT>(1000 + Resolved));
			};
			Result.Close = [this](void* Handle)
			{
				Closed.Add(reinterpret_cast<UPTRINT>(Handle));
			};
			return Result;
		}
	};

	struct FScopedLoadFailure
	{
		explicit FScopedLoadFailure(const FString& Error)
		{
			DTCctvSetFFmpegTestFailure(Error);
		}
		~FScopedLoadFailure()
		{
			DTCctvSetFFmpegTestFailure(FString());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvMissingFileTest, "DTCctv.Loader.MissingFile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCctvMissingFileTest::RunTest(const FString&)
{
	FLoadProbe Probe;
	Probe.MissingFile = TEXT("avcodec");
	FDTCctvFFmpegFunctions Functions;
	TArray<void*> Handles;
	FString Error;
	TestFalse(TEXT("파일 누락 실패"), DTCctvResolveFFmpeg(Functions, Handles, Error, TEXT("test"), Probe.Ops()));
	TestTrue(TEXT("누락 파일 식별"), Error.Contains(TEXT("avcodec")));
	TestEqual(TEXT("앞선3개 해제"), Probe.Closed.Num(), 3);
	TestEqual(TEXT("역순 첫 해제"), Probe.Closed[0], static_cast<UPTRINT>(3));
	TestTrue(TEXT("부분 함수 표 없음"), Functions.av_frame_alloc == nullptr && Handles.IsEmpty());
	AddExpectedError(TEXT("FFmpeg 로드 준비 실패:"), EAutomationExpectedErrorFlags::Contains, 1);
	FScopedLoadFailure Failure(Error);
	UDTCctvStream* Stream = NewObject<UDTCctvStream>();
	FDTCctvStreamConfig Config;
	Config.Url = TEXT("rtsp://test.invalid/stream");
	Stream->Open(Config);
	TestFalse(TEXT("실패 스트림 닫힘"), Stream->IsOpen());
	TestEqual(TEXT("오류 종료 상태"), Stream->GetState(), EDTCctvState::Idle);
	TestTrue(TEXT("파일 오류 전달"), Stream->GetStats().LastError.Contains(TEXT("avcodec")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvMissingFunctionTest, "DTCctv.Loader.MissingFunction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCctvMissingFunctionTest::RunTest(const FString&)
{
	FLoadProbe Probe;
	Probe.MissingFunction = TEXT("avcodec_open2");
	FDTCctvFFmpegFunctions Functions;
	TArray<void*> Handles;
	FString Error;
	TestFalse(TEXT("함수 누락 실패"), DTCctvResolveFFmpeg(Functions, Handles, Error, TEXT("test"), Probe.Ops()));
	TestTrue(TEXT("누락 함수 식별"), Error.Contains(TEXT("avcodec_open2")));
	TestEqual(TEXT("로드된5개 해제"), Probe.Closed.Num(), 5);
	TestEqual(TEXT("역순 첫 해제"), Probe.Closed[0], static_cast<UPTRINT>(5));
	TestTrue(TEXT("부분 함수 표 없음"), Functions.av_frame_alloc == nullptr && Handles.IsEmpty());
	AddExpectedError(TEXT("FFmpeg 로드 준비 실패:"), EAutomationExpectedErrorFlags::Contains, 1);
	FScopedLoadFailure Failure(Error);
	UDTCctvStream* Stream = NewObject<UDTCctvStream>();
	FDTCctvStreamConfig Config;
	Config.Url = TEXT("rtsp://test.invalid/stream");
	Stream->Open(Config);
	TestFalse(TEXT("실패 스트림 닫힘"), Stream->IsOpen());
	TestTrue(TEXT("함수 오류 전달"), Stream->GetStats().LastError.Contains(TEXT("avcodec_open2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvCompleteLoadTableTest, "DTCctv.Loader.CompleteTable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCctvCompleteLoadTableTest::RunTest(const FString&)
{
	FLoadProbe Probe;
	FDTCctvFFmpegFunctions Functions;
	TArray<void*> Handles;
	FString Error;
	TestTrue(TEXT("전체 표 확인"), DTCctvResolveFFmpeg(Functions, Handles, Error, TEXT("test"), Probe.Ops()));
	TestEqual(TEXT("실제 오브젝트 함수52개"), Probe.Resolved, 52);
	TestEqual(TEXT("라이브러리5개"), Handles.Num(), 5);
	TestTrue(TEXT("함수 주소 채움"), Functions.avcodec_open2 != nullptr && Functions.sws_scale != nullptr);
	TestTrue(TEXT("오류 없음"), Error.IsEmpty());
	return true;
}
#endif
