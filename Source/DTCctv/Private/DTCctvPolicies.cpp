#include "DTCctvPolicies.h"
#include "DTCctvTypes.h"

namespace
{
	constexpr int32 FrameAgeOverflowBin = 1001;

	void ComputeFrameAgeSummary(FDTCctvStreamStats& Stats)
	{
		Stats.FrameAgeMeanMs = Stats.FrameAgeSamples > 0 ? Stats.FrameAgeTotalMs / Stats.FrameAgeSamples : 0.0;
		Stats.FrameAgeP95Ms = 0.0;
		const int64 Target = FMath::CeilToInt64(Stats.FrameAgeSamples * 0.95);
		int64 Cumulative = 0;
		for (int32 Bin = 0; Target > 0 && Bin < Stats.FrameAgeHistogram.Num(); ++Bin)
		{
			Cumulative += Stats.FrameAgeHistogram[Bin];
			if (Cumulative >= Target)
			{
				Stats.FrameAgeP95Ms = Bin == FrameAgeOverflowBin ? Stats.FrameAgeMaxMs : Bin;
				break;
			}
		}
	}
}

void FDTCctvFrameAgeAccumulator::Record(double AgeMs)
{
	AgeMs = FMath::Max(0.0, AgeMs);
	if (Histogram.IsEmpty())
	{
		Histogram.SetNumZeroed(FrameAgeOverflowBin + 1);
	}
	const int32 Bin = AgeMs > 1000.0 ? FrameAgeOverflowBin : FMath::CeilToInt(AgeMs);
	++Histogram[Bin];
	++Samples;
	TotalMs += AgeMs;
	MaxMs = FMath::Max(MaxMs, AgeMs);
	LatestMs = AgeMs;
}

void FDTCctvFrameAgeAccumulator::FillStats(FDTCctvStreamStats& Stats) const
{
	Stats.FrameAgeSamples = Samples;
	Stats.FrameAgeTotalMs = TotalMs;
	Stats.FrameAgeMaxMs = MaxMs;
	Stats.FrameAgeLatestMs = LatestMs;
	Stats.FrameAgeHistogram = Histogram;
	ComputeFrameAgeSummary(Stats);
}

void DTCctvMergeFrameAgeStats(FDTCctvStreamStats& Total, const FDTCctvStreamStats& Stats)
{
	const bool bHadSamples = Total.FrameAgeSamples > 0;
	Total.FrameAgeSamples += Stats.FrameAgeSamples;
	Total.FrameAgeTotalMs += Stats.FrameAgeTotalMs;
	Total.FrameAgeMaxMs = FMath::Max(Total.FrameAgeMaxMs, Stats.FrameAgeMaxMs);
	if (!bHadSamples)
	{
		Total.FrameAgeLatestMs = Stats.FrameAgeLatestMs;
	}
	if (Total.FrameAgeHistogram.Num() < Stats.FrameAgeHistogram.Num())
	{
		Total.FrameAgeHistogram.SetNumZeroed(Stats.FrameAgeHistogram.Num());
	}
	for (int32 Bin = 0; Bin < Stats.FrameAgeHistogram.Num(); ++Bin)
	{
		Total.FrameAgeHistogram[Bin] += Stats.FrameAgeHistogram[Bin];
	}
	ComputeFrameAgeSummary(Total);
}

void DTCctvClearFrameAgeStats(FDTCctvStreamStats& Stats)
{
	Stats.FrameAgeSamples = 0;
	Stats.FrameAgeTotalMs = Stats.FrameAgeMeanMs = Stats.FrameAgeP95Ms = Stats.FrameAgeMaxMs = Stats.FrameAgeLatestMs = 0.0;
	Stats.FrameAgeHistogram.Reset();
}

bool DTCctvCanCatchUp(double TickSeconds, double SourceFps)
{
	return TickSeconds > 0.0 && SourceFps > 0.0 && 1.0 / TickSeconds >= SourceFps * 1.25;
}

bool DTCctvShouldBackpressure(double NowSeconds, double LastPollSeconds, double LastTakeSeconds, double WaitStartedSeconds, bool bCanCatchUp)
{
	return bCanCatchUp && LastPollSeconds > 0.0 && LastTakeSeconds > 0.0
		&& NowSeconds >= LastPollSeconds && NowSeconds >= LastTakeSeconds && NowSeconds >= WaitStartedSeconds
		&& NowSeconds - LastPollSeconds < 0.35
		&& NowSeconds - LastTakeSeconds < 0.75
		&& NowSeconds - WaitStartedSeconds < 0.75;
}

int32 DTCctvChooseStaleFrameSkip(TConstArrayView<double> FrameAgesSeconds, double BacklogSeconds, bool bCanCatchUp)
{
	if (bCanCatchUp && BacklogSeconds < 0.75)
	{
		return 0;
	}
	if (FrameAgesSeconds.Num() < 2 || (BacklogSeconds < 0.1 && FrameAgesSeconds[0] <= 0.1))
	{
		return 0;
	}
	int32 Skip = 0;
	while (Skip < FrameAgesSeconds.Num() - 1 && FrameAgesSeconds[Skip] > 0.04)
	{
		++Skip;
	}
	return Skip;
}

FString DTCctvMaskUrlCredentials(const FString& Text)
{
	FString Result = Text;
	int32 SearchFrom = 0;
	for (;;)
	{
		const int32 Scheme = Result.Find(TEXT("://"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
		if (Scheme == INDEX_NONE)
		{
			return Result;
		}

		const int32 HostStart = Scheme + 3;
		int32 HostEnd = HostStart;
		while (HostEnd < Result.Len() && Result[HostEnd] != TEXT('/') && !FChar::IsWhitespace(Result[HostEnd]))
		{
			++HostEnd;
		}

		// 비밀번호에 @가 있어도 가려지도록 마지막 @를 기준으로 한다.
		int32 At = INDEX_NONE;
		for (int32 Index = HostEnd - 1; Index >= HostStart; --Index)
		{
			if (Result[Index] == TEXT('@'))
			{
				At = Index;
				break;
			}
		}

		if (At != INDEX_NONE)
		{
			Result = Result.Left(HostStart) + TEXT("***") + Result.Mid(At);
			SearchFrom = HostStart + 3;
		}
		else
		{
			SearchFrom = HostEnd;
		}
	}
}

FIntPoint DTCctvComputeOutputSize(FIntPoint SourceSize, FVector2D ViewSize)
{
	if (SourceSize.X <= 0 || SourceSize.Y <= 0)
	{
		return FIntPoint::ZeroValue;
	}
	if (ViewSize.X <= 0.0 || ViewSize.Y <= 0.0)
	{
		return SourceSize;
	}

	const double Scale = FMath::Min(1.0, FMath::Min(ViewSize.X / SourceSize.X, ViewSize.Y / SourceSize.Y));
	return FIntPoint(
		FMath::Max(2, FMath::CeilToInt(SourceSize.X * Scale)),
		FMath::Max(2, FMath::CeilToInt(SourceSize.Y * Scale)));
}

EDTCctvOutputWatchdogAction DTCctvEvaluateOutputWatchdog(double NowSeconds, double ReadStartedSeconds,
	double FirstKeyPacketSeconds, double LastFrameSeconds, double ReadTimeoutSeconds, bool bHardware)
{
	double ReferenceSeconds = ReadStartedSeconds;
	double TimeoutSeconds = 30.0;
	if (LastFrameSeconds > 0.0)
	{
		ReferenceSeconds = LastFrameSeconds;
		TimeoutSeconds = ReadTimeoutSeconds;
	}
	else if (FirstKeyPacketSeconds > 0.0)
	{
		ReferenceSeconds = FirstKeyPacketSeconds;
		TimeoutSeconds = FMath::Max(ReadTimeoutSeconds, 10.0);
	}
	if (NowSeconds - ReferenceSeconds < TimeoutSeconds)
	{
		return EDTCctvOutputWatchdogAction::None;
	}
	return bHardware && FirstKeyPacketSeconds > 0.0
		? EDTCctvOutputWatchdogAction::RetrySoftware
		: EDTCctvOutputWatchdogAction::Reconnect;
}

FDTCctvBackoff::FDTCctvBackoff(float InMinSeconds, float InMaxSeconds)
	: MinSeconds(InMinSeconds)
	, MaxSeconds(FMath::Max(InMinSeconds, InMaxSeconds))
	, CurrentSeconds(InMinSeconds)
{
}

float FDTCctvBackoff::Next(double StableSeconds, float Random01)
{
	if (StableSeconds >= StableResetSeconds)
	{
		CurrentSeconds = MinSeconds;
	}
	const float Delay = CurrentSeconds * (1.f + 0.25f * FMath::Clamp(Random01, 0.f, 1.f));
	CurrentSeconds = FMath::Min(CurrentSeconds * 2.f, MaxSeconds);
	return Delay;
}
