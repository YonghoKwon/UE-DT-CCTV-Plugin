#pragma once

#include "CoreMinimal.h"
#include "DTCctvFFmpegInclude.h"

/** 하드웨어 장치 컨텍스트를 프로세스에서 하나만 만들어 채널끼리 공유한다. */
namespace DTCctvHwDevice
{
	/** 새 참조를 돌려준다. 호출자가 av_buffer_unref로 해제한다. 실패하면 nullptr. */
	AVBufferRef* Acquire(AVHWDeviceType Type);
	/** 코덱·프레임 참조가 모두 끝난 캐시만 읽기 스레드에서 해제한다. 실패 기록은 유지한다. */
	void ReleaseUnused();
	void ReleaseAll();
}
