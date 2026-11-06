#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DTCctvLog.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvLogRouteTest, "DTCctv.Logging.RouteAndLifetime", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FDTCctvLogRouteTest::RunTest(const FString& Parameters)
{
	FDTCctvLogRoute Route = DTCctvChooseLogRoute(true, true, true, false, false);
	TestTrue(TEXT("에디터 UE"), Route.bUE);
	TestFalse(TEXT("에디터 DTCore 없음"), Route.bDTCore);
	Route = DTCctvChooseLogRoute(false, false, true, false, false);
	TestTrue(TEXT("DTCore 없는 패키지"), Route.bUE && !Route.bDTCore);
	Route = DTCctvChooseLogRoute(false, true, false, false, false);
	TestTrue(TEXT("GI/서브시스템 미준비"), Route.bUE && !Route.bDTCore);
	Route = DTCctvChooseLogRoute(false, true, true, true, false);
	TestTrue(TEXT("종료 중 fallback"), Route.bUE && !Route.bDTCore);
	Route = DTCctvChooseLogRoute(false, true, true, false, true);
	TestTrue(TEXT("기본 동시 기록"), Route.bUE && Route.bDTCore);
	Route = DTCctvChooseLogRoute(false, true, true, false, false);
	TestTrue(TEXT("UE 미러 끔"), !Route.bUE && Route.bDTCore);
	return true;
}

#endif
