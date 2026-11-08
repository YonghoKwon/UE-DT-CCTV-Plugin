#pragma once

#include "CoreMinimal.h"
#include "DTCctvFFmpegInclude.h"

/** 하드웨어 장치 컨텍스트를 프로세스에서 하나만 만들어 채널끼리 공유한다. */
namespace DTCctvHwDevice
{
	/** 새 참조를 돌려준다. 호출자가 av_buffer_unref로 해제한다. 실패하면 nullptr. */
	AVBufferRef* Acquire(AVHWDeviceType Type);
	/** CUDA 캐시는 모듈 종료까지 유지한다. 다른 장치는 마지막 캐시 참조만 남으면 해제한다. */
	void ReleaseUnused();
	bool ShouldReleaseUnused(AVHWDeviceType Type);
	void ReleaseAll();
}
