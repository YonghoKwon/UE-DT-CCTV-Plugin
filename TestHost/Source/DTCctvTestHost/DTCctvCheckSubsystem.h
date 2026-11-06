#pragma once

#include "CoreMinimal.h"
#include "DTCctvCheckPolicy.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "DTCctvCheckSubsystem.generated.h"

class UDTCctvStream;
class SDTCctvCheckPanel;

struct FDTCctvCheckChannel
{
	FDTCctvCheckEndpoint Endpoint;
	FDTCctvCheckObservation Observation;
	FDTCctvCheckResult Result;
	FText Overlay;
	FString CsvPath;
};

/** C++ 점검 화면과 파일 기반 점검에서 같은 관측·판정을 사용한다. */
UCLASS()
class UDTCctvCheckSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

  public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override
	{
		return bEnabled && !IsTemplate();
	}
	virtual bool IsTickableWhenPaused() const override
	{
		return true;
	}
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(DTCctvCheckSubsystem, STATGROUP_Tickables);
	}

	bool AddEndpoint(const FDTCctvCheckEndpoint& Endpoint);
	void RemoveEndpoint(int32 Index);
	bool StartCheck(double Seconds);
	void StopCheck();
	bool SaveAddresses();
	bool IsRunning() const
	{
		return bRunning || bCapturePending;
	}
	const TArray<FDTCctvCheckEndpoint>& GetEndpoints() const
	{
		return Endpoints;
	}
	int32 GetChannelCount() const
	{
		return Channels.Num();
	}
	UDTCctvStream* GetChannelStream(int32 Index) const;
	FText GetChannelOverlay(int32 Index) const;
	FText GetSummaryText() const;
	FString GetReportDirectory() const
	{
		return ReportDirectory;
	}
	void ReportTileSize(int32 Index, FVector2D Size);
	bool HasResults() const
	{
		return bCompleted;
	}
	double GetDurationSeconds() const
	{
		return DurationSeconds;
	}
	const FDTCctvCheckResult* GetChannelResult(int32 Index) const;

  private:
	void BuildUI();
	bool PrepareReportDirectory();
	void SnapshotChannels(double Elapsed, bool bReadStats = true);
	void WriteSamples(double Elapsed);
	void FinishCheck(bool bInterrupted);
	void FinalizeReport();
	void WriteReports();
	void CloseStreams();
	FString StateName(EDTCctvState State) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDTCctvStream>> Streams;

	TArray<FDTCctvCheckEndpoint> Endpoints;
	TArray<FDTCctvCheckChannel> Channels;
	TSharedPtr<SDTCctvCheckPanel> Panel;
	bool bEnabled = false;
	bool bAutomatic = false;
	bool bStarted = false;
	bool bRunning = false;
	bool bCompleted = false;
	bool bDisplayAvailable = false;
	bool bWarmupDone = false;
	bool bCapturePending = false;
	bool bCaptureRequested = false;
	bool bWriteFailed = false;
	bool bInputModeApplied = false;
	double DurationSeconds = 60.0;
	double StartSeconds = 0.0;
	double EndSeconds = 0.0;
	double WarmupSeconds = 0.0;
	double NextSnapshot = 0.0;
	double NextCsv = 0.0;
	double CaptureSince = 0.0;
	FString ReportDirectory;
	FString ScreenshotStatus = TEXT("NOT_RUN");
	FString StartupError;
	FString Message;
};
