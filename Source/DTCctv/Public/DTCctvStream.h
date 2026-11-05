#pragma once

#include "CoreMinimal.h"
#include "DTCctvTypes.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include <atomic>
#include "DTCctvStream.generated.h"

class FDTCctvSession;
class FDTCctvYuvRenderer;
class UTexture;
class UTextureRenderTarget2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDTCctvStateChanged, UDTCctvStream*, Stream, EDTCctvState, State);

/**
 * 카메라 한 대의 재생 객체. 텍스처를 UMG나 머티리얼에 연결해 사용한다.
 * 모든 함수는 게임 스레드에서 호출하며 네트워크 작업으로 호출자를 막지 않는다.
 */
UCLASS(BlueprintType)
class DTCCTV_API UDTCctvStream : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** 스트림을 연다. 연결이 끊기면 Close를 호출할 때까지 자동으로 복구한다. */
	UFUNCTION(BlueprintCallable, Category = "DTCctv")
	void Open(const FDTCctvStreamConfig& InConfig);

	UFUNCTION(BlueprintCallable, Category = "DTCctv")
	void Close();

	UFUNCTION(BlueprintPure, Category = "DTCctv")
	bool IsOpen() const;

	/** 영상이 그려지는 텍스처. 객체가 살아 있는 동안 같은 텍스처를 유지한다. */
	UFUNCTION(BlueprintPure, Category = "DTCctv")
	UTexture* GetTexture() const;

	UFUNCTION(BlueprintPure, Category = "DTCctv")
	EDTCctvState GetState() const { return LastState; }

	UFUNCTION(BlueprintPure, Category = "DTCctv")
	FDTCctvStreamStats GetStats() const;

	/** 시험 워밍업 이후 새 지연 표본창을 시작한다. 기존 프레임·손실 카운터는 보존한다. */
	void ResetFrameAgeStats();

	/** 서브 스트림 사용 여부를 직접 정한다. 새 스트림의 첫 프레임이 나올 때 전환한다. */
	UFUNCTION(BlueprintCallable, Category = "DTCctv")
	void SetUseSubStream(bool bUseSubStream);

	/** 표시 크기(px)를 알려 출력 해상도와 자동 서브 스트림 전환에 사용한다. */
	UFUNCTION(BlueprintCallable, Category = "DTCctv")
	void ReportViewSize(FVector2D InViewSize);

	UPROPERTY(BlueprintAssignable, Category = "DTCctv")
	FDTCctvStateChanged OnStateChanged;

	FIntPoint GetSourceSize() const { return SourceSize; }

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;

	//~ UObject
	virtual void BeginDestroy() override;

private:
	void StartSession(bool bSubStream);
	void CreateTextureIfNeeded();
	void UpdateSubStreamSelection();
	void UpdateOutputSize(FIntPoint FrameSize);
	void RenderLatestFrame();
	void UpdateStats();

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Texture;

	FDTCctvStreamConfig Config;

	TSharedPtr<FDTCctvSession, ESPMode::ThreadSafe> Session;
	/** 서브/메인 전환 중인 새 세션. 첫 프레임이 나오면 Session을 대체한다. */
	TSharedPtr<FDTCctvSession, ESPMode::ThreadSafe> PendingSession;
	TSharedPtr<FDTCctvYuvRenderer, ESPMode::ThreadSafe> Renderer;
	TSharedPtr<std::atomic<int32>, ESPMode::ThreadSafe> RenderInFlight;

	EDTCctvState LastState = EDTCctvState::Idle;
	bool bUsingSubStream = false;
	bool bManualSubStream = false;

	FVector2D ViewSize = FVector2D::ZeroVector;
	FIntPoint SourceSize = FIntPoint::ZeroValue;
	FIntPoint OutputSize = FIntPoint::ZeroValue;
	double ResizeWantedSince = 0.0;
	double SubSwitchWantedSince = 0.0;

	/** 교체된 세션까지 합친 누적 통계. */
	FDTCctvStreamStats Accumulated;
	int64 FramesDisplayed = 0;
	float DecodeFps = 0.f;
	float DisplayFps = 0.f;
	double StatsWindowStart = 0.0;
	int64 StatsWindowDecoded = 0;
	int64 StatsWindowDisplayed = 0;
};
