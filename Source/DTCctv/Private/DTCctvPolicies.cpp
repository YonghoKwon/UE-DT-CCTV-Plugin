#include "DTCctvPolicies.h"

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
