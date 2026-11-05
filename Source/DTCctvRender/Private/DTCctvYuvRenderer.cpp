#include "DTCctvYuvRenderer.h"

#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "ShaderParameterStruct.h"

class FDTCctvYuvPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDTCctvYuvPS);
	SHADER_USE_PARAMETER_STRUCT(FDTCctvYuvPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, YTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, UTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, VTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER(FVector4f, RowR)
		SHADER_PARAMETER(FVector4f, RowG)
		SHADER_PARAMETER(FVector4f, RowB)
		SHADER_PARAMETER(FVector2f, InvOutputSize)
		SHADER_PARAMETER(FVector2f, TapOffset)
		SHADER_PARAMETER(uint32, bSemiPlanar)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FDTCctvYuvPS, "/DTCctv/Private/DTCctvYuv.usf", "MainPS", SF_Pixel);

namespace
{
	FTextureRHIRef CreatePlaneTexture(const TCHAR* Name, FIntPoint Size, EPixelFormat Format)
	{
		const FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(Name)
			.SetExtent(Size)
			.SetFormat(Format)
			.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::Dynamic)
			.SetInitialState(ERHIAccess::SRVMask);
		return RHICreateTexture(Desc);
	}

	void UploadPlane(FRHITexture* Texture, const uint8* Data, int32 Pitch, FIntPoint Size)
	{
		const FUpdateTextureRegion2D Region(0, 0, 0, 0, Size.X, Size.Y);
		RHIUpdateTexture2D(Texture->GetTexture2D(), 0, Region, Pitch, Data);
	}
}

FDTCctvColorTransform DTCctvMakeColorTransform(EDTCctvYuvMatrix Matrix, bool bFullRange)
{
	float Kr = 0.2126f;
	float Kb = 0.0722f;
	if (Matrix == EDTCctvYuvMatrix::Bt601)
	{
		Kr = 0.299f;
		Kb = 0.114f;
	}
	else if (Matrix == EDTCctvYuvMatrix::Bt2020)
	{
		Kr = 0.2627f;
		Kb = 0.0593f;
	}
	const float Kg = 1.f - Kr - Kb;

	const float YScale = bFullRange ? 1.f : 255.f / 219.f;
	const float CScale = bFullRange ? 1.f : 255.f / 224.f;
	const float YOffset = bFullRange ? 0.f : 16.f / 255.f;
	const float COffset = 128.f / 255.f;

	const float CrToR = CScale * 2.f * (1.f - Kr);
	const float CbToB = CScale * 2.f * (1.f - Kb);
	const float CbToG = -CScale * 2.f * (1.f - Kb) * Kb / Kg;
	const float CrToG = -CScale * 2.f * (1.f - Kr) * Kr / Kg;
	const float YBase = -YScale * YOffset;

	FDTCctvColorTransform Result;
	Result.RowR = FVector4f(YScale, 0.f, CrToR, YBase - CrToR * COffset);
	Result.RowG = FVector4f(YScale, CbToG, CrToG, YBase - (CbToG + CrToG) * COffset);
	Result.RowB = FVector4f(YScale, CbToB, 0.f, YBase - CbToB * COffset);
	return Result;
}

void FDTCctvYuvRenderer::EnsurePlaneTextures(const FDTCctvYuvPlanes& Planes)
{
	if (YTexture.IsValid() && CachedSize == Planes.Size && bCachedSemiPlanar == Planes.bSemiPlanar)
	{
		return;
	}

	const FIntPoint ChromaSize((Planes.Size.X + 1) / 2, (Planes.Size.Y + 1) / 2);
	YTexture = CreatePlaneTexture(TEXT("DTCctvY"), Planes.Size, PF_G8);
	if (Planes.bSemiPlanar)
	{
		UTexture = CreatePlaneTexture(TEXT("DTCctvUV"), ChromaSize, PF_R8G8);
		VTexture = UTexture;
	}
	else
	{
		UTexture = CreatePlaneTexture(TEXT("DTCctvU"), ChromaSize, PF_G8);
		VTexture = CreatePlaneTexture(TEXT("DTCctvV"), ChromaSize, PF_G8);
	}
	CachedSize = Planes.Size;
	bCachedSemiPlanar = Planes.bSemiPlanar;
	PlaneTextureRebuilds.fetch_add(1);
}

void FDTCctvYuvRenderer::Render(FRHICommandListImmediate& RHICmdList, const FDTCctvYuvPlanes& Planes, FRHITexture* Target)
{
	check(IsInRenderingThread());
	if (Target == nullptr || Planes.Data[0] == nullptr || Planes.Data[1] == nullptr)
	{
		return;
	}

	EnsurePlaneTextures(Planes);

	const FIntPoint ChromaSize((Planes.Size.X + 1) / 2, (Planes.Size.Y + 1) / 2);
	UploadPlane(YTexture, Planes.Data[0], Planes.Pitch[0], Planes.Size);
	UploadPlane(UTexture, Planes.Data[1], Planes.Pitch[1], ChromaSize);
	if (!Planes.bSemiPlanar)
	{
		UploadPlane(VTexture, Planes.Data[2], Planes.Pitch[2], ChromaSize);
	}

	const FIntPoint OutputSize = Target->GetSizeXY();

	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("DTCctvYuv"));
	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Target, TEXT("DTCctvOutput")));

	FDTCctvYuvPS::FParameters* Parameters = GraphBuilder.AllocParameters<FDTCctvYuvPS::FParameters>();
	Parameters->YTexture = YTexture;
	Parameters->UTexture = UTexture;
	Parameters->VTexture = VTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->RowR = Planes.Transform.RowR;
	Parameters->RowG = Planes.Transform.RowG;
	Parameters->RowB = Planes.Transform.RowB;
	Parameters->InvOutputSize = FVector2f(1.f / OutputSize.X, 1.f / OutputSize.Y);
	// 원본과 같은 크기에서는 흐려지지 않도록 탭 간격을 0으로 둔다.
	const bool bDownscaled = OutputSize.X < Planes.Size.X || OutputSize.Y < Planes.Size.Y;
	Parameters->TapOffset = bDownscaled ? FVector2f(0.25f / OutputSize.X, 0.25f / OutputSize.Y) : FVector2f::ZeroVector;
	Parameters->bSemiPlanar = Planes.bSemiPlanar ? 1u : 0u;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ENoAction);

	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FDTCctvYuvPS> PixelShader(ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("DTCctvYuvToRgb"),
		PixelShader,
		Parameters,
		FIntRect(FIntPoint::ZeroValue, OutputSize));

	GraphBuilder.SetTextureAccessFinal(OutputTexture, ERHIAccess::SRVMask);
	GraphBuilder.Execute();
}
