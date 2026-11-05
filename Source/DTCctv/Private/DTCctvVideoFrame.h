#pragma once

#include "CoreMinimal.h"
#include "DTCctvYuvRenderer.h"

struct AVFrame;

/** NV12 또는 I420 형식 프레임 하나를 소유한다. */
class FDTCctvVideoFrame
{
public:
	/** Frame의 소유권을 가져온다. */
	FDTCctvVideoFrame(AVFrame* InFrame, FIntPoint InSourceSize, double InDecodedSeconds = 0.0);
	~FDTCctvVideoFrame();

	FDTCctvVideoFrame(const FDTCctvVideoFrame&) = delete;
	FDTCctvVideoFrame& operator=(const FDTCctvVideoFrame&) = delete;

	FIntPoint GetSize() const;
	FIntPoint GetSourceSize() const { return SourceSize; }
	double GetDecodedSeconds() const { return DecodedSeconds; }
	int64 GetBufferBytes() const;
	FDTCctvYuvPlanes GetPlanes() const;

private:
	AVFrame* Frame;
	const FIntPoint SourceSize;
	const double DecodedSeconds;
};

using FDTCctvVideoFramePtr = TSharedPtr<FDTCctvVideoFrame, ESPMode::ThreadSafe>;
