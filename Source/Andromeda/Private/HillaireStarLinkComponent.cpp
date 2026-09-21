#include "HillaireStarLinkComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/LightComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Zephyr/ZephyrLog.h"
#include "HillairePlanetaryAtmosphereSubsystem.h"
#include "HillairePlanetLinkComponent.h"
#include "HillairePlanetState.h"
#include "Kismet/GameplayStatics.h"
#include "PlanetaryLightingComponent.h"
#include "Planet/Planet.h"
#include "StarSystem.h"
#include "Sun.h"


UHillaireStarLinkComponent::UHillaireStarLinkComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}


void UHillaireStarLinkComponent::BeginPlay()
{
    Super::BeginPlay();
    const UWorld* W = GetWorld();
    UE_LOG(LogZephyr, Log, TEXT("[StarLink] BeginPlay: Comp=%p Owner=%s World=%s"),
        this, GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"), W ? *W->GetName() : TEXT("<null>"));
    PushStarLight();
}


void UHillaireStarLinkComponent::EndPlay(
    const EEndPlayReason::Type EndPlayReason
)
{
    if (UWorld* World = GetWorld())
    {
        if (UHillairePlanetaryAtmosphereSubsystem* Subsystem = World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>())
        {
            // Unregister the same stable key used at registration (not the legacy slot id).
            if (const ASun* OwnerSun = Cast<ASun>(GetOwner()))
            {
                if (OwnerSun->StableStarId.IsValid())
                {
                    Subsystem->UnregisterExternalStar(OwnerSun->StableStarId);
                }
            }
        }
    }

    Super::EndPlay(EndPlayReason);
}


void UHillaireStarLinkComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction
)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    PushStarLight();
}


FGuid UHillaireStarLinkComponent::GetPrimaryStarSlotId()
{
    static const FGuid SlotId(
        0x48494C4C, // 'HILL'
        0x41525253, // 'ARRS'
        0x54325245, // 'T2E5'
        0x2E535441  // '.STA'
    );
    return SlotId;
}


FHillaireLightSource UHillaireStarLinkComponent::MakeStarLightSource(
    const FVector& StarWorldPosCm,
    const FVector& DirectionToStarWorld,
    const FLinearColor& StarColor,
    float EffectiveIntensity,
    const FGuid& LightId,
    const FName& LightName
)
{
    FHillaireLightSource Out;
    Out.LightId = LightId;
    Out.LightName = LightName;
    Out.bEnabled = true;
    Out.bDirectional = true;
    Out.WorldPositionCm = StarWorldPosCm;
    
    const FVector SafeDir = DirectionToStarWorld.GetSafeNormal();
    Out.WorldDirectionToLight = SafeDir.IsNearlyZero() ? FVector::ForwardVector : SafeDir;
    Out.Color = StarColor;
    Out.Intensity = FMath::Max(0.0f, EffectiveIntensity);
    Out.AngularRadiusRad = 0.004408f; // ~0.25 deg solar disk
    Out.bDrawDisk = true;
    return Out;
}


AStarSystem* UHillaireStarLinkComponent::ResolveStarSystem() const
{
    if (StarSystemActor) return StarSystemActor;

    if (const AActor* Owner = GetOwner())
    {
        if (AStarSystem* OwnerSystem = Cast<AStarSystem>(Owner->GetOwner()))
        {
            return OwnerSystem;
        }
    }

    if (!bAutoFindStarSystem) return nullptr;

    const UWorld* World = GetWorld();
    if (!World) return nullptr;

    return Cast<AStarSystem>(UGameplayStatics::GetActorOfClass(World, AStarSystem::StaticClass()));
}


bool UHillaireStarLinkComponent::PushStarLight()
{
    UWorld* World = GetWorld();
    if (!World) return false;

    AActor* Owner = GetOwner();
    if (!Owner) return false;

    // Primary path: the link is owned by the primary star itself
    if (ASun* OwnerSun = Cast<ASun>(Owner))
    {
        return PushStarLightFromSun(OwnerSun, World);
    }

    // Legacy/fallback: find star system and push for all planets in it
    AStarSystem* StarSystem = ResolveStarSystem();
    if (!StarSystem) return false;

    ASun* Sun = StarSystem->GetSunActor();
    if (!Sun) return false;

    return PushStarLightFromPlanetActor(Owner, Sun, World);
}


bool UHillaireStarLinkComponent::PushStarLightFromSun(
    ASun* OwnerSun,
    UWorld* World
)
{
    if (!OwnerSun || !World)
    {
        UE_LOG(LogZephyr, Warning, TEXT("[StarLink] PushStarLightFromSun RETURN: NullOwnerOrWorld Sub=NULL"));
        return false;
    }

    AStarSystem* StarSystem = ResolveStarSystem();
    if (!StarSystem)
    {
        const UWorld* W = GetWorld();
        UE_LOG(LogZephyr, Warning, TEXT("[StarLink] PushStarLightFromSun RETURN: NoStarSystem Owner=%s World=%s"),
            OwnerSun ? *OwnerSun->GetName() : TEXT("<null>"), W ? *W->GetName() : TEXT("<null>"));
        return false;
    }

    // Stable star ID: prefer the sun's cached ID, fall back to the owning
    // system (same deterministic value; covers BeginPlay ordering where the
    // component ticks before ASun::BeginPlay sets the cache).
    FGuid StableStarId = OwnerSun->StableStarId;
    if (!StableStarId.IsValid())
    {
        StableStarId = StarSystem->GetStableStarId();
    }
    if (!StableStarId.IsValid())
    {
        UE_LOG(LogZephyr, Warning, TEXT("[StarLink] PushStarLightFromSun RETURN: InvalidStableStarId Owner=%s"), *OwnerSun->GetName());
        return false;
    }

    // Get all planets in this star system
    TArray<FPlanetRuntimeData> Planets;
    StarSystem->GetAllPlanetRuntimeData(Planets);
    if (Planets.Num() == 0)
    {
        UE_LOG(LogZephyr, Warning, TEXT("[StarLink] PushStarLightFromSun RETURN: NoPlanets StarId=%s"), *StableStarId.ToString());
        return false;
    }

    // Camera position for source planet selection
    FVector CameraPos = FVector::ZeroVector;
    bool bHaveCamera = false;
    if (const APlayerController* PC = World->GetFirstPlayerController())
    {
        if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
        {
            CameraPos = Cam->GetCameraLocation();
            bHaveCamera = true;
        }
    }

    // Star radiometry (live read from owning sun)
    FLinearColor StarColor = FLinearColor::White;
    float StarRawIntensity = OwnerSun->LightIntensity;
    if (const ULightComponent* SunLight = OwnerSun->FindComponentByClass<ULightComponent>())
    {
        StarColor = SunLight->GetLightColor();
        StarRawIntensity = SunLight->Intensity;
    }

    // Register/update the shared star entry UNCONDITIONALLY as soon as the
    // star system resolves, independent of the (slow, staggered) planet spawn
    // schedule. The per-planet sun overrides below are preferred by
    // ResolveLightsForAllPlanets; this shared entry exists so ATMOS's light
    // registry is non-empty from the first tick the star exists, which lets
    // the sky gate (HasLights) be satisfied as soon as a planet is ready.
    UHillairePlanetaryAtmosphereSubsystem* Subsystem = World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>();
    if (Subsystem)
    {
        FVector FallbackDir = OwnerSun->GetActorForwardVector();
        if (!FallbackDir.Normalize())
        {
            FallbackDir = FVector::ForwardVector;
        }
        const FHillaireLightSource SharedStarLight = MakeStarLightSource(
            OwnerSun->GetActorLocation(), FallbackDir, StarColor, StarRawIntensity, StableStarId, StarLightName);
        Subsystem->RegisterExternalStar(StableStarId, SharedStarLight);
    }

    // For each planet with planetary lighting, push that planet's OWN
    // Planet->Star direction/intensity into the subsystem's per-planet sun
    // store (MULTIPLANETARY CORRECTION). The shared star registry holds ONE
    // directional vector per star: writing each planet's direction into it in
    // turn (last-writer-wins) gave every planet the WRONG sun — the bright
    // limb/terminator landed on the wrong hemisphere for all but one planet.
    // Canonical convention unchanged: SunDir = normalized(Star - Planet),
    // direction FROM the planet TOWARD the star. No negation anywhere.
    int32 SkippedInvalid = 0;
    int32 SkippedNoLighting = 0;
    int32 SkippedZeroDir = 0;
    int32 PlanetsFed = 0;
    for (const FPlanetRuntimeData& Planet : Planets)
    {
        if (!Planet.bValid || !Planet.PlanetActor) { SkippedInvalid++; continue; }

        const UPlanetaryLightingComponent* PlanetaryLighting = Planet.PlanetActor->FindComponentByClass<UPlanetaryLightingComponent>();
        if (!PlanetaryLighting) { SkippedNoLighting++; continue; }
        if (PlanetaryLighting->CurrentStarDirection.IsNearlyZero()) { SkippedZeroDir++; continue; }

        // Compute direction to star from this planet (world space).
        const FVector StarWorldPos = OwnerSun->GetActorLocation();
        const FVector PlanetWorldPos = Planet.WorldPosition;
        FVector DirectionToStar = (StarWorldPos - PlanetWorldPos).GetSafeNormal();

        // Ground-truth cross-check (diagnostic only): PlanetaryLighting
        // computes the same direction independently from the same live actor
        // positions. Any disagreement here proves a computation bug at the
        // source; agreement pushes the audit downstream (stored vs baked).
        {
            static uint64 DirCheckCount = 0;
            ++DirCheckCount;
            const FVector LightingDir = PlanetaryLighting->CurrentStarDirection;
            double AngleDeg = -1.0;
            const double Denom = (double)DirectionToStar.Size() * (double)LightingDir.Size();
            if (Denom > 1e-12)
            {
                const double CosA = FMath::Clamp(
                    DirectionToStar.Dot(LightingDir) / Denom, -1.0, 1.0);
                AngleDeg = FMath::RadiansToDegrees(FMath::Acos(CosA));
            }
            if (AngleDeg > 5.0)
            {
                UE_LOG(LogZephyr, Error,
                    TEXT("[StarLink] DIRECTION MISMATCH! Planet=%s PushedDir=(%.6f,%.6f,%.6f) LightingDir=(%.6f,%.6f,%.6f) AngleDeg=%.3f SunPos=(%.1f,%.1f,%.1f) PlanetPos=(%.1f,%.1f,%.1f)"),
                    *Planet.PlanetActor->GetName(),
                    DirectionToStar.X, DirectionToStar.Y, DirectionToStar.Z,
                    LightingDir.X, LightingDir.Y, LightingDir.Z, AngleDeg,
                    StarWorldPos.X, StarWorldPos.Y, StarWorldPos.Z,
                    PlanetWorldPos.X, PlanetWorldPos.Y, PlanetWorldPos.Z);
            }
            else if (DirCheckCount == 1 || (DirCheckCount % 900) == 0)
            {
                UE_LOG(LogZephyr, Log,
                    TEXT("[StarLink] DirCheck #%llu: Planet=%s AngleDeg=%.4f PushedDir=(%.6f,%.6f,%.6f) SunPos=(%.1f,%.1f,%.1f) PlanetPos=(%.1f,%.1f,%.1f)"),
                    DirCheckCount, *Planet.PlanetActor->GetName(), AngleDeg,
                    DirectionToStar.X, DirectionToStar.Y, DirectionToStar.Z,
                    StarWorldPos.X, StarWorldPos.Y, StarWorldPos.Z,
                    PlanetWorldPos.X, PlanetWorldPos.Y, PlanetWorldPos.Z);
            }
        }

        // Effective illumination from planetary lighting
        const float EffectiveIllumination = PlanetaryLighting->CurrentIllumination;
        const float EffectiveIntensity = EffectiveIllumination * StarRawIntensity;

        // Push to subsystem: per-planet sun override (authoritative direction
        // + intensity for THIS planet). The shared star entry is already
        // registered above (radiometry fallback for planets without override).
        if (Subsystem)
        {
            // Key agreement by construction: derive the planet key from the
            // ACTOR's own PlanetID/PlanetSeed (exactly what PlanetLink hashes),
            // never from a parallel copy that could drift.
            FGuid PlanetId;
            if (const APlanet* AsPlanet = Cast<APlanet>(Planet.PlanetActor))
            {
                PlanetId = HillaireMakeStablePlanetId(AsPlanet->PlanetID, AsPlanet->PlanetSeed);
            }
            else
            {
                PlanetId = HillaireMakeStablePlanetId(Planet.PlanetID, Planet.PlanetSeed);
            }
            Subsystem->SetPlanetSunDirection(PlanetId, DirectionToStar, EffectiveIntensity);
            PlanetsFed++;
        }
    }

    {
        static uint64 CallCount = 0;
        ++CallCount;
        const UWorld* W = GetWorld();
        UHillairePlanetaryAtmosphereSubsystem* Sub = World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>();
        if (CallCount == 1 || (CallCount % 300) == 0 || PlanetsFed == 0)
        {
            UE_LOG(LogZephyr, Log, TEXT("[StarLink] PushStarLightFromSun #%llu: Sub=%p World=%s GFrame=%llu StarId=%s Planets=%d PlanetsFed=%d SkipInvalid=%d SkipNoLighting=%d SkipZeroDir=%d"),
                CallCount, Sub, W ? *W->GetName() : TEXT("<null>"), (unsigned long long)GFrameCounter,
                *StableStarId.ToString(), Planets.Num(), PlanetsFed, SkippedInvalid, SkippedNoLighting, SkippedZeroDir);
        }
    }

    return true;
}


bool UHillaireStarLinkComponent::PushStarLightFromPlanetActor(
    const AActor* PlanetActor,
    ASun* Sun,
    UWorld* World
)
{
    if (!PlanetActor || !Sun || !World) return false;

    const UPlanetaryLightingComponent* PlanetaryLighting = PlanetActor->FindComponentByClass<UPlanetaryLightingComponent>();

    FVector DirectionToStar = FVector::ForwardVector;
    float EffectiveIllumination = 1.0f;

    if (PlanetaryLighting)
    {
        if (!PlanetaryLighting->CurrentStarDirection.IsNearlyZero())
        {
            DirectionToStar = PlanetaryLighting->CurrentStarDirection;
        }
        EffectiveIllumination = PlanetaryLighting->CurrentIllumination;
    }
    else
    {
        const FVector ToStar = Sun->GetActorLocation() - PlanetActor->GetActorLocation();
        if (!ToStar.IsNearlyZero()) DirectionToStar = ToStar.GetSafeNormal();
    }

    FLinearColor StarColor = FLinearColor::White;
    float StarRawIntensity = Sun->LightIntensity;
    if (const ULightComponent* SunLight = Sun->FindComponentByClass<ULightComponent>())
    {
        StarColor = SunLight->GetLightColor();
        StarRawIntensity = SunLight->Intensity;
    }

    const float EffectiveIntensity = EffectiveIllumination * StarRawIntensity;
    const FHillaireLightSource StarLight = MakeStarLightSource(
        Sun->GetActorLocation(),
        DirectionToStar,
        StarColor,
        EffectiveIntensity,
        GetPrimaryStarSlotId(),
        StarLightName
    );

    if (UHillairePlanetaryAtmosphereSubsystem* Subsystem = World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>())
    {
        // Same multiplanetary correction as the sun-owned path: this planet's
        // own direction/intensity wins over the shared entry.
        if (const APlanet* AsPlanet = Cast<APlanet>(PlanetActor))
        {
            Subsystem->SetPlanetSunDirection(
                HillaireMakeStablePlanetId(AsPlanet->PlanetID, AsPlanet->PlanetSeed),
                DirectionToStar, EffectiveIntensity);
        }
        Subsystem->RegisterExternalStar(GetPrimaryStarSlotId(), StarLight);
        return true;
    }

    return false;
}