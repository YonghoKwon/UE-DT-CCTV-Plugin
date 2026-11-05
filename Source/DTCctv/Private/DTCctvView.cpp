#include "DTCctvView.h"

#include "DTCctvStream.h"
#include "DTCctvSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SCompoundWidget.h"

/** 슬롯 크기를 스트림에 알리고 브러시 크기를 영상 비율에 맞춘다. */
class SDTCctvViewHost : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDTCctvViewHost)
		: _Brush(nullptr)
		, _Stretch(EStretch::ScaleToFit)
	{
	}
		SLATE_ARGUMENT(FSlateBrush*, Brush)
		SLATE_ARGUMENT(EStretch::Type, Stretch)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Brush = InArgs._Brush;
		ChildSlot
		[
			SAssignNew(ScaleBox, SScaleBox)
			.Stretch(InArgs._Stretch)
			[
				SNew(SImage).Image(Brush)
			]
		];
	}

	void SetStream(UDTCctvStream* InStream)
	{
		Stream = InStream;
	}

	void SetStretch(EStretch::Type InStretch)
	{
		ScaleBox->SetStretch(InStretch);
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		UDTCctvStream* PinnedStream = Stream.Get();
		if (PinnedStream == nullptr)
		{
			return;
		}
		PinnedStream->ReportViewSize(AllottedGeometry.GetAbsoluteSize());

		const FIntPoint SourceSize = PinnedStream->GetSourceSize();
		if (SourceSize.X > 0 && SourceSize.Y > 0)
		{
			Brush->ImageSize = FVector2f(SourceSize.X, SourceSize.Y);
		}
	}

private:
	FSlateBrush* Brush = nullptr;
	TSharedPtr<SScaleBox> ScaleBox;
	TWeakObjectPtr<UDTCctvStream> Stream;
};

namespace
{
	UDTCctvSubsystem* FindSubsystem(const UWidget* Widget)
	{
		const UWorld* World = Widget->GetWorld();
		const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		return GameInstance != nullptr ? GameInstance->GetSubsystem<UDTCctvSubsystem>() : nullptr;
	}
}

TSharedRef<SWidget> UDTCctvView::RebuildWidget()
{
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.ImageSize = FVector2f(16.f, 9.f);

	Host = SNew(SDTCctvViewHost).Brush(&Brush).Stretch(Stretch.GetValue());

	if (Stream != nullptr)
	{
		Host->SetStream(Stream);
		Brush.SetResourceObject(Stream->GetTexture());
	}
	else if (bAutoPlay && !IsDesignTime() && !Config.Url.IsEmpty())
	{
		Play();
	}
	return Host.ToSharedRef();
}

void UDTCctvView::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (Host.IsValid())
	{
		Host->SetStretch(Stretch.GetValue());
	}
}

#if WITH_EDITOR
const FText UDTCctvView::GetPaletteCategory()
{
	return NSLOCTEXT("DTCctv", "PaletteCategory", "DT CCTV");
}
#endif

void UDTCctvView::Play()
{
	if (Stream != nullptr)
	{
		Stream->Open(Config);
	}
	else if (UDTCctvSubsystem* Subsystem = FindSubsystem(this))
	{
		Stream = Subsystem->OpenStream(Config);
	}
	else
	{
		return;
	}

	Brush.SetResourceObject(Stream->GetTexture());
	if (Host.IsValid())
	{
		Host->SetStream(Stream);
	}
}

void UDTCctvView::PlayUrl(const FString& Url)
{
	Config.Url = Url;
	Play();
}

void UDTCctvView::Stop()
{
	if (Stream == nullptr)
	{
		return;
	}

	if (UDTCctvSubsystem* Subsystem = FindSubsystem(this))
	{
		Subsystem->CloseStream(Stream);
	}
	else
	{
		Stream->Close();
	}
	Stream = nullptr;

	if (Host.IsValid())
	{
		Host->SetStream(nullptr);
	}
	Brush.SetResourceObject(nullptr);
}

void UDTCctvView::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	if (!IsDesignTime())
	{
		Stop();
	}
	Host.Reset();
}
