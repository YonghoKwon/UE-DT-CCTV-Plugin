#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "DTCctvTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "DTCctvHarnessSubsystem.generated.h"

class UDTCctvView;
class UCanvasPanel;

/** UUserWidget은 추상 클래스라 코드로 위젯을 구성할 최소 파생 클래스를 둔다. */
UCLASS()
class UDTCctvHarnessRootWidget : public UUserWidget
{
	GENERATED_BODY()
};

/**
 * 명령줄로 다채널 재생을 실행하고 통계를 CSV로 남기는 측정 도구.
 * -CctvUrl=주소 -CctvCount=16 -CctvSeconds=60 -CctvCsv=경로 [-CctvSoftware] [-CctvShot=경로]
 * -CctvResizeScenario=drag|extremes|subset|disconnected -CctvSubUrl=주소
 * -CctvResizePeriod=12 -CctvResizeAffected=4 -CctvShotEvery=1
 * -CctvCloseOpenCycles=5 -CctvCloseOpenInterval=10 -CctvCloseHold=2
 * -CctvStallAt=30 -CctvStallSeconds=2
 */
UCLASS()
class UDTCctvHarnessSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override { return bEnabled; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UDTCctvHarnessSubsystem, STATGROUP_Tickables); }

private:
	void BuildViews();
	void WriteSample(float DeltaTime);
	void UpdateScenario(double Elapsed);
	void WriteEvent(const FString& Event, double Elapsed);
	FDTCctvStreamStats GetLifetimeStats(int32 Index) const;

	bool bEnabled = false;
	bool bBuilt = false;
	bool bShotTaken = false;
	bool bSoftware = false;
	bool bFrameAgeWarmupReset = false;
	bool bStallDone = false;
	double StallAtSeconds = 0.0;
	double StallSeconds = 2.0;
	FString Url;
	FString SubUrl;
	FString CsvPath;
	FString ChannelCsvPath;
	FString EventCsvPath;
	FString ShotPath;
	FString ResizeScenario;
	int32 Count = 16;
	double DurationSeconds = 60.0;
	double StartSeconds = 0.0;
	double WindowStartSeconds = 0.0;
	double MaxFrameMs = 0.0;
	int32 FramesThisWindow = 0;
	double ResizePeriod = 12.0;
	double ShotEvery = 0.0;
	double NextShotSeconds = 0.0;
	double CloseOpenInterval = 10.0;
	double CloseHoldSeconds = 2.0;
	double NextCycleSeconds = 10.0;
	double ReopenSeconds = 0.0;
	int32 ResizeAffected = 4;
	int32 CloseOpenCycles = 0;
	int32 CompletedCycles = 0;
	int32 LastScenarioPhase = INDEX_NONE;
	int32 ShotIndex = 0;
	bool bCycleClosed = false;
	TArray<FDTCctvStreamStats> ClosedTotals;
	TArray<FVector2D> RequestedSizes;

	UPROPERTY(Transient)
	TObjectPtr<UDTCctvHarnessRootWidget> RootWidget;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDTCctvView>> Views;
};
