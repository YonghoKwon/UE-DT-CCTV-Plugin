#pragma once

#include "CoreMinimal.h"
#include "DTCctvStreamInfo.generated.h"

/** 화면·로그·점검 보고서에서 같은 계정 가림 규칙을 사용한다. */
DTCCTV_API FString DTCctvMaskUrlCredentials(const FString& Text);

/** 연결에서 선언한 형식과 실제 관측한 값을 구분한다. 알려지지 않은 수치는 0이다. */
USTRUCT(BlueprintType)
struct DTCCTV_API FDTCctvStreamInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString SafeUrl;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString Transport;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString ServerTool;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString SessionName;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	bool bSdpKnown = false;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int32 VideoTracks = 0;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int32 SelectedVideoTrack = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int32 SelectedStreamIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int32 AudioTracks = 0;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	TArray<FString> AudioCodecs;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString CodecName;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString Profile;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString Level;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FIntPoint SourceSize = FIntPoint::ZeroValue;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString PixelFormat;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString FramePixelFormat;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int32 BitDepth = 0;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString Chroma;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	double DeclaredFps = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	bool bDeclaredBFrames = false;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString DecoderName;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	FString DecoderReason;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int32 ConnectionNumber = 0;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	bool bStabilized = false;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	double MeasuredFps = 0.0;
	/** 비디오 압축 패킷 payload의 평균. RTSP/RTP 헤더와 오디오는 제외한다. */
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int64 MeasuredBitrateBps = 0;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	bool bMeasuredGop = false;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	double GopSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	int32 GopFrames = 0;
	UPROPERTY(BlueprintReadOnly, Category = "DTCctv")
	bool bObservedBFrames = false;
};
