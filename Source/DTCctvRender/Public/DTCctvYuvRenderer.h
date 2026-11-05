#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include <atomic>

class FRHICommandListImmediate;

enum class EDTCctvYuvMatrix : uint8
{
	Bt601,
	Bt709,
	Bt2020
};

/** (Y, Cb, Cr, 1)과 내적해 감마 RGB를 얻는 행 벡터. */
struct FDTCctvColorTransform
{
	FVector4f RowR = FVector4f(1.f, 0.f, 0.f, 0.f);
	FVector4f RowG = FVector4f(1.f, 0.f, 0.f, 0.f);
	FVector4f RowB = FVector4f(1.f, 0.f, 0.f, 0.f);
};

DTCCTVRENDER_API FDTCctvColorTransform DTCctvMakeColorTransform(EDTCctvYuvMatrix Matrix, bool bFullRange);

/** 한 프레임의 평면 데이터. 세미플래너(NV12)이면 Data[1]이 CbCr 평면이고 Data[2]는 쓰지 않는다. */
struct FDTCctvYuvPlanes
{
	const uint8* Data[3] = { nullptr, nullptr, nullptr };
	int32 Pitch[3] = { 0, 0, 0 };
	FIntPoint Size = FIntPoint::ZeroValue;
	bool bSemiPlanar = true;
	FDTCctvColorTransform Transform;
};

/** 평면 텍스처를 재사용하며 YUV 프레임을 렌더 타깃으로 변환한다. 렌더 스레드 전용. */
class DTCCTVRENDER_API FDTCctvYuvRenderer
{
public:
	void Render(FRHICommandListImmediate& RHICmdList, const FDTCctvYuvPlanes& Planes, FRHITexture* Target);
	int32 GetPlaneTextureRebuilds() const { return PlaneTextureRebuilds.load(); }

private:
	void EnsurePlaneTextures(const FDTCctvYuvPlanes& Planes);

	FTextureRHIRef YTexture;
	FTextureRHIRef UTexture;
	FTextureRHIRef VTexture;
	FIntPoint CachedSize = FIntPoint::ZeroValue;
	bool bCachedSemiPlanar = false;
	std::atomic<int32> PlaneTextureRebuilds{ 0 };
};
