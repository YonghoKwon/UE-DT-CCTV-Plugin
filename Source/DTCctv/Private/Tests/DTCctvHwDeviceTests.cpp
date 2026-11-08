#include "DTCctvHwDevice.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCctvHwDeviceLifetimeTest, "DTCctv.Hardware.CacheLifetime",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FDTCctvHwDeviceLifetimeTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("CUDA 캐시는 모듈 종료까지 유지"),
		DTCctvHwDevice::ShouldReleaseUnused(AV_HWDEVICE_TYPE_CUDA));
	TestTrue(TEXT("D3D11VA 미사용 캐시 해제 유지"),
		DTCctvHwDevice::ShouldReleaseUnused(AV_HWDEVICE_TYPE_D3D11VA));
	TestTrue(TEXT("VAAPI 미사용 캐시 해제 유지"),
		DTCctvHwDevice::ShouldReleaseUnused(AV_HWDEVICE_TYPE_VAAPI));
	return true;
}
#endif
