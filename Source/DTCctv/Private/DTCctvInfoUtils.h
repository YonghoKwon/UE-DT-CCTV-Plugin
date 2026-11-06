#pragma once

#include "CoreMinimal.h"
#include "DTCctvFFmpegInclude.h"
#include "DTCctvStreamInfo.h"

FString DTCctvProfileName(AVCodecID Codec, int32 Profile);
FString DTCctvLevelName(AVCodecID Codec, int32 Level);
FString DTCctvDescribeError(int32 Error, bool bDeadlineExpired);
void DTCctvDescribePixelFormat(AVPixelFormat Format, FDTCctvStreamInfo& Info);
void DTCctvParseSdpInfo(const FString& Sdp, FDTCctvStreamInfo& Info);
TMap<FString, FString> DTCctvInfoFields(const FDTCctvStreamInfo& Info);
FString DTCctvInfoSummary(const FDTCctvStreamInfo& Info);
FString DTCctvInfoChanges(const FDTCctvStreamInfo& Previous, const FDTCctvStreamInfo& Current);

/** 디코더 스레드에서 사용하며 패킷 byte 카운터는 호출자가 전달한다. */
class FDTCctvStreamProbe
{
public:
	void Record(double Now, double MediaSeconds, bool bKey, bool bBFrame);
	void Fill(FDTCctvStreamInfo& Info, int64 PayloadBytes, double Now) const;
	bool IsStable(double Now) const { return Frames > 0 && Now - Start >= 10.0; }
	bool HasGop() const { return GopFrames > 0; }

private:
	double Start = 0.0;
	int64 Frames = 0;
	int64 LastKeyFrame = 0;
	double LastKeyTime = 0.0;
	double GopSeconds = 0.0;
	int32 GopFrames = 0;
	bool bSawKey = false;
	bool bSawB = false;
};
