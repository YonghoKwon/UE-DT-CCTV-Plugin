#pragma once

#include "CoreMinimal.h"
#include "DTCctvTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DTCctvSubsystem.generated.h"

class UDTCctvStream;

/** 게임 인스턴스 수명 동안 스트림을 관리한다. 종료 시 모두 닫는다. */
UCLASS()
class DTCCTV_API UDTCctvSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	/** 스트림을 만들어 바로 연다. */
	UFUNCTION(BlueprintCallable, Category = "DTCctv")
	UDTCctvStream* OpenStream(const FDTCctvStreamConfig& Config);

	UFUNCTION(BlueprintCallable, Category = "DTCctv")
	void CloseStream(UDTCctvStream* Stream);

	UFUNCTION(BlueprintCallable, Category = "DTCctv")
	void CloseAllStreams();

	UFUNCTION(BlueprintPure, Category = "DTCctv")
	TArray<UDTCctvStream*> GetStreams() const;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDTCctvStream>> Streams;
};
