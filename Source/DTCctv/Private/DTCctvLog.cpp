#include "DTCctvLog.h"

#include "Containers/Queue.h"
#include "DTCctvStreamInfo.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ScopeLock.h"
#include "Tickable.h"

#if DTCCTV_WITH_DTCORE
#include "Core/DxLogSubsystem.h"
#endif

namespace
{
	thread_local const FDTCctvLogContext* CurrentContext = nullptr;

	struct FLogEntry
	{
		FDTCctvLogContext Context;
		FString Message;
		ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
	};

	void EmitUE(const FLogEntry& Entry)
	{
		if (Entry.Verbosity == ELogVerbosity::Error)
		{
			UE_LOG(LogDTCctv, Error, TEXT("%s"), *Entry.Message);
		}
		else if (Entry.Verbosity == ELogVerbosity::Warning)
		{
			UE_LOG(LogDTCctv, Warning, TEXT("%s"), *Entry.Message);
		}
		else
		{
			UE_LOG(LogDTCctv, Log, TEXT("%s"), *Entry.Message);
		}
	}

	void Deliver(const FLogEntry& Entry)
	{
		check(IsInGameThread());
#if DTCCTV_WITH_DTCORE && !WITH_EDITOR
		UGameInstance* GameInstance = Entry.Context.GameInstance.Get();
		UDxLogSubsystem* Subsystem = Cast<UDxLogSubsystem>(Entry.Context.Subsystem.Get());
		if (GameInstance != nullptr && Subsystem == nullptr)
		{
			Subsystem = GameInstance->GetSubsystem<UDxLogSubsystem>();
		}
		UWorld* World = GameInstance != nullptr ? GameInstance->GetWorld() : nullptr;
		const bool bStopping = IsEngineExitRequested() || World == nullptr || World->bIsTearingDown;
		const bool bReady = IsValid(GameInstance) && IsValid(Subsystem);
		const FDTCctvLogRoute Route = DTCctvChooseLogRoute(false, true, bReady, bStopping, Entry.Context.bMirrorToUE);
		if (Route.bDTCore)
		{
			Subsystem->WriteLog(TEXT("[DTCctv] ") + Entry.Message, false);
		}
		if (Route.bUE)
		{
			EmitUE(Entry);
		}
#else
		EmitUE(Entry);
#endif
	}

	class FLogSink : public FTickableGameObject
	{
	public:
		bool Enqueue(FLogEntry&& Entry)
		{
			FScopeLock Lock(&Guard);
			if (!bAccepting)
			{
				return false;
			}
			Queue.Enqueue(MoveTemp(Entry));
			return true;
		}

		void Stop()
		{
			{
				FScopeLock Lock(&Guard);
				bAccepting = false;
			}
			Tick(0.f);
		}

		virtual void Tick(float DeltaTime) override
		{
			FLogEntry Entry;
			while (Queue.Dequeue(Entry))
			{
				Deliver(Entry);
			}
		}
		virtual bool IsTickableWhenPaused() const override { return true; }
		virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(DTCctvLogSink, STATGROUP_Tickables); }

	private:
		FCriticalSection Guard;
		bool bAccepting = true;
		TQueue<FLogEntry, EQueueMode::Mpsc> Queue;
	};

	FCriticalSection SinkLock;
	TSharedPtr<FLogSink, ESPMode::ThreadSafe> Sink;
}

FDTCctvLogRoute DTCctvChooseLogRoute(bool bEditor, bool bDTCore, bool bReady, bool bShuttingDown, bool bMirror)
{
	FDTCctvLogRoute Result;
	Result.bDTCore = !bEditor && bDTCore && bReady && !bShuttingDown;
	Result.bUE = !Result.bDTCore || bMirror;
	return Result;
}

FDTCctvLogContext DTCctvMakeLogContext(const UObject* WorldContext, bool bMirror)
{
	check(IsInGameThread());
	FDTCctvLogContext Context;
	Context.bMirrorToUE = bMirror;
	UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World != nullptr)
	{
		UGameInstance* GameInstance = World->GetGameInstance();
		Context.GameInstance = GameInstance;
#if DTCCTV_WITH_DTCORE
		if (GameInstance != nullptr)
		{
			Context.Subsystem = GameInstance->GetSubsystem<UDxLogSubsystem>();
		}
#endif
	}
	return Context;
}

void DTCctvInitializeLogSink()
{
	check(IsInGameThread());
	FScopeLock Lock(&SinkLock);
	Sink = MakeShared<FLogSink, ESPMode::ThreadSafe>();
}

void DTCctvShutdownLogSink()
{
	check(IsInGameThread());
	TSharedPtr<FLogSink, ESPMode::ThreadSafe> Closing;
	{
		FScopeLock Lock(&SinkLock);
		Closing = MoveTemp(Sink);
	}
	if (Closing.IsValid())
	{
		Closing->Stop();
	}
}

void DTCctvLogEvent(const FDTCctvLogContext& Context, const FString& Message, ELogVerbosity::Type Verbosity)
{
	FLogEntry Entry{ Context, DTCctvMaskUrlCredentials(Message), Verbosity };
#if DTCCTV_WITH_DTCORE && !WITH_EDITOR
	if (IsInGameThread())
	{
		Deliver(Entry);
		return;
	}
	TSharedPtr<FLogSink, ESPMode::ThreadSafe> Target;
	{
		FScopeLock Lock(&SinkLock);
		Target = Sink;
	}
	if (Target.IsValid() && Target->Enqueue(MoveTemp(Entry)))
	{
		return;
	}
#endif
	EmitUE(Entry);
}

FDTCctvLogScope::FDTCctvLogScope(const FDTCctvLogContext& Context)
{
	Previous = CurrentContext;
	CurrentContext = &Context;
}

FDTCctvLogScope::~FDTCctvLogScope()
{
	CurrentContext = Previous;
}

FDTCctvLogContext DTCctvCurrentLogContext()
{
	return CurrentContext != nullptr ? *CurrentContext : FDTCctvLogContext();
}
