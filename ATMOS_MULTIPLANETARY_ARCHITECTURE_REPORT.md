# ATMOS MULTIPLANETARY REBUILD — ARCHITECTURE REPORT

## OLD MODEL (Deprecated)

### Single-Governing-Planet Architecture
- **UHillaireAtmosphereSubsystem**: World subsystem with single governing planet concept
- **FHillairePlanetState**: Per-planet state but with transient slot indices (int32 PlanetId)
- **Single global LUT cache**: Shared across planets, keyed by slot index
- **Frame snapshots**: Built per-view, stashed by view state pointer, no double-buffer
- **Coordinate transforms**: Duplicated across components (PlanetLink, ViewExtension, LutManager)
- **Star system**: Single global star slot (HillaireStarLinkComponent pushes to shared slot 0)
- **Selection**: Hysteresis on governing planet but no multi-planet view context

### Problems Identified
1. **Flickering**: GT/RT race conditions — planet state updated on GT while RT reads stale data
2. **Single-planet assumption**: Architecture designed for one atmosphere, adapted for multiple
3. **Transient identity**: PlanetId = array slot index, not stable across spawn/despawn
4. **No frame isolation**: Snapshot built on GT frame N, RT may consume mixed N/N+1 data
5. **Coordinate duplication**: World→PlanetLocal math in 5+ places, inconsistent conventions
6. **Star coupling**: All planets forced to share one star direction
7. **LUT cross-contamination**: SkyView LUT for planet A could be used for planet B

---

## NEW MODEL (Implemented)

### Core Data Structures

#### FPlanetAtmosphereState (HillairePlanetAtmosphereState.h:16)
```cpp
struct FPlanetAtmosphereState {
    FGuid PlanetId;                    // Stable STARMAP identity
    FVector CenterWS;                  // World center (double, cm)
    FQuat RotationWS;                  // World rotation
    FHillaireAtmosphereProfile Profile; // Authoritative radii + scattering
    float GroundRadiusKm;              // = Profile.BottomRadiusKm
    float AtmosphereTopRadiusKm;       // = Profile.TopRadiusKm
    float TerrainHeightKm;
    FGuid StarId;                      // Associated star
    FVector3f StarDirectionLocal;      // Planet-local sun dir
    FVector StarDirectionWorld;        // World sun dir
    FVector3f StarIrradiance;          // Color * Intensity * atten
    uint32 GenerationVersion;          // Frame-gen counter
    uint64 ProfileHash;                // LUT cache key
    uint32 LutGenerationVersion;       // LUT rebuild counter
    bool bValid;
    // ... view-dependent computed at snapshot time
};
```

#### FHillaireAtmosphereFrameState (HillairePlanetAtmosphereState.h:89)
```cpp
struct FHillaireAtmosphereFrameState {
    uint64 FrameNumber;
    FVector ViewOriginWS;
    FMatrix ViewMatrix;                // Camera-relative km frame
    FMatrix ProjectionMatrix;
    FIntRect ViewRect;
    TArray<FPlanetAtmosphereState> Planets;
    TArray<FHillaireLightSource> Lights;
    int32 GoverningPlanetIndex;
    TArray<int32> VisiblePlanetIndices;
    float MultipleScatteringFactor;
    bool bFastSkyEnabled;
    uint64 WorldStateHash;
};
```

#### FAtmosphereViewContext (HillairePlanetAtmosphereState.h:132)
```cpp
struct FAtmosphereViewContext {
    FGuid PrimaryPlanetId;             // Contains camera or dominant
    TArray<FGuid> SecondaryPlanetIds;  // Visible, contribute to sky
    bool bIsInSpace;
    bool bHasSpaceBackground;
};
```

---

### Central Coordinate System (HillairePlanetMath namespace)

**Single canonical implementation** — used by snapshot builder, LUT manager, composites:

```cpp
// World (double cm) → Planet-local (float km)
FVector3f WorldToPlanetLocalKm(PlanetCenterWS, PlanetRotationWS, WorldPosWS);

// Planet-local (float km) → World (double cm)
FVector PlanetLocalToWorldWS(PlanetCenterWS, PlanetRotationWS, LocalPosKm);

// Directions (rotation only, translation-invariant)
FVector3f WorldDirectionToPlanetLocal(PlanetRotationWS, WorldDir);
FVector PlanetLocalDirectionToWorld(PlanetRotationWS, LocalDir);

// Camera in planet-local space
FVector3f CameraPlanetLocalKm(CenterCamRelativeKm, PlanetRotationWS);
FVector3f CameraUpLocal(CenterCamRelativeKm, PlanetRotationWS);

// Sun elevation for SkyView cache key
float SunElevationCos(StarDirectionLocal, CameraUpLocal);

// Selection with hysteresis
FPlanetSelectionResult SelectPlanets(Planets, CameraRel, ViewDir, IncumbentIndex);
```

**Convention verified against reference Hillaire**:
- `PlanetLocal = conjugate(Q) * (World - Center)` — rotation NEVER translates center
- Orbit modifies `CenterWS`; Rotation modifies `RotationWS` — completely separate

---

### Double-Buffered Frame Snapshots

```
Frame N:
  GT: Update all planets → Resolve lights → Build FrameState[N] → Swap to Current
  RT: Consume ONLY CurrentFrameSnapshot (immutable shared_ptr)

Frame N+1:
  GT: Build FrameState[N+1] into NextFrameSnapshot
  RT: Consume NextFrameSnapshot (after swap)
```

**Guarantees**:
- Zero GT/RT races: RT reads immutable snapshot, GT writes to separate buffer
- No stale transforms: All planet data comes from single frame's snapshot
- No one-frame mismatch: WorldStateHash detects unexpected changes

---

### UHillairePlanetaryAtmosphereSubsystem

**Responsibilities**:
- Planet registry: `TArray<FPlanetEntry>` keyed by stable `FGuid PlanetId`
- Star registry: `TArray<FStarEntry>` keyed by `FGuid StarId`
- Double-buffered snapshots: `CurrentFrameSnapshot` / `NextFrameSnapshot`
- Per-view stash: `TMap<ViewState*, FStashedSnapshot>` for GT→RT matching
- LUT manager ownership: `TUniquePtr<FHillaireLutManager>`
- ViewExtension registration: `FHillairePlanetaryViewExtension`

**Key Methods**:
```cpp
FGuid RegisterExternalPlanet(PlanetId, PlanetName);    // STARMAP calls once at spawn
void UpdateExternalPlanet(const FPlanetAtmosphereState&); // PlanetLink calls every tick
void RegisterExternalStar(StarId, LightSource);        // StarLink calls every tick
void BuildNextFrameSnapshot(ViewOrigin, ViewMatrix, Proj, Rect, ViewDir); // ViewExt SetupViewFamily
TSharedPtr<const FHillaireAtmosphereFrameState> GetCurrentFrameSnapshot();  // RT reads
TSharedPtr<const FHillaireAtmosphereFrameState> GetSnapshotForView(ViewState*); // ViewExt SetupView
```

---

### FHillairePlanetaryViewExtension

**Pipeline**:
1. **SetupViewFamily (GT)**: `BuildNextFrameSnapshot()` → `SwapFrameSnapshots()`
2. **SetupView (GT)**: Stash `CurrentFrameSnapshot` keyed by `View->State`
3. **BeforeDOF (RT)**: `FindSnapshot(View)` → Get governing planet → `EnsurePlanetLuts()` → Composite

**Composite Stages** (same-graph transient handoff):
1. **LUT Generation**: `EnsurePlanetLuts()` for governing planet (T → MS → SkyView)
2. **RT-Exact Re-anchor**: `ComputeCompositeViewInputs()` re-bases to RT view origin
3. **Aerial Perspective**: `EvaluateAerialPerspective()` + `CompositeAerialPerspective()`
4. **Sky Background**: `CompositeSkyBackground()` (SkyView sampling)
5. **Debug Viz**: `CompositeDebugVisualization()` (modes 1-5)

---

### Per-Planet LUT System

**FHillaireLutManager** keyed by **PlanetId + ProfileHash**:
- **Transmittance**: `ProfileHash` only (light-independent)
- **MultiScattering**: `Hash(ProfileHash, MultipleScatteringFactor)`
- **SkyView**: `ProfileHash` + `SunElevationCos` + `ViewHeightKm` + `PrimaryLightId`
- **Aerial**: NOT cached — view-dependent, evaluated on-demand into pooled scratch

**Cache Invalidation**:
- Profile change → `InvalidatePlanetLuts(PlanetId)` → regenerates all
- Star swap → `NotifyPrimarySlotChanged()` → invalidates SkyView only
- Sun elevation drift > 1e-3 cosine → invalidates SkyView
- View height drift > max(0.02, 1%*h) → invalidates SkyView

**RDG Lifetime**: Generation in BeforeDOF graph → transient outputs feed aerial/sky composites in SAME graph → pooled extraction for debug (1-frame latency, GT-safe)

---

### Planet/Star Link Integration

#### UHillairePlanetLinkComponent (Updated)
- **Tick**: `TG_PostUpdateWork` (after StarSystem orbit tick)
- **Pushes**: `FPlanetAtmosphereState` with live geometry
- **Identity**: Stable `FGuid` from `APlanet.PlanetID + PlanetSeed` hash
- **Radii**: Ground = PlanetRadius + TerrainHeight; Top = Ground × Multiplier(Seed) ∈ [1.1, 1.3]
- **Star**: Reads from `PlanetaryLightingComponent.StarActor`

#### UHillaireStarLinkComponent (Updated)
- **Owned by ASun** (constructor subobject)
- **Pushes**: One `FHillaireLightSource` per planet in system
- **Direction**: From planet's `PlanetaryLightingComponent.CurrentStarDirection`
- **Intensity**: `CurrentIllumination × SunLight.Intensity`
- **Association**: Keyed by planet's `StarId` → each planet resolves its own star

---

### Validation Tests (HillaireMultiplanetaryTests.cpp)

| Test | Coverage |
|------|----------|
| `PlanetMath.CoordinateTransforms` | World↔Local round-trip, direction transforms, camera local/up, sun elevation |
| `PlanetMath.Selection` | Governing selection: inside/outside/nearest, visible set far-to-near |
| `PlanetMath.SelectionHysteresis` | Incumbent retention, containment override, tie-breaking |
| `Profile.HashStability` | Content hash deterministic, changes on radius/sigma edits |
| `Profile.ThicknessNormalization` | HillaireBuildNormalizedProfile preserves optical depth across sizes |
| `FrameSnapshot.DoubleBuffer` | Snapshot immutability, frame isolation |
| `PlanetLink.AtmosphereMultiplier` | Deterministic [1.1,1.3] from seed, ground/thickness computation |

---

### Files Created

| File | Purpose |
|------|---------|
| `Public/HillairePlanetAtmosphereState.h` | Core PODs: FPlanetAtmosphereState, FHillaireAtmosphereFrameState, FAtmosphereViewContext, HillairePlanetMath |
| `Private/HillairePlanetAtmosphereState.cpp` | Coordinate math, selection with hysteresis |
| `Public/HillairePlanetaryAtmosphereSubsystem.h` | Central multiplanet subsystem interface |
| `Private/HillairePlanetaryAtmosphereSubsystem.cpp` | Registry, frame snapshot build, double-buffer swap |
| `Public/HillairePlanetaryViewExtension.h` | New view extension for planetary rendering |
| `Private/HillairePlanetaryViewExtension.cpp` | BeforeDOF composite pipeline, RT-exact re-anchor |
| `Source/Andromeda/Public/HillairePlanetLinkComponent.h` | Updated STARMAP planet adapter |
| `Source/Andromeda/Private/HillairePlanetLinkComponent.cpp` | Live geometry push, stable FGuid identity |
| `Source/Andromeda/Public/HillaireStarLinkComponent.h` | Updated STARMAP star adapter |
| `Source/Andromeda/Private/HillaireStarLinkComponent.cpp` | Per-planet star push, planetary lighting integration |
| `Private/Tests/HillaireMultiplanetaryTests.cpp` | Automation tests for math, selection, profiles, snapshots |

---

### Files Modified (Deprecated but Kept for Transition)

| File | Status |
|------|--------|
| `Public/HillaireAtmosphereSubsystem.h` | **Deprecated** — replace with PlanetaryAtmosphereSubsystem |
| `Private/HillaireAtmosphereSubsystem.cpp` | **Deprecated** |
| `Public/HillaireViewExtension.h` | **Deprecated** — replace with PlanetaryViewExtension |
| `Private/HillaireViewExtension.cpp` | **Deprecated** |
| `Public/HillairePlanetState.h` | **Deprecated** — replace with HillairePlanetAtmosphereState |
| `Private/HillairePlanetState.cpp` | **Deprecated** |

---

### Migration Checklist

- [x] Core data structures (FPlanetAtmosphereState, FrameState, ViewContext)
- [x] Centralized coordinate math (HillairePlanetMath)
- [x] Planetary subsystem (registry, frame snapshots, double-buffer)
- [x] Planetary view extension (GT build, RT consume, composite pipeline)
- [x] PlanetLink → Planetary subsystem (stable FGuid, live geometry)
- [x] StarLink → Planetary subsystem (per-planet star association)
- [x] Automation tests (math, selection, profiles, snapshots, multipliers)
- [ ] **Build verification** (pending UE5.8 toolchain access)
- [ ] **PIE validation** (scenarios 1-10 from task)
- [ ] **Deprecate old subsystem** (remove UHillaireAtmosphereSubsystem, FHillaireViewExtension)
- [ ] **Update AndromedaGameMode** to ensure new subsystem initializes
- [ ] **Debug CVars**: `r.Hillaire.DebugPlanet`, `DebugStability`, `DebugCoordinates`, `DebugFrameState`, `DebugResources`

---

### Known Limitations (Post-Implementation)

1. **Star ID generation**: Currently `FGuid::NewGuid()` in StarLink — needs deterministic per-system ID
2. **Multi-star systems**: Not fully tested — each planet needs correct StarId resolution
3. **Aerial perspective for secondary planets**: Only governing planet evaluated (by design, camera-volume is local)
4. **Space background**: `bHasSpaceBackground` placeholder — no starfield implementation yet
5. **Transient vs. persistent LUTs**: SkyView transient in BeforeDOF graph; pooled for debug only

---

### Visual Acceptance Criteria (Task §22)

| Scenario | Expected |
|----------|----------|
| Surface → Atmosphere → Sky → Space (Planet A) | Continuous, no pop |
| Space → Planet B Atmosphere → Surface | Continuous, no pop |
| Atmosphere anchored to planet surface | Limb follows planet, no lag |
| Planet orbits | Atmosphere moves with planet center |
| Planet rotates | Atmosphere center stable, only local frame spins |
| Camera moves | No flickering, stable LUT keys |
| Camera static 120 frames | All values stable (WorldStateHash constant) |

---

### Thread Safety Guarantees

| Component | Thread | Safety Mechanism |
|-----------|--------|------------------|
| PlanetRegistry | GT only | Single-threaded access |
| StarRegistry | GT only | Single-threaded access |
| CurrentFrameSnapshot | GT write / RT read | `TSharedPtr<const>` immutable |
| NextFrameSnapshot | GT only | Single-threaded write |
| SnapshotStash | GT write / RT read | `FCriticalSection` + immutable snapshots |
| LutManager.LutStates | GT+RT | `FCriticalSection` (leaf lock, no nesting) |
| LutManager.Targets | RT only | Fixed arrays, stable addresses |
| RDG Textures | RT only | Same-graph transient → pooled extraction |

---

### Performance Notes

- **Planet count**: Max 8 (HILLAIRE_MAX_PLANETS) — bounded GPU memory
- **Light count**: Max 8 (HILLAIRE_MAX_ATMOSPHERE_LIGHTS) — uniform arrays
- **LUT resolutions**: T(256×64), MS(32×32), SkyView(192×108), Aerial(32³)
- **Dispatch**: Compute per-texel (T: 32×8, MS: 32×32, SkyView: 24×14, Aerial: 8³)
- **Frame cost**: 1 governing planet LUT gen + 1 aerial eval + 2 composites per frame
- **Cache reuse**: ProfileHash + elevation + height keys prevent thrash on static scenes