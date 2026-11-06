#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DTCctvSettings.h"
#include "DTCctvWindowsTimerPolicy.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvTimerSettingsTest, "DTCctv.WindowsTimer.Settings", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvTimerSettingsTest::RunTest(const FString& Parameters)
{
	const UDTCctvSettings* Settings = NewObject<UDTCctvSettings>();
	TestFalse(TEXT("기본 꺼짐"), Settings->bIgnoreWindowsTimerThrottling);
	const FProperty* Property = FindFProperty<FProperty>(UDTCctvSettings::StaticClass(), GET_MEMBER_NAME_CHECKED(UDTCctvSettings, bIgnoreWindowsTimerThrottling));
	TestNotNull(TEXT("프로젝트 설정 항목"), Property);
	if (Property != nullptr)
	{
		TestTrue(TEXT("설정 저장"), Property->HasAnyPropertyFlags(CPF_Config));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvTimerCommandLineTest, "DTCctv.WindowsTimer.CommandLine", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvTimerCommandLineTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("기본 꺼짐 유지"), DTCctvResolveTimerThrottling(false, TEXT("")).bEnabled);
	TestTrue(TEXT("설정 켜짐 유지"), DTCctvResolveTimerThrottling(true, nullptr).bEnabled);
	const FDTCctvTimerThrottlingOption Enabled = DTCctvResolveTimerThrottling(false, TEXT("-unattended -DTCctvIgnoreTimerThrottling=1 -nosound"));
	TestTrue(TEXT("명령줄 켜짐"), Enabled.bEnabled && Enabled.bCommandLineOverride && Enabled.bValidCommandLine);
	TestFalse(TEXT("설정 켜짐을 명령줄로 끔"), DTCctvResolveTimerThrottling(true, TEXT("-DTCctvIgnoreTimerThrottling=0")).bEnabled);
	TestTrue(TEXT("대소문자 무관"), DTCctvResolveTimerThrottling(false, TEXT("-dtcctvignoretimerthrottling=1")).bEnabled);
	TestFalse(TEXT("비슷한 이름 무시"), DTCctvResolveTimerThrottling(false, TEXT("-OtherDTCctvIgnoreTimerThrottling=1 -DTCctvIgnoreTimerThrottlingExtra=1")).bEnabled);
	TestFalse(TEXT("마지막 유효 지정 우선"), DTCctvResolveTimerThrottling(true, TEXT("-DTCctvIgnoreTimerThrottling=1 -DTCctvIgnoreTimerThrottling=0")).bEnabled);
	for (const TCHAR* Invalid : { TEXT(""), TEXT("true"), TEXT("false"), TEXT("2"), TEXT("10"), TEXT("-1") })
	{
		const FString Command = FString(TEXT("-DTCctvIgnoreTimerThrottling=")) + Invalid;
		const FDTCctvTimerThrottlingOption Off = DTCctvResolveTimerThrottling(false, *Command);
		const FDTCctvTimerThrottlingOption On = DTCctvResolveTimerThrottling(true, *Command);
		TestTrue(TEXT("잘못된 값은 설정으로 복귀"), !Off.bEnabled && On.bEnabled && !Off.bValidCommandLine && !Off.bCommandLineOverride);
	}
	TestFalse(TEXT("값 없는 옵션 거부"), DTCctvResolveTimerThrottling(false, TEXT("-DTCctvIgnoreTimerThrottling")).bValidCommandLine);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvTimerMaskTest, "DTCctv.WindowsTimer.PreserveExecutionPolicy", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvTimerMaskTest::RunTest(const FString& Parameters)
{
	for (uint32 OriginalControl : { 0u, 1u, 4u, 5u, 0x81u })
	{
		for (uint32 OriginalState : { 0u, 1u, 4u, 5u, 0x81u })
		{
			uint32 Control = OriginalControl;
			uint32 State = OriginalState;
			DTCctvTimerPowerMasks(Control, State);
			TestEqual(TEXT("실행 속도와 다른 제어 비트 보존"), Control & ~4u, OriginalControl & ~4u);
			TestEqual(TEXT("실행 속도와 다른 상태 비트 보존"), State & ~4u, OriginalState & ~4u);
			TestEqual(TEXT("타이머 정책만 명시"), Control & 4u, 4u);
			TestEqual(TEXT("타이머 요청 무시 해제"), State & 4u, 0u);
		}
	}
	return true;
}

#endif
