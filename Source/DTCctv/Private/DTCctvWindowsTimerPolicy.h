#pragma once

#include "CoreMinimal.h"

struct FDTCctvTimerThrottlingOption
{
	bool bEnabled = false;
	bool bCommandLineOverride = false;
	bool bValidCommandLine = true;
};

FDTCctvTimerThrottlingOption DTCctvResolveTimerThrottling(bool bConfigured, const TCHAR* CommandLine);
void DTCctvTimerPowerMasks(uint32& ControlMask, uint32& StateMask);

class FDTCctvWindowsTimerPolicy
{
public:
	FString Initialize(bool bConfigured, const TCHAR* CommandLine, bool& bWarning);
	void Shutdown();

private:
	bool bApplied = false;
	uint32 OriginalControlMask = 0;
	uint32 OriginalStateMask = 0;
};
