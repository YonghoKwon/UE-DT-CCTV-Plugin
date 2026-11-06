#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DTCctvSettings.generated.h"

/** 프로젝트 전체 CCTV 설정. 카메라별 값은 FDTCctvStreamConfig에 둔다. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "DT CCTV"))
class DTCCTV_API UDTCctvSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 동시에 진행할 수 있는 연결 시도 수. 일제히 복구할 때 서버 부하를 줄인다. */
	UPROPERTY(config, EditAnywhere, Category = "DTCctv", meta = (ClampMin = "1"))
	int32 MaxConcurrentConnects = 8;

	/** 채널별 패킷 큐 상한. 넘으면 다음 키프레임까지 건너뛴다. */
	UPROPERTY(config, EditAnywhere, Category = "DTCctv", meta = (ClampMin = "16"))
	int32 MaxQueuedPackets = 120;

	/** 표시 폭이 이 값(px)보다 작으면 SubUrl로 전환한다. */
	UPROPERTY(config, EditAnywhere, Category = "DTCctv", meta = (ClampMin = "1"))
	int32 SubStreamMaxWidth = 960;

	/**
	 * 하드웨어 디코딩을 쓸 최대 채널 수. 나머지 채널은 소프트웨어로 디코딩한다.
	 * 디코딩 결과를 시스템 메모리로 내려받는 비용 때문에 GPU별 한계가 있으며, 측정 후 조정한다.
	 */
	UPROPERTY(config, EditAnywhere, Category = "DTCctv", meta = (ClampMin = "0"))
	int32 MaxHardwareDecodeStreams = 4;

	/** 소프트웨어 디코딩 때 채널별 FFmpeg 스레드 수. */
	UPROPERTY(config, EditAnywhere, Category = "DTCctv", meta = (ClampMin = "1"))
	int32 SoftwareDecodeThreads = 2;

	/** DTCore 패키지 로그를 UE 로그에도 남긴다. 새 스트림부터 적용한다. */
	UPROPERTY(config, EditAnywhere, Category = "DTCctv|Logging")
	bool bMirrorPackagedLogsToUE = true;
};
