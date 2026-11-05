#pragma once

#include "CoreMinimal.h"
#include "DTCctvTypes.generated.h"

UENUM(BlueprintType)
enum class EDTCctvTransport : uint8
{
	Tcp,
	Udp
};

UENUM(BlueprintType)
enum class EDTCctvDecodeMode : uint8
{
	/** NVDEC → D3D11VA/VAAPI → 소프트웨어 순서로 사용 가능한 방식을 고른다. */
	Auto,
	SoftwareOnly
};

UENUM(BlueprintType)
enum class EDTCctvState : uint8
{
	Idle,
	Connecting,
	Playing,
	/** 연결이 끊겨 복구 중이다. 텍스처에는 마지막 프레임이 남아 있다. */
	Reconnecting
};

USTRUCT(BlueprintType)
struct FDTCctvStreamConfig
{
	GENERATED_BODY()

	/** 메인 스트림 주소. 예: rtsp://user:pass@host:554/stream1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DTCctv")
	FString Url;

	/** 서브 스트림 주소(선택). 표시 크기가 작을 때 자동으로 전환한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DTCctv")
	FString SubUrl;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DTCctv")
	EDTCctvTransport Transport = EDTCctvTransport::Tcp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DTCctv")
	EDTCctvDecodeMode DecodeMode = EDTCctvDecodeMode::Auto;

	/** 연결과 스트림 분석에 허용하는 시간(초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DTCctv", meta = (ClampMin = "1.0"))
	float ConnectTimeoutSeconds = 5.f;

	/** 이 시간(초) 동안 데이터나 디코딩 결과가 없으면 끊긴 것으로 판단한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DTCctv", meta = (ClampMin = "1.0"))
	float ReadTimeoutSeconds = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DTCctv", meta = (ClampMin = "0.1"))
	float MinReconnectDelaySeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DTCctv", meta = (ClampMin = "0.1"))
	float MaxReconnectDelaySeconds = 10.f;
};

USTRUCT(BlueprintType)
struct FDTCctvStreamStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	EDTCctvState State = EDTCctvState::Idle;

	/** 스트림 해상도. */
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FIntPoint SourceSize = FIntPoint::ZeroValue;

	/** 렌더 타깃 해상도. */
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FIntPoint OutputSize = FIntPoint::ZeroValue;

	/** 스트림 코덱 이름. 예: h264, hevc */
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString CodecName;

	/** 사용 중인 디코딩 방식. 예: cuda, d3d11va, vaapi, software */
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString DecoderName;

	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	float DecodeFps = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	float DisplayFps = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int64 PacketsReceived = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int64 FramesDecoded = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int64 FramesDisplayed = 0;

	/** 화면 갱신 전에 더 새로운 프레임으로 대체된 수. */
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int64 FramesSuperseded = 0;

	/** 디코딩 과부하로 버린 패킷 수. 정상 운영에서는 0이어야 한다. */
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int64 PacketsDropped = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int64 DecodeErrors = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int32 Reconnects = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int32 QueuedPackets = 0;
};
