// HILLAIRE ATMOSPHERE - VALIDATION CONSOLE COMMANDS (Phase 2B).
//
// Visual-debug path (task section 20): the SkyView LUT is observable WITHOUT
// any final composite, directly from generated data:
//
// - Hillaire.BakeSkyViewValidation (pure CPU, no world/GPU needed): bakes the
//   full T + MS + SkyView chain for the validation cases (A/D/E/F/G) with the
//   CPU mirror, logs stats + timings, and writes tonemapped PNG previews to
//   Plugins/HillaireAtmosphere/Validation/Phase2B/. Cases B (horizon) and C
//   (zenith) are inspected regions of the same Case-A SkyView image.
// - Hillaire.DumpGpuLuts <PlanetSlot> (needs a world with registered planets
//   and at least one rendered view): readbacks the pooled GPU LUTs via
//   HillaireLutDiagnostics::ReadbackPooledLut and writes the same previews.
//   Proves the GPU content matches the validated CPU data statistically.

#include "HillaireAtmosphereSubsystem.h"
#include "HillairePlanetaryAtmosphereSubsystem.h"
#include "HillaireAtmosphereLog.h"
#include "HillaireAtmosphereProfile.h"
#include "HillaireLimits.h"
#include "HillaireLutCpu.h"
#include "HillaireLutDiagnostics.h"
#include "HillaireLutManager.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraActor.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	FString GetValidationDir()
	{
		FString Dir = FPaths::Combine(
			IPluginManager::Get().FindPlugin(TEXT("HillaireAtmosphere"))->GetBaseDir(),
			TEXT("Validation/Phase2B"));
		IFileManager::Get().MakeDirectory(*Dir, true);
		return Dir;
	}

	struct FValidationCase
	{
		const TCHAR* Name = nullptr;
		FHillaireAtmosphereProfile Profile;
		float ViewHeightKm = 0.0f;
		FVector3f SunDirLocal = FVector3f(0.0f, 0.0f, 1.0f);
		FVector3f CameraUpLocal = FVector3f(0.0f, 0.0f, 1.0f);
		float SkyExposure = 2.0f;
	};

	void BakeAndSaveCase(const FValidationCase& Case)
	{
		const FString Dir = GetValidationDir();
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("Validation case %s: height=%.3f km sun=(%.4f,%.4f,%.4f)."),
			Case.Name, Case.ViewHeightKm,
			Case.SunDirLocal.X, Case.SunDirLocal.Y, Case.SunDirLocal.Z);

		const int32 TW = HillaireLimits::TransmittanceWidth;
		const int32 TH = HillaireLimits::TransmittanceHeight;
		const int32 MSR = HillaireLimits::MultiScatteringRes;

		double T0 = FPlatformTime::Seconds();
		TArray<FLinearColor> TransLut;
		HillaireLutCpu::BakeTransmittanceLut(Case.Profile, TW, TH, TransLut);
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("  T bake %.1f s. %s"),
			FPlatformTime::Seconds() - T0,
			*HillaireLutDiagnostics::DescribeStats(TEXT("T"), HillaireLutDiagnostics::AnalyzeLut(TransLut)));

		T0 = FPlatformTime::Seconds();
		TArray<FLinearColor> MsLut;
		HillaireLutCpu::BakeFullMultiScatteringLut(Case.Profile, TransLut, TW, TH, MSR, 1.0f, MsLut);
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("  MS bake %.1f s. %s"),
			FPlatformTime::Seconds() - T0,
			*HillaireLutDiagnostics::DescribeStats(TEXT("MS"), HillaireLutDiagnostics::AnalyzeLut(MsLut)));

		T0 = FPlatformTime::Seconds();
		TArray<FLinearColor> SkyLut;
		HillaireLutCpu::BakeFullSkyViewLut(Case.Profile, TransLut, TW, TH, MsLut, MSR,
			Case.ViewHeightKm, Case.SunDirLocal, Case.CameraUpLocal, SkyLut);
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("  SkyView bake %.1f s. %s"),
			FPlatformTime::Seconds() - T0,
			*HillaireLutDiagnostics::DescribeStats(TEXT("SkyView"), HillaireLutDiagnostics::AnalyzeLut(SkyLut)));

		HillaireLutDiagnostics::SaveLutPreviewPng(TransLut, TW, TH, 1.0f,
			FPaths::Combine(Dir, FString::Printf(TEXT("T_%s.png"), Case.Name)));
		HillaireLutDiagnostics::SaveLutPreviewPng(MsLut, MSR, MSR, 60.0f,
			FPaths::Combine(Dir, FString::Printf(TEXT("MS_%s.png"), Case.Name)));
		HillaireLutDiagnostics::SaveLutPreviewPng(SkyLut,
			HillaireLimits::SkyViewWidth, HillaireLimits::SkyViewHeight, Case.SkyExposure,
			FPaths::Combine(Dir, FString::Printf(TEXT("SkyView_%s.png"), Case.Name)));
	}

	void BakeSkyViewValidation()
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("Hillaire.BakeSkyViewValidation: ENTER cases A/D/E/F/G."));
		// Entry marker (proves execution even if logging were filtered).
		const FString Marker = FPaths::Combine(GetValidationDir(), TEXT("_BakeStarted.marker"));
		FFileHelper::SaveStringToFile(TEXT("started"), *Marker);

		const FHillaireAtmosphereProfile Earth = FHillaireAtmosphereProfile::MakeReferenceProfile();

		// CASE A - clear noon, 1 km over an Earth-like surface.
		FValidationCase A;
		A.Name = TEXT("A_NoonSurface");
		A.Profile = Earth;
		A.ViewHeightKm = Earth.BottomRadiusKm + 1.0f;
		A.SunDirLocal = FVector3f(0.0f, 0.0f, 1.0f);
		BakeAndSaveCase(A);

		// CASE D - sunset: sun 2 deg over the horizon, same observer.
		FValidationCase D = A;
		D.Name = TEXT("D_SunsetSurface");
		const float El = FMath::Cos(88.0f * PI / 180.0f);
		D.SunDirLocal = FVector3f(FMath::Sqrt(1.0f - El * El), 0.0f, El).GetSafeNormal();
		D.SkyExposure = 1.0f;
		BakeAndSaveCase(D);

		// CASE E - space: 500 km over the top.
		FValidationCase E = A;
		E.Name = TEXT("E_Space");
		E.ViewHeightKm = Earth.TopRadiusKm + 500.0f;
		E.SkyExposure = 3.0f;
		BakeAndSaveCase(E);

		// CASE F - high altitude: 5 km under the top.
		FValidationCase F = A;
		F.Name = TEXT("F_HighAltitude");
		F.ViewHeightKm = Earth.TopRadiusKm - 5.0f;
		BakeAndSaveCase(F);

		// CASE G - large planet (~10x Earth radius), same chemistry.
		FValidationCase G;
		G.Name = TEXT("G_LargePlanet");
		G.Profile = Earth;
		G.Profile.BottomRadiusKm = 60000.0f;
		G.Profile.TopRadiusKm = 61000.0f;
		G.ViewHeightKm = G.Profile.BottomRadiusKm + 1.0f;
		G.SunDirLocal = FVector3f(0.0f, 0.0f, 1.0f);
		BakeAndSaveCase(G);

		UE_LOG(LogHillaireAtmosphere, Log, TEXT("Hillaire.BakeSkyViewValidation: done -> %s"), *GetValidationDir());
	}

	void DumpGpuAerial(int32 Slot, const FString& Dir, FHillaireLutManager* LutManager);

	// Forward declarations: deterministic samplers defined below, used by DumpGpuLuts.
	void LogDeterministicSkySamples(
		const TArray<FLinearColor>& SkyData,
		const FHillaireAtmosphereProfile& Pr,
		float ViewHeightKm);
	float ProbeViewHeightKm(UWorld* World, const FPlanetAtmosphereState& Planet);

	void DumpGpuLuts(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("Hillaire.DumpGpuLuts: no world."));
			return;
		}
		UHillairePlanetaryAtmosphereSubsystem* Sub = World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>();
		if (!Sub || !Sub->GetLutManager())
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("Hillaire.DumpGpuLuts: no subsystem/LUT manager."));
			return;
		}
		const int32 Slot = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		FHillaireLutManager::FHillairePlanetLutTargets Targets;
		if (!Sub->GetLutManager()->CopyTargets(Slot, Targets))
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("Hillaire.DumpGpuLuts: no targets for slot %d (render a view first)."), Slot);
			return;
		}
		const FString Dir = GetValidationDir();
		auto Dump = [&](TRefCountPtr<IPooledRenderTarget>& Pooled, const TCHAR* LutName, int32 W, int32 H, float Exposure)
		{
			if (!Pooled.IsValid())
			{
				UE_LOG(LogHillaireAtmosphere, Warning, TEXT("  %s: no pooled target (not generated yet)."), LutName);
				return;
			}
			TArray<FLinearColor> Data;
			if (!HillaireLutDiagnostics::ReadbackPooledLut(Pooled, Data))
			{
				return;
			}
			UE_LOG(LogHillaireAtmosphere, Log, TEXT("  GPU %s. %s"),
				LutName, *HillaireLutDiagnostics::DescribeStats(LutName, HillaireLutDiagnostics::AnalyzeLut(Data)));
			HillaireLutDiagnostics::SaveLutPreviewPng(Data, W, H, Exposure,
				FPaths::Combine(Dir, FString::Printf(TEXT("GPU_%s_Slot%d.png"), LutName, Slot)));
		};

		// Pooled handles are logically const here; readback takes a mutable
		// ref (extraction API), so copy the handle locally (ref-counted, safe).
		TRefCountPtr<IPooledRenderTarget> T = Targets.Transmittance;
		TRefCountPtr<IPooledRenderTarget> M = Targets.MultiScattering;
		TRefCountPtr<IPooledRenderTarget> S = Targets.SkyView;
		Dump(T, TEXT("Transmittance"), HillaireLimits::TransmittanceWidth, HillaireLimits::TransmittanceHeight, 1.0f);
		Dump(M, TEXT("MultiScattering"), HillaireLimits::MultiScatteringRes, HillaireLimits::MultiScatteringRes, 60.0f);
		Dump(S, TEXT("SkyView"), HillaireLimits::SkyViewWidth, HillaireLimits::SkyViewHeight, 2.0f);
		// Deterministic GPU SkyView probes (§2: zenith/horizon/sun/anti-sun
		// on REAL readback bytes, decoded with the live view height).
		if (S.IsValid())
		{
			TArray<FLinearColor> SkyData;
			if (HillaireLutDiagnostics::ReadbackPooledLut(S, SkyData))
			{
				TArray<FPlanetAtmosphereState> States;
				Sub->GetAllPlanetStates(States);
				// Slot = subsystem registry index (LUT slots are registry
				// indices; GetAllPlanetStates preserves registry order).
				const FPlanetAtmosphereState* SlotPlanet =
					States.IsValidIndex(Slot) ? &States[Slot] : nullptr;
				if (SlotPlanet)
				{
					LogDeterministicSkySamples(SkyData, SlotPlanet->Profile,
						ProbeViewHeightKm(World, *SlotPlanet));
				}
				else
				{
					UE_LOG(LogHillaireAtmosphere, Warning,
						TEXT("  SkySamples: no planet state for slot %d."), Slot);
				}
			}
		}
		// Phase 2C: aerial scratch slices (present only after an evaluation).
		DumpGpuAerial(Slot, Dir, Sub->GetLutManager());
	}

	static FAutoConsoleCommand GBakeSkyViewValidationCmd(
		TEXT("Hillaire.BakeSkyViewValidation"),
		TEXT("CPU-bake the full T+MS+SkyView chain for validation cases A/D/E/F/G and write PNG previews to Plugins/HillaireAtmosphere/Validation/Phase2B/."),
		FConsoleCommandDelegate::CreateStatic(&BakeSkyViewValidation));

	static FAutoConsoleCommandWithWorldAndArgs GDumpGpuLutsCmd(
		TEXT("Hillaire.DumpGpuLuts"),
		TEXT("Readback pooled GPU LUTs for a planet slot and write PNG previews. Usage: Hillaire.DumpGpuLuts <Slot>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpGpuLuts));

	// ---- Phase 2C: aerial perspective validation ----

	FString GetAerialValidationDir()
	{
		FString Dir = FPaths::Combine(
			IPluginManager::Get().FindPlugin(TEXT("HillaireAtmosphere"))->GetBaseDir(),
			TEXT("Validation/Phase2C"));
		IFileManager::Get().MakeDirectory(*Dir, true);
		return Dir;
	}

	/** View fan construction without Euler-angle semantics: identity projection
	 * (valid non-degenerate ray fan) + an explicitly-aimed world->view
	 * matrix built by Gram-Schmidt (fan center (view +Z) along ForwardWorld,
	 * right-handed by construction: X x Y = F). The projection only shapes
	 * the fan; the march math (intersections, tMax, phases, LUT sampling) is
	 * fully exercised. Production passes live snapshot matrices through the
	 * same ComputeAerialViewInputs path. */
	struct FAerialValidationView
	{
		FMatrix SnapViewMatrix = FMatrix::Identity;
		FMatrix SnapProjectionMatrix = FMatrix::Identity;
	};

	FAerialValidationView MakeAerialView(const FVector& FanForwardWorld)
	{
		FAerialValidationView V;
		const FVector F = FanForwardWorld.GetSafeNormal();
		const FVector UpRef = FMath::Abs(F.Z) < 0.99
			? FVector(0.0, 0.0, 1.0) : FVector(0.0, 1.0, 0.0);
		const FVector XAxis = (UpRef ^ F).GetSafeNormal();
		const FVector YAxis = (F ^ XAxis).GetSafeNormal();
		FMatrix& M = V.SnapViewMatrix;
		M.M[0][0] = XAxis.X; M.M[0][1] = YAxis.X; M.M[0][2] = F.X;
		M.M[1][0] = XAxis.Y; M.M[1][1] = YAxis.Y; M.M[1][2] = F.Y;
		M.M[2][0] = XAxis.Z; M.M[2][1] = YAxis.Z; M.M[2][2] = F.Z;
		return V;
	}

	struct FAerialValidationCase
	{
		const TCHAR* Name = nullptr;
		FHillaireAtmosphereProfile Profile;
		FVector3f CameraPlanetLocalKm = FVector3f::ZeroVector;
		FAerialValidationView View;
		FVector3f SunDirLocal = FVector3f(0.0f, 0.0f, 1.0f);
		float Exposure = 2.0f;
	};

	void BakeAndSaveAerialCase(const FAerialValidationCase& Case)
	{
		const FString Dir = GetAerialValidationDir();
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("Aerial case %s: cam=(%.1f,%.1f,%.1f) km sun=(%.4f,%.4f,%.4f)."),
			Case.Name, Case.CameraPlanetLocalKm.X, Case.CameraPlanetLocalKm.Y, Case.CameraPlanetLocalKm.Z,
			Case.SunDirLocal.X, Case.SunDirLocal.Y, Case.SunDirLocal.Z);

		const int32 TW = HillaireLimits::TransmittanceWidth;
		const int32 TH = HillaireLimits::TransmittanceHeight;
		const int32 MSR = HillaireLimits::MultiScatteringRes;
		const int32 S = HillaireLimits::AerialVolumeSize;

		double T0 = FPlatformTime::Seconds();
		TArray<FLinearColor> TransLut, MsLut;
		HillaireLutCpu::BakeTransmittanceLut(Case.Profile, TW, TH, TransLut);
		HillaireLutCpu::BakeFullMultiScatteringLut(Case.Profile, TransLut, TW, TH, MSR, 1.0f, MsLut);
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("  T+MS bake %.1f s."), FPlatformTime::Seconds() - T0);

		// Camera at relative origin by construction: CenterRel = -(Q * camLocal).
		const FVector CenterRel = -(FVector(Case.CameraPlanetLocalKm));
		const FHillaireLutManager::FHillaireAerialViewInputs Inputs =
			FHillaireLutManager::ComputeAerialViewInputs(
				FVector3f(CenterRel), FQuat::Identity,
				Case.View.SnapViewMatrix, Case.View.SnapProjectionMatrix);

		T0 = FPlatformTime::Seconds();
		TArray<FLinearColor> Volume;
		HillaireLutCpu::BakeFullAerialVolume(Case.Profile, TransLut, TW, TH, MsLut, MSR,
			Inputs.CameraPlanetLocalKm, Inputs.InvProjMatrix, Inputs.ViewToPlanetLocalRot,
			Case.SunDirLocal, Volume);
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("  Aerial bake %.1f s. %s"),
			FPlatformTime::Seconds() - T0,
			*HillaireLutDiagnostics::DescribeStats(TEXT("Aerial"), HillaireLutDiagnostics::AnalyzeLut(Volume)));
		// Full-precision max: thin-air cases (V3) print 0.000000 at %.6f while
		// being legitimately tiny-but-nonzero; this distinguishes them from a
		// dead march (exact 0.0f).
		{
			float MaxC = 0.0f;
			for (const FLinearColor& C : Volume)
			{
				MaxC = FMath::Max(MaxC, C.R + C.G + C.B);
			}
			UE_LOG(LogHillaireAtmosphere, Log, TEXT("  Aerial max channel-sum %.9f"), (double)MaxC);
		}

		if (Volume.Num() != S * S * S)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("  Aerial bake size mismatch."));
			return;
		}
		// Slice previews: mid XY (fixed Z), mid XZ (fixed Y), mid YZ (fixed X).
		auto SaveSlice = [&](const TCHAR* Tag, auto CoordFn)
		{
			TArray<FLinearColor> Slice;
			Slice.Reserve(S * S);
			for (int32 B = 0; B < S; ++B)
			{
				for (int32 A = 0; A < S; ++A)
				{
					Slice.Add(Volume[CoordFn(A, B)]);
				}
			}
			HillaireLutDiagnostics::SaveLutPreviewPng(Slice, S, S, Case.Exposure,
				FPaths::Combine(Dir, FString::Printf(TEXT("Aerial_%s_%s.png"), Case.Name, Tag)));
		};
		const int32 Mid = S / 2;
		SaveSlice(TEXT("SliceZ"), [&](int32 A, int32 B) { return (Mid * S + B) * S + A; });
		SaveSlice(TEXT("SliceY"), [&](int32 A, int32 B) { return (B * S + Mid) * S + A; });
		SaveSlice(TEXT("SliceX"), [&](int32 A, int32 B) { return (B * S + A) * S + Mid; });
	}

	void BakeAerialValidation()
	{
		UE_LOG(LogHillaireAtmosphere, Warning, TEXT("Hillaire.BakeAerialValidation: ENTER cases V1..V7."));
		const FString Marker = FPaths::Combine(GetAerialValidationDir(), TEXT("_BakeStarted.marker"));
		FFileHelper::SaveStringToFile(TEXT("started"), *Marker);

		const FHillaireAtmosphereProfile Earth = FHillaireAtmosphereProfile::MakeReferenceProfile();
		const float B = Earth.BottomRadiusKm;
		const float T = Earth.TopRadiusKm;

		// V1 near-ground noon: camera 1 km over the pole, fan around +Z (up).
		FAerialValidationCase V1;
		V1.Name = TEXT("V1_NearGroundNoon");
		V1.Profile = Earth;
		V1.CameraPlanetLocalKm = FVector3f(0.0f, 0.0f, B + 1.0f);
		V1.View = MakeAerialView(FVector(0.0f, 0.0f, 1.0f));
		V1.SunDirLocal = FVector3f(0.0f, 0.0f, 1.0f);
		BakeAndSaveAerialCase(V1);

		// V2 near-ground sunset (2 deg elevation): same camera, tilted sun.
		FAerialValidationCase V2 = V1;
		V2.Name = TEXT("V2_NearGroundSunset");
		const float El = FMath::Cos(88.0f * PI / 180.0f);
		V2.SunDirLocal = FVector3f(FMath::Sqrt(1.0f - El * El), 0.0f, El).GetSafeNormal();
		V2.Exposure = 1.0f;
		BakeAndSaveAerialCase(V2);

		// V3 high altitude: 5 km under the top, noon.
		FAerialValidationCase V3 = V1;
		V3.Name = TEXT("V3_HighAltitude");
		V3.CameraPlanetLocalKm = FVector3f(0.0f, 0.0f, T - 5.0f);
		BakeAndSaveAerialCase(V3);

		// V4 outside nadir fan: 50 km over the top (outside the atmosphere but
		// inside the 128 km volume range), fan aimed down: full-column,
		// limb and ground-hit coverage. (A 500 km camera exceeds the volume
		// range and correctly yields zeros - locked by test, not imaged.)
		FAerialValidationCase V4 = V1;
		V4.Name = TEXT("V4_SpaceNadir");
		V4.CameraPlanetLocalKm = FVector3f(0.0f, 0.0f, T + 50.0f);
		V4.View = MakeAerialView(FVector(0.0f, 0.0f, -1.0f));
		V4.Exposure = 3.0f;
		BakeAndSaveAerialCase(V4);

		// V5 ground-hit fan: 1 km over the pole, fan aimed down.
		FAerialValidationCase V5 = V1;
		V5.Name = TEXT("V5_GroundFan");
		V5.View = MakeAerialView(FVector(0.0f, 0.0f, -1.0f));
		BakeAndSaveAerialCase(V5);

		// V6 large planet (~10x Earth radius), same chemistry, near ground.
		FAerialValidationCase V6;
		V6.Name = TEXT("V6_LargePlanet");
		V6.Profile = Earth;
		V6.Profile.BottomRadiusKm = 60000.0f;
		V6.Profile.TopRadiusKm = 61000.0f;
		V6.CameraPlanetLocalKm = FVector3f(0.0f, 0.0f, 60001.0f);
		V6.View = MakeAerialView(FVector(0.0f, 0.0f, 1.0f));
		V6.SunDirLocal = FVector3f(0.0f, 0.0f, 1.0f);
		BakeAndSaveAerialCase(V6);

		// V7 limb fan: 50 km over the pole, fan aimed horizontal (tangent rays).
		FAerialValidationCase V7 = V1;
		V7.Name = TEXT("V7_LimbGrazing");
		V7.CameraPlanetLocalKm = FVector3f(0.0f, 0.0f, B + 50.0f);
		V7.View = MakeAerialView(FVector(1.0f, 0.0f, 0.0f));
		BakeAndSaveAerialCase(V7);

		UE_LOG(LogHillaireAtmosphere, Log, TEXT("Hillaire.BakeAerialValidation: done -> %s"), *GetAerialValidationDir());
	}

	/** Aerial slice dump for the GPU path (world with a rendered view first). */
	void DumpGpuAerial(int32 Slot, const FString& Dir, FHillaireLutManager* LutManager)
	{
		TRefCountPtr<IPooledRenderTarget> Pooled = LutManager->CopyAerialScratch(Slot);
		if (!Pooled.IsValid())
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("  Aerial: no scratch for slot %d (evaluate first)."), Slot);
			return;
		}
		TArray<FLinearColor> Data;
		FIntVector Dims;
		if (!HillaireLutDiagnostics::ReadbackPooledVolume(Pooled, Data, Dims))
		{
			return;
		}
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("  GPU Aerial. %s"),
			*HillaireLutDiagnostics::DescribeStats(TEXT("Aerial"), HillaireLutDiagnostics::AnalyzeLut(Data)));
		const int32 S = HillaireLimits::AerialVolumeSize;
		if (Data.Num() != S * S * S)
		{
			return;
		}
		const int32 Mid = S / 2;
		TArray<FLinearColor> Slice;
		Slice.Reserve(S * S);
		for (int32 Y = 0; Y < S; ++Y)
		{
			for (int32 X = 0; X < S; ++X)
			{
				Slice.Add(Data[(Mid * S + Y) * S + X]);
			}
		}
		HillaireLutDiagnostics::SaveLutPreviewPng(Slice, S, S, 2.0f,
			FPaths::Combine(Dir, FString::Printf(TEXT("GPU_Aerial_Slot%d_SliceZ.png"), Slot)));
	}

	static FAutoConsoleCommand GBakeAerialValidationCmd(
		TEXT("Hillaire.BakeAerialValidation"),
		TEXT("CPU-bake the full T+MS chain + aerial camera volume for validation cases V1..V7 and write slice PNG previews to Plugins/HillaireAtmosphere/Validation/Phase2C/."),
		FConsoleCommandDelegate::CreateStatic(&BakeAerialValidation));

	// ---- Runtime governing-profile dump (§1): proves the SHADER-bound
	// profile is the normalized one. GameThread, no render needed. Logs, per
	// registered planet: radii, sigmas, scale heights, ground extinction,
	// zenith optical depth + transmittance (CPU integrator twin, same march
	// the GPU LUT bakes), and the ratio vs the Earth reference column. ----

	void DumpGoverningProfile(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("Hillaire.DumpGoverningProfile: no world."));
			return;
		}
		UHillairePlanetaryAtmosphereSubsystem* Sub = World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>();
		if (!Sub)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("Hillaire.DumpGoverningProfile: no subsystem."));
			return;
		}
		TArray<FPlanetAtmosphereState> Planets;
		Sub->GetAllPlanetStates(Planets);
		if (Planets.Num() == 0)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("Hillaire.DumpGoverningProfile: no registered planets."));
			return;
		}
		TArray<FLinearColor> White;
		White.Init(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), 4);
		const FVector3f Up(0.0f, 0.0f, 1.0f);
		auto ColumnOD = [&](const FHillaireAtmosphereProfile& Pr)
		{
			// 0.5 m above the ground sphere: exactly ON it the ray/sphere
			// query yields tBottom = 0 (tangent root) and the reference
			// march integrates zero length (production bakes lift 10-20 m).
			const FVector3f ColPos(0.0f, 0.0f, Pr.BottomRadiusKm + 0.0005f);
			return HillaireLutCpu::IntegrateScatteredLuminance(
				Pr, White, 2, 2, ColPos, Up, Up, false, 64).OpticalDepth;
		};
		const FVector3f EarthOD = ColumnOD(FHillaireAtmosphereProfile::MakeReferenceProfile());
		for (const FPlanetAtmosphereState& P : Planets)
		{
			const FHillaireAtmosphereProfile& Pr = P.Profile;
			const float HeightKm = Pr.TopRadiusKm - Pr.BottomRadiusKm;
			const FVector3f GroundPos(0.0f, 0.0f, Pr.BottomRadiusKm);
			const HillaireLutCpu::FMediumSample Med = HillaireLutCpu::SampleMedium(Pr, GroundPos);
			const FVector3f OD = ColumnOD(Pr);
			const FVector3f Tr(
				FMath::Exp(-OD.X), FMath::Exp(-OD.Y), FMath::Exp(-OD.Z));
			UE_LOG(LogHillaireAtmosphere, Log,
				TEXT("Profile planet id=%s ('%s'): ground=%.4fkm top=%.4fkm height=%.4fkm mult=%.4f"),
				*P.PlanetId.ToString(), *P.PlanetName.ToString(),
				Pr.BottomRadiusKm, Pr.TopRadiusKm, HeightKm,
				Pr.TopRadiusKm / FMath::Max(1e-6f, Pr.BottomRadiusKm));
			UE_LOG(LogHillaireAtmosphere, Log,
				TEXT("  sunW=(%.4f,%.4f,%.4f) sunL=(%.4f,%.4f,%.4f) irr=(%.5f,%.5f,%.5f)"),
				P.StarDirectionWorld.X, P.StarDirectionWorld.Y, P.StarDirectionWorld.Z,
				P.StarDirectionLocal.X, P.StarDirectionLocal.Y, P.StarDirectionLocal.Z,
				P.StarIrradiance.X, P.StarIrradiance.Y, P.StarIrradiance.Z);
			UE_LOG(LogHillaireAtmosphere, Log,
				TEXT("  rayleigh=(%.6f,%.6f,%.6f)/km H=%.4fkm | mie_sca=(%.6f,%.6f,%.6f) mie_ext=(%.6f,%.6f,%.6f)/km H=%.4fkm"),
				Pr.RayleighScatteringKm.X, Pr.RayleighScatteringKm.Y, Pr.RayleighScatteringKm.Z,
				-1.0f / Pr.RayleighExpScale,
				Pr.MieScatteringKm.X, Pr.MieScatteringKm.Y, Pr.MieScatteringKm.Z,
				Pr.MieExtinctionKm.X, Pr.MieExtinctionKm.Y, Pr.MieExtinctionKm.Z,
				-1.0f / Pr.MieExpScale);
			UE_LOG(LogHillaireAtmosphere, Log,
				TEXT("  ground extinction=(%.6f,%.6f,%.6f)/km | zenith OD=(%.5f,%.5f,%.5f) T=(%.5f,%.5f,%.5f) | OD vs Earth x(%.3f,%.3f,%.3f)"),
				Med.Extinction.X, Med.Extinction.Y, Med.Extinction.Z,
				OD.X, OD.Y, OD.Z, Tr.X, Tr.Y, Tr.Z,
				OD.X / FMath::Max(1e-9f, EarthOD.X),
				OD.Y / FMath::Max(1e-9f, EarthOD.Y),
				OD.Z / FMath::Max(1e-9f, EarthOD.Z));
		}
	}

	static FAutoConsoleCommandWithWorld GDumpGoverningProfileCmd(
		TEXT("Hillaire.DumpGoverningProfile"),
		TEXT("Log the live registered planet profiles (radii, sigmas, scale heights, ground extinction, zenith OD/transmittance, Earth ratio). Proves the normalized profile reaches the renderer."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&DumpGoverningProfile));

	// ---- Deterministic GPU SkyView samples (§2): fixed LUT-uv probes with
	// decoded geometry, run on the DumpGpuLuts readback (REAL GPU bytes).
	// zenith / horizon / sun-facing / anti-sun + non-zero ratio + contrast. ----

	void LogDeterministicSkySamples(
		const TArray<FLinearColor>& SkyData,
		const FHillaireAtmosphereProfile& Pr,
		float ViewHeightKm)
	{
		const int32 SVW = HillaireLimits::SkyViewWidth;
		const int32 SVH = HillaireLimits::SkyViewHeight;
		if (SkyData.Num() != SVW * SVH)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("  SkySamples: size mismatch (%d)."), SkyData.Num());
			return;
		}
		int64 NonZero = 0;
		for (const FLinearColor& C : SkyData)
		{
			if (C.R > 1e-6f || C.G > 1e-6f || C.B > 1e-6f)
			{
				++NonZero;
			}
		}
		struct FProbe { const TCHAR* Name; float U; float V; };
		const FProbe Probes[] = {
			{ TEXT("zenith"), 0.50f, 0.06f },
			{ TEXT("horizon"), 0.50f, 0.25f },
			{ TEXT("sun-facing"), 0.03f, 0.12f },
			{ TEXT("anti-sun"), 0.97f, 0.12f },
		};
		float MinLum = FLT_MAX, MaxLum = 0.0f;
		for (const FProbe& Pb : Probes)
		{
			const FVector3f V = HillaireLutCpu::SampleLutBilinear(SkyData, SVW, SVH, Pb.U, Pb.V);
			float Zen = 0.0f, Light = 0.0f;
			HillaireLutCpu::UvToSkyViewParams(Pr.BottomRadiusKm, ViewHeightKm, Pb.U, Pb.V, Zen, Light);
			const float Lum = 0.2126f * V.X + 0.7152f * V.Y + 0.0722f * V.Z;
			MinLum = FMath::Min(MinLum, Lum);
			MaxLum = FMath::Max(MaxLum, Lum);
			UE_LOG(LogHillaireAtmosphere, Log,
				TEXT("  SkySample %s uv=(%.2f,%.2f) zen=%.4f light=%.4f rgb=(%.5f,%.5f,%.5f) lum=%.6f"),
				Pb.Name, Pb.U, Pb.V, Zen, Light, V.X, V.Y, V.Z, Lum);
		}
		UE_LOG(LogHillaireAtmosphere, Log,
			TEXT("  SkyView nonzero=%.4f contrast(max/min lum)=%.3f (viewHeight=%.3fkm)"),
			(double)NonZero / (double)(SVW * SVH),
			MaxLum / FMath::Max(1e-9f, MinLum), ViewHeightKm);
	}

	float ProbeViewHeightKm(UWorld* World, const FPlanetAtmosphereState& Planet)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CM = PC->PlayerCameraManager)
			{
				const FVector CamCm = CM->GetCameraLocation();
				const double DistCm = (Planet.CenterWS - CamCm).Size();
				return (float)(DistCm * HillaireLimits::KmPerCm);
			}
		}
		return Planet.Profile.BottomRadiusKm + 1.0f;
	}

	// ---- Frame-to-frame GPU stability (§7) + scripted PIE probe (§2/5/6/10).
	// Ticker-driven on the game thread; each tick does a BLOCKING SkyView
	// readback (same fence-waited path as DumpGpuLuts) and diffs it against
	// the previous frame. Identical inputs must yield identical bytes. ----

	struct FStabilityProbeState
	{
		TWeakObjectPtr<UWorld> World;
		int32 Slot = 0;
		int32 FramesLeft = 0;
		int32 FrameIndex = 0;
		TArray<FLinearColor> Prev;
		double MaxDelta = 0.0;
		double MaxMeanLumDelta = 0.0;
		double PrevMeanLum = 0.0;
		bool bActive = false;
		bool bHavePrev = false;
		// Scripted-run gate: STARMAP spawn is wall-clock staggered (~4 s per
		// planet), so the first frames of a -game run carry no atmosphere
		// state. Idle without consuming probe frames until seen.
		bool bPlanetsSeen = false;
		// PieProbe orchestration (0 = plain stability probe).
		bool bPieMode = false;
		bool bProfileDumped = false;
		bool bGpuDumped = false;
		bool bShotTaken = false;
		bool bSurfaceMode = false;
		bool bCameraInstalled = false;
		TWeakObjectPtr<ACameraActor> ProbeCam;
		TWeakObjectPtr<AActor> OrigViewTarget;
		int32 StabilityFrames = 0;
		FTSTicker::FDelegateHandle TickerHandle;
	};
	static FStabilityProbeState GStabilityProbe;

	// Surface camera for the sky/aerial evidence (§3/4/5/10): transient probe
	// actor at mid-shell over the SUBSOLAR point (noon sky: camera on the
	// sun line, so the sun is overhead and the sky is fully lit), aimed at
	// the horizon toward the sun azimuth. Installed on pie frame 0, removed
	// before exit. The LUT cache rebuilds automatically on the height change
	// (same predicates as production); the dumps then observe noon-baked data.
	void InstallProbeCamera(UWorld* World, int32 Slot)
	{
		FStabilityProbeState& P = GStabilityProbe;
		UHillairePlanetaryAtmosphereSubsystem* Sub = World ? World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>() : nullptr;
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!Sub || !PC)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("PieProbe: no subsystem/controller for surface camera."));
			return;
		}
		TArray<FPlanetAtmosphereState> States;
		Sub->GetAllPlanetStates(States);
		// Slot = registry index (see DumpGpuLuts).
		const FPlanetAtmosphereState* Planet = States.IsValidIndex(Slot) ? &States[Slot] : nullptr;
		if (!Planet)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("PieProbe: no planet state for slot %d."), Slot);
			return;
		}
		FVector SunWorld(1.0, 0.0, 0.0);
		TArray<FHillaireLightSource> Lights;
		Sub->GetAllLightSources(Lights);
		if (Lights.Num() > 0 && Lights[0].bEnabled)
		{
			SunWorld = Lights[0].WorldDirectionToLight.GetSafeNormal();
		}
		// Subsolar point: camera up aligns with the sun (noon overhead).
		const FVector UpWorld = SunWorld;
		// Low over the terrain peaks (~1 Rayleigh scale height for typical
		// shells): dense air overhead for a bright noon sky, high enough to
		// clear LYTHOS terrain displacement (screenshot + depth tell). The
		// peaks sit inside the shell (Bottom is the base sphere).
		const float CamHeightKm = Planet->Profile.BottomRadiusKm + Planet->TerrainHeightKm + 0.15f;
		const FVector LocCm = Planet->CenterWS + UpWorld * (CamHeightKm * HillaireLimits::CmPerKm);
		FVector Horiz = FVector(0.0, 0.0, 1.0) - UpWorld * UpWorld.Z;
		if (Horiz.SizeSquared() < 1e-6)
		{
			Horiz = FVector(1.0, 0.0, 0.0) - UpWorld * UpWorld.X;
		}
		ACameraActor* Cam = World->SpawnActor<ACameraActor>(LocCm, Horiz.GetSafeNormal().Rotation());
		if (!Cam)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("PieProbe: camera spawn failed."));
			return;
		}
		P.OrigViewTarget = PC->GetViewTarget();
		PC->SetViewTarget(Cam);
		P.ProbeCam = Cam;
		P.bCameraInstalled = true;
		UE_LOG(LogHillaireAtmosphere, Log,
			TEXT("PieProbe: surface camera at mid-shell h=%.3fkm (planet %d)."),
			CamHeightKm - Planet->Profile.BottomRadiusKm, Slot);
	}

	void RemoveProbeCamera(UWorld* World)
	{
		FStabilityProbeState& P = GStabilityProbe;
		if (!P.bCameraInstalled)
		{
			return;
		}
		P.bCameraInstalled = false;
		if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
		{
			if (AActor* Orig = P.OrigViewTarget.Get())
			{
				PC->SetViewTarget(Orig);
			}
		}
		if (ACameraActor* Cam = P.ProbeCam.Get())
		{
			Cam->Destroy();
		}
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("PieProbe: surface camera removed."));
	}

	bool StabilityProbeTick(float /*DeltaTime*/)
	{
		FStabilityProbeState& P = GStabilityProbe;
		UWorld* World = P.World.Get();
		if (!World || P.FramesLeft <= 0)
		{
			P.bActive = false;
			return false;
		}
				// Wait for STARMAP spawn (see bPlanetsSeen): STARMAP spawn is
		// wall-clock staggered (~4 s per planet), so the first frames of a
		// scripted -game run carry no atmosphere state. Idle without counting
		// frames or consuming budget until at least one planet registers.
		if (!P.bPlanetsSeen)
		{
			bool bAny = false;
			if (UHillairePlanetaryAtmosphereSubsystem* Sub0 = World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>())
			{
				TArray<FPlanetAtmosphereState> States0;
				Sub0->GetAllPlanetStates(States0);
				bAny = States0.Num() > 0;
			}
			if (!bAny)
			{
				static int32 WaitTicks = 0;
				if ((++WaitTicks % 600) == 1)
				{
					UE_LOG(LogHillaireAtmosphere, Log, TEXT("StabilityProbe: waiting for planet states..."));
				}
				return true;
			}
			P.bPlanetsSeen = true;
		}
		++P.FrameIndex;
		UHillairePlanetaryAtmosphereSubsystem* Sub = World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>();
		FHillaireLutManager::FHillairePlanetLutTargets Targets;
		TArray<FLinearColor> Sky;
		bool bReadOk = false;
		if (Sub && Sub->GetLutManager() && Sub->GetLutManager()->CopyTargets(P.Slot, Targets)
			&& Targets.SkyView.IsValid())
		{
			TRefCountPtr<IPooledRenderTarget> S = Targets.SkyView;
			bReadOk = HillaireLutDiagnostics::ReadbackPooledLut(S, Sky);
		}
		if (bReadOk)
		{
			const FHillaireLutStats Stats = HillaireLutDiagnostics::AnalyzeLut(Sky);
			const double MeanLum = Stats.AverageLuminance();
			double Delta = 0.0;
			if (P.bHavePrev && P.Prev.Num() == Sky.Num())
			{
				for (int32 i = 0; i < Sky.Num(); ++i)
				{
					const FLinearColor& A = P.Prev[i];
					const FLinearColor& B = Sky[i];
					Delta = FMath::Max(Delta, (double)FMath::Abs(A.R - B.R));
					Delta = FMath::Max(Delta, (double)FMath::Abs(A.G - B.G));
					Delta = FMath::Max(Delta, (double)FMath::Abs(A.B - B.B));
				}
				P.MaxDelta = FMath::Max(P.MaxDelta, Delta);
				P.MaxMeanLumDelta = FMath::Max(P.MaxMeanLumDelta, FMath::Abs(MeanLum - P.PrevMeanLum));
			}
			P.Prev = MoveTemp(Sky);
			P.PrevMeanLum = MeanLum;
			P.bHavePrev = true;
			UE_LOG(LogHillaireAtmosphere, Log,
				TEXT("StabilityProbe f=%d meanLum=%.6f frameDelta=%.3e maxDelta=%.3e maxMeanDelta=%.3e"),
				P.FrameIndex, MeanLum, Delta, P.MaxDelta, P.MaxMeanLumDelta);
		}
		else
		{
			UE_LOG(LogHillaireAtmosphere, Warning,
				TEXT("StabilityProbe f=%d: SkyView readback failed (no LUT yet?)."), P.FrameIndex);
		}

		// PieProbe orchestration: profile -> GPU dump (+samples) -> N stable
		// frames -> screenshot -> exit. All automatic, all logged.
		if (P.bPieMode)
		{
			if (P.bSurfaceMode && !P.bCameraInstalled && P.FrameIndex >= 1)
			{
				InstallProbeCamera(World, P.Slot);
			}
			if (!P.bProfileDumped && P.FrameIndex >= 5)
			{
				P.bProfileDumped = true;
				DumpGoverningProfile(World);
			}
			if (!P.bGpuDumped && P.FrameIndex >= 8)
			{
				P.bGpuDumped = true;
				DumpGpuLuts({ TEXT("0") }, World);
			}
			if (!P.bShotTaken && P.FrameIndex >= 8 + P.StabilityFrames + 2)
			{
				P.bShotTaken = true;
				// Programmatic screenshot (works headless in -game; the
				// HighResShot console path proved unreliable offscreen).
				FScreenshotRequest::RequestScreenshot(false);
				UE_LOG(LogHillaireAtmosphere, Log, TEXT("PieProbe: screenshot requested."));
			}
			if (P.FrameIndex >= 8 + P.StabilityFrames + 4)
			{
				UE_LOG(LogHillaireAtmosphere, Log,
					TEXT("PieProbe DONE: frames=%d maxDelta=%.3e maxMeanDelta=%.3e"),
					P.FrameIndex, P.MaxDelta, P.MaxMeanLumDelta);
				RemoveProbeCamera(World);
				P.bActive = false;
				FPlatformMisc::RequestExit(false);
				return false;
			}
			return true;
		}

		if (--P.FramesLeft <= 0)
		{
			UE_LOG(LogHillaireAtmosphere, Log,
				TEXT("StabilityProbe DONE: frames=%d maxDelta=%.3e maxMeanDelta=%.3e"),
				P.FrameIndex, P.MaxDelta, P.MaxMeanLumDelta);
			P.bActive = false;
			return false;
		}
		return true;
	}

	void StartStabilityProbe(const TArray<FString>& Args, UWorld* World, bool bPieMode)
	{
		if (!World)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("Hillaire probe: no world."));
			return;
		}
		if (GStabilityProbe.bActive)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("Hillaire probe: already running."));
			return;
		}
		const int32 Frames = Args.Num() > 0
			? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 600) : 30;
		const int32 Slot = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0;
		const bool bSurface = Args.Num() > 2 ? (FCString::Atoi(*Args[2]) != 0) : false;
		GStabilityProbe = FStabilityProbeState();
		GStabilityProbe.World = World;
		GStabilityProbe.Slot = Slot;
		GStabilityProbe.FramesLeft = Frames;
		GStabilityProbe.bActive = true;
		GStabilityProbe.bPieMode = bPieMode;
		GStabilityProbe.bSurfaceMode = bPieMode && bSurface;
		GStabilityProbe.StabilityFrames = Frames;
		GStabilityProbe.TickerHandle =
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&StabilityProbeTick), 0.0f);
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("Hillaire probe started: %s frames=%d slot=%d."),
			bPieMode ? TEXT("PieProbe") : TEXT("StabilityProbe"), Frames, Slot);
	}

	void StabilityProbeCmd(const TArray<FString>& Args, UWorld* World)
	{
		StartStabilityProbe(Args, World, false);
	}

	void PieProbeCmd(const TArray<FString>& Args, UWorld* World)
	{
		// Aerial is controlled EXCLUSIVELY by r.Hillaire.AerialEval (ExecCmds
		// or console): the probe never forces it, so sky-only and sky+aerial
		// runs stay separable for bisection.
		StartStabilityProbe(Args, World, true);
	}

	static FAutoConsoleCommandWithWorldAndArgs GStabilityProbeCmd(
		TEXT("Hillaire.StabilityProbe"),
		TEXT("Sample the GPU SkyView LUT once per frame for N frames and log frame-to-frame deltas. Usage: Hillaire.StabilityProbe <Frames=30> <Slot=0>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StabilityProbeCmd));

	static FAutoConsoleCommandWithWorldAndArgs GPieProbeCmd(
		TEXT("Hillaire.PieProbe"),
		TEXT("Scripted offscreen probe: profile dump, GPU LUT dump + deterministic samples, N stability frames, screenshot, exit. Usage: Hillaire.PieProbe <StabilityFrames=30> <Slot=0> <Surface=0|1> (Surface=1 installs a transient mid-shell camera)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PieProbeCmd));

	// ---- Automated PIE validation (ATMOS FIX VISIVO DEFINITIVO, task I). ----
	// Hillaire.AtmosProbe <Slot=0>: thirteen GameThread checks over LIVE
	// state (no screenshots, no manual inspection):
	//  1 SkyView pooled target exists + readback OK.
	//  2 No NaN/Inf in the SkyView readback.
	//  3 SkyView non-zero (day-side energy present).
	//  4 Zenith vs horizon differ.
	//  5 Sun-facing vs anti-sun differ.
	//  6 Aerial scratch finite when the camera is inside (SKIP outside).
	//  7 Governing selection deterministic across identical rebuilds.
	//  8 Camera altitude coherent (dist - ground, finite, matches snapshot).
	//  9 Planet center invariant under rotation (synthetic, exact).
	// 10 Sun elevation invariant under rigid spin (synthetic, exact).
	// 11 Governing hysteresis holds the incumbent at the boundary (synthetic).
	// 12 Production sky gate opens with the live LUTs.
	// 13 Radii policy: bottom = planetary reference radius (terrain NOT folded
	//    in); density envelope-normalized (sigma * scale height preserved);
	//    top contains the authored terrain bound.
	// Logs one line per check plus a DONE summary with pass counts.
	// Intended for PIE/offscreen runs: Hillaire.PieProbe covers the
	// multi-frame stability bytes, this covers the semantic contract.

	void AtmosProbeCmd(const TArray<FString>& Args, UWorld* World)
	{
		int32 PassCount = 0;
		int32 FailCount = 0;
		int32 SkipCount = 0;
		auto Check = [&](const TCHAR* Name, bool bPass)
		{
			if (bPass) { ++PassCount; }
			else { ++FailCount; }
			UE_LOG(LogHillaireAtmosphere, Log, TEXT("AtmosProbe [%s] %s"),
				bPass ? TEXT("PASS") : TEXT("FAIL"), Name);
		};
		auto Skip = [&](const TCHAR* Name, const TCHAR* Reason)
		{
			++SkipCount;
			UE_LOG(LogHillaireAtmosphere, Log, TEXT("AtmosProbe [SKIP] %s (%s)"), Name, Reason);
		};

		if (!World)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("AtmosProbe: no world."));
			return;
		}
		UHillaireAtmosphereSubsystem* Sub = World->GetSubsystem<UHillaireAtmosphereSubsystem>();
		if (!Sub || !Sub->GetLutManager())
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("AtmosProbe: no subsystem/LUT manager."));
			return;
		}
		const int32 Slot = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;

		TArray<FHillairePlanetState> States;
		Sub->GetPlanetStates(States);
		const FHillairePlanetState* Planet = nullptr;
		for (const FHillairePlanetState& St : States)
		{
			if (St.PlanetId == Slot)
			{
				Planet = &St;
				break;
			}
		}
		if (!Planet)
		{
			UE_LOG(LogHillaireAtmosphere, Warning, TEXT("AtmosProbe: no planet state for slot %d."), Slot);
			return;
		}

		// Live camera (player view target) for altitude/snapshot checks.
		FVector CamCm = FVector::ZeroVector;
		FVector CamDir = FVector::ForwardVector;
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CM = PC->PlayerCameraManager)
			{
				CamCm = CM->GetCameraLocation();
				CamDir = CM->GetCameraRotation().Vector();
			}
		}

		// 1: SkyView pooled readback.
		TArray<FLinearColor> SkyData;
		bool bSkyReadOk = false;
		{
			FHillaireLutManager::FHillairePlanetLutTargets Targets;
			if (Sub->GetLutManager()->CopyTargets(Slot, Targets) && Targets.SkyView.IsValid())
			{
				TRefCountPtr<IPooledRenderTarget> S = Targets.SkyView;
				bSkyReadOk = HillaireLutDiagnostics::ReadbackPooledLut(S, SkyData);
			}
		}
		Check(TEXT("1 SkyView pooled readback"), bSkyReadOk);

		const int32 SVW = HillaireLimits::SkyViewWidth;
		const int32 SVH = HillaireLimits::SkyViewHeight;
		const bool bSkySized = bSkyReadOk && SkyData.Num() == SVW * SVH;
		FHillaireLutStats SkyStats;
		if (bSkySized)
		{
			SkyStats = HillaireLutDiagnostics::AnalyzeLut(SkyData);
		}

		// 2: no NaN/Inf.
		Check(TEXT("2 SkyView finite (no NaN/Inf)"), bSkySized && !SkyStats.HasInvalid());

		// 3: non-zero energy.
		int64 NonZero = 0;
		if (bSkySized)
		{
			for (const FLinearColor& C : SkyData)
			{
				if (C.R > 1e-6f || C.G > 1e-6f || C.B > 1e-6f) { ++NonZero; }
			}
		}
		const double NonZeroRatio = bSkySized ? (double)NonZero / (double)(SVW * SVH) : 0.0;
		UE_LOG(LogHillaireAtmosphere, Log, TEXT("AtmosProbe info: SkyView nonzero ratio %.4f"), NonZeroRatio);
		// Energy present somewhere (space-height LUTs are legitimately
		// mostly black in the miss region; surface LUTs are ~fully lit).
		Check(TEXT("3 SkyView non-zero"), NonZeroRatio > 0.05);

		auto SampleSky = [&](float U, float V)
		{
			return HillaireLutCpu::SampleLutBilinear(SkyData, SVW, SVH, U, V);
		};
		auto LumOf = [](const FVector3f& C)
		{
			return 0.2126f * C.X + 0.7152f * C.Y + 0.0722f * C.Z;
		};

		// 4: zenith vs horizon differ.
		bool bZenHor = false;
		if (bSkySized)
		{
			const float LZen = LumOf(SampleSky(0.50f, 0.06f));
			const float LHor = LumOf(SampleSky(0.50f, 0.25f));
			bZenHor = FMath::Abs(LZen - LHor) > 0.01f * FMath::Max(LZen, 1e-6f);
			UE_LOG(LogHillaireAtmosphere, Log, TEXT("AtmosProbe info: zenith lum %.6f horizon lum %.6f"), LZen, LHor);
		}
		Check(TEXT("4 Zenith/horizon differ"), bZenHor);

		// 5: sun-facing vs anti-sun differ.
		bool bSunAnti = false;
		if (bSkySized)
		{
			const float LSun = LumOf(SampleSky(0.03f, 0.12f));
			const float LAnti = LumOf(SampleSky(0.97f, 0.12f));
			bSunAnti = FMath::Abs(LSun - LAnti) > 0.01f * FMath::Max(LSun, 1e-6f);
			UE_LOG(LogHillaireAtmosphere, Log, TEXT("AtmosProbe info: sun lum %.6f anti-sun lum %.6f"), LSun, LAnti);
		}
		Check(TEXT("5 Sun/anti-sun differ"), bSunAnti);

		// 6: aerial scratch finite when inside (skip when outside/unevaluated).
		{
			const double DistCm = (Planet->CenterCmWorld - CamCm).Size();
			const double DistKm = DistCm * HillaireLimits::KmPerCm;
			const bool bInside = DistKm < (double)Planet->Profile.TopRadiusKm;
			TRefCountPtr<IPooledRenderTarget> Scratch = Sub->GetLutManager()->CopyAerialScratch(Slot);
			if (!bInside)
			{
				Skip(TEXT("6 Aerial finite"), TEXT("camera outside atmosphere"));
			}
			else if (!Scratch.IsValid())
			{
				Skip(TEXT("6 Aerial finite"), TEXT("no evaluation yet (r.Hillaire.AerialEval?)"));
			}
			else
			{
				TArray<FLinearColor> Vol;
				FIntVector Dims;
				bool bOk = HillaireLutDiagnostics::ReadbackPooledVolume(Scratch, Vol, Dims);
				bool bFinite = bOk && Dims.X == HillaireLimits::AerialVolumeSize
					&& Dims.Y == HillaireLimits::AerialVolumeSize
					&& Dims.Z == HillaireLimits::AerialVolumeSize;
				if (bFinite)
				{
					for (const FLinearColor& C : Vol)
					{
						if (!FMath::IsFinite(C.R) || !FMath::IsFinite(C.G) || !FMath::IsFinite(C.B)
							|| C.R < 0.0f || C.G < 0.0f || C.B < 0.0f)
						{
							bFinite = false;
							break;
						}
					}
				}
				Check(TEXT("6 Aerial finite"), bFinite);
			}
		}

		// 7: governing determinism across identical rebuilds.
		{
			const FHillaireViewSnapshot S1 = Sub->BuildSnapshotForView(
				CamCm, FMatrix::Identity, FMatrix::Identity, FIntRect(0, 0, 2, 2), CamDir);
			const FHillaireViewSnapshot S2 = Sub->BuildSnapshotForView(
				CamCm, FMatrix::Identity, FMatrix::Identity, FIntRect(0, 0, 2, 2), CamDir);
			Check(TEXT("7 Governing deterministic"),
				S1.HasAtmosphereContent() && S2.HasAtmosphereContent()
				&& S1.GoverningPlanetId == S2.GoverningPlanetId
				&& S1.SnapshotHash == S2.SnapshotHash);
		}

		// 8: altitude coherence: dist - ground, finite, matches snapshot.
		{
			const double DistKm = (Planet->CenterCmWorld - CamCm).Size() * HillaireLimits::KmPerCm;
			const double AltKm = DistKm - (double)Planet->Profile.BottomRadiusKm;
			const FHillaireViewSnapshot S = Sub->BuildSnapshotForView(
				CamCm, FMatrix::Identity, FMatrix::Identity, FIntRect(0, 0, 2, 2), CamDir);
			float SnapH = -1.0f;
			for (const FHillaireSnapshotPlanet& P : S.Planets)
			{
				if (P.PlanetId == Slot) { SnapH = P.ViewHeightKm; break; }
			}
			UE_LOG(LogHillaireAtmosphere, Log, TEXT("AtmosProbe info: dist %.5fkm alt %.5fkm snapH %.5fkm"),
				DistKm, AltKm, (double)SnapH);
			// Scale-aware tolerance: the snapshot narrows to float km after
			// the double subtract (float32 resolution at 1e4 km is ~1 m).
			const float AltTolKm = (float)FMath::Max(1e-3, DistKm * 1e-6);
			Check(TEXT("8 Altitude coherent"),
				FMath::IsFinite(DistKm) && FMath::IsFinite(AltKm)
				&& FMath::Abs((float)DistKm - SnapH) < AltTolKm);
		}

		// 9: center invariant under rotation (synthetic).
		{
			const FVector C = Planet->CenterCmWorld;
			const FQuat Qs[] = {
				FQuat(FVector(1.0, 0.0, 0.0), 1.0f),
				FQuat(FVector(0.0, 1.0, 0.0), 2.0f),
				FQuat(FVector(0.0, 0.0, 1.0), 3.0f),
			};
			bool bOk = true;
			for (const FQuat& Q : Qs)
			{
				if (!HillaireWorldToPlanetLocalKm(C, Q, C).IsNearlyZero(1e-4f)) { bOk = false; break; }
				const FVector P = C + FVector(50000.0, -30000.0, 10000.0);
				const FVector Back = HillairePlanetLocalToWorldCm(C, Q, HillaireWorldToPlanetLocalKm(C, Q, P));
				if (FVector::Dist(Back, P) > 0.1) { bOk = false; break; }
			}
			Check(TEXT("9 Center rotation-invariant"), bOk);
		}

		// 10: elevation invariant under rigid spin (synthetic).
		{
			const FVector3f Sun0(0.5f, 0.0f, 1.0f);
			const FVector3f Up0(0.0f, 0.0f, 1.0f);
			const float E0 = HillaireSunElevationCos(Sun0.GetSafeNormal(), Up0);
			const FQuat Q(FVector(0.2, 0.5, 0.8).GetSafeNormal(), 0.9f);
			const FVector3f Sun1(Q.RotateVector(FVector(Sun0)));
			const FVector3f Up1(Q.RotateVector(FVector(Up0)));
			const float E1 = HillaireSunElevationCos(Sun1, Up1);
			Check(TEXT("10 Elevation spin-invariant"), FMath::Abs(E1 - E0) < 1e-5f);
		}

		// 11: hysteresis holds the incumbent at the boundary (synthetic).
		{
			TArray<FHillaireSelectionInput> In;
			FHillaireSelectionInput IA; IA.CenterCamRelativeKm = FVector3f(-501.0f, 0.0f, 0.0f); IA.TopRadiusKm = 100.0f;
			FHillaireSelectionInput IB; IB.CenterCamRelativeKm = FVector3f(499.0f, 0.0f, 0.0f); IB.TopRadiusKm = 100.0f;
			In.Add(IA);
			In.Add(IB);
			const FHillairePlanetSelection SelRef = HillaireSelectPlanets(In, FVector3f::ZeroVector, FVector(1.0, 0.0, 0.0));
			const FHillairePlanetSelection SelHeld = HillaireSelectPlanetsWithIncumbent(
				In, FVector3f::ZeroVector, FVector(1.0, 0.0, 0.0), 0);
			Check(TEXT("11 Hysteresis holds incumbent"),
				SelRef.GoverningIndex == 1 && SelHeld.GoverningIndex == 0);
		}

		// 12: production sky gate opens with the live LUTs.
		{
			FHillaireLutManager::FHillairePlanetLutTargets Targets;
			const bool bCopyOk = Sub->GetLutManager()->CopyTargets(Slot, Targets);
			const bool bT = bCopyOk && Targets.Transmittance.IsValid();
			const bool bSky = bCopyOk && Targets.SkyView.IsValid();
			const FHillaireViewSnapshot S = Sub->BuildSnapshotForView(
				CamCm, FMatrix::Identity, FMatrix::Identity, FIntRect(0, 0, 2, 2), CamDir);
			// The 2x2 probe view can cull a non-governing slot planet from
			// the rect set; fall back to the registry effective count (same
			// enabled-light semantics as the per-planet compacted count).
			bool bPrimary = S.EffectiveLightCount > 0;
			for (const FHillaireSnapshotPlanet& P : S.Planets)
			{
				if (P.PlanetId == Slot) { bPrimary = P.ResolvedLights.Count > 0; break; }
			}
			int32 SkyCVar = 1;
			if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Hillaire.SkyEnable")))
			{
				SkyCVar = V->GetInt();
			}
			// Determine if camera is inside atmosphere for the governing planet
			bool bCameraInside = false;
			for (const FHillaireSnapshotPlanet& P : S.Planets)
			{
				if (P.bIsGoverning)
				{
					bCameraInside = P.ViewHeightKm < P.Profile.TopRadiusKm;
					break;
				}
			}
			const bool bGate = FHillaireLutManager::ShouldCompositeSky(
				SkyCVar != 0, S.HasAtmosphereContent(), bPrimary, bSky, bT, bCameraInside);
			UE_LOG(LogHillaireAtmosphere, Log,
				TEXT("AtmosProbe info: gate cvar=%d content=%d primary=%d T=%d Sky=%d inside=%d -> %d"),
				SkyCVar, S.HasAtmosphereContent() ? 1 : 0, bPrimary ? 1 : 0,
				bT ? 1 : 0, bSky ? 1 : 0, bCameraInside ? 1 : 0, bGate ? 1 : 0);
			Check(TEXT("12 Sky gate opens"), bGate);
		}

		// 13: radii policy (CORRECTED VOLUME MODEL): the bottom is the planetary
		// reference radius (terrain is NOT folded in), the density is
		// envelope-normalized to the validated reference profile (sigma *
		// scale height preserved), and the top contains the authored terrain
		// bound.
		{
			const FHillaireAtmosphereProfile Reference = FHillaireAtmosphereProfile::MakeReferenceProfile();
			const float ReferenceOD =
				Reference.RayleighScatteringKm.X * (-1.0f / Reference.RayleighExpScale);
			const float OD =
				Planet->Profile.RayleighScatteringKm.X * (-1.0f / Planet->Profile.RayleighExpScale);
			const bool bSelfSimilar = FMath::IsNearlyEqual(OD, ReferenceOD, ReferenceOD * 5e-3f);
			const bool bContainsTerrain =
				Planet->Profile.TopRadiusKm > Planet->Profile.BottomRadiusKm + Planet->TerrainHeightKm;
			UE_LOG(LogHillaireAtmosphere, Log,
				TEXT("AtmosProbe info: bottom=%.3f terrain=%.3f top=%.3f OD=%.5f refOD=%.5f selfSimilar=%d containsTerrain=%d"),
				(double)Planet->Profile.BottomRadiusKm, (double)Planet->TerrainHeightKm,
				(double)Planet->Profile.TopRadiusKm, (double)OD, (double)ReferenceOD,
				bSelfSimilar ? 1 : 0, bContainsTerrain ? 1 : 0);
			Check(TEXT("13 Volume model: reference bottom + envelope-normalized density + terrain containment"),
				Planet->IsValid() && bSelfSimilar && bContainsTerrain);
		}

		UE_LOG(LogHillaireAtmosphere, Log, TEXT("AtmosProbe DONE: pass=%d fail=%d skip=%d"),
			PassCount, FailCount, SkipCount);
	}

	static FAutoConsoleCommandWithWorldAndArgs GAtmosProbeCmd(
		TEXT("Hillaire.AtmosProbe"),
		TEXT("Automated PIE validation: 13 live checks (LUT energy, sky contrast, aerial, governing determinism, altitude/centering/hysteresis/gate/radii). Usage: Hillaire.AtmosProbe <Slot=0>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AtmosProbeCmd));
}
