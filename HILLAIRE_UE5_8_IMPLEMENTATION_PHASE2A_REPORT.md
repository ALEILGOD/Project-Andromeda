# HILLAIRE → UE5.8 — IMPLEMENTATION PHASE 2A REPORT
## Transmittance LUT + Multi-Scattering LUT (real RDG + Global Shader generation)

Date: 2026-09-15 · Project: `C:\Users\aless\Documents\Unreal Projects\Andromeda` · Engine: UE 5.8 (`C:\Unreal Engine\UE_5.8`)
Reference (READ-ONLY, unmodified): `C:\Users\aless\UnrealEngineSkyAtmosphere`
Status: **PASS** — both LUTs genuinely generated, validated, cached, tested. SkyView NOT started (stop boundary respected).

---

## 1. Baseline audit

- `Plugins/HillaireAtmosphere/` exists with the exact Phase-1 file set from
  `HILLAIRE_UE5_8_IMPLEMENTATION_PHASE1_REPORT.md` (verified by read: LutManager,
  Shaders, Limits, RdgHelpers, ViewExtension, Subsystem, 6 stub `.usf`, 6/6 Phase-1 tests).
- No names were assumed: all Phase-2A work reuses the real Phase-1 identifiers
  (`FHillaireLutManager`, `FHillaireAtmosphereProfile`, `HillaireLimits::*`, …).
- Baseline build BEFORE any change: `Build.bat AndromedaEditor Win64 Development` →
  **Result: Succeeded** (up-to-date, ~2 s). No pre-existing breakage found, nothing fixed.

## 2. Files created

```
Plugins/HillaireAtmosphere/Shaders/HillaireLutCore.ush      shared LUT-gen math (medium, OD march, full integrator, future samplers)
Plugins/HillaireAtmosphere/Source/HillaireAtmosphere/Public/HillaireLutCpu.h
Plugins/HillaireAtmosphere/Source/HillaireAtmosphere/Private/HillaireLutCpu.cpp
        CPU mirror of the generation math (tests + reference comparison)
Plugins/HillaireAtmosphere/Source/HillaireAtmosphere/Public/HillaireLutDiagnostics.h
Plugins/HillaireAtmosphere/Source/HillaireAtmosphere/Private/HillaireLutDiagnostics.cpp
        AnalyzeLut (min/max/avg/NaN/Inf) + blocking GPU readback helper
Plugins/HillaireAtmosphere/Source/HillaireAtmosphere/Private/Tests/HillairePhase2ATests.cpp
        7 automation tests (Hillaire.Phase2A.*)
HILLAIRE_UE5_8_IMPLEMENTATION_PHASE2A_REPORT.md             (this file, project root)
```

## 3. Files modified

```
Shaders/HillaireTransmittanceLut.usf   stub PS -> REAL compute (MainCS, 40-step OD march)
Shaders/HillaireMultiScattering.usf    stub CS -> REAL compute (verbatim NewMultiScattCS)
Source/.../Public/HillaireShaders.h    FHillaireAtmosphereMediumParams, P0->CS class, P1 params extended,
                                       HillaireFillAtmosphereUniforms decl
Source/.../Private/HillaireShaders.cpp P0 registration MainPS/SF_Pixel -> MainCS/SF_Compute + Fill impl
Source/.../Public/HillaireLimits.h     march counts, thread groups, PLANET_RADIUS_OFFSET (centralized)
Source/.../Public/HillaireLutManager.h EnsurePlanetLuts API (replaces stub BuildLutPasses)
Source/.../Private/HillaireLutManager.cpp  P0/P1 RDG enqueue + persistent lifetime
Source/.../Private/HillaireViewExtension.cpp consumes EnsurePlanetLuts from snapshot data
Source/.../Public/HillaireViewSnapshot.h   +MultipleScatteringFactor/+bFastSkyEnabled (RT-safe knob copies, NOT hashed)
Source/.../Private/HillaireAtmosphereSubsystem.cpp stamps knobs post-build (GT)
Source/.../HillaireAtmosphere.Build.cs comment only (verified: no new module needed)
```

NOT modified: STARMAP (`StarSystem`), `Planet`, `Sun`, `PlanetaryLightingComponent`,
DX11 reference repo, ATMOS/ZEPHYR remnants, engine sources. Game `Source/` files show only
pre-existing timestamps (01:40–16:21, earlier work); Phase 2A touched `Plugins/HillaireAtmosphere/**` only.

## 4. Shaders created / modified

- `HillaireLutCore.ush` (NEW): `HillaireAtmosphereParams` + builder, `HillaireSampleMediumRGB`,
  `HillaireIntegrateOpticalDepth` (T path), `HillaireIntegrateScatteredLuminance` (MS path),
  `HillaireSampleTransmittanceLut` / `HillaireSampleMultiScatteringLut` (minimal future-phase
  consumption interfaces, no rendering).
- `HillaireTransmittanceLut.usf` (REAL): per-texel compute, `[numthreads(8,8,1)]`, dispatch
  from LUT desc = (32,8,1). Entry `MainCS`, class `FHillaireTransmittanceLutCS`, `SF_Compute`.
- `HillaireMultiScattering.usf` (REAL): verbatim `NewMultiScattCS` incl. `[numthreads(1,1,64)]`,
  groupshared 64→1 reduction, geometric-series closure, MS-factor bake. Dispatch (32,32,1).
- Untouched stubs: SkyView/Aerial/Final/Debug `.usf` (still Phase-1 stubs, still compiling).

## 5. Transmittance implementation

`uv=(x+0.5)/W,(y+0.5)/H` → `HillaireUvToLutTransmittanceParams` → `WorldPos=(0,0,h)`,
`WorldDir=(0,sqrt(1-c²),c)` → 40 fixed-step optical-depth march →
`exp(-OD)` → `RWTexture2D` UAV. RDG compute pass with `NeverCull`, target extracted to the
per-planet pooled handle. Light-independent by construction (extinction accumulation only).

## 6. Multi-Scattering implementation

Per texel: `uv=pixPos/Res` → `fromSubUvsToUnit` → `(cosSunZenith, viewHeight)` with the
`PLANET_RADIUS_OFFSET` adjustment → 64 stratified sphere dirs × 20-step full marches
(`ground=true`, uniform phase, T-LUT sampled) → groupshared reduction → `L = L2/(1-r)` →
`× MultipleScatteringFactor`. Reads the JUST-BAKED T texture in the same graph (RDG barrier).

## 7. Hillaire math mapping

| Reference | Port | Location |
|---|---|---|
| `sampleMediumRGB` | `HillaireSampleMediumRGB` (verbatim) | LutCore.ush + LutCpu.cpp |
| `raySphereIntersectNearest` | reused from Phase-1 `HillaireCommon.ush` / `RaySphereNearest(Center)` | — |
| `UvToLutTransmittanceParams` | reused (Common.ush) / `UvToTransmittanceParams` | — |
| `RenderTransmittanceLutPS` (40 sp) | `MainCS` T + `HillaireIntegrateOpticalDepth` | TransmittanceLut.usf |
| `NewMultiScattCS` (8×8×20) | `MainCS` MS verbatim incl. reduction + series | MultiScattering.usf |
| `IntegrateScatteredLuminance` | full port (MS path); OD-subset (T path, dead sun-terms documented) | LutCore.ush |
| `GetMultipleScattering` | `HillaireSampleMultiScatteringLut` (future interface) | LutCore.ush |
| `PLANET_RADIUS_OFFSET 0.01` | uniform from `HillaireLimits::PlanetRadiusOffsetKm` | — |

Dead-for-LUT terms removed with justification in `HillaireLutCore.ush` header: depth resolve
(always −1), VariableSampleCount branch (always false on this path), MULTISCATAPPROX (circular),
shadow map, debug/dither, Bruneton Step0/1 accumulators (3D chain is test-only per spec).

## 8. Parameter mapping

`HillaireFillAtmosphereUniforms` (single write path, profile → uniforms). C++ names ==
HLSL global names 1:1 (verified by successful binding at boot; a case typo
`MultiScatteringLUTRes` was caught by the compiler, fixed, documented as the packing-gate proof).
Deliberately NOT uploaded: `SolarIrradiance` (unit-illuminance bake), `MuSMin`, `MiePhaseG`
(phase/disk belong to SkyView/final). Float3+float row packing preserved in struct order.

## 9. Texture formats

`PF_FloatRGBA` (32F) for both LUTs — spec decision kept. No silent change; descriptors in
`HillaireRdg` unchanged (SRV+UAV+RT flags).

## 10. Resolutions

Centralized in `HillaireLimits.h`: T 256×64, MS 32×32 (Res 32), march counts 40/20,
sphere samples 64 (8×8), T thread group 8×8, MS group (1,1,64). Shaders take sizes from the
RDG descriptor (`GetDimensions` guard) or the `MultiScatteringLutRes` uniform — no literals.

## 11. RDG resource lifecycle

`Generate → graph execution → QueueTextureExtraction → pooled handle → next-frame
RegisterExternalTexture`. Fixed the Phase-1 placeholder flaw (unconditional create+extract
would have wiped content every frame): now create+extract ONLY on regen, import otherwise.
`NeverCull` on both passes (outputs consumed via extraction until Phase 2B wires readers).
No transient handle escapes the graph (outputs re-imported per graph).

## 12. Cache

Per-planet slots (`LutStates` + `LutTargets`), never global. T key = profile hash;
MS key = `hash(profile, factor)`. Reuse path imports pooled textures with zero passes.

## 13. Invalidation

Explicit policy (code + tests): T rebuilds on ANY profile-content change; MS rebuilds on
profile change OR factor change; sun/camera/planet motion rebuilds NEITHER (verified by test);
slot-0 identity/sun-cache still tracked via `QueryRegen` for the Phase-2B SkyView key
(the sun cache refreshes continuously so SkyView never starts stale).

## 14. Determinism

Fixed stratification, fixed step counts, no RNG, no dither, no frame input, no temporal state
on both paths. CPU-vs-CPU bakes are bit-identical (tested). GPU tolerance validated via
readback stats, never bitwise (documented).

## 15. Diagnostics

`HillaireLutDiagnostics::AnalyzeLut` (min/max/avg/NaN/Inf) used by all generation tests;
`ReadbackPooledLut` (blocking RT `ReadSurfaceData` + fence, GT-called, debug path) proves GPU
content with the same stats code. Measured CPU-bake stats (reference Earth profile):
- Transmittance 256×64: `min=(0.014858,0.000138,0.000000) max=(1.000000,1.000000,0.999999)
  avg=(0.737566,0.587792,0.553293) NaN=0 Inf=0` (blue limb underflows to finite 0 — physical).
- MultiScattering 8-texel spot @32²: `min=(0,0,0) max=(0.006446,0.011456,0.023900)
  avg=(0.002523,0.003971,0.008410) NaN=0 Inf=0` (midnight texel L=0 with zero albedo — physical).

## 16. Reference comparison

Same profile (transcribed `SetupEarthAtmosphere`), same resolutions (256×64, 32²), same
march configs (40/20/64) as the DX11 sample. Analytic anchors hold: top-zenith texel T=1
(zero march length); fixed-height rows monotone decreasing; MS ×2 factor bit-exact (bake
location proof); MS ×0 exact zero. A DX11 pixel-dump diff was NOT run (sample not executed
here) — recorded as the remaining manual step; the CPU mirror + anchors above are the
automated proxy, and any formula deviation would fail them (they failed nothing).

## 17. Automation tests

`Automation RunTests Hillaire` in-editor, exit code 0 — **13/13 Success**:
Phase-1 regression 6/6 (Profile, Light, NxM, Snapshot, LutCache, ResolveParity) +
Phase-2A 7/7 (Transmittance, MultiScattering, CacheReuse, Invalidation,
PlanetIndependence, Determinism, NumericValidation).

## 18. Build result

`Build.bat AndromedaEditor Win64 Development` on the final tree → **Result: Succeeded**.
One fix iteration during bring-up (C++): `IPooledRenderTarget::GetRHI()` (no
`GetRenderTargetItem`), runtime-Inf via `FMath::Exp(1000)` (literal `1/0` is a compile error).

## 19. Editor result

- Plugin loads, shader dir mapped, subsystem initializes (both intertwined worlds).
- `FHillaireMultiScatteringCS` compiled clean (log stats); `FHillaireTransmittanceLutCS`
  proven compiled via `#error`-probe (fatal named the file/line) + clean boot after restore.
- No shader compile errors, no RDG validation errors, no resource-lifetime errors.
- Pre-existing notices only (VS toolchain preference, `IncludeOrderVersion_Unreal5_6`).

## 20. Regression result

STARMAP / Planet / Sun / Planetary Lighting untouched (scope audit §3); Phase-1 suite still
6/6; editor boots clean. **Regressions: NONE.**

## 21. Known limitations

1. GPU-vs-CPU bitwise parity not asserted (filter/compiler reassociation) — stats-level only.
2. DX11 pixel-dump diff is manual (see §16).
3. `ReadbackPooledLut` is blocking/debug-only; async readback + debug UI belong to Phase 2B.
4. Gas-giant H/R ratios need T-mapping revalidation (spec-flagged test gap, unchanged).
5. MS full 32² CPU bake exists but tests use spots for speed (GPU does the full bake per regen).

## 22. Phase 2B scope (NOT started)

SkyView LUT generation + sun cache consumption + SkyView sampling. Then aerial, final
composite, multi-light volumetric — each its own gate. No Phase-2B code exists in this phase
(SkyView `.usf`/targets intentionally left as Phase-1 stubs/empty).
