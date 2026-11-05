#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "DTCctvTypes.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Layout/SScaleBox.h"
#include "DTCctvView.generated.h"

class SDTCctvViewHost;
class UDTCctvStream;

/** CCTV 영상을 표시하는 UMG 위젯. 슬롯 크기를 바꾸면 그에 맞춰 표시된다. */
UCLASS()
class DTCCTV_API UDTCctvView : public UWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DTCctv")
	FDTCctvStreamConfig Config;

	/** 위젯이 만들어지면 바로 재생한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DTCctv")
	bool bAutoPlay = true;

	/** ScaleToFit은 비율을 유지하고, Fill은 슬롯에 맞춰 늘린다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DTCctv")
	TEnumAsByte<EStretch::Type> Stretch = EStretch::ScaleToFit;

	UFUNCTION(BlueprintCallable, Category = "DTCctv")
	void Play();

	UFUNCTION(BlueprintCallable, Category = "DTCctv")
	void Stop();

	/** 메인 스트림 주소를 바꿔 다시 재생한다. */
	UFUNCTION(BlueprintCallable, Category = "DTCctv")
	void PlayUrl(const FString& Url);

	UFUNCTION(BlueprintPure, Category = "DTCctv")
	UDTCctvStream* GetStream() const { return Stream; }

	//~ UWidget
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void SynchronizeProperties() override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UDTCctvStream> Stream;

	UPROPERTY(Transient)
	FSlateBrush Brush;

	TSharedPtr<SDTCctvViewHost> Host;
};
