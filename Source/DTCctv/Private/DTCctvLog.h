#pragma once

#include "Logging/LogMacros.h"
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDTCctv, Log, All);

class UGameInstance;

struct FDTCctvLogContext
{
	TWeakObjectPtr<UGameInstance> GameInstance;
	TWeakObjectPtr<UObject> Subsystem;
	bool bMirrorToUE = true;
};

struct FDTCctvLogRoute
{
	bool bDTCore = false;
	bool bUE = true;
};

FDTCctvLogRoute DTCctvChooseLogRoute(bool bEditor, bool bDTCore, bool bReady, bool bShuttingDown, bool bMirror);
FDTCctvLogContext DTCctvMakeLogContext(const UObject* WorldContext, bool bMirror);
void DTCctvInitializeLogSink();
void DTCctvShutdownLogSink();
void DTCctvLogEvent(const FDTCctvLogContext& Context, const FString& Message, ELogVerbosity::Type Verbosity = ELogVerbosity::Log);
FDTCctvLogContext DTCctvCurrentLogContext();

class FDTCctvLogScope
{
public:
	explicit FDTCctvLogScope(const FDTCctvLogContext& Context);
	~FDTCctvLogScope();
private:
	const FDTCctvLogContext* Previous = nullptr;
};
