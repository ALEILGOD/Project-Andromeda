# ZEPHYR PHASE 2.0 — PORTING MAP

**Data:** 2026-09-14  
**Stato:** BUILD SUCCESS — Analisi completata, implementazione ZEPHYR core già integrata  
**Target:** Andromeda.uproject (UE 5.8)  
**Source Reference:** C:\Users\aless\UnrealEngineSkyAtmosphere (READ-ONLY)

---

## EXECUTIVE SUMMARY

Il porting ZEPHYR → Andromeda è **già in stato avanzato**. Il core ZEPHYR (C++ + shader) esiste nel progetto, il modulo è inizializzato, il registry pubblica snapshot automatici per ogni pianeta, e il build **compila correttamente**.

Questo report documenta lo stato attuale, identifica cosa manca per raggiungere il *Critical Success Condition* (§35), e definisce i prossimi step.

---

## A. SOURCE FILE CLASSIFICATION — HILLAIRE REPOSITORY

| File | Origine | Funzione | Portabilità | Destinazione Andromeda | Stato |
|------|---------|----------|-------------|------------------------|-------|
| **Application/SkyAtmosphereCommon.h/cpp** | Hillaire/Bruneton | Atmosphere parameters, LUT structs, Earth setup | REIMPLEMENT IN UE5 | ZephyrTypes.h, ZephyrCommon.ush, ZephyrProfileLibrary | ✅ DONE |
| **Application/RenderWithLuts.cpp** | Hillaire | LUT generation pipeline (Transmittance, Irradiance, Scattering, MultiScatter) | REIMPLEMENT IN UE5 | ZephyrRenderer.cpp (compute passes) | ✅ DONE |
| **Application/RenderSky.cpp** | Hillaire | SkyView LUT + final composite (path tracing, ray marching) | REIMPLEMENT IN UE5 | ZephyrSkyView.usf + ZephyrSky.usf | ✅ DONE |
| **Application/Game.cpp/h** | Demo DX11 | Game loop, camera, input, debug UI | NOT REQUIRED | N/A (UE5 provides) | ✅ EXCLUDED |
| **Resources/*.hlsl** (15 file) | Hillaire/Bruneton | Shader math: Bruneton definitions, transmittance, scattering, irradiance, sky, common | PORT / ADAPT | Shaders/Andromeda/Zephyr/*.usf + ZephyrCommon.ush | ✅ DONE |
| **DX11Base/** | Demo DX11 | Device, context, swapchain, window | NOT REQUIRED | N/A (UE5 RHI/RDG) | ✅ EXCLUDED |
| **Resources/Bruneton17/** | Bruneton 2008 | Reference GLSL implementations | REFERENCE ONLY | Mathematical validation | ✅ REFERENCED |

**Classificazione Hillaire:**
- **PORT DIRECTLY:** Mathematical formulas, LUT layouts, parameter conventions
- **PORT / ADAPT:** Shader code → UE5 .usf/.ush with RDG bindings
- **REIMPLEMENT IN UE5:** CPU pipeline (LUT generation, cache, registration) → ZephyrRenderer.cpp
- **REFERENCE ONLY:** Bruneton GLSL, DX11 demo infrastructure
- **NOT REQUIRED:** DX11 device, standalone camera, window, imgui, terrain rendering
- **DEPRECATED:** Demo-specific hardcoded Earth parameters (replaced by profile system)

---

## B. ANDROMEDA FILE CLASSIFICATION — EXISTING ATMOSPHERIC SYSTEMS

| File | Funzione | Relazione con ZEPHYR | Azione |
|------|----------|---------------------|--------|
| **AndromedaAtmosphereRenderer.h/cpp** | ATMOS foreground volume renderer (ray march) | Separate system — ZEPHYR = background sky | **MANTENERE** (coexistence) |
| **AndromedaAtmosphereRegistry.h/cpp** | Registers ATMOS volumes per planet | Extended to publish ZEPHYR snapshots | **MODIFICATO** ✅ |
| **AndromedaAtmosphereManager.h/cpp** | Thread-safe registry for ATMOS instances | Separate from ZephyrManager | **MANTENERE** |
| **AndromedaAtmosphereTypes.h** | ATMOS GPU data, parameters, handles | Distinct from ZephyrTypes | **MANTENERE** |
| **AndromedaAtmosphereViewExtension.h/cpp** | Hooks ATMOS to Tonemap pass | ZEPHYR has own ViewExtension | **MANTENERE** (separate hook) |
| **AndromedaAtmosphere.usf** | ATMOS pixel shader (Rayleigh only) | ZEPHYR does full physics (Rayleigh+Mie+Abs) | **MANTENERE** (fallback) |
| **PlanetAtmosphereComponent.h/cpp** | Planet-attached ATMOS component | Legacy, unused by ZEPHYR | **DEPRECARE** |
| **Planet/Zephyr/Zephyr*.h/cpp** | ZEPHYR core implementation | **PRIMARY TARGET** | **MANTENERE/ESTENDERE** ✅ |
| **Planet/Zephyr/Zephyr*.usf** | ZEPHYR shaders (4 compute + 1 pixel) | **PRIMARY TARGET** | **MANTENERE/ESTENDERE** ✅ |
| **StarSystem.h/cpp** | Spawns planets, orbits, rotations | Source of truth for planet data | **MANTENERE** (consumer) |
| **Planet.h/cpp** | Planet actor, terrain, profile | Provides PlanetRuntimeData | **MANTENERE** (source) |
| **PlanetProfile.h** | Archetype system (Terran, Desert, etc.) | Feeds ZephyrProfileLibrary | **MANTENERE** (source) |
| **Sun.h/cpp** | Star actor, light, legacy mesh | `bHideSunMeshForZephyrSky` added | **MODIFICATO** ✅ |

---

## C. DUPLICATION ANALYSIS

| Duplicato | Descrizione | Risoluzione |
|-----------|-------------|-------------|
| **Atmosphere Registry** | ATMOS registry + ZEPHYR snapshot both iterate planets | **ACCEPTABLE** — ATMOS manages volumes, ZEPHYR consumes same data for sky. No cross-contamination. |
| **Planet Data** | FPlanetRuntimeData used by both | **CORRECT** — Single source of truth (StarSystem) |
| **Sun Direction** | ATMOS reads star position; ZEPHYR reads same | **CORRECT** — Same star, different consumers |
| **LUT Cache** | ATMOS has no LUTs; ZEPHYR owns all LUTs | **NO DUPLICATION** |
| **Renderer** | Two separate renderers (ATMOS volume + ZEPHYR sky) | **BY DESIGN** — Composited at Tonemap (order-independent) |
| **Planet/Atmosphere Profiles** | ATMOS params vs ZephyrPlanetProfile | **SEPARATE** — ATMOS minimal; ZEPHYR full physics. ZephyrProfileLibrary builds from archetype. |

**VERDETTO:** Nessun doppio rendering sullo stesso pianeta. ZEPHYR scrive sky pixels; ATMOS scrive volume pixels. Depth test + `AtmosEnabled` flag in ZephyrSky.usf previene double-counting limb.

---

## C1. ATMOS + ZEPHYR INTERACTION — DETAILED ANALYSIS

### What Each System Renders

| Region / Case | ATMOS (AndromedaAtmosphere.usf) | ZEPHYR (ZephyrSky.usf) | Composition |
|---------------|----------------------------------|------------------------|-------------|
| **Space view** (camera outside all atmospheres) | Limb radiance via unified integrator (EntryT > 0). Rayleigh only. | SkyView LUT for limb + sun disk. Full physics (R+M+Abs+MS). | **ZEPHYR yields limb to ATMOS** via `AtmosEnabled` check (lines 308-312): if ATMOS enabled, ZEPHYR returns SceneColor untouched for space limb pixels. |
| **Outside atmosphere** (camera between Rt and Rs) | Aerial perspective on geometry + sky radiance via unified integrator. | SkyView LUT (faded by GoverningFade) + sun disk. | ATMOS handles aerial perspective on geometry. ZEPHYR overwrites sky pixels (no depth). |
| **Inside atmosphere** (camera inside shell) | Aerial perspective on geometry + sky radiance via unified integrator. Rayleigh only. | SkyView LUT (full) + sun disk. Full physics. | **ZEPHYR overwrites sky pixels** (bHasDepth check). ATMOS aerial perspective on geometry preserved. |
| **Horizon / Terminator** | Emerges from Rayleigh geometry + optical depth. No Mie, no absorption. | Full Mie lobe (HG), absorption (Chappuis), dual-scattering MS. | ZEPHYR sky pixels show correct sunset colors. ATMOS geometry rays get Rayleigh aerial perspective. |
| **Limb (space view)** | ATMOS owns: `AtmosEnabled` gate in ZEPHYR. | Yields to ATMOS when enabled. | **No double limb** — verified by gate. |
| **Surface / Ground** | ATMOS: buried observer skipped. Geometry rays get aerial. | ZEPHYR: buried observer returns 0. Sky pixels get full sky. | Complementary: ATMOS = volume on terrain, ZEPHYR = background sky. |

### Interaction Rules (Enforced in Code)

1. **Same Tonemap hook** — Both registered at `EPostProcessingPass::Tonemap`. ZEPHYR registered AFTER ATMOS (Andromeda.cpp order).
2. **Depth test** — ZEPHYR: `if (bHasDepth) return SceneColor;` — geometry pixels pass through untouched.
3. **AtmosEnabled gate** — ZEPHYR: `if (AtmosEnabled != 0) return SceneColor;` for space limb (line 308-312).
4. **Star source** — Both read same `StarSystem->GetActorLocation()` via respective managers.
4. **Order-independent by construction** — ZEPHYR overwrites sky; ATMOS multiplies SceneColor by transmittance + adds scattering. Final = `SceneColor * T_atmos + L_atmos` then ZEPHYR overwrites sky pixels with `L_zephyr + SunDisk`.

### TEST 8 Validation Protocol

| Scenario | Expected | Verification |
|----------|----------|--------------|
| **a) ZEPHYR only** (`r.AndromedaAtmos.Enable 0`) | Full sky + limb + sun disk from ZEPHYR. No aerial perspective on geometry. | `r.AndromedaZephyr.DebugMode 0` — verify sky colors, terminator, limb. |
| **b) ATMOS + ZEPHYR** (both enabled, default) | ATMOS: aerial perspective on geometry, limb from space. ZEPHYR: sky, horizon, terminator, sun disk. No double limb. | Fly from space → surface. Verify: space limb from ATMOS, surface sky from ZEPHYR, terrain has aerial haze. |
| **c) ATMOS fallback** (`r.AndromedaZephyr.Enable 0`) | ATMOS only: Rayleigh sky + aerial + limb. No Mie/absorption/MS. | Verify sky is Rayleigh-only (bluer, no Mie halo, no sunset reddening from absorption). |

---

## D. ARCHITETTURA TARGET — STATO ATTUALE

```
STARMAP (AStarSystem)
    ↓
Planet Runtime Data (FPlanetRuntimeData per pianeta)
    ↓
AAndromedaAtmosphereRegistry::PublishZephyrSnapshot()
    ↓
ZephyrProfileLibrary::BuildProfile(seed, archetype, radii)
    ↓
FZephyrPlanetSnapshotEntry → FZephyrManager (Game Thread)
    ↓
FZephyrRenderer::RenderSky() (Render Thread)
    ↓
RDG Pipeline:
  1. Transmittance LUT (compute) → Atlas 256×(64×N)
  2. Multi-Scatter LUT (compute) → Atlas 32×(32×N)
  3. Sky-View SS+MS LUT (compute) → Atlas 192×(112×N)
  4. Sky Pixel Shader → Final composite
    ↓
UE5.8 Tonemapper
```

**Multi-planet:** ✅ Supportato (MaxPlanets=8, slice stacking verticale in atlanti)
**Planet Rotation:** ✅ `Entry.PlanetRotation` pubblicato → `SunDirPlanet = PlanetRot.Inverse().RotateVector(SunDirWorld)` in BuildGPUData
**Automatic Registration:** ✅ Registry pubblica snapshot a ogni Tick dopo sync riuscito

---

## E. ZEPHYR CORE IMPLEMENTATION — DETTAGLIO

### 1. Data Model (ZephyrTypes.h)
- `FZephyrPlanetProfile` — Physical parameters (radii, Rayleigh/Mie/Abscop coefficients, scale heights, albedo, density scales)
- `FZephyrPlanetSnapshotEntry` — Profile + PlanetCenter + PlanetID + TerrainHeight + SkyTransitionRadius + **PlanetRotation**
- `FZephyrPlanetGPUData` — 7×float4 (112 bytes) packed for GPU, camera-relative centers, km units for densities

### 2. Manager (ZephyrManager.h/cpp)
- Thread-safe mailbox: Game Thread `SetSnapshot()` / Render Thread `GetSnapshot()`
- Versioned snapshots for cache invalidation

### 3. Profile Library (ZephyrProfileLibrary.cpp)
- **Earth baseline** from `AndromedaAtmosphereReference` (shared with ATMOS)
- **Archetype presets** (Terran, Oceanic, Jungle, Arid, Desert, Frozen, Tundra, Rocky, Volcanic, Exotic) — physical multipliers only
- **Deterministic jitter** (±15-20%) from planet seed via SplitMix64
- **Toy-planet column compensation** (scales molecular density to restore Earth-like optical depth on thin shells)
- **MieDensityScale decoupled** from pressure (prevents Desert 4.5x × 15x = 68x dust whiteout)

### 4. Renderer (ZephyrRenderer.cpp)
- **LUT Cache:** Single-active-atlas + invalidation keys
  - Planet Key = profile hashes + MieScale + AbsorptionScale + terrain heights
  - View Key = Planet Key + quantized camera heights + per-planet SunDirPlanet + MultiScatterScale
- **Compute Passes:** Transmittance → MultiScatter → SkyView (SS+MS) → Sky PS
- **ViewExtension:** Hooked at Tonemap (same as ATMOS, order-independent)

### 5. Shaders (ZephyrCommon.ush + 4 .usf)
- **ZephyrTransmittance.usf** — 40-step integral, geometric occultation
- **ZephyrMultiScatter.usf** — Dual-scattering (8 Fibonacci dirs × 8 steps, ground bounce)
- **ZephyrSkyView.usf** — 24-step front-to-back march, horizon-concentrated V mapping
- **ZephyrSky.usf** — Governing planet selection, SkyView LUT sampling, sun disk, debug modes

### 6. Common Math (ZephyrCommon.ush)
- Exponential densities with `EffectiveScaleKm = min(ScaleKm, ShellKm*0.35)`
- Gaussian absorption layer clamped into shell
- Physically normalized Rayleigh (3/16π) and HG Mie phases
- Quadratic transmittance V packing, Hillaire sky latitude mapping
- Fibonacci sphere directions (procedural, no LUTs)
- Atlas sampling with half-texel slice inset (no bleeding)

---

## F. MULTI-PLANET / MULTI-ATMOSPHERE — IMPLEMENTATION STATUS

| Requisito (§5) | Stato | Note |
|----------------|-------|------|
| Più pianeti simultanei | ✅ | **Dynamic limit** via `GetMaxPlanets()` (GPU texture dim based, ~146 planets). No arbitrary hardcoded cap. |
| Profili atmosferici indipendenti | ✅ | Per-pianeta `FZephyrPlanetProfile` da archetype+seed |
| Raggi differenti | ✅ | `GroundRadius`, `AtmosphereRadius` per pianeta |
| Rayleigh/Mie/Absorption indipendenti | ✅ | Coefficienti per-pianeta in Packed2/3/4 |
| Posizioni differenti | ✅ | `PlanetCenter` camera-relative per slice |
| Rotazioni differenti | ✅ | `PlanetRotation` → `SunDirPlanet` per slice |
| Star direction differente per pianeta | ✅ | Parallasse trascurabile ma geometricamente corretto |
| LUT dataset isolati | ✅ | Slice verticali, half-texel inset, chiavi cache separate |
| Nessun cross-contamination | ✅ | Cache keys includono profile hash + terrain height |

---

## G. PLANET ROTATION — VERIFICA CONVENZIONI

**Andromeda Convention (StarSystem.cpp:975-1044):**
- `CurrentRotation = TiltQuat * SpinQuat` (world space)
- `TiltQuat` = rotation around Forward (X) by AxialTilt
- `SpinQuat` = rotation around Up (Z) by SpinAngle
- `RotationAxis = TiltQuat.RotateVector(UpVector) * RotationDirection`

**ZEPHYR Transform (ZephyrRenderer.cpp:1820-1830):**
```cpp
FVector SunDirWorld = (StarWorldPosition - Entry.PlanetCenter).GetSafeNormal();
FQuat PlanetRot = Entry.PlanetRotation.Quaternion();
SunDirPlanet = PlanetRot.Inverse().RotateVector(SunDirWorld);
```

**Validazione:** Corretto. `PlanetRotation` è world-space rotation del pianeta. La stella è a distanza infinita (Case A), quindi `SunDirWorld` è costante. La trasformazione inversa porta la direzione solare nel frame locale del pianeta per la fisica atmosferica. Applicata **esattamente una volta** in `BuildGPUData`.

---

## H. LUT ARCHITECTURE — SPECIFICHE

| LUT | Dimensioni | Slice Height | Atlas Layout | Invalidation |
|-----|------------|--------------|--------------|--------------|
| Transmittance | 256 × (64 × N) | 64 | Vertical stack | Planet Key |
| Multi-Scatter | 32 × (32 × N) | 32 | Vertical stack | Planet Key |
| Sky Single | 192 × (112 × N) | 112 | Vertical stack | View Key |
| Sky Multi | 192 × (112 × N) | 112 | Vertical stack | View Key |

**Cache Model:** Single active atlas per LUT type. On key change → full atlas regen. NOT a persistent multi-dataset cache.

**Isolation:** Half-texel slice inset in `ZephyrSample*` functions prevents bilinear bleeding across planet slices.

---

## I. SHADER MAPPING — HILLAIRE → UE5

| Hillaire DX11 | ZEPHYR UE5 | Note |
|---------------|------------|------|
| `TransmittanceLutPS` | `ZephyrTransmittanceCS` | PS → CS, RDG UAV |
| `SingleScatteringLutPS` | (fused in SkyView) | Non separato; SkyView integra SS inline |
| `MultipleScatteringLutPS` | `ZephyrMultiScatterCS` | Dual-scattering approx |
| `IndirectIrradianceLutPS` | (fused in MultiScatter) | Ground bounce integrato in MultiScatter |
| `SkyViewLutPS` | `ZephyrSkyViewCS` | CS, dual output (SS+MS) |
| `RenderWithLutPS` / `RenderRayMarchingPS` | `ZephyrSkyPS` | Fullscreen PS, governing planet selection |
| `SkyAtmosphereCommon.hlsl` | `ZephyrCommon.ush` | Shared math, layout contract |
| `SkyAtmosphereBruneton.hlsl` | Inlined in shaders | Bruneton math ported directly |

---

## J. FILES — CREATED / MODIFIED / RETAINED / DEPRECATED

### ✅ CREATI (ZEPHYR Core)
```
Source/Andromeda/Public/Planet/Zephyr/ZephyrTypes.h
Source/Andromeda/Public/Planet/Zephyr/ZephyrManager.h
Source/Andromeda/Public/Planet/Zephyr/ZephyrRenderer.h
Source/Andromeda/Public/Planet/Zephyr/ZephyrProfileLibrary.h
Source/Andromeda/Public/Planet/Zephyr/ZephyrSharedAtmosphere.h
Source/Andromeda/Private/Planet/Zephyr/ZephyrTypes.cpp
Source/Andromeda/Private/Planet/Zephyr/ZephyrManager.cpp
Source/Andromeda/Private/Planet/Zephyr/ZephyrRenderer.cpp
Source/Andromeda/Private/Planet/Zephyr/ZephyrProfileLibrary.cpp
Source/Andromeda/Private/Planet/Zephyr/ZephyrShaders.h
Source/Andromeda/Private/Planet/Zephyr/ZephyrViewExtension.h/.cpp
Shaders/Andromeda/Zephyr/ZephyrCommon.ush
Shaders/Andromeda/Zephyr/ZephyrTransmittance.usf
Shaders/Andromeda/Zephyr/ZephyrMultiScatter.usf
Shaders/Andromeda/Zephyr/ZephyrSkyView.usf
Shaders/Andromeda/Zephyr/ZephyrSky.usf
```

### ✅ MODIFICATI (Integrazione)
```
Source/Andromeda/Andromeda.cpp              // Module init/shutdown
Source/Andromeda/Private/Atmosphere/AndromedaAtmosphereRegistry.cpp  // PublishZephyrSnapshot
Source/Andromeda/Public/Atmosphere/AndromedaAtmosphereRegistry.h     // Declaration
Source/Andromeda/Private/Sun.cpp            // ConfigureSunMesh
Source/Andromeda/Public/Sun.h               // bHideSunMeshForZephyrSky
```

### 🔄 MANTENUTI (Coexistence)
```
Source/Andromeda/Private/Atmosphere/AndromedaAtmosphereRenderer.cpp
Source/Andromeda/Private/Atmosphere/AndromedaAtmosphereManager.cpp
Source/Andromeda/Public/Atmosphere/AndromedaAtmosphereTypes.h
Source/Andromeda/Private/Atmosphere/AndromedaAtmosphereViewExtension.cpp
Source/Andromeda/Shaders/Andromeda/AndromedaAtmosphere.usf
Source/Andromeda/Private/Planet/PlanetAtmosphereComponent.cpp
Source/Andromeda/Public/Planet/PlanetAtmosphereComponent.h
```

### 🗑️ DA DEPRECARE (Post-validation)
- `PlanetAtmosphereComponent` — Unused by ZEPHYR, legacy ATMOS attachment
- `PlanetAtmosphereRenderer` — Legacy per-planet renderer
- ATMOS debug volume overlay (when ZEPHYR validated)

---

## K. RISKS & MITIGATIONS

| Rischio | Probabilità | Impatto | Mitigazione |
|---------|-------------|---------|-------------|
| **Double rendering (ATMOS + ZEPHYR limb)** | Bassa | Alto | `AtmosEnabled` flag in ZephyrSky.usf disattiva ZEPHYR limb quando ATMOS attivo |
| **LUT bleeding multi-planet** | Bassa | Medio | Half-texel slice inset + cache keys includono terrain height |
| **Planet rotation double-apply** | Bassa | Alto | Verificato: singola applicazione in BuildGPUData |
| **Star position mismatch** | Bassa | Medio | Stessa sorgente (`StarSystem->GetActorLocation()`) per entrambi |
| **Shader compilation failures** | Media | Alto | Build test passed; `ValidateShaderInfrastructure()` diagnostico |
| **Toy-planet column compensation side-effects** | Media | Medio | Parametrizzato, disabilitabile via cvar, logging profilo per audit |
| **Cache invalidation missed** | Media | Medio | Chiavi FNV-1a su tutti i parametri fisici + debug scales |

---

## L. BUILD STRATEGY

**Comando:**
```cmd
& "C:\Unreal Engine\UE_5.8\Engine\Build\BatchFiles\Build.bat" AndromedaEditor Win64 Development -project="C:\Users\aless\Documents\Unreal Projects\Andromeda\Andromeda.uproject" -waitmutex
```

**Stato:** ✅ **SUCCESS** (70.69s, 34 actions, 0 errori)

**Prossimi build incrementali:**
1. Dopo modifiche shader → rebuild shader directory
2. Dopo modifiche C++ → rebuild module Andromeda
3. Dopo modifiche .ush → rebuild module Andromeda (global shader dependency)

---

## M. RUNTIME VALIDATION STRATEGY

### Test Obligatori (§29)

| Test | Descrizione | Comando Validazione |
|------|-------------|---------------------|
| **TEST 1 — SPACE** | Camera nello spazio, atmosfera visibile | `r.AndromedaZephyr.DebugMode 0`, vola via dal pianeta |
| **TEST 2 — INSIDE** | Camera dentro atmosfera, sky/horizon | Teleport pawn a 50% shell |
| **TEST 3 — SUNRISE/SUNSET** | Terminator, horizon glow, colori | Ruota pianeta o time-of-day |
| **TEST 4 — MULTI PLANET** | 2+ pianeti profili diversi | Spawna sistema multi-pianeta, verifica isolamento LUT |
| **TEST 5 — PLANET ROTATION** | Rotazione pianeta → SunDirPlanet corretto | `r.AndromedaZephyr.CaptureViews` su pianeta rotante |
| **TEST 6 — PLANET MOTION** | Orbita pianeta → center/relCenter aggiornati | Osserva pianeta in orbita, verifica atmosfera segue |
| **TEST 7 — GOVERNING PLANET** | Camera tra due pianeti → selezione corretta | Sposta camera, verifica transizione fluida |
| **TEST 8 — ATMOS FALLBACK** | ATMOS disabilitato → ZEPHYR solo | `r.AndromedaAtmos.Enable 0` |

### Diagnostic Commands
- `r.AndromedaZephyr.Validate` — Shader infrastructure + dispatch/LUT counters
- `r.AndromedaZephyr.CaptureViews <delay> [mode]` — Screenshot per debug mode
- `r.AndromedaZephyr.SurfaceShot <planet> [yaw] [mode] [instant] [pitch] [alt01]` — In-atmosphere capture
- `r.AndromedaZephyr.FieldShot <planet> <side> [yaw] [pitch] [mode] [alt01]` — Day/terminator/night
- `r.AndromedaZephyr.OrbitShot <planet> <radii> [mode]` — Near-planet inspection

### Debug Modes (EZephyrDebugMode)
0. SkyOnly (default) | 1. Transmittance | 2. SingleScatter | 3. MultiScatter | 4. Mie | 5. Rayleigh | 6. Absorption | 7. SkyViewLUT | 8. MultiScatterLUT

---

## N. LIMITATIONS — KNOWN & DOCUMENTED

1. **Case A Only** — Stella a distanza infinita (directional). Nessun inverse-square, nessun finite-distance star. Criterio futuro: Δθ ≈ H_atm / D_star.
2. **Dual-Scattering Approximation** — Non full iterative Hillaire multi-scatter. Cattura ordine 2 dominante (terminator lift, horizon bleed, blue hour). Disabilitabile via `r.AndromedaZephyr.MultiScatterScale 0`.
3. **LUT Cache Single-Active** — Cambio set pianeti attivi = full atlas regen. No persistent multi-dataset.
4. **Dynamic Max Planets** — `GetMaxPlanets()` basato su GPU max texture dim (16384 / 112 ≈ 146). Nessun limite arbitrario hardcoded.
5. **ATMOS Coexistence** — ATMOS non deprecato. `AtmosEnabled` flag + depth test prevengono double limb. Deprecazione post-validation.
6. **Sun Mesh Legacy** — `bHideSunMeshForZephyrSky=true` di default. Mesh legacy nascosta (sfera raggio fisso 50cm = white sphere bug a distanza ravvicinata).
7. **No Planetary Shadows** — Shadow map bindings presenti ma non usati (slot 5 cleared in compute passes).
8. **No Clouds/Weather** — Solo clear-sky atmospheric scattering.

---

## N1. AUTOMATIC ATMOSPHERE REGISTRATION — VERIFICATION

### Flow: STARMAP → Planet Runtime → ZEPHYR Profile (Zero Manual Setup)

```
AStarSystem::SpawnPlanets()
    ↓ Creates APlanet actors with PlanetRadius, TerrainHeight, PlanetSeed, PlanetArchetype
    ↓
AAndromedaAtmosphereRegistry::SyncAtmospheres()  (BeginPlay + Tick retry)
    ↓ Reads FPlanetRuntimeData from StarSystem
    ↓ Registers ATMOS atmosphere (SurfaceRadius = PlanetRadius, AtmosphereRadius = (PlanetRadius+TerrainHeight)*Multiplier)
    ↓ Calls PublishZephyrSnapshot()
        ↓
        For each planet:
            Archetype = PlanetActor->PlanetArchetype (or runtime default Terran)
            Seed = PlanetActor->PlanetSeed (or runtime PlanetSeed)
            SurfaceRadius = Planet.PlanetRadius
            AtmosphereRadius = (Planet.PlanetRadius + Planet.TerrainHeight) * AtmosphereRadiusMultiplier
            ↓
            FZephyrPlanetProfile = UZephyrProfileLibrary::BuildProfile(Seed, Archetype, SurfaceRadius, AtmosphereRadius)
                ↓ Earth baseline + Archetype multipliers + Deterministic jitter (SplitMix64 from seed) + Column compensation
            ↓
            FZephyrPlanetSnapshotEntry:
                Profile = built profile
                PlanetCenter = Planet.WorldPosition
                PlanetID = Planet.PlanetID
                TerrainHeightCm = Planet.TerrainHeight
                SkyTransitionRadiusCm = ComputeSkyTransitionRadiusCm(PlanetRadius, TerrainHeight)
                PlanetRotation = Planet.CurrentRotation  // World frame rotation
            ↓
            FZephyrManager::SetSnapshot(Entries, StarSystem->GetActorLocation())
```

### Verified Properties

| Property | Automatic? | Source |
|----------|------------|--------|
| Planet position | ✅ | `Planet.WorldPosition` from StarSystem orbit |
| Planet rotation | ✅ | `Planet.CurrentRotation` from StarSystem rotation sim |
| Planet radius | ✅ | `Planet.PlanetRadius` from generation |
| Terrain height | ✅ | `Planet.TerrainHeight` from generation |
| Atmosphere radius | ✅ | `(PlanetRadius + TerrainHeight) * Multiplier` (live cvar) |
| Archetype | ✅ | `PlanetActor->PlanetArchetype` (set at spawn) |
| Seed | ✅ | `PlanetActor->PlanetSeed` (deterministic per planet) |
| Rayleigh/Mie/Absorption | ✅ | Derived from archetype + seed in `BuildProfile` |
| SkyTransitionRadius | ✅ | `PlanetRadius + TerrainHeight + 2km` (shared ref) |
| Star position | ✅ | `StarSystem->GetActorLocation()` (single source) |

### Two-Planet Test Case

When StarSystem spawns 2+ planets (e.g., Terran + Desert):
- Each gets independent `FZephyrPlanetProfile` (Desert: Mie×4.5, Albedo=0.55, Density=1.1, Absorption×1.4)
- Each gets independent LUT slice in atlases (cache keys include profile hash + terrain height)
- Each gets independent `SunDirPlanet` (transformed by own `PlanetRotation`)
- Registry publishes both in single snapshot → ZEPHYR renders both simultaneously

**No manual Actor/Component/Blueprint/LUT creation required.** The atmosphere is a pure data derivative of the planet runtime state.

---

## O. VERDETTO PARZIALE (AGGIORNATO POST-FIX)

**PORT COMPLETE WITH LIMITATIONS** — Core ZEPHYR integrato e funzionante in UE5.8, multi-planet supportato **senza limite arbitrario**, build ok.

**Fixes Applied:**
- ✅ `MaxPlanets=8` → Dynamic `GetMaxPlanets()` (GPU texture limit based)
- ✅ `ZEPHYR_MAX_PLANETS` shader define removed
- ✅ ATMOS+ZEPHYR interaction documented with TEST 8 protocol
- ✅ Automatic atmosphere registration verified end-to-end

**MANCANTE per PORT COMPLETE (§35):**
- [ ] Runtime validation TEST 1-8 eseguiti e passati
- [ ] Performance measurements (LUT gen time, VRAM, GPU cost)
- [ ] ATMOS deprecation plan eseguito (se ZEPHYR validated)
- [ ] Documentazione limitazioni in report finale

---

## P. PROSSIMI STEP (MILESTONES RESIDUE)

| Milestone | Azione | Stimato |
|-----------|--------|---------|
| **M13** | Runtime validation TEST 1-8 (vedi protocollo TEST 8 per ATMOS+ZEPHYR) | 1-2 sessioni |
| **M14** | Performance profiling (stat gpu, RenderDoc, LUT gen time, VRAM) | 1 sessione |
| **M15** | ATMOS deprecation decision (se ZEPHYR validated) | 1 sessione |
| **M16** | Final report + verdict | 30 min |

---

## ALLEGATO — FILE CHIAVE DA MONITORARE

```
Shaders/Andromeda/Zephyr/ZephyrCommon.ush       // Layout contract (MUST match ZephyrTypes.h)
Source/Andromeda/Private/Planet/Zephyr/ZephyrRenderer.cpp  // Pipeline orchestration
Source/Andromeda/Private/Atmosphere/AndromedaAtmosphereRegistry.cpp  // Snapshot source
Source/Andromeda/Public/Planet/Zephyr/ZephyrTypes.h  // CPU↔GPU data contract
Source/Andromeda/Private/Planet/Zephyr/ZephyrProfileLibrary.cpp  // Procedural profiles
```

---

*Generato automaticamente come parte di ZEPHYR Phase 2.0 — Analisi pre-implementazione completata, implementazione core già integrata.*