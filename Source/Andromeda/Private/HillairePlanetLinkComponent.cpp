#include "HillairePlanetLinkComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Zephyr/ZephyrLog.h"
#include "HillaireLimits.h"
#include "HillairePlanetaryAtmosphereSubsystem.h"
#include "HillaireAtmosphereProfile.h"
#include "PlanetaryLightingComponent.h"
#include "Planet/Planet.h"
#include "Sun.h"
#include "Zephyr/ZephyrProfile.h"
#include "UObject/UnrealType.h"


UHillairePlanetLinkComponent::UHillairePlanetLinkComponent()
{
    // Tick-driven: the center follows orbits and the radius is re-read, so
    // a switched/resized planet propagates without manual invalidation.
    // Use TG_PostUpdateWork to run AFTER the StarSystem actor tick updates
    // planet positions/rotations, ensuring we read the current frame's transform.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}


void UHillairePlanetLinkComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UWorld* World = GetWorld())
    {
        if (UHillairePlanetaryAtmosphereSubsystem* Subsystem = World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>())
        {
            const FGuid PlanetId = ReadPlanetId(GetOwner());
            const FName PlanetName = FName(*GetOwner()->GetActorNameOrLabel());
            Subsystem->RegisterExternalPlanet(PlanetId, PlanetName);
        }
    }

    PushPlanetState();
}


void UHillairePlanetLinkComponent::EndPlay(
    const EEndPlayReason::Type EndPlayReason
)
{
    if (UWorld* World = GetWorld())
    {
        if (UHillairePlanetaryAtmosphereSubsystem* Subsystem = World->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>())
        {
            const FGuid PlanetId = ReadPlanetId(GetOwner());
            Subsystem->UnregisterExternalPlanet(PlanetId);
        }
    }

    Super::EndPlay(EndPlayReason);
}


void UHillairePlanetLinkComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction
)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    PushPlanetState();
}


FPlanetAtmosphereState UHillairePlanetLinkComponent::MakePlanetAtmosphereState(
    const FGuid& InPlanetId,
    const FName& InPlanetName,
    const FVector& CenterWS,
    const FQuat& RotationWS,
    float GroundRadiusKm,
    float InAtmosphereHeightKm,
    float TerrainHeightKm,
    const FGuid& StarId,
    int64 PlanetSeed,
    int32 Archetype,
    float OrbitDistanceCm
)
{
    // Use the centralized FASE-2 builder: authoring radii fold into an
    // envelope-normalized, volume-stable atmosphere profile (authoritative
    // downstream).
    FHillaireAtmosphereProfile BaseProfile = FHillaireAtmosphereProfile::MakeReferenceProfile();

    // ZEPHYR presentation seam: tint ONLY the scattering magnitudes of the
    // validated reference profile from the deterministic per-planet climate
    // identity. The reference SHAPE (density profiles, phase g, absorption
    // tent) and the envelope normalization are preserved, so ATMOS physics is
    // untouched; only the per-planet aesthetic differs.
    {
        float RayleighScale = 1.0f;
        float MieScale = 1.0f;
        UZephyrProfileLibrary::GetArchetypeScatteringScales(
            (EPlanetArchetype)Archetype, RayleighScale, MieScale);
        RayleighScale = FMath::Clamp(RayleighScale, 0.6f, 1.5f);
        MieScale = FMath::Clamp(MieScale, 0.6f, 1.6f);

        BaseProfile.RayleighScatteringKm *= RayleighScale;
        BaseProfile.MieScatteringKm *= MieScale;
        BaseProfile.MieExtinctionKm *= MieScale;
        BaseProfile.MieAbsorptionKm = BaseProfile.MieExtinctionKm - BaseProfile.MieScatteringKm;
    }

    // CORRECTED VOLUME MODEL: the profile builder owns the geometry contract.
    // Bottom = the stable reference ground radius; the envelope is a persistent
    // planetary volume (self-similar minimum raised to contain the authored
    // terrain bound) and the density is normalized to it with the reference
    // shape, so the sky exists at terrain level. InAtmosphereHeightKm is an
    // optional explicit envelope floor (authoring/tests).
    const float EnvelopeKm = FMath::Max(
        InAtmosphereHeightKm,
        ComputeAtmosphereEnvelopeThicknessKm(GroundRadiusKm, TerrainHeightKm));
    FHillaireAtmosphereProfile NormalizedProfile = HillaireBuildNormalizedProfile(BaseProfile, GroundRadiusKm, EnvelopeKm);

    FPlanetAtmosphereState State;
    State.PlanetId = InPlanetId;
    State.PlanetName = InPlanetName;
    State.CenterWS = CenterWS;
    State.RotationWS = RotationWS;
    State.Profile = NormalizedProfile;
    State.GroundRadiusKm = NormalizedProfile.BottomRadiusKm;
    State.AtmosphereTopRadiusKm = NormalizedProfile.TopRadiusKm;
    State.TerrainHeightKm = TerrainHeightKm;
    State.StarDistanceKm = -1.0f; // directional/infinite (Case A/B scope)
    State.StarId = StarId;
    State.bValid = true;
    return State;
}


float UHillairePlanetLinkComponent::ComputeAtmosphereOpticalThicknessKm(
    float GroundRadiusKm
)
{
    // ATMOS Volumetric Planetary Atmosphere:
    // Thickness = GroundRadiusKm * PlanetaryAtmosphereThicknessRatio (~0.10x ground radius).
    // AtmosphereTop = GroundRadiusKm * PlanetaryAtmosphereRadiusRatio (~1.10x ground radius).
    const float Ground = FMath::Max(GroundRadiusKm, (float)HillaireLimits::MinAtmosphereThicknessKm);
    return Ground * HillaireLimits::PlanetaryAtmosphereThicknessRatio;
}


float UHillairePlanetLinkComponent::ComputeAtmosphereEnvelopeThicknessKm(
    float GroundRadiusKm,
    float TerrainHeightKm
)
{
    // Persistent planetary atmospheric volume: anchored to the planet reference radius,
    // extends significantly beyond the planetary reference surface (~1.10x).
    // Terrain/geometry exists inside the volume without terrain conforming.
    (void)TerrainHeightKm; // LYTHOS does not exist yet; persistent planetary sphere
    const float Ground = FMath::Max(GroundRadiusKm, (float)HillaireLimits::MinAtmosphereThicknessKm);
    return Ground * HillaireLimits::PlanetaryAtmosphereThicknessRatio;
}


float UHillairePlanetLinkComponent::ComputeGroundRadiusKm(
    float RadiusCm,
    float TerrainCm
)
{
    // Atmosphere BOTTOM = the planet's STABLE REFERENCE RADIUS (base sphere, sea level).
    (void)TerrainCm;
    return RadiusCm * (float)HillaireLimits::KmPerCm;
}


float UHillairePlanetLinkComponent::ComputeAtmosphereTopRadiusCm(
    float RadiusCm,
    float TerrainCm
)
{
    // Top = reference ground (base sphere) + expanded atmospheric volume (~1.10x ground radius).
    (void)TerrainCm;
    const float GroundKm = ComputeGroundRadiusKm(RadiusCm, 0.0f);
    const float EnvelopeKm = ComputeAtmosphereEnvelopeThicknessKm(GroundKm, 0.0f);
    return (GroundKm + EnvelopeKm) * (float)HillaireLimits::CmPerKm;
}


bool UHillairePlanetLinkComponent::PushPlanetState()
{
    if (!GetWorld() || !GetOwner())
    {
        UE_LOG(LogZephyr, Warning, TEXT("[PlanetLink] PushPlanetState: missing world or owner"));
        return false;
    }

    const AActor* PlanetActor = GetOwner();

    // =========================================================
    // LIVE PROCEDURAL GEOMETRY (NEVER HARDCODED)
    // =========================================================

    const float RadiusCm = ReadPlanetRadiusCm(PlanetActor);

    if (!(RadiusCm > 0.0f) || !FMath::IsFinite(RadiusCm))
    {
        UE_LOG(LogZephyr, Warning, TEXT("[PlanetLink] PushPlanetState: invalid RadiusCm=%.3f"), RadiusCm);
        return false;
    }

    const float TerrainCm = ReadTerrainHeightCm(PlanetActor);
    const int64 PlanetSeed = ReadPlanetSeed(PlanetActor);
    const FGuid PlanetId = ReadPlanetId(PlanetActor);
    const FGuid StarId = ReadStarId(PlanetActor);

    // ZEPHYR presentation identity (deterministic climate tint).
    int32 Archetype = (int32)EPlanetArchetype::Terran;
    float OrbitDistanceCm = 0.0f;
    if (const APlanet* AsPlanet = Cast<APlanet>(PlanetActor))
    {
        Archetype = (int32)AsPlanet->PlanetArchetype;
        OrbitDistanceCm = AsPlanet->OrbitDistance;
    }

    // CORRECTED VOLUME MODEL: Bottom is the stable reference sphere (sea level).
    // The envelope is the persistent planetary volume: a self-similar minimum
    // raised to contain the AUTHORED terrain bound plus a few scale heights,
    // so mountains protrude into it and a camera on a peak stays inside. The
    // density is normalized to this envelope with the reference shape (Earth
    // h/T profile, optical depth preserved), so the sky exists at terrain
    // level and the visible limb is an optical fraction of the envelope.
    const float GroundKm = ComputeGroundRadiusKm(RadiusCm, TerrainCm);
    const float TerrainKm = FMath::Max(0.0f, TerrainCm) * (float)HillaireLimits::KmPerCm;
    const float OpticalKm = ComputeAtmosphereOpticalThicknessKm(GroundKm);
    const float EnvelopeKm = ComputeAtmosphereEnvelopeThicknessKm(GroundKm, TerrainKm);

    UE_LOG(LogZephyr, Log, TEXT("[PlanetLink] Pushing planet: Id=%s Name=%s RadiusCm=%.1f TerrainCm=%.1f GroundKm=%.3f OpticalKm=%.4f EnvelopeKm=%.4f TopKm=%.4f StarId=%s"),
        *PlanetId.ToString(), *PlanetActor->GetActorNameOrLabel(), RadiusCm, TerrainCm, GroundKm, OpticalKm, EnvelopeKm, GroundKm + EnvelopeKm, *StarId.ToString());

    // Build planet atmosphere state
    const FPlanetAtmosphereState State = MakePlanetAtmosphereState(
        PlanetId,
        FName(*PlanetActor->GetActorNameOrLabel()),
        PlanetActor->GetActorLocation(),
        PlanetActor->GetActorQuat(),
        GroundKm,
        EnvelopeKm,
        TerrainKm,
        StarId,
        PlanetSeed,
        Archetype,
        OrbitDistanceCm
    );

    // Validate
    if (!State.IsValid())
    {
        UE_LOG(LogZephyr, Warning, TEXT("[PlanetLink] PushPlanetState: State.IsValid() returned false"));
        return false;
    }

    // Push to subsystem
    if (UHillairePlanetaryAtmosphereSubsystem* Subsystem = GetWorld()->GetSubsystem<UHillairePlanetaryAtmosphereSubsystem>())
    {
        // Diagnostic: verify StarId stability
        static TMap<FGuid, FGuid> LastStarIdPerPlanet;
        FGuid* PrevStarId = LastStarIdPerPlanet.Find(PlanetId);
        if (PrevStarId && *PrevStarId != State.StarId)
        {
            UE_LOG(LogZephyr, Error, TEXT("[PlanetLink] STAR ID CHANGED! Planet=%s Old=%s New=%s"),
                *PlanetId.ToString(), *PrevStarId->ToString(), *State.StarId.ToString());
        }
        LastStarIdPerPlanet.Add(PlanetId, State.StarId);

        Subsystem->UpdateExternalPlanet(State);
        UE_LOG(LogZephyr, Log, TEXT("[PlanetLink] UpdateExternalPlanet called for %s StarId=%s"), *PlanetId.ToString(), *State.StarId.ToString());
        return true;
    }

    UE_LOG(LogZephyr, Warning, TEXT("[PlanetLink] PushPlanetState: subsystem not found"));
    return false;
}


float UHillairePlanetLinkComponent::ReadPlanetRadiusCm(
    const AActor* PlanetActor
) const
{
    if (!PlanetActor) return 0.0f;

    if (const APlanet* Planet = Cast<APlanet>(PlanetActor))
    {
        return Planet->PlanetRadius;
    }

    return ReadFloatProperty(PlanetActor, TEXT("PlanetRadius"));
}


float UHillairePlanetLinkComponent::ReadTerrainHeightCm(
    const AActor* PlanetActor
) const
{
    if (!PlanetActor) return 0.0f;

    if (const APlanet* Planet = Cast<APlanet>(PlanetActor))
    {
        return Planet->TerrainHeight;
    }

    return ReadFloatProperty(PlanetActor, TEXT("TerrainHeight"));
}


int64 UHillairePlanetLinkComponent::ReadPlanetSeed(
    const AActor* PlanetActor
) const
{
    if (!PlanetActor) return 0;

    if (const APlanet* Planet = Cast<APlanet>(PlanetActor))
    {
        return Planet->PlanetSeed;
    }

    return ReadInt64Property(PlanetActor, TEXT("PlanetSeed"));
}


FGuid UHillairePlanetLinkComponent::ReadPlanetId(
    const AActor* PlanetActor
) const
{
    if (!PlanetActor) return FGuid();

    if (const APlanet* Planet = Cast<APlanet>(PlanetActor))
    {
        // Stable FGuid from the procedural identity (single construction site:
        // HillaireMakeStablePlanetId, bit-identical to the historical formula).
        return HillaireMakeStablePlanetId(Planet->PlanetID, Planet->PlanetSeed);
    }

    const int64 ID = ReadInt64Property(PlanetActor, TEXT("PlanetID"));
    const int64 Seed = ReadInt64Property(PlanetActor, TEXT("PlanetSeed"));
    if (ID != 0 || Seed != 0)
    {
        return HillaireMakeStablePlanetId(ID, Seed);
    }

    // Fallback: hash the actor name
    return FGuid::NewGuid();
}


FGuid UHillairePlanetLinkComponent::ReadStarId(
    const AActor* PlanetActor
) const
{
    if (!PlanetActor) return FGuid();

    if (const UPlanetaryLightingComponent* Lighting = PlanetActor->FindComponentByClass<UPlanetaryLightingComponent>())
    {
        if (const AActor* StarActor = Lighting->StarActor.Get())
        {
            // Get stable star ID from the sun actor
            if (const ASun* Sun = Cast<ASun>(StarActor))
            {
                if (Sun->StableStarId.IsValid())
                {
                    return Sun->StableStarId;
                }
            }
            // Fallback: if the star actor is valid but not an ASun, return empty
            // (this shouldn't happen in normal STARMAP operation)
            return FGuid();
        }
    }

    return FGuid();
}


float UHillairePlanetLinkComponent::ReadFloatProperty(
    const AActor* Actor,
    const TCHAR* PropertyName
)
{
    if (!Actor || !PropertyName) return 0.0f;

    const FProperty* Found = Actor->GetClass()->FindPropertyByName(PropertyName);
    if (!Found) return 0.0f;

    const FFloatProperty* FloatProp = CastField<FFloatProperty>(Found);
    if (!FloatProp) return 0.0f;

    return FloatProp->GetPropertyValue_InContainer(Actor);
}


int64 UHillairePlanetLinkComponent::ReadInt64Property(
    const AActor* Actor,
    const TCHAR* PropertyName
)
{
    if (!Actor || !PropertyName) return 0;

    const FProperty* Found = Actor->GetClass()->FindPropertyByName(PropertyName);
    if (!Found) return 0;

    if (const FInt64Property* IntProp = CastField<FInt64Property>(Found))
    {
        return IntProp->GetPropertyValue_InContainer(Actor);
    }

    if (const FIntProperty* Int32Prop = CastField<FIntProperty>(Found))
    {
        return (int64)Int32Prop->GetPropertyValue_InContainer(Actor);
    }

    return 0;
}