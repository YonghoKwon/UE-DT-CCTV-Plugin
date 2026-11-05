#pragma once

#include "CoreMinimal.h"
#include "DTCctvYuvRenderer.h"

struct AVFrame;

/** NV12 또는 I420 형식 프레임 하나를 소유한다. */
class FDTCctvVideoFrame
{
public:
	/** Frame의 소유권을 가져온다. */
	explicit FDTCctvVideoFrame(AVFrame* InFrame);
	~FDTCctvVideoFrame();

	FDTCctvVideoFrame(const FDTCctvVideoFrame&) = delete;
	FDTCctvVideoFrame& operator=(const FDTCctvVideoFrame&) = delete;

	FIntPoint GetSize() const;
	FDTCctvYuvPlanes GetPlanes() const;

private:
	AVFrame* Frame;
};

using FDTCctvVideoFramePtr = TSharedPtr<FDTCctvVideoFrame, ESPMode::ThreadSafe>;
