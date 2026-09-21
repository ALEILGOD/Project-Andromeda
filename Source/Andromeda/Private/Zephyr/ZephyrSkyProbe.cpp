// ZEPHYR SKY PROBE - scripted GPU validation cameras (test tooling only).
//
// Console: Zephyr.SkyProbe <PlanetIndex=0>
// Drives a set of scripted viewpoints on one live planet (from the
// authoritative ATMOS subsystem states - no new planet/sun state anywhere),
// waits for LUT rebuild + exposure settle at each, takes a programmatic
// screenshot, then restores the view target and exits. Intended for headless
// -game runs:
//
//   UnrealEditor.exe <project> <map> -game -d3d12 -ExecCmds="Zephyr.SkyProbe 0"
//
// Shots (sun elevation e = asin(dot(sunDir, camUp))):
//   0 day:      subsolar (e=+90), 0.5 km over the surface, look horizontal.
//   1 sunset:   terminator (e=0), 0.05 km, look at the horizon toward the sun.
//   2 twilight: e=-6deg, 0.05 km, look at the horizon toward the sun.
//   3 night:    e=-40deg, 0.05 km, look at the zenith.
//   4 space:    day side, 3x shell thickness, look at the planet center.
//
// Screenshots land in Saved/Screenshots/<Platform>/ScreenShotNNNNN.png.

#include "HillairePlanetaryAtmosphereSubsystem.h"
#include "HillairePlanetAtmosphereState.h"
#include "HillaireLimits.h"
#include "AndromedaNoiseLibrary.h"
#include "Zephyr/ZephyrLog.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Planet/Planet.h"
#include "ProceduralMeshComponent.h"
#include "Sun.h"
#include "UnrealClient.h"

namespace
{
	struct FSkyProbeShot
	{
		const TCHAR* Name = TEXT("?");
		float SunElevDeg = 0.0f; // desired sun elevation at the camera up
		float HeightKm = 0.05f;  // over the surface (space shot overrides)
		float ShellMult = 0.0f;  // when >0: h = ShellMult * thickness
		int32 LookMode = 0;      // 0 horizontal, 1 horizon-to-sun, 2 zenith, 3 planet center
	};

	const FSkyProbeShot GSkyProbeShots[] = {
		{ TEXT("day"),      90.0f, 0.005f, 0.0f, 0 },
		{ TEXT("sunset"),    0.0f, 0.005f, 0.0f, 1 },
		{ TEXT("twilight"), -6.0f, 0.005f, 0.0f, 1 },
		{ TEXT("night"),   -40.0f, 0.005f, 0.0f, 2 },
		{ TEXT("space"),    45.0f, 0.000f, 25.0f, 3 },
	};
	constexpr int32 GSkyProbeShotCount = UE_ARRAY_COUNT(GSkyProbeShots);
	constexpr int32 GSkyProbeSettleFrames = 150;

	struct FSkyProbeState
	{
		TWeakObjectPtr<UWorld> World;
		int32 PlanetIndex = 0;
		int32 ShotIndex = -1; // -1 = waiting for planets
		int32 ShotFrame = 0;
		AActor* OrigViewTarget = nullptr;
		ACameraActor* ProbeCam = nullptr;
		FTSTicker::FDelegateHandle TickerHandle;
		bool bActive = false;
	};
	FSkyProbeState GSkyProbe;

	FVector SkyProbePerp(const FVector& SunDir)
	{
		FVector Ref = FMath::Abs(SunDir.Z) < 0.99 ? FVector(0.0, 0.0, 1.0) : FVector(1.0, 0.0, 0.0);
		FVector Perp = SunDir ^ Ref;
		if (Perp.SizeSquared() < 1e-12)
		{
			Perp = FVector(0.0, 1.0, 0.0);
		}
		return Perp.GetSafeNormal();
	}

	bool SkyProbeTick(float /*DeltaTime*/)
	{
		FSkyProbeState& P = GSkyProbe;
		if (!P.bActive)
		{
			return false;
		}
		UWorld* World = P.World.Get();
		if (!World)
		{
			if (GEngine && GEngine->GameViewport)
			{
				World = GEngine->GameViewport->GetWorld();
				P.World = World;
			}
			if (!World)
			{
				return true;
			}
		}
		UHillairePlanetaryAtmosphereSubsystem* Sub =
			World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>();
		if (!Sub)
		{
			return true;
		}
		TArray<FPlanetAtmosphereState> States;
		Sub->GetAllPlanetStates(States);
		if (!States.IsValidIndex(P.PlanetIndex))
		{
			return true; // STARMAP stagger: wait for the planet
		}
		const FPlanetAtmosphereState& Planet = States[P.PlanetIndex];
		if (!Planet.IsValid())
		{
			return true;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC)
		{
			return true;
		}

		if (P.ShotIndex < 0)
		{
			P.ShotIndex = 0;
			P.ShotFrame = 0;
			P.OrigViewTarget = PC->GetViewTarget();

			// Mesh-scale diagnostic: ATMOS radii come from APlanet::PlanetRadius,
			// but the RENDERED mesh may be scaled by the Blueprint. Log the live
			// actor scale + procedural-mesh world bounds vs the ATMOS ground.
			for (TActorIterator<APlanet> It(World); It; ++It)
			{
				const APlanet* Pl = *It;
				if (!Pl)
				{
					continue;
				}
				const FVector Scale = Pl->GetActorScale3D();
				const float MeshRadiusKm = Pl->PlanetProceduralMesh
					? (float)(Pl->PlanetProceduralMesh->Bounds.SphereRadius * HillaireLimits::KmPerCm)
					: -1.0f;
				UE_LOG(LogZephyr, Log,
					TEXT("[SkyProbe] meshcheck %s PlanetRadius=%.3fkm actorScale=(%.3f,%.3f,%.3f) meshBoundsRadius=%.3fkm"),
					*Pl->GetName(), Pl->PlanetRadius * (float)HillaireLimits::KmPerCm,
					Scale.X, Scale.Y, Scale.Z, MeshRadiusKm);
			}
		}
		if (P.ShotIndex >= GSkyProbeShotCount)
		{
			if (P.ProbeCam)
			{
				P.ProbeCam->Destroy();
				P.ProbeCam = nullptr;
			}
			if (P.OrigViewTarget)
			{
				PC->SetViewTarget(P.OrigViewTarget);
			}
			UE_LOG(LogZephyr, Log, TEXT("[SkyProbe] DONE (%d shots)."), GSkyProbeShotCount);
			P.bActive = false;
			FPlatformMisc::RequestExit(false);
			return false;
		}

		const FSkyProbeShot& Shot = GSkyProbeShots[P.ShotIndex];
		{
			const FVector SunDir = Planet.StarDirectionWorld.GetSafeNormal();
			const FVector Perp = SkyProbePerp(SunDir);
			const float ElevRad = FMath::DegreesToRadians(Shot.SunElevDeg);
			const FVector CamUp =
				(SunDir * FMath::Sin(ElevRad) + Perp * FMath::Cos(ElevRad)).GetSafeNormal();
			const float ThicknessKm =
				FMath::Max(0.05f, Planet.AtmosphereTopRadiusKm - Planet.GroundRadiusKm);
			// CORRECTED VOLUME MODEL: the atmosphere bottom is the planetary
			// REFERENCE radius (sea level); terrain rises through the volume.
			// Surface shots place the eye at the LOCAL terrain height + 2 m
			// (never at the authored peak bound), so they validate the real
			// surface sky. The space shot orbits at 3x the volume thickness
			// above the reference radius.
			float SurfaceHeightKm = 0.0f;
			if (Shot.ShellMult <= 0.0f)
			{
				const APlanet* LivePlanet = nullptr;
				for (TActorIterator<APlanet> It(World); It; ++It)
				{
					if (HillaireMakeStablePlanetId((*It)->PlanetID, (*It)->PlanetSeed) == Planet.PlanetId)
					{
						LivePlanet = *It;
						break;
					}
				}
				if (LivePlanet)
				{
					// The procedural mesh is generated in PLANET-LOCAL space:
					// sample the terrain in the planet frame (rotation removed).
					const FVector LocalUp = Planet.RotationWS.Inverse().RotateVector(CamUp);
					const float NoiseHeight = UAndromedaNoiseLibrary::GeneratePlanetHeight(
						LocalUp,
						LivePlanet->PlanetSeed,
						LivePlanet->ContinentalScale,
						LivePlanet->MountainScale,
						LivePlanet->DetailScale,
						LivePlanet->MountainStrength,
						LivePlanet->DetailStrength);
					SurfaceHeightKm = FMath::Max(0.0f,
						NoiseHeight * LivePlanet->TerrainHeight * (float)HillaireLimits::KmPerCm);
				}
			}
			const float RadiusKm = (Shot.ShellMult > 0.0f)
				? (Planet.GroundRadiusKm + Shot.ShellMult * ThicknessKm)
				: (Planet.GroundRadiusKm + SurfaceHeightKm + FMath::Max(Shot.HeightKm, 0.002f));
			const FVector LocCm = Planet.CenterWS + CamUp * (RadiusKm * HillaireLimits::CmPerKm);
			const float Hkm = RadiusKm - Planet.GroundRadiusKm;

			FVector LookDir = Perp;
			if (Shot.LookMode == 1)
			{
				FVector SunHoriz = SunDir - CamUp * (SunDir | CamUp);
				if (SunHoriz.SizeSquared() < 1e-8)
				{
					SunHoriz = Perp;
				}
				LookDir = (SunHoriz.GetSafeNormal() - CamUp * 0.05f).GetSafeNormal();
			}
			else if (Shot.LookMode == 2)
			{
				LookDir = CamUp;
			}
			else if (Shot.LookMode == 3)
			{
				LookDir = -CamUp;
			}
			else
			{
				// Horizontal, 30 deg up, away from the sun azimuth.
				FVector SunHoriz = SunDir - CamUp * (SunDir | CamUp);
				if (SunHoriz.SizeSquared() < 1e-8)
				{
					SunHoriz = Perp;
				}
				const FVector Away = (Perp - SunHoriz.GetSafeNormal() * 0.5f).GetSafeNormal();
				LookDir = (Away * 0.87f + CamUp * 0.5f).GetSafeNormal();
			}

			// Prefer driving the actual player pawn: a single scene view keeps
			// the ATMOS per-view snapshot unambiguous. A spawned camera would
			// add a second view (see ATMOS multi-view snapshot handling).
			APawn* Pawn = PC->GetPawn();
			if (Pawn)
			{
				Pawn->SetActorLocationAndRotation(LocCm, LookDir.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
				PC->SetControlRotation(LookDir.Rotation());
			}
			else if (!P.ProbeCam)
			{
				P.ProbeCam = World->SpawnActor<ACameraActor>(LocCm, LookDir.Rotation());
				if (!P.ProbeCam)
				{
					UE_LOG(LogZephyr, Warning, TEXT("[SkyProbe] camera spawn failed."));
					return true;
				}
				PC->SetViewTarget(P.ProbeCam);
			}
			else
			{
				P.ProbeCam->SetActorLocationAndRotation(LocCm, LookDir.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
			}

			if (P.ShotFrame == 0)
			{
				UE_LOG(LogZephyr, Log,
					TEXT("[SkyProbe] shot %d/%d '%s' planet=%s center=(%.0f,%.0f,%.0f) ground=%.3fkm top=%.3fkm h=%.3fkm cam=(%.0f,%.0f,%.0f) sunIrr=(%.3f,%.3f,%.3f) lights=%d"),
					P.ShotIndex + 1, GSkyProbeShotCount, Shot.Name,
					*Planet.PlanetName.ToString(),
					Planet.CenterWS.X, Planet.CenterWS.Y, Planet.CenterWS.Z,
					Planet.GroundRadiusKm, Planet.AtmosphereTopRadiusKm, Hkm,
					LocCm.X, LocCm.Y, LocCm.Z,
					Planet.StarIrradiance.X, Planet.StarIrradiance.Y, Planet.StarIrradiance.Z,
					Planet.ResolvedLights.Count);
			}
		}

		++P.ShotFrame;
		if (P.ShotFrame >= GSkyProbeSettleFrames)
		{
			const AActor* VT = PC->GetViewTarget();
			const FVector CamLoc = P.ProbeCam ? P.ProbeCam->GetActorLocation() : FVector::ZeroVector;
			FVector ActualCamCm = FVector::ZeroVector;
			if (const APawn* Pawn = PC->GetPawn())
			{
				ActualCamCm = Pawn->GetActorLocation();
			}
			const double ActualDistKm =
				(ActualCamCm - Planet.CenterWS).Size() * HillaireLimits::KmPerCm;
			UE_LOG(LogZephyr, Log,
				TEXT("[SkyProbe] shot '%s' captured (inside=%d viewTarget=%s probeCam=%s camLoc=(%.0f,%.0f,%.0f) actualDistKm=%.3f topKm=%.3f)."),
				Shot.Name, Planet.bCameraInside ? 1 : 0,
				VT ? *VT->GetName() : TEXT("<null>"),
				P.ProbeCam ? TEXT("yes") : TEXT("null"),
				CamLoc.X, CamLoc.Y, CamLoc.Z,
				ActualDistKm, Planet.AtmosphereTopRadiusKm);
			FScreenshotRequest::RequestScreenshot(false);
			// Diagnostic: read back the real GPU SkyView LUT at this shot's
			// camera height and log deterministic probes (zenith/horizon/sun).
			if (P.ShotIndex == 0 && GEngine)
			{
				GEngine->Exec(World, TEXT("Hillaire.DumpGpuLuts 0"));
			}
			if (P.ProbeCam)
			{
				P.ProbeCam->Destroy();
				P.ProbeCam = nullptr;
			}
			++P.ShotIndex;
			P.ShotFrame = 0;
		}
		return true;
	}

	void ShowStarMeshCmd(const TArray<FString>& Args, UWorld* World, FOutputDevice& /*Ar*/)
	{
		const bool bShow = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : true;
		if (!World)
		{
			return;
		}
		int32 Count = 0;
		for (TActorIterator<ASun> It(World); It; ++It)
		{
			if (ASun* Sun = *It)
			{
				if (Sun->SunMesh)
				{
					Sun->SunMesh->SetVisibility(bShow, true);
					++Count;
				}
			}
		}
		UE_LOG(LogZephyr, Log, TEXT("[SkyProbe] ShowStarMesh %d applied to %d sun(s)."), bShow ? 1 : 0, Count);
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GZephyrShowStarMeshCmd(
		TEXT("Zephyr.ShowStarMesh"),
		TEXT("Show/hide the decorative star mesh (arg 0/1). Test tooling."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ShowStarMeshCmd));

	void StartSkyProbe()
	{
		if (GSkyProbe.bActive)
		{
			UE_LOG(LogZephyr, Warning, TEXT("[SkyProbe] already running."));
			return;
		}
		GSkyProbe = FSkyProbeState();
		GSkyProbe.PlanetIndex = 0;
		GSkyProbe.bActive = true;
		GSkyProbe.TickerHandle =
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&SkyProbeTick), 0.0f);
		UE_LOG(LogZephyr, Log, TEXT("[SkyProbe] armed: shots=%d settle=%d."),
			GSkyProbeShotCount, GSkyProbeSettleFrames);
	}

	void SkyProbeCmd(const TArray<FString>& Args, UWorld* /*World*/, FOutputDevice& /*Ar*/)
	{
		if (Args.Num() > 0)
		{
			GSkyProbe.PlanetIndex = FMath::Max(0, FCString::Atoi(*Args[0]));
		}
		StartSkyProbe();
	}

	static FAutoConsoleCommandWithWorldArgsAndOutputDevice GSkyProbeCmd(
		TEXT("Zephyr.SkyProbe"),
		TEXT("Scripted sky validation cameras (day/sunset/twilight/night/space) with screenshots, then exit."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&SkyProbeCmd));
}
