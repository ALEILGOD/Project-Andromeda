// =========================================================
// UNIFIED ATMOSPHERE CONSOLE (PHASE 2.1 — §21/§22/§23)
// =========================================================
// The ONLY documented console namespace of the atmosphere system:
//
//   r.AndromedaAtmosphere.Enable
//   r.AndromedaAtmosphere.DebugMode
//   r.AndromedaAtmosphere.Validate
//   r.AndromedaAtmosphere.Status
//   r.AndromedaAtmosphere.CaptureViews
//   r.AndromedaAtmosphere.SurfaceShot
//   r.AndromedaAtmosphere.FieldShot
//   r.AndromedaAtmosphere.OrbitShot
//
// Every entry above is REALLY registered here (verified by build +
// by r.AndromedaAtmosphere.Validate/Status at runtime). The legacy
// stage namespaces (r.AndromedaZephyr.*, r.AndromedaAtmos.*) remain
// as transitional stage gates; the unified commands govern them.
//
// Root cause fixed (§R1 unification plan): the spec names did not
// exist in code, so PIE reported "command not recognized".
//
// SUN LIGHT REFERENCE: r.AndromedaAtmosphere.SunReference reports the
// authoritative light reference (Sun actor + emission point +
// convention + baked per-planet directions).
// =========================================================

#include "Atmosphere/AndromedaAtmosphereSystem.h"
#include "Atmosphere/AndromedaUnifiedAtmosphereRenderer.h"
#include "Atmosphere/AndromedaAtmosphereRenderer.h"
#include "Atmosphere/AtmosphereLightReferenceComponent.h"
#include "Planet/Zephyr/ZephyrRenderer.h"
#include "Planet/Zephyr/ZephyrSharedAtmosphere.h"
#include "Planet/Zephyr/ZephyrTypes.h"
#include "StarSystem.h"
#include "Sun.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceNull.h"

namespace
{
    // Single gate of the unified pass (checked render-thread-safe
    // by both stage wrappers). 1 = enabled, 0 = pass-through.
    TAutoConsoleVariable<int> CVarUnifiedAtmosphereEnable(
        TEXT("r.AndromedaAtmosphere.Enable"),
        1,
        TEXT("Enable the unified Andromeda atmosphere (aerial stage + LUT sky). 0 = pass-through, proves the pixels come from the atmosphere system."),
        ECVF_RenderThreadSafe
    );

    void ForwardToStageCommand(
        const TCHAR* StageCommand,
        const TArray<FString>& Args
    )
    {
        IConsoleObject* Obj =
            IConsoleManager::Get().FindConsoleObject(StageCommand);

        if (Obj == nullptr)
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Warning,
                TEXT("[ATMOS-UNIFIED] Stage command not found: %s"),
                StageCommand
            );
            return;
        }

        IConsoleCommand* Cmd = Obj->AsCommand();

        if (Cmd == nullptr)
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Warning,
                TEXT("[ATMOS-UNIFIED] Console object is not a command: %s"),
                StageCommand
            );
            return;
        }

        FOutputDeviceNull Ar;
        Cmd->Execute(Args, nullptr, Ar);
    }

    void SetStageDebugMode(int32 ZephyrVisual)
    {
        IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.AndromedaZephyr.DebugMode")
        );

        if (CVar != nullptr)
        {
            CVar->Set(ZephyrVisual);
        }
        else
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Warning,
                TEXT("[ATMOS-UNIFIED] Stage debug variable not found.")
            );
        }
    }

    // ---- Real per-planet diagnostics (shared by DebugMode 1-4
    // ---- and by Status). Everything printed comes from the live
    // ---- unified snapshot / last rendered frame. No estimates.

    void PrintRegions()
    {
        TArray<FUnifiedAtmosphereFramePlanetInfo> Frame;
        int32 Governing = -1;
        uint64 Version = 0;

        FUnifiedAtmosphereRenderer::GetLastFrameInfo(
            Frame,
            Governing,
            Version
        );

        if (Frame.Num() == 0)
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Log,
                TEXT("[ATMOS-UNIFIED] Regions: no frame rendered yet (snapshot v%llu)."),
                Version
            );
            return;
        }

        for (const FUnifiedAtmosphereFramePlanetInfo& Info : Frame)
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Log,
                TEXT("[ATMOS-UNIFIED] Regions: planet %lld dist %.1f cm height %.1f cm %s (snapshot v%llu)."),
                Info.PlanetID,
                Info.DistanceCm,
                Info.HeightAboveGroundCm,
                Info.bInsideAtmosphere ? TEXT("INSIDE") : TEXT("OUTSIDE"),
                Version
            );
        }
    }

    void PrintSelection()
    {
        TArray<FUnifiedAtmosphereFramePlanetInfo> Frame;
        int32 Governing = -1;
        uint64 Version = 0;

        FUnifiedAtmosphereRenderer::GetLastFrameInfo(
            Frame,
            Governing,
            Version
        );

        UE_LOG(
            LogAndromedaAtmosphere,
            Log,
            TEXT("[ATMOS-UNIFIED] Selection: %d planets, governing frame index %d (snapshot v%llu)."),
            Frame.Num(),
            Governing,
            Version
        );

        for (int32 i = 0; i < Frame.Num(); ++i)
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Log,
                TEXT("[ATMOS-UNIFIED] Selection: [%d] planet %lld dist %.1f cm%s."),
                i,
                Frame[i].PlanetID,
                Frame[i].DistanceCm,
                (i == Governing) ? TEXT(" <== GOVERNING") : TEXT("")
            );
        }
    }

    // SUN LIGHT REFERENCE diagnostics (DebugMode 3): prints the
    // baked per-planet SunDirectionWorld straight from the snapshot
    // (the exact vectors Packed6 carries to the GPU), plus planet
    // center, rotation (informational: NOT applied to the sun) and
    // the governing planet of the last rendered frame. No
    // reconstruction, no rotation: what you read is what renders.
    void PrintSunDirections()
    {
        TArray<FAndromedaAtmosphereInstance> Snapshot;
        FVector Star = FVector::ZeroVector;
        uint64 Version = 0;

        FAndromedaAtmosphereSystem::Get().GetSnapshot(
            Snapshot,
            Star,
            Version
        );

        TArray<FUnifiedAtmosphereFramePlanetInfo> Frame;
        int32 Governing = -1;
        uint64 FrameVersion = 0;

        FUnifiedAtmosphereRenderer::GetLastFrameInfo(
            Frame,
            Governing,
            FrameVersion
        );

        if (Snapshot.Num() == 0)
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Log,
                TEXT("[ATMOS-UNIFIED] SunDir: snapshot empty (v%llu)."),
                Version
            );
            return;
        }

        UE_LOG(
            LogAndromedaAtmosphere,
            Log,
            TEXT("[ATMOS-UNIFIED] SunDir: frame WORLD (Case A, Planet->Sun). Emission point %s (snapshot v%llu, governing frame index %d)."),
            *Star.ToString(),
            Version,
            Governing
        );

        for (int32 i = 0; i < Snapshot.Num(); ++i)
        {
            const FAndromedaAtmosphereInstance& Entry = Snapshot[i];
            const FVector& D = Entry.SunDirectionWorld;

            UE_LOG(
                LogAndromedaAtmosphere,
                Log,
                TEXT("[ATMOS-UNIFIED] SunDir: planet %lld dir (%.4f %.4f %.4f)|len %.4f| center %s rot (P%.2f Y%.2f R%.2f)%s."),
                Entry.PlanetID,
                D.X, D.Y, D.Z, D.Size(),
                *Entry.PlanetCenter.ToString(),
                Entry.PlanetRotation.Pitch,
                Entry.PlanetRotation.Yaw,
                Entry.PlanetRotation.Roll,
                (i == Governing) ? TEXT(" <== GOVERNING") : TEXT("")
            );
        }
    }

    // SUN LIGHT REFERENCE report (r.AndromedaAtmosphere.SunReference).
    // Game thread only (touches UObjects). No per-frame spam: manual
    // command. Shows: Sun actor resolution path, reference validity,
    // emission point vs mailbox star, and the baked per-planet
    // directions (the exact Packed6 contents).
    void PrintSunReference()
    {
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] ---- SUN LIGHT REFERENCE ----"));

        UWorld* World =
            (GEngine != nullptr)
                ? GEngine->GetCurrentPlayWorld()
                : nullptr;

        if (World == nullptr)
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Warning,
                TEXT("[ATMOS-UNIFIED] SunReference: no play world (outside PIE?).")
            );
            return;
        }

        AStarSystem* FoundSystem = nullptr;
        for (TActorIterator<AStarSystem> It(World); It; ++It)
        {
            FoundSystem = *It;
            break;
        }

        if (FoundSystem == nullptr)
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Warning,
                TEXT("[ATMOS-UNIFIED] SunReference: no AStarSystem in world.")
            );
            return;
        }

        AActor* SunActor = FoundSystem->GetSunActor();

        UE_LOG(
            LogAndromedaAtmosphere,
            Log,
            TEXT("[ATMOS-UNIFIED] SunReference: StarSystem=%s SunActor=%s."),
            *FoundSystem->GetName(),
            SunActor != nullptr ? *SunActor->GetName() : TEXT("<none: fallback active>")
        );

        const UAtmosphereLightReferenceComponent* LightRef =
            (SunActor != nullptr)
                ? SunActor->FindComponentByClass<UAtmosphereLightReferenceComponent>()
                : nullptr;

        if (LightRef != nullptr)
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Log,
                TEXT("[ATMOS-UNIFIED] SunReference: %s."),
                *LightRef->GetReferenceSummary()
            );
        }
        else
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Warning,
                TEXT("[ATMOS-UNIFIED] SunReference: no AtmosphereLightReference; Registry fallback (StarSystem location as emission point) is active.")
            );
        }

        TArray<FAndromedaAtmosphereInstance> Snapshot;
        FVector Star = FVector::ZeroVector;
        uint64 Version = 0;

        FAndromedaAtmosphereSystem::Get().GetSnapshot(
            Snapshot,
            Star,
            Version
        );

        UE_LOG(
            LogAndromedaAtmosphere,
            Log,
            TEXT("[ATMOS-UNIFIED] SunReference: mailbox star (emission point) %s, %d planets (snapshot v%llu). Convention: TowardSun = Planet->Sun, WORLD frame, no rotation."),
            *Star.ToString(),
            Snapshot.Num(),
            Version
        );

        for (const FAndromedaAtmosphereInstance& Entry : Snapshot)
        {
            const FVector& D = Entry.SunDirectionWorld;
            const bool bUnit = FMath::Abs(D.Size() - 1.0f) < 1e-3f;

            UE_LOG(
                LogAndromedaAtmosphere,
                Log,
                TEXT("[ATMOS-UNIFIED] SunReference: planet %lld baked dir (%.4f %.4f %.4f) %s."),
                Entry.PlanetID,
                D.X, D.Y, D.Z,
                bUnit ? TEXT("UNIT-OK") : TEXT("NON-UNIT (check!)")
            );
        }
    }

    void PrintProfiles()
    {
        TArray<FAndromedaAtmosphereInstance> Snapshot;
        FVector Star = FVector::ZeroVector;
        uint64 Version = 0;

        FAndromedaAtmosphereSystem::Get().GetSnapshot(
            Snapshot,
            Star,
            Version
        );

        for (const FAndromedaAtmosphereInstance& Entry : Snapshot)
        {
            const FAndromedaAtmosphereProfile& P = Entry.Profile;

            UE_LOG(
                LogAndromedaAtmosphere,
                Log,
                TEXT("[ATMOS-UNIFIED] Profile: planet %lld hash %llu ground %.0f cm atmo %.0f cm terrain %.0f cm Rayleigh (%.4f %.4f %.4f) Mie (%.4f %.4f %.4f) g %.3f Abs (%.5f %.5f %.5f) dens %.2f mieDens %.2f albedo %.2f."),
                Entry.PlanetID,
                P.ComputeProfileHash(),
                P.GroundRadius,
                P.AtmosphereRadius,
                Entry.TerrainHeightCm,
                P.RayleighScattering.X, P.RayleighScattering.Y, P.RayleighScattering.Z,
                P.MieScattering.X, P.MieScattering.Y, P.MieScattering.Z,
                P.MieAnisotropy,
                P.AbsorptionCoefficients.X, P.AbsorptionCoefficients.Y, P.AbsorptionCoefficients.Z,
                P.AtmosphericDensityScale,
                P.MieDensityScale,
                P.GroundAlbedo
            );
        }
    }

    void PrintRuntimeProof()
    {
        const bool bInit = FUnifiedAtmosphereRenderer::IsInitialized();

        TArray<FAndromedaAtmosphereInstance> Snapshot;
        FVector Star = FVector::ZeroVector;
        uint64 Version = 0;

        FAndromedaAtmosphereSystem::Get().GetSnapshot(
            Snapshot,
            Star,
            Version
        );

        const uint64 Aerial = FUnifiedAtmosphereRenderer::GetAerialPassCount();
        const uint64 Sky = FUnifiedAtmosphereRenderer::GetSkyPassCount();
        const uint64 LUTRegens = FZephyrRenderer::GetLUTRegenCount();

        TArray<FUnifiedAtmosphereFramePlanetInfo> Frame;
        int32 Governing = -1;
        uint64 FrameVersion = 0;

        FUnifiedAtmosphereRenderer::GetLastFrameInfo(
            Frame,
            Governing,
            FrameVersion
        );

        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] ---- RUNTIME PROOF ----"));
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] System initialized: %s"), bInit ? TEXT("YES") : TEXT("NO"));
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] Atmosphere instances: %d"), Snapshot.Num());
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] Planet snapshot received: %s (v%llu)"), Snapshot.Num() > 0 ? TEXT("YES") : TEXT("NO"), Version);
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] Star published: %s"), (Star != FVector::ZeroVector) ? TEXT("YES") : TEXT("NO"));
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] GPU data uploaded: %s (%d planets, frame v%llu)"), Frame.Num() > 0 ? TEXT("YES") : TEXT("PENDING"), Frame.Num(), FrameVersion);
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] LUT generation executed: %s (%llu regens)"), LUTRegens > 0 ? TEXT("YES") : TEXT("PENDING"), LUTRegens);
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] SkyView generation executed: %s (%llu regens)"), LUTRegens > 0 ? TEXT("YES") : TEXT("PENDING"), LUTRegens);
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] Atmosphere render pass executed: %s (aerial %llu, sky %llu)"), (Aerial > 0 && Sky > 0) ? TEXT("YES") : TEXT("PENDING"), Aerial, Sky);
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] Final composite executed: %s (sky stage writes Tonemap output)"), Sky > 0 ? TEXT("YES") : TEXT("PENDING"));
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] Governing planet (last frame): %d"), Governing);
        UE_LOG(LogAndromedaAtmosphere, Log, TEXT("[ATMOS-UNIFIED] SkyView LUT stores ZEPHYR_RADIANCE_SCALE-calibrated radiance (SS+MS bake outputs, ZephyrCommon.ush contract; inspect post-scale radiance via DebugMode 2/3/7)."));
    }

    // Transition diagnostics (mandated completion rule): for the
    // governing planet of the last rendered frame prints live
    // PlanetRadius / TerrainHeight / CompletionRadius /
    // CameraDistance / Factor. CameraDistance comes from the render
    // thread frame info (the exact ViewOrigin the frame rendered
    // with). Factor mirrors ZephyrSkyTransitionFade
    // (1 at/below completion, smoothstep to 0 at the outer edge);
    // the shader owns the curve, this mirror is display-only.
    void PrintTransition()
    {
        TArray<FAndromedaAtmosphereInstance> Snapshot;
        FVector Star = FVector::ZeroVector;
        uint64 Version = 0;

        FAndromedaAtmosphereSystem::Get().GetSnapshot(
            Snapshot,
            Star,
            Version
        );

        TArray<FUnifiedAtmosphereFramePlanetInfo> Frame;
        int32 Governing = -1;
        uint64 FrameVersion = 0;

        FUnifiedAtmosphereRenderer::GetLastFrameInfo(
            Frame,
            Governing,
            FrameVersion
        );

        if (Governing < 0 || Governing >= Snapshot.Num()
            || Governing >= Frame.Num())
        {
            UE_LOG(
                LogAndromedaAtmosphere,
                Log,
                TEXT("[ATMOS-UNIFIED] Transition: no governing planet (deep space or no frame yet).")
            );
            return;
        }

        const FAndromedaAtmosphereInstance& Entry = Snapshot[Governing];

        const float PlanetRadius = Entry.Profile.GroundRadius;
        const float TerrainHeight = Entry.TerrainHeightCm;
        const float CompletionRadius =
            AndromedaAtmosphereReference::ComputeTransitionCompleteRadiusCm(
                PlanetRadius,
                TerrainHeight);
        const double CameraDistance = Frame[Governing].DistanceCm;
        const float OuterRadius = FMath::Max(
            Entry.SkyTransitionRadiusCm,
            CompletionRadius);

        float Factor = 1.0f;
        if (CameraDistance > static_cast<double>(CompletionRadius)
            && OuterRadius > CompletionRadius)
        {
            const float F = FMath::Clamp(
                static_cast<float>(
                    (CameraDistance - static_cast<double>(CompletionRadius))
                    / static_cast<double>(FMath::Max(
                        OuterRadius - CompletionRadius, 1e-6f))),
                0.0f,
                1.0f);
            Factor = 1.0f - F * F * (3.0f - 2.0f * F);
        }
        else if (CameraDistance > static_cast<double>(CompletionRadius))
        {
            Factor = 0.0f;
        }

        UE_LOG(
            LogAndromedaAtmosphere,
            Log,
            TEXT("[ATMOS-UNIFIED] Transition: Planet %lld PlanetRadius = %.0f TerrainHeight = %.0f CompletionRadius = %.0f CameraDistance = %.0f Factor = %.3f."),
            Entry.PlanetID,
            static_cast<double>(PlanetRadius),
            static_cast<double>(TerrainHeight),
            static_cast<double>(CompletionRadius),
            CameraDistance,
            static_cast<double>(Factor)
        );

        // Sun state for the governing planet (same inputs the sun disk
        // path consumes): sky outer blend start, solar elevation above
        // the local horizon, and the exact transmittance-LUT coordinates
        // (HCam01, MuSunG) used for SunTrans. Proves SkyVisibility != 0
        // regimes and the disk attenuation inputs without sampling the
        // GPU LUT on the CPU. Game-thread camera may lag the rendered
        // frame by one tick; noted, not hidden.
        if (GEngine != nullptr)
        {
            if (UWorld* PlayWorld = GEngine->GetCurrentPlayWorld())
            {
                if (APlayerController* PC =
                    PlayWorld->GetFirstPlayerController())
                {
                    if (const APlayerCameraManager* CamMgr =
                        PC->PlayerCameraManager)
                    {
                        const FVector CamPos =
                            CamMgr->GetCameraLocation();
                        const FVector ToCam =
                            CamPos - Entry.PlanetCenter;
                        const double CamDistNow = ToCam.Size();

                        if (CamDistNow > 1e-6)
                        {
                            const FVector UpNow = ToCam / CamDistNow;
                            const float MuSunNow = static_cast<float>(
                                FVector::DotProduct(
                                    Entry.SunDirectionWorld, UpNow));
                            const float SunElevDeg =
                                FMath::RadiansToDegrees(
                                    FMath::Asin(FMath::Clamp(
                                        MuSunNow, -1.0f, 1.0f)));
                            const float ShellNow = FMath::Max(
                                Entry.Profile.AtmosphereRadius
                                - Entry.Profile.GroundRadius,
                                1.0f);
                            const float HCam01Now = FMath::Clamp(
                                static_cast<float>(
                                    (CamDistNow - static_cast<double>(
                                        Entry.Profile.GroundRadius))
                                    / static_cast<double>(ShellNow)),
                                0.0f,
                                1.0f);

                            UE_LOG(
                                LogAndromedaAtmosphere,
                                Log,
                                TEXT("[ATMOS-UNIFIED] SunState: Planet %lld SkyOuterStart = %.0f SunElevation = %.2f deg SunDiskLUT = (HCam01 %.3f, MuSunG %.3f)."),
                                Entry.PlanetID,
                                static_cast<double>(
                                    Entry.SkyTransitionRadiusCm),
                                static_cast<double>(SunElevDeg),
                                static_cast<double>(HCam01Now),
                                static_cast<double>(MuSunNow)
                            );
                        }
                    }
                }
            }
        }
    }

    // Unified DebugMode (int 0-8). Visual modes forward to the real
    // LUT-pipeline visuals; diagnostic modes keep the normal sky and
    // print real per-planet data. Mapping (unified -> stage visual):
    //   0->0, 5->1, 6->7, 7->8, 8->3, 1/2/3/4->0 + text diagnostics.
    FAutoConsoleCommand GUnifiedDebugModeCommand(
        TEXT("r.AndromedaAtmosphere.DebugMode"),
        TEXT("Unified atmosphere diagnostics: 0 normal, 1 regions, 2 selection, 3 SunDirPlanet, 4 profile, 5 transmittance, 6 SkyView, 7 atlas/dataset, 8 MultiScatter."),
        FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
        {
            int32 Mode = 0;
            if (Args.Num() > 0)
            {
                Mode = FCString::Atoi(*Args[0]);
            }
            Mode = FMath::Clamp(Mode, 0, 8);

            switch (Mode)
            {
            case 1:
                SetStageDebugMode(0);
                PrintRegions();
                break;
            case 2:
                SetStageDebugMode(0);
                PrintSelection();
                break;
            case 3:
                SetStageDebugMode(0);
                PrintSunDirections();
                break;
            case 4:
                SetStageDebugMode(0);
                PrintProfiles();
                break;
            case 5:
                SetStageDebugMode(1);
                break;
            case 6:
                SetStageDebugMode(7);
                break;
            case 7:
                SetStageDebugMode(8);
                break;
            case 8:
                SetStageDebugMode(3);
                break;
            case 0:
            default:
                SetStageDebugMode(0);
                break;
            }

            UE_LOG(
                LogAndromedaAtmosphere,
                Log,
                TEXT("[ATMOS-UNIFIED] DebugMode %d applied."),
                Mode
            );
        })
    );

    FAutoConsoleCommand GUnifiedValidateCommand(
        TEXT("r.AndromedaAtmosphere.Validate"),
        TEXT("Validates the unified atmosphere: shader infrastructure, snapshot, dispatch/LUT counters."),
        FConsoleCommandDelegate::CreateLambda([]()
        {
            const bool bAerialValid =
                FAndromedaAtmosphereRenderer::ValidateShaderInfrastructure();
            const bool bSkyValid =
                FZephyrRenderer::ValidateShaderInfrastructure();

            UE_LOG(
                LogAndromedaAtmosphere,
                Log,
                TEXT("[ATMOS-UNIFIED] Shader infrastructure aerial(annex legacy): %s | sky(LUT): %s."),
                bAerialValid ? TEXT("VALID") : TEXT("PENDING"),
                bSkyValid ? TEXT("VALID") : TEXT("PENDING")
            );

            PrintRuntimeProof();
        })
    );

    FAutoConsoleCommand GUnifiedStatusCommand(
        TEXT("r.AndromedaAtmosphere.Status"),
        TEXT("Runtime proof of the unified atmosphere (instances, snapshot, GPU, LUT, passes, governing planet)."),
        FConsoleCommandDelegate::CreateLambda([]()
        {
            PrintRuntimeProof();
            PrintSelection();
            PrintProfiles();
            PrintTransition();
        })
    );

    FAutoConsoleCommand GUnifiedSunReferenceCommand(
        TEXT("r.AndromedaAtmosphere.SunReference"),
        TEXT("Sun light reference diagnostics: Sun actor, emission point, convention, per-planet baked directions."),
        FConsoleCommandDelegate::CreateLambda([]()
        {
            PrintSunReference();
        })
    );

    FAutoConsoleCommand GUnifiedCaptureViewsCommand(
        TEXT("r.AndromedaAtmosphere.CaptureViews"),
        TEXT("Unified forward: captures one screenshot per debug visual. Usage: r.AndromedaAtmosphere.CaptureViews <DelaySeconds>"),
        FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
        {
            ForwardToStageCommand(TEXT("r.AndromedaZephyr.CaptureViews"), Args);
        })
    );

    FAutoConsoleCommand GUnifiedSurfaceShotCommand(
        TEXT("r.AndromedaAtmosphere.SurfaceShot"),
        TEXT("Unified forward: in-atmosphere capture. Usage: r.AndromedaAtmosphere.SurfaceShot <planet> [yaw] [mode] [instant] [pitch] [alt01]"),
        FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
        {
            ForwardToStageCommand(TEXT("r.AndromedaZephyr.SurfaceShot"), Args);
        })
    );

    FAutoConsoleCommand GUnifiedFieldShotCommand(
        TEXT("r.AndromedaAtmosphere.FieldShot"),
        TEXT("Unified forward: day/terminator/night capture. Usage: r.AndromedaAtmosphere.FieldShot <planet> <side> [yaw] [pitch] [mode] [alt01]"),
        FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
        {
            ForwardToStageCommand(TEXT("r.AndromedaZephyr.FieldShot"), Args);
        })
    );

    FAutoConsoleCommand GUnifiedOrbitShotCommand(
        TEXT("r.AndromedaAtmosphere.OrbitShot"),
        TEXT("Unified forward: near-planet inspection. Usage: r.AndromedaAtmosphere.OrbitShot <planet> <radii> [mode]"),
        FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
        {
            ForwardToStageCommand(TEXT("r.AndromedaZephyr.OrbitShot"), Args);
        })
    );
}
