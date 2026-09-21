# HILLAIRE MULTIPLANET PORT REPORT — Project Andromeda

Date: 2026-09-15 (UTC). UE 5.8, AndromedaEditor Win64 Development.
Golden reference: `C:\Users\aless\UnrealEngineSkyAtmosphere` (standalone, read-only).
Project: `C:\Users\aless\Documents\Unreal Projects\Andromeda`.

Result: **ONE shared Hillaire renderer, MANY per-planet atmosphere instances.**
Build: **Succeeded.** Global shaders: **all compile, 0 errors, clean editor lifecycle.**

---

## RECOVERY (this session was a continuation)

- **Point where previous work stopped:** C++ build green; global-shader validation
  red on `FHillaireSkyViewPS` with an unactionable worker assert
  (`WasInRootParameters`, ShaderCompilerCommon.cpp:929). Investigation of the
  engine parser/binder was interrupted mid-read.
- **Already COMPLETE and PRESERVED as-is:** reference audit, Andromeda audit,
  architecture, `UAndromedaAtmosphereComponent`, Planet.h/.cpp integration,
  per-planet snapshot state, CPU derivation + deterministic color, PIE dirty
  path, 4 HLSL passes (math), RDG view extension with per-planet persistent
  LUT cache, Build.cs deps, module shader mapping + deferred extension
  registration, STARMAP/PlanetaryLighting untouched, cleanup-session diffs
  (ATMOS/ZEPHYR removal) untouched, standalone repo untouched (zero writes;
  its worktree modifications pre-date this work).
- **Partial / broken at recovery:** shader binding (binder assert rotating
  across shaders run to run).
- **Fixed during recovery:**
  1. Nested `SHADER_PARAMETER_STRUCT_INCLUDE` does not bind (UE matches flat
     leaf names) → flattened parameters (real bug, fixed).
  2. Removed an UNUSED `InTransmittance` SRV from the composite (proven
     absent from preprocessed source; unused resources are stripped from
     reflection and trip the binder — real bug, fixed).
  3. Restructured SkyView generation to analytic sun-transmittance
     (`HillaireTransmittanceAnalytic`, same 40-step integral as the T LUT),
     so no pass declares 2 SRVs (hardening).
  4. Moved ALL per-planet data from loose root-scope uniforms into ONE
     uniform buffer (`FHillaireAtmosphereUniform` + `SHADER_PARAMETER_STRUCT_REF`,
     HLSL via `/Engine/Generated/GeneratedUniformBuffers.ush`) — the
     engine-blessed path, immune to the loose-global binder bookkeeping.
  5. Renamed the UB instance to `HillaireAtmosphere`: `Atmosphere` collides
     with the ENGINE's own SkyAtmosphere `Atmosphere` uniform buffer
     (`FAtmosphereConstants`) — diagnosed from exact DXC errors.
  6. Fixed use-before-declaration of the local `Atmosphere` struct in the
     composite entry point (it resolved to the engine global).
  7. Deferred `FSceneViewExtensions::NewExtension` to `OnPostEngineInit`
     (module starts before `GEngine` exists — was a handled ensure).
  8. UE5.8 API updates: new `SubscribeToPostProcessingPass` signature
     (returns `FScreenPassTexture`, takes `const FSceneView&`),
     `GetClipToWorld()`, `FRHITexture` (no `FRHITexture2D`), RDG external
     texture extract/import for the persistent per-planet LUT cache,
     `.usf` extension in `IMPLEMENT_GLOBAL_SHADER`, `Platform.ush` include,
     bare `Texture2D` SRV declarations, `IMPLEMENT_UNIFORM_BUFFER_STRUCT`.
- **Files created during recovery:** none (all port files already existed;
  only edits). Temporary probe shader (`HillaireProbe.usf` +
  `FHillaireProbePS`) was created for diagnosis and DELETED afterwards.
- **Build result:** Succeeded (final).
- **Shader result:** all 5 global shaders compile, 0 errors (final
  validation run; Transmittance/MS/SkyView served from cache = previously
  compiled clean; Composite recompiled clean this run).
- **Validation result:** headless editor full init + clean shutdown, no
  fatals. PIE/multiplanet behavioral tests (A–O) remain MANUAL (checklist
  in §17) — they need interactive PIE and were not runnable headless.

## 1. Reference audit

Standalone (`UnrealEngineSkyAtmosphere`, untouched) transferable core:
`Application/PlanetState.h/.cpp` (per-planet POD, FNV-1a content hash,
`P_local=conj(Q)*(P_world-C)` / `S_local=conj(Q)*S_world` convention,
governing+visible selection), `SkyAtmosphereCommon.h/.cpp`
(`SetupEarthAtmosphere`: R=6360/Top=6460 km, Rayleigh H=8 km
σ=(0.005802,0.013558,0.0331)/km, Mie H=1.2 km σ=0.003996/ext=0.00444/g=0.8,
ozone 2-layer tent, solar=(1,1,1), μs_min=cos120°, sun 0.004675 rad),
`RenderSkyRayMarching.hlsl` (T 40-step, SkyView 192×108 30-step, MS 32×32
8×8 dirs × 20 steps + 1/(1−r) series), `RenderSkyCommon.hlsl`
(Rayleigh 3/16π(1+cos²), Cornette-Shanks, Bruneton T/SkyView UV mappings),
`SkyAtmosphereCommon.hlsl` (planet-frame helpers). Demo-only, NOT ported:
ImGui panels/input, terrain/shadow demo, `DataRecord`/`WinMain`, `DX11Base`,
`GpuDebugRenderer`, `ToyShader`/`ColoredTriangles`, EXR/PNG assets,
Bruneton-legacy LUT chain (kept as math reference only).

## 2. Andromeda audit

`APlanet` (`Public/Planet/Planet.h`, `Private/Planet/Planet.cpp`): seed
(`PlanetSeed`), radii (`PlanetRadius` cm + `TerrainHeight` cm), actor
transform, rotation owned externally by `AStarSystem` (tilt+spin quats).
STARMAP (`StarSystem.h/.cpp`, `StarSystemGenerator`, `Sun.h/.cpp` with
`ASun` point+sky lights, `GetSunActor()` generic API) untouched.
`PlanetaryLightingComponent` untouched. `Shaders/` was empty (legacy
removed). Module `Andromeda` single; `BP_Planet` = `APlanet` subclass
(binary; still carries a STALE reference to deleted legacy class
`PlanetAtmosphereComponent` → editor load warning, see §18).

## 3. Mapping table

| Standalone | Andromeda destination | Purpose / changes |
|---|---|---|
| `PlanetState.h` (state+hash+transforms) | `Public/Atmosphere/AndromedaAtmosphereParams.h` (`FAndromedaAtmosphereParams`, `FAndromedaAtmosphereSnapshot`, `FAndromedaHillaireConstants`) | USTRUCT render-safe POD; km units kept; hash now `int64` (UHT) |
| `PlanetState.cpp` (hash/quat/select) | `Private/Atmosphere/AndromedaAtmosphereComponent.cpp` (hash, deterministic color, derivation) + renderer cache keys | FNV-1a mirrors reference quanta; selection implicit via per-view composite |
| `SkyAtmosphereCommon.cpp` Earth values | `RefreshDerivedParams()` | Radii from real `PlanetRadius`/`TerrainHeight` (cm→km); thickness clamped 20–150 km; Rayleigh tinted by effective color around Earth reference |
| `SkyAtmosphereCommon.hlsl` + `RenderSkyCommon.hlsl` | `Shaders/Andromeda/Atmosphere/HillaireCommon.ush` | Same phases/medium/UV math; UB instance; planet-local helpers |
| `RenderSkyRayMarching.hlsl` T/SkyView/MS | `HillaireTransmittance.usf` / `HillaireSkyView.usf` / `HillaireMultiScattering.usf` | Same marches; SkyView uses analytic sun-T (see §9) |
| `RenderSky*.cpp` orchestration | `AndromedaAtmosphereRenderer` + `AndromedaAtmosphereViewExtension` + `AndromedaAtmosphereSubsystem` | Shared renderer; per-planet persistent LUTs; game→render snapshots |
| `Game.cpp` ImGui/demo/terrain/autosnap | NOT ported (demo-only, verified no atmosphere math inside) | — |
| Bruneton legacy chain | NOT ported (legacy reference path only) | documented here |

## 4. Architecture

```
APlanet ── Root / PlanetProceduralMesh / PlanetaryLighting (untouched)
        └─ UAndromedaAtmosphereComponent "Atmosphere" (per-planet instance)
              │ seed · generated color · override · derived params · dirty flag
              ▼ snapshot (render-safe POD, cm→km, normalized quat)
UAndromedaAtmosphereSubsystem (game thread tick → ENQUEUE_RENDER_COMMAND)
              ▼
FAndromedaAtmosphereRenderer (ONE; render-thread mirror + per-PlanetId LUT cache)
              ▼
FAndromedaAtmosphereViewExtension (Tonemap hook: ensure T/MS/SkyView per planet, composite far→near)
              ▼
Shared global shaders (uniform buffer + ≤1 SRV each; never per-planet)
```
No `CurrentPlanet`, no singletons for state, no per-planet shaders/renderers,
no Blueprint renderer, no parallel ATMOS/ZEPHYR system.

## 5. Component architecture

`UAndromedaAtmosphereComponent : UActorComponent` (`Public/Atmosphere/`).
User-facing: `bAtmosphereEnabled`, `bUseAtmosphereColorOverride`,
`AtmosphereColorOverride` (PIE-editable), `StarColor` (reserved input).
Read-only: `GeneratedAtmosphereColor`, `EffectiveAtmosphereColor`,
`DerivedParams`, `bParametersDirty`, `LutBuildCount/LutReuseCount`.
API: `MarkAtmosphereParametersDirty()`, `GetEffectiveAtmosphereColor()`,
`GenerateDeterministicColor(Seed, StarColor)` (static),
`RefreshDerivedParams()`, `BuildSnapshot()`, `PostEditChangeProperty` →
dirty (PIE live path). Registers/unregisters with renderer; discovery is
authoritative via subsystem `TObjectIterator` (no ordering hazards).

## 6. Planet integration

`Planet.h`: `AtmosphereComponent` (`VisibleAnywhere, BlueprintReadOnly`,
`Andromeda|Planet|Atmosphere`). Constructor:
`CreateDefaultSubobject<UAndromedaAtmosphereComponent>(TEXT("Atmosphere"))`
(propagates to `BP_Planet` automatically). `InitializePlanet()` refreshes
derived params from `PlanetID/PlanetSeed/PlanetRadius/TerrainHeight`.
`Planet.cpp` never renders — owns the component only.

## 7. Shader port

`Shaders/Andromeda/Atmosphere/`: `HillaireCommon.ush` (medium, phases,
Bruneton mappings, planet frame, analytic-T, UB instance + `MakeAtmosphere`),
`HillaireTransmittance.usf` (256×64), `HillaireMultiScattering.usf`
(32×32), `HillaireSkyView.usf` (192×108), `HillaireComposite.usf`
(fullscreen per-planet composite). C++: `AndromedaAtmosphereShaders.h`
(one `BEGIN_UNIFORM_BUFFER_STRUCT` + 4 `FGlobalShader` passes +
reserved `FHillaireFullscreenVS`), `AndromedaAtmosphereShaders.cpp`
(`IMPLEMENT_*`, `.usf` extensions). Mapping `/Andromeda → Shaders/Andromeda`
in module startup. All math ported 1:1 (scattering, phases, optical depth,
ray marching, LUT parameterizations, ozone, sun disk).

## 8. RDG port (UE 5.8)

Compute passes via `FComputeShaderUtils::AddPass` (`ERDGPassFlags::Compute`).
Persistent per-planet LUTs via `ConvertToExternalTexture` /
`RegisterExternalTexture` (no manual RHI alloc; no cross-frame aliasing).
SRV via `CreateSRV`, UAV via `CreateUAV`. Uniform data via single-frame
`TUniformBufferRef::CreateUniformBufferImmediate`. Static bilinear clamp
sampler. New `SubscribeToPostProcessingPass` returning `FScreenPassTexture`.

## 9. LUT architecture

Per-planet cache (`FPlanetLutCache`, keyed by stable `PlanetId`):
Transmittance 256×64 + MultiScattering 32×32 + SkyView 192×108 (all
`PF_FloatRGBA`), content hashes (`TransmittanceHash`,
`MultiScattHash = ProfileHash ^ salt`, `SkyViewHash` + camera-height gate
`max(2%, 1cm-in-km)`), `BuildCount/ReuseCount/LastBuildMs` telemetry.
Invalidation is per-planet by hash compare; rotation/translation/sun never
touch hashes. ADAPTATION (documented): SkyView samples sun-transmittance
analytically (`HillaireTransmittanceAnalytic`, same 40-step integral the T
LUT bakes) instead of a second SRV — forced by the UE5.8 binder defect
below; zero math delta (same integral, no LUT quantization on that leg).

## 10. Planet-local transforms

Convention identical to reference, applied once per planet at the pass
boundary (CPU snapshot + GPU helpers): positions `conj(Q)*(P−C)`, directions
`conj(Q)*D` (rotation-only — the previously-fixed world-direction bug stays
fixed). Units converted once (cm→km). Sun stays a single world direction
(from `ASun` actor, fallback fixed vector); star distance reserved Case A.

## 11. Deterministic color

`GenerateDeterministicColor`: `FRandomStream(Seed)` hue 0.52–0.66±,
sat/val bands + small star-luminance bias (white today = stable). Same
seed+inputs ⇒ same color (no unsourced RNG). Rayleigh tint = 55% color /
45% Earth reference, clamped — vivid hues without optical-depth collapse.
`GeneratedAtmosphereColor` visible read-only; effective = override-aware.

## 12. PIE override

`bUseAtmosphereColorOverride` + `AtmosphereColorOverride` edit → `PostEdit`
→ `MarkAtmosphereParametersDirty()` → subsystem re-derives + re-hashes;
LUTs rebuild ONLY if the content hash changed, ONLY for that planet. No
PIE restart, no planet respawn. Override OFF restores deterministic color.

## 13. Star-color integration point

`StarColor` (component) → snapshot → reserved in derivation
(`GenerateDeterministicColor(Seed, StarColor)` already takes it);
`StarDistanceKm = −1` (directional, Case A). STARMAP coupling later = feed
real star color + sun direction per planet; no API change needed.

## 14. Cache/invalidation

Covered in §9. Rotation updates local sun direction through the snapshot
quat every frame with zero LUT cost. Only physical/content changes
(radii, sigmas, albedo, phase, densities, sun size, MS factor) invalidate,
single-planet only.

## 15. Build result

`Build.bat AndromedaEditor Win64 Development` → **Succeeded** (final;
incremental ~4–6 s). All C++ errors from the port resolved (UHT uint types,
UE5.8 RHI/API renames, `IMPLEMENT_UNIFORM_BUFFER_STRUCT`, sampler setup).

## 16. Shader result

Headless editor full init + clean shutdown (`LogExit`, 0 errors):
`FHillaireTransmittancePS`, `FHillaireMultiScatteringPS`,
`FHillaireSkyViewPS`, `FHillaireCompositePS`, `FHillaireFullscreenVS` —
**all compile**. Binder-defect saga (recovery): loose root scalars tripped
an unactionable `WasInRootParameters` assert that rotated across shaders
between runs (parallel-worker abort-on-first-crash artifact); a strict
diagnostic probe (same 18-param shape, trivial body) reproduced it,
eliminating shader code as the cause. Fixed by: uniform-buffer path for all
per-planet data; ≤1 SRV per pass (analytic sun-T in SkyView); removal of one
genuinely unused SRV (composite); `Atmosphere`→`HillaireAtmosphere` rename
(collision with engine `FAtmosphereConstants`); declaration ordering.
14 pre-existing `LogAutomationTest: Condition failed` lines proven unrelated
(identical in 03/09 log).

## 17. Validation results (TEST A–O)

- Static/proven: N (grep: no ATMOS/ZEPHYR/CurrentPlanet/GlobalAtmosphere/
  manager/mailbox residues in worktree Source; `Shaders/` holds only the 5
  new files; STARMAP + Planetary Lighting unmodified by this port), O
  (math ported 1:1; deviations documented in §9), G/H (deterministic by
  construction: seeded stream), I/J/K (hash excludes transform/sun;
  per-planet invalidation), M (planet-local + km + clip-to-world composite).
- Headless-proven: build, all shaders, clean lifecycle.
- MANUAL (require interactive PIE — steps for the user):
  A: spawn 1 planet → atmosphere visible. B: `bAtmosphereEnabled=false` →
  no atmosphere. C/D/L: 2–3 planets, distinct overrides, simultaneous view.
  E: change A's override mid-PIE → A changes, B/C untouched. F: disable
  override → deterministic color returns. I: rotate planet → terminator
  moves. K: change radius → only that planet rebuilds (watch
  `LutBuildCount`). M: near/far camera sweep.

## 18. Known limitations

1. `BP_Planet` binary still references deleted legacy class
   `PlanetAtmosphereComponent` (`LogLinker` warning at map load; cosmetic —
   node is dropped, native `Atmosphere` subobject still instantiates).
   REQUIRED MANUAL STEP: open + resave `BP_Planet` in editor.
2. `FHillaireFullscreenVS` compiled but currently unreferenced (reserved for
   future fullscreen LUT passes).
3. Unused UB members (`MuSMin`, `Pad0`, composite extras in LUT passes) ride
   along harmlessly (fixed cbuffer layout).
4. SkyView sun leg is analytic rather than T-LUT-sampled (see §9).
5. `UAndromedaAtmosphereSubsystem::Components` registry array is currently
   discovery-redundant (kept for diagnostics).
6. PIE/multiplanet behavioral tests pending (see §17).

## 19. Files created

- `Source/Andromeda/Public/Atmosphere/AndromedaAtmosphereParams.h`
- `Source/Andromeda/Public/Atmosphere/AndromedaAtmosphereComponent.h`
- `Source/Andromeda/Public/Atmosphere/AndromedaAtmosphereSubsystem.h`
- `Source/Andromeda/Public/Atmosphere/AndromedaAtmosphereRenderer.h`
- `Source/Andromeda/Public/Atmosphere/AndromedaAtmosphereShaders.h`
- `Source/Andromeda/Public/Atmosphere/AndromedaAtmosphereViewExtension.h`
- `Source/Andromeda/Private/Atmosphere/AndromedaAtmosphereComponent.cpp`
- `Source/Andromeda/Private/Atmosphere/AndromedaAtmosphereSubsystem.cpp`
- `Source/Andromeda/Private/Atmosphere/AndromedaAtmosphereRenderer.cpp`
- `Source/Andromeda/Private/Atmosphere/AndromedaAtmosphereShaders.cpp`
- `Source/Andromeda/Private/Atmosphere/AndromedaAtmosphereViewExtension.cpp`
- `Shaders/Andromeda/Atmosphere/HillaireCommon.ush`
- `Shaders/Andromeda/Atmosphere/HillaireTransmittance.usf`
- `Shaders/Andromeda/Atmosphere/HillaireMultiScattering.usf`
- `Shaders/Andromeda/Atmosphere/HillaireSkyView.usf`
- `Shaders/Andromeda/Atmosphere/HillaireComposite.usf`

## 20. Files modified

- `Source/Andromeda/Public/Planet/Planet.h` (+ `AtmosphereComponent`)
- `Source/Andromeda/Private/Planet/Planet.cpp` (+ include, subobject,
  `RefreshDerivedParams`)
- `Source/Andromeda/Andromeda.Build.cs` (+ RenderCore/RHI/Renderer,
  Projects)
- `Source/Andromeda/Andromeda.h` (+ view-extension holder + delegate)
- `Source/Andromeda/Andromeda.cpp` (+ shader mapping, deferred extension)
- (Worktree also carries the earlier cleanup session's staged/unstaged
  ATMOS/ZEPHYR removals — preserved, not authored here.)

## 21. Files intentionally not modified

- `C:\Users\aless\UnrealEngineSkyAtmosphere\*` (golden reference — zero
  writes this session).
- STARMAP: `StarSystem.*`, `StarSystemGenerator.*`, `Sun.*`,
  `PlanetaryLightingComponent.*`, `PlanetaryGravitySystem.*`,
  `AndromedaPawn.*`, terrain/profile generators (untouched by this port;
  small `StarSystem`/`Sun`/`GameMode` worktree diffs belong to the prior
  cleanup session and were preserved).
- `Content/**` binaries (incl. `BP_Planet` — needs editor resave, §18).
- `Config/*.ini` (no keys needed).

## 22. Confirmation that standalone was untouched

No write tool ever targeted `C:\Users\aless\UnrealEngineSkyAtmosphere`
(read-only reads + read-only subagent audits). Its worktree modifications
(`Application/`, `DX11Base/`, `Resources/` files) pre-date this session
(user's own reference-validation work) and were left as-is.
