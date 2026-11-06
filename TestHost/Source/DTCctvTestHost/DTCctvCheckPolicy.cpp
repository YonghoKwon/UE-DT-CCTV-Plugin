#include "DTCctvCheckPolicy.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool IsRtsp(const FString& Url)
{
	return Url.StartsWith(TEXT("rtsp://"), ESearchCase::IgnoreCase) || Url.StartsWith(TEXT("rtsps://"), ESearchCase::IgnoreCase);
}

FString EncodeUserInfo(const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text);
	FString Result;
	for (int32 Index = 0; Index < Utf8.Length(); ++Index)
	{
		const uint8 Value = static_cast<uint8>(Utf8.Get()[Index]);
		const bool bPlain = (Value >= 'a' && Value <= 'z') || (Value >= 'A' && Value <= 'Z') || (Value >= '0' && Value <= '9') ||
							Value == '-' || Value == '_' || Value == '.' || Value == '~';
		Result += bPlain ? FString::Chr(Value) : FString::Printf(TEXT("%%%02X"), Value);
	}
	return Result;
}
} // namespace

FString DTCctvCheckVerdictName(EDTCctvCheckVerdict Verdict)
{
	switch (Verdict)
	{
	case EDTCctvCheckVerdict::Pass:
		return TEXT("PASS");
	case EDTCctvCheckVerdict::Warn:
		return TEXT("WARN");
	default:
		return TEXT("FAIL");
	}
}

FDTCctvCheckResult DTCctvEvaluateCheck(const FDTCctvCheckObservation& Observation)
{
	FDTCctvCheckResult Result;
	if (Observation.WindowSeconds > 0.0)
	{
		const int64 Decoded = Observation.Current.FramesDecoded - Observation.Warmup.FramesDecoded;
		const int64 Displayed = Observation.Current.FramesDisplayed - Observation.Warmup.FramesDisplayed;
		Result.DecodeFps = FMath::Max<int64>(0, Decoded) / Observation.WindowSeconds;
		Result.DisplayFps = FMath::Max<int64>(0, Displayed) / Observation.WindowSeconds;
	}
	if (!Observation.bEverPlayed)
	{
		Result.Reasons.Add(Observation.Error.IsEmpty() ? TEXT("점검 시간 안에 연결·디코딩을 확인하지 못했습니다.") : Observation.Error);
		return Result;
	}
	if (Observation.bInterrupted || Observation.Current.State != EDTCctvState::Playing)
	{
		Result.Reasons.Add(TEXT("점검 중 재생이 중단됐습니다."));
		if (!Observation.Error.IsEmpty())
		{
			Result.Reasons.Add(Observation.Error);
		}
		return Result;
	}
	if (Observation.WindowSeconds <= 0.0)
	{
		Result.Reasons.Add(TEXT("초기 10초 이후의 판정 구간이 부족합니다."));
		return Result;
	}

	if (!Observation.bPlayingAfterWarmup)
	{
		Result.Reasons.Add(TEXT("초기 10초가 지난 뒤 첫 재생을 시작했습니다."));
	}
	if (Observation.Current.Reconnects > 0)
	{
		Result.Reasons.Add(FString::Printf(TEXT("재연결 %d회가 발생했습니다."), Observation.Current.Reconnects));
	}
	if (Observation.Current.PacketsDropped > 0)
	{
		Result.Reasons.Add(FString::Printf(TEXT("패킷 손실 %lld개가 발생했습니다."), Observation.Current.PacketsDropped));
	}
	if (Observation.Current.DecodeErrors > 0)
	{
		Result.Reasons.Add(FString::Printf(TEXT("디코딩 오류 %lld개가 발생했습니다."), Observation.Current.DecodeErrors));
	}
	if (!Observation.bDisplayAvailable)
	{
		Result.Reasons.Add(TEXT("화면 없는 실행으로 표시 FPS·색·스크린샷을 검증하지 못했습니다."));
	}
	else if (Observation.Info.DeclaredFps <= 0.0)
	{
		Result.Reasons.Add(TEXT("원본 FPS가 미확정이어서 표시 비율을 판정하지 못했습니다."));
	}
	else if ((Observation.Current.FramesDisplayed - Observation.Warmup.FramesDisplayed) * 100.0 <
			 Observation.Info.DeclaredFps * 97.0 * Observation.WindowSeconds)
	{
		Result.Reasons.Add(
			FString::Printf(TEXT("표시 %.3ffps가 원본 %.3ffps의 97%%에 미달합니다."), Result.DisplayFps, Observation.Info.DeclaredFps));
	}
	if (Observation.Info.DecoderName == TEXT("software"))
	{
		Result.Reasons.Add(FString::Printf(TEXT("%s %s를 소프트웨어로 디코딩 중입니다. %s"), *Observation.Info.CodecName,
										   *Observation.Info.Profile, *Observation.Info.DecoderReason));
	}
	if (Observation.bDisplayAvailable && Observation.Current.FramesSuperseded > 0)
	{
		Result.Reasons.Add(FString::Printf(TEXT("표시 전 프레임 교체 %lld회가 관측됐습니다."), Observation.Current.FramesSuperseded));
	}
	if (Observation.FirstFrameSeconds > 5.0)
	{
		Result.Reasons.Add(
			FString::Printf(TEXT("첫 재생까지 %.2f초가 걸렸습니다. 긴 GOP와 연결 상태를 확인하세요."), Observation.FirstFrameSeconds));
	}
	Result.Verdict = Result.Reasons.IsEmpty() ? EDTCctvCheckVerdict::Pass : EDTCctvCheckVerdict::Warn;
	return Result;
}

FString DTCctvCheckSafeUrl(const FString& Url)
{
	return IsRtsp(Url) ? DTCctvMaskUrlCredentials(Url) : TEXT("유효하지 않은 RTSP 주소");
}

static FString StripUserInfo(const FString& Url)
{
	if (!IsRtsp(Url))
	{
		return FString();
	}
	const int32 Start = Url.Find(TEXT("://")) + 3;
	int32 End = Url.Len();
	for (int32 Index = Start; Index < Url.Len(); ++Index)
	{
		if (Url[Index] == '/' || Url[Index] == '?' || Url[Index] == '#')
		{
			End = Index;
			break;
		}
	}
	const FString Authority = Url.Mid(Start, End - Start);
	const int32 At = Authority.Find(TEXT("@"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	return At == INDEX_NONE ? Url : Url.Left(Start) + Authority.Mid(At + 1) + Url.Mid(End);
}

FString DTCctvCheckStripCredentials(const FString& Url)
{
	return DTCctvMaskUrlCredentials(StripUserInfo(Url));
}

bool DTCctvCheckComposeUrl(const FString& Url, const FString& Username, const FString& Password, FString& Result, FString& Error)
{
	FString Input = Url.TrimStartAndEnd();
	if (!IsRtsp(Input))
	{
		Error = TEXT("RTSP 주소 형식이 올바르지 않습니다.");
		return false;
	}
	if (Username.IsEmpty() && !Password.IsEmpty())
	{
		Error = TEXT("비밀번호를 사용할 사용자 이름이 필요합니다.");
		return false;
	}
	const int32 Start = Input.Find(TEXT("://")) + 3;
	if (Input.Len() <= Start || Input.Mid(Start).StartsWith(TEXT("/")))
	{
		Error = TEXT("RTSP 서버 주소가 비어 있습니다.");
		return false;
	}
	if (!Username.IsEmpty())
	{
		Input = StripUserInfo(Input);
		Input.InsertAt(Start, EncodeUserInfo(Username) + TEXT(":") + EncodeUserInfo(Password) + TEXT("@"));
	}
	Result = MoveTemp(Input);
	return true;
}

bool DTCctvLoadCheckList(const FString& File, TArray<FDTCctvCheckEndpoint>& Entries, FString& Error)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		Error = TEXT("주소 설정 파일을 읽을 수 없습니다.");
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		Error = TEXT("주소 설정 JSON을 해석하지 못했습니다.");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Channels = nullptr;
	if (!Root->TryGetArrayField(TEXT("channels"), Channels) || Channels->IsEmpty() || Channels->Num() > 16)
	{
		Error = TEXT("channels에 주소를 1~16개 넣어야 합니다.");
		return false;
	}
	TArray<FDTCctvCheckEndpoint> Loaded;
	for (const TSharedPtr<FJsonValue>& Value : *Channels)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value->TryGetObject(Object))
		{
			Error = TEXT("채널 항목은 JSON 객체여야 합니다.");
			return false;
		}
		FDTCctvCheckEndpoint Entry;
		const auto ReadString = [Object](const TCHAR* Key, FString& Target)
		{ return !(*Object)->HasField(Key) || (*Object)->TryGetStringField(Key, Target); };
		if (!ReadString(TEXT("url"), Entry.Url) || !ReadString(TEXT("sub_url"), Entry.SubUrl) ||
			!ReadString(TEXT("username"), Entry.Username) || !ReadString(TEXT("password"), Entry.Password))
		{
			Error = TEXT("주소·계정 필드는 문자열이어야 합니다.");
			return false;
		}

		(*Object)->TryGetBoolField(TEXT("software_only"), Entry.bSoftware);
		FString Transport;
		if ((*Object)->TryGetStringField(TEXT("transport"), Transport))
		{
			if (!Transport.Equals(TEXT("tcp"), ESearchCase::IgnoreCase) && !Transport.Equals(TEXT("udp"), ESearchCase::IgnoreCase))
			{
				Error = TEXT("전송 방식은 tcp 또는 udp여야 합니다.");
				return false;
			}
			Entry.Transport = Transport.Equals(TEXT("udp"), ESearchCase::IgnoreCase) ? EDTCctvTransport::Udp : EDTCctvTransport::Tcp;
		}
		Loaded.Add(MoveTemp(Entry));
	}
	Entries = MoveTemp(Loaded);
	return true;
}

bool DTCctvSaveCheckList(const FString& File, const TArray<FDTCctvCheckEndpoint>& Entries)
{
	if (Entries.IsEmpty() || Entries.Num() > 16)
	{
		return false;
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	TArray<TSharedPtr<FJsonValue>> Channels;
	for (const FDTCctvCheckEndpoint& Entry : Entries)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("url"), DTCctvCheckStripCredentials(Entry.Url));
		Object->SetStringField(TEXT("sub_url"), DTCctvCheckStripCredentials(Entry.SubUrl));
		Object->SetStringField(TEXT("transport"), Entry.Transport == EDTCctvTransport::Tcp ? TEXT("tcp") : TEXT("udp"));
		Object->SetBoolField(TEXT("software_only"), Entry.bSoftware);
		Channels.Add(MakeShared<FJsonValueObject>(Object));
	}
	Root->SetArrayField(TEXT("channels"), Channels);
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
