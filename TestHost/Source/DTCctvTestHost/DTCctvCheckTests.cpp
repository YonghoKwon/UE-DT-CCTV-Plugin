#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DTCctvCheckPolicy.h"
#include "DTCctvCheckSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvCheckBoundaryTest, "DTCctv.Check.VerdictBoundaries",
								 EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvCheckBoundaryTest::RunTest(const FString& Parameters)
{
	FDTCctvCheckObservation Observation;
	Observation.bEverPlayed = true;
	Observation.FirstFrameSeconds = 1.0;
	Observation.WindowSeconds = 10.0;
	Observation.Current.State = EDTCctvState::Playing;
	Observation.Current.FramesDecoded = 300;
	Observation.Current.FramesDisplayed = 291;
	Observation.Info.DeclaredFps = 30.0;
	Observation.Info.DecoderName = TEXT("cuda");
	TestTrue(TEXT("정확히97% PASS"), DTCctvEvaluateCheck(Observation).Verdict == EDTCctvCheckVerdict::Pass);
	Observation.Current.FramesDisplayed = 290;
	TestTrue(TEXT("97% 미만 WARN"), DTCctvEvaluateCheck(Observation).Verdict == EDTCctvCheckVerdict::Warn);
	Observation.Current.FramesDisplayed = 300;
	Observation.Current.Reconnects = 1;
	TestTrue(TEXT("재연결 WARN"), DTCctvEvaluateCheck(Observation).Verdict == EDTCctvCheckVerdict::Warn);
	Observation.Current.Reconnects = 0;
	Observation.Info.DecoderName = TEXT("software");
	TestTrue(TEXT("SW 진단 WARN"), DTCctvEvaluateCheck(Observation).Verdict == EDTCctvCheckVerdict::Warn);
	Observation.Info.DecoderName = TEXT("cuda");
	Observation.bDisplayAvailable = false;
	TestTrue(TEXT("NullRHI 표시 미검증 WARN"), DTCctvEvaluateCheck(Observation).Verdict == EDTCctvCheckVerdict::Warn);
	Observation.bDisplayAvailable = true;
	Observation.bInterrupted = true;
	const FDTCctvCheckResult Interrupted = DTCctvEvaluateCheck(Observation);
	TestTrue(TEXT("재생 중단 FAIL"), Interrupted.Verdict == EDTCctvCheckVerdict::Fail);
	TestEqual(TEXT("FAIL에도 관측 FPS 유지"), Interrupted.DisplayFps, 30.0);
	Observation.bInterrupted = false;
	Observation.bEverPlayed = false;
	Observation.Error = TEXT("인증 실패(401)");
	const FDTCctvCheckResult Failed = DTCctvEvaluateCheck(Observation);
	TestTrue(TEXT("연결 실패 FAIL"), Failed.Verdict == EDTCctvCheckVerdict::Fail);
	TestTrue(TEXT("원인 유지"), Failed.Reasons[0].Contains(TEXT("401")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvCheckCredentialTest, "DTCctv.Check.CredentialsAndQuery",
								 EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvCheckCredentialTest::RunTest(const FString& Parameters)
{
	const FString Secret = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString User = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Raw =
		TEXT("rtsp://") + User + TEXT(":") + Secret + TEXT("@camera.invalid/live?%70assword=") + Secret + TEXT("&channel=1");
	const FString Masked = DTCctvCheckSafeUrl(Raw);
	TestFalse(TEXT("계정 숨김"), Masked.Contains(User));
	TestFalse(TEXT("userinfo·query 비밀번호 숨김"), Masked.Contains(Secret));
	TestTrue(TEXT("일반 query 유지"), Masked.Contains(TEXT("channel=1")));
	FString Composed;
	FString Error;
	TestTrue(TEXT("분리 계정 입력"),
			 DTCctvCheckComposeUrl(TEXT("rtsp://camera.invalid/live?channel=1"), TEXT("unit user"), Secret + TEXT("@"), Composed, Error));
	TestTrue(TEXT("userinfo 인코딩"), Composed.Contains(TEXT("unit%20user:")) && Composed.Contains(TEXT("%40@")));
	TestFalse(TEXT("가린 구성 URL"), DTCctvCheckSafeUrl(Composed).Contains(Secret));
	const FString Nested = TEXT("rtsp://camera.invalid/live?redirect=rtsp://") + User + TEXT(":") + Secret + TEXT("@other.invalid/live");
	TestFalse(TEXT("중첩 URL 숨김"), DTCctvMaskUrlCredentials(Nested).Contains(Secret));
	TestFalse(TEXT("잘못된 주소 평문 생략"), DTCctvCheckSafeUrl(Secret).Contains(Secret));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvCheckStorageTest, "DTCctv.Check.SafeStorage",
								 EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvCheckStorageTest::RunTest(const FString& Parameters)
{
	const FString User = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Secret = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDTCctvCheckEndpoint Entry;
	Entry.Url = TEXT("rtsp://") + User + TEXT(":") + Secret + TEXT("@camera.invalid/live?password=") + Secret + TEXT("&channel=3");
	Entry.SubUrl = Entry.Url;
	Entry.Username = User;
	Entry.Password = Secret;
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/CctvCheck");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString File = Directory / (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	TestTrue(TEXT("선택 주소 저장"), DTCctvSaveCheckList(File, {Entry}));
	FString Text;
	FFileHelper::LoadFileToString(Text, *File);
	TestFalse(TEXT("계정 저장 제외"), Text.Contains(User));
	TestFalse(TEXT("비밀번호 저장 제외"), Text.Contains(Secret));
	TestTrue(TEXT("카메라 선택 query 보존"), Text.Contains(TEXT("channel=3")));
	TArray<FDTCctvCheckEndpoint> Reloaded;
	FString Error;
	TestTrue(TEXT("저장한 목록 재해석"), DTCctvLoadCheckList(File, Reloaded, Error));
	TestEqual(TEXT("주소1개"), Reloaded.Num(), 1);
	if (!Reloaded.IsEmpty())
	{
		TestTrue(TEXT("계정 재입력 필요"), Reloaded[0].Username.IsEmpty() && Reloaded[0].Password.IsEmpty());
	}
	IFileManager::Get().Delete(*File, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvCheckLimitTest, "DTCctv.Check.AddressLimitAndRemoval",
								 EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvCheckLimitTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>();
	UDTCctvCheckSubsystem* Checker = NewObject<UDTCctvCheckSubsystem>(Instance);
	FDTCctvCheckEndpoint Entry;
	Entry.Url = TEXT("rtsp://camera.invalid/live");
	for (int32 Index = 0; Index < 16; ++Index)
	{
		TestTrue(TEXT("16개까지 추가"), Checker->AddEndpoint(Entry));
	}
	TestFalse(TEXT("17번째 거부"), Checker->AddEndpoint(Entry));
	Checker->RemoveEndpoint(5);
	TestEqual(TEXT("삭제 반영"), Checker->GetEndpoints().Num(), 15);
	TestTrue(TEXT("삭제 후 추가"), Checker->AddEndpoint(Entry));
	return true;
}

#endif
