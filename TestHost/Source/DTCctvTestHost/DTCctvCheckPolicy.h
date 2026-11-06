#pragma once

#include "CoreMinimal.h"
#include "DTCctvStreamInfo.h"
#include "DTCctvTypes.h"

enum class EDTCctvCheckVerdict : uint8
{
	Pass,
	Warn,
	Fail
};

struct FDTCctvCheckEndpoint
{
	FString Url;
	FString SubUrl;
	FString Username;
	FString Password;
	EDTCctvTransport Transport = EDTCctvTransport::Tcp;
	bool bSoftware = false;
};

struct FDTCctvCheckObservation
{
	FDTCctvStreamStats Current;
	FDTCctvStreamStats Warmup;
	FDTCctvStreamInfo Info;
	double WindowSeconds = 0.0;
	double FirstFrameSeconds = -1.0;
	bool bEverPlayed = false;
	bool bPlayingAfterWarmup = true;
	bool bInterrupted = false;
	bool bDisplayAvailable = true;
	FString Error;
};

struct FDTCctvCheckResult
{
	EDTCctvCheckVerdict Verdict = EDTCctvCheckVerdict::Fail;
	TArray<FString> Reasons;
	double DecodeFps = 0.0;
	double DisplayFps = 0.0;
};

FString DTCctvCheckVerdictName(EDTCctvCheckVerdict Verdict);
FDTCctvCheckResult DTCctvEvaluateCheck(const FDTCctvCheckObservation& Observation);
FString DTCctvCheckSafeUrl(const FString& Url);
FString DTCctvCheckStripCredentials(const FString& Url);
bool DTCctvCheckComposeUrl(const FString& Url, const FString& Username, const FString& Password, FString& Result, FString& Error);
bool DTCctvLoadCheckList(const FString& File, TArray<FDTCctvCheckEndpoint>& Entries, FString& Error);
bool DTCctvSaveCheckList(const FString& File, const TArray<FDTCctvCheckEndpoint>& Entries);
