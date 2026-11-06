#include "DTCctvCheckWidget.h"

#include "DTCctvCheckSubsystem.h"
#include "DTCctvStream.h"
#include "Engine/Texture.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

class SDTCctvCheckTile : public SCompoundWidget
{
  public:
	SLATE_BEGIN_ARGS(SDTCctvCheckTile) {}
	SLATE_ARGUMENT(TWeakObjectPtr<UDTCctvCheckSubsystem>, Checker)
	SLATE_ARGUMENT(int32, Index)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args)
	{
		Checker = Args._Checker;
		Index = Args._Index;
		Brush.ImageSize = FVector2D(16, 9);
		ChildSlot[SNew(SBorder).Padding(2).BorderBackgroundColor(FLinearColor(
			.12f, .14f, .18f))[SNew(SOverlay) +
							   SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SAssignNew(Image, SImage).Image(&Brush)]] +
							   SOverlay::Slot()
								   .HAlign(HAlign_Left)
								   .VAlign(VAlign_Top)[SNew(SBorder)
														   .Padding(4)
														   .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
														   .BorderBackgroundColor(FLinearColor(0, 0, 0, .65f))
															   [SNew(STextBlock)
																	.ColorAndOpacity(FLinearColor::White)
																	.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
																	.Text_Lambda(
																		[this]()
																		{
																			return Checker.IsValid() ? Checker->GetChannelOverlay(Index)
																									 : FText::GetEmpty();
																		})]]]];
	}
	virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override
	{
		SCompoundWidget::Tick(Geometry, Time, Delta);
		if (Checker.IsValid())
		{
			Checker->ReportTileSize(Index, Geometry.GetLocalSize() * Geometry.GetAccumulatedLayoutTransform().GetScale());
			if (UDTCctvStream* Stream = Checker->GetChannelStream(Index))
			{
				if (Brush.GetResourceObject() != Stream->GetTexture())
				{
					Brush.SetResourceObject(Stream->GetTexture());
					Image->Invalidate(EInvalidateWidgetReason::Paint);
				}
				const FIntPoint Size = Stream->GetSourceSize();
				if (Size.Y > 0)
				{
					const FVector2D Ratio(static_cast<double>(Size.X) / Size.Y, 1);
					if (Brush.ImageSize != Ratio)
					{
						Brush.ImageSize = Ratio;
						Image->Invalidate(EInvalidateWidgetReason::Layout);
					}
				}
			}
		}
	}

  private:
	TWeakObjectPtr<UDTCctvCheckSubsystem> Checker;
	int32 Index = 0;
	FSlateBrush Brush;
	TSharedPtr<SImage> Image;
};

void SDTCctvCheckPanel::Construct(const FArguments& Args)
{
	Checker = Args._Checker;
	const auto Editable = [this]() { return Checker.IsValid() && !Checker->IsRunning(); };
	ChildSlot[SNew(SBorder)
				  .Padding(10)
				  .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				  .BorderBackgroundColor(FLinearColor(.025f, .035f, .05f))
					  [SNew(SVerticalBox) +
					   SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[SNew(STextBlock)
																				 .Text(FText::FromString(TEXT("CCTV 연결 점검")))
																				 .Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
																				 .ColorAndOpacity(FLinearColor::White)] +
					   SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox) +
														 SHorizontalBox::Slot().FillWidth(1).Padding(
															 2)[SAssignNew(MainUrl, SEditableTextBox)
																	.HintText(FText::FromString(TEXT("RTSP 주소 (입력 내용 가림)")))
																	.IsPassword(true)
																	.IsEnabled_Lambda(Editable)] +
														 SHorizontalBox::Slot().FillWidth(1).Padding(
															 2)[SAssignNew(SubUrl, SEditableTextBox)
																	.HintText(FText::FromString(TEXT("서브 주소 (선택)")))
																	.IsPassword(true)
																	.IsEnabled_Lambda(Editable)]] +
					   SVerticalBox::Slot().AutoHeight()
						   [SNew(SHorizontalBox) +
							SHorizontalBox::Slot().FillWidth(1).Padding(
								2)[SAssignNew(Username, SEditableTextBox)
									   .HintText(FText::FromString(TEXT("사용자 이름 (선택)")))
									   .IsPassword(true)
									   .IsEnabled_Lambda(Editable)] +
							SHorizontalBox::Slot().FillWidth(1).Padding(
								2)[SAssignNew(Password, SEditableTextBox)
									   .HintText(FText::FromString(TEXT("비밀번호 (선택)")))
									   .IsPassword(true)
									   .IsEnabled_Lambda(Editable)] +
							SHorizontalBox::Slot().AutoWidth().Padding(2)
								[SNew(SButton)
									 .IsEnabled_Lambda(Editable)
									 .Text_Lambda(
										 [this]()
										 { return FText::FromString(InputTransport == EDTCctvTransport::Tcp ? TEXT("TCP") : TEXT("UDP")); })
									 .OnClicked_Lambda(
										 [this]()
										 {
											 InputTransport =
												 InputTransport == EDTCctvTransport::Tcp ? EDTCctvTransport::Udp : EDTCctvTransport::Tcp;
											 return FReply::Handled();
										 })] +
							SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton)
																			  .Text(FText::FromString(TEXT("추가")))
																			  .IsEnabled_Lambda(Editable)
																			  .OnClicked(this, &SDTCctvCheckPanel::AddAddress)]] +
					   SVerticalBox::Slot().AutoHeight().Padding(2, 5)
						   [SNew(SHorizontalBox) +
							SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[SNew(STextBlock).Text(FText::FromString(TEXT("점검 초 "))).ColorAndOpacity(FLinearColor::White)] +
							SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(
								75)[SAssignNew(Seconds, SEditableTextBox)
										.Text(FText::FromString(
											FString::SanitizeFloat(Checker.IsValid() ? Checker->GetDurationSeconds() : 60.0)))
										.IsEnabled_Lambda(Editable)]] +
							SHorizontalBox::Slot().AutoWidth().Padding(5, 0)[SNew(SButton)
																				 .Text(FText::FromString(TEXT("시작")))
																				 .IsEnabled_Lambda(Editable)
																				 .OnClicked(this, &SDTCctvCheckPanel::Start)] +
							SHorizontalBox::Slot().AutoWidth().Padding(5, 0)[SNew(SButton)
																				 .Text(FText::FromString(TEXT("중지")))
																				 .OnClicked_Lambda(
																					 [this]()
																					 {
																						 if (Checker.IsValid())
																						 {
																							 Checker->StopCheck();
																						 }
																						 return FReply::Handled();
																					 })] +
							SHorizontalBox::Slot().AutoWidth().Padding(5, 0)[SNew(SButton)
																				 .Text(FText::FromString(TEXT("1열")))
																				 .OnClicked_Lambda(
																					 [this]()
																					 {
																						 Columns = 1;
																						 RefreshGrid();
																						 return FReply::Handled();
																					 })] +
							SHorizontalBox::Slot().AutoWidth().Padding(5, 0)[SNew(SButton)
																				 .Text(FText::FromString(TEXT("2열")))
																				 .OnClicked_Lambda(
																					 [this]()
																					 {
																						 Columns = 2;
																						 RefreshGrid();
																						 return FReply::Handled();
																					 })] +
							SHorizontalBox::Slot().AutoWidth().Padding(5, 0)[SNew(SButton)
																				 .Text(FText::FromString(TEXT("4열")))
																				 .OnClicked_Lambda(
																					 [this]()
																					 {
																						 Columns = 4;
																						 RefreshGrid();
																						 return FReply::Handled();
																					 })] +
							SHorizontalBox::Slot().AutoWidth().Padding(5, 0)[SNew(SButton)
																				 .Text(FText::FromString(TEXT("주소 저장 (계정 제외)")))
																				 .IsEnabled_Lambda(Editable)
																				 .OnClicked_Lambda(
																					 [this]()
																					 {
																						 if (Checker.IsValid())
																						 {
																							 Checker->SaveAddresses();
																						 }
																						 return FReply::Handled();
																					 })]] +
					   SVerticalBox::Slot().AutoHeight().MaxHeight(110).Padding(
						   2)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(EndpointBox, SVerticalBox)]] +
					   SVerticalBox::Slot().FillHeight(1).Padding(2)[SAssignNew(Grid, SGridPanel)] +
					   SVerticalBox::Slot().AutoHeight().MaxHeight(140).Padding(2)[SNew(SScrollBox)
																					   .Visibility_Lambda(
																						   [this]()
																						   {
																							   return Checker.IsValid() &&
																											  Checker->HasResults()
																										  ? EVisibility::Visible
																										  : EVisibility::Collapsed;
																						   }) +
																				   SScrollBox::Slot()[SAssignNew(Results, SGridPanel)]] +
					   SVerticalBox::Slot().AutoHeight().Padding(
						   2)[SNew(STextBlock)
								  .AutoWrapText(true)
								  .ColorAndOpacity(FLinearColor::White)
								  .Text_Lambda([this]() { return Checker.IsValid() ? Checker->GetSummaryText() : FText::GetEmpty(); })]]];
	Refresh();
}

FReply SDTCctvCheckPanel::AddAddress()
{
	if (Checker.IsValid())
	{
		FDTCctvCheckEndpoint Endpoint;
		Endpoint.Url = MainUrl->GetText().ToString();
		Endpoint.SubUrl = SubUrl->GetText().ToString();
		Endpoint.Username = Username->GetText().ToString();
		Endpoint.Password = Password->GetText().ToString();
		Endpoint.Transport = InputTransport;
		if (Checker->AddEndpoint(Endpoint))
		{
			MainUrl->SetText(FText::GetEmpty());
			SubUrl->SetText(FText::GetEmpty());
			Username->SetText(FText::GetEmpty());
			Password->SetText(FText::GetEmpty());
			Refresh();
		}
	}
	return FReply::Handled();
}

FReply SDTCctvCheckPanel::Start()
{
	double Duration = 0.0;
	LexTryParseString(Duration, *Seconds->GetText().ToString());
	if (Checker.IsValid())
	{
		Checker->StartCheck(Duration);
	}
	return FReply::Handled();
}

void SDTCctvCheckPanel::Refresh()
{
	RefreshEndpoints();
	RefreshGrid();
	RefreshResults();
}

void SDTCctvCheckPanel::RefreshEndpoints()
{
	EndpointBox->ClearChildren();
	if (!Checker.IsValid())
	{
		return;
	}
	const TArray<FDTCctvCheckEndpoint>& Entries = Checker->GetEndpoints();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FDTCctvCheckEndpoint& Entry = Entries[Index];
		FString Label =
			FString::Printf(TEXT("%02d [%s] %s"), Index + 1, Entry.Transport == EDTCctvTransport::Tcp ? TEXT("TCP") : TEXT("UDP"),
							*DTCctvCheckSafeUrl(Entry.Url));
		if (!Entry.SubUrl.IsEmpty())
		{
			Label += TEXT(" / 서브 ") + DTCctvCheckSafeUrl(Entry.SubUrl);
		}
		EndpointBox->AddSlot().AutoHeight().Padding(
			0,
			2)[SNew(SHorizontalBox) +
			   SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FLinearColor::White)] +
			   SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
													  .Text(FText::FromString(TEXT("삭제")))
													  .IsEnabled_Lambda([this]() { return Checker.IsValid() && !Checker->IsRunning(); })
													  .OnClicked_Lambda(
														  [this, Index]()
														  {
															  if (Checker.IsValid())
															  {
																  Checker->RemoveEndpoint(Index);
																  Refresh();
															  }
															  return FReply::Handled();
														  })]];
	}
}

void SDTCctvCheckPanel::RefreshGrid()
{
	Grid->ClearChildren();
	if (!Checker.IsValid())
	{
		return;
	}
	const int32 Count = Checker->GetChannelCount();
	const int32 UsedColumns = FMath::Max(1, FMath::Min(Columns, Count));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Grid->SetColumnFill(Index % UsedColumns, 1.f);
		Grid->SetRowFill(Index / UsedColumns, 1.f);
		Grid->AddSlot(Index % UsedColumns, Index / UsedColumns).Padding(2)[SNew(SDTCctvCheckTile).Checker(Checker).Index(Index)];
	}
}

void SDTCctvCheckPanel::RefreshResults()
{
	Results->ClearChildren();
	if (!Checker.IsValid() || !Checker->HasResults())
	{
		return;
	}
	const TArray<FString> Headers = {TEXT("채널"), TEXT("판정"), TEXT("디코드 FPS"), TEXT("표시 FPS"), TEXT("원인")};
	for (int32 Column = 0; Column < Headers.Num(); ++Column)
	{
		Results->AddSlot(Column, 0).Padding(
			4)[SNew(STextBlock).Text(FText::FromString(Headers[Column])).ColorAndOpacity(FLinearColor::White)];
	}
	for (int32 Index = 0; Index < Checker->GetChannelCount(); ++Index)
	{
		const FDTCctvCheckResult* Result = Checker->GetChannelResult(Index);
		if (Result == nullptr)
		{
			continue;
		}
		const TArray<FString> Values = {FString::FromInt(Index + 1), DTCctvCheckVerdictName(Result->Verdict),
										FString::Printf(TEXT("%.2f"), Result->DecodeFps), FString::Printf(TEXT("%.2f"), Result->DisplayFps),
										FString::Join(Result->Reasons, TEXT(" / "))};
		FLinearColor Color(1.f, .3f, .3f);
		if (Result->Verdict == EDTCctvCheckVerdict::Pass)
		{
			Color = FLinearColor(.3f, 1.f, .5f);
		}
		else if (Result->Verdict == EDTCctvCheckVerdict::Warn)
		{
			Color = FLinearColor(1.f, .8f, .2f);
		}
		for (int32 Column = 0; Column < Values.Num(); ++Column)
		{
			Results->AddSlot(Column, Index + 1)
				.Padding(4)[SNew(STextBlock).Text(FText::FromString(Values[Column])).ColorAndOpacity(Color).AutoWrapText(true)];
		}
	}
}
