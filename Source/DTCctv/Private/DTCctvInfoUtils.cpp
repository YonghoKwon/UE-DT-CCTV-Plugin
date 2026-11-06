#include "DTCctvInfoUtils.h"

#include "DTCctvPolicies.h"

FString DTCctvProfileName(AVCodecID Codec, int32 Profile)
{
	const char* Name = avcodec_profile_name(Codec, Profile);
	return Name != nullptr ? UTF8_TO_TCHAR(Name) : TEXT("미확인");
}

FString DTCctvLevelName(AVCodecID Codec, int32 Level)
{
	if (Level <= 0)
	{
		return TEXT("미확인");
	}
	const double Divisor = Codec == AV_CODEC_ID_HEVC ? 30.0 : 10.0;
	return FString::Printf(TEXT("%.1f"), Level / Divisor);
}

void DTCctvDescribePixelFormat(AVPixelFormat Format, FDTCctvStreamInfo& Info)
{
	const AVPixFmtDescriptor* Description = av_pix_fmt_desc_get(Format);
	if (Description == nullptr || (Description->flags & AV_PIX_FMT_FLAG_HWACCEL) != 0)
	{
		return;
	}
	Info.PixelFormat = UTF8_TO_TCHAR(Description->name);
	Info.BitDepth = Description->nb_components > 0 ? Description->comp[0].depth : 0;
	if ((Description->flags & AV_PIX_FMT_FLAG_RGB) != 0)
	{
		Info.Chroma = TEXT("RGB");
	}
	else if (Description->nb_components < 3)
	{
		Info.Chroma = TEXT("4:0:0");
	}
	else
	{
		const int32 ChromaWidth = 4 >> Description->log2_chroma_w;
		Info.Chroma = FString::Printf(TEXT("4:%d:%d"), ChromaWidth,
			Description->log2_chroma_h > 0 ? 0 : ChromaWidth);
	}
}

void DTCctvParseSdpInfo(const FString& Sdp, FDTCctvStreamInfo& Info)
{
	TArray<FString> Lines;
	Sdp.ParseIntoArrayLines(Lines);
	bool bAudio = false;
	for (FString Line : Lines)
	{
		Line.TrimStartAndEndInline();
		if (Line.StartsWith(TEXT("m=")))
		{
			Info.bSdpKnown = true;
			bAudio = Line.StartsWith(TEXT("m=audio "));
			Info.AudioTracks += bAudio ? 1 : 0;
			Info.VideoTracks += Line.StartsWith(TEXT("m=video ")) ? 1 : 0;
		}
		else if (Line.StartsWith(TEXT("s=")))
		{
			Info.SessionName = DTCctvMaskUrlCredentials(Line.Mid(2));
		}
		else if (Line.StartsWith(TEXT("a=tool:")))
		{
			Info.ServerTool = DTCctvMaskUrlCredentials(Line.Mid(7));
		}
		else if (bAudio && Line.StartsWith(TEXT("a=rtpmap:")))
		{
			FString Payload;
			FString Encoding;
			if (Line.Split(TEXT(" "), &Payload, &Encoding))
			{
				FString Codec;
				FString Clock;
				Encoding.Split(TEXT("/"), &Codec, &Clock);
				Info.AudioCodecs.AddUnique(Codec.IsEmpty() ? Encoding : Codec);
			}
		}
	}
}

TMap<FString, FString> DTCctvInfoFields(const FDTCctvStreamInfo& Info)
{
	TMap<FString, FString> Fields;
	Fields.Add(TEXT("주소"), Info.SafeUrl);
	Fields.Add(TEXT("전송"), Info.Transport);
	Fields.Add(TEXT("서버도구"), Info.ServerTool.IsEmpty() ? TEXT("미제공") : Info.ServerTool);
	Fields.Add(TEXT("세션"), Info.SessionName.IsEmpty() ? TEXT("미제공") : Info.SessionName);
	Fields.Add(TEXT("비디오트랙"), FString::Printf(TEXT("%d, 선택=%d, AV인덱스=%d"), Info.VideoTracks, Info.SelectedVideoTrack, Info.SelectedStreamIndex));
	Fields.Add(TEXT("코덱"), Info.CodecName);
	Fields.Add(TEXT("프로파일"), Info.Profile);
	Fields.Add(TEXT("레벨"), Info.Level);
	Fields.Add(TEXT("해상도"), FString::Printf(TEXT("%dx%d"), Info.SourceSize.X, Info.SourceSize.Y));
	Fields.Add(TEXT("픽셀형식"), Info.PixelFormat.IsEmpty() ? TEXT("미확인") : Info.PixelFormat);
	Fields.Add(TEXT("전송후형식"), Info.FramePixelFormat.IsEmpty() ? TEXT("미확인") : Info.FramePixelFormat);
	Fields.Add(TEXT("비트깊이"), FString::FromInt(Info.BitDepth));
	Fields.Add(TEXT("크로마"), Info.Chroma);
	Fields.Add(TEXT("선언FPS"), FString::Printf(TEXT("%.3f"), Info.DeclaredFps));
	Fields.Add(TEXT("선언B프레임"), Info.bDeclaredBFrames ? TEXT("있음") : TEXT("없음"));
	Fields.Add(TEXT("오디오"), Info.bSdpKnown ? FString::Printf(TEXT("%d트랙 %s"), Info.AudioTracks, *FString::Join(Info.AudioCodecs, TEXT(","))) : TEXT("SDP 미확인"));
	Fields.Add(TEXT("디코딩"), Info.DecoderName);
	Fields.Add(TEXT("선택이유"), Info.DecoderReason);
	return Fields;
}

FString DTCctvInfoSummary(const FDTCctvStreamInfo& Info)
{
	const TMap<FString, FString> Fields = DTCctvInfoFields(Info);
	TArray<FString> Keys;
	Fields.GetKeys(Keys);
	Keys.Sort();
	TArray<FString> Parts;
	for (const FString& Key : Keys)
	{
		Parts.Add(Key + TEXT("=") + Fields[Key]);
	}
	return FString::Join(Parts, TEXT(" | "));
}

FString DTCctvInfoChanges(const FDTCctvStreamInfo& Previous, const FDTCctvStreamInfo& Current)
{
	const TMap<FString, FString> Before = DTCctvInfoFields(Previous);
	const TMap<FString, FString> After = DTCctvInfoFields(Current);
	TArray<FString> Keys;
	After.GetKeys(Keys);
	Keys.Sort();
	TArray<FString> Parts;
	for (const FString& Key : Keys)
	{
		if (Before.FindRef(Key) != After[Key])
		{
			Parts.Add(Key + TEXT("=") + After[Key]);
		}
	}
	return FString::Join(Parts, TEXT(" | "));
}

void FDTCctvStreamProbe::Record(double Now, double MediaSeconds, bool bKey, bool bBFrame)
{
	if (Frames == 0)
	{
		Start = Now;
	}
	bSawB |= bBFrame;
	if (bKey)
	{
		const double KeyTime = MediaSeconds >= 0.0 ? MediaSeconds : Now;
		if (bSawKey && KeyTime > LastKeyTime)
		{
			GopSeconds = KeyTime - LastKeyTime;
			GopFrames = static_cast<int32>(Frames - LastKeyFrame);
		}
		LastKeyFrame = Frames;
		LastKeyTime = KeyTime;
		bSawKey = true;
	}
	++Frames;
}

void FDTCctvStreamProbe::Fill(FDTCctvStreamInfo& Info, int64 PayloadBytes, double Now) const
{
	const double Seconds = Now - Start;
	Info.bStabilized = IsStable(Now);
	Info.MeasuredFps = Seconds > 0.0 ? FMath::Max<int64>(0, Frames - 1) / Seconds : 0.0;
	Info.MeasuredBitrateBps = Seconds > 0.0 ? FMath::RoundToInt64(PayloadBytes * 8.0 / Seconds) : 0;
	Info.bMeasuredGop = GopFrames > 0;
	Info.GopFrames = GopFrames;
	Info.GopSeconds = GopSeconds;
	Info.bObservedBFrames = bSawB;
}
