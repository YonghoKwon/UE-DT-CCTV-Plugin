#include "DTCctvSubsystem.h"

#include "DTCctvStream.h"
#include "UObject/StrongObjectPtr.h"

void UDTCctvSubsystem::Deinitialize()
{
	CloseAllStreams();
	Super::Deinitialize();
}

UDTCctvStream* UDTCctvSubsystem::OpenStream(const FDTCctvStreamConfig& Config)
{
	UDTCctvStream* Stream = NewObject<UDTCctvStream>(this);
	Streams.Add(Stream);
	Stream->Open(Config);
	return Stream;
}

void UDTCctvSubsystem::CloseStream(UDTCctvStream* Stream)
{
	if (Stream != nullptr)
	{
		Stream->Close();
		Streams.Remove(Stream);
	}
}

void UDTCctvSubsystem::CloseAllStreams()
{
	// 상태 콜백이 목록을 바꾸거나 GC를 호출해도 닫을 대상의 수명을 유지한다.
	TArray<TStrongObjectPtr<UDTCctvStream>> ClosingStreams;
	ClosingStreams.Reserve(Streams.Num());
	for (UDTCctvStream* Stream : Streams)
	{
		ClosingStreams.Emplace(Stream);
	}
	Streams.Reset();
	for (const TStrongObjectPtr<UDTCctvStream>& Stream : ClosingStreams)
	{
		if (Stream.IsValid())
		{
			Stream->Close();
		}
	}
}

TArray<UDTCctvStream*> UDTCctvSubsystem::GetStreams() const
{
	TArray<UDTCctvStream*> Result;
	Result.Reserve(Streams.Num());
	for (UDTCctvStream* Stream : Streams)
	{
		Result.Add(Stream);
	}
	return Result;
}
