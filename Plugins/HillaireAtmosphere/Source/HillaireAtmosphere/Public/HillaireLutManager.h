#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "SceneUniformBuffer.h"
#include "HillaireAtmosphereProfile.h"
#include "HillaireLightSource.h"
#include "HillaireLimits.h"
#include "HillairePlanetState.h"

struct IPooledRenderTarget;
class FRDGBuilder;
class FRDGTexture;

/**
 * HILLAIRE ATMOSPHERE - LUT OWNERSHIP AND INVALIDATION (Phase 1).
 *
 * Per-atmosphere cache (never global). Mirrors ensurePlanetLuts predicates:
 *
 * - Transmittance: regen when the profile content hash changes. NEVER on
 *   sun/camera/planet motion (light-independent by construction).
 * - MultiScattering: regen when hash(profile, MultipleScatteringFactor).
 *   The factor is baked into MS output, so it joins the key; it must NOT
 *   invalidate SkyView (no over-invalidation).
 * - SkyView: regen when the profile hash changes, when the planet-local
 *   PRIMARY-sun direction moves (>~0.001 rad, the sun cache), when the
 *   primary slot-0 identity swaps, or when the view height drifts beyond
 *   max(0.02, 1%*h). Secondary-light motion must NOT invalidate SkyView
 *   (SkyView is primary-baked; in multi mode it is unused anyway).
 *
 * Phase 1 implements ownership, keys, predicates and lifecycle. The RDG
 * generation passes themselves arrive with the ray marching milestone and
 * call Mark*Built + BuildLutPasses; the data model does not change.
 */

struct FHillaireLutRegenQuery
{
	bool bTransmittance = false;
	bool bMultiScattering = false;
	bool bSkyView = false;

	bool NeedsAnything() const { return bTransmittance || bMultiScattering || bSkyView; }
};

class HILLAIREATMOSPHERE_API FHillaireLutManager
{
public:
	FHillaireLutManager();
	~FHillaireLutManager();

	FHillaireLutManager(const FHillaireLutManager&) = delete;
	FHillaireLutManager& operator=(const FHillaireLutManager&) = delete;

	// ---- Lifecycle (per atmosphere, never global) ----
	//
	// THREAD SAFETY (runtime wiring): planet slots are registered and
	// released on the GameThread (component/link BeginPlay/EndPlay, STARMAP
	// spawn churn) while PreRenderView and the post-processing delegates
	// consume them on the RenderThread. TMap add/remove can reallocate, so
	// every registry touch below is serialized by RegistryLock. The lock
	// only ever guards map/state bookkeeping (microseconds; GPU passes run
	// outside it or under the single leaf lock with no nesting), and GT
	// holders never take any other lock: no deadlock, no behavior change
	// single-threaded (all automation expectations bit-stable).
	void RegisterPlanet(int32 PlanetId);
	void UnregisterPlanet(int32 PlanetId);
	void Clear();
	bool HasPlanet(int32 PlanetId) const;

	/**
	 * Locked find+copy for GameThread readers (snapshot builds): the copy
	 * stays valid even if the slot is released concurrently. Returns false
	 * for unknown slots.
	 */
	bool CopyLutState(int32 PlanetId, FHillairePlanetLutState& OutState) const;

	/** Locked flag invalidation by slot (replaces find+mutate sequences). */
	bool InvalidatePlanetById(int32 PlanetId);

	/**
	 * Locked scratch copy (ref-counted handle, safe to use after the lock
	 * releases even if the slot is released concurrently).
	 */
	TRefCountPtr<IPooledRenderTarget> CopyAerialScratch(int32 PlanetId) const;

	// Raw lookups below are for single-threaded contexts only (automation
	// tests, synchronous debug). Production cross-thread readers must use
	// the Copy* accessors above.
	FHillairePlanetLutState* FindLutState(int32 PlanetId);
	const FHillairePlanetLutState* FindLutState(int32 PlanetId) const;

	// ---- Generation keys ----
	static uint64 MakeMultiScatteringKey(uint64 ProfileHash, float MultipleScatteringFactor);

	/**
	 * Evaluate regen predicates (reference: ensurePlanetLuts). Updates the
	 * sun cache inside State (init on first call, refresh on threshold miss)
	 * exactly like the reference; returns which LUTs need regeneration.
	 *
	 * SkyView sun key (ATMOS FIX VISIVO DEFINITIVO): the bake consumes the
	 * primary sun ONLY through its elevation above the planet-local camera
	 * up, so the cache compares HillaireSunElevationCos(sun, up) with a
	 * 1e-3 cosine delta (reference ~0.001 rad worst case at the horizon).
	 * Comparing raw sun vectors over-invalidated on every planet-spin frame
	 * (spin rotates sun+up rigidly: elevation and baked content identical),
	 * which - through the pooled handoff - dropped the sky continuously.
	 *
	 * @param PrimarySunLocalDir  planet-local primary-sun dir (slot 0 resolved
	 *                            LightDirLocal), valid only if bHavePrimary.
	 * @param CameraUpLocal       planet-local camera up (normalized; the same
	 *                            vector the SkyView bake consumes). Defaults to
	 *                            +Z so legacy single-arg call sites keep their
	 *                            zenith-sun meaning.
	 * @param bHavePrimary        single-primary fast path active for this planet.
	 * @param bFastSkyEnabled     SkyView path enabled (reference: currentFastSky).
	 */
	FHillaireLutRegenQuery QueryRegen(
		FHillairePlanetLutState& State,
		uint64 ProfileHash,
		float MultipleScatteringFactor,
		const FVector3f& PrimarySunLocalDir,
		bool bHavePrimary,
		const FGuid& PrimaryLightId,
		float ViewHeightKm,
		bool bFastSkyEnabled,
		const FVector3f& CameraUpLocal = FVector3f(0.0f, 0.0f, 1.0f));

	// ---- Post-generation bookkeeping (called by future RDG passes) ----
	void MarkTransmittanceBuilt(FHillairePlanetLutState& State, uint64 ProfileHash);
	void MarkMultiScatteringBuilt(FHillairePlanetLutState& State, uint64 MultiScatteringKey);
	void MarkSkyViewBuilt(FHillairePlanetLutState& State, uint64 ProfileHash, float ViewHeightKm, const FVector3f& BakedSunDirLocal);
	void NoteReuse(FHillairePlanetLutState& State);

	/**
	 * Targeted invalidation: a light-registry change invalidates ONLY the
	 * planets whose primary slot changed. Returns true if this planet's
	 * SkyView was invalidated.
	 */
	bool NotifyPrimarySlotChanged(
		FHillairePlanetLutState& State,
		const FGuid& CurrentPrimaryId,
		bool bCurrentPrimaryDirectional,
		bool bHaveSinglePrimary);

	void InvalidatePlanet(FHillairePlanetLutState& State);

	// ---- RDG generation boundary (called from the ViewExtension, RenderThread) ----
	//
	// ATMOS FIX VISIVO DEFINITIVO: REAL Transmittance + MultiScattering +
	// SkyView generation, called IN the BeforeDOF composite graph for the
	// governing planet (AerialCompositePass -> EnsurePlanetLuts). For the
	// planet, QueryRegen decides OUTSIDE graph construction whether a pass
	// is enqueued (no no-op passes, task section 18):
	//   invalid/missing -> CreateTexture + compute pass + QueueTextureExtraction
	//                      (persistent pooled handle survives frames; the fresh
	//                      TRANSIENT feeds the same-graph consumers below);
	//   valid           -> RegisterExternalTexture (import pooled, zero regen).
	// Order in one graph: T -> MS -> SkyView -> aerial volume -> composites.
	// MS reads the transient T (fresh) or the imported one (reuse); SkyView
	// reads transient/imported T + MS; the aerial volume and both composites
	// consume the same-graph transients. RDG inserts the UAV->SRV barriers.
	// Passes carry NeverCull: outputs are consumed via extraction (and the
	// debug path) until the final composite wires in-graph readers.
	//
	// Generation must NOT move to PreRenderView: the pooled slot queued there
	// is unusable until that graph executes, i.e. AFTER the BeforeDOF
	// composite of the same frame already built, so every regen frame saw
	// null targets and skipped the sky (flicker/absence root cause).
	//
	// SkyView v1 rule: bakes the SLOT-0 registry light whenever at least one
	// light is enabled (QueryRegen receives bHavePrimary = Count > 0, NOT the
	// single-primary fast-path flag - that flag still governs ONLY the future
	// final composite). With zero lights the SkyView pass is skipped (no sun
	// to bake). SkyView also rebuilds whenever MS regenerates in the same
	// call: the MS values are baked INTO the SkyView march, so an MS change
	// with a warm SkyView would go stale (Phase-2B invalidation completion).
	struct FHillairePlanetLutGraphOutputs
	{
		FRDGTextureRef Transmittance = nullptr;
		FRDGTextureRef MultiScattering = nullptr;
		FRDGTextureRef SkyView = nullptr;
	};

	bool EnsurePlanetLuts(
		FRDGBuilder& GraphBuilder,
		int32 PlanetId,
		const FHillaireAtmosphereProfile& Profile,
		float ViewHeightKm,
		const FVector3f& CenterCamRelativeKm,
		const FQuat& PlanetRotation,
		const FHillaireCompactedLights& ResolvedLights,
		float MultipleScatteringFactor,
		bool bFastSkyEnabled,
		FHillairePlanetLutGraphOutputs& OutOutputs);

	/**
	 * Planet-local camera UP (unit) from snapshot data, RenderThread-safe
	 * pure math: the camera sits at the relative origin, so
	 * camLocal = conj(Q) * (0 - CenterRel), up = normalize(camLocal).
	 * Degenerate (camera at planet center) falls back to +Z.
	 * Implemented over HillaireCameraPlanetLocalKm (single conversion path).
	 */
	static FVector3f ComputeCameraUpLocal(
		const FVector3f& CenterCamRelativeKm, const FQuat& PlanetRotation);

	/**
	 * COMPOSITE view inputs, RenderThread frame-exact (ATMOS FIX VISIVO
	 * DEFINITIVO, task C/F).
	 *
	 * The GT snapshot is built a frame AHEAD of the RT view that consumes
	 * it (GT runs ahead; the stash holds the newest snapshot). Deriving the
	 * composite rays from snapshot matrices alone therefore renders the
	 * atmosphere shell with a 1-frame-old camera while the depth buffer was
	 * rasterized with the current one: under any motion the shell offsets
	 * from the terrain and limb pixels flicker between sky/terrain classes.
	 *
	 * These inputs re-anchor the SAME planet to the RT view's own frame:
	 * the snapshot center is re-expressed relative to the RT view origin
	 * (double-subtract in cm, then narrow: far-field precise), and rays use
	 * the RT view/projection rotation. The planet identity (center world
	 * position, rotation, radii, sun) still comes from the snapshot; only
	 * the camera-relative presentation is re-based, so LUT content (baked
	 * from snapshot inputs) stays valid while rays match the depth buffer
	 * exactly. Pure math, no UObject access, unit-testable.
	 *
	 * ViewHeightKm here is the RT-relative center distance (ray geometry);
	 * LUT uv mapping keeps using the snapshot (bake) height so sampling
	 * inverts the bake parameterization exactly.
	 */
	struct FHillaireCompositeViewInputs
	{
		// Planet-local camera position, km, RT-relative (unnormalized).
		FVector3f CameraPlanetLocalKm = FVector3f::ZeroVector;
		// Normalized planet-local camera up, RT-relative.
		FVector3f CameraUpLocal = FVector3f(0.0f, 0.0f, 1.0f);
		// RT center distance, km (ray geometry height, unclamped).
		float ViewHeightKm = 0.0f;
		// Sun elevation cosine the bake consumed (diagnostics/keys).
		float SunElevationCos = -2.0f;
		// Inverse RT projection (row-vector: mul(float4(clip,1), InvProj)).
		FMatrix InvProjMatrix = FMatrix::Identity;
		// RT view-to-planet-local rotation, translation zeroed (row-vector:
		// mul(dir, (float3x3)ViewToPlanetLocalRot)).
		FMatrix ViewToPlanetLocalRot = FMatrix::Identity;
		// False when any RT input is degenerate (NaN, non-invertible
		// projection): callers must fall back to snapshot-derived inputs.
		bool bValid = false;
	};

	static FHillaireCompositeViewInputs ComputeCompositeViewInputs(
		const FVector3f& CenterCamRelativeKmSnap,
		const FQuat& PlanetRotation,
		const FVector& SnapshotOriginCm,
		const FVector& RTViewOriginCm,
		const FMatrix& RTViewMatrix,
		const FMatrix& RTProjectionMatrix,
		const FVector3f& PrimarySunDirLocal);

	/**
	 * Pure SkyView rebuild decision (unit-testable): rebuild when the
	 * predicate fires, when MS regenerated underneath (its values bake into
	 * the march), or when no pooled target exists yet.
	 */
	static bool ShouldRebuildSkyView(
		const FHillaireLutRegenQuery& Regen,
		bool bMultiScatteringRegenerated,
		bool bHavePooledSkyView);

	struct FHillairePlanetLutTargets
	{
		TRefCountPtr<IPooledRenderTarget> Transmittance;
		TRefCountPtr<IPooledRenderTarget> MultiScattering;
		// Reserved for Phase 2B (SkyView). Never allocated in Phase 2A.
		TRefCountPtr<IPooledRenderTarget> SkyView;
	};

	const FHillairePlanetLutTargets* FindTargets(int32 PlanetId) const;

	/**
	 * Locked target copy for GameThread debug readers (ref-counted
	 * handles, safe to use after the lock releases even if the slot is
	 * released concurrently on the render thread's behalf).
	 */
	bool CopyTargets(int32 PlanetId, FHillairePlanetLutTargets& OutTargets) const;

	// ---- Phase 2C: Aerial perspective evaluation (view-dependent) ----
	//
	// The camera volume is NOT a cached planet LUT: camera position, view
	// rays and sun direction enter every froxel, so caching it per planet
	// would be incorrect (task section 9). Instead:
	//   planet-dependent data (T/MS/SkyView) stays cached via EnsurePlanetLuts;
	//   camera/view-dependent evaluation runs ON DEMAND into pooled SCRATCH
	//   (one reusable target per planet, no Valid flags, no regen predicates,
	//   never perturbs LutStates/cache keys).
	// Callers ensure T/MS first in the same graph and pass the handles in.
	// Production calls this from the BeforeDOF composite graph and consumes
	// the transient OutVolume in that same graph (see ShouldCompositeAerial
	// note); the pooled extraction additionally serves debug/dump readers.
	struct FHillaireAerialViewInputs
	{
		// Planet-local camera position, km (unnormalized camLocal).
		FVector3f CameraPlanetLocalKm = FVector3f::ZeroVector;
		// Inverse projection (snapshot ProjectionMatrix, inverted once).
		// Row-vector convention in-shader: mul(float4(clip,1), InvProjMatrix).
		FMatrix InvProjMatrix = FMatrix::Identity;
		// View-to-planet-local rotation only, translation zeroed:
		// transpose(snapshot ViewMatrix rotation) * Qconj (row-vector order:
		// view -> cam-relative world -> planet-local, matching the reference
		// mul(gSkyInvViewMat, HViewPos) followed by the quat-conjugate frame
		// change). Row-vector convention in-shader:
		// mul(dir, (float3x3)ViewToPlanetLocalRot).
		FMatrix ViewToPlanetLocalRot = FMatrix::Identity;
	};

	/**
	 * Pure view-input derivation (unit-testable, RenderThread-safe): planet-
	 * local camera + inverse projection + view-to-planet rotation from
	 * snapshot data. No UObject access, no manager state touched.
	 */
	static FHillaireAerialViewInputs ComputeAerialViewInputs(
		const FVector3f& CenterCamRelativeKm,
		const FQuat& PlanetRotation,
		const FMatrix& SnapViewMatrix,
		const FMatrix& SnapProjectionMatrix);

	/**
	 * Enqueue one aerial volume evaluation (P3 compute, 32x32x32). ALWAYS
	 * evaluates when called (view-dependent: no caching predicates apply).
	 * Reuses the per-planet pooled scratch target (no per-frame allocation);
	 * content is scratch (overwritten every evaluation by design).
	 * Returns false for unregistered planets. OutVolume receives the
	 * transient RDG handle for same-graph consumers (debug/readback).
	 */
	bool EvaluateAerialPerspective(
		FRDGBuilder& GraphBuilder,
		int32 PlanetId,
		const FHillaireAtmosphereProfile& Profile,
		const FHillaireAerialViewInputs& ViewInputs,
		const FVector3f& PrimarySunDirLocal,
		FRDGTextureRef RdgTransmittance,
		FRDGTextureRef RdgMultiScattering,
		FRDGTextureRef& OutVolume);

	/** Pooled aerial scratch for a planet (nullptr if never evaluated). */
	const TRefCountPtr<IPooledRenderTarget>* FindAerialScratch(int32 PlanetId) const;

	// ---- Phase 2D: Aerial composite (post-process, BeforeDOF) ----
	//
	// Consumes the Phase-2C camera volume + scene color/depth:
	// Out = In * (1-AP.a) + SunColor * AP.rgb * PreExposure, sky identical.
	// GPU-only production path (no readback here; validation reads back).
	// The output texture is created fresh each call (same desc as input +
	// UAV); callers return it as the new scene color.
	//
	// PRODUCTION GRAPH DESIGN (hardened): the volume is evaluated IN THE
	// SAME RDG GRAPH as the composite (EvaluateAerialPerspective called from
	// the BeforeDOF hook, transient handoff, no pool round-trip). A pooled
	// PreRenderView volume CANNOT feed the composite: QueueTextureExtraction
	// nulls the pooled handle synchronously at ENQUEUE time and the fresh
	// fill only lands at graph execute, i.e. after BeforeDOF already built,
	// so a pool-reading composite always sees null (dead aerial) — and the
	// pooled extraction is still queued for debug/dump readers (1-frame
	// latency, GameThread-safe).
	//
	// Pure gating decision (unit-testable): composite runs only when the CVar
	// enables it, the view has atmosphere content, the camera is inside the
	// governing atmosphere (reference: volume holds air only there), and the
	// Transmittance + MultiScattering inputs for the in-graph evaluation are
	// available. Every other case is identity (baseline preserved).
	static bool ShouldCompositeAerial(
		bool bCVarEnabled,
		bool bHasAtmosphereContent,
		bool bCameraInsideAtmosphere,
		bool bHaveTransmittance,
		bool bHaveMultiScattering);

	bool CompositeAerialPerspective(
		FRDGBuilder& GraphBuilder,
		FRDGTextureSRVRef SceneColorSrv,
		FRDGTextureRef RdgSceneDepth,
		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer,
		const FMatrix& InvProjMatrix,
		const FVector3f& SunColorAttenuation,
		float AerialKmPerSlice,
		float AerialAltitude01,
		float AerialSunElevCos,
		FRDGTextureRef RdgAerialVolume,
		const FIntRect& ViewRect,
		FRDGTextureRef& OutColor);

	// ---- Production sky background (SkyView sampling, BeforeDOF) ----
	//
	// Verbatim port of the reference FASTSKY branch
	// (RenderSkyRayMarching.hlsl lines 432-466): per-pixel planet-local ray
	// from the snapshot matrices, viewZenith + sun-plane azimuth uv mapping,
	// SkyView transfer sample, view-ray transmittance lookup, composite
	// (sky + T * background) with unit-white transfer x slot-0
	// ColorAttenuation x view PreExposure (the same buffer-space convention
	// as the Phase-2D aerial composite). Opaque pixels pass through identical
	// (aerial owns them; no double-scatter); background pixels (reversed-Z
	// device depth <= epsilon) carry live scattered sky radiance over the
	// transmittance-attenuated background. No sun disk, no hardcoded gradient.
	//
	// Pure gating decision (unit-testable): sky runs only when its CVar
	// enables it, the view has atmosphere content, a primary sun is resolved
	// and the SkyView + Transmittance LUTs are available, AND the camera is
	// inside the atmosphere (ViewHeightKm < TopRadiusKm). Space views must
	// NOT show a full-screen atmospheric sky; the planet limb is handled by
	// the ray-marching path when a view ray intersects the atmosphere.
	static bool ShouldCompositeSky(
		bool bCVarEnabled,
		bool bHasAtmosphereContent,
		bool bHavePrimary,
		bool bHaveSkyView,
		bool bHaveTransmittance,
		bool bCameraInsideAtmosphere);

bool CompositeSkyBackground(
 		FRDGBuilder& GraphBuilder,
 		FRDGTextureSRVRef SceneColorSrv,
 		FRDGTextureRef RdgSceneDepth,
 		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer,
 		const FMatrix& InvProjMatrix,
 		const FVector3f& CameraPlanetLocalKm,
 		const FMatrix& ViewToPlanetLocalRot,
 		const FVector3f& PrimarySunDirLocal,
 		const FVector3f& SunColorAttenuation,
 		float BottomRadiusKm,
 		float TopRadiusKm,
 		float ViewHeightKm,
 		float SunAngularRadiusRad,
 		FRDGTextureRef RdgSkyView,
 		FRDGTextureRef RdgTransmittance,
 		const FIntRect& ViewRect,
 		FRDGTextureRef& OutColor);

	// ---- FASE 10: Debug visualization (diagnostic only, r.Hillaire.DebugMode) ----
	//
	// Fullscreen view of REAL GPU data: 1 = Transmittance LUT, 2 =
	// MultiScattering LUT, 3 = SkyView LUT, 4 = Aerial Perspective volume,
	// 5 = density/profile from the live governing profile, 6 = per-pixel
	// planet-local view ray, 7 = view zenith cosine, 8 = SkyView UV,
	// 9 = ray/depth classification mask, 10 = view-ray transmittance,
	// 11 = raw SkyView transfer. Raw linear HDR (alpha 1); the mode's own
	// source is always real data, never-sampled slots bind harmless
	// fallbacks (RDG fatals on unset SRV params). Any other mode keeps the
	// input (baseline preserved).
	//
	// Pure gating decision (unit-testable): each mode runs only when its own
	// source exists (mode 5 needs the profile alone, always available when
	// the snapshot has content; modes 6-11 additionally need the ray frame:
	// matrices + depth bound by the caller, plus their LUT for 10/11).
	static bool ShouldVisualizeDebug(
		int32 DebugMode,
		bool bHaveTransmittance,
		bool bHaveMultiScattering,
		bool bHaveSkyView,
		bool bHaveAerial,
		bool bHaveRayFrame);

	bool CompositeDebugVisualization(
		FRDGBuilder& GraphBuilder,
		FRDGTextureSRVRef SceneColorSrv,
		const FIntRect& ViewRect,
		int32 DebugMode,
		FRDGTextureRef RdgTransmittance,
		FRDGTextureRef RdgMultiScattering,
		FRDGTextureRef RdgSkyView,
		FRDGTextureRef RdgAerialVolume,
		const FHillaireAtmosphereProfile& Profile,
		FRDGTextureRef RdgSceneDepth,
		const FHillaireAerialViewInputs& RayFrame,
		const FVector3f& PrimarySunDirLocal,
		float ViewHeightKm,
		const FVector3f& StarCamRelativeKm,
		const FVector3f& CenterCamRelativeKmDbg,
		const FVector4f& PlanetQuatWS,
		FRDGTextureRef& OutColor);

private:
	// Serializes GameThread registration lifecycle vs RenderThread
	// consumption (see Lifecycle note above). Mutable so const readers
	// (FindTargets/FindAerialScratch/Copy*) can take it.
	mutable FCriticalSection RegistryLock;
	TMap<int32, FHillairePlanetLutState> LutStates;
	// STABLE extraction storage (crash-hardening, FASE 9): RDG
	// QueueTextureExtraction stores a RAW POINTER into the target across the
	// enqueue->execute gap and fires the assignment WITHOUT holding
	// RegistryLock. A TMap rehash (new planet slot, governing flap across
	// views) relocates the value and dangles that pointer: the pooled entry
	// then stays null forever (observed: aerial scratch permanently null)
	// or the write lands on freed memory (observed: intermittent
	// EXCEPTION_ACCESS_VIOLATION). Fixed slot arrays are stable for the
	// manager lifetime, so stored pointers can never dangle. PlanetIds are
	// slot indices in [0, HILLAIRE_MAX_PLANETS) by subsystem construction
	// (AssignPlanetSlot); anything else is rejected like an unknown slot.
	// LutStates keeps its TMap: no RDG-held pointers into it, only transient
	// locked finds.
	FHillairePlanetLutTargets LutTargetStorage[HILLAIRE_MAX_PLANETS];
	bool LutTargetUsed[HILLAIRE_MAX_PLANETS] = {};
	TRefCountPtr<IPooledRenderTarget> AerialScratchStorage[HILLAIRE_MAX_PLANETS];
	bool AerialScratchUsed[HILLAIRE_MAX_PLANETS] = {};

	static bool IsValidSlot(int32 PlanetId)
	{
		return PlanetId >= 0 && PlanetId < HILLAIRE_MAX_PLANETS;
	}
};
