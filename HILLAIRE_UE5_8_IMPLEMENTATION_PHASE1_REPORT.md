# HILLAIRE → UE5.8 — IMPLEMENTATION PHASE 1 REPORT
## Core State + Light Sources + Per-View Snapshot + RDG/LUT Infrastructure

Date: 2026-09-15 · Target: `C:\Users\aless\Documents\Unreal Projects\Andromeda` · Engine: UE 5.8 (`C:\Unreal Engine\UE_5.8`)
Spec: `HILLAIRE_UE5_8_PORT_SPECIFICATION.md` (DX11 repo, read-only reference, NOT modified)
Status: **PASS** — compilable foundation, no final rendering (as scoped).

---

## 1. Initial audit

- **Modules / Build.cs:** single game module `Andromeda` (`Source/Andromeda/Andromeda.Build.cs`: Core, CoreUObject, Engine, InputCore, ProceduralMeshComponent). No `Plugins/` directory existed.
- **STARMAP:** `AStarSystem` (`Public/StarSystem.h` + `Private/StarSystem.cpp`) — UniverseSeed, PlanetClass/SunClass spawning, orbit/rotation simulation, read-only planet queries (`GetPlanetRuntimeData`, `GetSunActor`). Untouched source of truth.
- **Planet:** `APlanet` — `PlanetRadius` (default 500000 cm), `TerrainHeight`, `FPlanetProfile` (biome/archetype data, NOT scattering data), `UPlanetaryLightingComponent`. Untouched.
- **Planetary Lighting:** `UPlanetaryLightingComponent` — per-planet, star-referenced, inverse-square illumination. Untouched. Convention confirmed: direction TO star = `(Star-Planet).GetSafeNormal()`, reused by the new light source.
- **Sun:** `ASun` — owns a `UPointLightComponent` (`SunLight`) + ambient. This matches the required `Point Light + Atmosphere Light Source` architecture; the new component attaches to it without modifying it. Untouched.
- **Old atmosphere: VERIFIED ABSENT.** No ATMOS/ZEPHYR/Hillaire code remains (`ATMOS_*_CLEANUP_REPORT.md`, `HILLAIRE_LEGACY_CLEANUP_REPORT.md` document the total removal; game code carries "CLEAN SLATE" comments). Grep for `UAtmosphere|UHillaire|FHillaire|CurrentSun|PrimarySun` in `Source/` returned zero pre-existing hits — **no name collisions** with the new `Hillaire*` namespace.
- **Uproject:** EngineAssociation 5.8; added one entry: `HillaireAtmosphere: Enabled`.

## 2. Files created (all under `Plugins/HillaireAtmosphere/`)

```
HillaireAtmosphere.uplugin
Source/HillaireAtmosphere/HillaireAtmosphere.Build.cs      (Core, CoreUObject, Engine, RHI, RenderCore, Renderer, Projects)
Source/HillaireAtmosphere/Public/
  HillaireLimits.h              centralized bounds (N<=8, M<=8, thresholds, LUT sizes, units)
  HillaireUnits.h               cm<->km + world->camera-relative conversions (documented chain)
  HillaireHash.h                FNV-1a + quantized-float hashing (reference contract)
  HillaireAtmosphereLog.h       LogHillaireAtmosphere category
  HillaireAtmosphereProfile.h   FHillaireAtmosphereProfile (UPROPERTY-editable, km units)
  HillaireLightSource.h         FHillaireLightSource + FHillaireResolvedLight + FHillaireCompactedLights + pure resolve/compact
  HillairePlanetState.h         FHillairePlanetLutState + FHillairePlanetState + SelectPlanets + ScreenRect (pure)
  HillaireViewSnapshot.h        FHillaireSnapshotLight/Planet + FHillaireViewSnapshot + pure builder
  HillaireLutManager.h          per-planet ownership, generation keys, invalidation predicates, RDG boundary
  HillaireRdgHelpers.h          LUT descriptors + persistent-texture import/export helpers
  HillaireShaders.h             6 FGlobalShader subclasses + pass parameter structs (uniform arrays [8])
  HillaireAtmosphereSubsystem.h UWorldSubsystem: registries, snapshot builds, LUT manager, knobs
  HillaireAtmosphereComponent.h planet registration component (test/manual path)
  HillaireAtmosphereLightComponent.h  atmosphere light source component (any light)
  HillaireViewExtension.h       FSceneViewExtensionBase skeleton (GT snapshot + RT RDG boundary)
  HillaireTestFunctions.h       scenario table + serializable test snapshot (MPLN2 analogue)
Source/HillaireAtmosphere/Private/  (matching .cpps + HillaireAtmosphereModule.cpp + Tests/HillaireAtmosphereTests.cpp)
Shaders/
  HillaireCommon.ush            VERBATIM PORTED MATH: quat frame, ray-sphere, transmittance UV <-> params
  HillaireSampling.ush          VERBATIM PORTED MATH: SkyView UV <-> params, phases, sun disk (both overloads)
  HillaireTransmittanceLut.usf / HillaireMultiScattering.usf / HillaireSkyViewLut.usf /
  HillaireAerialVolume.usf / HillaireFinal.usf / HillaireDebugLut.usf   (Phase-1 stubs, honestly marked)
```

## 3. Files modified

- `Andromeda.uproject` — added `{"Name": "HillaireAtmosphere", "Enabled": true}`. Nothing else.
- `Plugins/HillaireAtmosphere/Source/...` — several fix iterations during build (see §18).

## 4. Resulting architecture

```text
STARMAP (untouched source of truth: AStarSystem / APlanet / ASun)
   │  (future adapters in Andromeda, NOT in this plugin)
   ▼
UHillaireAtmosphereComponent (planet authoring: radii + profile + terrain)
UHillaireAtmosphereLightComponent (reads owner ULightComponent live)
   │  BeginPlay/EndPlay registration, no ticking
   ▼
UHillaireAtmosphereSubsystem (per-world; registries + FHillaireLutManager + knobs)
   │  BuildSnapshotForView (GameThread)
   ▼
FHillaireViewSnapshot (immutable, camera-relative km, per-planet resolved lights)
   │  consumed read-only on RenderThread — NO UObject access on RT
   ▼
FHillaireViewExtension (SetupView builds/stashes; PreRenderView_RenderThread → BuildLutPasses)
   ▼
RDG (persistent pooled LUT targets via QueueTextureExtraction; P0–P4 attach in Phase 2)
```

Dependency graph: Components → Subsystem → {SnapshotBuilder (pure), LutManager}; ViewExtension → Subsystem + LutManager; Shaders ← RdgHelpers/LutManager; Tests → pure layer only. No cycles, no singletons, no engine fork.

## 5. Main classes

| Class | Role |
|---|---|
| `UHillaireAtmosphereSubsystem` | Per-world owner of planet/light registries, LUT manager, `MultipleScatteringFactor` / `bFastSkyEnabled` / `bAtmosphereEnabled` knobs, GT snapshot builds, view-extension lifetime |
| `UHillaireAtmosphereComponent` | Planet authoring + registration; folds Ground/Height into authoritative profile radii (single write path) |
| `UHillaireAtmosphereLightComponent` | Atmosphere Light Source; ANDs own gates with live owner-light state |
| `FHillaireLutManager` | Per-planet LUT slots, `QueryRegen` predicates, targeted invalidation, `BuildLutPasses` RDG boundary |
| `FHillaireViewExtension` | GT per-view snapshot stash → RT LUT-target boundary; filters by world scene (PIE-safe) |
| `FHillaireViewSnapshotBuilder` | Pure world-state → immutable snapshot (fully unit-tested) |
| 6× `FHillaire*` global shaders | P0–P5 entries + parameter structs |

## 6. Dependency graph

See §4. External deps (Build.cs, all actually used): Core, CoreUObject, Engine (components/subsystem), RHI + RenderCore (RDG types, pooled targets, shader-parameter macros), Renderer (`FSceneViewExtensionBase`, `FSceneView`), Projects (`IPluginManager` for the shader-dir mapping).

## 7. Atmosphere State

`FHillaireAtmosphereProfile` mirrors reference `AtmosphereInfo` semantics: bottom/top radii (km), Rayleigh/Mie/absorption sigmas (1/km), two-layer-equivalent density terms, `MiePhaseG`, `MuSMin`, ground albedo, white-transfer solar. `FHillairePlanetState` is autonomous per planet (id, guid, name, double-cm center, quat, profile, derived radii mirrors, terrain height, `StarDistanceKm` with <0 = directional Case A/B). No `CurrentSun/PrimarySun/CurrentAtmosphere` anywhere (verified by construction + grep). `MakeReferenceProfile()` transcribes `SetupEarthAtmosphere` values (with `MuSMin = cos(120°)` computed, not pasted).

## 8. Atmosphere Light Source

`UHillaireAtmosphereLightComponent` attaches to any actor; reads the owning `ULightComponent` live (position, `GetLightColor()`, `Intensity`, `IsVisible()`) and ANDs with `bEnabled`. Without an owner light it acts as a test sun rig (own color/intensity/transform). Directional TO-light = owner +Z (directional-proxy convention; **provisional — verified by the sun-facing scenario in the rendering phase**). Fields: stable `FGuid`, name, enabled, directional, color, intensity (documented raw lab units), disk radius/flag.

## 9. N×M model

Registry arrays in the subsystem; per-frame GT expansion `Atmosphere[planet] × Light[0..N]` via `HillaireCompactLightsForPlanet` (skip disabled, resolve, zero-fill tail to 8, `Count`, `bSinglePrimary`). Uniform arrays, NOT structured buffers (spec decision 3). Single bound: `HillaireLimits.h` (`constexpr` + `HILLAIRE_MAX_*` defines, `static_assert`ed equal, brackets form for `SHADER_PARAMETER_ARRAY`). `bSinglePrimary` mirrors `UseSinglePrimaryFastPath` verbatim incl. the slot-0 identity rule.

## 10. Per-view snapshot

`FHillaireViewSnapshot`: origin (double cm), relative-frame matrices, rect, relevant planets in draw order (visibles far-to-near, governing last), raw + per-planet-resolved lights, governing id, effective count, `SnapshotHash`. Builder is pure and side-effect-free. RT consumes via stashed `TSharedPtr<const FHillaireViewSnapshot>` (critical-section guarded, frame-pruned).

## 11. Camera-relative conversion

Chain enforced by types (`HillaireUnits.h`): world double-cm → subtract per-view origin in double → narrow to float km. Snapshot matrices live in the relative-km frame (view translation stripped, rotation preserved; projection passed through — perspective division is scale-invariant, so km input projects exactly). Planet-local `conj(Q)*(P-C)` still applied exactly once in-shader (`HillaireCommon.ush`). Translation-invariance at +5 000 km offsets is test-proven bit-identical.

## 12. LUT architecture

Per-planet slots (`Register/Unregister`, separate pooled handles — no cross-planet sharing in v1 by decision). Keys: T = profile hash; MS = hash(profile, factor); SkyView = profile hash + sun cache + height eps + slot-0 identity. Descriptors: 256×64 / 32×32 / 192×108, `PF_FloatRGBA` (32F until 16F A/B), SRV+UAV+RT flags. Lifetime: `CreateTexture` + `QueueTextureExtraction` into persistent pool handles; regen predicates evaluated on GT outside graph build (no no-op passes).

## 13. RDG architecture

`HillaireRdg` namespace (descriptors + persistent-texture helper) and `FHillaireLutManager::BuildLutPasses` (per-planet target guarantee, called from `PreRenderView_RenderThread`). Pass slots P0–P5 reserved with documented attach points; parameter structs (`FHillaireLutGenParameters`, `FHillaireAtmospherePassParameters` with `[8]` light arrays) ready for binding.

## 14. View Extension

`FHillaireViewExtension : FSceneViewExtensionBase`, created per world-subsystem via `FSceneViewExtensions::NewExtension` (self-registering, lifetime-tied), filtering views by world scene. `SetupView` (GT) builds the snapshot; `PreRenderView_RenderThread` consumes it into the LUT boundary. `r.Hillaire.Enable` master switch. No renderer source touched.

## 15. Shader architecture

`HillaireCommon.ush` + `HillaireSampling.ush` = verbatim-ported frozen math (cbuffer/slots removed, functions parameterized). Six `.usf` entries compile as global shaders (verified: all 6 compiled at editor boot, `IMPLEMENT_SHADER_TYPE` + `ShouldCompilePermutation` gating, `/Plugin/HillaireAtmosphere` dir mapping in `StartupModule`). `.usf` bodies are marked Phase-1 stubs — no invented scattering math. Fixed during bring-up: mandatory `/Engine/Public/Platform.ush` first-include (was a fatal editor-boot error) and full C++↔HLSL parameter binding (was a shader-struct ensure).

## 16. Tests executed

`Automation RunTests Hillaire.Phase1` in-editor, exit code 0 — **6/6 Success**: Profile (validation, hash determinism, units), Light (directional/point/disabled/degenerate/rotation), NxM (1×1, 1×2, 2×1, 2×2, slot-swap rule, point-not-fast-path, all-disabled), Snapshot (determinism, governing/rect/fast-path resolution, translation invariance of all relative content, empty world), LutCache (cold/warm reuse + counter, sun-threshold behavior, T/MS untouched by sun moves, MS-factor key isolation, per-planet independence, targeted slot-swap invalidation, unregister), ResolveParity (compact == direct resolve — precursor of the `MultiA == Surface` gate) + scenario table + `FHillaireTestSnapshot` round-trip.

## 17. Build result

`Build.bat AndromedaEditor Win64 Development` → **Result: Succeeded** (final incremental: ~8 s; full module compile clean).

## 18. Warnings / errors

- UHT `-WarningsAsErrors`: fixed by using bare `UPROPERTY()` for non-exposed snapshot fields.
- C++ API corrections (all verified against UE 5.8 sources): `SHADER_PARAMETER_ARRAY` needs `[N]`; `RenderGraphFwd.h` (no `RDGClasses.h`); `FQuat::GetAxisZ()`; `FVector::ContainsNaN()`; `EAutomationTestFlags::EditorContext`; `struct IPooledRenderTarget`; subsystem includes `HillaireLutManager.h` (ctor needs complete type); `FViewMatrices::GetWorldToView()/GetViewToClip()` (5.8 deprecations fixed, no new warnings).
- Runtime bring-up (caught by editor runs, fixed): missing `Platform.ush` (fatal), DebugLut unbound params (ensure), one test-expectation bug (hash covers absolute origin by design — test now asserts content invariance) + one test-setup bug (unshifted light).
- Pre-existing, untouched: VS toolchain "not preferred" notice, `IncludeOrderVersion_Unreal5_6` upgrade notice. No new warnings from this plugin in the final build.

## 19. Decisions

1. Profile owns radii; state mirrors are derived via a single write path (validated equal).
2. Uniform arrays `[8]`, single bound in `HillaireLimits.h`.
3. Snapshot stores GT-resolved per-planet lights AND raw lights (RT = upload only; future multi-light needs no model change).
4. No subsystem/component ticking — pull-driven snapshots + event-driven invalidation.
5. Per-planet LUT handles, no sharing in v1; secondary-light motion never invalidates SkyView.
6. Directional TO-light = owner +Z: provisional, sun-facing scenario verifies in Phase 2.
7. `.usf` entries are stubs: no invented math, no premature "working atmosphere" claims.
8. No git operations performed (per task rule); DX11 repo untouched (read-only).

## 20. NOT yet implemented (explicitly out of scope)

Ray marching, LUT generation math, final composite, SkyView/aerial rendering, multi-light volumetric integration, terrain/terrain-shadow integration, final post-process, STARMAP adapters, test map + demo pawn + debug UI, full 20-scenario harness, perf baselines, Case C/D, per-light SkyView. The `MultiA == Surface` pixel gate is reserved as the blocking validation of the rendering phase (resolve-level parity proven here).

## 21. Next milestone (separate task — NOT started)

Phase 2 ray marching in §19 order: LUT generators → SkyView + sun cache → aerial → single-primary final → selection/rects composite → multi-light volumetric → hardening → demo/test map → automation + perf → docs + Andromeda seam. Stop condition respected: Phase 1 foundation complete, Phase 2 not begun.
