#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "DTCctvHarnessSubsystem.generated.h"

class UDTCctvView;

/** UUserWidget은 추상 클래스라 코드로 위젯을 구성할 최소 파생 클래스를 둔다. */
UCLASS()
class UDTCctvHarnessRootWidget : public UUserWidget
{
	GENERATED_BODY()
};

/**
 * 명령줄로 다채널 재생을 실행하고 통계를 CSV로 남기는 측정 도구.
 * -CctvUrl=주소 -CctvCount=16 -CctvSeconds=60 -CctvCsv=경로 [-CctvSoftware] [-CctvShot=경로]
 */
UCLASS()
class UDTCctvHarnessSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override { return bEnabled; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UDTCctvHarnessSubsystem, STATGROUP_Tickables); }

private:
	void BuildViews();
	void WriteSample(float DeltaTime);

	bool bEnabled = false;
	bool bBuilt = false;
	bool bShotTaken = false;
	bool bSoftware = false;
	FString Url;
	FString CsvPath;
	FString ShotPath;
	int32 Count = 16;
	double DurationSeconds = 60.0;
	double StartSeconds = 0.0;
	double WindowStartSeconds = 0.0;
	double MaxFrameMs = 0.0;
	int32 FramesThisWindow = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDTCctvView>> Views;
};
