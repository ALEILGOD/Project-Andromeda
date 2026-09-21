#include "HillaireLutManager.h"

#include "GlobalShader.h"
#include "HillaireAtmosphereLog.h"
#include "HillaireHash.h"
#include "HillaireRdgHelpers.h"
#include "HillaireShaders.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"

FHillaireLutManager::FHillaireLutManager() = default;
FHillaireLutManager::~FHillaireLutManager() = default;

void FHillaireLutManager::RegisterPlanet(int32 PlanetId)
{
	FScopeLock Lock(&RegistryLock);
	if (!IsValidSlot(PlanetId))
	{
		UE_LOG(LogHillaireAtmosphere, Warning,
			TEXT("RegisterPlanet: PlanetId %d outside slot range [0, %d); slot rejected."),
			PlanetId, HILLAIRE_MAX_PLANETS);
		return;
	}
	if (!LutStates.Contains(PlanetId))
	{
		LutStates.Add(PlanetId, FHillairePlanetLutState());
	}
}

void FHillaireLutManager::UnregisterPlanet(int32 PlanetId)
{
	FScopeLock Lock(&RegistryLock);
	LutStates.Remove(PlanetId);
	if (IsValidSlot(PlanetId))
	{
		LutTargetStorage[PlanetId] = FHillairePlanetLutTargets();
		LutTargetUsed[PlanetId] = false;
		AerialScratchStorage[PlanetId] = nullptr;
		AerialScratchUsed[PlanetId] = false;
	}
}

void FHillaireLutManager::Clear()
{
	FScopeLock Lock(&RegistryLock);
	LutStates.Empty();
	for (int32 i = 0; i < HILLAIRE_MAX_PLANETS; ++i)
	{
		LutTargetStorage[i] = FHillairePlanetLutTargets();
		LutTargetUsed[i] = false;
		AerialScratchStorage[i] = nullptr;
		AerialScratchUsed[i] = false;
	}
}

bool FHillaireLutManager::HasPlanet(int32 PlanetId) const
{
	FScopeLock Lock(&RegistryLock);
	return LutStates.Contains(PlanetId);
}

bool FHillaireLutManager::CopyLutState(int32 PlanetId, FHillairePlanetLutState& OutState) const
{
	FScopeLock Lock(&RegistryLock);
	if (const FHillairePlanetLutState* Found = LutStates.Find(PlanetId))
	{
		OutState = *Found;
		return true;
	}
	return false;
}

bool FHillaireLutManager::InvalidatePlanetById(int32 PlanetId)
{
	FScopeLock Lock(&RegistryLock);
	if (FHillairePlanetLutState* Found = LutStates.Find(PlanetId))
	{
		Found->InvalidateAll();
		return true;
	}
	return false;
}

bool FHillaireLutManager::CopyTargets(int32 PlanetId, FHillairePlanetLutTargets& OutTargets) const
{
	FScopeLock Lock(&RegistryLock);
	if (!IsValidSlot(PlanetId) || !LutTargetUsed[PlanetId])
	{
		return false;
	}
	OutTargets = LutTargetStorage[PlanetId];
	return true;
}

TRefCountPtr<IPooledRenderTarget> FHillaireLutManager::CopyAerialScratch(int32 PlanetId) const
{
	FScopeLock Lock(&RegistryLock);
	if (!IsValidSlot(PlanetId))
	{
		return nullptr;
	}
	return AerialScratchStorage[PlanetId];
}

FHillairePlanetLutState* FHillaireLutManager::FindLutState(int32 PlanetId)
{
	return LutStates.Find(PlanetId);
}

const FHillairePlanetLutState* FHillaireLutManager::FindLutState(int32 PlanetId) const
{
	return LutStates.Find(PlanetId);
}

uint64 FHillaireLutManager::MakeMultiScatteringKey(uint64 ProfileHash, float MultipleScatteringFactor)
{
	// Reference: msGen = PlanetHashBytes(&currentMultipleScatteringFactor, ..., contentHash).
	return HillaireHash::HashBytes(&MultipleScatteringFactor, sizeof(MultipleScatteringFactor), ProfileHash);
}

FHillaireLutRegenQuery FHillaireLutManager::QueryRegen(
	FHillairePlanetLutState& State,
	uint64 ProfileHash,
	float MultipleScatteringFactor,
	const FVector3f& PrimarySunLocalDir,
	bool bHavePrimary,
	const FGuid& PrimaryLightId,
	float ViewHeightKm,
	bool bFastSkyEnabled,
	const FVector3f& CameraUpLocal)
{
	FHillaireLutRegenQuery Out;

	// Hashes are COMPARED, not just gated on Valid flags, so any missed
	// invalidation still regenerates (reference belt-and-suspenders).
	if (!State.bTransmittanceValid || State.TransmittanceHash != ProfileHash)
	{
		Out.bTransmittance = true;
	}

	const uint64 MsKey = MakeMultiScatteringKey(ProfileHash, MultipleScatteringFactor);
	if (!State.bMultiScatteringValid || State.MultiScatteringHash != MsKey)
	{
		Out.bMultiScattering = true;
	}

	// SkyView sun cache: the bake consumes ONLY the sun elevation above the
	// planet-local camera up, so the cache keys THAT scalar. A rigid planet
	// spin rotates the local sun and the local up identically (elevation and
	// baked content invariant): spin alone never regenerates. A genuine sun
	// move (orbit, day/night, camera orbit at fixed height) changes the
	// elevation and regenerates exactly like the reference.
	if (bHavePrimary)
	{
		const float ElevCos = HillaireSunElevationCos(PrimarySunLocalDir, CameraUpLocal);
		if (!State.bSkyViewSunInit)
		{
			State.SkyViewCachedSunElevationCos = ElevCos;
			State.bSkyViewSunInit = true;
		}
		else if (FMath::Abs(ElevCos - State.SkyViewCachedSunElevationCos)
			> HillaireLimits::SkyViewSunElevationCosDelta)
		{
			State.bSkyViewValid = false;
			State.SkyViewCachedSunElevationCos = ElevCos;
		}
		// Slot-0 identity is part of the generation key: an A-off/B-on swap
		// with an identical direction must still count as a cache miss.
		if (State.LastPrimaryLightId != PrimaryLightId)
		{
			State.bSkyViewValid = false;
			State.LastPrimaryLightId = PrimaryLightId;
			State.bLastPrimaryDirectional = true;
		}
	}

	if (bFastSkyEnabled && bHavePrimary)
	{
		const float Eps = FMath::Max(HillaireLimits::ViewHeightEpsFloorKm, ViewHeightKm * HillaireLimits::ViewHeightEpsRelative);
		if (!State.bSkyViewValid || State.SkyViewHash != ProfileHash
			|| FMath::Abs(ViewHeightKm - State.SkyViewCachedHeightKm) > Eps)
		{
			Out.bSkyView = true;
		}
	}

	return Out;
}

void FHillaireLutManager::MarkTransmittanceBuilt(FHillairePlanetLutState& State, uint64 ProfileHash)
{
	State.TransmittanceHash = ProfileHash;
	State.bTransmittanceValid = true;
	++State.LutBuildCount;
}

void FHillaireLutManager::MarkMultiScatteringBuilt(FHillairePlanetLutState& State, uint64 MultiScatteringKey)
{
	State.MultiScatteringHash = MultiScatteringKey;
	State.bMultiScatteringValid = true;
}

void FHillaireLutManager::MarkSkyViewBuilt(FHillairePlanetLutState& State, uint64 ProfileHash, float ViewHeightKm, const FVector3f& BakedSunDirLocal)
{
	State.SkyViewHash = ProfileHash;
	State.SkyViewCachedHeightKm = ViewHeightKm;
	State.bSkyViewValid = true;
	// Audit snapshot of the sun actually baked (diagnostic only; the regen
	// predicate intentionally stays elevation-based — see QueryRegen).
	const float SunLenSq = BakedSunDirLocal.SizeSquared();
	if (SunLenSq > 1e-12f)
	{
		const float InvLen = 1.0f / FMath::Sqrt(SunLenSq);
		State.SkyViewCachedSunDirLocal = FVector3f(
			BakedSunDirLocal.X * InvLen, BakedSunDirLocal.Y * InvLen, BakedSunDirLocal.Z * InvLen);
		State.bSkyViewSunDirInit = true;
	}
}

void FHillaireLutManager::NoteReuse(FHillairePlanetLutState& State)
{
	++State.LutReuseCount;
}

bool FHillaireLutManager::NotifyPrimarySlotChanged(
	FHillairePlanetLutState& State,
	const FGuid& CurrentPrimaryId,
	bool bCurrentPrimaryDirectional,
	bool bHaveSinglePrimary)
{
	// No single primary -> SkyView unused -> nothing to invalidate.
	if (!bHaveSinglePrimary)
	{
		State.LastPrimaryLightId = CurrentPrimaryId;
		State.bLastPrimaryDirectional = bCurrentPrimaryDirectional;
		return false;
	}
	if (State.LastPrimaryLightId != CurrentPrimaryId
		|| State.bLastPrimaryDirectional != bCurrentPrimaryDirectional)
	{
		State.bSkyViewValid = false;
		State.LastPrimaryLightId = CurrentPrimaryId;
		State.bLastPrimaryDirectional = bCurrentPrimaryDirectional;
		return true;
	}
	return false;
}

void FHillaireLutManager::InvalidatePlanet(FHillairePlanetLutState& State)
{
	State.InvalidateAll();
}

namespace
{
	/** P0 enqueue: per-texel transmittance compute, dispatch from the LUT desc. */
	void AddTransmittanceLutPass(
		FRDGBuilder& GraphBuilder,
		const FHillaireAtmosphereProfile& Profile,
		FRDGTextureRef RdgTransmittance)
	{
		FHillaireTransmittanceLutCS::FParameters* Params =
			GraphBuilder.AllocParameters<FHillaireTransmittanceLutCS::FParameters>();
		HillaireFillAtmosphereUniforms(Params->Atmosphere, Profile);
		Params->TransmittanceUav = GraphBuilder.CreateUAV(RdgTransmittance);

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FHillaireTransmittanceLutCS> Shader(ShaderMap);
		const FIntPoint Extent = RdgTransmittance->Desc.Extent;
		const FIntVector GroupCount(
			(Extent.X + HillaireLimits::TransmittanceThreadGroupX - 1) / HillaireLimits::TransmittanceThreadGroupX,
			(Extent.Y + HillaireLimits::TransmittanceThreadGroupY - 1) / HillaireLimits::TransmittanceThreadGroupY,
			1);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Hillaire.TransmittanceLut"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			Shader,
			Params,
			GroupCount);
	}

	/** P2 enqueue: SkyView per-texel compute, dispatch from the LUT desc. */
	void AddSkyViewLutPass(
		FRDGBuilder& GraphBuilder,
		const FHillaireAtmosphereProfile& Profile,
		const FVector3f& PrimarySunDirLocal,
		const FVector3f& CameraUpLocal,
		float ViewHeightKm,
		FRDGTextureRef RdgTransmittance,
		FRDGTextureRef RdgMultiScattering,
		FRDGTextureRef RdgSkyView)
	{
		FHillaireSkyViewLutCS::FParameters* Params =
			GraphBuilder.AllocParameters<FHillaireSkyViewLutCS::FParameters>();
		HillaireFillAtmosphereUniforms(Params->Atmosphere, Profile);
		Params->PrimarySunDirLocal = PrimarySunDirLocal;
		Params->CameraUpLocal = CameraUpLocal;
		Params->ViewHeightKm = ViewHeightKm;
		Params->MiePhaseG = Profile.MiePhaseG;
		Params->RayMarchMinMaxSPP = FVector2f(
			HillaireLimits::SkyViewMarchMinSamples, HillaireLimits::SkyViewMarchMaxSamples);
		Params->PlanetRadiusOffsetKm = HillaireLimits::PlanetRadiusOffsetKm;
		Params->MultiScatteringLutRes = (float)HillaireLimits::MultiScatteringRes;
		Params->TransmittanceLut = GraphBuilder.CreateSRV(RdgTransmittance);
		Params->MultiScatteringLut = GraphBuilder.CreateSRV(RdgMultiScattering);
		Params->LinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->SkyViewUav = GraphBuilder.CreateUAV(RdgSkyView);

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FHillaireSkyViewLutCS> Shader(ShaderMap);
		const FIntPoint Extent = RdgSkyView->Desc.Extent;
		const FIntVector GroupCount(
			(Extent.X + HillaireLimits::SkyViewThreadGroupX - 1) / HillaireLimits::SkyViewThreadGroupX,
			(Extent.Y + HillaireLimits::SkyViewThreadGroupY - 1) / HillaireLimits::SkyViewThreadGroupY,
			1);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Hillaire.SkyViewLut"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			Shader,
			Params,
			GroupCount);
	}

	/** P1 enqueue: multi-scattering compute, one (1,1,64) group per texel. */
	void AddMultiScatteringLutPass(
		FRDGBuilder& GraphBuilder,
		const FHillaireAtmosphereProfile& Profile,
		float MultipleScatteringFactor,
		FRDGTextureRef RdgTransmittance,
		FRDGTextureRef RdgMultiScattering)
	{
		FHillaireMultiScatteringCS::FParameters* Params =
			GraphBuilder.AllocParameters<FHillaireMultiScatteringCS::FParameters>();
		HillaireFillAtmosphereUniforms(Params->Atmosphere, Profile);
		Params->GroundAlbedo = FVector3f((float)Profile.GroundAlbedo.X, (float)Profile.GroundAlbedo.Y, (float)Profile.GroundAlbedo.Z);
		Params->PlanetRadiusOffsetKm = HillaireLimits::PlanetRadiusOffsetKm;
		Params->MultipleScatteringFactor = MultipleScatteringFactor;
		Params->MultiScatteringLutRes = (float)HillaireLimits::MultiScatteringRes;
		Params->TransmittanceLut = GraphBuilder.CreateSRV(RdgTransmittance);
		Params->LinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->MultiScatteringUav = GraphBuilder.CreateUAV(RdgMultiScattering);

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FHillaireMultiScatteringCS> Shader(ShaderMap);
		// Verbatim threading: exactly one thread group per texel.
		const FIntPoint Extent = RdgMultiScattering->Desc.Extent;
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Hillaire.MultiScatteringLut"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			Shader,
			Params,
			FIntVector(Extent.X, Extent.Y, 1));
	}
} // namespace

bool FHillaireLutManager::ShouldRebuildSkyView(
	const FHillaireLutRegenQuery& Regen,
	bool bMultiScatteringRegenerated,
	bool bHavePooledSkyView)
{
	return Regen.bSkyView || bMultiScatteringRegenerated || !bHavePooledSkyView;
}

FVector3f FHillaireLutManager::ComputeCameraUpLocal(
	const FVector3f& CenterCamRelativeKm, const FQuat& PlanetRotation)
{
	// Single conversion path (HillaireCameraPlanetLocalKm); normalize here.
	return HillaireCameraUpLocal(CenterCamRelativeKm, PlanetRotation);
}

bool FHillaireLutManager::EnsurePlanetLuts(
	FRDGBuilder& GraphBuilder,
	int32 PlanetId,
	const FHillaireAtmosphereProfile& Profile,
	float ViewHeightKm,
	const FVector3f& CenterCamRelativeKm,
	const FQuat& PlanetRotation,
	const FHillaireCompactedLights& ResolvedLights,
	float MultipleScatteringFactor,
	bool bFastSkyEnabled,
	FHillairePlanetLutGraphOutputs& OutOutputs)
{
	// Whole-body serialization: State/Targets stay alive for the entire
	// graph build even if the GameThread releases the slot concurrently
	// (STARMAP spawn/teardown churn). Leaf lock only (GT holders take no
	// other lock), GPU passes unaffected.
	FScopeLock Lock(&RegistryLock);
	OutOutputs = FHillairePlanetLutGraphOutputs();
	FHillairePlanetLutState* State = LutStates.Find(PlanetId);
	if (!State)
	{
		UE_LOG(LogHillaireAtmosphere, Warning,
			TEXT("EnsurePlanetLuts: unregistered PlanetId %d, skipping (register the component first)."), PlanetId);
		return false;
	}
	if (!IsValidSlot(PlanetId))
	{
		UE_LOG(LogHillaireAtmosphere, Warning,
			TEXT("EnsurePlanetLuts: PlanetId %d outside slot range [0, %d); skipping."),
			PlanetId, HILLAIRE_MAX_PLANETS);
		return false;
	}

	// The SkyView LUT is parameterized by an INSIDE height: bake it at the
	// camera height clamped into [Bottom + floor, Top - floor]. Outside the
	// atmosphere the physical limb is the radiance arriving at the top
	// boundary, which is exactly the LUT baked at Top - floor (the reference
	// fast path is only valid inside; this keeps the divergence explicit and
	// bounded instead of baking an out-of-domain LUT). The floor is the same
	// 20 m spec precision rule as before: the parameterization needs h >=
	// Bottom, and the cache key stays stable instead of churning.
	const float HeightFloorKm = Profile.BottomRadiusKm + HillaireLimits::ViewHeightEpsFloorKm;
	const float HeightCeilKm = FMath::Max(
		HeightFloorKm,
		Profile.TopRadiusKm - HillaireLimits::ViewHeightEpsFloorKm);
	const float ClampedHeightKm = FMath::Clamp(ViewHeightKm, HeightFloorKm, HeightCeilKm);

	const uint64 ProfileHash = Profile.ComputeContentHash();
	// SkyView v1 primary = slot 0 whenever at least one light is enabled.
	// (bSinglePrimary still governs ONLY the future final-composite fast path.)
	const bool bHavePrimary = ResolvedLights.Count > 0;
	const FVector3f PrimaryDir = bHavePrimary ? ResolvedLights.Lights[0].LightDirLocal : FVector3f::ZeroVector;
	const FGuid PrimaryId = bHavePrimary ? ResolvedLights.Lights[0].LightId : FGuid();

	// Regen predicates evaluated OUTSIDE graph construction (no no-op passes).
	// The camera up is derived once through the unified helper and feeds
	// both the SkyView bake below and the elevation-based sun cache: bake
	// inputs and cache key can never disagree.
	const FVector3f CameraUpLocal = ComputeCameraUpLocal(CenterCamRelativeKm, PlanetRotation);
	const FHillaireLutRegenQuery Regen = QueryRegen(
		*State, ProfileHash, MultipleScatteringFactor,
		PrimaryDir, bHavePrimary, PrimaryId, ClampedHeightKm, bFastSkyEnabled,
		CameraUpLocal);

	FHillairePlanetLutTargets& Targets = LutTargetStorage[PlanetId];
	LutTargetUsed[PlanetId] = true;

	// ---- P0: Transmittance (light-independent: profile key only) ----
	// Phase-2A behavior frozen (single-light regression gate, task section 26).
	if (Regen.bTransmittance || !Targets.Transmittance.IsValid())
	{
		FRDGTextureRef RdgT = GraphBuilder.CreateTexture(
			HillaireRdg::MakeTransmittanceLutDesc(), TEXT("Hillaire.Transmittance"));
		AddTransmittanceLutPass(GraphBuilder, Profile, RdgT);
		GraphBuilder.QueueTextureExtraction(RdgT, &Targets.Transmittance);
		MarkTransmittanceBuilt(*State, ProfileHash);
		OutOutputs.Transmittance = RdgT;
	}
	else
	{
		OutOutputs.Transmittance = GraphBuilder.RegisterExternalTexture(Targets.Transmittance);
		NoteReuse(*State);
	}

	// ---- P1: MultiScattering (key: profile + MS factor; reads T above) ----
	// Phase-2A behavior frozen (see above).
	const uint64 MsKey = MakeMultiScatteringKey(ProfileHash, MultipleScatteringFactor);
	bool bMsRegenerated = false;
	if (Regen.bMultiScattering || !Targets.MultiScattering.IsValid())
	{
		FRDGTextureRef RdgMs = GraphBuilder.CreateTexture(
			HillaireRdg::MakeMultiScatteringLutDesc(), TEXT("Hillaire.MultiScattering"));
		AddMultiScatteringLutPass(
			GraphBuilder, Profile, MultipleScatteringFactor,
			OutOutputs.Transmittance, RdgMs);
		GraphBuilder.QueueTextureExtraction(RdgMs, &Targets.MultiScattering);
		MarkMultiScatteringBuilt(*State, MsKey);
		OutOutputs.MultiScattering = RdgMs;
		bMsRegenerated = true;
	}
	else
	{
		OutOutputs.MultiScattering = GraphBuilder.RegisterExternalTexture(Targets.MultiScattering);
	}

	// ---- P2: SkyView (slot-0 bake; reads T + MS above) ----
	// Rebuilds when its own predicate fires, when MS regenerated underneath
	// it (MS values are baked into the march - a warm SkyView would go stale),
	// or when no pooled target exists yet. Skipped with zero lights (no sun).
	if (!bHavePrimary)
	{
		return true;
	}
	if (ShouldRebuildSkyView(Regen, bMsRegenerated, Targets.SkyView.IsValid()))
	{
		FRDGTextureRef RdgSky = GraphBuilder.CreateTexture(
			HillaireRdg::MakeSkyViewLutDesc(), TEXT("Hillaire.SkyView"));
		AddSkyViewLutPass(
			GraphBuilder, Profile, PrimaryDir,
			CameraUpLocal,
			ClampedHeightKm,
			OutOutputs.Transmittance, OutOutputs.MultiScattering, RdgSky);
		GraphBuilder.QueueTextureExtraction(RdgSky, &Targets.SkyView);
		MarkSkyViewBuilt(*State, ProfileHash, ClampedHeightKm, PrimaryDir);
		OutOutputs.SkyView = RdgSky;
	}
	else
	{
		OutOutputs.SkyView = GraphBuilder.RegisterExternalTexture(Targets.SkyView);
	}

	return true;
}

const FHillaireLutManager::FHillairePlanetLutTargets* FHillaireLutManager::FindTargets(int32 PlanetId) const
{
	if (!IsValidSlot(PlanetId) || !LutTargetUsed[PlanetId])
	{
		return nullptr;
	}
	return &LutTargetStorage[PlanetId];
}

FHillaireLutManager::FHillaireAerialViewInputs FHillaireLutManager::ComputeAerialViewInputs(
	const FVector3f& CenterCamRelativeKm,
	const FQuat& PlanetRotation,
	const FMatrix& SnapViewMatrix,
	const FMatrix& SnapProjectionMatrix)
{
	FHillaireAerialViewInputs Out;
	// Camera sits at the relative origin: camLocal = conj(Q) * (0 - CenterRel).
	// Unnormalized here (SkyView's ComputeCameraUpLocal normalizes; the volume
	// march needs the true position). Single conversion path, no duplication
	// of the frame rule.
	Out.CameraPlanetLocalKm = HillaireCameraPlanetLocalKm(CenterCamRelativeKm, PlanetRotation);

	// View-to-planet-local rotation: transpose of the snapshot view rotation
	// (view -> cam-relative world), built explicitly, then right-apply the
	// planet-frame conjugation (world -> planet-local). Row-vector order
	// (v * (W * Qc) applies W first): matches the reference
	// mul(gSkyInvViewMat, HViewPos) followed by GetPlanetViewDirLocal.
	// Snapshot ViewMatrix is rotation-only (translation stripped by the
	// ViewExtension).
	FMatrix ViewToWorld(EForceInit::ForceInitToZero);
	for (int32 R = 0; R < 3; ++R)
	{
		for (int32 C = 0; C < 3; ++C)
		{
			ViewToWorld.M[R][C] = SnapViewMatrix.M[C][R];
		}
	}
	ViewToWorld.M[3][3] = 1.0;
	Out.ViewToPlanetLocalRot = ViewToWorld * PlanetRotation.Inverse().ToMatrix();

	// Inverse projection (pure, RT-safe; views always carry a valid proj).
	Out.InvProjMatrix = SnapProjectionMatrix.Inverse();
	return Out;
}

FHillaireLutManager::FHillaireCompositeViewInputs FHillaireLutManager::ComputeCompositeViewInputs(
	const FVector3f& CenterCamRelativeKmSnap,
	const FQuat& PlanetRotation,
	const FVector& SnapshotOriginCm,
	const FVector& RTViewOriginCm,
	const FMatrix& RTViewMatrix,
	const FMatrix& RTProjectionMatrix,
	const FVector3f& PrimarySunDirLocal)
{
	FHillaireCompositeViewInputs Out;

	// Re-anchor the snapshot center to the RT view origin: double-subtract
	// in cm FIRST (far-field precision), then narrow to km. When both
	// origins coincide this reduces to CenterCamRelativeKmSnap exactly.
	const FVector OriginDeltaCm = SnapshotOriginCm - RTViewOriginCm;
	if (!FMath::IsFinite(OriginDeltaCm.X) || !FMath::IsFinite(OriginDeltaCm.Y) || !FMath::IsFinite(OriginDeltaCm.Z))
	{
		return Out; // bValid = false -> caller falls back to snapshot inputs
	}
	const FVector3f OriginDeltaKm(
		(float)(OriginDeltaCm.X * HillaireLimits::KmPerCm),
		(float)(OriginDeltaCm.Y * HillaireLimits::KmPerCm),
		(float)(OriginDeltaCm.Z * HillaireLimits::KmPerCm));
	const FVector3f CenterRelRT = CenterCamRelativeKmSnap + OriginDeltaKm;
	if (CenterRelRT.ContainsNaN())
	{
		return Out;
	}
	Out.ViewHeightKm = CenterRelRT.Size();

	// Planet-local camera + up through the single conversion path.
	Out.CameraPlanetLocalKm = HillaireCameraPlanetLocalKm(CenterRelRT, PlanetRotation);
	if (Out.CameraPlanetLocalKm.ContainsNaN())
	{
		return Out;
	}
	Out.CameraUpLocal = HillaireCameraUpLocal(CenterRelRT, PlanetRotation);
	Out.SunElevationCos = HillaireSunElevationCos(PrimarySunDirLocal, Out.CameraUpLocal);

	// View-to-planet-local rotation from the RT view's own rotation:
	// transpose(RTView rotation) * Qconj. Row-vector order (v * (W * Qc)
	// applies W first): view -> RT cam-relative world -> planet-local.
	// Matches ComputeAerialViewInputs with RT matrices instead of snapshot.
	FMatrix ViewToWorld(EForceInit::ForceInitToZero);
	for (int32 R = 0; R < 3; ++R)
	{
		for (int32 C = 0; C < 3; ++C)
		{
			const double V = RTViewMatrix.M[C][R];
			if (!FMath::IsFinite(V))
			{
				return Out;
			}
			ViewToWorld.M[R][C] = V;
		}
	}
	ViewToWorld.M[3][3] = 1.0;
	Out.ViewToPlanetLocalRot = ViewToWorld * PlanetRotation.Inverse().ToMatrix();

	// Inverse RT projection. A non-invertible projection (NaN/zero) keeps
	// bValid false and the caller falls back to the snapshot path.
	const FMatrix InvProj = RTProjectionMatrix.Inverse();
	if (!FMath::IsFinite(InvProj.M[0][0]) || !FMath::IsFinite(InvProj.M[3][3]))
	{
		return Out;
	}
	// Reject NaN anywhere in the inverse (cheap full scan, RT-safe).
	for (int32 R = 0; R < 4; ++R)
	{
		for (int32 C = 0; C < 4; ++C)
		{
			if (!FMath::IsFinite(InvProj.M[R][C]))
			{
				return Out;
			}
		}
	}
	Out.InvProjMatrix = InvProj;
	Out.bValid = true;
	return Out;
}

/** Narrow double matrices once at upload (FMatrix is not SHADER_PARAMETER-able). */
static FMatrix44f ToMatrix44f(const FMatrix& M)
{
	FMatrix44f R;
	for (int32 Rr = 0; Rr < 4; ++Rr)
	{
		for (int32 Cc = 0; Cc < 4; ++Cc)
		{
			R.M[Rr][Cc] = (float)M.M[Rr][Cc];
		}
	}
	return R;
}

namespace
{
	/** P3 enqueue: aerial volume per-froxel compute, dispatch from the volume desc. */
	void AddAerialPerspectivePass(
		FRDGBuilder& GraphBuilder,
		const FHillaireAtmosphereProfile& Profile,
		const FHillaireLutManager::FHillaireAerialViewInputs& ViewInputs,
		const FVector3f& PrimarySunDirLocal,
		FRDGTextureRef RdgTransmittance,
		FRDGTextureRef RdgMultiScattering,
		FRDGTextureRef RdgVolume)
	{
		FHillaireAerialPerspectiveCS::FParameters* Params =
			GraphBuilder.AllocParameters<FHillaireAerialPerspectiveCS::FParameters>();
		HillaireFillAtmosphereUniforms(Params->Atmosphere, Profile);
		Params->CameraPlanetLocalKm = ViewInputs.CameraPlanetLocalKm;
		Params->InvProjMatrix = ToMatrix44f(ViewInputs.InvProjMatrix);
		Params->ViewToPlanetLocalRot = ToMatrix44f(ViewInputs.ViewToPlanetLocalRot);
		Params->PrimarySunDirLocal = PrimarySunDirLocal;
		Params->MiePhaseG = Profile.MiePhaseG;
		Params->PlanetRadiusOffsetKm = HillaireLimits::PlanetRadiusOffsetKm;
		Params->AerialGroundClampLiftKm = HillaireLimits::AerialGroundClampLiftKm;
		// Atmosphere-relative slice depth (see HillaireLimits): 25 slices per
		// envelope, matching the reference's relative resolution.
		Params->AerialKmPerSlice = HillaireLimits::AerialKmPerSliceForEnvelope(
			Profile.TopRadiusKm - Profile.BottomRadiusKm);
		Params->MultiScatteringLutRes = (float)HillaireLimits::MultiScatteringRes;
		Params->TransmittanceLut = GraphBuilder.CreateSRV(RdgTransmittance);
		Params->MultiScatteringLut = GraphBuilder.CreateSRV(RdgMultiScattering);
		Params->LinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->AerialVolumeUav = GraphBuilder.CreateUAV(RdgVolume);

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FHillaireAerialPerspectiveCS> Shader(ShaderMap);
		// FRDGTextureDesc carries 2D Extent + separate Depth (RHITextureDesc).
		const FIntVector Extent(
			RdgVolume->Desc.Extent.X, RdgVolume->Desc.Extent.Y, RdgVolume->Desc.Depth);
		const FIntVector GroupCount(
			(Extent.X + HillaireLimits::AerialThreadGroupX - 1) / HillaireLimits::AerialThreadGroupX,
			(Extent.Y + HillaireLimits::AerialThreadGroupY - 1) / HillaireLimits::AerialThreadGroupY,
			(Extent.Z + HillaireLimits::AerialThreadGroupZ - 1) / HillaireLimits::AerialThreadGroupZ);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Hillaire.AerialPerspective"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			Shader,
			Params,
			GroupCount);
	}
} // namespace

bool FHillaireLutManager::EvaluateAerialPerspective(
	FRDGBuilder& GraphBuilder,
	int32 PlanetId,
	const FHillaireAtmosphereProfile& Profile,
	const FHillaireAerialViewInputs& ViewInputs,
	const FVector3f& PrimarySunDirLocal,
	FRDGTextureRef RdgTransmittance,
	FRDGTextureRef RdgMultiScattering,
	FRDGTextureRef& OutVolume)
{
	// Same whole-body guarantee as EnsurePlanetLuts: the scratch entry
	// cannot be released mid-evaluation by GameThread teardown.
	FScopeLock Lock(&RegistryLock);
	OutVolume = nullptr;
	if (!LutStates.Contains(PlanetId))
	{
		UE_LOG(LogHillaireAtmosphere, Warning,
			TEXT("EvaluateAerialPerspective: unregistered PlanetId %d, skipping (register the component first)."), PlanetId);
		return false;
	}
	if (!IsValidSlot(PlanetId))
	{
		UE_LOG(LogHillaireAtmosphere, Warning,
			TEXT("EvaluateAerialPerspective: PlanetId %d outside slot range [0, %d); skipping."),
			PlanetId, HILLAIRE_MAX_PLANETS);
		return false;
	}
	// Scratch reuse: one pooled target per planet, overwritten every call by
	// design (view-dependent output must never be mistaken for cached data).
	// Stable slot address (never a map value: see header note).
	TRefCountPtr<IPooledRenderTarget>& Scratch = AerialScratchStorage[PlanetId];
	AerialScratchUsed[PlanetId] = true;
	FRDGTextureRef RdgVolume = GraphBuilder.CreateTexture(
		HillaireRdg::MakeAerialVolumeDesc(), TEXT("Hillaire.AerialPerspective"));
	AddAerialPerspectivePass(
		GraphBuilder, Profile, ViewInputs, PrimarySunDirLocal,
		RdgTransmittance, RdgMultiScattering, RdgVolume);
	GraphBuilder.QueueTextureExtraction(RdgVolume, &Scratch);
	OutVolume = RdgVolume;
	return true;
}

const TRefCountPtr<IPooledRenderTarget>* FHillaireLutManager::FindAerialScratch(int32 PlanetId) const
{
	if (!IsValidSlot(PlanetId) || !AerialScratchUsed[PlanetId])
	{
		return nullptr;
	}
	return &AerialScratchStorage[PlanetId];
}

bool FHillaireLutManager::ShouldCompositeAerial(
	bool bCVarEnabled,
	bool bHasAtmosphereContent,
	bool bCameraInsideAtmosphere,
	bool bHaveTransmittance,
	bool bHaveMultiScattering)
{
	return bCVarEnabled && bHasAtmosphereContent && bCameraInsideAtmosphere
		&& bHaveTransmittance && bHaveMultiScattering;
}

namespace
{
	/** P4-COMP enqueue: fullscreen aerial composite over the view rect. */
	void AddAerialCompositePass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureSRVRef SceneColorSrv,
		FRDGTextureRef RdgSceneDepth,
		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer,
		const FMatrix44f& InvProjMatrix,
		const FVector3f& SunColorAttenuation,
		float AerialKmPerSlice,
		FRDGTextureRef RdgAerialVolume,
		const FIntRect& ViewRect,
		FRDGTextureRef RdgOutput)
	{
		FHillaireAerialCompositeCS::FParameters* Params =
			GraphBuilder.AllocParameters<FHillaireAerialCompositeCS::FParameters>();
		// Engine pattern (mirrors PostProcessBokehDOF/PostProcessDOF):
		// immediate view UB binds directly to the RDG UB struct member.
		Params->View = ViewUniformBuffer;
		Params->SceneColorInput = SceneColorSrv;
		Params->SceneDepthInput = GraphBuilder.CreateSRV(RdgSceneDepth);
		Params->AerialVolume = GraphBuilder.CreateSRV(RdgAerialVolume);
		Params->LinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->InvProjMatrix = InvProjMatrix;
		Params->SunColorAttenuation = SunColorAttenuation;
		Params->ViewRectMin = FVector2f((float)ViewRect.Min.X, (float)ViewRect.Min.Y);
		Params->ViewRectSize = FVector2f((float)ViewRect.Width(), (float)ViewRect.Height());
		Params->AerialKmPerSlice = AerialKmPerSlice;
		Params->SkyDepthEpsilon = HillaireLimits::CompositeSkyDepthEpsilon;
		Params->CompositeOutputUav = GraphBuilder.CreateUAV(RdgOutput);

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FHillaireAerialCompositeCS> Shader(ShaderMap);
		const FIntVector GroupCount(
			(ViewRect.Width() + HillaireLimits::CompositeThreadGroupX - 1) / HillaireLimits::CompositeThreadGroupX,
			(ViewRect.Height() + HillaireLimits::CompositeThreadGroupY - 1) / HillaireLimits::CompositeThreadGroupY,
			1);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Hillaire.AerialComposite"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			Shader,
			Params,
			GroupCount);
	}
} // namespace

bool FHillaireLutManager::CompositeAerialPerspective(
	FRDGBuilder& GraphBuilder,
	FRDGTextureSRVRef SceneColorSrv,
	FRDGTextureRef RdgSceneDepth,
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer,
	const FMatrix& InvProjMatrix,
	const FVector3f& SunColorAttenuation,
	float AerialKmPerSlice,
	FRDGTextureRef RdgAerialVolume,
	const FIntRect& ViewRect,
	FRDGTextureRef& OutColor)
{
	OutColor = nullptr;
	if (!SceneColorSrv || !RdgSceneDepth || !RdgAerialVolume || ViewRect.IsEmpty())
	{
		return false;
	}
	// Output clones the input scene-color desc (+UAV). Format/flags preserved
	// exactly: the pass must not alter pipeline color behavior by itself.
	FRDGTextureDesc OutDesc = SceneColorSrv->Desc.Texture->Desc;
	OutDesc.Flags |= TexCreate_UAV;
	OutDesc.ClearValue = FClearValueBinding::None;
	FRDGTextureRef RdgOutput = GraphBuilder.CreateTexture(OutDesc, TEXT("Hillaire.AerialComposite"));
	AddAerialCompositePass(
		GraphBuilder, SceneColorSrv, RdgSceneDepth, ViewUniformBuffer,
		ToMatrix44f(InvProjMatrix), SunColorAttenuation, AerialKmPerSlice,
		RdgAerialVolume, ViewRect, RdgOutput);
	OutColor = RdgOutput;
	return true;
}

bool FHillaireLutManager::ShouldCompositeSky(
	bool bCVarEnabled,
	bool bHasAtmosphereContent,
	bool bHavePrimary,
	bool bHaveSkyView,
	bool bHaveTransmittance,
	bool bCameraInsideAtmosphere)
{
	return bCVarEnabled && bHasAtmosphereContent && bHavePrimary && bHaveSkyView && bHaveTransmittance && bCameraInsideAtmosphere;
}

namespace
{
	/** P4-SKY enqueue: fullscreen SkyView background sampling over the view rect. */
	void AddSkyBackgroundPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureSRVRef SceneColorSrv,
		FRDGTextureRef RdgSceneDepth,
		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer,
		const FMatrix44f& InvProjMatrix,
		const FVector3f& CameraPlanetLocalKm,
		const FMatrix44f& ViewToPlanetLocalRot,
		const FVector3f& PrimarySunDirLocal,
		const FVector3f& SunColorAttenuation,
		float BottomRadiusKm,
		float TopRadiusKm,
		float ViewHeightKm,
		float SunAngularRadiusRad,
		FRDGTextureRef RdgSkyView,
		FRDGTextureRef RdgTransmittance,
		const FIntRect& ViewRect,
		FRDGTextureRef RdgOutput)
	{
		FHillaireSkyBackgroundCS::FParameters* Params =
			GraphBuilder.AllocParameters<FHillaireSkyBackgroundCS::FParameters>();
		// Engine pattern (mirrors the aerial composite): immediate view UB
		// binds directly to the RDG UB struct member.
		Params->View = ViewUniformBuffer;
		Params->SceneColorInput = SceneColorSrv;
		Params->SceneDepthInput = GraphBuilder.CreateSRV(RdgSceneDepth);
		Params->SkyViewLut = GraphBuilder.CreateSRV(RdgSkyView);
		Params->TransmittanceLut = GraphBuilder.CreateSRV(RdgTransmittance);
		Params->LinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->InvProjMatrix = InvProjMatrix;
		Params->ViewToPlanetLocalRot = ViewToPlanetLocalRot;
		Params->CameraPlanetLocalKm = CameraPlanetLocalKm;
		Params->PrimarySunDirLocal = PrimarySunDirLocal;
		Params->SunColorAttenuation = SunColorAttenuation;
		Params->BottomRadiusKm = BottomRadiusKm;
		Params->TopRadiusKm = TopRadiusKm;
		Params->ViewHeightKm = ViewHeightKm;
		Params->SunAngularRadiusRad = SunAngularRadiusRad;
		Params->ViewRectMin = FVector2f((float)ViewRect.Min.X, (float)ViewRect.Min.Y);
		Params->ViewRectSize = FVector2f((float)ViewRect.Width(), (float)ViewRect.Height());
		Params->SkyDepthEpsilon = HillaireLimits::CompositeSkyDepthEpsilon;
		Params->CompositeOutputUav = GraphBuilder.CreateUAV(RdgOutput);

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FHillaireSkyBackgroundCS> Shader(ShaderMap);
		const FIntVector GroupCount(
			(ViewRect.Width() + HillaireLimits::CompositeThreadGroupX - 1) / HillaireLimits::CompositeThreadGroupX,
			(ViewRect.Height() + HillaireLimits::CompositeThreadGroupY - 1) / HillaireLimits::CompositeThreadGroupY,
			1);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Hillaire.SkyBackground"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			Shader,
			Params,
			GroupCount);
	}
} // namespace

bool FHillaireLutManager::CompositeSkyBackground(
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
	FRDGTextureRef& OutColor)
{
	OutColor = nullptr;
	if (!SceneColorSrv || !RdgSceneDepth || !RdgSkyView || !RdgTransmittance || ViewRect.IsEmpty())
	{
		return false;
	}
	// Output clones the input scene-color desc (+UAV). Format/flags preserved
	// exactly: the pass must not alter pipeline color behavior by itself.
	FRDGTextureDesc OutDesc = SceneColorSrv->Desc.Texture->Desc;
	OutDesc.Flags |= TexCreate_UAV;
	OutDesc.ClearValue = FClearValueBinding::None;
	FRDGTextureRef RdgOutput = GraphBuilder.CreateTexture(OutDesc, TEXT("Hillaire.SkyBackground"));
	AddSkyBackgroundPass(
		GraphBuilder, SceneColorSrv, RdgSceneDepth, ViewUniformBuffer,
		ToMatrix44f(InvProjMatrix), CameraPlanetLocalKm, ToMatrix44f(ViewToPlanetLocalRot),
		PrimarySunDirLocal, SunColorAttenuation,
		BottomRadiusKm, TopRadiusKm, ViewHeightKm, SunAngularRadiusRad,
		RdgSkyView, RdgTransmittance, ViewRect, RdgOutput);
	OutColor = RdgOutput;
	return true;
}

bool FHillaireLutManager::ShouldVisualizeDebug(
	int32 DebugMode,
	bool bHaveTransmittance,
	bool bHaveMultiScattering,
	bool bHaveSkyView,
	bool bHaveAerial,
	bool bHaveRayFrame)
{
	switch (DebugMode)
	{
	case 1: return bHaveTransmittance;
	case 2: return bHaveMultiScattering;
	case 3: return bHaveSkyView;
	case 4: return bHaveAerial;
	case 5: return true;
	case 6:
	case 7:
	case 8:
	case 9: return bHaveRayFrame;
	// Modes 10/11/21 sample their own LUTs in HillaireAtmosphereDebug.usf
	// (10: Transmittance, 11: SkyView, 21: SkyView + Transmittance + MS),
	// so the gate must require them, not just the ray frame.
	case 10: return bHaveRayFrame && bHaveTransmittance;
	case 11: return bHaveRayFrame && bHaveSkyView;
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
	case 17:
	case 18:
	case 19:
	case 20: return bHaveRayFrame;
	case 21: return bHaveRayFrame && bHaveTransmittance && bHaveMultiScattering;
	case 22: return bHaveRayFrame;
	// Modes 23-27 are not implemented by the debug shader ("disabled" there):
	// the gate must NOT claim them, or the pass would emit an uninitialized image.
	default: return false;
	}
}

namespace
{
	/** FASE-10 enqueue: fullscreen debug visualization over the view rect. */
	void AddDebugVisualizationPass(
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
		const FHillaireLutManager::FHillaireAerialViewInputs& RayFrame,
		const FVector3f& PrimarySunDirLocal,
		float ViewHeightKm,
		const FVector3f& StarCamRelativeKm,
		const FVector3f& CenterCamRelativeKmDbg,
		const FVector4f& PlanetQuatWS,
		FRDGTextureRef RdgOutput)
	{
		FHillaireAtmosphereDebugCS::FParameters* Params =
			GraphBuilder.AllocParameters<FHillaireAtmosphereDebugCS::FParameters>();
		Params->DebugMode = (uint32)DebugMode;
		// RDG validation fatals on UNSET SRV params (assigning nullptr does
		// NOT count as set), so every slot binds something valid. Slots the
		// selected mode never samples get harmless fallbacks: missing 2D
		// LUTs alias the scene-color SRV (same Texture2D<float4> type), a
		// missing volume gets a transient 1^3 texture, a missing depth gets
		// a transient 1x1 depth texture (mode 9 needs real depth; the gate
		// guarantees it). The gate guarantees the mode's OWN source is real.
		Params->TransmittanceLut = RdgTransmittance ? GraphBuilder.CreateSRV(RdgTransmittance) : SceneColorSrv;
		Params->MultiScatteringLut = RdgMultiScattering ? GraphBuilder.CreateSRV(RdgMultiScattering) : SceneColorSrv;
		Params->SkyViewLut = RdgSkyView ? GraphBuilder.CreateSRV(RdgSkyView) : SceneColorSrv;
		if (RdgAerialVolume)
		{
			Params->AerialVolume = GraphBuilder.CreateSRV(RdgAerialVolume);
		}
		else
		{
			// Produced fallback: a 1^3 volume with a clear value AND an explicit
			// clear pass, so RDG never sees an unwritten read dependency. Only
			// sampled by modes that the gate already rejected without a real
			// volume.
			FRDGTextureDesc FallbackDesc = FRDGTextureDesc::Create3D(
				FIntVector(1, 1, 1), PF_FloatRGBA, FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)),
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef FallbackVolume = GraphBuilder.CreateTexture(
				FallbackDesc, TEXT("Hillaire.DebugAerialFallback"));
			AddClearUAVPass(GraphBuilder,
				GraphBuilder.CreateUAV(FallbackVolume), FVector4(0.0f, 0.0f, 0.0f, 1.0f));
			Params->AerialVolume = GraphBuilder.CreateSRV(FallbackVolume);
		}
		if (RdgSceneDepth)
		{
			Params->SceneDepthInput = GraphBuilder.CreateSRV(RdgSceneDepth);
		}
		else
		{
			// Produced fallback: 1x1 R32F (bindable as Texture2D<float>)
			// with clear value AND explicit clear pass (reads as sky).
			// Same producer guarantee as above.
			FRDGTextureDesc FallbackDepthDesc = FRDGTextureDesc::Create2D(
				FIntPoint(1, 1), PF_R32_FLOAT, FClearValueBinding(FLinearColor::Black),
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef FallbackDepth = GraphBuilder.CreateTexture(
				FallbackDepthDesc, TEXT("Hillaire.DebugDepthFallback"));
			AddClearUAVPass(GraphBuilder,
				GraphBuilder.CreateUAV(FallbackDepth), 0.0f);
			Params->SceneDepthInput = GraphBuilder.CreateSRV(FallbackDepth);
		}
		Params->LinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		// Ray-debug frame (modes 6+): same matrices the production sky
		// composite consumes this frame.
		Params->InvProjMatrix = ToMatrix44f(RayFrame.InvProjMatrix);
		Params->ViewToPlanetLocalRot = ToMatrix44f(RayFrame.ViewToPlanetLocalRot);
		Params->CameraPlanetLocalKm = RayFrame.CameraPlanetLocalKm;
		Params->PrimarySunDirLocal = PrimarySunDirLocal;
		Params->ViewHeightKm = ViewHeightKm;
		Params->SkyDepthEpsilon = HillaireLimits::CompositeSkyDepthEpsilon;
		// World-frame ground truth (mode 20) and direct-march medium (mode 21).
		Params->StarCamRelativeKm = StarCamRelativeKm;
		Params->CenterCamRelativeKmDbg = CenterCamRelativeKmDbg;
		Params->PlanetQuatWS = PlanetQuatWS;
		Params->RayleighScatteringKm = FVector3f(
			(float)Profile.RayleighScatteringKm.X,
			(float)Profile.RayleighScatteringKm.Y,
			(float)Profile.RayleighScatteringKm.Z);
		Params->MieScatteringKm = FVector3f(
			(float)Profile.MieScatteringKm.X,
			(float)Profile.MieScatteringKm.Y,
			(float)Profile.MieScatteringKm.Z);
		Params->MieExtinctionKm = FVector3f(
			(float)Profile.MieExtinctionKm.X,
			(float)Profile.MieExtinctionKm.Y,
			(float)Profile.MieExtinctionKm.Z);
		Params->MieAbsorptionKm = FVector3f(
			(float)Profile.MieAbsorptionKm.X,
			(float)Profile.MieAbsorptionKm.Y,
			(float)Profile.MieAbsorptionKm.Z);
		Params->AbsorptionExtinctionKm = FVector3f(
			(float)Profile.AbsorptionExtinctionKm.X,
			(float)Profile.AbsorptionExtinctionKm.Y,
			(float)Profile.AbsorptionExtinctionKm.Z);
		Params->MiePhaseG = Profile.MiePhaseG;
		Params->RayMarchMinMaxSPP = FVector2f(
			HillaireLimits::SkyViewMarchMinSamples, HillaireLimits::SkyViewMarchMaxSamples);
		Params->PlanetRadiusOffsetKm = HillaireLimits::PlanetRadiusOffsetKm;
		Params->MultiScatteringLutRes = (float)HillaireLimits::MultiScatteringRes;
		Params->BottomRadiusKm = Profile.BottomRadiusKm;
		Params->TopRadiusKm = Profile.TopRadiusKm;
		Params->RayleighExpScale = Profile.RayleighExpScale;
		Params->MieExpScale = Profile.MieExpScale;
		Params->AbsorptionWidthKm = Profile.AbsorptionWidthKm;
		Params->AbsorptionLinear0 = Profile.AbsorptionLinear0;
		Params->AbsorptionConstant0 = Profile.AbsorptionConstant0;
		Params->AbsorptionLinear1 = Profile.AbsorptionLinear1;
		Params->AbsorptionConstant1 = Profile.AbsorptionConstant1;
		Params->ViewRectMin = FVector2f((float)ViewRect.Min.X, (float)ViewRect.Min.Y);
		Params->ViewRectSize = FVector2f((float)ViewRect.Width(), (float)ViewRect.Height());
		Params->DebugOutputUav = GraphBuilder.CreateUAV(RdgOutput);

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FHillaireAtmosphereDebugCS> Shader(ShaderMap);
		const FIntVector GroupCount(
			(ViewRect.Width() + HillaireLimits::CompositeThreadGroupX - 1) / HillaireLimits::CompositeThreadGroupX,
			(ViewRect.Height() + HillaireLimits::CompositeThreadGroupY - 1) / HillaireLimits::CompositeThreadGroupY,
			1);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Hillaire.DebugVisualization"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			Shader,
			Params,
			GroupCount);
	}
} // namespace

bool FHillaireLutManager::CompositeDebugVisualization(
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
	FRDGTextureRef& OutColor)
{
	OutColor = nullptr;
	if (!SceneColorSrv || ViewRect.IsEmpty())
	{
		return false;
	}
	if (!ShouldVisualizeDebug(
		DebugMode,
		RdgTransmittance != nullptr,
		RdgMultiScattering != nullptr,
		RdgSkyView != nullptr,
		RdgAerialVolume != nullptr,
		RdgSceneDepth != nullptr))
	{
		return false;
	}
	// Output clones the input scene-color desc (+UAV), like the production
	// composites: the pass must not alter pipeline color behavior by itself.
	FRDGTextureDesc OutDesc = SceneColorSrv->Desc.Texture->Desc;
	OutDesc.Flags |= TexCreate_UAV;
	OutDesc.ClearValue = FClearValueBinding::None;
	FRDGTextureRef RdgOutput = GraphBuilder.CreateTexture(OutDesc, TEXT("Hillaire.DebugVisualization"));
	AddDebugVisualizationPass(
		GraphBuilder, SceneColorSrv, ViewRect, DebugMode,
		RdgTransmittance, RdgMultiScattering, RdgSkyView, RdgAerialVolume,
		Profile, RdgSceneDepth, RayFrame, PrimarySunDirLocal, ViewHeightKm,
		StarCamRelativeKm, CenterCamRelativeKmDbg, PlanetQuatWS, RdgOutput);
	OutColor = RdgOutput;
	return true;
}
