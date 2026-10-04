#include "AndromedaPawnMovement.h"

#include "AndromedaPawn.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Planet/Planet.h"
#include "PlanetaryMotionMath.h"
#include "PlanetaryWorldSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include <limits>

UAndromedaPawnMovement::UAndromedaPawnMovement(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // CameraManager reads the final pose after PostPhysics. Input/orbits have
    // already ticked; explicit registry/controller prerequisites cover lifecycle.
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
    bTickBeforeOwner = false;
}

void UAndromedaPawnMovement::BeginPlay()
{
    Super::BeginPlay();
    // Legacy migration: MaxSpeed predates BaseVelocity as the cruise-speed
    // control. Adopt a customized MaxSpeed once so existing content keeps its
    // behavior; BaseVelocity is canonical afterwards and MaxSpeed mirrors it
    // for the engine-level GetMaxSpeed() API.
    if (BaseVelocity == 1200.f && MaxSpeed != 1200.f)
    {
        BaseVelocity = MaxSpeed;
    }
    MaxSpeed = BaseVelocity;
    Registry = GetWorld()->GetSubsystem<UPlanetaryWorldSubsystem>();
    Registry->RegisterMovement(this);
    StartSamples.Reserve(16);
    MidSamples.Reserve(16);
    EndSamples.Reserve(16);
}

void UAndromedaPawnMovement::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Registry)
    {
        Registry->UnregisterMovement(this);
    }
    Super::EndPlay(EndPlayReason);
}

void UAndromedaPawnMovement::InitializeMotion(const FVector& InitialWorldVelocity, const FVector& InitialCarrierVelocity)
{
    if (!bSimulationInitialized && PlanetaryMotion::IsFinite(InitialWorldVelocity) && PlanetaryMotion::IsFinite(InitialCarrierVelocity))
    {
        Velocity = InitialWorldVelocity;
        CarrierVelocity = InitialCarrierVelocity;
        ControlledVelocity = FVector::ZeroVector;
    }
}

void UAndromedaPawnMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    if (ShouldSkipUpdate(DeltaTime) || !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f)
    {
        return;
    }
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AAndromedaPawn* Pawn = Cast<AAndromedaPawn>(PawnOwner);
    if (!Pawn || !UpdatedPrimitive || !Registry || !PlanetaryMotion::IsFinite(Velocity)
        || !PlanetaryMotion::IsFinite(UpdatedComponent->GetComponentLocation()))
    {
        return;
    }
    if (Pawn->GetLocalRole() == ROLE_SimulatedProxy)
    {
        return;
    }

    FVector ExternalIntent = ConsumeInputVector(); // Already world space. No heuristic remapping.
    if (!PlanetaryMotion::IsFinite(ExternalIntent))
    {
        ExternalIntent = FVector::ZeroVector;
    }
    bGrounded = false;
    Registry->SamplePlanets(0.0, EndSamples);
    uint64 BodySignature = 14695981039346656037ULL;
    SceneQueryParams.ClearIgnoredSourceObjects();
    SceneQueryParams.AddIgnoredActor(PawnOwner);
    for (const FPlanetRuntimeData& Planet : EndSamples)
    {
        if (IsValid(Planet.PlanetActor))
        {
            BodySignature = (BodySignature ^ uint64(Planet.PlanetActor->GetUniqueID())) * 1099511628211ULL;
        }
        if (Planet.PlanetActor && UPlanetaryGravitySystem::IsSurfaceGravityBody(Planet))
        {
            SceneQueryParams.AddIgnoredActor(Planet.PlanetActor);
        }
    }
    const bool bBodySetChanged = BodySignature != PreviousBodySignature;
    PreviousBodySignature = BodySignature;
    double Elapsed = 0.0;
    const double Duration = DeltaTime;
    while (Elapsed < Duration - 1.e-10)
    {
        Registry->SamplePlanets(Elapsed - Duration, StartSamples);
        const FVector Position = UpdatedComponent->GetComponentLocation();
        const double Dt = UPlanetaryGravitySystem::ChooseSimulationStep(StartSamples, Position, Velocity, Duration - Elapsed, Settings);
        Registry->SamplePlanets(Elapsed - Duration + Dt * 0.5, MidSamples);
        Registry->SamplePlanets(Elapsed - Duration + Dt, EndSamples);
        // Predictor/midpoint force evaluation, including moving orbital centers.
        Field = UPlanetaryGravitySystem::Sample(MidSamples, Position + Velocity * (Dt * 0.5), Velocity, Settings);
        if (!bSimulationInitialized || bBodySetChanged)
        {
            // Initial placement / runtime registration is not a traversed shell.
            // Acquire it temporally instead of treating w=1 as an entry impulse.
            PreviousReferenceWeight = Field.ReferenceInfluence;
            bSimulationInitialized = true;
        }
        FVector Residual = Velocity - CarrierVelocity - ControlledVelocity;
        CarrierVelocity = PlanetaryMotion::AdvanceCarrier(CarrierVelocity, Field.FrameVelocity,
            Field.FrameMaterialAcceleration, Field.ReferenceInfluence, PreviousReferenceWeight,
            Settings.CaptureResponseTime, Dt);
        PreviousReferenceWeight = Field.ReferenceInfluence;
        if (Field.ReferenceInfluence == 0.0)
        {
            // Coordinate bookkeeping only: Vworld does not change. Once the
            // frame has faded, captured momentum is ordinary inertial drift,
            // which the pilot can counter with actual thrust in deep space.
            Residual += CarrierVelocity;
            CarrierVelocity = FVector::ZeroVector;
        }

        const double SpinRate = Field.AngularVelocity.Size();
        if (SpinRate > UE_DOUBLE_SMALL_NUMBER)
        {
            // Orthogonal transport preserves high-speed residual magnitude.
            // Together with material frame acceleration this is the inertial
            // expression of a continuously acquired rotating reference frame.
            Residual = FQuat(Field.AngularVelocity / SpinRate, SpinRate * Dt).RotateVector(Residual);
        }

        const FQuat OldNavigation = Pawn->GetNavigationQuat();
        Pawn->TransportNavigation(Field, Dt);
        const FQuat NavigationDelta = Pawn->GetNavigationQuat() * OldNavigation.Inverse();
        ControlledVelocity = FQuat::Slerp(FQuat::Identity, NavigationDelta,
            Field.SurfaceInfluence).RotateVector(ControlledVelocity);

        const FVector Intent = (Pawn->GetMovementIntent(Field.SurfaceInfluence) + ExternalIntent).GetClampedToMaxSize(1.0);
        // Cruise regime follows the existing reference influence: full space
        // speed in deep space, planetary base speed at the surface, geometric
        // blend between. No mode switch and no velocity overwrite: only the
        // chase target of the controlled-propulsion channel changes, so actual
        // Velocity X/Y/Z and carrier momentum stay continuous.
        const double EffectiveCruise = PlanetaryMotion::BlendCruiseSpeed(
            BaseVelocity, SpaceBaseVelocity, Field.ReferenceInfluence);
        const FVector TargetControl = Intent * float(EffectiveCruise);
        if (!Intent.IsNearlyZero())
        {
            // Thrust scales with the cruise regime so spin-up time stays
            // roughly constant from surface to deep space; without this, space
            // cruise would take hours to reach. Deliberate counter-thrust
            // against inherited drift shares the same budget.
            const double CruiseScale = (BaseVelocity > 0.f)
                ? FMath::Max(EffectiveCruise / double(BaseVelocity), 1.0)
                : 1.0;
            double ThrustBudget = FMath::Max(Acceleration, 0.f) * Intent.Size() * Dt * CruiseScale;
            const FVector ThrustDirection = Intent.GetSafeNormal();
            const FVector UncontrolledRelativeVelocity = Residual + CarrierVelocity -
                Field.FrameVelocity * Field.ReferenceInfluence;
            const double OpposedDrift = FMath::Max(-(UncontrolledRelativeVelocity | ThrustDirection), 0.0);
            const double CounterThrust = FMath::Min(OpposedDrift, ThrustBudget);
            // Preserve inertia unless the player deliberately thrusts against
            // it. Sharing one budget avoids double acceleration, and permits
            // braking a fast fall/orbital departure beyond the cruise channel.
            Residual += ThrustDirection * CounterThrust;
            ThrustBudget -= CounterThrust;
            ControlledVelocity += (TargetControl - ControlledVelocity).GetClampedToMaxSize(ThrustBudget);
        }
        else
        {
            // Inertial in space. Only the propulsion channel gains surface
            // braking; gravity, incoming drift and the carrier are never clamped.
            ControlledVelocity += (-ControlledVelocity).GetClampedToMaxSize(FMath::Max(Deceleration, 0.f) * Field.SurfaceInfluence * Dt);
        }
        // Holding local ascent supplies lift, then propulsion. This prevents a
        // capped control channel from losing a sustained ascent to accumulated
        // gravity. Releasing it restores free fall continuously with the input.
        const double Lift = PlanetaryMotion::SmoothStep(Pawn->GetAscentInput());
        const FVector PhysicalAcceleration = Field.GravityAcceleration * (1.0 - Lift);
        Residual += PhysicalAcceleration * Dt;
        const FVector OldVelocity = Velocity;
        Velocity = CarrierVelocity + ControlledVelocity + Residual;
        // Midpoint displacement using the authoritative world velocity. Collision
        // applies impulses to it; no displacement-derived overwrite of channels.
        FVector EndVelocity = Velocity;
        Velocity = (OldVelocity + EndVelocity) * 0.5;
        MoveWithMovingBodyCollision(Dt, StartSamples, EndSamples, EndVelocity);
        Velocity = EndVelocity;
        Elapsed += Dt;
    }
    Registry->SamplePlanets(0.0, EndSamples);
    Field = UPlanetaryGravitySystem::Sample(EndSamples, UpdatedComponent->GetComponentLocation(), Velocity, Settings);
    const FQuat OldNavigation = Pawn->GetNavigationQuat();
    Pawn->TransportNavigation(Field, 0.0);
    const FVector OldControl = ControlledVelocity;
    ControlledVelocity = FQuat::Slerp(FQuat::Identity, Pawn->GetNavigationQuat() * OldNavigation.Inverse(),
        Field.SurfaceInfluence).RotateVector(ControlledVelocity);
    Velocity += ControlledVelocity - OldControl;
    if (!bGrounded && Field.SurfaceInfluence > 0.0 && !Field.LocalUp.IsNearlyZero())
    {
        // Contact can persist without a new impulse in every frame. A short
        // support QUERY stabilizes the diagnostic state across the numerical
        // triangle skin; it never moves the pawn or changes its reference frame.
        if (const auto* Sphere = Cast<USphereComponent>(UpdatedPrimitive))
        {
            const double ProbeDistance = Sphere->GetScaledSphereRadius() * 0.1;
            const FVector Position = UpdatedComponent->GetComponentLocation();
            FHitResult Support;
            FVector ContactVelocity;
            double Skin;
            if (SweepMotion(Position, Position - Field.LocalUp * ProbeDistance, EndSamples, EndSamples,
                1.0, Support, ContactVelocity, Skin))
            {
                const double SettlingSpeed = FMath::Sqrt(2.0 * Field.GravityAcceleration.Size() * Skin);
                bGrounded = Support.Time * ProbeDistance <= 2.0 * Skin
                    && (Support.Normal | Field.LocalUp) > FMath::Cos(FMath::DegreesToRadians(WalkableSlopeDegrees))
                    && ((Velocity - ContactVelocity) | Support.Normal) <= SettlingSpeed;
            }
        }
    }
    Pawn->PublishView();
    UpdateComponentVelocity();
}

bool UAndromedaPawnMovement::SweepMotion(const FVector& Start, const FVector& End,
    TConstArrayView<FPlanetRuntimeData> StartPlanets, TConstArrayView<FPlanetRuntimeData> EndPlanets,
    double StartFraction, FHitResult& OutHit, FVector& OutContactVelocity, double& OutContactSkin) const
{
    const USphereComponent* Sphere = Cast<USphereComponent>(UpdatedPrimitive);
    if (!Sphere)
    {
        return false;
    }
    const FCollisionShape Shape = FCollisionShape::MakeSphere(Sphere->GetScaledSphereRadius());
    OutContactVelocity = FVector::ZeroVector;
    OutContactSkin = 0.01; // 0.1 mm for ordinary engine collision geometry.
    bool bHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, UpdatedComponent->GetComponentQuat(),
        UpdatedPrimitive->GetCollisionObjectType(), Shape, SceneQueryParams,
        FCollisionResponseParams(UpdatedPrimitive->GetCollisionResponseToChannels()));
    if (bHit && OutHit.GetComponent())
    {
        OutContactVelocity = OutHit.GetComponent()->GetComponentVelocity();
    }
    // Geometry has already moved to this frame's final orbital pose. Sweep in
    // its co-moving coordinates, then convert contact normals/velocities back.
    // This is a QUERY transform only; neither planet nor player is teleported.
    for (int32 Index = 0; Index < EndPlanets.Num(); ++Index)
    {
        const FPlanetRuntimeData& B = EndPlanets[Index];
        if (!UPlanetaryGravitySystem::IsSurfaceGravityBody(B) || !StartPlanets.IsValidIndex(Index))
        {
            continue;
        }
        const APlanet* Planet = Cast<APlanet>(B.PlanetActor);
        if (!IsValid(Planet) || !Planet->PlanetProceduralMesh || !Planet->PlanetProceduralMesh->IsQueryCollisionEnabled()
            || Planet->PlanetProceduralMesh->GetCollisionResponseToChannel(UpdatedPrimitive->GetCollisionObjectType()) != ECR_Block)
        {
            continue;
        }
        const FPlanetRuntimeData& A = StartPlanets[Index];
        const FQuat QA = A.CurrentRotation.Quaternion();
        const FQuat QB = B.CurrentRotation.Quaternion();
        const FQuat QS = FQuat::Slerp(QA, QB, StartFraction);
        const FVector CS = FMath::Lerp(A.WorldPosition, B.WorldPosition, StartFraction);
        const FBodyInstance* Body = Planet->PlanetProceduralMesh->GetBodyInstance();
        if (!Body || !Body->IsValidBodyInstance())
        {
            continue;
        }
        // Chaos can suppress very small kinematic rotation updates. Its query
        // geometry pose is therefore NOT necessarily the actor/render pose.
        // Express virtual sweeps in the actual physics pose, then map normals
        // back to the authoritative orbital pose. This also handles async lag.
        FTransform LivePose = Body->GetUnrealWorldTransform();
        LivePose.NormalizeRotation();
        // Cooked triangle vertices are float, even with double world transforms.
        // Four local-coordinate float epsilons bound their contact error. This
        // is a numerical collision skin, not a movement/gravity speed clamp.
        const double Skin = FMath::Max(0.01, double(B.PlanetRadius + B.TerrainHeight) *
            double(std::numeric_limits<float>::epsilon()) * 4.0);
        const FCollisionShape PlanetShape = FCollisionShape::MakeSphere(float(Shape.GetSphereRadius() + Skin));
        // Offsets already have world lengths (including authored uniform scale).
        const FVector QueryStart = LivePose.GetLocation() + LivePose.GetRotation().RotateVector(QS.UnrotateVector(Start - CS));
        const FVector QueryEnd = LivePose.GetLocation() + LivePose.GetRotation().RotateVector(QB.UnrotateVector(End - B.WorldPosition));
        // Broadphase before an expensive procedural triangle sweep.
        const double MeshEnvelope = Planet->PlanetProceduralMesh->Bounds.SphereRadius +
            FVector::Dist(Planet->PlanetProceduralMesh->Bounds.Origin, LivePose.GetLocation());
        const double Envelope = FMath::Max(double(B.PlanetRadius + B.TerrainHeight), MeshEnvelope) + Shape.GetSphereRadius();
        const FVector LocalStart = QueryStart - LivePose.GetLocation();
        const FVector LocalEnd = QueryEnd - LivePose.GetLocation();
        if (FMath::PointDistToSegment(FVector::ZeroVector, LocalStart, LocalEnd) > Envelope)
        {
            continue;
        }
        FHitResult Hit;
        if (Planet->PlanetProceduralMesh->SweepComponent(Hit, QueryStart, QueryEnd, FQuat::Identity, PlanetShape, true)
            && (!bHit || Hit.Time < OutHit.Time))
        {
            const double Fraction = StartFraction + (1.0 - StartFraction) * Hit.Time;
            const FQuat QHit = FQuat::Slerp(QA, QB, Fraction);
            const FVector Center = FMath::Lerp(A.WorldPosition, B.WorldPosition, Fraction);
            const FVector Position = FMath::Lerp(Start, End, double(Hit.Time));
            Hit.Normal = QHit.RotateVector(LivePose.GetRotation().UnrotateVector(Hit.Normal));
            Hit.ImpactNormal = QHit.RotateVector(LivePose.GetRotation().UnrotateVector(Hit.ImpactNormal));
            // These procedural planets are closed radial height fields. A
            // solid-body contact points outward, including initial overlaps;
            // a triangle's backface must not trap an ascending sphere inside.
            if ((Hit.Normal | (Position - Center)) < 0.0)
            {
                Hit.Normal *= -1.0;
                Hit.ImpactNormal *= -1.0;
            }
            Hit.Location = Position;
            Hit.ImpactPoint = Position - Hit.Normal * PlanetShape.GetSphereRadius();
            OutContactVelocity = FMath::Lerp(A.OrbitalVelocity, B.OrbitalVelocity, Fraction)
                + (B.AngularVelocity ^ (Hit.ImpactPoint - Center));
            OutHit = Hit;
            OutContactSkin = Skin;
            bHit = true;
        }
    }
    return bHit;
}

void UAndromedaPawnMovement::MoveWithMovingBodyCollision(double Dt,
    TConstArrayView<FPlanetRuntimeData> StartPlanets, TConstArrayView<FPlanetRuntimeData> EndPlanets, FVector& EndVelocity)
{
    double Fraction = 0.0;
    // Four sequential contact constraints (the usual bounded slide solver), not
    // a speed/integration cap. All orbital time is still simulated by Tick.
    for (int32 Contact = 0; Contact < 4 && Fraction < 1.0; ++Contact)
    {
        const double Remaining = Dt * (1.0 - Fraction);
        const FVector Start = UpdatedComponent->GetComponentLocation();
        const FVector Delta = Velocity * Remaining;
        FHitResult Hit;
        FVector ContactVelocity;
        double ContactSkin;
        if (!SweepMotion(Start, Start + Delta, StartPlanets, EndPlanets, Fraction, Hit, ContactVelocity, ContactSkin))
        {
            MoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), false);
            break;
        }
        MoveUpdatedComponent(Delta * Hit.Time, UpdatedComponent->GetComponentQuat(), false);
        Fraction += (1.0 - Fraction) * Hit.Time;
        if (Hit.bStartPenetrating || Hit.Time <= 1.e-6)
        {
            // Conventional contact MTD only, never a reference-frame correction.
            MoveUpdatedComponent(Hit.Normal * (FMath::Max(double(Hit.PenetrationDepth), 0.0) + ContactSkin), UpdatedComponent->GetComponentQuat(), false);
        }
        const double InwardSpeed = (Velocity - ContactVelocity) | Hit.Normal;
        if (InwardSpeed < 0.0)
        {
            Velocity -= Hit.Normal * InwardSpeed;
        }
        const double EndInwardSpeed = (EndVelocity - ContactVelocity) | Hit.Normal;
        if (EndInwardSpeed < 0.0)
        {
            EndVelocity -= Hit.Normal * EndInwardSpeed;
        }
        const double ControlInward = ControlledVelocity | Hit.Normal;
        if (ControlInward < 0.0)
        {
            ControlledVelocity -= Hit.Normal * ControlInward;
        }
        const bool bWalkable = Field.OrientationInfluence > 0.0 &&
            (Hit.Normal | Field.LocalUp) > FMath::Cos(FMath::DegreesToRadians(WalkableSlopeDegrees));
        bGrounded |= bWalkable;
        // Coulomb friction applies on ANY solid contact, not just walkable
        // ground: steep walls and light grazes otherwise preserve slide energy
        // losslessly (normal projection alone never touches the tangential
        // channel) and re-loft the pawn into long suborbital hops. The impulse
        // stays bounded by the normal force times tan(slope), so a brief
        // diagonal touch keeps most tangential momentum while sustained
        // sliding converges. Grounded state below remains walkable-gated, and
        // a high-speed arrival is never clamped to walking speed.
        if (EndInwardSpeed < 0.0)
        {
            const FVector Drift = FVector::VectorPlaneProject(EndVelocity - ContactVelocity - ControlledVelocity, Hit.Normal);
            const FVector Friction = (-Drift).GetClampedToMaxSize(-EndInwardSpeed * FMath::Tan(FMath::DegreesToRadians(WalkableSlopeDegrees)));
            EndVelocity += Friction;
            Velocity += Friction;
        }
        HandleImpact(Hit, float(Remaining), Delta);
    }
}
