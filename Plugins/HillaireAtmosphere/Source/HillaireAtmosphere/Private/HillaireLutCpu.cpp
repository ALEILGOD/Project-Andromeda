#include "HillaireLutCpu.h"

#include "HillaireAtmosphereLog.h"

namespace HillaireLutCpu
{
	FMediumSample SampleMedium(const FHillaireAtmosphereProfile& Profile, const FVector3f& WorldPosPlanetLocal)
	{
		const float ViewHeight = WorldPosPlanetLocal.Size() - Profile.BottomRadiusKm;

		const float DensityMie = FMath::Exp(Profile.MieExpScale * ViewHeight);
		const float DensityRay = FMath::Exp(Profile.RayleighExpScale * ViewHeight);
		const float DensityOzo = FMath::Clamp(
			ViewHeight < Profile.AbsorptionWidthKm
				? Profile.AbsorptionLinear0 * ViewHeight + Profile.AbsorptionConstant0
				: Profile.AbsorptionLinear1 * ViewHeight + Profile.AbsorptionConstant1,
			0.0f, 1.0f);

		FMediumSample S;
		S.ScatteringMie = DensityMie * FVector3f((float)Profile.MieScatteringKm.X, (float)Profile.MieScatteringKm.Y, (float)Profile.MieScatteringKm.Z);
		S.ScatteringRay = DensityRay * FVector3f((float)Profile.RayleighScatteringKm.X, (float)Profile.RayleighScatteringKm.Y, (float)Profile.RayleighScatteringKm.Z);
		const FVector3f ExtMie = DensityMie * FVector3f((float)Profile.MieExtinctionKm.X, (float)Profile.MieExtinctionKm.Y, (float)Profile.MieExtinctionKm.Z);
		const FVector3f AbsOzo = DensityOzo * FVector3f((float)Profile.AbsorptionExtinctionKm.X, (float)Profile.AbsorptionExtinctionKm.Y, (float)Profile.AbsorptionExtinctionKm.Z);
		const FVector3f AbsMie = DensityMie * FVector3f((float)Profile.MieAbsorptionKm.X, (float)Profile.MieAbsorptionKm.Y, (float)Profile.MieAbsorptionKm.Z);
		S.Scattering = S.ScatteringMie + S.ScatteringRay;
		S.Absorption = AbsMie + AbsOzo;
		S.Extinction = ExtMie + S.ScatteringRay + AbsOzo;
		return S;
	}

	bool RaySphereNearest(const FVector3f& R0, const FVector3f& Rd, float Radius, float& OutT)
	{
		return RaySphereNearestCenter(R0, Rd, FVector3f::ZeroVector, Radius, OutT);
	}

	bool RaySphereNearestCenter(
		const FVector3f& R0, const FVector3f& Rd,
		const FVector3f& Center, float Radius, float& OutT)
	{
		const FVector3f S0R0 = R0 - Center;
		const float A = Rd | Rd;
		const float B = 2.0f * (Rd | S0R0);
		const float C = (S0R0 | S0R0) - Radius * Radius;
		const float Delta = B * B - 4.0f * A * C;
		if (Delta < 0.0f || A == 0.0f)
		{
			return false;
		}
		const float SqrtD = FMath::Sqrt(Delta);
		const float Sol0 = (-B - SqrtD) / (2.0f * A);
		const float Sol1 = (-B + SqrtD) / (2.0f * A);
		if (Sol0 < 0.0f && Sol1 < 0.0f)
		{
			return false;
		}
		if (Sol0 < 0.0f)
		{
			OutT = FMath::Max(0.0f, Sol1);
		}
		else if (Sol1 < 0.0f)
		{
			OutT = FMath::Max(0.0f, Sol0);
		}
		else
		{
			OutT = FMath::Max(0.0f, FMath::Min(Sol0, Sol1));
		}
		return true;
	}

	void UvToTransmittanceParams(
		const FHillaireAtmosphereProfile& Profile, float U, float V,
		float& OutViewHeightKm, float& OutViewZenithCos)
	{
		const float Bottom = Profile.BottomRadiusKm;
		const float Top = Profile.TopRadiusKm;
		const float H = FMath::Sqrt(Top * Top - Bottom * Bottom);
		const float Rho = H * V;
		OutViewHeightKm = FMath::Sqrt(Rho * Rho + Bottom * Bottom);

		const float DMin = Top - OutViewHeightKm;
		const float DMax = Rho + H;
		const float D = DMin + U * (DMax - DMin);
		float Cos = D == 0.0f ? 1.0f : (H * H - Rho * Rho - D * D) / (2.0f * OutViewHeightKm * D);
		OutViewZenithCos = FMath::Clamp(Cos, -1.0f, 1.0f);
	}

	// Reference-derived sampler scale adaptation, CPU mirror of
	// HillaireLutCore.ush (HillaireMinDensityScaleKm /
	// HillaireEnvelopeAwareSampleCount). Earth outputs stay bit-identical:
	// H_min = 1.2 km -> threshold 60 km, reference envelope 100 km.
	static float MinDensityScaleKm(const FHillaireAtmosphereProfile& Profile)
	{
		return 1.0f / FMath::Max(1e-4f,
			FMath::Max(-Profile.RayleighExpScale, -Profile.MieExpScale));
	}

	static float EnvelopeAwareSampleCount(
		float SampleCountIni, float TMaxKm, float InMinDensityScaleKm)
	{
		const float ReferenceEnvelopeKm =
			HillaireLimits::ReferenceEnvelopeScaleUnits * InMinDensityScaleKm;
		const float EnvelopeFactor = FMath::Clamp(
			FMath::CeilToFloat(TMaxKm / FMath::Max(ReferenceEnvelopeKm, 1e-4f)),
			1.0f, HillaireLimits::MaxEnvelopeSampleScale);
		return FMath::Min(
			SampleCountIni * EnvelopeFactor,
			(float)HillaireLimits::IntegratorMaxSamples);
	}

	static FVector3f IntegrateOpticalDepth(
		const FHillaireAtmosphereProfile& Profile,
		const FVector3f& WorldPos, const FVector3f& WorldDir, int32 SampleCount)
	{
		const FVector3f Origin = FVector3f::ZeroVector;
		float TBottom, TTop;
		const bool bHitBottom = RaySphereNearest(WorldPos, WorldDir, Profile.BottomRadiusKm, TBottom);
		const bool bHitTop = RaySphereNearest(WorldPos, WorldDir, Profile.TopRadiusKm, TTop);
		float TMax = 0.0f;
		if (!bHitBottom)
		{
			if (!bHitTop)
			{
				return FVector3f::ZeroVector;
			}
			TMax = TTop;
		}
		else if (bHitTop && TTop > 0.0f)
		{
			TMax = FMath::Min(TTop, TBottom);
		}
		// NOTE: when bHitBottom && !(bHitTop > 0), TMax stays 0 (reference:
		// tMax is left 0 when only the ground is hit from inside - the march
		// is empty). This matches the HLSL verbatim.

		// Envelope-aware fixed count (mirror of the HLSL integrator): the
		// reference 40 steps assume a 100 km envelope; containment-enlarged
		// volumes scale the count (bounded) so the dense layer is resolved.
		const int32 MarchCount = FMath::Max(1, FMath::RoundToInt(
			EnvelopeAwareSampleCount((float)SampleCount, TMax, MinDensityScaleKm(Profile))));

		FVector3f OD = FVector3f::ZeroVector;
		float T = 0.0f;
		for (int32 S = 0; S < MarchCount; ++S)
		{
			const float NewT = TMax * ((float)S + 0.3f) / (float)MarchCount;
			const float Dt = NewT - T;
			T = NewT;
			const FVector3f P = WorldPos + T * WorldDir;
			const FMediumSample Medium = SampleMedium(Profile, P);
			OD += Medium.Extinction * Dt;
		}
		return OD;
	}

	FVector3f ComputeTransmittancePixel(
		const FHillaireAtmosphereProfile& Profile, int32 X, int32 Y, int32 W, int32 H)
	{
		const float U = ((float)X + 0.5f) / (float)W;
		const float V = ((float)Y + 0.5f) / (float)H;
		float ViewHeight, Cos;
		UvToTransmittanceParams(Profile, U, V, ViewHeight, Cos);
		const FVector3f WorldPos(0.0f, 0.0f, ViewHeight);
		const float SinC = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Cos * Cos));
		const FVector3f WorldDir(0.0f, SinC, Cos);
		const FVector3f OD = IntegrateOpticalDepth(Profile, WorldPos, WorldDir, HillaireLimits::TransmittanceMarchSamples);
		return FVector3f(FMath::Exp(-OD.X), FMath::Exp(-OD.Y), FMath::Exp(-OD.Z));
	}

	void BakeTransmittanceLut(
		const FHillaireAtmosphereProfile& Profile, int32 W, int32 H, TArray<FLinearColor>& OutLut)
	{
		OutLut.Reset(W * H);
		OutLut.AddUninitialized(W * H);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const FVector3f T = ComputeTransmittancePixel(Profile, X, Y, W, H);
				OutLut[Y * W + X] = FLinearColor(T.X, T.Y, T.Z, 1.0f);
			}
		}
	}

	static float Clamp01(float V) { return FMath::Clamp(V, 0.0f, 1.0f); }

	FVector3f SampleLutBilinear(const TArray<FLinearColor>& Lut, int32 W, int32 H, float U, float V)
	{
		check(Lut.Num() == W * H);
		const float FX = Clamp01(U) * (float)(W - 1);
		const float FY = Clamp01(V) * (float)(H - 1);
		const int32 X0 = FMath::Clamp(FMath::FloorToInt(FX), 0, W - 1);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt(FY), 0, H - 1);
		const int32 X1 = FMath::Min(X0 + 1, W - 1);
		const int32 Y1 = FMath::Min(Y0 + 1, H - 1);
		const float TX = FX - (float)X0;
		const float TY = FY - (float)Y0;
		const FLinearColor& C00 = Lut[Y0 * W + X0];
		const FLinearColor& C10 = Lut[Y0 * W + X1];
		const FLinearColor& C01 = Lut[Y1 * W + X0];
		const FLinearColor& C11 = Lut[Y1 * W + X1];
		const FLinearColor C0 = C00 * (1.0f - TX) + C10 * TX;
		const FLinearColor C1 = C01 * (1.0f - TX) + C11 * TX;
		const FLinearColor C = C0 * (1.0f - TY) + C1 * TY;
		return FVector3f(C.R, C.G, C.B);
	}

	static float FromSubUvsToUnit(float U, float Res)
	{
		return (U - 0.5f / Res) * (Res / (Res - 1.0f));
	}

	static float FromUnitToSubUvs(float U, float Res)
	{
		return (U + 0.5f / Res) * (Res / (Res + 1.0f));
	}

	float RayleighPhase(float CosTheta)
	{
		return (3.0f / (16.0f * PI)) * (1.0f + CosTheta * CosTheta);
	}

	float CornetteShanksMiePhase(float G, float CosTheta)
	{
		const float K = 3.0f / (8.0f * PI) * (1.0f - G * G) / (2.0f + G * G);
		return K * (1.0f + CosTheta * CosTheta)
			/ FMath::Pow(1.0f + G * G - 2.0f * G * -CosTheta, 1.5f);
	}

	bool MoveToTopAtmosphere(FVector3f& WorldPos, const FVector3f& WorldDir, float TopRadiusKm)
	{
		const float ViewHeight = WorldPos.Size();
		if (ViewHeight > TopRadiusKm)
		{
			float TTop;
			if (!RaySphereNearest(WorldPos, WorldDir, TopRadiusKm, TTop) || TTop < 0.0f)
			{
				return false;
			}
			const FVector3f Up = WorldPos / ViewHeight;
			WorldPos = WorldPos + WorldDir * TTop - Up * HillaireLimits::PlanetRadiusOffsetKm;
		}
		return true;
	}

	FSingleScatteringResult IntegrateScatteredLuminance(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const FVector3f& WorldPos, const FVector3f& WorldDir, const FVector3f& SunDir,
		bool bGround, int32 SampleCount,
		bool bVariableSampleCount,
		float MinSamples, float MaxSamples,
		bool bMieRayPhase, float MiePhaseG,
		bool bUseMultiScatteringApprox,
		const TArray<FLinearColor>* MultiScatteringLut,
		int32 MsRes,
		float TMaxMax)
	{
		FSingleScatteringResult Result;
		float TBottom, TTop;
		const bool bHitBottom = RaySphereNearest(WorldPos, WorldDir, Profile.BottomRadiusKm, TBottom);
		const bool bHitTop = RaySphereNearest(WorldPos, WorldDir, Profile.TopRadiusKm, TTop);
		float TMax = 0.0f;
		if (!bHitBottom)
		{
			if (!bHitTop)
			{
				return Result;
			}
			TMax = TTop;
		}
		else if (bHitTop && TTop > 0.0f)
		{
			TMax = FMath::Min(TTop, TBottom);
		}
		// Reference line 78 verbatim (see HillaireLutCore.ush). Default 9e6 is
		// a no-op for T/MS/SkyView callers (bit-stable vs Phase 2A/2B).
		TMax = FMath::Min(TMax, TMaxMax);

		float MarchCount = (float)SampleCount;
		float CountFloor = (float)SampleCount;
		float TMaxFloor = TMax;
		bool bDistant = false;
		const float InMinDensityScale = MinDensityScaleKm(Profile);
		if (bVariableSampleCount)
		{
			// A ray that STARTS inside the atmosphere has the dense layer at
			// its origin (surface sky/aerial bakes): use the near-loaded
			// quadratic distribution for long rays too. Rays that start
			// outside use the uniform distant march (density in the middle of
			// the chord). Both are reference branch distributions; on the
			// reference Earth config they are equally valid.
			const bool bRayStartsInside =
				WorldPos.Size() <= Profile.TopRadiusKm + 1e-3f;
			if (TMax > HillaireLimits::DistantMarchScaleUnits * InMinDensityScale)
			{
				MarchCount = FMath::Clamp(
					FMath::CeilToFloat(TMax / (0.5f * InMinDensityScale)),
					16.0f, (float)HillaireLimits::VariableMarchMaxSamples);
				CountFloor = FMath::FloorToFloat(MarchCount);
				TMaxFloor = TMax * CountFloor / MarchCount;
				bDistant = !bRayStartsInside;
			}
			else
			{
				MarchCount = FMath::Lerp(MinSamples, MaxSamples, FMath::Clamp(TMax * 0.01f, 0.0f, 1.0f));
				CountFloor = FMath::FloorToFloat(MarchCount);
				TMaxFloor = TMax * CountFloor / MarchCount;
			}
		}
		else
		{
			// Fixed bakes (Transmittance / MultiScattering): envelope-aware
			// count, mirror of the HLSL integrator. Earth stays 40/20.
			MarchCount = EnvelopeAwareSampleCount((float)SampleCount, TMax, InMinDensityScale);
			CountFloor = MarchCount;
			TMaxFloor = TMax;
		}

		const float UniformPhase = 1.0f / (4.0f * PI);
		const float CosTheta = SunDir | WorldDir;
		const float MiePhaseValue = CornetteShanksMiePhase(MiePhaseG, -CosTheta);
		const float RayleighPhaseValue = RayleighPhase(CosTheta);

		FVector3f L = FVector3f::ZeroVector;
		FVector3f Throughput(1.0f, 1.0f, 1.0f);
		FVector3f OD = FVector3f::ZeroVector;
		float T = 0.0f;
		const int32 StepCount = FMath::Max(1, FMath::RoundToInt(MarchCount));
		for (int32 S = 0; S < StepCount; ++S)
		{
			float Dt;
			if (bVariableSampleCount && !bDistant)
			{
				float T0 = (float)S / CountFloor;
				float T1 = ((float)S + 1.0f) / CountFloor;
				T0 *= T0;
				T1 *= T1;
				T0 *= TMaxFloor;
				T1 = (T1 > 1.0f) ? TMax : TMaxFloor * T1;
				T = T0 + (T1 - T0) * 0.3f;
				Dt = T1 - T0;
			}
			else
			{
				const float NewT = TMax * ((float)S + 0.3f) / MarchCount;
				Dt = NewT - T;
				T = NewT;
			}
			const FVector3f P = WorldPos + T * WorldDir;
			const FMediumSample Medium = SampleMedium(Profile, P);
			const FVector3f SampleOD = Medium.Extinction * Dt;
			const FVector3f SampleT(
				FMath::Exp(-SampleOD.X), FMath::Exp(-SampleOD.Y), FMath::Exp(-SampleOD.Z));
			OD += SampleOD;

			const float PHeight = P.Size();
			const FVector3f Up = P / PHeight;
			const float SunCos = SunDir | Up;
			// Transmittance LUT lookup: params->uv is the FORWARD mapping
			// (HillaireLutTransmittanceParamsToUv); invert numerically via the
			// same closed form used by the reference LutTransmittanceParamsToUv.
			const float H = FMath::Sqrt(Profile.TopRadiusKm * Profile.TopRadiusKm - Profile.BottomRadiusKm * Profile.BottomRadiusKm);
			const float Rho = FMath::Sqrt(FMath::Max(0.0f, PHeight * PHeight - Profile.BottomRadiusKm * Profile.BottomRadiusKm));
			const float Disc = PHeight * PHeight * (SunCos * SunCos - 1.0f) + Profile.TopRadiusKm * Profile.TopRadiusKm;
			const float D = FMath::Max(0.0f, -PHeight * SunCos + FMath::Sqrt(FMath::Max(0.0f, Disc)));
			const float DMin = Profile.TopRadiusKm - PHeight;
			const float DMax = Rho + H;
			const float XMu = (D - DMin) / (DMax - DMin);
			const float XR = Rho / H;
			const FVector3f TransToSun = SampleLutBilinear(TransmittanceLut, TransW, TransH, XMu, XR);

			FVector3f PhaseScat;
			if (bMieRayPhase)
			{
				PhaseScat = Medium.ScatteringMie * MiePhaseValue + Medium.ScatteringRay * RayleighPhaseValue;
			}
			else
			{
				PhaseScat = Medium.Scattering * UniformPhase;
			}

			// Earth shadow (reference verbatim: sphere CENTER lifted by
			// PlanetRadiusOffsetKm along Up, radius unchanged; t >= 0 -> shadowed).
			float TEarthLifted;
			const FVector3f LiftedCenter = Up * HillaireLimits::PlanetRadiusOffsetKm;
			const bool bShadowed = RaySphereNearestCenter(P, SunDir,
				LiftedCenter, Profile.BottomRadiusKm, TEarthLifted)
				&& TEarthLifted >= 0.0f;
			const float EarthShadow = bShadowed ? 0.0f : 1.0f;

			// MS approximation (SkyView path): sample the baked transfer LUT
			// with the GetMultipleScattering UV mapping (bilinear CPU mirror).
			FVector3f MsApprox = FVector3f::ZeroVector;
			if (bUseMultiScatteringApprox && MultiScatteringLut && MultiScatteringLut->Num() == MsRes * MsRes)
			{
				// Same pipeline as HillaireSampleMultiScatteringLut: unit
				// coords -> saturate -> unit-to-sub -> sample. (The CPU
				// bilinear itself approximates HW filtering; documented.)
				const float Mu = FMath::Clamp(SunCos * 0.5f + 0.5f, 0.0f, 1.0f);
				const float Mv = FMath::Clamp((PHeight - Profile.BottomRadiusKm)
					/ (Profile.TopRadiusKm - Profile.BottomRadiusKm), 0.0f, 1.0f);
				const float Su = FromUnitToSubUvs(Mu, (float)MsRes);
				const float Sv_ = FromUnitToSubUvs(Mv, (float)MsRes);
				MsApprox = SampleLutBilinear(*MultiScatteringLut, MsRes, MsRes, Su, Sv_);
			}

			const FVector3f Sv = FVector3f(
				EarthShadow * TransToSun.X * PhaseScat.X + MsApprox.X * Medium.Scattering.X,
				EarthShadow * TransToSun.Y * PhaseScat.Y + MsApprox.Y * Medium.Scattering.Y,
				EarthShadow * TransToSun.Z * PhaseScat.Z + MsApprox.Z * Medium.Scattering.Z);

			const FVector3f MS = Medium.Scattering;
			const FVector3f MSint(
				Medium.Extinction.X > 0.0f ? (MS.X - MS.X * SampleT.X) / Medium.Extinction.X : 0.0f,
				Medium.Extinction.Y > 0.0f ? (MS.Y - MS.Y * SampleT.Y) / Medium.Extinction.Y : 0.0f,
				Medium.Extinction.Z > 0.0f ? (MS.Z - MS.Z * SampleT.Z) / Medium.Extinction.Z : 0.0f);
			Result.MultiScatAs1 += FVector3f(Throughput.X * MSint.X, Throughput.Y * MSint.Y, Throughput.Z * MSint.Z);

			const FVector3f Sint(
				Medium.Extinction.X > 0.0f ? (Sv.X - Sv.X * SampleT.X) / Medium.Extinction.X : 0.0f,
				Medium.Extinction.Y > 0.0f ? (Sv.Y - Sv.Y * SampleT.Y) / Medium.Extinction.Y : 0.0f,
				Medium.Extinction.Z > 0.0f ? (Sv.Z - Sv.Z * SampleT.Z) / Medium.Extinction.Z : 0.0f);
			L += FVector3f(Throughput.X * Sint.X, Throughput.Y * Sint.Y, Throughput.Z * Sint.Z);
			Throughput = FVector3f(Throughput.X * SampleT.X, Throughput.Y * SampleT.Y, Throughput.Z * SampleT.Z);
		}

		if (bGround && TMax == TBottom && TBottom > 0.0f)
		{
			const FVector3f P = WorldPos + TBottom * WorldDir;
			const float PHeight = P.Size();
			const FVector3f Up = P / PHeight;
			const float SunCos = SunDir | Up;
			const float H = FMath::Sqrt(Profile.TopRadiusKm * Profile.TopRadiusKm - Profile.BottomRadiusKm * Profile.BottomRadiusKm);
			const float Rho = FMath::Sqrt(FMath::Max(0.0f, PHeight * PHeight - Profile.BottomRadiusKm * Profile.BottomRadiusKm));
			const float Disc = PHeight * PHeight * (SunCos * SunCos - 1.0f) + Profile.TopRadiusKm * Profile.TopRadiusKm;
			const float D = FMath::Max(0.0f, -PHeight * SunCos + FMath::Sqrt(FMath::Max(0.0f, Disc)));
			const float DMin = Profile.TopRadiusKm - PHeight;
			const float DMax = Rho + H;
			const float XMu = (D - DMin) / (DMax - DMin);
			const float XR = Rho / H;
			const FVector3f TransToSun = SampleLutBilinear(TransmittanceLut, TransW, TransH, XMu, XR);
			const FVector3f UpN = Up.GetSafeNormal();
			const FVector3f SunN = SunDir.GetSafeNormal();
			const float NdotL = FMath::Max(0.0f, UpN | SunN);
			const FVector3f Albedo((float)Profile.GroundAlbedo.X, (float)Profile.GroundAlbedo.Y, (float)Profile.GroundAlbedo.Z);
			L += FVector3f(
				TransToSun.X * Throughput.X * NdotL * Albedo.X / PI,
				TransToSun.Y * Throughput.Y * NdotL * Albedo.Y / PI,
				TransToSun.Z * Throughput.Z * NdotL * Albedo.Z / PI);
		}

		Result.L = L;
		Result.OpticalDepth = OD;
		Result.Transmittance = Throughput;
		return Result;
	}

	FVector3f ComputeMultiScatteringTexel(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		int32 X, int32 Y, int32 Res, float MultipleScatteringFactor)
	{
		const float U = FromSubUvsToUnit((((float)X + 0.5f) / (float)Res), (float)Res);
		const float V = FromSubUvsToUnit((((float)Y + 0.5f) / (float)Res), (float)Res);

		const float CosSun = U * 2.0f - 1.0f;
		const FVector3f SunDir(0.0f, FMath::Sqrt(FMath::Max(0.0f, 1.0f - CosSun * CosSun)), CosSun);
		const float ViewHeight = Profile.BottomRadiusKm + FMath::Clamp(V + HillaireLimits::PlanetRadiusOffsetKm, 0.0f, 1.0f)
			* (Profile.TopRadiusKm - Profile.BottomRadiusKm - HillaireLimits::PlanetRadiusOffsetKm);
		const FVector3f WorldPos(0.0f, 0.0f, ViewHeight);

		const float SphereSolid = 4.0f * PI;
		const float IsoPhase = 1.0f / SphereSolid;
		FVector3f SumAs1 = FVector3f::ZeroVector;
		FVector3f SumL = FVector3f::ZeroVector;
		for (int32 Z = 0; Z < HillaireLimits::MultiScatteringSphereSamples; ++Z)
		{
			const float I = 0.5f + (float)(Z / HillaireLimits::MultiScatteringSphereSqrt);
			const float J = 0.5f + (float)(Z - (Z / HillaireLimits::MultiScatteringSphereSqrt) * HillaireLimits::MultiScatteringSphereSqrt);
			const float RandA = I / (float)HillaireLimits::MultiScatteringSphereSqrt;
			const float RandB = J / (float)HillaireLimits::MultiScatteringSphereSqrt;
			const float Theta = 2.0f * PI * RandA;
			const float Phi = FMath::Acos(FMath::Clamp(1.0f - 2.0f * RandB, -1.0f, 1.0f));
			FVector3f Dir(
				FMath::Cos(Theta) * FMath::Sin(Phi),
				FMath::Sin(Theta) * FMath::Sin(Phi),
				FMath::Cos(Phi));
			const FSingleScatteringResult R = IntegrateScatteredLuminance(
				Profile, TransmittanceLut, TransW, TransH,
				WorldPos, Dir, SunDir, true, HillaireLimits::MultiScatteringMarchSamples);
			SumAs1 += R.MultiScatAs1 * (SphereSolid / 64.0f);
			SumL += R.L * (SphereSolid / 64.0f);
		}
		const FVector3f Fms = SumAs1 * IsoPhase;
		const FVector3f L2 = SumL * IsoPhase;
		// Geometric series 1/(1-r), guarded like the reference domain (r < 1
		// for physical profiles; degenerate input yields +Inf, which the
		// diagnostics test flags rather than hides).
		const FVector3f Closed(
			(1.0f - Fms.X) != 0.0f ? L2.X / (1.0f - Fms.X) : 0.0f,
			(1.0f - Fms.Y) != 0.0f ? L2.Y / (1.0f - Fms.Y) : 0.0f,
			(1.0f - Fms.Z) != 0.0f ? L2.Z / (1.0f - Fms.Z) : 0.0f);
		return Closed * MultipleScatteringFactor;
	}

	void BakeMultiScatteringTexels(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		int32 Res, float MultipleScatteringFactor,
		const TArray<FIntPoint>& Coords, TArray<FLinearColor>& OutValues)
	{
		OutValues.Reset(Coords.Num());
		for (const FIntPoint& C : Coords)
		{
			const FVector3f V = ComputeMultiScatteringTexel(
				Profile, TransmittanceLut, TransW, TransH, C.X, C.Y, Res, MultipleScatteringFactor);
			OutValues.Add(FLinearColor(V.X, V.Y, V.Z, 1.0f));
		}
	}

	void BakeFullMultiScatteringLut(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		int32 Res, float MultipleScatteringFactor, TArray<FLinearColor>& OutLut)
	{
		TArray<FIntPoint> Coords;
		Coords.Reserve(Res * Res);
		for (int32 Y = 0; Y < Res; ++Y)
		{
			for (int32 X = 0; X < Res; ++X)
			{
				Coords.Add(FIntPoint(X, Y));
			}
		}
		BakeMultiScatteringTexels(Profile, TransmittanceLut, TransW, TransH,
			Res, MultipleScatteringFactor, Coords, OutLut);
	}

	void UvToSkyViewParams(
		float BottomRadiusKm, float ViewHeightKm, float U, float V,
		float& OutViewZenithCos, float& OutLightViewCos)
	{
		// Mirrors HillaireUvToSkyViewLutParams (NONLINEARSKYVIEWLUT = 1).
		// Resolutions centralized in HillaireLimits (the .ush keeps the
		// reference 192/108 literals verbatim - same values, single source).
		const float ResW = (float)HillaireLimits::SkyViewWidth;
		const float ResH = (float)HillaireLimits::SkyViewHeight;
		const float UU = (U - 0.5f / ResW) * (ResW / (ResW - 1.0f));
		float VV = (V - 0.5f / ResH) * (ResH / (ResH - 1.0f));

		const float VHor = FMath::Sqrt(FMath::Max(0.0f, ViewHeightKm * ViewHeightKm - BottomRadiusKm * BottomRadiusKm));
		const float CosBeta = VHor / ViewHeightKm;
		const float Beta = FMath::Acos(FMath::Clamp(CosBeta, -1.0f, 1.0f));
		const float ZenithHorizonAngle = PI - Beta;

		if (VV < 0.5f)
		{
			float Coord = 2.0f * VV;
			Coord = 1.0f - Coord;
			Coord *= Coord;
			Coord = 1.0f - Coord;
			OutViewZenithCos = FMath::Cos(ZenithHorizonAngle * Coord);
		}
		else
		{
			float Coord = VV * 2.0f - 1.0f;
			Coord *= Coord;
			OutViewZenithCos = FMath::Cos(ZenithHorizonAngle + Beta * Coord);
		}

		float Coord = UU;
		Coord *= Coord;
		OutLightViewCos = -(Coord * 2.0f - 1.0f);
	}

	FVector3f ComputeSkyViewTexel(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		float ViewHeightKm, const FVector3f& SunDirLocal, const FVector3f& CameraUpLocal,
		int32 X, int32 Y, int32 W, int32 H)
	{
		const float U = ((float)X + 0.5f) / (float)W;
		const float V = ((float)Y + 0.5f) / (float)H;
		float CosV, CosL;
		UvToSkyViewParams(Profile.BottomRadiusKm, ViewHeightKm, U, V, CosV, CosL);

		const FVector3f Up = CameraUpLocal.GetSafeNormal();
		const float SunCos = FMath::Clamp(Up | SunDirLocal, -1.0f, 1.0f);
		const FVector3f SunDirRaw(
			FMath::Sqrt(FMath::Max(0.0f, 1.0f - SunCos * SunCos)), 0.0f, SunCos);
		const FVector3f SunDir = SunDirRaw.GetSafeNormal();

		FVector3f WorldPos(0.0f, 0.0f, ViewHeightKm);
		const float SinV = FMath::Sqrt(FMath::Max(0.0f, 1.0f - CosV * CosV));
		const FVector3f WorldDir(
			SinV * CosL,
			SinV * FMath::Sqrt(FMath::Max(0.0f, 1.0f - CosL * CosL)),
			CosV);

		if (!MoveToTopAtmosphere(WorldPos, WorldDir, Profile.TopRadiusKm))
		{
			return FVector3f::ZeroVector;
		}
		const FSingleScatteringResult R = IntegrateScatteredLuminance(
			Profile, TransmittanceLut, TransW, TransH,
			WorldPos, WorldDir, SunDir, false,
			FMath::RoundToInt(HillaireLimits::SkyViewMarchDeadSampleCountIni),
			true,
			HillaireLimits::SkyViewMarchMinSamples, HillaireLimits::SkyViewMarchMaxSamples,
			true, Profile.MiePhaseG,
			true, &MultiScatteringLut, MsRes);
		return R.L;
	}

	void BakeSkyViewTexels(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		float ViewHeightKm, const FVector3f& SunDirLocal, const FVector3f& CameraUpLocal,
		int32 W, int32 H,
		const TArray<FIntPoint>& Coords, TArray<FLinearColor>& OutValues)
	{
		OutValues.Reset(Coords.Num());
		for (const FIntPoint& C : Coords)
		{
			const FVector3f V = ComputeSkyViewTexel(
				Profile, TransmittanceLut, TransW, TransH,
				MultiScatteringLut, MsRes, ViewHeightKm, SunDirLocal, CameraUpLocal,
				C.X, C.Y, W, H);
			OutValues.Add(FLinearColor(V.X, V.Y, V.Z, 1.0f));
		}
	}

	void BakeFullSkyViewLut(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		float ViewHeightKm, const FVector3f& SunDirLocal, const FVector3f& CameraUpLocal,
		TArray<FLinearColor>& OutLut)
	{
		const int32 W = HillaireLimits::SkyViewWidth;
		const int32 H = HillaireLimits::SkyViewHeight;
		TArray<FIntPoint> Coords;
		Coords.Reserve(W * H);
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				Coords.Add(FIntPoint(X, Y));
			}
		}
		BakeSkyViewTexels(Profile, TransmittanceLut, TransW, TransH,
			MultiScatteringLut, MsRes, ViewHeightKm, SunDirLocal, CameraUpLocal,
			W, H, Coords, OutLut);
	}

	FLinearColor ComputeAerialFroxel(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		const FVector3f& CameraPlanetLocalKm,
		const FMatrix& InvProjMatrix,
		const FMatrix& ViewToPlanetLocalRot,
		const FVector3f& SunDirLocal,
		int32 X, int32 Y, int32 Z, int32 W, int32 H, int32 D)
	{
		// Reference RenderCameraVolumePS lines 808-812. UE row-vector maps
		// (out_j = sum_i V_i * M[i][j], identical to FMatrix::TransformVector4
		// and to the shader's row-vector muls) on the true matrices.
		auto MulPoint = [](const FMatrix& M, const FVector4& V) -> FVector4
		{
			return FVector4(
				M.M[0][0] * V.X + M.M[1][0] * V.Y + M.M[2][0] * V.Z + M.M[3][0] * V.W,
				M.M[0][1] * V.X + M.M[1][1] * V.Y + M.M[2][1] * V.Z + M.M[3][1] * V.W,
				M.M[0][2] * V.X + M.M[1][2] * V.Y + M.M[2][2] * V.Z + M.M[3][2] * V.W,
				M.M[0][3] * V.X + M.M[1][3] * V.Y + M.M[2][3] * V.Z + M.M[3][3] * V.W);
		};
		auto MulDir = [](const FMatrix& M, const FVector& V) -> FVector
		{
			return FVector(
				M.M[0][0] * V.X + M.M[1][0] * V.Y + M.M[2][0] * V.Z,
				M.M[0][1] * V.X + M.M[1][1] * V.Y + M.M[2][1] * V.Z,
				M.M[0][2] * V.X + M.M[1][2] * V.Y + M.M[2][2] * V.Z);
		};
		const FVector2f PixPos((float)X + 0.5f, (float)Y + 0.5f);
		const FVector ClipSpace(
			(PixPos.X / (float)W) * 2.0f - 1.0f,
			-((PixPos.Y / (float)H) * 2.0f - 1.0f),
			0.5f);
		const FVector4 HViewPos = MulPoint(InvProjMatrix, FVector4(ClipSpace, 1.0f));
		const FVector DirView = FVector(HViewPos.X, HViewPos.Y, HViewPos.Z) / HViewPos.W;
		// Non-const: the under-ground clamp-out recomputes it (reference lines 839-840).
		FVector3f WorldDir = FVector3f(MulDir(ViewToPlanetLocalRot, DirView).GetSafeNormal());

		// Squared slice distribution (reference lines 820-822).
		const float SliceCount = (float)D;
		float Slice = ((float)Z + 0.5f) / SliceCount;
		Slice *= Slice;
		Slice *= SliceCount;

		FVector3f WorldPos = CameraPlanetLocalKm;

		// Froxel fragment position (reference lines 829-830). Slice depth is
		// atmosphere-relative (25 slices per envelope; see HillaireLimits).
		const float AerialKmPerSlice = HillaireLimits::AerialKmPerSliceForEnvelope(
			Profile.TopRadiusKm - Profile.BottomRadiusKm);
		float TMax = Slice * AerialKmPerSlice;
		FVector3f NewWorldPos = WorldPos + TMax * WorldDir;

		// Under-ground voxel clamp-out (reference lines 834-841).
		float ViewHeight = NewWorldPos.Size();
		if (ViewHeight <= (Profile.BottomRadiusKm + HillaireLimits::PlanetRadiusOffsetKm))
		{
			NewWorldPos = NewWorldPos.GetSafeNormal()
				* (Profile.BottomRadiusKm + HillaireLimits::PlanetRadiusOffsetKm
					+ HillaireLimits::AerialGroundClampLiftKm);
			WorldDir = (NewWorldPos - CameraPlanetLocalKm).GetSafeNormal();
			TMax = (NewWorldPos - CameraPlanetLocalKm).Size();
		}
		float TMaxMax = TMax;

		// Above-top camera (reference lines 846-863).
		ViewHeight = WorldPos.Size();
		if (ViewHeight >= Profile.TopRadiusKm)
		{
			const FVector3f PrevWorldPos = WorldPos;
			if (!MoveToTopAtmosphere(WorldPos, WorldDir, Profile.TopRadiusKm))
			{
				return FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
			}
			const float LengthToAtmosphere = (PrevWorldPos - WorldPos).Size();
			if (TMaxMax < LengthToAtmosphere)
			{
				return FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
			}
			TMaxMax = FMath::Max(0.0f, TMaxMax - LengthToAtmosphere);
		}

		// March (reference lines 866-871): ground = false, fixed
		// max(1,(sliceId+1)*2) steps, MieRayPhase = true, MS approx ON.
		const int32 SampleCount = FMath::Max(1, (Z + 1) * 2);
		const FSingleScatteringResult R = IntegrateScatteredLuminance(
			Profile, TransmittanceLut, TransW, TransH,
			WorldPos, WorldDir, SunDirLocal, false, SampleCount,
			false,
			HillaireLimits::SkyViewMarchMinSamples, HillaireLimits::SkyViewMarchMaxSamples,
			true, Profile.MiePhaseG,
			true, &MultiScatteringLut, MsRes,
			TMaxMax);
		const float Transmittance = (R.Transmittance.X + R.Transmittance.Y + R.Transmittance.Z) / 3.0f;
		return FLinearColor(R.L.X, R.L.Y, R.L.Z, 1.0f - Transmittance);
	}

	FVector3f SampleVolumeTrilinear(
		const TArray<FLinearColor>& Volume, int32 W, int32 H, int32 D,
		float U, float V, float Wgt)
	{
		check(Volume.Num() == W * H * D);
		auto Fetch = [&](int32 X, int32 Y, int32 Z) -> FVector3f
		{
			X = FMath::Clamp(X, 0, W - 1);
			Y = FMath::Clamp(Y, 0, H - 1);
			Z = FMath::Clamp(Z, 0, D - 1);
			const FLinearColor& C = Volume[(Z * H + Y) * W + X];
			return FVector3f(C.R, C.G, C.B);
		};
		const float FX = FMath::Clamp(U, 0.0f, 1.0f) * (float)(W - 1);
		const float FY = FMath::Clamp(V, 0.0f, 1.0f) * (float)(H - 1);
		const float FZ = FMath::Clamp(Wgt, 0.0f, 1.0f) * (float)(D - 1);
		const int32 X0 = FMath::FloorToInt(FX), Y0 = FMath::FloorToInt(FY), Z0 = FMath::FloorToInt(FZ);
		const float TX = FX - (float)X0, TY = FY - (float)Y0, TZ = FZ - (float)Z0;
		const FVector3f C000 = Fetch(X0, Y0, Z0), C100 = Fetch(X0 + 1, Y0, Z0);
		const FVector3f C010 = Fetch(X0, Y0 + 1, Z0), C110 = Fetch(X0 + 1, Y0 + 1, Z0);
		const FVector3f C001 = Fetch(X0, Y0, Z0 + 1), C101 = Fetch(X0 + 1, Y0, Z0 + 1);
		const FVector3f C011 = Fetch(X0, Y0 + 1, Z0 + 1), C111 = Fetch(X0 + 1, Y0 + 1, Z0 + 1);
		const FVector3f C00 = C000 * (1.0f - TX) + C100 * TX;
		const FVector3f C10 = C010 * (1.0f - TX) + C110 * TX;
		const FVector3f C01 = C001 * (1.0f - TX) + C101 * TX;
		const FVector3f C11 = C011 * (1.0f - TX) + C111 * TX;
		const FVector3f C0 = C00 * (1.0f - TY) + C10 * TY;
		const FVector3f C1 = C01 * (1.0f - TY) + C11 * TY;
		return C0 * (1.0f - TZ) + C1 * TZ;
	}

	FLinearColor CompositeAerialPixel(
		const FLinearColor& SceneColor,
		float DeviceZ,
		float ViewU, float ViewV,
		const FMatrix& InvProjMatrix,
		const TArray<FLinearColor>& Volume, int32 VW, int32 VH, int32 VD,
		const FVector3f& SunColor,
		float PreExposure,
		float AerialKmPerSlice)
	{
		// Sky/background: identity (reversed-Z far == 0).
		if (DeviceZ <= HillaireLimits::CompositeSkyDepthEpsilon)
		{
			return SceneColor;
		}
		// Clip reconstruction (same formula as the shader).
		const FVector ClipSpace(
			ViewU * 2.0f - 1.0f,
			1.0f - ViewV * 2.0f,
			DeviceZ);
		const FVector4 HViewPos(
			InvProjMatrix.M[0][0] * ClipSpace.X + InvProjMatrix.M[0][1] * ClipSpace.Y + InvProjMatrix.M[0][2] * ClipSpace.Z + InvProjMatrix.M[0][3],
			InvProjMatrix.M[1][0] * ClipSpace.X + InvProjMatrix.M[1][1] * ClipSpace.Y + InvProjMatrix.M[1][2] * ClipSpace.Z + InvProjMatrix.M[1][3],
			InvProjMatrix.M[2][0] * ClipSpace.X + InvProjMatrix.M[2][1] * ClipSpace.Y + InvProjMatrix.M[2][2] * ClipSpace.Z + InvProjMatrix.M[2][3],
			InvProjMatrix.M[3][0] * ClipSpace.X + InvProjMatrix.M[3][1] * ClipSpace.Y + InvProjMatrix.M[3][2] * ClipSpace.Z + InvProjMatrix.M[3][3]);
		const FVector ViewPos = FVector(HViewPos.X, HViewPos.Y, HViewPos.Z) / HViewPos.W;
		const float TDepth = ViewPos.Size();

		// Slice mapping + near fade (reference lines 489-497), atmosphere-relative.
		float Slice = TDepth / AerialKmPerSlice;
		float Weight = 1.0f;
		if (Slice < 0.5f)
		{
			Weight = FMath::Clamp(Slice * 2.0f, 0.0f, 1.0f);
			Slice = 0.5f;
		}
		const float Wgt = FMath::Sqrt(Slice / (float)VD);

		// Trilinear volume fetch returns RGB; alpha (opacity) needs the same
		// trilinear weights, so fetch full texels (A included) manually.
		auto FetchA = [&](int32 X, int32 Y, int32 Z) -> float
		{
			X = FMath::Clamp(X, 0, VW - 1);
			Y = FMath::Clamp(Y, 0, VH - 1);
			Z = FMath::Clamp(Z, 0, VD - 1);
			return Volume[(Z * VH + Y) * VW + X].A;
		};
		const float FX = FMath::Clamp(ViewU, 0.0f, 1.0f) * (float)(VW - 1);
		const float FY = FMath::Clamp(ViewV, 0.0f, 1.0f) * (float)(VH - 1);
		const float FZ = FMath::Clamp(Wgt, 0.0f, 1.0f) * (float)(VD - 1);
		const int32 X0 = FMath::FloorToInt(FX), Y0 = FMath::FloorToInt(FY), Z0 = FMath::FloorToInt(FZ);
		const float TX = FX - (float)X0, TY = FY - (float)Y0, TZ = FZ - (float)Z0;
		const float A00 = FetchA(X0, Y0, Z0) * (1.0f - TX) + FetchA(X0 + 1, Y0, Z0) * TX;
		const float A10 = FetchA(X0, Y0 + 1, Z0) * (1.0f - TX) + FetchA(X0 + 1, Y0 + 1, Z0) * TX;
		const float A01 = FetchA(X0, Y0, Z0 + 1) * (1.0f - TX) + FetchA(X0 + 1, Y0, Z0 + 1) * TX;
		const float A11 = FetchA(X0, Y0 + 1, Z0 + 1) * (1.0f - TX) + FetchA(X0 + 1, Y0 + 1, Z0 + 1) * TX;
		const float A0 = A00 * (1.0f - TY) + A10 * TY;
		const float A1 = A01 * (1.0f - TY) + A11 * TY;
		// Reference multiplies the whole float4 (RGB AND alpha) by Weight.
		const float Opacity = (A0 * (1.0f - TZ) + A1 * TZ) * Weight;

		const FVector3f AP = SampleVolumeTrilinear(Volume, VW, VH, VD, ViewU, ViewV, Wgt) * Weight;
		const FVector3f InC(SceneColor.R, SceneColor.G, SceneColor.B);
		const FVector3f OutC = InC * (1.0f - Opacity)
			+ FVector3f(SunColor.X * AP.X, SunColor.Y * AP.Y, SunColor.Z * AP.Z) * PreExposure;
		return FLinearColor(OutC.X, OutC.Y, OutC.Z, SceneColor.A);
	}

	void BakeAerialFroxels(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		const FVector3f& CameraPlanetLocalKm,
		const FMatrix& InvProjMatrix,
		const FMatrix& ViewToPlanetLocalRot,
		const FVector3f& SunDirLocal,
		int32 W, int32 H, int32 D,
		const TArray<FIntVector>& Coords, TArray<FLinearColor>& OutValues)
	{
		OutValues.Reset(Coords.Num());
		for (const FIntVector& C : Coords)
		{
			OutValues.Add(ComputeAerialFroxel(
				Profile, TransmittanceLut, TransW, TransH,
				MultiScatteringLut, MsRes, CameraPlanetLocalKm,
				InvProjMatrix, ViewToPlanetLocalRot, SunDirLocal,
				C.X, C.Y, C.Z, W, H, D));
		}
	}

	void BakeFullAerialVolume(
		const FHillaireAtmosphereProfile& Profile,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		const TArray<FLinearColor>& MultiScatteringLut, int32 MsRes,
		const FVector3f& CameraPlanetLocalKm,
		const FMatrix& InvProjMatrix,
		const FMatrix& ViewToPlanetLocalRot,
		const FVector3f& SunDirLocal,
		TArray<FLinearColor>& OutVolume)
	{
		const int32 S = HillaireLimits::AerialVolumeSize;
		TArray<FIntVector> Coords;
		Coords.Reserve(S * S * S);
		for (int32 Z = 0; Z < S; ++Z)
		{
			for (int32 Y = 0; Y < S; ++Y)
			{
				for (int32 X = 0; X < S; ++X)
				{
					Coords.Add(FIntVector(X, Y, Z));
				}
			}
		}
		BakeAerialFroxels(Profile, TransmittanceLut, TransW, TransH,
			MultiScatteringLut, MsRes, CameraPlanetLocalKm,
			InvProjMatrix, ViewToPlanetLocalRot, SunDirLocal,
			S, S, S, Coords, OutVolume);
	}

	void SkyViewLutParamsToUv(
		float BottomRadiusKm,
		bool bIntersectGround,
		float ViewZenithCos,
		float LightViewCos,
		float ViewHeightKm,
		float& OutU, float& OutV)
	{
		// Mirrors HillaireSkyViewLutParamsToUv (NONLINEARSKYVIEWLUT = 1).
		const float ResW = (float)HillaireLimits::SkyViewWidth;
		const float ResH = (float)HillaireLimits::SkyViewHeight;
		auto FromUnitToSubUvs = [](float U, float Res) -> float
		{
			return (U + 0.5f / Res) * (Res / (Res + 1.0f));
		};

		const float VHor = FMath::Sqrt(FMath::Max(0.0f, ViewHeightKm * ViewHeightKm - BottomRadiusKm * BottomRadiusKm));
		const float CosBeta = FMath::Clamp(VHor / ViewHeightKm, -1.0f, 1.0f);
		const float Beta = FMath::Acos(CosBeta);
		const float ZenithHorizonAngle = PI - Beta;

		float UvY = 0.0f;
		if (!bIntersectGround)
		{
			float Coord = FMath::Acos(FMath::Clamp(ViewZenithCos, -1.0f, 1.0f)) / ZenithHorizonAngle;
			Coord = 1.0f - Coord;
			Coord = FMath::Sqrt(FMath::Max(0.0f, Coord));
			Coord = 1.0f - Coord;
			UvY = Coord * 0.5f;
		}
		else
		{
			float Coord = (FMath::Acos(FMath::Clamp(ViewZenithCos, -1.0f, 1.0f)) - ZenithHorizonAngle) / Beta;
			Coord = FMath::Sqrt(FMath::Max(0.0f, Coord));
			UvY = Coord * 0.5f + 0.5f;
		}

		float CoordX = -FMath::Clamp(LightViewCos, -1.0f, 1.0f) * 0.5f + 0.5f;
		CoordX = FMath::Sqrt(FMath::Max(0.0f, CoordX));

		OutU = FromUnitToSubUvs(CoordX, ResW);
		OutV = FromUnitToSubUvs(FMath::Clamp(UvY, 0.0f, 1.0f), ResH);
	}

	FLinearColor SampleSkyBackgroundPixel(
		const FLinearColor& SceneColor,
		float DeviceZ,
		float ViewU, float ViewV,
		const FMatrix& InvProjMatrix,
		const FMatrix& ViewToPlanetLocalRot,
		const FVector3f& CameraPlanetLocalKm,
		const FVector3f& SunDirLocal,
		const FVector3f& SunColor,
		float BottomRadiusKm,
		float TopRadiusKm,
		float ViewHeightKm,
		const TArray<FLinearColor>& SkyViewLut, int32 SVW, int32 SVH,
		const TArray<FLinearColor>& TransmittanceLut, int32 TransW, int32 TransH,
		float PreExposure)
	{
		// Opaque scene geometry: identity (aerial owns these pixels).
		if (DeviceZ > HillaireLimits::CompositeSkyDepthEpsilon)
		{
			return SceneColor;
		}
		// View ray in the planet-local frame (same fan as the shader: fixed
		// clip z = 0.5, UE row-vector maps on the true matrices).
		auto MulPoint = [](const FMatrix& M, const FVector4& V) -> FVector4
		{
			return FVector4(
				M.M[0][0] * V.X + M.M[1][0] * V.Y + M.M[2][0] * V.Z + M.M[3][0] * V.W,
				M.M[0][1] * V.X + M.M[1][1] * V.Y + M.M[2][1] * V.Z + M.M[3][1] * V.W,
				M.M[0][2] * V.X + M.M[1][2] * V.Y + M.M[2][2] * V.Z + M.M[3][2] * V.W,
				M.M[0][3] * V.X + M.M[1][3] * V.Y + M.M[2][3] * V.Z + M.M[3][3] * V.W);
		};
		auto MulDir = [](const FMatrix& M, const FVector& V) -> FVector
		{
			return FVector(
				M.M[0][0] * V.X + M.M[1][0] * V.Y + M.M[2][0] * V.Z,
				M.M[0][1] * V.X + M.M[1][1] * V.Y + M.M[2][1] * V.Z,
				M.M[0][2] * V.X + M.M[1][2] * V.Y + M.M[2][2] * V.Z);
		};
		const FVector ClipSpace(
			ViewU * 2.0f - 1.0f,
			1.0f - ViewV * 2.0f,
			0.5f);
		const FVector4 HViewPos = MulPoint(InvProjMatrix, FVector4(ClipSpace, 1.0f));
		if (FMath::Abs(HViewPos.W) < 1e-9f)
		{
			return SceneColor;
		}
		const FVector DirView = FVector(HViewPos.X, HViewPos.Y, HViewPos.Z) / HViewPos.W;
		if (DirView.SizeSquared() < 1e-18)
		{
			return SceneColor;
		}
		const FVector3f ViewDirLocal = FVector3f(MulDir(ViewToPlanetLocalRot, DirView).GetSafeNormal());

		const float CamLen = CameraPlanetLocalKm.Size();
		const FVector3f Up = CamLen > 1e-6f ? CameraPlanetLocalKm / CamLen : FVector3f(0.0f, 0.0f, 1.0f);
		const float ViewZenithCos = FMath::Clamp(ViewDirLocal | Up, -1.0f, 1.0f);

		const FVector3f SunDir = SunDirLocal.GetSafeNormal();
		const float SunZenith = SunDir | Up;
		const FVector3f ViewHoriz = ViewDirLocal - Up * ViewZenithCos;
		const FVector3f SunHoriz = SunDir - Up * SunZenith;
		float LightViewCos = 0.0f;
		if (ViewHoriz.SizeSquared() > 1e-12f && SunHoriz.SizeSquared() > 1e-12f)
		{
			LightViewCos = FMath::Clamp(ViewHoriz.GetSafeNormal() | SunHoriz.GetSafeNormal(), -1.0f, 1.0f);
		}

		float GroundT = 0.0f;
		const bool bIntersectGround = RaySphereNearest(CameraPlanetLocalKm, ViewDirLocal, BottomRadiusKm, GroundT);

		float UvU = 0.0f, UvV = 0.0f;
		SkyViewLutParamsToUv(BottomRadiusKm, bIntersectGround, ViewZenithCos, LightViewCos, ViewHeightKm, UvU, UvV);
		const FVector3f SkyTransfer = SampleLutBilinear(SkyViewLut, SVW, SVH, UvU, UvV);
		// View-ray transmittance (reference composite: sky + T * background).
		float TU = 0.0f, TV = 0.0f;
		TransmittanceParamsToUv(BottomRadiusKm, TopRadiusKm, ViewHeightKm, ViewZenithCos, TU, TV);
		const FVector3f ViewTransmittance = SampleLutBilinear(TransmittanceLut, TransW, TransH, TU, TV);
		const float TransmittanceMean =
			(ViewTransmittance.X + ViewTransmittance.Y + ViewTransmittance.Z) / 3.0f;
		const FVector3f OutC(
			SceneColor.R * TransmittanceMean + SunColor.X * SkyTransfer.X * PreExposure,
			SceneColor.G * TransmittanceMean + SunColor.Y * SkyTransfer.Y * PreExposure,
			SceneColor.B * TransmittanceMean + SunColor.Z * SkyTransfer.Z * PreExposure);
		return FLinearColor(OutC.X, OutC.Y, OutC.Z, SceneColor.A);
	}

	void TransmittanceParamsToUv(
		float BottomRadiusKm, float TopRadiusKm,
		float ViewHeightKm, float ViewZenithCos,
		float& OutU, float& OutV)
	{
		// Mirrors HillaireLutTransmittanceParamsToUv (Bruneton 2017).
		const float H = FMath::Sqrt(FMath::Max(0.0f, TopRadiusKm * TopRadiusKm - BottomRadiusKm * BottomRadiusKm));
		const float Rho = FMath::Sqrt(FMath::Max(0.0f, ViewHeightKm * ViewHeightKm - BottomRadiusKm * BottomRadiusKm));
		const float ClampedCos = FMath::Clamp(ViewZenithCos, -1.0f, 1.0f);
		const float Discriminant = ViewHeightKm * ViewHeightKm * (ClampedCos * ClampedCos - 1.0f) + TopRadiusKm * TopRadiusKm;
		const float D = FMath::Max(0.0f, -ViewHeightKm * ClampedCos + FMath::Sqrt(FMath::Max(0.0f, Discriminant)));
		const float DMin = TopRadiusKm - ViewHeightKm;
		const float DMax = Rho + H;
		OutU = (DMax > DMin) ? (D - DMin) / (DMax - DMin) : 0.0f;
		OutV = (H > 0.0f) ? Rho / H : 0.0f;
	}
}
