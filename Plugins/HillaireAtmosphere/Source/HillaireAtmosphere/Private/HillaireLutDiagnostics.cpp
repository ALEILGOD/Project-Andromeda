#include "HillaireLutDiagnostics.h"

#include "HillaireAtmosphereLog.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RenderCommandFence.h"
#include "RenderTargetPool.h"
#include "RHI.h"
#include "RHICommandList.h"

namespace HillaireLutDiagnostics
{
	FHillaireLutStats AnalyzeLut(const TArray<FLinearColor>& Data)
	{
		FHillaireLutStats Stats;
		for (const FLinearColor& C : Data)
		{
			++Stats.TexelCount;
			const float V[3] = { C.R, C.G, C.B };
			bool bRowValid = true;
			for (int32 i = 0; i < 3; ++i)
			{
				const float X = V[i];
				if (FMath::IsNaN(X))
				{
					++Stats.NaNCount;
					bRowValid = false;
					continue;
				}
				if (!FMath::IsFinite(X))
				{
					++Stats.InfCount;
					bRowValid = false;
					continue;
				}
				if (i == 0) { Stats.MinR = FMath::Min(Stats.MinR, X); Stats.MaxR = FMath::Max(Stats.MaxR, X); Stats.SumR += X; }
				if (i == 1) { Stats.MinG = FMath::Min(Stats.MinG, X); Stats.MaxG = FMath::Max(Stats.MaxG, X); Stats.SumG += X; }
				if (i == 2) { Stats.MinB = FMath::Min(Stats.MinB, X); Stats.MaxB = FMath::Max(Stats.MaxB, X); Stats.SumB += X; }
			}
			if (bRowValid)
			{
				const float Lum = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
				Stats.MinLum = FMath::Min(Stats.MinLum, Lum);
				Stats.MaxLum = FMath::Max(Stats.MaxLum, Lum);
				Stats.SumLum += Lum;
			}
		}
		if (Stats.TexelCount == 0)
		{
			Stats.MinR = Stats.MinG = Stats.MinB = 0.0f;
			Stats.MaxR = Stats.MaxG = Stats.MaxB = 0.0f;
			Stats.MinLum = Stats.MaxLum = 0.0f;
		}
		return Stats;
	}

	FString DescribeStats(const TCHAR* LutName, const FHillaireLutStats& Stats)
	{
		return FString::Printf(TEXT("%s: texels=%lld min=(%.6f,%.6f,%.6f) max=(%.6f,%.6f,%.6f) avg=(%.6f,%.6f,%.6f) lumMin=%.6f lumMax=%.6f lumAvg=%.6f NaN=%lld Inf=%lld"),
			LutName, Stats.TexelCount,
			Stats.MinR, Stats.MinG, Stats.MinB,
			Stats.MaxR, Stats.MaxG, Stats.MaxB,
			Stats.AverageR(), Stats.AverageG(), Stats.AverageB(),
			Stats.MinLum, Stats.MaxLum, Stats.AverageLuminance(),
			Stats.NaNCount, Stats.InfCount);
	}

	bool ReadbackPooledLut(
		TRefCountPtr<IPooledRenderTarget>& PooledTarget, TArray<FLinearColor>& OutData)
	{
		OutData.Reset();
		if (!PooledTarget.IsValid() || !PooledTarget->GetRHI())
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("ReadbackPooledLut: no pooled target."));
			return false;
		}
		if (!IsInGameThread())
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("ReadbackPooledLut: must run on the game thread."));
			return false;
		}

		FTextureRHIRef TargetTexture = PooledTarget->GetRHI();
		const FIntPoint Size = TargetTexture->GetSizeXY();
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("ReadbackPooledLut: reading %dx%d PF_FloatRGBA."),
			Size.X, Size.Y);

		TSharedRef<TArray<FLinearColor>> Pixels = MakeShared<TArray<FLinearColor>>();
		ENQUEUE_RENDER_COMMAND(HillaireLutReadback)(
			[TargetTexture, Pixels, Size](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.ReadSurfaceData(
					TargetTexture,
					FIntRect(0, 0, Size.X, Size.Y),
					*Pixels,
					FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX));
			});
		FRenderCommandFence Fence;
		Fence.BeginFence();
		Fence.Wait();
		OutData = MoveTemp(*Pixels);
		return OutData.Num() == Size.X * Size.Y;
	}

	bool ReadbackPooledVolume(
		TRefCountPtr<IPooledRenderTarget>& PooledTarget,
		TArray<FLinearColor>& OutData, FIntVector& OutDims)
	{
		OutData.Reset();
		OutDims = FIntVector::ZeroValue;
		if (!PooledTarget.IsValid() || !PooledTarget->GetRHI())
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("ReadbackPooledVolume: no pooled target."));
			return false;
		}
		if (!IsInGameThread())
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("ReadbackPooledVolume: must run on the game thread."));
			return false;
		}

		FTextureRHIRef TargetTexture = PooledTarget->GetRHI();
		const FIntVector Size = TargetTexture->GetSizeXYZ();
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("ReadbackPooledVolume: reading %dx%dx%d PF_FloatRGBA."),
			Size.X, Size.Y, Size.Z);

		TSharedRef<TArray<FFloat16Color>> Pixels16 = MakeShared<TArray<FFloat16Color>>();
		ENQUEUE_RENDER_COMMAND(HillaireVolumeReadback)(
			[TargetTexture, Pixels16, Size](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.Read3DSurfaceFloatData(
					TargetTexture,
					FIntRect(0, 0, Size.X, Size.Y),
					FIntPoint(0, Size.Z),
					*Pixels16,
					FReadSurfaceDataFlags());
			});
		FRenderCommandFence Fence;
		Fence.BeginFence();
		Fence.Wait();
		OutDims = Size;
		OutData.Reserve(Pixels16->Num());
		for (const FFloat16Color& H : *Pixels16)
		{
			OutData.Add(FLinearColor((float)H.R, (float)H.G, (float)H.B, (float)H.A));
		}
		return OutData.Num() == Size.X * Size.Y * Size.Z;
	}

	bool SaveLutPreviewPng(
		const TArray<FLinearColor>& Data, int32 W, int32 H,
		float Exposure, const FString& FilePath)
	{
		if (Data.Num() != W * H || W <= 0 || H <= 0)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("SaveLutPreviewPng: bad dimensions for %s."), *FilePath);
			return false;
		}
		// Presentation-only tonemap (never grading): filmic-ish toe via
		// 1-exp(-L*ev), then gamma 2.2. Invalid texels render magenta so
		// corruption is VISIBLE in validation images instead of hidden.
		TArray64<uint8> Bgra;
		Bgra.AddUninitialized((int64)W * (int64)H * 4);
		for (int32 i = 0; i < Data.Num(); ++i)
		{
			const FLinearColor& C = Data[i];
			uint8 R, G, B;
			if (FMath::IsNaN(C.R) || FMath::IsNaN(C.G) || FMath::IsNaN(C.B)
				|| !FMath::IsFinite(C.R) || !FMath::IsFinite(C.G) || !FMath::IsFinite(C.B))
			{
				R = 255; G = 0; B = 255;
			}
			else
			{
				auto Tonemap = [Exposure](float L) -> uint8
				{
					const float T = 1.0f - FMath::Exp(-FMath::Max(0.0f, L) * Exposure);
					return (uint8)FMath::Clamp(FMath::RoundToInt(255.0f * FMath::Pow(T, 1.0f / 2.2f)), 0, 255);
				};
				R = Tonemap(C.R); G = Tonemap(C.G); B = Tonemap(C.B);
			}
			Bgra[i * 4 + 0] = B;
			Bgra[i * 4 + 1] = G;
			Bgra[i * 4 + 2] = R;
			Bgra[i * 4 + 3] = 255;
		}
		IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetRaw(Bgra.GetData(), Bgra.Num(), W, H, ERGBFormat::BGRA, 8))
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("SaveLutPreviewPng: wrapper failed for %s."), *FilePath);
			return false;
		}
		if (!FFileHelper::SaveArrayToFile(Wrapper->GetCompressed(), *FilePath))
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("SaveLutPreviewPng: write failed for %s."), *FilePath);
			return false;
		}
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("SaveLutPreviewPng: %s (%dx%d, exposure %.3f)."),
			*FilePath, W, H, Exposure);
		return true;
	}
}
