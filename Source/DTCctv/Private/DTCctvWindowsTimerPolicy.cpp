#include "DTCctvWindowsTimerPolicy.h"

#include "DTCctvLog.h"
#include "Misc/Parse.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/WindowsPlatformMisc.h"
#endif

namespace
{
	constexpr uint32 TimerResolutionBit = 0x4;
}

FDTCctvTimerThrottlingOption DTCctvResolveTimerThrottling(bool bConfigured, const TCHAR* CommandLine)
{
	FDTCctvTimerThrottlingOption Result;
	Result.bEnabled = bConfigured;
	const TCHAR* Cursor = CommandLine != nullptr ? CommandLine : TEXT("");
	FString Token;
	const FString Prefix = TEXT("-DTCctvIgnoreTimerThrottling=");
	while (FParse::Token(Cursor, Token, false))
	{
		if (Token.StartsWith(Prefix, ESearchCase::IgnoreCase) || Token.Equals(TEXT("-DTCctvIgnoreTimerThrottling"), ESearchCase::IgnoreCase))
		{
			const FString Value = Token.Mid(Prefix.Len());
			Result.bValidCommandLine = Value == TEXT("0") || Value == TEXT("1");
			Result.bCommandLineOverride = Result.bValidCommandLine;
			Result.bEnabled = Result.bValidCommandLine ? Value == TEXT("1") : bConfigured;
		}
	}
	return Result;
}

void DTCctvTimerPowerMasks(uint32& ControlMask, uint32& StateMask)
{
	// 실행 속도를 포함한 다른 비트는 조회한 원래 값을 보존한다.
	ControlMask |= TimerResolutionBit;
	StateMask &= ~TimerResolutionBit;
}

FString FDTCctvWindowsTimerPolicy::Initialize(bool bConfigured, const TCHAR* CommandLine, bool& bWarning)
{
	const FDTCctvTimerThrottlingOption Option = DTCctvResolveTimerThrottling(bConfigured, CommandLine);
	bWarning = !Option.bValidCommandLine;
	const FString Summary = FString::Printf(TEXT("WindowsTimerThrottling enabled=%d source=%s command_line=%s"),
		Option.bEnabled, Option.bCommandLineOverride ? TEXT("command_line") : TEXT("settings"),
		Option.bValidCommandLine ? TEXT("valid") : TEXT("invalid_use_settings"));
	if (!Option.bEnabled)
	{
		return Summary + TEXT(" result=disabled");
	}

#if PLATFORM_WINDOWS
	if (!FWindowsPlatformMisc::VerifyWindowsVersion(10, 0, 22000))
	{
		return Summary + TEXT(" result=unsupported_windows");
	}
	PROCESS_POWER_THROTTLING_STATE State = {};
	State.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	if (!::GetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State)))
	{
		bWarning = true;
		return Summary + FString::Printf(TEXT(" result=query_failed error=%lu"), ::GetLastError());
	}
	OriginalControlMask = State.ControlMask;
	OriginalStateMask = State.StateMask;
	uint32 ControlMask = State.ControlMask;
	uint32 StateMask = State.StateMask;
	DTCctvTimerPowerMasks(ControlMask, StateMask);
	State.ControlMask = ControlMask;
	State.StateMask = StateMask;
	if (!::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State)))
	{
		bWarning = true;
		return Summary + FString::Printf(TEXT(" result=apply_failed error=%lu"), ::GetLastError());
	}
	bApplied = true;
	return Summary + FString::Printf(TEXT(" result=applied control=%lu state=%lu original_control=%u original_state=%u"),
		State.ControlMask, State.StateMask, OriginalControlMask, OriginalStateMask);
#else
	return Summary + TEXT(" result=unsupported_platform");
#endif
}

void FDTCctvWindowsTimerPolicy::Shutdown()
{
#if PLATFORM_WINDOWS
	if (!bApplied)
	{
		return;
	}
	PROCESS_POWER_THROTTLING_STATE State = {};
	State.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	if (::GetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State)))
	{
		// 외부에서 타이머 정책을 바꿨으면 덮어쓰지 않는다. 다른 정책은 현재 값을 보존한다.
		if ((State.ControlMask & TimerResolutionBit) != 0 && (State.StateMask & TimerResolutionBit) == 0)
		{
			State.ControlMask = (State.ControlMask & ~TimerResolutionBit) | (OriginalControlMask & TimerResolutionBit);
			State.StateMask = (State.StateMask & ~TimerResolutionBit) | (OriginalStateMask & TimerResolutionBit);
			if (!::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State)))
			{
				UE_LOG(LogDTCctv, Warning, TEXT("WindowsTimerThrottling result=restore_failed error=%lu"), ::GetLastError());
			}
		}
	}
	else
	{
		UE_LOG(LogDTCctv, Warning, TEXT("WindowsTimerThrottling result=restore_query_failed error=%lu"), ::GetLastError());
	}
	bApplied = false;
#endif
}
