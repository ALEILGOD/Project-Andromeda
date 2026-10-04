#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AndromedaPawn.h"
#include "AndromedaPlayerController.h"
#include "AndromedaGameMode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Planet/Planet.h"
#include "PlanetaryMotionMath.h"
#include "PlanetaryOrbitMath.h"
#include "PlanetaryWorldSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Sun.h"
#include "Tests/PlanetaryTestPlanet.h"

namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

    FPlanetRuntimeData MakePlanet(int64 ID = 0, FVector Center = FVector::ZeroVector)
    {
        FPlanetRuntimeData P;
        P.bValid = true;
        P.BodyType = ECelestialBodyType::Planet;
        P.PlanetID = ID;
        P.WorldPosition = Center;
        P.PlanetRadius = 500000.f;
        P.TerrainHeight = 20000.f;
        P.SurfaceGravity = 980.0;
        P.AtmosphereTopRadius = 550000.0;
        P.MaxInfluenceRadius = 1800000.0;
        return P;
    }

    struct FTestWorld
    {
        UWorld* World;
        FTestWorld()
        {
            const UWorld::InitializationValues IV = UWorld::InitializationValues()
                .AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false)
                .CreatePhysicsScene(true).ShouldSimulatePhysics(false).EnableTraceCollision(true);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
                ERHIFeatureLevel::SM5, &IV);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        }
        void Start()
        {
            World->InitializeActorsForPlay(FURL());
            World->GetWorldSettings()->NotifyBeginPlay();
        }
        void Tick(float Dt)
        {
            // TickTaskManager deduplicates by engine frame, not UWorld time.
            ++GFrameCounter;
            World->Tick(LEVELTICK_All, Dt);
        }
        ~FTestWorld()
        {
            World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetaryOrbitDerivativeTest, "Andromeda.PlanetaryMotion.OrbitalTruth", Flags)
bool FPlanetaryOrbitDerivativeTest::RunTest(const FString& Parameters)
{
    FPlanetGenerationData P;
    P.OrbitDistance = 3500000.f;
    P.OrbitalPeriod = 30.f;
    P.OrbitAngle = 127.f;
    P.OrbitInclination = -24.f;
    for (double Scale : {0.0, 0.25, 1.0, 8.0})
    {
        for (double Time : {0.0, 3600.0, 86400.0 * 30.0})
        {
            constexpr double Dt = 0.001;
            const auto A = AndromedaOrbit::Evaluate(P, Time - Dt * Scale, 0.05, Scale);
            const auto B = AndromedaOrbit::Evaluate(P, Time + Dt * Scale, 0.05, Scale);
            const auto M = AndromedaOrbit::Evaluate(P, Time, 0.05, Scale);
            TestTrue(TEXT("Position derivative agrees at every time scale, including long sessions"),
                ((B.Position - A.Position) / (2.0 * Dt) - M.Velocity).Size() < FMath::Max(0.1, M.Velocity.Size() * 1.e-6));
            TestTrue(TEXT("Velocity derivative agrees with orbital acceleration"),
                ((B.Velocity - A.Velocity) / (2.0 * Dt) - M.Acceleration).Size() < FMath::Max(0.01, M.Acceleration.Size() * 1.e-6));
            TestTrue(TEXT("Circular solution preserves orbit radius"), FMath::Abs(M.Position.Size() - P.OrbitDistance) < 1.e-6);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetaryFieldContinuityTest, "Andromeda.PlanetaryMotion.FieldContinuity", Flags)
bool FPlanetaryFieldContinuityTest::RunTest(const FString& Parameters)
{
    const FPlanetaryMotionSettings Settings;
    const FPlanetRuntimeData P = MakePlanet();
    const TArray<FPlanetRuntimeData> Bodies = {P};
    const auto Sample = [&](double R) { return UPlanetaryGravitySystem::Sample(Bodies, FVector(R, 0, 0), FVector::ZeroVector, Settings); };
    TestEqual(TEXT("Deep space has no gravity or frame"), Sample(5000000).ReferenceInfluence, 0.0);
    TestEqual(TEXT("Outer boundary has zero acceleration"), Sample(P.MaxInfluenceRadius).GravityAcceleration.Size(), 0.0);
    TestTrue(TEXT("Gravity begins in space, before the atmosphere"), Sample(1200000).GravityAcceleration.Size() > 0.0);
    TestEqual(TEXT("Fully planet-relative by atmosphere entry"), Sample(P.AtmosphereTopRadius).ReferenceInfluence, 1.0);
    TestTrue(TEXT("Planet-specific surface acceleration"), FMath::Abs(Sample(P.PlanetRadius).GravityAcceleration.Size() - 980.0) < 1.e-6);
    TestTrue(TEXT("Inverse square below taper"), FMath::Abs(Sample(700000).GravityAcceleration.Size() - 980.0 * FMath::Square(500000.0 / 700000.0)) < 1.e-6);
    TestTrue(TEXT("No hard outer-shell force jump"), Sample(P.MaxInfluenceRadius - 1.0).GravityAcceleration.Size() < 1.e-9);
    TestTrue(TEXT("No atmosphere-entry change in acceleration"),
        (Sample(P.AtmosphereTopRadius - 1.0).GravityAcceleration - Sample(P.AtmosphereTopRadius + 1.0).GravityAcceleration).Size() < 0.01);
    for (const FVector Up : {FVector::UpVector, -FVector::UpVector, FVector::ForwardVector, -FVector::ForwardVector,
        FVector::RightVector, FVector(0.2, -0.7, 0.4).GetSafeNormal()})
    {
        const auto F = UPlanetaryGravitySystem::Sample(Bodies, Up * 520000.0, FVector::ZeroVector, Settings);
        TestTrue(TEXT("Every hemisphere uses radial down"), (F.GravityAcceleration.GetSafeNormal() | -Up) > 1.0 - 1.e-10);
    }
    TestTrue(TEXT("Center is nonsingular"), Sample(0).GravityAcceleration.IsNearlyZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetaryStarSelectionTest, "Andromeda.PlanetaryMotion.StarsAndOverlap", Flags)
bool FPlanetaryStarSelectionTest::RunTest(const FString& Parameters)
{
    const FPlanetaryMotionSettings Settings;
    FPlanetRuntimeData Star = MakePlanet(99);
    Star.BodyType = ECelestialBodyType::Star;
    Star.PlanetRadius = 1.e9f;
    Star.MaxInfluenceRadius = 1.e10;
    FPlanetRuntimeData A = MakePlanet(0, FVector(-1250000, 0, 0));
    FPlanetRuntimeData B = MakePlanet(1, FVector(1250000, 0, 0));
    A.OrbitalVelocity = FVector(0, 50000, 0);
    B.OrbitalVelocity = FVector(0, -50000, 0);
    const TArray<FPlanetRuntimeData> Stars = {Star};
    TestEqual(TEXT("A star cannot contribute a reference frame"), UPlanetaryGravitySystem::Sample(Stars, FVector(10, 20, 30), FVector::ZeroVector, Settings).ReferenceInfluence, 0.0);
    TestTrue(TEXT("A star cannot contribute gravity"), UPlanetaryGravitySystem::Sample(Stars, FVector(10, 20, 30), FVector::ZeroVector, Settings).GravityAcceleration.IsNearlyZero());
    const TArray<FPlanetRuntimeData> Bodies = {A, Star, B};
    const auto L = UPlanetaryGravitySystem::Sample(Bodies, FVector(-0.1, 0, 0), FVector::ZeroVector, Settings);
    const auto R = UPlanetaryGravitySystem::Sample(Bodies, FVector(0.1, 0, 0), FVector::ZeroVector, Settings);
    TestTrue(TEXT("Diagnostic dominant planet changes without a frame-velocity jump"), (L.FrameVelocity - R.FrameVelocity).Size() < 0.1);
    TestTrue(TEXT("Forces remain continuous at equal influence"), (L.GravityAcceleration - R.GravityAcceleration).Size() < 0.01);
    const auto Center = UPlanetaryGravitySystem::Sample(Bodies, FVector::ZeroVector, FVector::ZeroVector, Settings);
    TestEqual(TEXT("Opposing horizons fade alignment instead of flipping"), Center.OrientationInfluence, 0.0);
    Star.BodyType = ECelestialBodyType::Unknown;
    TestFalse(TEXT("Unclassified runtime data cannot become a planet"), UPlanetaryGravitySystem::IsSurfaceGravityBody(Star));
    UClass* PlanetBP = LoadClass<AActor>(nullptr, TEXT("/Game/Blueprints/Planets/BP_Planet.BP_Planet_C"));
    TestTrue(TEXT("Existing project planet Blueprint obeys the typed surface-body contract"), PlanetBP && PlanetBP->IsChildOf(APlanet::StaticClass()));
    const auto* GameMode = GetDefault<AAndromedaGameMode>();
    TestTrue(TEXT("Default game mode uses local-frame controller"), GameMode->PlayerControllerClass == AAndromedaPlayerController::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetaryMomentumTest, "Andromeda.PlanetaryMotion.MomentumAndDeparture", Flags)
bool FPlanetaryMomentumTest::RunTest(const FString& Parameters)
{
    const FVector Incoming(1.e8, -2.e7, 3.e7);
    const FVector PlanetVelocity(12000, 60000, -5000);
    FVector Carrier = FVector::ZeroVector;
    FVector Velocity = Incoming;
    double Previous = 0.0;
    for (int32 I = 1; I <= 1000; ++I)
    {
        const double Weight = PlanetaryMotion::SmoothStep(double(I) / 1000);
        const FVector Next = PlanetaryMotion::AdvanceCarrier(Carrier, PlanetVelocity, FVector::ZeroVector, Weight, Previous, 0.5, 0.0001);
        Velocity += Next - Carrier;
        Carrier = Next;
        Previous = Weight;
        TestTrue(TEXT("High incoming momentum is untouched by capture"), (Velocity - Carrier - Incoming).Size() < 1.e-5);
    }
    TestTrue(TEXT("Frame inheritance actually acquires the planet's velocity (not a cancelling rebase)"), (Carrier - PlanetVelocity).Size() < 1.e-6);
    const FVector DepartureVelocity = Velocity;
    for (int32 I = 999; I >= 0; --I)
    {
        const double Weight = PlanetaryMotion::SmoothStep(double(I) / 1000);
        const FVector Next = PlanetaryMotion::AdvanceCarrier(Carrier, PlanetVelocity, FVector::ZeroVector, Weight, Previous, 0.5, 0.0001);
        Velocity += Next - Carrier;
        Carrier = Next;
        Previous = Weight;
    }
    TestTrue(TEXT("Departure retains the acquired inertial/orbital momentum"), (Velocity - DepartureVelocity).Size() < 1.e-6);
    const FVector Coasting = PlanetaryMotion::AdvanceCarrier(Carrier, FVector::ZeroVector, FVector(100, 200, 300), 0, 0, 0.5, 30);
    TestTrue(TEXT("Deep space does not erase a departing carrier"), Coasting.Equals(Carrier, 1.e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetaryAdaptiveStepTest, "Andromeda.PlanetaryMotion.HighSpeedSampling", Flags)
bool FPlanetaryAdaptiveStepTest::RunTest(const FString& Parameters)
{
    const FPlanetaryMotionSettings Settings;
    const TArray<FPlanetRuntimeData> Bodies = {MakePlanet()};
    for (double Speed : {1200.0, 1.e6, 1.e8, 1.e10})
    {
        FVector Position(-10000000, 0, 300000);
        const FVector Velocity(Speed, 0, 0);
        double Time = 0.0;
        double MaxWeight = 0.0;
        int32 Steps = 0;
        const double Duration = 20000000.0 / Speed;
        while (Time < Duration - 1.e-10)
        {
            const double Dt = UPlanetaryGravitySystem::ChooseSimulationStep(Bodies, Position, Velocity, Duration - Time, Settings);
            const auto F = UPlanetaryGravitySystem::Sample(Bodies, Position + Velocity * (Dt * 0.5), Velocity, Settings);
            MaxWeight = FMath::Max(MaxWeight, F.ReferenceInfluence);
            Position += Velocity * Dt;
            Time += Dt;
            ++Steps;
        }
        TestTrue(TEXT("Even a one-frame high-speed flyby samples the complete influence field"), MaxWeight > 0.99);
        TestTrue(TEXT("Substeps consume elapsed time without speed or position clamping"), (Position - FVector(10000000, 0, 300000)).Size() < 1.0);
        AddInfo(FString::Printf(TEXT("speed=%.0f cm/s steps=%d maxInfluence=%.6f"), Speed, Steps, MaxWeight));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetaryNavigationTest, "Andromeda.PlanetaryMotion.HorizonAndLook", Flags)
bool FPlanetaryNavigationTest::RunTest(const FString& Parameters)
{
    FTestWorld Fixture;
    AAndromedaPawn* Pawn = Fixture.World->SpawnActor<AAndromedaPawn>();
    AAndromedaPlayerController* Controller = Fixture.World->SpawnActor<AAndromedaPlayerController>();
    Controller->Possess(Pawn);
    Fixture.Start();
    FPlanetaryFieldSample Field;
    Field.ReferenceInfluence = Field.OrientationInfluence = Field.SurfaceInfluence = 1.0;
    Field.LocalUp = -FVector::UpVector;
    for (int32 I = 0; I < 300; ++I) { Pawn->TransportNavigation(Field, 1.0 / 120); }
    TestTrue(TEXT("Stable acquisition at the exact antipode"), (Pawn->GetPlanetaryUp() | Field.LocalUp) > 1.0 - 1.e-8);
    Pawn->ApplyLocalLook(FRotator(30, 17, 0));
    double MaxStep = 0.0;
    FQuat PreviousView = Pawn->GetViewRotation().Quaternion();
    for (int32 I = 0; I <= 4000; ++I)
    {
        // Two great circles, including both poles and the opposite hemisphere.
        const double T = double(I) * 4.0 * UE_DOUBLE_PI / 4000.0;
        Field.LocalUp = FVector(FMath::Sin(T), 0, -FMath::Cos(T));
        Pawn->TransportNavigation(Field, 1.0 / 120);
        Pawn->PublishView();
        const FQuat View = Pawn->GetViewRotation().Quaternion();
        MaxStep = FMath::Max(MaxStep, PreviousView.AngularDistance(View));
        TestTrue(TEXT("Local look pitch survives curvature without global Euler clamps"), FMath::Abs((View.GetAxisX() | Field.LocalUp) - 0.5) < 1.e-6);
        TestTrue(TEXT("Navigation up follows all latitudes"), (Pawn->GetPlanetaryUp() | Field.LocalUp) > 1.0 - 1.e-8);
        // AngularDistance(q,q) can report ~1e-7 radians from the rounded dot
        // product alone. Compare the published rotator exactly instead.
        TestTrue(TEXT("Controller and camera share one quaternion result"), Controller->GetControlRotation().Equals(Pawn->GetViewRotation(), 1.e-10));
        PreviousView = View;
    }
    TestTrue(TEXT("No horizon flip over poles"), MaxStep < 0.01);
    const FQuat BeforeExit = Pawn->GetNavigationQuat();
    Pawn->TransportNavigation(FPlanetaryFieldSample(), 1.0);
    TestTrue(TEXT("Leaving influence keeps the last inertial horizon"), BeforeExit.AngularDistance(Pawn->GetNavigationQuat()) < 1.e-8);
    const FQuat Opposite = PlanetaryMotion::Swing(FVector::UpVector, -FVector::UpVector, FVector::ForwardVector);
    TestTrue(TEXT("Antipodal swing remains normalized and finite"), Opposite.IsNormalized() && !Opposite.ContainsNaN());
    Pawn->MoveForward(1.f);
    TestTrue(TEXT("Surface forward remains tangent while looking up"), FMath::Abs(Pawn->GetMovementIntent(1.0) | Pawn->GetPlanetaryUp()) < 1.e-8);
    Pawn->MoveForward(0.f);
    Pawn->MoveUp_World(1.f);
    TestTrue(TEXT("Vertical intent is local up even on the southern hemisphere"), Pawn->GetMovementIntent(1.0).Equals(Pawn->GetPlanetaryUp(), 1.e-8));
    Pawn->MoveUp_World(0.f);
    for (double Weight : {0.0, 0.25, 0.5, 0.75, 1.0})
    {
        Pawn->MoveForward(1.f);
        const FVector Forward = Pawn->GetMovementIntent(Weight);
        Pawn->MoveForward(0.f);
        Pawn->MoveUp_World(1.f);
        const FVector Up = Pawn->GetMovementIntent(Weight);
        Pawn->MoveUp_World(0.f);
        TestTrue(TEXT("Movement basis remains orthonormal across free flight / surface transitions"), FMath::Abs(Forward | Up) < 1.e-10);
    }
    Controller->SetControlRotation(FRotator(10, 120, 25));
    TestTrue(TEXT("Explicit debug/cinematic look commands still reach the camera"),
        Pawn->GetViewRotation().Quaternion().AngularDistance(FRotator(10, 120, 25).Quaternion()) < 1.e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetaryRuntimeTest, "Andromeda.PlanetaryMotion.RuntimeOrbitAndSurface", Flags)
bool FPlanetaryRuntimeTest::RunTest(const FString& Parameters)
{
    FTestWorld Fixture;
    // The test subclass only reduces resolution; geometry/collision code is real.
    AStarSystem* System = Fixture.World->SpawnActor<AStarSystem>();
    System->SetActorLocationAndRotation(FVector(25000000, -37000000, 14000000), FRotator(12, 37, -7));
    System->PlanetClass = APlanetaryTestPlanet::StaticClass();
    System->SunClass = ASun::StaticClass();
    System->SimulationTimeScale = 2.f;
    AAndromedaPawn* Pawn = Fixture.World->SpawnActor<AAndromedaPawn>();
    Fixture.Start();
    TArray<FPlanetRuntimeData> Planets;
    System->GetAllPlanetRuntimeData(Planets);
    if (!TestTrue(TEXT("Real generated runtime planets exist"), Planets.Num() >= 3)) { return false; }
    TestTrue(TEXT("Native star systems have a real translated/rotated system origin"), System->GetActorLocation().Equals(FVector(25000000, -37000000, 14000000)));
    for (const auto& P : Planets)
    {
        TestTrue(TEXT("Runtime query classifies only valid surface planets"), UPlanetaryGravitySystem::IsSurfaceGravityBody(P));
        TestTrue(TEXT("Orbital clearance leaves influence beginning before atmosphere"), P.MaxInfluenceRadius > P.AtmosphereTopRadius);
        TestTrue(TEXT("Query orbital velocity matches public orbital truth"), P.OrbitalVelocity.Equals(System->GetPlanetOrbitalVelocity(P.PlanetID), 1.e-9));
    }
    auto* Movement = CastChecked<UAndromedaPawnMovement>(Pawn->GetMovementComponent());
    // Choose an actual triangle intersection, not the terrain-height envelope.
    APlanet* Planet = CastChecked<APlanet>(Planets[0].PlanetActor);
    const FVector InitialUp = FVector(0.3, 0.8, -0.4).GetSafeNormal();
    FHitResult Ground;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PlanetaryTest), true);
    if (!TestTrue(TEXT("Procedural planetary collision is available"), Planet->PlanetProceduralMesh->LineTraceComponent(Ground,
        Planet->GetActorLocation() + InitialUp * (Planets[0].PlanetRadius * 2.0), Planet->GetActorLocation(), Query))) { return false; }
    Pawn->SetActorLocation(Ground.ImpactPoint + Ground.ImpactNormal * 40.0);
    AddInfo(FString::Printf(TEXT("Initial ground normal dot radial up=%.6f surfaceRadius=%.3f poseError=%.9f"),
        Ground.ImpactNormal | InitialUp, FVector::Dist(Ground.ImpactPoint, Planet->GetActorLocation()),
        Planets[0].CurrentRotation.Quaternion().AngularDistance(Planet->GetActorQuat())));
    FHitResult InitialSweep;
    const bool bInitialSweep = Planet->PlanetProceduralMesh->SweepComponent(InitialSweep, Pawn->GetActorLocation(),
        Pawn->GetActorLocation() - InitialUp * 100.0, FQuat::Identity, FCollisionShape::MakeSphere(35.f), true);
    AddInfo(FString::Printf(TEXT("Initial sphere support sweep hit=%d normalDot=%.6f time=%.6f"), bInitialSweep, InitialSweep.Normal | InitialUp, InitialSweep.Time));
    const FVector InitialFrameVelocity = Planets[0].OrbitalVelocity +
        (Planets[0].AngularVelocity ^ (Pawn->GetActorLocation() - Planets[0].WorldPosition));
    Movement->InitializeMotion(InitialFrameVelocity, InitialFrameVelocity);
    const FVector StartLocal = Planet->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
    const FVector OldCenter = Planet->GetActorLocation();
    for (int32 I = 0; I < 360; ++I) { Fixture.Tick(1.f / 120.f); }
    const FVector EndLocal = Planet->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
    const FTransform PhysicsPose = Planet->PlanetProceduralMesh->GetBodyInstance()->GetUnrealWorldTransform();
    AddInfo(FString::Printf(TEXT("Physics/component pose offset=%.6f rotationOffset=%.9f worldRadius=%.3f"),
        FVector::Dist(PhysicsPose.GetLocation(), Planet->GetActorLocation()), PhysicsPose.GetRotation().GetNormalized().AngularDistance(Planet->GetActorQuat()),
        FVector::Dist(Pawn->GetActorLocation(), Planet->GetActorLocation())));
    TestTrue(TEXT("Planet really orbited during the test"), FVector::Dist(OldCenter, Planet->GetActorLocation()) > 1000.0);
    TestTrue(TEXT("Player remains on the moving spherical surface without orbital position corrections"), FVector::Dist(StartLocal, EndLocal) < 2000.0);
    TestTrue(TEXT("World velocity follows the orbit rather than the old cancelling rebase"), Movement->Velocity.Size() > 1000.0);
    TestTrue(TEXT("Real moving geometry supplies surface contact"), Movement->IsGrounded());
    TestTrue(TEXT("Grounded orientation uses the planet, not the star"),
        (Pawn->GetPlanetaryUp() | (Pawn->GetActorLocation() - Planet->GetActorLocation()).GetSafeNormal()) > 0.99);
    TestTrue(TEXT("All runtime motion remains finite"), PlanetaryMotion::IsFinite(Movement->Velocity) && PlanetaryMotion::IsFinite(Pawn->GetActorLocation()));
    AddInfo(FString::Printf(TEXT("3 second moving-surface local drift=%.3f cm worldSpeed=%.3f cm/s grounded=%d"),
        FVector::Dist(StartLocal, EndLocal), Movement->Velocity.Size(), Movement->IsGrounded()));

    const FVector BeforeWalking = Planet->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
    Pawn->MoveForward(1.f);
    Pawn->ApplyLocalLook(FRotator(30, 0, 0));
    for (int32 I = 0; I < 360; ++I) { Fixture.Tick(1.f / 120.f); }
    Pawn->MoveForward(0.f);
    Pawn->ApplyLocalLook(FRotator(-30, 0, 0));
    TestTrue(TEXT("Forward input walks the real moving surface while camera look remains independent"),
        FVector::Dist(BeforeWalking, Planet->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation())) > 1000.0);

    // Sustained, powered ascent through atmosphere and out of the field, using
    // the SAME input and integrator. Test speed is 1 km/s; production is authored.
    Movement->BaseVelocity = 100000.f;
    Movement->Acceleration = 400000.f;
    const double StartRadius = (Pawn->GetActorLocation() - Planet->GetActorLocation()).Size();
    Pawn->MoveUp_World(1.f);
    for (int32 I = 0; I < 2400; ++I) { Fixture.Tick(1.f / 120.f); }
    AddInfo(FString::Printf(TEXT("Ascent startRadius=%.3f endRadius=%.3f influence=%.6f upAlignment=%.6f worldSpeed=%.3f"),
        StartRadius, FVector::Dist(Pawn->GetActorLocation(), Planet->GetActorLocation()), Movement->GetField().ReferenceInfluence,
        Pawn->GetPlanetaryUp() | Movement->GetField().LocalUp, Movement->Velocity.Size()));
    TestTrue(TEXT("Held local ascent remains powered instead of losing to accumulated gravity"),
        (Pawn->GetActorLocation() - Planet->GetActorLocation()).Size() > StartRadius + 1000000.0);
    TestTrue(TEXT("Ascent exits the atmosphere and planetary field"), Movement->GetField().ReferenceInfluence == 0.0);
    Pawn->MoveUp_World(0.f);
    const FVector InertialVelocity = Movement->Velocity;
    const FQuat InertialHorizon = Pawn->GetNavigationQuat();
    for (int32 I = 0; I < 120; ++I) { Fixture.Tick(1.f / 120.f); }
    TestTrue(TEXT("Space input release retains the complete inherited momentum"), (Movement->Velocity - InertialVelocity).Size() < 1.e-5);
    TestTrue(TEXT("Space camera retains its exiting horizon"), InertialHorizon.AngularDistance(Pawn->GetNavigationQuat()) < 1.e-7);

    // Actual high-speed descent + swept collision (not just field quadrature).
    const FPlanetRuntimeData Current = System->GetPlanetRuntimeData(Planets[0].PlanetID);
    const FVector ApproachUp = InitialUp;
    const double Outer = UPlanetaryGravitySystem::GetOuterRadius(Current, Movement->Settings);
    const FVector ApproachPosition = Current.WorldPosition + ApproachUp * (Outer + Current.PlanetRadius * 0.5);
    AAndromedaPawn* IncomingPawn = Fixture.World->SpawnActor<AAndromedaPawn>(ApproachPosition, FRotator::ZeroRotator);
    auto* IncomingMovement = CastChecked<UAndromedaPawnMovement>(IncomingPawn->GetMovementComponent());
    IncomingMovement->InitializeMotion(Current.OrbitalVelocity - ApproachUp * 1.e7, Current.OrbitalVelocity);
    double MinimumRadius = TNumericLimits<double>::Max();
    double MaximumInfluence = 0.0;
    double EntrySpeed = 0.0;
    for (int32 I = 0; I < 28; ++I)
    {
        IncomingPawn->ApplyLocalLook(FRotator(0.1, 0.25, 0));
        Fixture.Tick(1.f / 120.f);
        MinimumRadius = FMath::Min(MinimumRadius, FVector::Dist(IncomingPawn->GetActorLocation(), Planet->GetActorLocation()));
        MaximumInfluence = FMath::Max(MaximumInfluence, IncomingMovement->GetField().ReferenceInfluence);
        if (IncomingMovement->GetField().ReferenceInfluence > 0.1 && !IncomingMovement->IsGrounded())
        {
            EntrySpeed = FMath::Max(EntrySpeed, IncomingMovement->Velocity.Size());
        }
        TestTrue(TEXT("Look and orientation stay finite during high-speed descent and collision"), !IncomingPawn->GetViewRotation().ContainsNaN());
    }
    TestTrue(TEXT("High-speed descent actually enters the full planetary field"), MaximumInfluence > 0.99);
    TestTrue(TEXT("Frame capture does not cap high-speed inertial approach"), EntrySpeed > 9.e6);
    TestTrue(TEXT("Moving-body sweep prevents tunneling through the procedural planet"), MinimumRadius > Current.PlanetRadius - Current.TerrainHeight - 100.0);
    AddInfo(FString::Printf(TEXT("100 km/s descent: minRadius=%.3f cm maxInfluence=%.6f entrySpeed=%.3f cm/s"), MinimumRadius, MaximumInfluence, EntrySpeed));
    // Registry removal must not dereference destroyed systems or erase inertia.
    const FVector BeforeRemoval = Movement->Velocity;
    System->Destroy();
    Fixture.Tick(1.f / 120.f);
    TestTrue(TEXT("Removing a system returns continuously to inertial motion"), (Movement->Velocity - BeforeRemoval).Size() < 1.e-5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetaryCounterThrustTest, "Andromeda.PlanetaryMotion.InertialPilotControl", Flags)
bool FPlanetaryCounterThrustTest::RunTest(const FString& Parameters)
{
    FTestWorld Fixture;
    AAndromedaPawn* Pawn = Fixture.World->SpawnActor<AAndromedaPawn>(FVector(1000000, 0, 0), FRotator::ZeroRotator);
    Fixture.Start();
    auto* Movement = CastChecked<UAndromedaPawnMovement>(Pawn->GetMovementComponent());
    const FVector Incoming(100000, -45000, 35000);
    // Simulate a departing frame's inherited motion as initial conditions.
    Movement->InitializeMotion(Incoming, Incoming);
    Pawn->MoveForward(-1.f);
    for (int32 I = 0; I < 120; ++I) { Fixture.Tick(1.f / 120.f); }
    // Deep space: the space cruise regime (default 1000000 cm/s) engages and
    // thrust scales with it, so 1 s of full reverse thrust drives a large
    // negative X velocity while leaving orthogonal drift bit-exact.
    TestTrue(TEXT("Space cruise engages in deep space"),
        Movement->Velocity.X < -500000.0 && Movement->Velocity.Size() > 900000.0);
    TestTrue(TEXT("Counter-thrust does not destroy orthogonal momentum"),
        FMath::Abs(Movement->Velocity.Y - Incoming.Y) < 1.e-8 &&
        FMath::Abs(Movement->Velocity.Z - Incoming.Z) < 1.e-8);
    Pawn->MoveForward(0.f);
    const FVector Coasting = Movement->Velocity;
    for (int32 I = 0; I < 120; ++I) { Fixture.Tick(1.f / 120.f); }
    TestTrue(TEXT("Releasing space controls resumes inertia without automatic braking"), Movement->Velocity.Equals(Coasting, 1.e-8));
    Movement->InitializeMotion(FVector::ZeroVector, FVector::ZeroVector);
    TestTrue(TEXT("Spawner initial-condition API cannot snap an already simulated velocity"), Movement->Velocity.Equals(Coasting, 1.e-8));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetaryReferenceRangeTest, "Andromeda.PlanetaryMotion.ReferenceRangeAndBaseVelocity", Flags)
bool FPlanetaryReferenceRangeTest::RunTest(const FString& Parameters)
{
    const FPlanetaryMotionSettings Settings;
    // Open-space fixture: clearance far beyond both natural radii, so the
    // natural reference extension (not the safety cap) governs.
    FPlanetRuntimeData P = MakePlanet();
    P.MaxInfluenceRadius = 1.e8;
    const TArray<FPlanetRuntimeData> Bodies = {P};
    const double GravityOuter = UPlanetaryGravitySystem::GetOuterRadius(P, Settings);
    const double ReferenceOuter = UPlanetaryGravitySystem::GetReferenceOuterRadius(P, Settings);
    TestTrue(TEXT("Gravity outer radius is unchanged (10R natural)"), FMath::Abs(GravityOuter - 5000000.0) < 1.0);
    TestTrue(TEXT("Reference outer radius hugs the atmosphere (550km envelope + 10% margin)"),
        FMath::Abs(ReferenceOuter - 605000.0) < 1.0);
    TestTrue(TEXT("Reference range is dramatically smaller than gravity range"), ReferenceOuter < GravityOuter);
    const auto InnerField = UPlanetaryGravitySystem::Sample(Bodies, FVector(580000, 0, 0), FVector::ZeroVector, Settings);
    TestTrue(TEXT("Reference influence engages just outside the atmosphere"), InnerField.ReferenceInfluence > 0.0);
    const auto AboveField = UPlanetaryGravitySystem::Sample(Bodies, FVector(1000000, 0, 0), FVector::ZeroVector, Settings);
    TestEqual(TEXT("No reference influence above the margin shell"), AboveField.ReferenceInfluence, 0.0);
    TestTrue(TEXT("Gravity still reaches far beyond the reference shell (unchanged)"),
        AboveField.GravityAcceleration.Size() > 0.0);
    const auto FarField = UPlanetaryGravitySystem::Sample(Bodies, FVector(605000, 0, 0), FVector::ZeroVector, Settings);
    TestEqual(TEXT("Zero reference influence at the outer boundary"), FarField.ReferenceInfluence, 0.0);
    const auto SurfaceField = UPlanetaryGravitySystem::Sample(Bodies, FVector(520000, 0, 0), FVector::ZeroVector, Settings);
    TestEqual(TEXT("Full reference influence near the surface"), SurfaceField.ReferenceInfluence, 1.0);
    // Stars stay excluded at any range.
    FPlanetRuntimeData Star = MakePlanet(99);
    Star.BodyType = ECelestialBodyType::Star;
    Star.PlanetRadius = 1.e9f;
    Star.MaxInfluenceRadius = 1.e10;
    const TArray<FPlanetRuntimeData> Stars = {Star};
    TestEqual(TEXT("A star cannot gain reference influence at extended range"),
        UPlanetaryGravitySystem::Sample(Stars, FVector(50000000, 0, 0), FVector::ZeroVector, Settings).ReferenceInfluence, 0.0);

    // BaseVelocity: canonical cruise speed, separate from Velocity state.
    FTestWorld Fixture;
    AAndromedaPawn* Pawn = Fixture.World->SpawnActor<AAndromedaPawn>(FVector(1000000, 0, 0), FRotator::ZeroRotator);
    Fixture.Start();
    auto* Movement = CastChecked<UAndromedaPawnMovement>(Pawn->GetMovementComponent());
    TestEqual(TEXT("Default base velocity"), Pawn->GetBaseVelocity(), 1200.f);
    TestEqual(TEXT("Engine max-speed API follows base velocity"), Movement->GetMaxSpeed(), 1200.f);
    Pawn->SetBaseVelocity(100000.f);
    TestEqual(TEXT("Pawn setter drives the movement channel"), Movement->BaseVelocity, 100000.f);
    TestEqual(TEXT("Engine max-speed API follows the new base velocity"), Movement->GetMaxSpeed(), 100000.f);
    Pawn->SetBaseVelocity(-5.f);
    TestEqual(TEXT("Base velocity clamps at zero, never negative"), Pawn->GetBaseVelocity(), 0.f);
    Pawn->SetBaseVelocity(1200.f);

    // Space cruise: orders of magnitude above planetary, geometrically
    // blended by the existing reference influence (0 = space, 1 = surface).
    TestEqual(TEXT("Default space cruise velocity"), Pawn->GetSpaceBaseVelocity(), 1000000.f);
    TestTrue(TEXT("Full space speed in deep space"),
        FMath::Abs(PlanetaryMotion::BlendCruiseSpeed(1200.0, 1000000.0, 0.0) - 1000000.0) < 1.0);
    TestEqual(TEXT("Planetary speed at full influence"),
        PlanetaryMotion::BlendCruiseSpeed(1200.0, 1000000.0, 1.0), 1200.0);
    const double MidCruise = PlanetaryMotion::BlendCruiseSpeed(1200.0, 1000000.0, 0.5);
    TestTrue(TEXT("Geometric midpoint, not collapsed to either endpoint"),
        MidCruise > 12000.0 && MidCruise < 100000.0);
    TestEqual(TEXT("Disabled space speed falls back to planetary everywhere"),
        PlanetaryMotion::BlendCruiseSpeed(1200.0, 0.0, 0.0), 1200.0);
    Pawn->SetSpaceBaseVelocity(5000000.f);
    TestEqual(TEXT("Space cruise is configurable"), Pawn->GetSpaceBaseVelocity(), 5000000.f);
    Pawn->SetSpaceBaseVelocity(-5.f);
    TestEqual(TEXT("Space cruise clamps at zero"), Pawn->GetSpaceBaseVelocity(), 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetaryImpactSettlesTest, "Andromeda.PlanetaryMotion.ImpactSettles", Flags)
bool FPlanetaryImpactSettlesTest::RunTest(const FString& Parameters)
{
    FTestWorld Fixture;
    AStarSystem* System = Fixture.World->SpawnActor<AStarSystem>();
    System->PlanetClass = APlanetaryTestPlanet::StaticClass();
    System->SunClass = ASun::StaticClass();
    System->SimulationTimeScale = 2.f;
    AAndromedaPawn* DropPawn = Fixture.World->SpawnActor<AAndromedaPawn>();
    AAndromedaPawn* PressPawn = Fixture.World->SpawnActor<AAndromedaPawn>();
    Fixture.Start();
    TArray<FPlanetRuntimeData> Planets;
    System->GetAllPlanetRuntimeData(Planets);
    if (!TestTrue(TEXT("Planets exist"), Planets.Num() >= 1)) { return false; }
    auto* DropMovement = CastChecked<UAndromedaPawnMovement>(DropPawn->GetMovementComponent());
    auto* PressMovement = CastChecked<UAndromedaPawnMovement>(PressPawn->GetMovementComponent());

    // Radial 30 km/s drop, no input: inward fully absorbed, no rebound, rest.
    {
        const double Envelope = Planets[0].PlanetRadius + Planets[0].TerrainHeight;
        const FVector Up = FVector(0.3, 0.8, -0.4).GetSafeNormal();
        DropPawn->SetActorLocation(Planets[0].WorldPosition + Up * (Envelope + 300000.0));
        DropMovement->InitializeMotion(Planets[0].OrbitalVelocity - Up * 3000000.0, Planets[0].OrbitalVelocity);
        bool bContact = false;
        double MaxOutward = 0.0;
        for (int32 I = 0; I < 400; ++I)
        {
            Fixture.Tick(1.f / 120.f);
            System->GetAllPlanetRuntimeData(Planets);
            const FVector ToPawn = DropPawn->GetActorLocation() - Planets[0].WorldPosition;
            const double Dist = ToPawn.Size();
            const FVector Radial = ToPawn / FMath::Max(Dist, 1.0);
            const double Height = Dist - Envelope;
            if (!bContact && Height < 100.0) { bContact = true; }
            if (bContact)
            {
                MaxOutward = FMath::Max(MaxOutward, ((DropMovement->Velocity - Planets[0].OrbitalVelocity) | Radial));
            }
        }
        System->GetAllPlanetRuntimeData(Planets);
        const FVector FinalRel = DropMovement->Velocity - Planets[0].OrbitalVelocity;
        AddInfo(FString::Printf(TEXT("Radial impact: maxOutward=%.1f (impact 3000000 inward) finalRelSpeed=%.1f grounded=%d"),
            MaxOutward, FinalRel.Size(), DropMovement->IsGrounded()));
        TestTrue(TEXT("Impact happened"), bContact);
        TestTrue(TEXT("No outward rebound from a radial impact"), MaxOutward < 150000.0);
        TestTrue(TEXT("Radial arrival comes to rest"), FinalRel.Size() < 20000.0);
    }

    // Flown homing press into the planet, then release: hops must decay and
    // release must settle (Coulomb friction on every contact dissipates slide
    // energy; normal projection is fully inelastic throughout).
    {
        const double Envelope = Planets[0].PlanetRadius + Planets[0].TerrainHeight;
        const FVector StartUp = FVector(-0.4, 0.7, 0.3).GetSafeNormal();
        PressPawn->SetActorLocation(Planets[0].WorldPosition + StartUp * (Envelope + 300000.0));
        PressMovement->InitializeMotion(Planets[0].OrbitalVelocity, Planets[0].OrbitalVelocity);
        bool bContact = false;
        double MaxOutward = -1e18;
        for (int32 I = 0; I < 400; ++I)
        {
            System->GetAllPlanetRuntimeData(Planets);
            const FVector ToPlanet = (Planets[0].WorldPosition - PressPawn->GetActorLocation()).GetSafeNormal();
            FVector Right = FVector::CrossProduct(ToPlanet, FVector(0, 0, 1));
            if (Right.IsNearlyZero()) { Right = FVector(0, 1, 0); }
            PressPawn->SetExternalView(FRotationMatrix::MakeFromXZ(ToPlanet,
                FVector::CrossProduct(Right.GetSafeNormal(), ToPlanet)).ToQuat());
            PressPawn->MoveForward(1.f);
            Fixture.Tick(1.f / 120.f);
            System->GetAllPlanetRuntimeData(Planets);
            const FVector ToPawn = PressPawn->GetActorLocation() - Planets[0].WorldPosition;
            const double Dist = ToPawn.Size();
            const FVector Radial = ToPawn / FMath::Max(Dist, 1.0);
            if (!bContact && Dist - Envelope < 500.0) { bContact = true; }
            if (bContact)
            {
                MaxOutward = FMath::Max(MaxOutward, ((PressMovement->Velocity - Planets[0].OrbitalVelocity) | Radial));
            }
        }
        PressPawn->MoveForward(0.f);
        for (int32 I = 0; I < 300; ++I) { Fixture.Tick(1.f / 120.f); }
        System->GetAllPlanetRuntimeData(Planets);
        const double MidRelSpeed = (PressMovement->Velocity - Planets[0].OrbitalVelocity).Size();
        for (int32 I = 0; I < 300; ++I) { Fixture.Tick(1.f / 120.f); }
        System->GetAllPlanetRuntimeData(Planets);
        const FVector SettleRel = PressMovement->Velocity - Planets[0].OrbitalVelocity;
        AddInfo(FString::Printf(TEXT("Flown press: maxOutward=%.1f midRelSpeed=%.1f settleRelSpeed=%.1f"),
            MaxOutward, MidRelSpeed, SettleRel.Size()));
        TestTrue(TEXT("Flown impact happened"), bContact);
        TestTrue(TEXT("No elastic rebound from a flown impact"), MaxOutward < 300000.0);
        // Superorbital slide energy strictly decays once the pilot releases:
        // a true bouncer/energizer would hold or grow.
        TestTrue(TEXT("Released slide energy decays instead of bouncing forever"),
            SettleRel.Size() < MidRelSpeed && SettleRel.Size() < 150000.0);
    }
    return true;
}

#endif
