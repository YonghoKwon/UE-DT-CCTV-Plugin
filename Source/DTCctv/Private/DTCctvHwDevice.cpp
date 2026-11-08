#include "DTCctvHwDevice.h"
#include "DTCctvFFmpegApi.h"

#include "DTCctvLog.h"
#include "HAL/PlatformTLS.h"
#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection& GetLock()
	{
		static FCriticalSection Lock;
		return Lock;
	}

	/** 생성에 실패한 장치도 nullptr로 기록해 반복 시도하지 않는다. */
	TMap<int32, AVBufferRef*>& GetDevices()
	{
		static TMap<int32, AVBufferRef*> Devices;
		return Devices;
	}
}

AVBufferRef* DTCctvHwDevice::Acquire(AVHWDeviceType Type)
{
	FScopeLock Lock(&GetLock());
	TMap<int32, AVBufferRef*>& Devices = GetDevices();
	AVBufferRef** Found = Devices.Find(static_cast<int32>(Type));
	if (Found == nullptr)
	{
		AVBufferRef* Device = nullptr;
		const int32 Error = DTCctvFFmpeg().av_hwdevice_ctx_create(&Device, Type, nullptr, nullptr, 0);
		if (Error < 0)
		{
			UE_LOG(LogDTCctv, Log, TEXT("하드웨어 장치를 쓸 수 없습니다: %s"), UTF8_TO_TCHAR(DTCctvFFmpeg().av_hwdevice_get_type_name(Type)));
			Device = nullptr;
		}
		Found = &Devices.Add(static_cast<int32>(Type), Device);
		UE_LOG(LogDTCctv, Log, TEXT("HwDeviceCache created type=%s cache=%p thread=%u"),
			UTF8_TO_TCHAR(DTCctvFFmpeg().av_hwdevice_get_type_name(Type)), Device != nullptr ? Device->data : nullptr,
			FPlatformTLS::GetCurrentThreadId());
	}
	return *Found != nullptr ? DTCctvFFmpeg().av_buffer_ref(*Found) : nullptr;
}

void DTCctvHwDevice::ReleaseAll()
{
	FScopeLock Lock(&GetLock());
	for (TPair<int32, AVBufferRef*>& Pair : GetDevices())
	{
		if (Pair.Value != nullptr)
		{
			UE_LOG(LogDTCctv, Log, TEXT("HwDeviceCache shutdown_release type=%s cache=%p refs=%d thread=%u"),
				UTF8_TO_TCHAR(DTCctvFFmpeg().av_hwdevice_get_type_name(static_cast<AVHWDeviceType>(Pair.Key))),
				Pair.Value->data, DTCctvFFmpeg().av_buffer_get_ref_count(Pair.Value), FPlatformTLS::GetCurrentThreadId());
		}
		DTCctvFFmpeg().av_buffer_unref(&Pair.Value);
	}
	GetDevices().Empty();
}

bool DTCctvHwDevice::ShouldReleaseUnused(AVHWDeviceType Type)
{
	return Type != AV_HWDEVICE_TYPE_CUDA;
}

void DTCctvHwDevice::ReleaseUnused()
{
	FScopeLock Lock(&GetLock());
	for (auto Iterator = GetDevices().CreateIterator(); Iterator; ++Iterator)
	{
		AVBufferRef*& Device = Iterator.Value();
		if (Device != nullptr && DTCctvFFmpeg().av_buffer_get_ref_count(Device) == 1)
		{
			const AVHWDeviceType Type = static_cast<AVHWDeviceType>(Iterator.Key());
			if (!ShouldReleaseUnused(Type))
			{
				// NVENC가 동작하는 동안 같은 GPU의 CUDA 컨텍스트를 파괴하지 않는다.
				UE_LOG(LogDTCctv, Log, TEXT("HwDeviceCache retained type=cuda cache=%p refs=1 thread=%u game_thread=%d"),
					Device->data, FPlatformTLS::GetCurrentThreadId(), IsInGameThread() ? 1 : 0);
				continue;
			}
			UE_LOG(LogDTCctv, Log, TEXT("사용하지 않는 공유 하드웨어 장치 해제: %s"), UTF8_TO_TCHAR(DTCctvFFmpeg().av_hwdevice_get_type_name(static_cast<AVHWDeviceType>(Iterator.Key()))));
			DTCctvFFmpeg().av_buffer_unref(&Device);
			Iterator.RemoveCurrent();
		}
	}
}
