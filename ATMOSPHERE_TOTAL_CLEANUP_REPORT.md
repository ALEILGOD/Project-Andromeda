# ATMOSPHERE TOTAL CLEANUP REPORT — Project Andromeda (UE 5.8)

## 1. Initial State (pre-cleanup)

The project had accumulated several overlapping atmosphere/sky systems:

- **Hillaire multi-planet port** (`Source/…/Public+Private/Atmosphere/AndromedaAtmosphere*`,
  `Shaders/Andromeda/Atmosphere/Hillaire*.usf/.ush`, view extension, RDG LUT passes,
  `UAndromedaAtmosphereComponent`, subsystem, renderer, `r.AndromedaAtmosphere.Enable`).
- **ATMOS legacy** (manager, registry, renderer, unified renderer, view extensions,
  `UPlanetAtmosphereComponent`, `PlanetAtmosphereRenderer`, `AndromedaAtmosphere.usf`,
  `M_UAS_Atmosphere`, `SM_Athmoshpere` generated mesh).
- **ZEPHYR legacy** (`Public/Planet/Zephyr/`, `Private/Planet/Zephyr/`,
  `Shaders/Andromeda/Zephyr/`, LUT pipeline, view extension, profile library).
- Unified glue (mailbox, dual-stage renderer, light reference component, registry auto-spawn).

Cleanup strategy per mandate: full removal down to a CLEAN BASE
(STARMAP + Planet Generation + Solar System + Planetary Lighting + Gameplay),
no new atmosphere, no refactoring of valid systems.

## 2. Deleted (cumulative, working tree)

- `Source/Andromeda/Public/Atmosphere/` — entire Hillaire module headers (6 files).
- `Source/Andromeda/Private/Atmosphere/` — entire Hillaire module sources (5 files).
- `Shaders/Andromeda/Atmosphere/` — all 5 Hillaire shaders (`HillaireCommon.ush`,
  `HillaireTransmittance/SkyView/MultiScattering/Composite.usf`).
- ATMOS legacy: `AndromedaAtmosphereManager/Registry/Renderer/Shader/System/UnifiedCommands/
  ViewExtension/UnifiedRenderer`, `AtmosphereLightReferenceComponent`,
  `PlanetAtmosphereComponent`, `PlanetAtmosphereRenderer`,
  `Shaders/Andromeda/AndromedaAtmosphere.usf`.
- ZEPHYR: `Public/Planet/Zephyr/` (5 headers), `Private/Planet/Zephyr/` (7 files),
  `Shaders/Andromeda/Zephyr/` (5 files).
- Assets: `Content/Maps/_GENERATED/aless/SM_Athmoshpere.uasset`,
  `Content/Materials/M_UAS_Atmosphere.uasset`.
- `Tools/ZephyrSim/` — offline Python simulator + report + swatches (5 files,
  never referenced by game runtime). Removed in FINAL MICRO CLEANUP.
- Empty dir `Shaders/Andromeda/` (atmosphere-only shader mount point, mapping already
  removed). Removed in FINAL MICRO CLEANUP. Generic `Shaders/` and `Tools/` roots kept.

## 3. Modified

- `Source/Andromeda/Public/Planet/Planet.h` — removed `Atmosphere`/`AtmosphereMesh`
  members, `GenerateAtmosphereMesh()` decl. Terrain, profile, lighting intact.
- `Source/Andromeda/Private/Planet/Planet.cpp` — removed `UPlanetAtmosphereComponent`
  creation, `AtmosphereMesh` creation, `InitializeAtmosphere()` call,
  `GenerateAtmosphereMesh()` (186 lines). Mesh generation + `M_Planet` intact.
- `Source/Andromeda/Andromeda.h` / `Andromeda.cpp` — removed shader directory mapping,
  view-extension registration, and (FINAL MICRO CLEANUP) the dead `PostEngineInitHandle`
  + its unregister block + now-unused `Misc/CoreDelegates.h` include.
  Module is now an empty startup/shutdown shell; STARMAP/Planetary Lighting untouched.
- `Source/Andromeda/Andromeda.Build.cs` — removed `RenderCore/RHI/Renderer` deps and
  private Renderer include paths. Kept: Core, CoreUObject, Engine, InputCore,
  ProceduralMeshComponent, Projects.
- `Source/Andromeda/Public/Sun.h` / `Private/Sun.cpp` — removed
  `AtmosphereLightReference` member + `bHideSunMeshForZephyrSky` flag; decorative sun
  mesh restored visible. Lights (`SunLight`, `SpaceAmbientLight`) intact.
- `Source/Andromeda/Public+Private/AndromedaGameMode.*` — removed atmosphere Registry
  auto-spawn (ATMOS-03). Default pawn (`AAndromedaPawn`) intact.
- `Source/Andromeda/Public/StarSystem.h` / `Private/StarSystem.cpp` — comment-only
  (removed AtmosphereLightReference/Registry mentions; API unchanged).
- `Content/Blueprints/Planets/BP_Planet.uasset` — resaved via Editor automation
  (`UnrealEditor-Cmd -run=pythonscript`): stale `Atmosphere` subobject
  (`AndromedaAtmosphereComponent` era + `PlanetAtmosphereComponent` era) dropped
  on load and purged by forced save (29685 → 29245 bytes). No valid component touched.

## 4. Preserved

- **STARMAP**: `AStarSystem`, `UStarSystemGenerator`, spawn/orbit/rotation/simulation —
  diff review shows comment-only changes; runtime log shows Sun + 7 planets spawned.
- **Planetary Lighting**: `UPlanetaryLightingComponent` + Sun lights + `MF_PlanetaryLighting`.
- Planet generation (terrain/biome/continental/landform/depression/profile),
  seeds, gravity (`PlanetaryGravitySystem`), Pawn, GameMode, PlayerController,
  `BP_StarSystem`, `BP_Sun`, `M_Planet`, `M_Sun`, `Andromeda_Main` map.
- Historical docs (NOT deleted per mandate): `ZEPHYR_PHASE_*`, `ATMOS_ZEPHYR_CLEANUP_REPORT.md`,
  `HILLAIRE_*`, `SKY_ATMOSPHERE_*`. Historical code comments kept as-is.
- Standalone reference `C:\Users\aless\UnrealEngineSkyAtmosphere` — never touched.

## 5. Blueprint Cleanup

- `BP_Planet` kept. Stale `Atmosphere` subobject removed via load + forced resave in
  `UnrealEditor-Cmd` (pythonscript commandlet). Verified: fresh load emits **zero**
  `Unable to load Atmosphere` warnings; stale `M_UAS_Atmosphere` dependency warning gone.
- `Root`, `PlanetProceduralMesh` (+`M_Planet`), `PlanetaryLighting` and all other
  valid components preserved (C++ parent unchanged in that respect; BP diff is
  package-size-only, 29685 → 29245 bytes).

## 6. Shader Cleanup

- Zero custom `.usf`/`.ush` remain in the project (`Shaders/Andromeda/` removed as empty).
- Zero `IMPLEMENT_GLOBAL_SHADER` / uniform-buffer / shader-parameter structs in `Source`.
- `Intermediate/ShaderAutogen` regenerates without atmosphere entries (autogen unchanged/valid).

## 7. View Extension Cleanup

- Zero `FSceneViewExtension` / `RegisterViewExtension` / `SubscribeToPostProcessingPass`
  in `Source`. No `OnPostEngineInit` usage remains (dead handle removed; the C4996
  deprecation warning is gone from the build).

## 8. RDG Cleanup

- Zero `FRDGBuilder` / `ENQUEUE_RENDER_COMMAND` / fullscreen/post-process atmosphere passes
  in `Source`. Generic engine RDG untouched.

## 9. Config Cleanup

- `Config/` contains no atmosphere/sky CVars (`r.AndromedaAtmosphere.*`,
  `r.AndromedaAtmos.*`, `r.AndromedaZephyr.*` all absent). Only generic UE renderer/input
  settings remain.

## 10. Build — PASS

`Build.bat AndromedaEditor Win64 Development` → **Result: Succeeded** (21.4 s,
`Andromeda.cpp` recompiled + relink after micro-cleanup). Remaining warnings are
pre-existing and unrelated (VS toolchain newer-than-preferred, include-order notice,
engine `TextureCube::GetAssetRegistryTags` deprecation).

## 11. Editor — PASS

`UnrealEditor-Cmd` pythonscript runs load `BP_Planet` cleanly (no LogLinker warnings);
asset registry + shader autogen healthy; map check 0 errors in prior full-editor boot.

## 12. PIE — PASS (clean base, no custom atmosphere)

Fresh `-game` run of `/Game/Maps/Andromeda_Main`: `Game class AndromedaGameMode`,
`Bringing World up for play`, `LoadMap complete` (41.7 s), `StarSystem: Sun spawned`,
`Planet 0–6 spawned` with seeds/radii/orbits. **Zero** atmosphere/sky warnings or errors.
No custom atmosphere rendered, as mandated.

## 13. Residue Scan (post-cleanup, `Source`)

- Functional matches for Hillaire/ATMOS/ZEPHYR/Scattering/Transmittance/Irradiance/
  SkyView/LUT/RDG/ViewExtension/CurrentPlanet/GlobalAtmosphere/CVars: **ZERO**.
- Remaining hits are explicitly PRESERVED: historical comments
  (`AndromedaGameMode.h:33`, `AndromedaGameMode.cpp:21-23`, `StarSystem.h:229`,
  `PlanetaryGravitySystem.h:53,120`, `PlanetBiomeGenerator.cpp:863` — biome climate
  model comment), and legitimate engine API `USkyLightComponent` ambient fill in
  `Sun.h/.cpp` (not a custom sky renderer).

## FINAL MICRO CLEANUP (this phase)

| Item | Action | Result |
|---|---|---|
| `BP_Planet` stale `Atmosphere` subobject | Editor automation load + forced resave | FIXED — 0 warnings on fresh load |
| `M_UAS_Atmosphere` stale reference | dropped with the subobject purge | GONE from logs |
| `Tools/ZephyrSim/` (5 files) | deleted | GONE (disk verified) |
| Dead `PostEngineInitHandle` | removed from `Andromeda.h/.cpp` + unused include | GONE, deprecation warning gone |
| `Shaders/Andromeda/` (empty, atmosphere-only) | removed | GONE; generic `Shaders/`, `Tools/` roots kept |
| Historical reports / comments | explicitly NOT touched | PRESERVED |
| Residue scan | full term list re-run | only comments + engine API |
| Build | rebuilt | PASS |
| Editor / PIE | fresh runs + log grep | PASS, STARMAP + Lighting live, no atmosphere |

Final state: **CLEAN BASE** — STARMAP, Planet Generation, Solar System, Planetary
Lighting, Sun, Gameplay present; Hillaire, ATMOS, ZEPHYR, custom atmosphere/sky,
LUTs, scattering, RDG passes, view extensions, global shaders, components, managers,
registries all absent. Ready for a future from-scratch atmosphere implementation.
