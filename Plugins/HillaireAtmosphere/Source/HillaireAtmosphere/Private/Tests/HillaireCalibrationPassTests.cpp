// HILLAIRE ATMOSPHERE - CALIBRATION PASS DIAGNOSTICS.
//
// Targeted investigation for three visual issues (measure first, fix after):
//   FogRegimes  - how much aerial in-scatter reaches opaque terrain at
//                 surface / medium / far / entry path lengths (froxel-column
//                 march + composite-level veil for an albedo proxy).
//   InterpError - volume-trilinear sample vs converged direct march
//                 (separates bake-quadrature error from interpolation error,
//                 locating blockiness: angular/lateral vs depth/slice).
//   SkyAmbient  - hemisphere ambient marcher vs hemisphere integral of the
//                 fully-baked SkyView LUT across solar elevations (proves the
//                 terrain ambient driver tracks the visible sky, symmetric
//                 sunrise/sunset, exact night zero).
// All tests log full tables (AddInfo) so the numbers - not guesses - drive
// the calibration decisions.

#include "Misc/AutomationTest.h"

#include "HillaireAtmosphereProfile.h"
#include "HillaireLimits.h"
#include "HillaireLutCpu.h"
#include "HillaireLutManager.h"
#include "HillairePlanetState.h"

namespace
{
	struct FCalibPlanet
	{
		FHillaireAtmosphereProfile Profile;
		TArray<FLinearColor> TransLut;
		TArray<FLinearColor> MsLut;
		int32 TW = 0;
		int32 TH = 0;
		int32 MSR = 0;
		float BottomKm = 0.0f;
		float TopKm = 0.0f;
		float EnvelopeKm = 0.0f;
		float KmPerSlice = 0.0f;

		bool Bake()
		{
			const FHillairePlanetState PS = HillaireMakeExternalPlanetState(
				0, FGuid(0xCA11B001, 0, 0, 0), FName(TEXT("CalibProbe")),
				FVector::ZeroVector, FQuat::Identity,
				5.0f, 1.0f, 0.05f,
				FHillaireAtmosphereProfile::MakeReferenceProfile());
			if (!PS.IsValid())
			{
				return false;
			}
			Profile = PS.Profile;
			TW = HillaireLimits::TransmittanceWidth;
			TH = HillaireLimits::TransmittanceHeight;
			MSR = HillaireLimits::MultiScatteringRes;
			HillaireLutCpu::BakeTransmittanceLut(Profile, TW, TH, TransLut);
			HillaireLutCpu::BakeFullMultiScatteringLut(Profile, TransLut, TW, TH, MSR, 1.0f, MsLut);
			BottomKm = Profile.BottomRadiusKm;
			TopKm = Profile.TopRadiusKm;
			EnvelopeKm = TopKm - BottomKm;
			KmPerSlice = HillaireLimits::AerialKmPerSliceForEnvelope(EnvelopeKm);
			return TransLut.Num() > 0 && MsLut.Num() > 0;
		}
	};

	FMatrix CalibDiagInvProj(float Diag)
	{
		FMatrix M(EForceInit::ForceInitToZero);
		M.M[0][0] = Diag; M.M[1][1] = Diag; M.M[2][2] = Diag; M.M[3][3] = 1.0;
		return M;
	}

	// Row-vector map with a hand-built orthonormal-row rotation (rows R0,R1,D).
	FVector3f CalibMapDir(const FMatrix& Rot, const FVector& V)
	{
		return FVector3f(
			(float)(V.X * Rot.M[0][0] + V.Y * Rot.M[1][0] + V.Z * Rot.M[2][0]),
			(float)(V.X * Rot.M[0][1] + V.Y * Rot.M[1][1] + V.Z * Rot.M[2][1]),
			(float)(V.X * Rot.M[0][2] + V.Y * Rot.M[1][2] + V.Z * Rot.M[2][2]));
	}

	FMatrix CalibCenterRayRot(const FVector3f& D)
	{
		FVector3f R0 = FVector3f::CrossProduct(FVector3f(0.0f, 1.0f, 0.0f), D).GetSafeNormal();
		if (R0.SizeSquared() < 0.5f)
		{
			R0 = FVector3f(1.0f, 0.0f, 0.0f);
		}
		const FVector3f R1 = FVector3f::CrossProduct(D, R0).GetSafeNormal();
		FMatrix Rot(EForceInit::ForceInitToZero);
		Rot.M[0][0] = R0.X; Rot.M[0][1] = R0.Y; Rot.M[0][2] = R0.Z;
		Rot.M[1][0] = R1.X; Rot.M[1][1] = R1.Y; Rot.M[1][2] = R1.Z;
		Rot.M[2][0] = D.X;  Rot.M[2][1] = D.Y;  Rot.M[2][2] = D.Z;
		Rot.M[3][3] = 1.0f;
		return Rot;
	}

	float CalibLuminance3(const FVector3f& V) { return 0.2126f * V.X + 0.7152f * V.Y + 0.0722f * V.Z; }

	bool CalibAllFinite(const FLinearColor& C)
	{
		return FMath::IsFinite(C.R) && FMath::IsFinite(C.G) && FMath::IsFinite(C.B) && FMath::IsFinite(C.A);
	}
}

// ---------------------------------------------------------------------------
// D1 - Fog regimes: froxel-column march + composite veil per path length.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireCalibrationFogRegimesTest,
	"Hillaire.Calibration.FogRegimes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireCalibrationFogRegimesTest::RunTest(const FString& Parameters)
{
	FCalibPlanet P;
	if (!TestTrue(TEXT("Calib planet + T/MS baked"), P.Bake()))
	{
		return false;
	}
	const FMatrix InvProj = CalibDiagInvProj(100.0f);
	const FVector3f SunDay(0.0f, 0.0f, 1.0f);
	const FVector3f SunSet(1.0f, 0.0f, 0.0f);
	const FVector3f SunWhite30(30.0f, 30.0f, 30.0f);
	const float Albedo = 0.3f;

	struct FRegime { const TCHAR* Name; FVector3f CamLocal; FVector3f RayDir; float Alt01; float SunElev; bool bStrictMono; };
	const FRegime Regimes[] = {
		{ TEXT("surface-nadir"), FVector3f(0, 0, P.BottomKm + 0.002f), FVector3f(0, 0, -1), 0.0f, 1.0f, true },
		{ TEXT("medium-depr2"), FVector3f(0, 0, P.BottomKm + 0.002f), FVector3f(0.99939f, 0, -0.03490f), 0.0f, 1.0f, false },
		{ TEXT("far-depr02"), FVector3f(0, 0, P.BottomKm + 0.002f), FVector3f(0.99999f, 0, -0.00349f), 0.0f, 1.0f, false },
		{ TEXT("entry-nadir"), FVector3f(0, 0, P.TopKm - 0.02f), FVector3f(0, 0, -1), 0.98f, 1.0f, true },
		// Vista regimes from near the top (guaranteed ground hits at km
		// scale: depression 30/45 deg from 0.05 km below the top gives
		// ~1.3-1.9 km paths, w > 1, exercising the beyond-range ease on
		// real clamped marches).
		{ TEXT("vista-depr30"), FVector3f(0, 0, P.TopKm - 0.05f), FVector3f(0.86603f, 0, -0.5f), 0.95f, 1.0f, false },
		{ TEXT("vista-depr45"), FVector3f(0, 0, P.TopKm - 0.05f), FVector3f(0.70711f, 0, -0.70711f), 0.95f, 1.0f, false },
		// Low far-ish dense regime: 0.15 km eye, 18 deg depression always
		// clears the ~14 deg horizon dip (off-center fan tilt included),
		// giving ~0.4-0.6 km paths through low dense air.
		{ TEXT("vista-depr18"), FVector3f(0, 0, P.BottomKm + 0.15f), FVector3f(0.95106f, 0, -0.30902f), 0.15f, 1.0f, false },
	};
	for (const FRegime& R : Regimes)
	{
		for (int32 SunCase = 0; SunCase < 2; ++SunCase)
		{
			const FVector3f Sun = (SunCase == 0) ? SunDay : SunSet;
			const float SunElev = (SunCase == 0) ? 1.0f : (Sun | FVector3f(0, 0, 1));
			const FMatrix Rot = CalibCenterRayRot(R.RayDir);
			TArray<FIntVector> Coords;
			for (int32 Z = 0; Z < 32; ++Z)
			{
				Coords.Add(FIntVector(16, 16, Z));
			}
			TArray<FLinearColor> Froxels;
			HillaireLutCpu::BakeAerialFroxels(P.Profile, P.TransLut, P.TW, P.TH, P.MsLut, P.MSR,
				R.CamLocal, InvProj, Rot, Sun, 32, 32, 32, Coords, Froxels);
			if (!TestEqual(TEXT("Column baked"), Froxels.Num(), 32))
			{
				return false;
			}
			// Ray for the column (bake pixel 16,16 of 32 with the diag-100 fan).
			const FVector ClipV(0.03125f, -0.03125f, 0.5f);
			const FVector ViewD(ClipV.X * 100.0, ClipV.Y * 100.0, ClipV.Z * 100.0);
			const FVector3f RayDir = CalibMapDir(Rot, ViewD.GetSafeNormal());
			float GroundT = 1e9f;
			HillaireLutCpu::RaySphereNearest(R.CamLocal, RayDir, P.BottomKm, GroundT);
			AddInfo(FString::Printf(TEXT("--- %s sun=%s ray=(%.4f,%.4f,%.4f) groundT=%.4fkm ---"),
				R.Name, SunCase == 0 ? TEXT("day") : TEXT("sunset"), RayDir.X, RayDir.Y, RayDir.Z, GroundT));
			float PrevA = -1.0f;
			bool bMonoA = true;
			// Production-like composite veil for an albedo proxy (white sun
			// x30, PreExposure 1, distance x altitude x boundary x sunset
			// presentation; the boundary fade is exactly 1.0 for every
			// regime here, alt <= 0.98).
			const FVector3f SunChroma(1.0f, 1.0f, 1.0f);
			const FVector3f SunsetMult = HillaireLimits::SunsetAerialMultiplier(SunElev, SunChroma);
			for (int32 Z = 0; Z < 32; Z += 1)
			{
				const FLinearColor& F = Froxels[Z];
				const float SliceBake = ((float)Z + 0.5f) / 32.0f;
				const float SliceQ = SliceBake * SliceBake * 32.0f;
				const float TMaxBake = SliceQ * P.KmPerSlice;
				// NOTE: TPath uses the original-ray ground hit, but clamped
				// slices re-aim at their own (farther) ground point and the
				// integrator then truncates at first ground contact: deep
				// slices on tilted rays are SHORT corner-cutting marches, so
				// column opacity is NOT monotonic there. That is reference
				// clamp behavior (frozen bake math); the composite handles it
				// via the beyond-range presentation ease. Strict monotonicity
				// is therefore asserted only on unclamped columns.
				const float TPath = FMath::Min(TMaxBake, GroundT);
				const float Slice = TPath / P.KmPerSlice;
				const float W = FMath::Sqrt(FMath::Max(Slice, 0.5f) / 32.0f);
				bMonoA &= (F.A >= PrevA - 1e-6f);
				PrevA = F.A;
				const float DistScale = HillaireLimits::AerialDistanceScale(W);
				const float AltScale = HillaireLimits::AerialAltitudeScale(R.Alt01);
				const float OutR = Albedo * (1.0f - F.A)
					+ SunWhite30.X * F.R * DistScale * AltScale * SunsetMult.X;
				const float OutG = Albedo * (1.0f - F.A)
					+ SunWhite30.Y * F.G * DistScale * AltScale * SunsetMult.Y;
				const float OutB = Albedo * (1.0f - F.A)
					+ SunWhite30.Z * F.B * DistScale * AltScale * SunsetMult.Z;
				const float OutLum = 0.2126f * OutR + 0.7152f * OutG + 0.0722f * OutB;
				if ((Z % 4) == 0 || Z == 31)
				{
					AddInfo(FString::Printf(
						TEXT("  z=%2d tPath=%7.4fkm A=%.4f 1-A=%.4f L=(%.5f,%.5f,%.5f) w=%.3f dS=%.3f aS=%.3f out/alb=%.3f"),
						Z, TPath, F.A, 1.0f - F.A, F.R, F.G, F.B, W, DistScale, AltScale,
						OutLum / Albedo));
				}
				if (!TestTrue(TEXT("Froxel finite"), CalibAllFinite(F)))
				{
					return false;
				}
			}
			if (R.bStrictMono)
			{
				TestTrue(TEXT("Opacity monotonic along unclamped column"), bMonoA);
			}
			else
			{
				AddInfo(FString::Printf(TEXT("Tilted column monotonic=%d (reference clamp corner-cut, presentation-owned)"),
					bMonoA ? 1 : 0));
			}
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// D2 - Interpolation error: volume-trilinear vs converged direct march.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireCalibrationInterpErrorTest,
	"Hillaire.Calibration.InterpError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireCalibrationInterpErrorTest::RunTest(const FString& Parameters)
{
	FCalibPlanet P;
	if (!TestTrue(TEXT("Calib planet + T/MS baked"), P.Bake()))
	{
		return false;
	}
	const FMatrix InvProj = CalibDiagInvProj(100.0f);
	const FMatrix Rot = FMatrix::Identity;
	const FVector3f CamLocal(0.0f, 0.0f, P.BottomKm + 0.05f);
	const FVector3f SunDay(0.0f, 0.0f, 1.0f);
	const FVector3f SunSet(1.0f, 0.0f, 0.0f);

	for (int32 SunCase = 0; SunCase < 2; ++SunCase)
	{
		const FVector3f Sun = (SunCase == 0) ? SunDay : SunSet;
		TArray<FLinearColor> Volume;
		HillaireLutCpu::BakeFullAerialVolume(P.Profile, P.TransLut, P.TW, P.TH, P.MsLut, P.MSR,
			CamLocal, InvProj, Rot, Sun, Volume);
		if (!TestEqual(TEXT("Full volume baked"), Volume.Num(), 32 * 32 * 32))
		{
			return false;
		}
		const float FanU[] = { 0.3f, 0.4f, 0.5f, 0.6f, 0.7f };
		const float FanW[] = { 0.2f, 0.4f, 0.7f, 1.0f };
		double SumTot = 0.0, SumQuad = 0.0;
		double MaxTot = 0.0, MaxQuad = 0.0;
		float MaxU = 0, MaxV = 0, MaxW = 0;
		int32 N = 0;
		bool bFin = true;
		for (float U : FanU)
		{
			for (float V : FanU)
			{
				// Bake-fan ray for (U,V) with the diag-100 test projection.
				const FVector ClipV(U * 2.0f - 1.0f, 1.0f - V * 2.0f, 0.5f);
				const FVector Vd(ClipV.X * 100.0, ClipV.Y * 100.0, ClipV.Z * 100.0);
				const FVector3f RayDir = FVector3f(Vd.GetSafeNormal());
				for (float W : FanW)
				{
					const float Slice = W * W * 32.0f;
					const float TDepth = Slice * P.KmPerSlice;
					const FVector3f Vol = HillaireLutCpu::SampleVolumeTrilinear(Volume, 32, 32, 32, U, V, W);
					// A needs the same trilinear weights: fetch alpha channel.
					float AccA = 0.0f;
					{
						const float FX = FMath::Clamp(U, 0.0f, 1.0f) * 31.0f;
						const float FY = FMath::Clamp(V, 0.0f, 1.0f) * 31.0f;
						const float FZ = FMath::Clamp(W, 0.0f, 1.0f) * 31.0f;
						const int32 X0 = FMath::FloorToInt(FX), Y0 = FMath::FloorToInt(FY), Z0 = FMath::FloorToInt(FZ);
						const float TX = FX - X0, TY = FY - Y0, TZ = FZ - Z0;
						auto FetchA = [&](int32 X, int32 Y, int32 Z) -> float
						{
							X = FMath::Clamp(X, 0, 31); Y = FMath::Clamp(Y, 0, 31); Z = FMath::Clamp(Z, 0, 31);
							return Volume[(Z * 32 + Y) * 32 + X].A;
						};
						const float A00 = FetchA(X0, Y0, Z0) * (1 - TX) + FetchA(X0 + 1, Y0, Z0) * TX;
						const float A10 = FetchA(X0, Y0 + 1, Z0) * (1 - TX) + FetchA(X0 + 1, Y0 + 1, Z0) * TX;
						const float A01 = FetchA(X0, Y0, Z0 + 1) * (1 - TX) + FetchA(X0 + 1, Y0, Z0 + 1) * TX;
						const float A11 = FetchA(X0, Y0 + 1, Z0 + 1) * (1 - TX) + FetchA(X0 + 1, Y0 + 1, Z0 + 1) * TX;
						AccA = (A00 * (1 - TY) + A10 * TY) * (1 - TZ) + (A01 * (1 - TY) + A11 * TY) * TZ;
					}
					// Converged reference: 256 fixed steps, MS approx ON.
					const HillaireLutCpu::FSingleScatteringResult Ref =
						HillaireLutCpu::IntegrateScatteredLuminance(P.Profile, P.TransLut, P.TW, P.TH,
							CamLocal, RayDir, Sun, false, 256, false, 4.0f, 14.0f,
							true, P.Profile.MiePhaseG, true, &P.MsLut, P.MSR, TDepth);
					// Bake-matched quadrature: same step count as the nearest slice.
					const int32 SliceId = FMath::Clamp(FMath::RoundToInt(W * 32.0f - 0.5f), 0, 31);
					const int32 BakeSteps = FMath::Max(1, (SliceId + 1) * 2);
					const HillaireLutCpu::FSingleScatteringResult Quad =
						HillaireLutCpu::IntegrateScatteredLuminance(P.Profile, P.TransLut, P.TW, P.TH,
							CamLocal, RayDir, Sun, false, BakeSteps, false, 4.0f, 14.0f,
							true, P.Profile.MiePhaseG, true, &P.MsLut, P.MSR, TDepth);
					const float RefL = CalibLuminance3(Ref.L) + 1e-6f;
					const float TotErr = (FMath::Abs(Vol.X - Ref.L.X) + FMath::Abs(Vol.Y - Ref.L.Y) + FMath::Abs(Vol.Z - Ref.L.Z)) / (3.0f * RefL)
						+ FMath::Abs(AccA - (1.0f - (Ref.Transmittance.X + Ref.Transmittance.Y + Ref.Transmittance.Z) / 3.0f));
					const float QuadErr = (FMath::Abs(Quad.L.X - Ref.L.X) + FMath::Abs(Quad.L.Y - Ref.L.Y) + FMath::Abs(Quad.L.Z - Ref.L.Z)) / (3.0f * RefL);
					SumTot += TotErr; SumQuad += QuadErr;
					if (TotErr > MaxTot) { MaxTot = TotErr; MaxU = U; MaxV = V; MaxW = W; }
					MaxQuad = FMath::Max(MaxQuad, QuadErr);
					bFin &= FMath::IsFinite(TotErr) && FMath::IsFinite(QuadErr);
					++N;
				}
			}
		}
		AddInfo(FString::Printf(TEXT("sun=%s N=%d meanTot=%.4f maxTot=%.4f at (U=%.2f,V=%.2f,w=%.2f) meanQuad=%.4f maxQuad=%.4f interp~%.4f"),
			SunCase == 0 ? TEXT("day") : TEXT("sunset"), N, SumTot / N, MaxTot, MaxU, MaxV, MaxW,
			SumQuad / N, MaxQuad, (SumTot - SumQuad) / N));
		TestTrue(TEXT("Error finite"), bFin);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Sky-ambient light mapping: transfer -> skylight state (pure, symmetric).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireCalibrationSkyAmbientLightTest,
	"Hillaire.Calibration.SkyAmbientLight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireCalibrationSkyAmbientLightTest::RunTest(const FString& Parameters)
{
	const FVector3f WhiteSun(1.0f, 1.0f, 1.0f);
	// Day: hand-verified anchor (transfer (0.01,0.02,0.04), scale 2.5).
	{
		const HillaireLimits::FSkyAmbientLightState S =
			HillaireLimits::SkyAmbientLightState(
				FVector3f(0.01f, 0.02f, 0.04f), 1.0f, WhiteSun,
				HillaireLimits::SkyAmbientPresentationScale);
		TestTrue(TEXT("Day intensity anchor"),
			FMath::Abs(S.Intensity - 0.048295f) < 1e-4f);
		TestTrue(TEXT("Day color anchor (blue skylight)"),
			FMath::Abs(S.Color.X - 0.51765f) < 1e-3f
			&& FMath::Abs(S.Color.Y - 1.03529f) < 1e-3f
			&& FMath::Abs(S.Color.Z - 2.07059f) < 1e-3f);
		TestTrue(TEXT("Day ambient cooler than neutral (B>R)"), S.Color.Z > S.Color.X);
	}
	// Night: terminator gate is exactly 0 below -0.20 -> zero intensity,
	// no residual of any color.
	{
		const HillaireLimits::FSkyAmbientLightState S =
			HillaireLimits::SkyAmbientLightState(
				FVector3f(0.03f, 0.015f, 0.008f), -0.5f, WhiteSun,
				HillaireLimits::SkyAmbientPresentationScale);
		TestTrue(TEXT("Night intensity exactly zero"), S.Intensity == 0.0f);
	}
	// Warm sunset: intensity lands in the faint-readable band and the color
	// is warm (R>B), tracking the sky instead of the static cold fill.
	{
		const HillaireLimits::FSkyAmbientLightState S =
			HillaireLimits::SkyAmbientLightState(
				FVector3f(0.03f, 0.015f, 0.008f), -0.02f, WhiteSun,
				HillaireLimits::SkyAmbientPresentationScale);
		TestTrue(TEXT("Sunset ambient faint but nonzero"),
			S.Intensity > 0.005f && S.Intensity < 0.15f);
		TestTrue(TEXT("Sunset ambient warm (R>B)"), S.Color.X > S.Color.Z);
		AddInfo(FString::Printf(TEXT("Sunset ambient I=%.6f color=(%.3f,%.3f,%.3f)"),
			S.Intensity, S.Color.X, S.Color.Y, S.Color.Z));
	}
	// Elevation shaping comes ONLY from the shared terminator gate (no sign
	// branch, no sunset-only hack): mirrored elevations with identical
	// transfer differ exactly by the gate ratio, with identical chromaticity.
	{
		const HillaireLimits::FSkyAmbientLightState Plus =
			HillaireLimits::SkyAmbientLightState(
				FVector3f(0.02f, 0.015f, 0.01f), 0.05f, WhiteSun,
				HillaireLimits::SkyAmbientPresentationScale);
		const HillaireLimits::FSkyAmbientLightState Minus =
			HillaireLimits::SkyAmbientLightState(
				FVector3f(0.02f, 0.015f, 0.01f), -0.05f, WhiteSun,
				HillaireLimits::SkyAmbientPresentationScale);
		const float GateRatio =
			HillaireLimits::TerminatorFactor(-0.05f) / HillaireLimits::TerminatorFactor(0.05f);
		TestTrue(TEXT("Only the shared gate shapes elevation"),
			FMath::Abs(Minus.Intensity / Plus.Intensity - GateRatio) < 1e-4f);
		TestTrue(TEXT("Chromaticity gate-independent"),
			FMath::Abs(Plus.Color.X - Minus.Color.X) < 1e-4f
			&& FMath::Abs(Plus.Color.Y - Minus.Color.Y) < 1e-4f
			&& FMath::Abs(Plus.Color.Z - Minus.Color.Z) < 1e-4f);
	}
	// Scale linearity: doubling the presentation scale doubles intensity,
	// color untouched.
	{
		const HillaireLimits::FSkyAmbientLightState A =
			HillaireLimits::SkyAmbientLightState(
				FVector3f(0.01f, 0.02f, 0.04f), 1.0f, WhiteSun, 2.5f);
		const HillaireLimits::FSkyAmbientLightState B =
			HillaireLimits::SkyAmbientLightState(
				FVector3f(0.01f, 0.02f, 0.04f), 1.0f, WhiteSun, 5.0f);
		TestTrue(TEXT("Scale linear in intensity, color stable"),
			FMath::Abs(B.Intensity - 2.0f * A.Intensity) < 1e-6f
			&& B.Color.X == A.Color.X && B.Color.Y == A.Color.Y && B.Color.Z == A.Color.Z);
	}
	// Red star shifts the ambient (planet variation preserved).
	{
		const HillaireLimits::FSkyAmbientLightState Ref =
			HillaireLimits::SkyAmbientLightState(
				FVector3f(0.02f, 0.015f, 0.01f), 0.0f, WhiteSun,
				HillaireLimits::SkyAmbientPresentationScale);
		const HillaireLimits::FSkyAmbientLightState Red =
			HillaireLimits::SkyAmbientLightState(
				FVector3f(0.02f, 0.015f, 0.01f), 0.0f, FVector3f(1.0f, 0.35f, 0.15f),
				HillaireLimits::SkyAmbientPresentationScale);
		TestTrue(TEXT("Red star shifts ambient"),
			FMath::Abs(Red.Color.X - Ref.Color.X) > 0.05f && Red.Intensity == Ref.Intensity);
	}
	return true;
}

// ---------------------------------------------------------------------------
// D3 - Sky ambient marcher vs hemisphere integral of the baked SkyView LUT.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHillaireCalibrationSkyAmbientTest,
	"Hillaire.Calibration.SkyAmbient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHillaireCalibrationSkyAmbientTest::RunTest(const FString& Parameters)
{
	FCalibPlanet P;
	if (!TestTrue(TEXT("Calib planet + T/MS baked"), P.Bake()))
	{
		return false;
	}
	const int32 SVW = HillaireLimits::SkyViewWidth;
	const int32 SVH = HillaireLimits::SkyViewHeight;
	const FVector3f Up(0.0f, 0.0f, 1.0f);
	const float SurfH = P.BottomKm + HillaireLimits::PlanetRadiusOffsetKm;
	// Same 17-dir quadrature as the marcher (zenith + 8@40 + 8@70, ring0 sun).
	auto LutHemisphere = [&](const TArray<FLinearColor>& SkyLut, const FVector3f& Sun) -> FVector3f
	{
		FVector3f SunHoriz = Sun - Up * (Sun | Up);
		FVector3f T0 = SunHoriz.SizeSquared() > 1e-12f ? SunHoriz.GetSafeNormal()
			: FVector3f::CrossProduct(Up, FVector3f(0, 0, 1)).GetSafeNormal();
		const FVector3f T1 = FVector3f::CrossProduct(Up, T0).GetSafeNormal();
		FVector3f E = FVector3f::ZeroVector;
		auto Accum = [&](const FVector3f& Dir, float CosZen, float W)
		{
			const float VZC = FMath::Clamp(Dir | Up, -1.0f, 1.0f);
			const FVector3f VH = Dir - Up * VZC;
			const FVector3f SH = Sun - Up * (Sun | Up);
			float LVC = 0.0f;
			if (VH.SizeSquared() > 1e-12f && SH.SizeSquared() > 1e-12f)
			{
				LVC = FMath::Clamp(VH.GetSafeNormal() | SH.GetSafeNormal(), -1.0f, 1.0f);
			}
			float Uu = 0.0f, Vv = 0.0f;
			HillaireLutCpu::SkyViewLutParamsToUv(P.BottomKm, false, VZC, LVC, SurfH, Uu, Vv);
			E += HillaireLutCpu::SampleLutBilinear(SkyLut, SVW, SVH, Uu, Vv) * (CosZen * W);
		};
		Accum(Up, 1.0f, 2.0f * PI * (1.0f - FMath::Cos(25.0f * PI / 180.0f)));
		const float Z2[2] = { 40.0f * PI / 180.0f, 70.0f * PI / 180.0f };
		const float W2[2] = {
			2.0f * PI * (FMath::Cos(25.0f * PI / 180.0f) - FMath::Cos(55.0f * PI / 180.0f)) / 8.0f,
			2.0f * PI * (FMath::Cos(55.0f * PI / 180.0f) - FMath::Cos(80.0f * PI / 180.0f)) / 8.0f };
		for (int32 R = 0; R < 2; ++R)
		{
			const float CZ = FMath::Cos(Z2[R]);
			const float SZ = FMath::Sqrt(FMath::Max(0.0f, 1.0f - CZ * CZ));
			for (int32 J = 0; J < 8; ++J)
			{
				const float Az = (float)J * PI / 4.0f;
				Accum(Up * CZ + (T0 * FMath::Cos(Az) + T1 * FMath::Sin(Az)) * SZ, CZ, W2[R]);
			}
		}
		return E;
	};

	const float Elevs[] = { 1.0f, 0.1f, 0.0f, -0.1f, -0.5f, -1.0f };
	for (float E : Elevs)
	{
		const FVector3f Sun(FMath::Sqrt(FMath::Max(0.0f, 1.0f - E * E)), 0.0f, E);
		TArray<FLinearColor> SkyLut;
		HillaireLutCpu::BakeFullSkyViewLut(P.Profile, P.TransLut, P.TW, P.TH, P.MsLut, P.MSR,
			SurfH, Sun, Up, SkyLut);
		const FVector3f Marcher = HillaireLutCpu::ComputeSkyAmbientTransfer(P.Profile, Sun, Up, SurfH);
		const FVector3f LutInt = LutHemisphere(SkyLut, Sun);
		const float ML = CalibLuminance3(Marcher);
		const float LL = CalibLuminance3(LutInt);
		const float Ratio = (LL > 1e-9f) ? (ML / LL) : (ML < 1e-9f ? 1.0f : 0.0f);
		AddInfo(FString::Printf(TEXT("elev=%+.2f marcherL=%.6f lutL=%.6f ratio=%.3f marcherRB=%.3f lutRB=%.3f"),
			E, ML, LL, Ratio,
			Marcher.Z > 1e-12f ? Marcher.X / Marcher.Z : -1.0f,
			LutInt.Z > 1e-12f ? LutInt.X / LutInt.Z : -1.0f));
		TestTrue(TEXT("Marcher finite"), FMath::IsFinite(ML));
		if (E <= -0.99f)
		{
			// Sun at nadir: every sun leg terminates in the planet.
			TestTrue(TEXT("Deep-night ambient exactly zero"), ML == 0.0f);
		}
		else if (E < -0.2f)
		{
			// Small-planet twilight persists geometrically; the caller gates
			// by TerminatorFactor (0 here), so only ordering is asserted.
			TestTrue(TEXT("Twilight ambient below sunset level"), ML < 0.5f);
		}
		else
		{
			TestTrue(TEXT("Ambient positive when sunlit"), ML > 0.0f);
			TestTrue(TEXT("Single-scatter bounded by full integral"), Ratio > 0.2f && Ratio <= 1.05f);
		}
	}
	return true;
}
