#include "DTCctvVideoFrame.h"

#include "DTCctvFFmpegInclude.h"
#include "HAL/PlatformTime.h"

FDTCctvVideoFrame::FDTCctvVideoFrame(AVFrame* InFrame, FIntPoint InSourceSize)
	: Frame(InFrame)
	, SourceSize(InSourceSize)
	, DecodedSeconds(FPlatformTime::Seconds())
{
}

FDTCctvVideoFrame::~FDTCctvVideoFrame()
{
	av_frame_free(&Frame);
}

FIntPoint FDTCctvVideoFrame::GetSize() const
{
	return FIntPoint(Frame->width, Frame->height);
}

FDTCctvYuvPlanes FDTCctvVideoFrame::GetPlanes() const
{
	FDTCctvYuvPlanes Planes;
	Planes.Size = GetSize();
	Planes.bSemiPlanar = Frame->format == AV_PIX_FMT_NV12;
	const int32 PlaneCount = Planes.bSemiPlanar ? 2 : 3;
	for (int32 Index = 0; Index < PlaneCount; ++Index)
	{
		Planes.Data[Index] = Frame->data[Index];
		Planes.Pitch[Index] = Frame->linesize[Index];
	}

	EDTCctvYuvMatrix Matrix;
	switch (Frame->colorspace)
	{
	case AVCOL_SPC_BT709:
		Matrix = EDTCctvYuvMatrix::Bt709;
		break;
	case AVCOL_SPC_BT470BG:
	case AVCOL_SPC_SMPTE170M:
		Matrix = EDTCctvYuvMatrix::Bt601;
		break;
	case AVCOL_SPC_BT2020_NCL:
	case AVCOL_SPC_BT2020_CL:
		Matrix = EDTCctvYuvMatrix::Bt2020;
		break;
	default:
		// 스트림에 정보가 없으면 해상도로 추정한다.
		Matrix = SourceSize.Y >= 720 ? EDTCctvYuvMatrix::Bt709 : EDTCctvYuvMatrix::Bt601;
		break;
	}

	const bool bFullRange = Frame->color_range == AVCOL_RANGE_JPEG || Frame->format == AV_PIX_FMT_YUVJ420P;
	Planes.Transform = DTCctvMakeColorTransform(Matrix, bFullRange);
	return Planes;
}
