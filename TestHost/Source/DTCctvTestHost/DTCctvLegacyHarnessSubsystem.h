#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "DTCctvLegacyHarnessSubsystem.generated.h"

class UMediaPlayer;
class UMediaTexture;
class UDTCctvHarnessRootWidget;
struct FDTCctvLegacyChannel;

/** -CctvLegacyCount로만 켜는 기존 플레이어 공존 시험. 제품 플러그인에는 의존성을 추가하지 않는다. */
UCLASS()
class UDTCctvLegacyHarnessSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return bEnabled && !IsTemplate(); }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UDTCctvLegacyHarnessSubsystem, STATGROUP_Tickables); }

private:
	void StartPlayers();
	void WriteSamples(double Elapsed);
	bool bEnabled = false;
	bool bStarted = false;
	int32 Count = 0;
	FString Url;
	FString CsvPath;
	double StartSeconds = 0.0;
	double LastSampleSeconds = 0.0;
	TArray<TSharedPtr<FDTCctvLegacyChannel>> Channels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMediaPlayer>> Players;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMediaTexture>> Textures;

	UPROPERTY(Transient)
	TObjectPtr<UDTCctvHarnessRootWidget> RootWidget;
};
