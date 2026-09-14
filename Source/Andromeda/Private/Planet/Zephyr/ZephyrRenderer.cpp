#include "Planet/Zephyr/ZephyrRenderer.h"

#include "Planet/Zephyr/ZephyrShaders.h"
#include "Planet/Zephyr/ZephyrSharedAtmosphere.h"
#include "Planet/Zephyr/ZephyrManager.h"
#include "Planet/Zephyr/ZephyrViewExtension.h"
#include "Atmosphere/AndromedaAtmosphereSystem.h"
#include "Atmosphere/AtmosphereLightReferenceComponent.h"

#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIFeatureLevel.h"
#include "RHI.h"
#include "ScreenPass.h"
#include "SceneView.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "RenderCommandFence.h"
#include "Kismet/GameplayStatics.h"

#include <atomic>

// =========================================================
// GLOBAL SHADER REGISTRATION
// =========================================================

IMPLEMENT_GLOBAL_SHADER(
    FZephyrTransmittanceCS,
    "/Andromeda/Zephyr/ZephyrTransmittance.usf",
    "ZephyrTransmittanceMainCS",
    SF_Compute
);

IMPLEMENT_GLOBAL_SHADER(
    FZephyrMultiScatterCS,
    "/Andromeda/Zephyr/ZephyrMultiScatter.usf",
    "ZephyrMultiScatterMainCS",
    SF_Compute
);

IMPLEMENT_GLOBAL_SHADER(
    FZephyrSkyViewCS,
    "/Andromeda/Zephyr/ZephyrSkyView.usf",
    "ZephyrSkyViewMainCS",
    SF_Compute
);

IMPLEMENT_GLOBAL_SHADER(
    FZephyrSkyPS,
    "/Andromeda/Zephyr/ZephyrSky.usf",
    "ZephyrSkyMainPS",
    SF_Pixel
);

// =========================================================
// CONSOLE VARIABLES
// =========================================================

namespace
{
    TAutoConsoleVariable<int> CVarZephyrEnable(
        TEXT("r.AndromedaZephyr.Enable"),
        1,
        TEXT("Enable the ZEPHYR true planetary sky (0 = pass-through, proves ATMOS/ZEPHYR separation)."),
        ECVF_RenderThreadSafe
    );

    TAutoConsoleVariable<int> CVarZephyrDebug(
        TEXT("r.AndromedaZephyr.DebugMode"),
        0,
        TEXT("ZEPHYR diagnostic: 0 SKY_ONLY, 1 TRANSMITTANCE, 2 SINGLE_SCATTER, 3 MULTI_SCATTER, 4 MIE, 5 RAYLEIGH, 6 ABSORPTION, 7 SKY_VIEW_LUT, 8 MULTI_SCATTER_LUT."),
        ECVF_RenderThreadSafe
    );

    TAutoConsoleVariable<float> CVarZephyrExposure(
        TEXT("r.AndromedaZephyr.Exposure"),
        1.0f,
        TEXT("Display-referred exposure of the ZEPHYR sky composite."),
        ECVF_RenderThreadSafe
    );

    TAutoConsoleVariable<float> CVarZephyrSunRadiusDeg(
        TEXT("r.AndromedaZephyr.SunAngularRadiusDeg"),
        0.53f,
        TEXT("Apparent angular DIAMETER of the star disk in degrees (Earth sun ~0.53). Disk only, never a sky tint."),
        ECVF_RenderThreadSafe
    );

    // Display calibration (visual refinement): noon disk (T~=1) still
    // saturates white, while a Stratopause-thin sunset path
    // (T_red~=0.4, T_green~=0.13, T_blue~=0.01 on toy columns) maps to
    // (1.7, 0.5, 0.03): warm attenuated orange-red instead of clipping
    // white at 20x (8.4, 2.6, 0.14). Noon appearance is preserved
    // (still clipped white); only the sunset tail enters display
    // range. Tunable at runtime, never a sky tint (sky never reads it).
    TAutoConsoleVariable<float> CVarZephyrSunLuminance(
        TEXT("r.AndromedaZephyr.SunLuminance"),
        4.0f,
        TEXT("Display-referred luminance of the transmittance-attenuated star disk."),
        ECVF_RenderThreadSafe
    );

    TAutoConsoleVariable<float> CVarZephyrMS(
        TEXT("r.AndromedaZephyr.MultiScatterScale"),
        1.0f,
        TEXT("Exposure of the real second-order term (0 = single scattering only; proves MS is not single*constant). View-keyed."),
        ECVF_RenderThreadSafe
    );

    TAutoConsoleVariable<float> CVarZephyrMie(
        TEXT("r.AndromedaZephyr.MieScale"),
        1.0f,
        TEXT("Mie cross-section multiplier (0 disables aerosols in extinction AND scattering; re-bakes planet LUTs). Planet-keyed."),
        ECVF_RenderThreadSafe
    );

    TAutoConsoleVariable<float> CVarZephyrAbs(
        TEXT("r.AndromedaZephyr.AbsorptionScale"),
        1.0f,
        TEXT("Absorption cross-section multiplier (0 disables the ozone-like layer; re-bakes planet LUTs). Planet-keyed."),
        ECVF_RenderThreadSafe
    );

    TAutoConsoleVariable<int> CVarZephyrFreezeLUTs(
        TEXT("r.AndromedaZephyr.FreezeLUTs"),
        0,
        TEXT("Diagnostic: reuse the cached LUTs even when invalidation keys change (proves the cache path)."),
        ECVF_RenderThreadSafe
    );

    std::atomic<uint64> GDispatchCounter{ 0 };
    std::atomic<uint64> GLUTRegenCounter{ 0 };
}

// =========================================================
// DIAGNOSTIC CONSOLE COMMAND
// =========================================================

namespace
{
    FAutoConsoleCommand GZephyrValidateCommand(
        TEXT("r.AndromedaZephyr.Validate"),
        TEXT("Validates ZEPHYR shader infrastructure and reports LUT cache state + dispatch counts."),
        FConsoleCommandDelegate::CreateLambda([]()
        {
            const bool bValid =
                FZephyrRenderer::ValidateShaderInfrastructure();

            UE_LOG(
                LogAndromedaZephyr,
                Log,
                TEXT("[ZEPHYR-01] Shader infrastructure %s. Sky dispatches: %llu | LUT regens: %llu"),
                bValid ? TEXT("VALID") : TEXT("NOT VALIDATED (global shader map may still compile)"),
                FZephyrRenderer::GetDispatchCount(),
                FZephyrRenderer::GetLUTRegenCount()
            );
        })
    );
}

// =========================================================
// DEBUG CAPTURE COMMAND (game thread)
// =========================================================
// r.AndromedaZephyr.CaptureViews <DelaySeconds>
//
// Headless validation helper (also usable in PIE): waits
// <DelaySeconds> for planets to spawn, then captures one
// screenshot per ZEPHYR debug mode (0..7) into
// [Project]/Saved/Screenshots/Zephyr/ and restores mode 0.
// Debug modes 1 and 7 render fullscreen LUT maps, so they
// prove LUT contents from ANY camera (even a buried one).
namespace
{
    struct FZephyrCaptureState
    {
        bool bActive = false;
        float WaitRemaining = 0.0f;
        float CaptureCooldown = 0.0f;
        int32 NextMode = 0;
        int32 LastMode = 7;
        FString OutputDir;
        IConsoleVariable* DebugCVar = nullptr;
        int32 PendingTeleportPlanet = -1;
        float PendingYawDeg = 90.0f;
        float PendingPitchDeg = 0.0f;
        // Tracking teleport: re-applied every ticker fire until
        // the capture completes, so the pawn stays glued to the
        // fast-moving substellar point even if world pause is
        // unavailable. -1 = off.
        int32 TrackingPlanet = -1;
        float TrackingYawDeg = 90.0f;
        float TrackingPitchDeg = 0.0f;
        // TrackingRadii > 0 selects orbit mode (re-teleport via
        // ZephyrTeleportPawnToOrbit at this radii multiple);
        // 0 selects surface mode (ZephyrTeleportPawnToPlanet).
        float TrackingRadii = 0.0f;
        // Instant capture: screenshot on the same tick as the
        // teleport (SurfaceShot 4th arg = 1). No pause, no
        // cooldown: proves whether the view follows the pawn
        // before orbital motion can intervene.
        bool bInstantCapture = false;
        // LIVE-FRAME settle (diagnostic reliability, day/night
        // task): a screenshot requested in the same fire as the
        // teleport/mode-set captures a stale backbuffer predating
        // the new state. Phase machine: 0 = gate/direct (instant
        // leftovers finish here), 1 = settle presents, 2 = set
        // mode + request + await completion.
        int32 Phase = 0;
        float PhaseTime = 0.0f;
        float SettleNeed = 2.5f;
        // FieldShot pending (day/terminator/night positioning by
        // planet-centered side). -1 = none.
        int32 PendingFieldPlanet = -1;
        int32 PendingFieldSide = 0; // 0 day(substellar) 1 terminator 2 night(antistellar)
        float PendingFieldYawDeg = 90.0f;
        float PendingFieldPitchDeg = 0.0f;
        // Shell-height fraction for surface teleports (0.5 keeps
        // the historical default; 0.8+ clears tall local terrain
        // whose mesh can exceed the analytic TerrainHeight).
        float PendingAlt01 = 0.5f;
        float TrackingAlt01 = 0.5f;
        // Render-backlog fence (diagnostic): run once per capture
        // run before the first screenshot request. Measures (and
        // drains) the render-thread backlog so the captured frame
        // is freshly rendered, not minutes-old queued work.
        bool bFenceDone = false;
        // Field tracking (FieldShot glue): when true the ticker
        // re-teleports to the planet-centered field side instead
        // of substellar, so terminator/night spots survive long
        // hitches and orbital drift like SurfaceShot tracking.
        bool bTrackingField = false;
        int32 TrackingFieldSide = 0;
        // Jump recovery: last glued pawn position; a re-teleport
        // displacing the pawn by more than this (post-hitch
        // recovery) resets the live-frame settle.
        FVector LastGluePos = FVector::ZeroVector;
        bool bHasGluePos = false;
        // World pause for capture stability (diagnostic): set on
        // the first successful teleport, cleared at completion.
        // Freezing the world stops orbital key churn (no more
        // per-frame LUT rebakes), lets the GPU queue drain so
        // presents go live, and pins the pawn (no gravity fall).
        // The core ticker, render thread and screenshot backend
        // all keep running while paused (photo-mode pattern).
        bool bPausedForCapture = false;
    };

    FZephyrCaptureState GZephyrCapture;

    // Shared game-thread helpers for the teleport diagnostics.
    // Returns false when the mailbox is empty.
    static bool ZephyrFetchSnapshot(
        TArray<FZephyrPlanetSnapshotEntry>& OutPlanets,
        FVector& OutStarWorld)
    {
        uint64 Version = 0;
        // PHASE 2.1: single unified mailbox (same snapshot the
        // aerial stage reads). FZephyrManager is deprecated.
        FAndromedaAtmosphereSystem::Get().GetSnapshot(
            OutPlanets, OutStarWorld, Version);

        return OutPlanets.Num() > 0
            && OutStarWorld != FVector::ZeroVector;
    }

    // Returns the first player controller with a possessed pawn
    // in the game/PIE world, or null.
    static APlayerController* ZephyrGetPlayerController()
    {
        if (!GEngine)
        {
            return nullptr;
        }

        for (const FWorldContext& Context :
            GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::Game
                || Context.WorldType == EWorldType::PIE)
            {
                UWorld* GameWorld = Context.World();

                if (!GameWorld)
                {
                    continue;
                }

                APlayerController* PC =
                    GameWorld->GetFirstPlayerController();

                if (PC && PC->GetPawn())
                {
                    return PC;
                }
            }
        }

        return nullptr;
    }

    // Substellar direction (planet -> star), unit length.
    static FVector ZephyrSubstellarUp(
        const FZephyrPlanetSnapshotEntry& Entry,
        const FVector& StarWorld)
    {
        return (StarWorld - Entry.PlanetCenter).GetSafeNormal(1e-6);
    }

    // Horizontal look rotation at bearing YawDeg around Up.
    static FRotator ZephyrHorizontalLook(
        const FVector& Up, float YawDeg, float PitchDeg = 0.0f)
    {
        const FVector RefAxis =
            (FMath::Abs(Up.Z) < 0.99f)
                ? FVector(0, 0, 1)
                : FVector(1, 0, 0);
        const FVector H0 =
            FVector::CrossProduct(Up, RefAxis).GetSafeNormal(1e-6);
        const FVector H1 =
            FVector::CrossProduct(Up, H0).GetSafeNormal(1e-6);
        const float YawRad = FMath::DegreesToRadians(YawDeg);
        const FVector Forward =
            H0 * FMath::Cos(YawRad) + H1 * FMath::Sin(YawRad);

        return FRotator(
            PitchDeg,
            FMath::RadiansToDegrees(
                FMath::Atan2(Forward.Y, Forward.X)),
            0.0f);
    }

    // Tripod freeze (diagnostic): stop the pawn's movement
    // simulation after a teleport so gravity/fall cannot drag the
    // camera into the dirt during startup hitches. Position glue
    // comes from tracking re-teleports; reactivation happens at
    // capture completion. No-op when nothing is possessed.
    // NOTE: this must NOT pause the world (PlayerCameraManager
    // stops updating while paused, freezing the camera at its
    // pre-teleport pose). Pausing happens at settle completion.
    static void ZephyrSetCapturePause(bool bPause);
    static void ZephyrFreezePawnMotion()
    {
        APlayerController* PC = ZephyrGetPlayerController();

        if (!PC || !PC->GetPawn())
        {
            return;
        }

        if (UPawnMovementComponent* MC = PC->GetPawn()->GetMovementComponent())
        {
            MC->Deactivate();
        }
    }

    static void ZephyrUnfreezePawnMotion()
    {
        APlayerController* PC = ZephyrGetPlayerController();

        if (!PC || !PC->GetPawn())
        {
            return;
        }

        if (UPawnMovementComponent* MC = PC->GetPawn()->GetMovementComponent())
        {
            if (!MC->IsActive())
            {
                MC->Activate();
            }
        }
    }

    // World pause for capture stability (diagnostic): freezing the
    // world stops orbital view-key churn (per-frame LUT rebakes
    // end), lets the GPU queue drain so presents go live, and pins
    // the pawn (no gravity fall during backend stalls). The core
    // ticker, render thread and screenshot backend keep running
    // while paused. Set on first successful teleport, cleared at
    // completion. Instant captures never pause.
    static void ZephyrSetCapturePause(bool bPause)
    {
        APlayerController* PC = ZephyrGetPlayerController();

        if (!PC)
        {
            return;
        }

        UWorld* World = PC->GetWorld();

        if (!World)
        {
            return;
        }

        UGameplayStatics::SetGamePaused(World, bPause);
        GZephyrCapture.bPausedForCapture = bPause;
    }

    // Teleports the possessed pawn to 50% shell height above the
    // substellar point of a published planet and faces it
    // horizontally. Returns false when planets/pawn are missing.
    static bool ZephyrTeleportPawnToPlanet(
        int32 PlanetIndex, float YawDeg, float PitchDeg = 0.0f, float Alt01 = 0.5f)
    {
        TArray<FZephyrPlanetSnapshotEntry> Planets;
        FVector StarWorld = FVector::ZeroVector;

        if (!ZephyrFetchSnapshot(Planets, StarWorld))
        {
            return false;
        }

        const int32 ClampedIndex = FMath::Clamp(
            PlanetIndex, 0, Planets.Num() - 1);
        const FZephyrPlanetSnapshotEntry& Entry =
            Planets[ClampedIndex];

        APlayerController* PC = ZephyrGetPlayerController();

        if (!PC)
        {
            return false;
        }

        // Substellar point at Alt01 shell fraction height (0.5
        // clears analytic terrain since R > 9t holds; use higher
        // fractions where the mesh locally exceeds the analytic
        // TerrainHeight).
        const FVector Up = ZephyrSubstellarUp(Entry, StarWorld);
        const float ShellCm =
            Entry.Profile.AtmosphereRadius
            - Entry.Profile.GroundRadius;
        const float ClampedAlt =
            FMath::Clamp(Alt01, 0.05f, 0.98f);
        const FVector TargetPos =
            Entry.PlanetCenter
            + Up * (Entry.Profile.GroundRadius + ShellCm * ClampedAlt);

        // Horizontal bearing (sun at zenith here: yaw selects
        // the horizontal look direction).
        const FRotator LookRot =
            ZephyrHorizontalLook(Up, YawDeg, PitchDeg);

        APawn* Pawn = PC->GetPawn();
        const bool bTeleported = Pawn->TeleportTo(
            TargetPos, LookRot, false, true);
        PC->SetControlRotation(LookRot);

        // NOTE: no world pause here. A previous revision paused
        // for capture stability, but pause may freeze frame
        // presentation in unattended runs (stale-frame captures).
        // Tracking re-teleports (see ticker) keep the pawn glued
        // instead. Use the `pause` console command manually when
        // inspecting interactively.
        const bool bPaused = false;

        // Overlap audit: with toy-planet shells this large the
        // camera can sit inside SEVERAL shells at once. Log every
        // containing shell so the governing-slice choice (and any
        // bright low-altitude neighbor contribution) is visible.
        // The teleported planet's full physical profile is logged
        // too (ground truth for hand-checking LUT magnitudes).
        for (int32 PlanetIdx = 0; PlanetIdx < Planets.Num(); ++PlanetIdx)
        {
            const FZephyrPlanetSnapshotEntry& Other =
                Planets[PlanetIdx];
            const double DistToOther =
                FVector::Dist(TargetPos, Other.PlanetCenter);
            const double ShellOther =
                static_cast<double>(Other.Profile.AtmosphereRadius)
                - static_cast<double>(Other.Profile.GroundRadius);
            const double HeightOther =
                DistToOther
                - static_cast<double>(Other.Profile.GroundRadius);

            UE_LOG(
                LogAndromedaZephyr,
                Log,
                TEXT("[ZEPHYR-01] SurfaceShot shell audit: planet %d dist=%.0f Rt=%.0f h01=%.3f %s"),
                PlanetIdx,
                DistToOther,
                static_cast<double>(Other.Profile.AtmosphereRadius),
                ShellOther > 0.0
                    ? HeightOther / ShellOther
                    : -1.0,
                (DistToOther < static_cast<double>(
                    Other.Profile.AtmosphereRadius))
                    ? TEXT("INSIDE")
                    : TEXT("outside")
            );
        }

        {
            const FZephyrPlanetProfile& Prof =
                Planets[FMath::Clamp(
                    PlanetIndex, 0, Planets.Num() - 1)].Profile;

            UE_LOG(
                LogAndromedaZephyr,
                Log,
                TEXT("[ZEPHYR-01] SurfaceShot profile: Rg=%.0f Rt=%.0f Ray=(%.5f,%.5f,%.5f)/km H_R=%.0fcm Mie=(%.5f,%.5f,%.5f)/km H_M=%.0fcm g=%.3f Abs=(%.6f,%.6f,%.6f)/km AbsH=%.0fcm AbsW=%.0fcm Albedo=%.3f Dens=%.3f"),
                Prof.GroundRadius,
                Prof.AtmosphereRadius,
                Prof.RayleighScattering.X,
                Prof.RayleighScattering.Y,
                Prof.RayleighScattering.Z,
                Prof.RayleighScaleHeight,
                Prof.MieScattering.X,
                Prof.MieScattering.Y,
                Prof.MieScattering.Z,
                Prof.MieScaleHeight,
                Prof.MieAnisotropy,
                Prof.AbsorptionCoefficients.X,
                Prof.AbsorptionCoefficients.Y,
                Prof.AbsorptionCoefficients.Z,
                Prof.AbsorptionLayerHeight,
                Prof.AbsorptionLayerWidth,
                Prof.GroundAlbedo,
                Prof.AtmosphericDensityScale
            );
        }

        // Belt and suspenders: even with pause, re-apply the
        // teleport every ticker fire (tracking) so a planet
        // that keeps moving cannot leave the pawn behind.
        GZephyrCapture.TrackingPlanet = ClampedIndex;
        GZephyrCapture.TrackingYawDeg = YawDeg;
        GZephyrCapture.TrackingPitchDeg = PitchDeg;
        GZephyrCapture.TrackingRadii = 0.0f;
        GZephyrCapture.bTrackingField = false;
        GZephyrCapture.TrackingAlt01 = ClampedAlt;

        UE_LOG(
            LogAndromedaZephyr,
            Log,
            TEXT("[ZEPHYR-01] SurfaceShot: teleport=%d pause=%d pawn=(%.0f,%.0f,%.0f) planet=%d (%.0f cm above ground, alt01=%.2f)."),
            bTeleported ? 1 : 0,
            bPaused ? 1 : 0,
            TargetPos.X, TargetPos.Y, TargetPos.Z,
            ClampedIndex,
            static_cast<double>(ShellCm * ClampedAlt),
            static_cast<double>(ClampedAlt)
        );

        return true;
    }

    // Teleports the possessed pawn to Radii x atmosphere outer
    // radius above the substellar point of a published planet,
    // facing the planet center (fully lit disk + limb all
    // around). Used by OrbitShot for near-planet inspection
    // (sphere hunt, TEST H). Returns false when missing.
    static bool ZephyrTeleportPawnToOrbit(
        int32 PlanetIndex, float Radii)
    {
        TArray<FZephyrPlanetSnapshotEntry> Planets;
        FVector StarWorld = FVector::ZeroVector;

        if (!ZephyrFetchSnapshot(Planets, StarWorld))
        {
            return false;
        }

        const int32 ClampedIndex = FMath::Clamp(
            PlanetIndex, 0, Planets.Num() - 1);
        const FZephyrPlanetSnapshotEntry& Entry =
            Planets[ClampedIndex];

        APlayerController* PC = ZephyrGetPlayerController();

        if (!PC)
        {
            return false;
        }

        const FVector Up = ZephyrSubstellarUp(Entry, StarWorld);
        const FVector TargetPos =
            Entry.PlanetCenter
            + Up * (Entry.Profile.AtmosphereRadius
                * FMath::Max(Radii, 1.05f));

        // Face the planet center.
        const FVector ToCenter = (-Up).GetSafeNormal(1e-6);
        const FRotator LookRot(
            0.0f,
            FMath::RadiansToDegrees(
                FMath::Atan2(ToCenter.Y, ToCenter.X)),
            0.0f);

        APawn* Pawn = PC->GetPawn();
        const bool bTeleported = Pawn->TeleportTo(
            TargetPos, LookRot, false, true);
        PC->SetControlRotation(LookRot);

        GZephyrCapture.TrackingPlanet = ClampedIndex;
        GZephyrCapture.TrackingYawDeg = 0.0f;
        GZephyrCapture.TrackingRadii =
            FMath::Max(Radii, 1.05f);

        UE_LOG(
            LogAndromedaZephyr,
            Log,
            TEXT("[ZEPHYR-01] OrbitShot: teleport=%d pawn=(%.0f,%.0f,%.0f) planet=%d radii=%.2f."),
            bTeleported ? 1 : 0,
            TargetPos.X, TargetPos.Y, TargetPos.Z,
            ClampedIndex,
            static_cast<double>(GZephyrCapture.TrackingRadii)
        );

        return true;
    }

    // Teleports the possessed pawn to 50% shell height above the
    // surface point selected by Side (0 = substellar/day,
    // 1 = terminator, 2 = antistellar/night) of a published
    // planet, facing it horizontally at bearing YawDeg with
    // PitchDeg elevation. Day/night task validation: dawn and
    // dusk are the same model geometry mirrored (no rotation
    // terms exist in the bake), so one terminator side covers
    // both. Deliberately does NOT arm tracking: the pawn rides
    // the planet frame (same assumption as SurfaceShot), and
    // arming TrackingPlanet would yank it back to substellar
    // via ZephyrTeleportPawnToPlanet. Returns false when
    // planets/pawn are missing.
    static bool ZephyrTeleportPawnToField(
        int32 PlanetIndex, int32 Side, float YawDeg, float PitchDeg, float Alt01 = 0.5f)
    {
        TArray<FZephyrPlanetSnapshotEntry> Planets;
        FVector StarWorld = FVector::ZeroVector;

        if (!ZephyrFetchSnapshot(Planets, StarWorld))
        {
            return false;
        }

        const int32 ClampedIndex = FMath::Clamp(
            PlanetIndex, 0, Planets.Num() - 1);
        const FZephyrPlanetSnapshotEntry& Entry =
            Planets[ClampedIndex];

        APlayerController* PC = ZephyrGetPlayerController();

        if (!PC)
        {
            return false;
        }

        const FVector SubUp = ZephyrSubstellarUp(Entry, StarWorld);

        FVector FieldUp = SubUp;

        if (Side == 2)
        {
            FieldUp = -SubUp;
        }
        else if (Side == 1)
        {
            const FVector RefAxis =
                (FMath::Abs(SubUp.Z) < 0.99f)
                    ? FVector(0, 0, 1)
                    : FVector(1, 0, 0);
            FieldUp = FVector::CrossProduct(SubUp, RefAxis)
                .GetSafeNormal(1e-6);
        }

        const float ShellCm =
            Entry.Profile.AtmosphereRadius
            - Entry.Profile.GroundRadius;
        const float ClampedAlt =
            FMath::Clamp(Alt01, 0.05f, 0.98f);
        const FVector TargetPos =
            Entry.PlanetCenter
            + FieldUp * (Entry.Profile.GroundRadius + ShellCm * ClampedAlt);

        const FRotator LookRot =
            ZephyrHorizontalLook(FieldUp, YawDeg, PitchDeg);

        APawn* Pawn = PC->GetPawn();
        const bool bTeleported = Pawn->TeleportTo(
            TargetPos, LookRot, false, true);
        PC->SetControlRotation(LookRot);

        for (int32 PlanetIdx = 0; PlanetIdx < Planets.Num(); ++PlanetIdx)
        {
            const FZephyrPlanetSnapshotEntry& Other =
                Planets[PlanetIdx];
            const double DistToOther =
                FVector::Dist(TargetPos, Other.PlanetCenter);

            UE_LOG(
                LogAndromedaZephyr,
                Log,
                TEXT("[ZEPHYR-01] FieldShot shell audit: planet %d dist=%.0f Rt=%.0f %s"),
                PlanetIdx,
                DistToOther,
                static_cast<double>(Other.Profile.AtmosphereRadius),
                (DistToOther < static_cast<double>(
                    Other.Profile.AtmosphereRadius))
                    ? TEXT("INSIDE")
                    : TEXT("outside")
            );
        }

        UE_LOG(
            LogAndromedaZephyr,
            Log,
            TEXT("[ZEPHYR-01] FieldShot: teleport=%d pause=0 pawn=(%.0f,%.0f,%.0f) planet=%d side=%d (%.0f cm above ground, alt01=%.2f)."),
            bTeleported ? 1 : 0,
            TargetPos.X, TargetPos.Y, TargetPos.Z,
            ClampedIndex,
            Side,
            static_cast<double>(ShellCm * ClampedAlt),
            static_cast<double>(ClampedAlt)
        );

        return bTeleported;
    }

    static void ZephyrScheduleCapture(const TArray<FString>& Args)
    {
        if (GZephyrCapture.bActive)
        {
            UE_LOG(
                LogAndromedaZephyr,
                Warning,
                TEXT("[ZEPHYR-01] Capture already in progress.")
            );
            return;
        }

        float DelaySeconds = 60.0f;

        if (Args.Num() > 0)
        {
            DelaySeconds = FMath::Max(
                0.0f, FCString::Atof(*Args[0]));
        }

        // Optional single-mode capture:
        //   CaptureViews 45 7  -> only SKY_VIEW_LUT.
        int32 FirstMode = 0;
        int32 LastMode = 7;

        if (Args.Num() > 1)
        {
            FirstMode = FMath::Clamp(
                FCString::Atoi(*Args[1]), 0, 8);
            LastMode = FirstMode;
        }

        GZephyrCapture.bActive = true;
        GZephyrCapture.WaitRemaining = DelaySeconds;
        GZephyrCapture.CaptureCooldown = 0.0f;
        GZephyrCapture.NextMode = FirstMode;
        GZephyrCapture.LastMode = LastMode;
        // Fresh settle/pending state for every run (commands arm
        // their own pending/teleport/tracking after this call).
        GZephyrCapture.Phase = 0;
        GZephyrCapture.PhaseTime = 0.0f;
        GZephyrCapture.SettleNeed = 2.5f;
        GZephyrCapture.PendingTeleportPlanet = -1;
        GZephyrCapture.PendingFieldPlanet = -1;
        GZephyrCapture.TrackingPlanet = -1;
        GZephyrCapture.TrackingRadii = 0.0f;
        GZephyrCapture.bInstantCapture = false;
        GZephyrCapture.bFenceDone = false;
        GZephyrCapture.bTrackingField = false;
        GZephyrCapture.TrackingFieldSide = 0;
        GZephyrCapture.bHasGluePos = false;
        GZephyrCapture.PendingAlt01 = 0.5f;
        GZephyrCapture.TrackingAlt01 = 0.5f;
        GZephyrCapture.bPausedForCapture = false;
        GZephyrCapture.DebugCVar =
            IConsoleManager::Get().FindConsoleVariable(
                TEXT("r.AndromedaZephyr.DebugMode"));

        // Absolute path: ProjectSavedDir can arrive in a
        // relative form depending on launch context; the
        // screenshot backend requires a full path.
        GZephyrCapture.OutputDir =
            FPaths::ConvertRelativePathToFull(FPaths::Combine(
                FPaths::ProjectSavedDir(),
                TEXT("Screenshots"),
                TEXT("Zephyr")
            ));

        IPlatformFile& PlatformFile =
            FPlatformFileManager::Get().GetPlatformFile();
        PlatformFile.CreateDirectoryTree(
            *GZephyrCapture.OutputDir);

        UE_LOG(
            LogAndromedaZephyr,
            Log,
            TEXT("[ZEPHYR-01] Capture scheduled in %.0f s -> %s"),
            DelaySeconds,
            *GZephyrCapture.OutputDir
        );

        FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda(
                [](float DeltaTime)
                {
                    if (!GZephyrCapture.bActive)
                    {
                        return false;
                    }

                    // Adaptive gate: start capturing as soon as
                    // planets exist (snapshot published); the
                    // delay is only a fallback start for
                    // planet-less maps (plus a 300 s hang
                    // guard). This keeps headless runs
                    // independent of boot/DDC speed.
                    static float Elapsed = 0.0f;

                    if (GZephyrCapture.WaitRemaining > 0.0f)
                    {
                        GZephyrCapture.WaitRemaining -= DeltaTime;
                        Elapsed += DeltaTime;

                        const int32 PlanetsReady =
                            FAndromedaAtmosphereSystem::Get().GetPlanetCount();

                        if (PlanetsReady <= 0
                            && GZephyrCapture.WaitRemaining > 0.0f
                            && Elapsed < 300.0f)
                        {
                            return true;
                        }

                        UE_LOG(
                            LogAndromedaZephyr,
                            Log,
                            TEXT("[ZEPHYR-01] Capture starting (planets: %d, waited %.0f s)."),
                            PlanetsReady,
                            Elapsed
                        );

                        Elapsed = 0.0f;
                        GZephyrCapture.WaitRemaining = 0.0f;

                        // Deferred SurfaceShot teleport: runs here
                        // (planets exist now), then holds 0.5 s so the
                        // Sky-View LUT re-bakes at the new camera
                        // height before the capture. Kept short on
                        // purpose: close-orbit planets move several
                        // km/s, so the capture must follow the
                        // teleport within a fraction of a second
                        // even if frame inheritance lags.
                        if (GZephyrCapture.PendingTeleportPlanet >= 0)
                        {
                            const bool bTeleported =
                                ZephyrTeleportPawnToPlanet(
                                    GZephyrCapture.PendingTeleportPlanet,
                                    GZephyrCapture.PendingYawDeg,
                                    GZephyrCapture.PendingPitchDeg,
                                    GZephyrCapture.PendingAlt01);

                            if (bTeleported)
                            {
                                if (GZephyrCapture.bInstantCapture)
                                {
                                    // Same-tick capture: set the mode
                                    // and queue the screenshot NOW, then
                                    // mark the sequence complete. No
                                    // pause, no tracking, no cooldown.
                                    const int32 Mode =
                                        GZephyrCapture.NextMode;

                                    if (GZephyrCapture.DebugCVar)
                                    {
                                        GZephyrCapture.DebugCVar->Set(
                                            Mode, ECVF_SetByConsole);
                                    }

                                    const FString Filename =
                                        FPaths::Combine(
                                            GZephyrCapture.OutputDir,
                                            FString::Printf(
                                                TEXT("Zephyr_Mode%d"),
                                                Mode));

                                    FScreenshotRequest::RequestScreenshot(
                                        Filename, false, true);

                                    UE_LOG(
                                        LogAndromedaZephyr,
                                        Log,
                                        TEXT("[ZEPHYR-01] Instant capture queued (mode %d)."),
                                        Mode
                                    );

                                    GZephyrCapture.NextMode =
                                        GZephyrCapture.LastMode + 1;
                                    GZephyrCapture.WaitRemaining = 0.0f;
                                    return true;
                                }

                                // Consume only on success (a failed
                                // teleport is retried post-gate, so
                                // captures never proceed from an
                                // un-teleported viewpoint).
                                GZephyrCapture.PendingTeleportPlanet = -1;
                                ZephyrFreezePawnMotion();
                                GZephyrCapture.Phase = 1;
                                GZephyrCapture.PhaseTime = 0.0f;
                                GZephyrCapture.SettleNeed = 2.5f;
                            }
                            else
                            {
                                // Pawn usually not possessed yet this
                                // early; the post-gate retry below
                                // re-attempts every fire.
                                GZephyrCapture.PhaseTime = 0.0f;
                            }
                        }

                        // Deferred FieldShot teleport (day/night
                        // task): planet-centered side positioning
                        // (0 day, 1 terminator, 2 night). Field-side
                        // tracking glue is armed by the command, so
                        // the moving spot survives hitches and drift
                        // (never falls back to substellar).
                        if (GZephyrCapture.PendingFieldPlanet >= 0)
                        {
                            const bool bFieldTeleported =
                                ZephyrTeleportPawnToField(
                                    GZephyrCapture.PendingFieldPlanet,
                                    GZephyrCapture.PendingFieldSide,
                                    GZephyrCapture.PendingFieldYawDeg,
                                    GZephyrCapture.PendingFieldPitchDeg,
                                    GZephyrCapture.PendingAlt01);

                            if (bFieldTeleported)
                            {
                                GZephyrCapture.PendingFieldPlanet = -1;
                                ZephyrFreezePawnMotion();
                                GZephyrCapture.Phase = 1;
                                GZephyrCapture.PhaseTime = 0.0f;
                                GZephyrCapture.SettleNeed = 2.5f;
                            }
                            else
                            {
                                GZephyrCapture.PhaseTime = 0.0f;
                            }
                        }

                        return true;
                    }

                    // Retry teleports that failed at gate time
                    // (pawn usually not possessed yet): re-attempt
                    // every fire until one succeeds or 30 s elapse.
                    // Captures must never proceed from an
                    // un-teleported viewpoint.
                    if (GZephyrCapture.Phase == 0
                        && (GZephyrCapture.PendingTeleportPlanet >= 0
                            || GZephyrCapture.PendingFieldPlanet >= 0))
                    {
                        GZephyrCapture.PhaseTime += DeltaTime;

                        if (GZephyrCapture.PendingTeleportPlanet >= 0
                            && ZephyrTeleportPawnToPlanet(
                                GZephyrCapture.PendingTeleportPlanet,
                                GZephyrCapture.PendingYawDeg,
                                GZephyrCapture.PendingPitchDeg,
                                GZephyrCapture.PendingAlt01))
                        {
                            GZephyrCapture.PendingTeleportPlanet = -1;
                            ZephyrFreezePawnMotion();
                            GZephyrCapture.Phase = 1;
                            GZephyrCapture.PhaseTime = 0.0f;
                            GZephyrCapture.SettleNeed = 2.5f;
                        }
                        else if (GZephyrCapture.PendingFieldPlanet >= 0
                            && ZephyrTeleportPawnToField(
                                GZephyrCapture.PendingFieldPlanet,
                                GZephyrCapture.PendingFieldSide,
                                GZephyrCapture.PendingFieldYawDeg,
                                GZephyrCapture.PendingFieldPitchDeg,
                                GZephyrCapture.PendingAlt01))
                        {
                            GZephyrCapture.PendingFieldPlanet = -1;
                            ZephyrFreezePawnMotion();
                            GZephyrCapture.Phase = 1;
                            GZephyrCapture.PhaseTime = 0.0f;
                            GZephyrCapture.SettleNeed = 2.5f;
                        }
                        else if (GZephyrCapture.PhaseTime > 30.0f)
                        {
                            UE_LOG(
                                LogAndromedaZephyr,
                                Warning,
                                TEXT("[ZEPHYR-01] Teleport retry timed out; capturing from the current viewpoint.")
                            );

                            GZephyrCapture.PendingTeleportPlanet = -1;
                            GZephyrCapture.PendingFieldPlanet = -1;
                            GZephyrCapture.Phase = 1;
                            GZephyrCapture.PhaseTime = 0.0f;
                            GZephyrCapture.SettleNeed = 2.5f;
                        }

                        return true;
                    }

                    // Tracking glue (SurfaceShot/OrbitShot/FieldShot):
                    // keep the pawn on the moving target on every
                    // post-gate fire (settle + request + await).
                    // FieldShot deliberately uses field-side glue
                    // (never substellar). A macro-jump (post-hitch
                    // recovery) resets the live-frame settle so the
                    // request below never captures a predating view.
                    if (GZephyrCapture.TrackingPlanet >= 0)
                    {
                        APlayerController* GluePC =
                            ZephyrGetPlayerController();
                        const FVector PreGluePos =
                            (GluePC && GluePC->GetPawn())
                                ? GluePC->GetPawn()->GetActorLocation()
                                : FVector::ZeroVector;
                        const bool bHadGlue =
                            GZephyrCapture.bHasGluePos
                            && GluePC && GluePC->GetPawn();

                        if (GZephyrCapture.TrackingRadii > 0.0f)
                        {
                            ZephyrTeleportPawnToOrbit(
                                GZephyrCapture.TrackingPlanet,
                                GZephyrCapture.TrackingRadii);
                        }
                        else if (GZephyrCapture.bTrackingField)
                        {
                            ZephyrTeleportPawnToField(
                                GZephyrCapture.TrackingPlanet,
                                GZephyrCapture.TrackingFieldSide,
                                GZephyrCapture.TrackingYawDeg,
                                GZephyrCapture.TrackingPitchDeg,
                                GZephyrCapture.TrackingAlt01);
                        }
                        else
                        {
                            ZephyrTeleportPawnToPlanet(
                                GZephyrCapture.TrackingPlanet,
                                GZephyrCapture.TrackingYawDeg,
                                GZephyrCapture.TrackingPitchDeg,
                                GZephyrCapture.TrackingAlt01);
                        }

                        // Tripod: re-freeze after every glue snap so
                        // fall velocity never accumulates (idempotent).
                        ZephyrFreezePawnMotion();

                        if (GluePC && GluePC->GetPawn())
                        {
                            const FVector PostGluePos =
                                GluePC->GetPawn()->GetActorLocation();
                            GZephyrCapture.LastGluePos = PostGluePos;
                            GZephyrCapture.bHasGluePos = true;

                            if (bHadGlue
                                && FVector::Dist(
                                    PreGluePos, PostGluePos) > 200000.0)
                            {
                                GZephyrCapture.Phase = 1;
                                GZephyrCapture.PhaseTime = 0.0f;
                                GZephyrCapture.SettleNeed = 2.5f;

                                UE_LOG(
                                    LogAndromedaZephyr,
                                    Log,
                                    TEXT("[ZEPHYR-01] Glue jump: re-settling captures (pawn moved %.0f cm)."),
                                    FVector::Dist(PreGluePos, PostGluePos)
                                );
                            }
                        }
                    }

                    if (GZephyrCapture.Phase == 0)
                    {
                        // Instant-capture leftovers finish here;
                        // anything else starts settling.
                        if (GZephyrCapture.NextMode <= GZephyrCapture.LastMode)
                        {
                            GZephyrCapture.Phase = 1;
                            GZephyrCapture.PhaseTime = 0.0f;
                            GZephyrCapture.SettleNeed = 2.5f;
                        }
                    }

                    if (GZephyrCapture.Phase == 1)
                    {
                        // Settle: let the render thread present
                        // live frames of the new viewpoint/mode
                        // before the screenshot backend grabs the
                        // backbuffer (stale-frame guard: a request
                        // in the same fire as the state change
                        // captures a predating backbuffer).
                        // The world stays UNPAUSED throughout the
                        // settle so the camera keeps following the
                        // pawn (PlayerCameraManager freezes while
                        // paused).
                        GZephyrCapture.PhaseTime += DeltaTime;

                        if (GZephyrCapture.PhaseTime < GZephyrCapture.SettleNeed)
                        {
                            return true;
                        }

                        // Settle complete: freeze the world for the
                        // capture (orbits stop churning view keys so
                        // LUT rebakes end, GPU queue drains, pawn
                        // pinned). Camera already glued; core ticker,
                        // render and screenshots keep running.
                        if (!GZephyrCapture.bPausedForCapture)
                        {
                            ZephyrSetCapturePause(true);
                        }

                        GZephyrCapture.Phase = 2;
                    }

                    // Phase 2: request (then await) one mode at a
                    // time; finish when modes are exhausted.
                    if (GZephyrCapture.NextMode > GZephyrCapture.LastMode)
                    {
                        // Do not restore mode 0 while the last
                        // screenshot is still queued: it would
                        // capture the restored mode under the
                        // previous mode's filename.
                        if (FScreenshotRequest::IsScreenshotRequested())
                        {
                            return true;
                        }

                        if (GZephyrCapture.DebugCVar)
                        {
                            GZephyrCapture.DebugCVar->Set(
                                0, ECVF_SetByConsole);
                        }
                        GZephyrCapture.bActive = false;
                        GZephyrCapture.TrackingPlanet = -1;
                        ZephyrUnfreezePawnMotion();

                        if (GZephyrCapture.bPausedForCapture)
                        {
                            ZephyrSetCapturePause(false);
                        }

                        UE_LOG(
                            LogAndromedaZephyr,
                            Log,
                            TEXT("[ZEPHYR-01] Capture complete, debug mode restored to 0.")
                        );

                        return false;
                    }

                    // One screenshot at a time: check BEFORE
                    // switching modes. If the previous request is
                    // still queued (low fps, streaming hitches),
                    // hold the current mode; switching first and
                    // checking after would mislabel the capture.
                    if (FScreenshotRequest::IsScreenshotRequested())
                    {
                        return true;
                    }

                    const int32 Mode = GZephyrCapture.NextMode;

                    if (GZephyrCapture.DebugCVar)
                    {
                        GZephyrCapture.DebugCVar->Set(
                            Mode, ECVF_SetByConsole);
                    }

                    const FString Filename = FPaths::Combine(
                        GZephyrCapture.OutputDir,
                        FString::Printf(
                            TEXT("Zephyr_Mode%d"), Mode)
                    );

                    // Render-backlog fence (once per run) BEFORE the
                    // first request: drain the render queue and
                    // measure its depth. A flooded GPU queue would
                    // otherwise present minutes-old graphs and the
                    // screenshot below would capture a stale
                    // backbuffer predating the teleport/mode. Full
                    // pose ground truth is logged here too (pawn vs
                    // actual render camera vs star).
                    if (!GZephyrCapture.bFenceDone)
                    {
                        GZephyrCapture.bFenceDone = true;

                        const double FenceStartSeconds =
                            FPlatformTime::Seconds();
                        FRenderCommandFence DrainFence;
                        DrainFence.BeginFence();
                        DrainFence.Wait();
                        const double FenceWaitMs =
                            (FPlatformTime::Seconds() - FenceStartSeconds)
                            * 1000.0;

                        APlayerController* PosePC =
                            ZephyrGetPlayerController();
                        FVector PosePawn = FVector::ZeroVector;
                        FVector PoseCam = FVector::ZeroVector;
                        FRotator PoseControlRot = FRotator::ZeroRotator;
                        FRotator PoseCamRot = FRotator::ZeroRotator;
                        float PoseFov = 0.0f;

                        if (PosePC)
                        {
                            if (PosePC->GetPawn())
                            {
                                PosePawn =
                                    PosePC->GetPawn()->GetActorLocation();
                            }

                            PoseControlRot = PosePC->GetControlRotation();

                            if (PosePC->PlayerCameraManager)
                            {
                                PoseCam = PosePC->PlayerCameraManager
                                    ->GetCameraLocation();
                                PoseCamRot = PosePC->PlayerCameraManager
                                    ->GetCameraRotation();
                                PoseFov = PosePC->PlayerCameraManager
                                    ->GetFOVAngle();
                            }
                        }

                        TArray<FZephyrPlanetSnapshotEntry> PosePlanets;
                        FVector PoseStar = FVector::ZeroVector;
                        ZephyrFetchSnapshot(PosePlanets, PoseStar);

                        int32 PoseGov = -1;
                        float PoseGovRt = 1e30f;

                        for (int32 PSel = 0;
                            PSel < PosePlanets.Num();
                            ++PSel)
                        {
                            const float PRt =
                                PosePlanets[PSel].Profile.AtmosphereRadius;
                            const float PRg =
                                PosePlanets[PSel].Profile.GroundRadius;
                            const double PDist = FVector::Dist(
                                PoseCam,
                                PosePlanets[PSel].PlanetCenter);

                            if (PDist >= static_cast<double>(PRg)
                                    * (1.0 - 1e-4)
                                && PDist <= static_cast<double>(PRt)
                                && PRt < PoseGovRt)
                            {
                                PoseGovRt = PRt;
                                PoseGov = PSel;
                            }
                        }

                        FVector PoseUp = FVector::ZeroVector;

                        if (PoseGov >= 0)
                        {
                            PoseUp = ZephyrSubstellarUp(
                                PosePlanets[PoseGov], PoseStar);
                        }

                        UE_LOG(
                            LogAndromedaZephyr,
                            Log,
                            TEXT("[ZEPHYR-01] Capture pose: fence=%.0fms pawn=(%.0f,%.0f,%.0f) cam=(%.0f,%.0f,%.0f) camrot=(%.1f,%.1f,%.1f) ctrlrot=(%.1f,%.1f,%.1f) fov=%.1f star=(%.0f,%.0f,%.0f) gov=%d subUp=(%.3f,%.3f,%.3f)."),
                            FenceWaitMs,
                            PosePawn.X, PosePawn.Y, PosePawn.Z,
                            PoseCam.X, PoseCam.Y, PoseCam.Z,
                            PoseCamRot.Pitch, PoseCamRot.Yaw, PoseCamRot.Roll,
                            PoseControlRot.Pitch, PoseControlRot.Yaw, PoseControlRot.Roll,
                            PoseFov,
                            PoseStar.X, PoseStar.Y, PoseStar.Z,
                            PoseGov,
                            PoseUp.X, PoseUp.Y, PoseUp.Z
                        );
                    }

                    // The screenshot is queued for a frame
                    // AFTER this tick, so it captures the
                    // mode set just above (already settled on
                    // screen by the Phase 1 wait, and the fence
                    // above just drained any render backlog).
                    FScreenshotRequest::RequestScreenshot(
                        Filename, false, true);

                    UE_LOG(
                        LogAndromedaZephyr,
                        Log,
                        TEXT("[ZEPHYR-01] Capturing debug mode %d."),
                        Mode
                    );

                    // Traceability: log the pawn position that the
                    // captured frame was requested from (screenshot
                    // backend captures a later presented frame).
                    {
                        APlayerController* CapPC =
                            ZephyrGetPlayerController();
                        const FVector CapPawnPos =
                            (CapPC && CapPC->GetPawn())
                                ? CapPC->GetPawn()->GetActorLocation()
                                : FVector::ZeroVector;
                        UE_LOG(
                            LogAndromedaZephyr,
                            Log,
                            TEXT("[ZEPHYR-01] Capture viewpoint pawn=(%.0f,%.0f,%.0f)."),
                            CapPawnPos.X, CapPawnPos.Y, CapPawnPos.Z
                        );
                    }

                    ++GZephyrCapture.NextMode;

                    // Brief re-settle between modes so every
                    // capture resolves on frames carrying its own
                    // mode (LUTs do not rebake between modes, so
                    // 0.6 s suffices).
                    GZephyrCapture.Phase = 1;
                    GZephyrCapture.PhaseTime = 0.0f;
                    GZephyrCapture.SettleNeed = 0.6f;

                    return true;
                }),
            0.25f
        );
    }

    FAutoConsoleCommand GZephyrCaptureCommand(
        TEXT("r.AndromedaZephyr.CaptureViews"),
        TEXT("Capture one screenshot per ZEPHYR debug mode after <DelaySeconds> (usage: r.AndromedaZephyr.CaptureViews 60 [SingleMode]). Output: [Project]/Saved/Screenshots/Zephyr/."),
        FConsoleCommandWithArgsDelegate::CreateLambda(
            [](const TArray<FString>& Args)
            {
                ZephyrScheduleCapture(Args);
            })
    );

    // r.AndromedaZephyr.SurfaceShot <PlanetIndex> [YawDeg] [Mode]
    //
    // Day-side in-atmosphere validation: waits for planets,
    // teleports the player pawn to 50% shell height above the
    // substellar point of planet <PlanetIndex>, faces it
    // horizontally at bearing [YawDeg], then captures sky in
    // debug [Mode] (default 0 = full radiance). Inside the
    // gravity influence the pawn inherits the planet frame, so
    // it rides the planet during the capture. The Sky-View LUT
    // re-bakes automatically at the new camera height
    // (view-key invalidation).
    FAutoConsoleCommand GZephyrSurfaceShotCommand(
        TEXT("r.AndromedaZephyr.SurfaceShot"),
        TEXT("Teleport the player pawn into a planet atmosphere (day side, 50% shell) and capture the sky (usage: r.AndromedaZephyr.SurfaceShot 0 [YawDeg] [Mode])."),
        FConsoleCommandWithArgsDelegate::CreateLambda(
            [](const TArray<FString>& Args)
            {
                if (GZephyrCapture.bActive)
                {
                    UE_LOG(
                        LogAndromedaZephyr,
                        Warning,
                        TEXT("[ZEPHYR-01] Capture already in progress.")
                    );
                    return;
                }

                const int32 PlanetIndex = (Args.Num() > 0)
                    ? FCString::Atoi(*Args[0])
                    : 0;

                const float YawDeg = (Args.Num() > 1)
                    ? static_cast<float>(FCString::Atof(*Args[1]))
                    : 90.0f;

                const int32 CaptureMode = (Args.Num() > 2)
                    ? FMath::Clamp(
                        FCString::Atoi(*Args[2]), 0, 8)
                    : 0;

                // 4th arg = 1: instant same-tick capture (no pause,
                // no cooldown). Diagnostic for view-following.
                const bool bInstant = (Args.Num() > 3)
                    && (FCString::Atoi(*Args[3]) != 0);

                // 5th arg: pitch degrees override (default 0 =
                // horizontal). Diagnostic for view orientation
                // (negative looks down at terrain, positive up).
                const float PitchDeg = (Args.Num() > 4)
                    ? static_cast<float>(FCString::Atof(*Args[4]))
                    : 0.0f;

                // 6th arg: shell-height fraction 0.05..0.98
                // (default 0.5). Use 0.8+ where local mesh relief
                // exceeds the analytic TerrainHeight.
                const float Alt01 = (Args.Num() > 5)
                    ? FMath::Clamp(
                        static_cast<float>(FCString::Atof(*Args[5])),
                        0.05f, 0.98f)
                    : 0.5f;

                TArray<FString> CaptureArgs;
                CaptureArgs.Add(TEXT("300"));
                CaptureArgs.Add(FString::FromInt(CaptureMode));
                ZephyrScheduleCapture(CaptureArgs);

                GZephyrCapture.PendingTeleportPlanet = PlanetIndex;
                GZephyrCapture.PendingYawDeg = YawDeg;
                GZephyrCapture.PendingPitchDeg = PitchDeg;
                GZephyrCapture.PendingAlt01 = Alt01;
                GZephyrCapture.bInstantCapture = bInstant;

                UE_LOG(
                    LogAndromedaZephyr,
                    Log,
                    TEXT("[ZEPHYR-01] SurfaceShot scheduled for planet %d."),
                    PlanetIndex
                );
            })
    );

    // r.AndromedaZephyr.FieldShot <PlanetIndex> <Side> [YawDeg] [PitchDeg] [Mode]
    //
    // Day/night task validation: teleports the player pawn to 50%
    // shell height above the planet-centered SIDE normal of planet
    // <PlanetIndex> (0 = substellar/day, 1 = terminator,
    // 2 = antistellar/night), faces it horizontally at bearing
    // [YawDeg] with [PitchDeg] elevation, then captures sky in
    // debug [Mode] (default 0). Dawn and dusk are the same model
    // geometry mirrored (the bake has no rotation terms), so one
    // terminator side covers both. No tracking is armed (it would
    // pull the pawn back to substellar); the pawn rides the
    // planet frame. Usage: r.AndromedaZephyr.FieldShot 0 1 90 0 0
    // (terminator, look horizontal, full radiance).
    FAutoConsoleCommand GZephyrFieldShotCommand(
        TEXT("r.AndromedaZephyr.FieldShot"),
        TEXT("Teleport the player pawn to a day/terminator/night field position (50% shell) and capture the sky (usage: r.AndromedaZephyr.FieldShot 0 <Side 0|1|2> [YawDeg] [PitchDeg] [Mode])."),
        FConsoleCommandWithArgsDelegate::CreateLambda(
            [](const TArray<FString>& Args)
            {
                if (GZephyrCapture.bActive)
                {
                    UE_LOG(
                        LogAndromedaZephyr,
                        Warning,
                        TEXT("[ZEPHYR-01] Capture already in progress.")
                    );
                    return;
                }

                const int32 PlanetIndex = (Args.Num() > 0)
                    ? FCString::Atoi(*Args[0])
                    : 0;

                const int32 Side = (Args.Num() > 1)
                    ? FMath::Clamp(
                        FCString::Atoi(*Args[1]), 0, 2)
                    : 0;

                const float YawDeg = (Args.Num() > 2)
                    ? static_cast<float>(FCString::Atof(*Args[2]))
                    : 90.0f;

                const float PitchDeg = (Args.Num() > 3)
                    ? static_cast<float>(FCString::Atof(*Args[3]))
                    : 0.0f;

                const int32 CaptureMode = (Args.Num() > 4)
                    ? FMath::Clamp(
                        FCString::Atoi(*Args[4]), 0, 8)
                    : 0;

                const float FieldAlt01 = (Args.Num() > 5)
                    ? FMath::Clamp(
                        static_cast<float>(FCString::Atof(*Args[5])),
                        0.05f, 0.98f)
                    : 0.5f;

                TArray<FString> CaptureArgs;
                CaptureArgs.Add(TEXT("300"));
                CaptureArgs.Add(FString::FromInt(CaptureMode));
                ZephyrScheduleCapture(CaptureArgs);

                GZephyrCapture.PendingFieldPlanet = PlanetIndex;
                GZephyrCapture.PendingFieldSide = Side;
                GZephyrCapture.PendingFieldYawDeg = YawDeg;
                GZephyrCapture.PendingFieldPitchDeg = PitchDeg;
                GZephyrCapture.PendingAlt01 = FieldAlt01;
                GZephyrCapture.bInstantCapture = false;
                // Glue the moving field spot through hitches and
                // orbital drift (recomputed from fresh snapshots;
                // never falls back to substellar).
                GZephyrCapture.TrackingPlanet = PlanetIndex;
                GZephyrCapture.bTrackingField = true;
                GZephyrCapture.TrackingFieldSide = Side;
                GZephyrCapture.TrackingYawDeg = YawDeg;
                GZephyrCapture.TrackingPitchDeg = PitchDeg;
                GZephyrCapture.TrackingRadii = 0.0f;
                GZephyrCapture.TrackingAlt01 = FieldAlt01;

                UE_LOG(
                    LogAndromedaZephyr,
                    Log,
                    TEXT("[ZEPHYR-01] FieldShot scheduled for planet %d side %d."),
                    PlanetIndex,
                    Side
                );
            })
    );

    // r.AndromedaZephyr.OrbitShot <PlanetIndex> <Radii> [Mode]
    //
    // Near-planet inspection: teleports the pawn to Radii x
    // atmosphere outer radius above the substellar point facing
    // the planet center (fully lit disk, limb all around), then
    // captures [Mode] instantly (same tick: frozen relative
    // geometry). Used for the white-sphere hunt + TEST H.
    FAutoConsoleCommand GZephyrOrbitShotCommand(
        TEXT("r.AndromedaZephyr.OrbitShot"),
        TEXT("Teleport the pawn outside a planet atmosphere facing its center and capture instantly (usage: r.AndromedaZephyr.OrbitShot 0 2.0 [Mode])."),
        FConsoleCommandWithArgsDelegate::CreateLambda(
            [](const TArray<FString>& Args)
            {
                if (GZephyrCapture.bActive)
                {
                    UE_LOG(
                        LogAndromedaZephyr,
                        Warning,
                        TEXT("[ZEPHYR-01] Capture already in progress.")
                    );
                    return;
                }

                const int32 PlanetIndex = (Args.Num() > 0)
                    ? FCString::Atoi(*Args[0])
                    : 0;

                const float Radii = (Args.Num() > 1)
                    ? FMath::Max(
                        static_cast<float>(FCString::Atof(*Args[1])),
                        1.05f)
                    : 2.0f;

                const int32 CaptureMode = (Args.Num() > 2)
                    ? FMath::Clamp(
                        FCString::Atoi(*Args[2]), 0, 8)
                    : 0;

                TArray<FString> CaptureArgs;
                CaptureArgs.Add(TEXT("300"));
                CaptureArgs.Add(FString::FromInt(CaptureMode));
                ZephyrScheduleCapture(CaptureArgs);

                GZephyrCapture.PendingTeleportPlanet = -1;
                GZephyrCapture.PendingYawDeg = 0.0f;
                // OrbitShot teleports immediately (planets may
                // still be spawning: retry each tick until the
                // capture fires via the tracking path below).
                GZephyrCapture.TrackingPlanet = PlanetIndex;
                GZephyrCapture.TrackingYawDeg = 0.0f;
                GZephyrCapture.TrackingRadii = Radii;
                GZephyrCapture.bInstantCapture = true;

                // Prime first teleport right away when possible.
                ZephyrTeleportPawnToOrbit(PlanetIndex, Radii);

                UE_LOG(
                    LogAndromedaZephyr,
                    Log,
                    TEXT("[ZEPHYR-01] OrbitShot scheduled for planet %d at %.2f radii."),
                    PlanetIndex,
                    static_cast<double>(Radii)
                );
            })
    );
}

// =========================================================
// CROSS-FRAME LUT HISTORY CACHE (render thread only)
// =========================================================
// Pooled render targets extracted from the RDG graph survive
// across frames and are re-registered as SRV inputs while
// their invalidation key still matches (standard RDG history
// pattern). Planet-dependent LUTs (transmittance, multi-
// scatter) and view-dependent LUTs (sky SS/MS) carry separate
// keys, so star motion re-bakes only the sky LUTs while a
// profile change re-bakes everything (ZEPHYR-01 section 18).
namespace
{
    TRefCountPtr<IPooledRenderTarget> GTransmittanceCache;
    TRefCountPtr<IPooledRenderTarget> GMultiScatterCache;
    TRefCountPtr<IPooledRenderTarget> GSkySingleCache;
    TRefCountPtr<IPooledRenderTarget> GSkyMultiCache;

    uint64 GPlanetCacheKey = 0;
    uint64 GViewCacheKey = 0;
    int32 GCachePlanetCount = 0;
    bool bCacheValid = false;

    void ZephyrInvalidateCache()
    {
        GTransmittanceCache.SafeRelease();
        GMultiScatterCache.SafeRelease();
        GSkySingleCache.SafeRelease();
        GSkyMultiCache.SafeRelease();
        bCacheValid = false;
        GPlanetCacheKey = 0;
        GViewCacheKey = 0;
        GCachePlanetCount = 0;
    }
}

// =========================================================
// LIFECYCLE
// =========================================================

bool FZephyrRenderer::bInitialized = false;
FDelegateHandle FZephyrRenderer::PostEngineInitDelegateHandle;
FDelegateHandle FZephyrRenderer::WorldCleanupDelegateHandle;

void FZephyrRenderer::Initialize()
{
    if (bInitialized)
    {
        return;
    }

    check(IsInGameThread());

    RegisterShaderDirectoryMapping();

    // NOTE: registered AFTER the ATMOS handler (StartupModule
    // calls ATMOS first), so the ZEPHYR view extension lands
    // after ATMOS in delegate order. Order is irrelevant to
    // correctness (overwrite-vs-passthrough compositing) but
    // keeps PIX captures readable.
    PostEngineInitDelegateHandle =
        FCoreDelegates::GetOnPostEngineInit().AddStatic(
            &FZephyrRenderer::HandlePostEngineInit
        );

    bInitialized = true;

    UE_LOG(
        LogAndromedaZephyr,
        Log,
        TEXT("[ZEPHYR-01] FZephyrRenderer initialized.")
    );
}

void FZephyrRenderer::Shutdown()
{
    if (!bInitialized)
    {
        return;
    }

    FCoreDelegates::GetOnPostEngineInit().Remove(
        PostEngineInitDelegateHandle
    );

    FWorldDelegates::OnWorldCleanup.Remove(
        WorldCleanupDelegateHandle
    );

    WorldCleanupDelegateHandle.Reset();

    FZephyrViewExtension::Unregister();

    bInitialized = false;
}

bool FZephyrRenderer::IsInitialized()
{
    return bInitialized;
}

void FZephyrRenderer::RegisterShaderDirectoryMapping()
{
    // The /Andromeda mapping is registered by the ATMOS
    // renderer at startup (same physical directory); the
    // /Andromeda/Zephyr/*.usf files live under it, so no
    // second mapping is required. Kept as an explicit step so
    // ZEPHYR does not silently depend on ATMOS init order for
    // shader resolution: verify the directory exists.
    const FString RealShaderDirectory =
        FPaths::Combine(
            FPaths::ProjectDir(),
            TEXT("Shaders"),
            TEXT("Andromeda"),
            TEXT("Zephyr")
        );

    checkf(
        FPaths::DirectoryExists(RealShaderDirectory),
        TEXT("ZEPHYR shader directory is missing: %s"),
        *RealShaderDirectory
    );
}

void FZephyrRenderer::HandlePostEngineInit()
{
    // PHASE 2.1 DEPRECATED hook: the sky stage no longer owns a
    // Tonemap subscription. FUnifiedAtmosphereViewExtension owns the
    // single hook and dispatches this stage in deterministic order.
    // Intentionally NOT calling FZephyrViewExtension::Register().

    WorldCleanupDelegateHandle =
        FWorldDelegates::OnWorldCleanup.AddStatic(
            &FZephyrRenderer::HandleWorldCleanup
        );

    const bool bValid = ValidateShaderInfrastructure();

    UE_LOG(
        LogAndromedaZephyr,
        Log,
        TEXT("[ZEPHYR-01] View extension registered (Tonemap hook, any FSceneView). Shader infrastructure %s."),
        bValid ? TEXT("VALID") : TEXT("pending (use r.AndromedaZephyr.Validate after shaders compile)")
    );
}

void FZephyrRenderer::HandleWorldCleanup(
    UWorld* InWorld,
    bool bSessionEnded,
    bool bCleanupResources)
{
    // Game-thread mailbox reset is owned by the registry actor;
    // here (render thread) drop GPU history so no stale LUT
    // outlives its world session.
    ZephyrInvalidateCache();

    UE_LOG(
        LogAndromedaZephyr,
        Log,
        TEXT("[ZEPHYR-01] LUT history cache invalidated (world cleanup).")
    );
}

// =========================================================
// CPU DATA
// =========================================================

void FZephyrRenderer::BuildGPUData(
    const TArray<FZephyrPlanetSnapshotEntry>& Snapshot,
    const FVector& ViewOrigin,
    const FVector& StarWorldPosition,
    TArray<FZephyrPlanetGPUData>& OutGPUData)
{
    OutGPUData.Reset(Snapshot.Num());

    constexpr double CmToKm = 0.00001;

    for (const FZephyrPlanetSnapshotEntry& Entry : Snapshot)
    {
        const FZephyrPlanetProfile& P = Entry.Profile;

        FZephyrPlanetGPUData GPUData = {};

        const FVector RelCenter = Entry.PlanetCenter - ViewOrigin;

        GPUData.CenterX = static_cast<float>(RelCenter.X);
        GPUData.CenterY = static_cast<float>(RelCenter.Y);
        GPUData.CenterZ = static_cast<float>(RelCenter.Z);
        GPUData.GroundRadiusCm = P.GroundRadius;

        GPUData.AtmosphereRadiusCm = P.AtmosphereRadius;
        GPUData.RayleighScaleKm =
            static_cast<float>(static_cast<double>(P.RayleighScaleHeight) * CmToKm);
        GPUData.MieScaleKm =
            static_cast<float>(static_cast<double>(P.MieScaleHeight) * CmToKm);
        GPUData.MieAnisotropy = P.MieAnisotropy;

        GPUData.RayleighX = static_cast<float>(P.RayleighScattering.X);
        GPUData.RayleighY = static_cast<float>(P.RayleighScattering.Y);
        GPUData.RayleighZ = static_cast<float>(P.RayleighScattering.Z);
        GPUData.DensityScale = P.AtmosphericDensityScale;

        GPUData.MieX = static_cast<float>(P.MieScattering.X);
        GPUData.MieY = static_cast<float>(P.MieScattering.Y);
        GPUData.MieZ = static_cast<float>(P.MieScattering.Z);
        GPUData.GroundAlbedo = P.GroundAlbedo;

        GPUData.AbsorbX = static_cast<float>(P.AbsorptionCoefficients.X);
        GPUData.AbsorbY = static_cast<float>(P.AbsorptionCoefficients.Y);
        GPUData.AbsorbZ = static_cast<float>(P.AbsorptionCoefficients.Z);
        GPUData.AbsorbCenterKm =
            static_cast<float>(static_cast<double>(P.AbsorptionLayerHeight) * CmToKm);

        GPUData.AbsorbWidthKm =
            static_cast<float>(static_cast<double>(P.AbsorptionLayerWidth) * CmToKm);

        // Per-view camera height above ground (km, double until
        // the final narrow). Negative when buried, beyond the
        // shell when in space: both are valid LUT inputs.
        const double CamDistCm = RelCenter.Length();
        const double HeightCm =
            CamDistCm - static_cast<double>(P.GroundRadius);
        GPUData.ViewHeightKm =
            static_cast<float>(HeightCm * CmToKm);

        GPUData.MieDensityScale = P.MieDensityScale;
        // Sky visual-transition radius (cm). Legacy snapshots
        // publish 0: fall back to the physical shell so the
        // shader never reads an empty bound.
        GPUData.SkyTransitionRadiusCm =
            (Entry.SkyTransitionRadiusCm > 0.0f)
                ? Entry.SkyTransitionRadiusCm
                : P.AtmosphereRadius;

        // SUN LIGHT REFERENCE (world frame, convention A: Planet -> Sun).
        // Packed6 carries Entry.SunDirectionWorld VERBATIM: baked on the
        // game thread from ASun::AtmosphereLightReference, never
        // reconstructed here ((Star - Center) is FORBIDDEN as an
        // atmospheric source of truth) and never rotated.
        // WHY NO PlanetRot.Inverse(): the Hillaire Case A sun is
        // directional over a rotation-symmetric shell, and every
        // geometric quantity in the shaders (RelCenter, Up, RayDir)
        // is world-frame. Rotating ONLY the sun mixed the frames and
        // rotated the day/night pattern by the inverse planet spin
        // (read as "inverted sun" at ~180 deg). Planet rotation still
        // drives the terrain mesh; it is reported, not applied, here.
        FVector SunDirWorld = Entry.SunDirectionWorld;
        if (SunDirWorld.IsNearlyZero(1e-6f))
        {
            if (StarWorldPosition != FVector::ZeroVector)
            {
                // Legacy-snapshot guard (pre-reference data): same owned
                // helper, explicit emission point. A live Registry always
                // fills SunDirectionWorld, so this path is cold.
                SunDirWorld =
                    UAtmosphereLightReferenceComponent::ComputeDirectionTowardSunWorld(
                        StarWorldPosition,
                        Entry.PlanetCenter
                    );
            }
            else
            {
                SunDirWorld = FVector(0.0f, 0.0f, 1.0f);
            }
        }
        GPUData.SunDirPlanetX = SunDirWorld.X;
        GPUData.SunDirPlanetY = SunDirWorld.Y;
        GPUData.SunDirPlanetZ = SunDirWorld.Z;

        // Transition COMPLETION radius (cm, Packed6.w slot): the
        // mandated (PlanetRadius + TerrainHeight) * 1.1 from the
        // snapshot, inner edge of the transition fade (100% planetary
        // regime at/below it). Legacy 0 guard recomputes from the
        // same shared helper so the shader never reads an empty
        // bound; radii are already covered by the planet key via the
        // profile hash + TerrainHeightCm, so no key change is needed.
        GPUData.TransitionCompleteRadiusCm =
            (Entry.TransitionCompleteRadiusCm > 0.0f)
                ? Entry.TransitionCompleteRadiusCm
                : AndromedaAtmosphereReference::ComputeTransitionCompleteRadiusCm(
                    P.GroundRadius,
                    Entry.TerrainHeightCm);

        OutGPUData.Add(GPUData);
    }
}

uint64 FZephyrRenderer::ComputePlanetKey(
    const TArray<FZephyrPlanetSnapshotEntry>& Snapshot,
    float MieScale,
    float AbsorptionScale)
{
    uint64 Key = 14695981039346656037ULL;

    auto Mix64 = [&](uint64 Value)
    {
        Key ^= Value + 0x9E3779B97F4A7C15ULL + (Key << 6) + (Key >> 2);
    };

    auto MixFloat = [&](float Value)
    {
        uint32 Bits = 0;
        FMemory::Memcpy(&Bits, &Value, sizeof(float));
        Mix64(static_cast<uint64>(Bits));
    };

    Mix64(static_cast<uint64>(Snapshot.Num()));
    MixFloat(MieScale);
    MixFloat(AbsorptionScale);

    for (const FZephyrPlanetSnapshotEntry& Entry : Snapshot)
    {
        Mix64(Entry.Profile.ComputeProfileHash());
        // Sky-transition geometry participates in the planet key:
        // distinct terrain heights bake distinct transition zones.
        MixFloat(Entry.TerrainHeightCm);
        MixFloat(Entry.SkyTransitionRadiusCm);
    }

    return Key;
}

uint64 FZephyrRenderer::ComputeViewKey(
    uint64 PlanetKey,
    const TArray<FZephyrPlanetGPUData>& GPUData,
    float MultiScatterScale)
{
    uint64 Key = PlanetKey;

    auto Mix64 = [&](uint64 Value)
    {
        Key ^= Value + 0x9E3779B97F4A7C15ULL + (Key << 6) + (Key >> 2);
    };

    // Quantized camera heights and FULL per-planet sun-vector geometry.
    // Sun direction is stored in GPUData.Packed6.xyz (world-frame
    // toward-sun from the Sun reference; rotation-stable, so planet
    // spin alone no longer churns the view key).
    for (const FZephyrPlanetGPUData& Planet : GPUData)
    {
        const int32 HeightBucket =
            static_cast<int32>(Planet.ViewHeightKm * 4096.0f);
        Mix64(static_cast<uint64>(
            static_cast<int64>(HeightBucket) * 1099511628211LL));

        // Sun direction, world-frame toward-sun (from Packed6.xyz)
        const double SunX = Planet.SunDirPlanetX;
        const double SunY = Planet.SunDirPlanetY;
        const double SunZ = Planet.SunDirPlanetZ;
        const int32 SunXBucket = static_cast<int32>(SunX * 4096.0);
        const int32 SunYBucket = static_cast<int32>(SunY * 4096.0);
        const int32 SunZBucket = static_cast<int32>(SunZ * 4096.0);
        Mix64(static_cast<uint64>(
            static_cast<int64>(SunXBucket) * 16777619LL + 0x85EBCA6BULL));
        Mix64(static_cast<uint64>(
            static_cast<int64>(SunYBucket) * 16777619LL + 0x9E3779B9ULL));
        Mix64(static_cast<uint64>(
            static_cast<int64>(SunZBucket) * 16777619LL + 0xC2B2AE35ULL));
    }

    uint32 MSBits = 0;
    FMemory::Memcpy(&MSBits, &MultiScatterScale, sizeof(float));
    Mix64(static_cast<uint64>(MSBits));

    return Key;
}

// =========================================================
// RENDERING
// =========================================================

// READ-ONLY ATMOS state query (no ATMOS modification).
// r.AndromedaAtmos.Enable is ECVF_RenderThreadSafe, so reading
// it here on the render thread is safe. Resolved lazily: ATMOS
// registers its console variables during static init, which may
// run after this TU's statics, so lookup happens on first use
// (function-local static = thread-safe init).
static int32 ZephyrReadAtmosEnabled()
{
    static IConsoleVariable* AtmosEnableCVar =
        IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.AndromedaAtmos.Enable"));

    if (!AtmosEnableCVar)
    {
        // ATMOS console variables not registered (should not
        // happen in practice: ATMOS initializes first). Assume
        // enabled so ZEPHYR never double-counts against an
        // active ATMOS pass.
        return 1;
    }

    return (AtmosEnableCVar->GetInt() != 0) ? 1 : 0;
}

FScreenPassTexture FZephyrRenderer::RenderSky(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs)
{
    if (!bInitialized
        || CVarZephyrEnable.GetValueOnRenderThread() == 0)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    // --------------------------------------------------------
    // Snapshot: planet profiles + star (render-thread copy).
    // --------------------------------------------------------
    TArray<FZephyrPlanetSnapshotEntry> Snapshot;
    FVector StarWorldPosition = FVector::ZeroVector;
    uint64 SnapshotVersion = 0;

    // PHASE 2.1: single unified mailbox (same snapshot the aerial
    // stage reads). FZephyrManager is deprecated.
    FAndromedaAtmosphereSystem::Get().GetSnapshot(
        Snapshot, StarWorldPosition, SnapshotVersion
    );

    int32 ActiveCount = FMath::Min(Snapshot.Num(), GetMaxPlanets());

    if (ActiveCount <= 0)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    const FVector ViewOrigin = View.ViewMatrices.GetViewOrigin();

    const bool bHasStar = (StarWorldPosition != FVector::ZeroVector);

    if (!bHasStar)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    const FVector StarRelCam = StarWorldPosition - ViewOrigin;
    const double StarDist = StarRelCam.Length();

    if (!(StarDist > 1e-6))
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    const FVector SunDirectionCam = StarRelCam / StarDist;

    // --------------------------------------------------------
    // Global debug scales (part of the invalidation keys).
    // --------------------------------------------------------
    const float MieScale =
        CVarZephyrMie.GetValueOnRenderThread();
    const float AbsorptionScale =
        CVarZephyrAbs.GetValueOnRenderThread();
    const float MultiScatterScale =
        CVarZephyrMS.GetValueOnRenderThread();
    const float Exposure =
        CVarZephyrExposure.GetValueOnRenderThread();
    const int32 DebugMode =
        FMath::Clamp(
            CVarZephyrDebug.GetValueOnRenderThread(), 0, 8);
    const float SunRadiusDeg =
        CVarZephyrSunRadiusDeg.GetValueOnRenderThread();
    const float SunLuminance =
        CVarZephyrSunLuminance.GetValueOnRenderThread();
    const bool bFreezeLUTs =
        (CVarZephyrFreezeLUTs.GetValueOnRenderThread() != 0);

    // --------------------------------------------------------
    // GPU planet data (camera-relative, explicit units).
    // --------------------------------------------------------
    TArray<FZephyrPlanetGPUData> GPUData;
    BuildGPUData(Snapshot, ViewOrigin, StarWorldPosition, GPUData);

    GPUData.SetNum(ActiveCount);

    const uint64 PlanetKey =
        ComputePlanetKey(Snapshot, MieScale, AbsorptionScale);
    const uint64 ViewKey = ComputeViewKey(
        PlanetKey, GPUData, MultiScatterScale);

    const bool bCacheUsable = bCacheValid
        && GCachePlanetCount == ActiveCount
        && GTransmittanceCache.IsValid()
        && GMultiScatterCache.IsValid()
        && GSkySingleCache.IsValid()
        && GSkyMultiCache.IsValid();

    const bool bPlanetDirty = !bCacheUsable
        || (GPlanetCacheKey != PlanetKey);
    const bool bViewDirty = !bCacheUsable
        || bPlanetDirty
        || (GViewCacheKey != ViewKey);

    const bool bRegenPlanet = bPlanetDirty && !bFreezeLUTs;
    const bool bRegenView =
        (bViewDirty && !bFreezeLUTs) || bRegenPlanet;

    // If frozen without any cache there is nothing to sample:
    // fall back to a full regen so the pass never reads null.
    const bool bMustRegenAll = !bCacheUsable;

    const bool bDoPlanet = bRegenPlanet || bMustRegenAll;
    const bool bDoView = bRegenView || bMustRegenAll;

    FGlobalShaderMap* GlobalShaderMap =
        GetGlobalShaderMap(GMaxRHIFeatureLevel);

    if (!GlobalShaderMap)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    // --------------------------------------------------------
    // Planet structured buffer (shared by every ZEPHYR pass).
    // --------------------------------------------------------
    const uint32 NumElements =
        FMath::Max(1u, static_cast<uint32>(GPUData.Num()));

    FRDGBufferDesc BufferDesc =
        FRDGBufferDesc::CreateStructuredDesc(
            sizeof(FZephyrPlanetGPUData),
            NumElements
        );

    FRDGBufferRef PlanetBuffer = GraphBuilder.CreateBuffer(
        BufferDesc,
        TEXT("ZephyrPlanetBuffer"),
        ERDGBufferFlags::None
    );

    const FZephyrPlanetGPUData DummyPlanet = {};

    GraphBuilder.QueueBufferUpload(
        PlanetBuffer,
        (GPUData.Num() > 0) ? GPUData.GetData() : &DummyPlanet,
        (GPUData.Num() > 0)
            ? static_cast<uint64>(
                GPUData.Num() * sizeof(FZephyrPlanetGPUData))
            : static_cast<uint64>(sizeof(FZephyrPlanetGPUData)),
        ERDGInitialDataFlags::None
    );

    FRDGBufferSRVRef PlanetBufferSRV = GraphBuilder.CreateSRV(
        FRDGBufferSRVDesc(PlanetBuffer)
    );

    const ETextureCreateFlags LUTFlags =
        TexCreate_ShaderResource | TexCreate_UAV;

    // --------------------------------------------------------
    // Transmittance LUT (planet-dependent).
    // --------------------------------------------------------
    FRDGTextureRef TransmittanceTex = nullptr;

    if (!bDoPlanet)
    {
        TransmittanceTex = GraphBuilder.RegisterExternalTexture(
            GTransmittanceCache,
            TEXT("ZephyrTransmittanceHistory")
        );
    }
    else
    {
        FRDGTextureDesc TransDesc = FRDGTextureDesc::Create2D(
            FIntPoint(
                TransmittanceWidth,
                TransmittanceSliceHeight * ActiveCount),
            PF_FloatRGBA,
            FClearValueBinding::Black,
            LUTFlags
        );

        TransmittanceTex = GraphBuilder.CreateTexture(
            TransDesc,
            TEXT("ZephyrTransmittanceLUT")
        );

        TShaderMapRef<FZephyrTransmittanceCS> TransCS(
            GlobalShaderMap
        );

        FZephyrTransmittanceCS::FParameters* TransParams =
            GraphBuilder.AllocParameters<
                FZephyrTransmittanceCS::FParameters
            >();

        TransParams->PlanetBuffer = PlanetBufferSRV;
        TransParams->OutTransmittance = GraphBuilder.CreateUAV(
            FRDGTextureUAVDesc(TransmittanceTex)
        );
        TransParams->PlanetCount = ActiveCount;
        TransParams->SliceHeight = TransmittanceSliceHeight;
        TransParams->MieScale = MieScale;
        TransParams->AbsorptionScale = AbsorptionScale;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("ZephyrTransmittanceLUT"),
            TransCS,
            TransParams,
            FIntVector(
                TransmittanceWidth / 8,
                (TransmittanceSliceHeight * ActiveCount) / 8,
                1)
        );

        GraphBuilder.QueueTextureExtraction(
            TransmittanceTex,
            &GTransmittanceCache
        );
    }

    // --------------------------------------------------------
    // Multi-scattering LUT (planet-dependent, samples T LUT).
    // --------------------------------------------------------
    FRDGTextureRef MultiScatterTex = nullptr;

    if (!bDoPlanet)
    {
        MultiScatterTex = GraphBuilder.RegisterExternalTexture(
            GMultiScatterCache,
            TEXT("ZephyrMultiScatterHistory")
        );
    }
    else
    {
        FRDGTextureDesc MSDesc = FRDGTextureDesc::Create2D(
            FIntPoint(
                MultiScatterWidth,
                MultiScatterSliceHeight * ActiveCount),
            PF_FloatRGBA,
            FClearValueBinding::Black,
            LUTFlags
        );

        MultiScatterTex = GraphBuilder.CreateTexture(
            MSDesc,
            TEXT("ZephyrMultiScatterLUT")
        );

        TShaderMapRef<FZephyrMultiScatterCS> MSCS(
            GlobalShaderMap
        );

        FZephyrMultiScatterCS::FParameters* MSParams =
            GraphBuilder.AllocParameters<
                FZephyrMultiScatterCS::FParameters
            >();

        MSParams->PlanetBuffer = PlanetBufferSRV;
        MSParams->InTransmittance = TransmittanceTex;
        MSParams->LinearClampSampler = TStaticSamplerState<
            SF_Bilinear,
            AM_Clamp,
            AM_Clamp,
            AM_Clamp
        >::GetRHI();
        MSParams->OutMultiScatter = GraphBuilder.CreateUAV(
            FRDGTextureUAVDesc(MultiScatterTex)
        );
        MSParams->PlanetCount = ActiveCount;
        MSParams->TransSliceHeight = TransmittanceSliceHeight;
        MSParams->SliceHeight = MultiScatterSliceHeight;
        MSParams->MieScale = MieScale;
        MSParams->AbsorptionScale = AbsorptionScale;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("ZephyrMultiScatterLUT"),
            MSCS,
            MSParams,
            FIntVector(
                MultiScatterWidth / 8,
                (MultiScatterSliceHeight * ActiveCount) / 8,
                1)
        );

        GraphBuilder.QueueTextureExtraction(
            MultiScatterTex,
            &GMultiScatterCache
        );
    }

    // --------------------------------------------------------
    // Sky-view LUTs (view-dependent: SS + MS pair).
    // --------------------------------------------------------
    FRDGTextureRef SkySingleTex = nullptr;
    FRDGTextureRef SkyMultiTex = nullptr;

    if (!bDoView)
    {
        SkySingleTex = GraphBuilder.RegisterExternalTexture(
            GSkySingleCache,
            TEXT("ZephyrSkySingleHistory")
        );
        SkyMultiTex = GraphBuilder.RegisterExternalTexture(
            GSkyMultiCache,
            TEXT("ZephyrSkyMultiHistory")
        );
    }
    else
    {
        FRDGTextureDesc SkySSDesc = FRDGTextureDesc::Create2D(
            FIntPoint(
                SkyViewWidth,
                SkyViewSliceHeight * ActiveCount),
            PF_FloatRGBA,
            FClearValueBinding::Black,
            LUTFlags
        );

        FRDGTextureDesc SkyMSDesc = FRDGTextureDesc::Create2D(
            FIntPoint(
                SkyViewWidth,
                SkyViewSliceHeight * ActiveCount),
            PF_FloatRGBA,
            FClearValueBinding::Black,
            LUTFlags
        );

        SkySingleTex = GraphBuilder.CreateTexture(
            SkySSDesc,
            TEXT("ZephyrSkySingleLUT")
        );
        SkyMultiTex = GraphBuilder.CreateTexture(
            SkyMSDesc,
            TEXT("ZephyrSkyMultiLUT")
        );

        TShaderMapRef<FZephyrSkyViewCS> SkyCS(
            GlobalShaderMap
        );

        FZephyrSkyViewCS::FParameters* SkyParams =
            GraphBuilder.AllocParameters<
                FZephyrSkyViewCS::FParameters
            >();

        SkyParams->PlanetBuffer = PlanetBufferSRV;
        SkyParams->InTransmittance = TransmittanceTex;
        SkyParams->InMultiScatter = MultiScatterTex;
        SkyParams->LinearClampSampler = TStaticSamplerState<
            SF_Bilinear,
            AM_Clamp,
            AM_Clamp,
            AM_Clamp
        >::GetRHI();
        SkyParams->OutSkySingle = GraphBuilder.CreateUAV(
            FRDGTextureUAVDesc(SkySingleTex)
        );
        SkyParams->OutSkyMulti = GraphBuilder.CreateUAV(
            FRDGTextureUAVDesc(SkyMultiTex)
        );
        // Per-planet sun direction now in PlanetBuffer.Packed6.xyz
        // SkyParams->SunDirectionCam = FVector3f(SunDirectionCam); // REMOVED
        SkyParams->PlanetCount = ActiveCount;
        SkyParams->TransSliceHeight = TransmittanceSliceHeight;
        SkyParams->MSSliceHeight = MultiScatterSliceHeight;
        SkyParams->SliceHeight = SkyViewSliceHeight;
        SkyParams->MieScale = MieScale;
        SkyParams->AbsorptionScale = AbsorptionScale;
        SkyParams->MultiScatterScale = MultiScatterScale;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("ZephyrSkyViewLUT"),
            SkyCS,
            SkyParams,
            FIntVector(
                SkyViewWidth / 8,
                (SkyViewSliceHeight * ActiveCount) / 8,
                1)
        );

        GraphBuilder.QueueTextureExtraction(
            SkySingleTex,
            &GSkySingleCache
        );
        GraphBuilder.QueueTextureExtraction(
            SkyMultiTex,
            &GSkyMultiCache
        );
    }

    if (bDoPlanet || bDoView)
    {
        GLUTRegenCounter.fetch_add(1);
    }

    // Keys update only from real regens (a frozen frame must
    // not poison the cache state).
    if (bDoPlanet)
    {
        GPlanetCacheKey = PlanetKey;
    }
    if (bDoView)
    {
        GViewCacheKey = ViewKey;
    }
    if (bDoPlanet || bDoView)
    {
        GCachePlanetCount = ActiveCount;
        bCacheValid = true;
    }

    // --------------------------------------------------------
    // Scene color in/out (same fullscreen pattern as ATMOS).
    // --------------------------------------------------------
    const FScreenPassTextureSlice SceneColorSlice =
        Inputs.GetInput(
            EPostProcessMaterialInput::SceneColor
        );

    if (!SceneColorSlice.TextureSRV)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    FScreenPassTexture SceneColor =
        FScreenPassTexture::CopyFromSlice(
            GraphBuilder,
            SceneColorSlice,
            Inputs.OverrideOutput
        );

    if (!SceneColor.Texture)
    {
        return Inputs.ReturnUntouchedSceneColorForPostProcessing(
            GraphBuilder
        );
    }

    FScreenPassRenderTarget Output;

    ERenderTargetLoadAction OutputLoadAction =
        ERenderTargetLoadAction::ELoad;

    if (Inputs.OverrideOutput.IsValid())
    {
        Output = Inputs.OverrideOutput;
        OutputLoadAction = ERenderTargetLoadAction::ELoad;
    }
    else
    {
        FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
        OutputDesc.Flags |= ETextureCreateFlags::RenderTargetable;

        Output = FScreenPassRenderTarget(
            GraphBuilder.CreateTexture(
                OutputDesc,
                TEXT("ZephyrSkyOutput")
            ),
            SceneColor.ViewRect,
            ERenderTargetLoadAction::ENoAction
        );

        OutputLoadAction = ERenderTargetLoadAction::ENoAction;
    }

    FZephyrSkyPS::FParameters* SkyPassParams =
        GraphBuilder.AllocParameters<
            FZephyrSkyPS::FParameters
        >();

    SkyPassParams->ViewportSize = Output.ViewRect.Size();
    SkyPassParams->CameraWorldPosition = FVector3f(
        View.ViewMatrices.GetViewOrigin()
    );

    // Camera-relative stabilized inverse projection (same
    // double-precision convention as the ATMOS path).
    const FMatrix& ClipToWorldD =
        View.ViewMatrices.GetClipToWorld();
    const FVector& ViewOriginD =
        View.ViewMatrices.GetViewOrigin();

    FMatrix44f RelClipToWorld;

    for (int32 Row = 0; Row < 4; ++Row)
    {
        const double Wd = ClipToWorldD.M[Row][3];

        RelClipToWorld.M[Row][0] = static_cast<float>(
            ClipToWorldD.M[Row][0] - ViewOriginD.X * Wd);
        RelClipToWorld.M[Row][1] = static_cast<float>(
            ClipToWorldD.M[Row][1] - ViewOriginD.Y * Wd);
        RelClipToWorld.M[Row][2] = static_cast<float>(
            ClipToWorldD.M[Row][2] - ViewOriginD.Z * Wd);
        RelClipToWorld.M[Row][3] = static_cast<float>(Wd);
    }

    SkyPassParams->InvViewProjection = RelClipToWorld;
    SkyPassParams->PlanetCount = ActiveCount;
    SkyPassParams->PlanetBuffer = PlanetBufferSRV;

    SkyPassParams->SceneColorTexture = SceneColor.Texture;
    SkyPassParams->SceneColorSampler = TStaticSamplerState<
        SF_Bilinear,
        AM_Clamp,
        AM_Clamp,
        AM_Clamp
    >::GetRHI();

    SkyPassParams->SkySingleLUT = SkySingleTex;
    SkyPassParams->SkyMultiLUT = SkyMultiTex;
    SkyPassParams->TransmittanceLUT = TransmittanceTex;
    SkyPassParams->MultiScatterLUT = MultiScatterTex;
    SkyPassParams->LutSampler = TStaticSamplerState<
        SF_Bilinear,
        AM_Clamp,
        AM_Clamp,
        AM_Clamp
    >::GetRHI();

    SkyPassParams->SceneTexturesStruct =
        Inputs.SceneTextures.SceneTextures;
    SkyPassParams->ViewUniformBuffer =
        View.ViewUniformBuffer;
    SkyPassParams->DepthOcclusionEnabled =
        Inputs.SceneTextures.SceneTextures ? 1 : 0;
    SkyPassParams->AtmosEnabled = ZephyrReadAtmosEnabled();

    // Per-planet sun direction now in PlanetBuffer.Packed6.xyz
    // SkyPassParams->SunDirectionCam = FVector3f(SunDirectionCam); // REMOVED
    SkyPassParams->StarValid = 1;
    SkyPassParams->DebugMode = DebugMode;
    SkyPassParams->TransSliceHeight = TransmittanceSliceHeight;
    SkyPassParams->MSSliceHeight = MultiScatterSliceHeight;
    SkyPassParams->SkySliceHeight = SkyViewSliceHeight;

    const double SunRadiusRad =
        static_cast<double>(SunRadiusDeg) * 0.5 * (PI / 180.0);
    SkyPassParams->SunCosAngularRadius =
        static_cast<float>(FMath::Cos(SunRadiusRad));
    SkyPassParams->SunLuminance = SunLuminance;
    SkyPassParams->Exposure = Exposure;
    SkyPassParams->MieScale = MieScale;
    SkyPassParams->AbsorptionScale = AbsorptionScale;
    // Bound every frame (same cvar the Sky-View MS bake reads): the
    // space-limb MS path multiplies by it. Missing binding = stale
    // register -> MS-scaled limb disagrees with the surface sky.
    SkyPassParams->MultiScatterScale = MultiScatterScale;

    SkyPassParams->RenderTargets[0] = FRenderTargetBinding(
        Output.Texture,
        OutputLoadAction
    );

    check(SkyPassParams->SkySingleLUT);
    check(SkyPassParams->SkyMultiLUT);
    check(SkyPassParams->TransmittanceLUT);
    check(SkyPassParams->MultiScatterLUT);

    TShaderMapRef<FZephyrSkyPS> SkyPixelShader(
        GlobalShaderMap
    );

    FPixelShaderUtils::AddFullscreenPass<FZephyrSkyPS>(
        GraphBuilder,
        GlobalShaderMap,
        RDG_EVENT_NAME("ZephyrSky"),
        SkyPixelShader,
        SkyPassParams,
        Output.ViewRect
    );

    const uint64 DispatchId =
        GDispatchCounter.fetch_add(1) + 1;

    if (DispatchId == 1)
    {
        UE_LOG(
            LogAndromedaZephyr,
            Log,
            TEXT("[ZEPHYR-01] First sky dispatch live: %d planet slice(s), star-linked, LUT-cached. Any FSceneView (game/PIE/editor/photo) receives this sky."),
            ActiveCount
        );
    }

    return FScreenPassTexture(
        Output.Texture,
        Output.ViewRect
    );
}

uint64 FZephyrRenderer::GetDispatchCount()
{
    return GDispatchCounter.load();
}

uint64 FZephyrRenderer::GetLUTRegenCount()
{
    return GLUTRegenCounter.load();
}

// =========================================================
// MAX PLANETS CALCULATION
// =========================================================
// Dynamic limit based on GPU max 2D texture dimension.
// No arbitrary hardcoded cap.

int32 FZephyrRenderer::GetMaxPlanets()
{
    // Max 2D texture dimension (conservative: 16384 for broad GPU support).
    // Each LUT atlas stacks planet slices vertically.
    // The limiting factor is the tallest atlas: SkyView at 112 texels/slice.
    // MaxPlanets = MaxTextureDim / SliceHeight
    constexpr int32 MaxTextureDim = 16384;
    constexpr int32 TallestSliceHeight = 112; // SkyViewSliceHeight
    const int32 MaxByTexture = MaxTextureDim / TallestSliceHeight;

    // Also bound by structured buffer size (practical limit far higher).
    // Return a reasonable ceiling with headroom.
    return FMath::Min(MaxByTexture, 128);
}

// =========================================================
// DIAGNOSTICS
// =========================================================

bool FZephyrRenderer::ValidateShaderInfrastructure()
{
    if (!bInitialized)
    {
        UE_LOG(
            LogAndromedaZephyr,
            Warning,
            TEXT("ValidateShaderInfrastructure: renderer not initialized.")
        );
        return false;
    }

    if (IsRunningCommandlet())
    {
        UE_LOG(
            LogAndromedaZephyr,
            Warning,
            TEXT("ValidateShaderInfrastructure: not available in commandlets.")
        );
        return false;
    }

    FGlobalShaderMap* GlobalShaderMap =
        GetGlobalShaderMap(GMaxRHIFeatureLevel);

    if (!GlobalShaderMap)
    {
        UE_LOG(
            LogAndromedaZephyr,
            Warning,
            TEXT("ValidateShaderInfrastructure: global shader map unavailable.")
        );
        return false;
    }

    const bool bTransCompiled = GlobalShaderMap->HasShader(
        &FZephyrTransmittanceCS::GetStaticType(), 0);
    const bool bMSCompiled = GlobalShaderMap->HasShader(
        &FZephyrMultiScatterCS::GetStaticType(), 0);
    const bool bSkyViewCompiled = GlobalShaderMap->HasShader(
        &FZephyrSkyViewCS::GetStaticType(), 0);
    const bool bSkyCompiled = GlobalShaderMap->HasShader(
        &FZephyrSkyPS::GetStaticType(), 0);

    UE_CLOG(
        !bTransCompiled,
        LogAndromedaZephyr,
        Log,
        TEXT("ValidateShaderInfrastructure: FZephyrTransmittanceCS not compiled yet.")
    );
    UE_CLOG(
        !bMSCompiled,
        LogAndromedaZephyr,
        Log,
        TEXT("ValidateShaderInfrastructure: FZephyrMultiScatterCS not compiled yet.")
    );
    UE_CLOG(
        !bSkyViewCompiled,
        LogAndromedaZephyr,
        Log,
        TEXT("ValidateShaderInfrastructure: FZephyrSkyViewCS not compiled yet.")
    );
    UE_CLOG(
        !bSkyCompiled,
        LogAndromedaZephyr,
        Log,
        TEXT("ValidateShaderInfrastructure: FZephyrSkyPS not compiled yet.")
    );

    return bTransCompiled && bMSCompiled
        && bSkyViewCompiled && bSkyCompiled;
}
