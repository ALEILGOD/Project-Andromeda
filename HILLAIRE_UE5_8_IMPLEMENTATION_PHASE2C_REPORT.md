# HILLAIRE → UE5.8 — IMPLEMENTATION PHASE 2C REPORT
## Aerial Perspective camera volume (real GPU evaluation, Hillaire math)

Date: 2026-09-16 · Project: `C:\Users\aless\Documents\Unreal Projects\Andromeda` · Engine: UE 5.8 (`C:\Unreal Engine\UE_5.8`)
Reference (READ-ONLY, unmodified): `C:\Users\aless\UnrealEngineSkyAtmosphere`
Status: **PASS** — aerial perspective genuinely evaluated from the Hillaire pipeline
(cached T + MS inputs, Rayleigh + Mie, optical depth, slice-limited path integration),
view-dependent by design (never planet-cached), GPU-executed, validated, tested 14/14.

---

## STATUS

PASS

## PHASE 2A REGRESSION

7/7 (Transmittance, MultiScattering, CacheReuse, Invalidation, PlanetIndependence,
Determinism, NumericValidation — untouched math, green).

## PHASE 2B REGRESSION

11/11 (incl. SkyViewGpuExecution — green; SkyView implementation, CPU mirror,
validation and tolerances frozen and untouched).

## PHASE 2C

14/14 (BasicAerial, NearGround, HighAltitude, OutsideAtmosphere, GroundIntersect,
SpaceLimb, RayleighMie, MultiScatteringDependency, TransmittanceDependency,
Determinism, AerialGpuExecution, AerialGpuCpu, AerialNaNInfScan, CacheSeparation).

## BUILD

PASS — `Build.bat AndromedaEditor Win64 Development` → `Result: Succeeded` (final tree).

## SHADER COMPILATION

PASS — `FHillaireAerialPerspectiveCS` (MainCS/SF_Compute) compiles; no HLSL errors,
no missing includes, no parameter mismatches. Proven functionally: Tests 11/12 dispatch
the pass through the production path (a broken/unbound shader fails graph execution).
Log scan: zero shader errors, zero RDG validation errors, zero D3D12 errors, exit code 0.

## AUTOMATION

38/38 (Phase1 6/6 + Phase2A 7/7 + Phase2B 11/11 + Phase2C 14/14), exit code 0.

## GPU EXECUTION

Proven by `Hillaire.Phase2C.AerialGpuExecution`: GT builds a real planet + directional
sun → immutable snapshot → `ENQUEUE_RENDER_COMMAND` constructs a real `FRDGBuilder`,
calls production `EnsurePlanetLuts` (T → MS) + production `EvaluateAerialPerspective`,
`Execute()`s on the render thread → fence → `ReadbackPooledVolume` on the pooled
scratch. Recorded: dims 32×32×32, 32768 texels, NaN=0, Inf=0,
`GPU L near-slice=0.000131 far-slice=0.013892` (100× growth along the ray — real
integration; a constant fill or dead pass fails this trend gate by orders of magnitude).
A CPU-only result was never accepted: the test fails honestly if the graph, the
extraction, or the readback fails.

## GPU VS CPU

Sparse-grid (stride 4, 512 samples, absolute floor 1e-3) luminance relative error:
**mean 0.62%, max 1.68%** (gates: mean <10%, max <25% — same gates as Phase 2B, held
without loosening). Gap budget, all documented: half-float readback quantization,
HW vs CPU-mirror bilinear LUT sampling, float reassociation across 2..64-step marches,
matrix-narrowing float64→float32 at upload. Reference SkyView GPU-vs-CPU re-measured
this run: mean 1.61%, max 6.43% (unchanged behavior).

## NUMERICAL VALIDATION

- GPU volume: NaN = 0, Inf = 0 (32768 texels).
- CPU stratified scan (Test 13): 4 configs × 2048 froxels + giant-planet column subset,
  covering surface/inside/space/sunset/limb/ground-hit/miss branches: NaN = 0, Inf = 0.
- Exact miss path returns (0,0,0,1); out-of-range early-out returns (0,0,0,1).
- Opacity strictly inside (0,1) on terminated paths; no division-by-zero (march needs
  ≥1 step; extinction >0 for any valid profile; degenerate camera handled by fallback).

## REFERENCE COMPARISON

Model: `RenderCameraVolumePS` (`RenderSkyRayMarching.hlsl:803-876`), governing-planet
camera volume, `MULTISCATAPPROX_ENABLED=1` permutation (factor>0 default).

| Reference | Andromeda port | Verdict |
|---|---|---|
| Raster-instanced 32×32 slices, `sliceId` = instance | Per-froxel compute [4,4,4], `sliceId` = `DispatchId.z` | identical math, documented §6 choice |
| Clip ray via `gSkyInvProjMat`/`gSkyInvViewMat`, planet-local rotation | Snapshot proj (inverted once) + Qconj·transpose(view-rot); row-vector `mul(v,M)` == reference column-vector `mul(M,v)` under UE's transposed upload | identical map, proven by 0.62% GPU-vs-CPU |
| Squared slice distribution, 4 km/slice (128 km range) | verbatim, descriptor-driven | identical |
| Under-ground clamp-out (offset + 0.001) | verbatim, named limits | identical |
| Above-top MoveToTop + shrink + early-outs | verbatim | identical |
| March: ground=false, max(1,(slice+1)*2) fixed, MieRayPhase=true, MS-approx ON, tMaxMax | verbatim (tMaxMax added as trailing param) | identical |
| Output (L, 1−mean(T)) | verbatim | identical |
| `gSunIlluminance` throughput | unit-white `globalL` | identical at default IllumScale 1.0; REQUIRED by N×M transfer convention (Phase-2B rule, kept) |
| Blue-noise/dither | absent (commented out in reference) | identical (deterministic) |

New `tMaxMax` integrator parameter: T/MS/SkyView callers pass the reference default
9000000.0f (mechanical, zero behavior change — proven by green 2A/2B suites); CPU mirror
takes it as a defaulted trailing arg (existing callers unchanged).

## FILES CREATED

```
Plugins/HillaireAtmosphere/Shaders/HillaireAerialPerspective.usf
        P3 compute (MainCS, CameraVolumePS verbatim port, 138-line header documents every mapping)
Plugins/HillaireAtmosphere/Source/HillaireAtmosphere/Private/Tests/HillairePhase2CTests.cpp
        14 automation tests (Hillaire.Phase2C.*)
Plugins/HillaireAtmosphere/Validation/Phase2C/  22 files (21 slice PNGs V1..V7 × SliceX/Y/Z + marker)
HILLAIRE_UE5_8_IMPLEMENTATION_PHASE2C_REPORT.md  (this file, project root)
```

## FILES MODIFIED

```
Source/.../Public/HillaireLimits.h     aerial constants (AP slice/depth/threading/clamp lifts/TMaxMax default)
Source/.../Shaders/HillaireLutCore.ush integrator +tMaxMax param + min() clamp (only additive change)
Source/.../Shaders/HillaireMultiScattering.usf  pass 9000000.0f (mechanical, frozen behavior)
Source/.../Shaders/HillaireSkyViewLut.usf       pass 9000000.0f (mechanical, frozen behavior)
Source/.../Public/HillaireShaders.h    FHillaireAerialPerspectiveCS replaces P3 stub (FMatrix44f params)
Source/.../Private/HillaireShaders.cpp P3 registration MainCS/SF_Compute
Source/.../Public/HillaireLutManager.h FHillaireAerialViewInputs, ComputeAerialViewInputs,
                                       EvaluateAerialPerspective, FindAerialScratch, scratch map
Source/.../Private/HillaireLutManager.cpp  above + AddAerialPerspectivePass (NeverCull, extraction)
Source/.../Private/HillaireViewExtension.cpp  r.Hillaire.AerialEval CVar (default 0) + governing-only call
Source/.../Public/HillaireLutCpu.h     tMaxMax default + ComputeAerialFroxel/bakers decl
Source/.../Private/HillaireLutCpu.cpp  tMaxMax clamp + froxel/volume bakers (explicit row-dot maps)
Source/.../Public/HillaireLutDiagnostics.h  ReadbackPooledVolume decl
Source/.../Private/HillaireLutDiagnostics.cpp  Read3DSurfaceFloatData readback + half->float
Source/.../Private/HillaireValidationCommands.cpp  BakeAerialValidation (V1..V7) + DumpGpuLuts aerial slices
Source/.../Shaders/HillaireAerialVolume.usf  DELETED (Phase-1 raster stub, replaced by the file above)
```

NOT modified: STARMAP, Planet, Sun, PlanetaryLighting, DX11 reference, engine sources,
USkyAtmosphereComponent (never used), game Source/, Phase-2A/2B expectations or tolerances.

## LIMITATIONS (real, remaining)

1. Volume range is 128 km by reference design: cameras farther than that from air
   correctly yield zeros (locked by test + documented; not a bug).
2. V3-class thin-air columns are legitimately ~1e-7 (near-vacuum) — invisible at normal
   exposure, proven nonzero at full precision (3.94e-7 max channel-sum).
3. GPU-vs-CPU tolerance (10%/25%) documents half-quantization + HW-vs-mirror bilinear +
   reassociation; never bitwise (per task §13/§16).
4. Validation views use identity projection + explicit aim (fan shape only); production
   passes live snapshot matrices through the same helper.
5. Below-horizon dimness inherits the reference no-ground-bounce SkyView bake (terrain
   bounce is a later phase, out of scope).
6. No composite consumer yet: volume is evaluated on demand (CVar-gated, default off);
   the Phase-2D composite will consume it (insertion points documented, not implemented).

## OUT OF SCOPE (confirmed not implemented)

Aerial final composite, multi-light volumetric final, STARMAP integration, Planetary
Lighting/Sun redesign, engine fork, post-process atmosphere, demo map, gameplay,
gravity, planet movement, performance optimization beyond pooled scratch reuse.

## NEXT PHASE

Phase 2D — final composite consuming SkyView + aerial volume (NOT implemented).
