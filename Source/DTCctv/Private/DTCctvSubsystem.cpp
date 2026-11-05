#include "DTCctvSubsystem.h"

#include "DTCctvStream.h"

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
	for (UDTCctvStream* Stream : Streams)
	{
		if (Stream != nullptr)
		{
			Stream->Close();
		}
	}
	Streams.Reset();
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
