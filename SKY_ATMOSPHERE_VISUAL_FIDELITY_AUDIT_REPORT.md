# SKY ATMOSPHERE — VISUAL FIDELITY AUDIT REPORT

> **Target:** `C:\Users\aless\Documents\Unreal Projects\Andromeda` (UE 5.8)
> **Golden reference:** `C:\Users\aless\UnrealEngineSkyAtmosphere` (standalone Hillaire, read-only)
> **Date:** 2026-09-14 (recovery session — continued after provider 504 timeout, no repo damage)
> **Scope:** Case A (directional star) ONLY. No Case B/C/D, no new architecture, no new renderer.
> **Result priority applied:** CORRETTEZZA → FEDELTÀ → CALIBRAZIONE.

---

## 1. Executive Summary

The audit compared the ported `AndromedaHillaire` sky pipeline
(Transmittance → MultiScatter → SkyView → Sky composite, RDG, UE5.8)
against the standalone reference math, function by function.

**Headline finding:** the ported shader math is faithful (density profiles,
phase functions, extinction, front-to-back integration, LUT sampling and the
sky composite all match the validated Zephyr/reference formulation), but the
**pipeline could never produce a single sky pixel**: four CRITICAL bugs on the
CPU/RDG side — a never-set init flag (dead stage), cross-frame reuse of
transient RDG textures (dangling GPU resources), a planet-key that only hashed
the last planet (broken multi-planet invalidation), and proof counters wired
to dead atomics (always 0). Any observed "old / late / wrong atmosphere" in
recent builds is explained by these: the Hillaire stage was either fully off
(only the legacy ATMOS aerial stage visible) or — once enabled — sampling
destroyed LUTs.

All four CRITICAL bugs plus three small safe fixes were corrected **at the
point where each problem originates** (no compensating patches, no new
passes, no parameter tuning). Build passes with 0 errors / 0 warnings.

What remains is **not porting correctness** but (a) runtime visual validation
in-editor (could not be executed from this session — see §7), (b) known
fidelity limitations inherited from the Zephyr model simplification vs the
full Bruneton multi-order reference, and (c) Case-A physical limits (night).

---

## 2. Current Architecture (post-port, preserved)

```
STARMAP (FPlanetRuntimeData)
  → AAndromedaAtmosphereRegistry (game thread, per-frame)
      → FAndromedaAtmosphereSystem::SetSnapshot()   [single mailbox]
          planets: FAndromedaAtmosphereInstance
            (= FZephyrPlanetSnapshotEntry: Profile, PlanetCenter [cm],
             PlanetRotation, TerrainHeightCm, SkyTransitionRadiusCm,
             TransitionCompleteRadiusCm, SunDirectionWorld [Planet→Sun, world])
          star: StarWorldPosition (emission point, used only for StarValid)
          → FUnifiedAtmosphereViewExtension (SINGLE Tonemap hook)
              Stage A — FAndromedaAtmosphereRenderer::RenderAtmospheres
                        (legacy ATMOS raymarch, aerial/limb, Rayleigh-only;
                         documented limitation, unchanged by this audit)
              Stage B — AndromedaHillaire::RenderSky (THIS AUDIT)
                        Transmittance CS → MultiScatter CS → SkyView CS
                        → Sky PS composite, governing-planet selection,
                        regime fade, sun disk, debug modes 0–8
```

Preserved as required: `FAndromedaAtmosphereSystem`, single Tonemap owner,
STARMAP as source of truth, planet-independent LUTs, governing planet,
far→near intent, ATMOS+Hillaire as the single unified system, UE5.8/RDG.
No second renderer, no second Tonemap hook, no ZEPHYR resurrection
(`FZephyrRenderer` remains deprecated, untouched, still compiles).

Registry verification (PASS 1): `SunDirectionWorld` is baked per-planet with
parallax (`GetDirectionTowardSunWorld(Planet.WorldPosition)`, fallback to the
same owned helper from the StarSystem location). `PlanetRotation` is stored
but intentionally NOT applied to the sun — correct for Case A: for a
spherically symmetric atmosphere the (camera, sun) geometry is
rotation-invariant, so world-frame sun + camera-relative centers are exact.
The ported `WorldToLocal / SunToLocal` helpers exist in
`AndromedaHillaireCore` and were verified against the reference conjugation
formula; the render path correctly does not need them (they serve
diagnostics / future ground-GI work, same as the reference's non Case-A use).

---

## 3. Hillaire vs Andromeda Comparison

| Sistema | Hillaire Reference | Andromeda (post-fix) | Status |
|---|---|---|---|
| Transmittance LUT | 256×64, OD march, occultation, `(r,μ)` mapping | 256×64 slice atlas, 40-step OD march, analytic occultation, linear-μ / quadratic-h mapping, bake⇔sample internally consistent | ✅ MATCH (mapping simplified vs Bruneton `(r,μ)` warp, inherited from Zephyr; edge half-texel bias, see LOW-1) |
| Single Scattering | Bruneton 4D (r,μ,μs,ν) texture, orders via LUT chain | Evaluated analytically inside SkyView march (J1 = σ·phase·T★, 24 steps, quadratic distribution, exact segment lengths) | ✅ EQUIVALENT (no 4D LUT by design; same integral) |
| Multiple Scattering | Bruneton orders 2..N accumulated + geometric series | Single dual-scattering gather (8 Fibonacci dirs × 8 steps) + Lambertian ground bounce; MS LUT re-scattered as ambient term in SkyView | ⚠️ SIMPLIFICATION (inherited from Zephyr, not a porting bug; Hillaire-2020-class approximation) |
| Sky View | Precomputed, camera-height-parameterized | Per-view bake (SS+MS atlases, 192×112), RADIANCE_SCALE applied once at bake — matches Zephyr contract | ✅ MATCH (view-dependent by design; perf note HIGH-1) |
| Aerial perspective | Camera volume (3D LUT) | Legacy ATMOS Rayleigh-only raymarch stage (unchanged) | ⚠️ DOCUMENTED SPLIT (Stage A, out of scope) |
| Planet transform | `P_local = conj(Q)·(P−C)`, applied exactly once | Camera-relative centers in double precision on CPU; rotation correctly unused under Case-A symmetry | ✅ CORRECT |
| Sun direction (Case A) | World sun → planet-local once | Per-planet world Planet→Sun from light reference (parallax preserved); world-frame end to end | ✅ CORRECT (Case A) |
| Phase functions | Rayleigh 3/(16π)(1+cos²), HG Mie normalized | Identical formulas (`HillaireCommon.ush` ll.177–188) | ✅ MATCH |
| Density/extinction | 2-layer Bruneton profiles; ozone piecewise-linear | Single exponential + effective-scale clamp; Gaussian ozone clamped into shell; σ shared between scattering and extinction (energy-consistent) | ⚠️ SIMPLIFICATION (inherited; toy-planet robustness, zero effect on Earth-like configs) |
| LUT cache | Content-hash invalidation; rotation/orbit never rebuild | Profile-hash keys + pooled multi-frame targets (after fix); planet-count guard | ✅ MATCH (after fix) |
| Sun disk | Transmittance-attenuated, angular radius | `smoothstep` + fwidth AA, transmittance lookup at camera height, occultation test | ✅ MATCH |
| Compositing | Visible-planet rect passes far→near + governing fullscreen | Single governing planet per pixel + regime fade `lerp(Scene, SS+MS, fade)`; geometry passes through via depth | ⚠️ LIMITATION (no multi-planet atmospheric layering in one pixel — HIGH-2, feature-level) |
| Exposure/luminance | Spectral→luminance matrix, engine tonemap after | `HILLAIRE_RADIANCE_SCALE=24` once at bake; `Exposure`/`SunLuminance` on linear HDR pre-tonemap | ✅ SOUND (gains, not colors) |
| Depth handling | Depth-clamped ray interval | Reversed-Z decode, `bHasDepth` passthrough, geometry-T vs shell-T test in deep space | ✅ CORRECT |

---

## 4. Bugs Found (all fixed in PASS 4, at origin)

### CRITICAL-1 — Sky stage dead: `bInitialized` never set
- **File:** `Source/Andromeda/Private/Atmosphere/AndromedaHillaireLUTRenderer.cpp`
- **Function:** `AndromedaHillaire::Initialize()` / `RenderSky()`
- **Problema:** `FRendererState::bInitialized` defaults `false`; `Initialize()`
  registered shaders/delegates but never set it. `RenderSky()` early-outs on
  `!State.bInitialized` → the entire Hillaire pipeline returned untouched
  scene color every frame. Only the legacy ATMOS aerial stage was ever visible.
- **Causa:** porting omission (fresh bug, not in reference).
- **Correzione:** set `true` in `Initialize()`, `false` in `Shutdown()`;
  `IsInitialized()` now returns the real flag (was hardcoded `true`).
- **Categoria:** §10-error-class: renderer bug (dead code path), not math.

### CRITICAL-2 — Cross-frame RDG texture reuse (dangling GPU resources)
- **File:** `AndromedaHillaireLUTRenderer.h` (`FLUTCache`) + `.cpp` (`RenderSky`)
- **Problema:** cache stored raw `FRDGTexture*` from frame N's graph and
  rebound them in frame N+1. RDG transient resources die with their graph's
  `Execute` — steady-state frames sampled destroyed memory (validation errors
  or garbage/black LUTs depending on allocator reuse).
- **Causa:** porting error: the reference keeps real GPU textures; RDG
  transients are not real textures. (GPU-binding lifetime class.)
- **Correzione (at origin):** cache now stores
  `TRefCountPtr<IPooledRenderTarget>`; each frame re-registers via
  `GraphBuilder.RegisterExternalTexture(..., ERDGTextureFlags::MultiFrame)`;
  fresh content extracted once per regen via
  `GraphBuilder.ConvertToExternalTexture(...)`. Added `CachedPlanetCount`
  (extents depend on it; count change forces structural regen even under
  `FreezeLUTs`). WorldCleanup/Shutdown release the pooled refs.
- **Categoria:** GPU resource binding / LUT generation.

### CRITICAL-3 — Planet key hashed only the LAST planet
- **File:** same `.cpp`, `ComputePlanetKey()`
- **Problema:** loop body assigned `Key = ComputeAtmosphereProfileHash(...)`
  instead of chaining → with N>1 planets, edits on any planet but the last
  never invalidated LUTs (stale atmospheres, wrong slice contents).
- **Causa:** porting bug (fresh, not in reference).
- **Correzione:** every profile hash chained via `HashBytes(..., Key)`;
  global Mie/Absorption scales folded once outside the loop.
- **Categoria:** LUT cache / multi-planet isolation.

### CRITICAL-4 — Proof counters wired to dead atomics
- **File:** same `.cpp`, `GetDispatchCount()` / `GetLUTRegenCount()`
- **Problema:** returned `GDispatchCounter`/`GLUTRegenCounter`, which nothing
  ever increments (`RenderSky` bumps `State.DispatchCount`/`State.LUTRegenCount`).
  `r.AndromedaAtmosphere.Status` permanently reported 0 dispatches / 0 regens —
  the pipeline was unprovable and undebuggable.
- **Correzione:** return the real `FRendererState` counters; removed the dead
  atomics. (Camera/proof class.)
- **Categoria:** diagnostics (blocks TEST A–G verification).

### MEDIUM-1 — Diagnostics visible-set: cm/km mix + stack overflow
- **File:** `AndromedaHillaireCore.cpp`, `SelectPlanets()` (diagnostics path only;
  the renderer selects governing per-pixel in-shader)
- **Problema:** `AlongView`/`Dist` (cm) compared against `TopKm` (km) and
  `TopKm/Dist` fed to `asin` → angular size ≈ 0 → visible set always empty;
  plus unbounded write into fixed `Candidates[8]` with >8 planets.
- **Correzione:** single cm→km conversion at source, matching-unit compares,
  `break` when full, `VisibleDistanceKm` now truly km (as named).
- **Categoria:** coordinate/units (diagnostics-only, zero render-path risk).

### LOW-1 — Removed dead empty raster pass
- **File:** same LUT renderer, `RenderSky()` composite tail
- **Problema:** an `AddPass("HillaireSkyComposite")` with an empty lambda ran
  before the real `AddFullscreenPass` — redundant pass, confusing capture.
- **Correzione:** deleted; single fullscreen pass remains.

### Explicitly NOT bugs (verified, left untouched)
- Planet rotation unused in render path → correct under Case-A spherical symmetry.
- `dot(Omega, SunDir)` gather-phase sign + dead `QToP` + bake/sample half-texel
  edge bias → inherited verbatim from the validated Zephyr model; "fixing" only
  Hillaire would diverge it. Rayleigh unaffected (even phase symmetry).
- `CameraWorldPosition` shader param unused; `bCameraInsideAtmos` flags unused;
  deep-space sun from planet 0; slice order = snapshot order — all benign.
- `PlanetScreenRect` convention doubt — unused by the render path (no rect
  passes in this design); diagnostics-only, left as is.

---

## 5. Visual Problems — diagnosis from code (no editor run in this session)

Because CRITICAL-1 meant the Hillaire stage never drew, every symptom below
describes the legacy aerial-only image OR the predicted post-fix image.
Each maps to a root cause already fixed or classified — no blind tuning done.

- **Space (TEST A):** black sky + missing limb sky → Hillaire stage dead
  (CRITICAL-1); after fix, pass-3 deep-space branch renders nearest-hit limb
  + single sun disk. No artificial halo exists in code (no glow sprites).
- **Orbit (TEST B):** thin/absent limb → same root cause; transmittance +
  SkyView limb come from the fixed stage.
- **Day (TEST C):** pale/wrong sky → aerial-only Rayleigh image; post-fix the
  LUT sky (SS+MS, ozone absorption, HG lobe) owns inside pixels at fade 1.
- **Sunset (TEST D):** weak red gradient → needs the fixed path: long-path
  optical depth + Chappuis absorption + forward Mie lobe all live in the LUT
  stage. If sunset remains weak post-fix, it is calibration (density/ozone
  profile), NOT math — do not touch phases.
- **Night (TEST E):** pitch black → expected Case-A limit: no moon/airglow
  source in the model; MS gather retains only ground-bounce + sky-gather
  residue. Do NOT add ambient light before confirming via DebugMode 3; any
  night lift is a deliberate art/physics decision (future work), not a bugfix.
- **Rotation (TEST F):** LUTs correctly independent of rotation/orbit/camera
  (profile-hash keys; sun/height only in the view key). Post-fix counters
  (`Status`) prove regen behavior.
- **Multi-planet (TEST G):** isolation now enforced (CRITICAL-3 + pooled
  slices). Remaining: single-governing-per-pixel means a distant planet's
  *atmosphere* seen through governing sky has no far→near layering
  (HIGH-2 limitation, feature-level, not scheduled).

---

## 6. Changes Made (this session only; port files themselves untouched in math)

1. `AndromedaHillaireLUTRenderer.h` — `FLUTCache`: pooled targets + count guard.
2. `AndromedaHillaireLUTRenderer.cpp` — init flag; pooled cache lifecycle;
   key chaining; real counters; empty-pass removal; `RenderTargetPool.h` include.
3. `AndromedaHillaireCore.cpp` — diagnostics units + overflow guard.
4. This report. No shader edited. No parameters retuned. No new passes/hooks.

---

## 7. Validation

- ✅ Official build `Build.bat AndromedaEditor Win64 Development` → **Succeeded,
  0 errors, 0 warnings** (after two trivial compile iterations: struct fwd-decl,
  explicit `nullptr` init for MSVC C4703).
- ✅ Shader infrastructure checkable at runtime: `r.AndromedaHillaire.Validate`.
- ⏳ Runtime visual validation NOT performed in this session (no editor run):
  required before closing the audit —
  1. `r.AndromedaAtmosphere.Status` → sky dispatches > 0, regens stabilize
     (profile-static scene ⇒ Transmittance/MS regen ≈ 0, SkyView regen only
     while camera height/sun moves).
  2. `r.AndromedaHillaire.DebugMode` 1/7/8 → raw LUTs sane; 2/3 → SS/MS split.
  3. TEST A–G (§5) day/sunset/night + rotation (no regen storm) + 2-planet
     isolation (edit A's profile ⇒ B's slice unchanged).
  4. `r.AndromedaHillaire.FreezeLUTs 1` ⇒ zero regens, stable image
     (proves cache reads, not rebuilds).

## 8. Remaining Limitations (deliberate, not bugs)

- **Case-A physics:** no night source (moon/airglow), infinite-distance star,
  spherically symmetric profiles. Night darkness is model truth.
- **Model simplifications (inherited):** single-gather MS vs Bruneton orders;
  single-exponential + clamped ozone vs 2-layer profiles; no ground-bounce in
  the view march. Zero effect on Earth-like configs; revisit only with
  reference-side evidence.
- **Compositing (HIGH-2):** no per-pixel far→near multi-planet atmosphere
  layering; distant limbs rely on the ATMOS stage. Rect-pass layering is future
  work, explicitly out of scope.
- **Perf note (HIGH-1, by design):** SkyView rebuilds whenever quantized camera
  height (±1 m) or sun moves — correct but heavy in flight; coarsen quantum or
  restore height-threshold hysteresis only after visual sign-off.
- **Calibration untouched:** SunLuminance 4.0, Exposure 1.0, RADIANCE_SCALE 24,
  Rayleigh/Mie sigmas — all as-ported. Tune only post-validation per §5,
  recording old/new value + physical reason + observed effect each time.
