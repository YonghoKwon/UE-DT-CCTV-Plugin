#pragma once

#include "CoreMinimal.h"
#include "DTCctvCheckPolicy.h"
#include "Widgets/SCompoundWidget.h"

class UDTCctvCheckSubsystem;
class SEditableTextBox;
class SVerticalBox;
class SGridPanel;

class SDTCctvCheckPanel : public SCompoundWidget
{
  public:
	SLATE_BEGIN_ARGS(SDTCctvCheckPanel) {}
	SLATE_ARGUMENT(TWeakObjectPtr<UDTCctvCheckSubsystem>, Checker)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void Refresh();

  private:
	void RefreshEndpoints();
	void RefreshGrid();
	void RefreshResults();
	FReply AddAddress();
	FReply Start();
	TWeakObjectPtr<UDTCctvCheckSubsystem> Checker;
	TSharedPtr<SEditableTextBox> MainUrl;
	TSharedPtr<SEditableTextBox> SubUrl;
	TSharedPtr<SEditableTextBox> Username;
	TSharedPtr<SEditableTextBox> Password;
	TSharedPtr<SEditableTextBox> Seconds;
	TSharedPtr<SVerticalBox> EndpointBox;
	TSharedPtr<SGridPanel> Grid;
	TSharedPtr<SGridPanel> Results;
	EDTCctvTransport InputTransport = EDTCctvTransport::Tcp;
	int32 Columns = 4;
};
