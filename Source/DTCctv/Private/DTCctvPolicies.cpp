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

FString DTCctvMaskUrlCredentials(const FString &Text)
{
	FString Result = Text;
	int32 SearchFrom = 0;
	const auto Hex = [](TCHAR Value) -> int32
	{
		if (Value >= '0' && Value <= '9')
		{
			return Value - '0';
		}
		Value = FChar::ToLower(Value);
		return Value >= 'a' && Value <= 'f' ? Value - 'a' + 10 : -1;
	};
	static const TSet<FString> SecretKeys = {
		TEXT("user"),		   TEXT("username"), TEXT("userid"), TEXT("login"),	  TEXT("password"),
		TEXT("passwd"),		   TEXT("pass"),	 TEXT("pwd"),	 TEXT("token"),	  TEXT("access_token"),
		TEXT("authorization"), TEXT("auth"),	 TEXT("apikey"), TEXT("api_key"), TEXT("signature")};
	for (;;)
	{
		const int32 Scheme = Result.Find(TEXT("://"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
		if (Scheme == INDEX_NONE)
		{
			return Result;
		}
		const int32 Start = Scheme + 3;
		int32 End = Start;
		while (End < Result.Len() && !FChar::IsWhitespace(Result[End]))
		{
			++End;
		}
		FString Url = Result.Mid(Start, End - Start);
		int32 AuthorityEnd = 0;
		while (AuthorityEnd < Url.Len() && Url[AuthorityEnd] != '/' && Url[AuthorityEnd] != '?' &&
			   Url[AuthorityEnd] != '#')
		{
			++AuthorityEnd;
		}
		const int32 At = Url.Left(AuthorityEnd).Find(TEXT("@"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (At != INDEX_NONE)
		{
			Url = TEXT("***") + Url.Mid(At);
		}
		const int32 Question = Url.Find(TEXT("?"));
		if (Question != INDEX_NONE)
		{
			TArray<FString> Parts;
			Url.Mid(Question + 1).ParseIntoArray(Parts, TEXT("&"), false);
			for (FString &Part : Parts)
			{
				FString Key;
				FString Value;
				if (!Part.Split(TEXT("="), &Key, &Value))
				{
					continue;
				}
				FString Decoded;
				for (int32 Index = 0; Index < Key.Len(); ++Index)
				{
					if (Key[Index] == '%' && Index + 2 < Key.Len() && Hex(Key[Index + 1]) >= 0 &&
						Hex(Key[Index + 2]) >= 0)
					{
						Decoded.AppendChar(static_cast<TCHAR>(Hex(Key[Index + 1]) * 16 + Hex(Key[Index + 2])));
						Index += 2;
					}
					else
					{
						Decoded.AppendChar(Key[Index]);
					}
				}
				if (SecretKeys.Contains(Decoded.ToLower()))
				{
					Part = Key + TEXT("=***");
				}
			}
			Url = Url.Left(Question + 1) + FString::Join(Parts, TEXT("&"));
		}
		Result = Result.Left(Start) + Url + Result.Mid(End);
		SearchFrom = Start;
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
