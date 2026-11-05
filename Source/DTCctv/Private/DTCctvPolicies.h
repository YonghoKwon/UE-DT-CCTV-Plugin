#pragma once

#include "CoreMinimal.h"

struct FDTCctvStreamStats;

/** 1ms 구간과 1초 초과 구간으로 모든 제출 프레임을 집계한다. 표본을 추출하지 않는다. */
class FDTCctvFrameAgeAccumulator
{
public:
	void Record(double AgeMs);
	void FillStats(FDTCctvStreamStats& Stats) const;

private:
	int64 Samples = 0;
	double TotalMs = 0.0;
	double MaxMs = 0.0;
	double LatestMs = 0.0;
	TArray<int64> Histogram;
};

void DTCctvMergeFrameAgeStats(FDTCctvStreamStats& Total, const FDTCctvStreamStats& Stats);
void DTCctvClearFrameAgeStats(FDTCctvStreamStats& Stats);

/** 짧은 버스트는 보존하되, 100ms 이상 지속된 대기 또는 100ms 초과 age는 40ms 안쪽으로 따라잡는다. */
int32 DTCctvChooseStaleFrameSkip(TConstArrayView<double> FrameAgesSeconds, double BacklogSeconds);

/** 로그에 남기기 전에 URL의 계정 정보를 가린다. */
FString DTCctvMaskUrlCredentials(const FString& Text);

/** 표시 크기에 맞는 렌더 타깃 크기. 비율을 유지하고 원본보다 커지지 않는다. */
FIntPoint DTCctvComputeOutputSize(FIntPoint SourceSize, FVector2D ViewSize);

enum class EDTCctvOutputWatchdogAction : uint8
{
	None,
	Reconnect,
	RetrySoftware
};

/** 키프레임 전에는 30초 대기 후 일반 재연결만 하고, 이후에만 디코더 출력을 감시한다. */
EDTCctvOutputWatchdogAction DTCctvEvaluateOutputWatchdog(double NowSeconds, double ReadStartedSeconds,
	double FirstKeyPacketSeconds, double LastFrameSeconds, double ReadTimeoutSeconds, bool bHardware);

/** 재연결 대기 시간 계산. 안정적으로 재생되면 처음 값으로 돌아간다. */
class FDTCctvBackoff
{
public:
	FDTCctvBackoff(float InMinSeconds, float InMaxSeconds);

	/**
	 * @param StableSeconds 직전 연결에서 첫 프레임 이후 유지된 시간
	 * @param Random01 [0, 1) 난수. 채널이 동시에 재시도하지 않도록 최대 25%를 더한다.
	 */
	float Next(double StableSeconds, float Random01);

	static constexpr double StableResetSeconds = 30.0;

private:
	float MinSeconds;
	float MaxSeconds;
	float CurrentSeconds;
};
