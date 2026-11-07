#include "DTCctvFFmpegInclude.h"
#include "DTCctvFFmpegApi.h"
#include "DTCctvHwDevice.h"
#include "DTCctvLog.h"
#include "DTCctvPolicies.h"
#include "DTCctvSession.h"
#include "DTCctvSettings.h"
#include "DTCctvWindowsTimerPolicy.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformTime.h"
#include "Modules/ModuleManager.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include "RenderingThread.h"

#if DTCCTV_WITH_DTCORE && !WITH_EDITOR
#include "Core/DxLogSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#endif

DEFINE_LOG_CATEGORY(LogDTCctv);

namespace
{
	/** FFmpeg 오류를 UE 로그로 옮긴다. 손상된 스트림에서 로그가 넘치지 않도록 초당 10건으로 제한한다. */
	void FFmpegLogCallback(void* Avcl, int Level, const char* Format, va_list Args)
	{
		if (Level == AV_LOG_VERBOSE && FCStringAnsi::Strstr(Format, "SDP:") != nullptr)
		{
			va_list Copy;
			va_copy(Copy, Args);
			const int32 Length = vsnprintf(nullptr, 0, Format, Copy);
			va_end(Copy);
			if (Length > 0 && Length <= 1024 * 1024)
			{
				TArray<char> Sdp;
				Sdp.SetNumUninitialized(Length + 1);
				vsnprintf(Sdp.GetData(), Sdp.Num(), Format, Args);
				FDTCctvSession::CaptureSdpLog(Avcl, UTF8_TO_TCHAR(Sdp.GetData()));
			}
			return;
		}
		if (Level > AV_LOG_ERROR)
		{
			return;
		}

		static std::atomic<int64> WindowSecond{ 0 };
		static std::atomic<int32> WindowCount{ 0 };
		const int64 Second = static_cast<int64>(FPlatformTime::Seconds());
		if (WindowSecond.exchange(Second) != Second)
		{
			WindowCount.store(0);
		}
		if (WindowCount.fetch_add(1) >= 10)
		{
			return;
		}

		char Line[1024] = { 0 };
		int PrintPrefix = 1;
		DTCctvFFmpeg().av_log_format_line2(Avcl, Level, Format, Args, Line, sizeof(Line), &PrintPrefix);
		FString Message = DTCctvMaskUrlCredentials(UTF8_TO_TCHAR(Line));
		Message.TrimEndInline();
		DTCctvLogEvent(DTCctvCurrentLogContext(), TEXT("FFmpeg: ") + Message);
	}
}

class FDTCctvModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// SDP만 메모리에서 수집하며, 나머지 상세 로그는 콜백에서 버린다.
		DTCctvInitializeLogSink();
		bool bTimerPolicyWarning = false;
		TimerPolicyMessage = TimerPolicy.Initialize(GetDefault<UDTCctvSettings>()->bIgnoreWindowsTimerThrottling, FCommandLine::Get(), bTimerPolicyWarning);
		DTCctvLogEvent(FDTCctvLogContext(), TimerPolicyMessage, bTimerPolicyWarning ? ELogVerbosity::Warning : ELogVerbosity::Log);
#if DTCCTV_WITH_DTCORE && !WITH_EDITOR
		// 모듈 시작 때는 GameInstance가 없으므로 DTCore에는 준비된 뒤 한 줄 남긴다.
		GameInstanceStartedHandle = FWorldDelegates::OnStartGameInstance.AddRaw(this, &FDTCctvModule::LogTimerPolicyToDTCore);
#endif
		if (!DTCctvInitializeFFmpeg())
		{
			DTCctvLogEvent(FDTCctvLogContext(), TEXT("FFmpeg 로드 준비 실패: ") + DTCctvFFmpegLoadError(), ELogVerbosity::Error);
			return;
		}
		DTCctvFFmpeg().av_log_set_level(AV_LOG_VERBOSE);
		DTCctvFFmpeg().av_log_set_callback(&FFmpegLogCallback);
		DTCctvFFmpeg().avformat_network_init();
		UE_LOG(LogDTCctv, Log, TEXT("FFmpeg %s"), UTF8_TO_TCHAR(DTCctvFFmpeg().av_version_info()));
	}

	virtual void ShutdownModule() override
	{
#if DTCCTV_WITH_DTCORE && !WITH_EDITOR
		FWorldDelegates::OnStartGameInstance.Remove(GameInstanceStartedHandle);
#endif
		TimerPolicy.Shutdown();
		FDTCctvSession::ShutdownRetired();
		FlushRenderingCommands();
		DTCctvHwDevice::ReleaseAll();
		if (DTCctvFFmpegReady())
		{
			DTCctvFFmpeg().av_log_set_callback(DTCctvFFmpeg().av_log_default_callback);
			if (DTCctvFFmpegOwners() == 0)
			{
				DTCctvFFmpeg().avformat_network_deinit();
			}
		}
		DTCctvShutdownLogSink();
		DTCctvShutdownFFmpeg();
	}

private:
	FDTCctvWindowsTimerPolicy TimerPolicy;
	FString TimerPolicyMessage;

#if DTCCTV_WITH_DTCORE && !WITH_EDITOR
	FDelegateHandle GameInstanceStartedHandle;

	void LogTimerPolicyToDTCore(UGameInstance* GameInstance)
	{
		if (IsValid(GameInstance))
		{
			if (UDxLogSubsystem* Subsystem = GameInstance->GetSubsystem<UDxLogSubsystem>())
			{
				Subsystem->WriteLog(TEXT("[DTCctv] ") + TimerPolicyMessage, false);
				if (!DTCctvFFmpegReady())
				{
					Subsystem->WriteLog(TEXT("[DTCctv] FFmpeg 로드 준비 실패: ") + DTCctvFFmpegLoadError(), false);
				}
			}
		}
	}
#endif
};

IMPLEMENT_MODULE(FDTCctvModule, DTCctv);
