#include "HillaireLightSource.h"

#include "HillaireHash.h"
#include "HillaireLimits.h"
#include "HillaireUnits.h"

uint64 FHillaireLightSource::ComputeContentHash() const
{
	uint64 H = HillaireHash::OffsetBasis;
	H = HillaireHash::HashBytes(&LightId, sizeof(LightId), H);
	H = HillaireHash::HashFloat(bEnabled ? 1.0f : 0.0f, 1.0f, H);
	H = HillaireHash::HashFloat(bDirectional ? 1.0f : 0.0f, 1.0f, H);
	H = HillaireHash::HashVector(WorldPositionCm, HillaireLimits::CmPerKm * 1e-3, H);
	H = HillaireHash::HashVector(WorldDirectionToLight, 1e-6, H);
	H = HillaireHash::HashFloat3(Color.R, Color.G, Color.B, 1e-6f, H);
	H = HillaireHash::HashFloat(Intensity, 1e-4f, H);
	H = HillaireHash::HashFloat(AngularRadiusRad, 1e-7f, H);
	H = HillaireHash::HashFloat(bDrawDisk ? 1.0f : 0.0f, 1.0f, H);
	return H;
}

bool FHillaireLightSource::operator==(const FHillaireLightSource& Other) const
{
	return LightId == Other.LightId
		&& LightName == Other.LightName
		&& bEnabled == Other.bEnabled
		&& bDirectional == Other.bDirectional
		&& WorldPositionCm.Equals(Other.WorldPositionCm)
		&& WorldDirectionToLight.Equals(Other.WorldDirectionToLight)
		&& Color == Other.Color
		&& Intensity == Other.Intensity
		&& AngularRadiusRad == Other.AngularRadiusRad
		&& bDrawDisk == Other.bDrawDisk;
}

bool FHillaireResolvedLight::operator==(const FHillaireResolvedLight& Other) const
{
	return LightDirLocal.Equals(Other.LightDirLocal)
		&& ColorAttenuation.Equals(Other.ColorAttenuation)
		&& AngularRadiusRad == Other.AngularRadiusRad
		&& bDrawDisk == Other.bDrawDisk
		&& LightId == Other.LightId;
}

static FORCEINLINE FVector3f RotateByConjugate(const FQuat& Q, const FVector3f& V)
{
	// v' = v + 2*cross(q.xyz, cross(q.xyz,v) + w*v) with conjugated q.
	// Mirrors PlanetRotateVec + the HLSL QuatRotate exactly.
	const FQuat Qc(-Q.X, -Q.Y, -Q.Z, Q.W);
	const FVector3f Qv((float)Qc.X, (float)Qc.Y, (float)Qc.Z);
	const float W = (float)Qc.W;
	// Inner = cross(q.xyz, v) + w*v; Out = v + 2*cross(q.xyz, inner).
	const FVector3f Inner = FVector3f::CrossProduct(Qv, V) + FVector3f(W * V.X, W * V.Y, W * V.Z);
	const FVector3f Outer = FVector3f::CrossProduct(Qv, Inner);
	return V + Outer * 2.0f;
}

FHillaireResolvedLight HillaireResolveLightForPlanet(
	const FHillaireLightSource& Light,
	const FVector3f& PlanetCenterCamRelativeKm,
	const FVector3f& LightPosCamRelativeKm,
	const FQuat& PlanetRotation)
{
	FHillaireResolvedLight Out;
	Out.LightId = Light.LightId;

	if (!Light.bEnabled)
	{
		// Reference: disabled -> zero attenuation, no disk (dir left zero).
		return Out;
	}

	if (Light.bDirectional)
	{
		const FVector D = Light.WorldDirectionToLight.GetSafeNormal();
		const FVector3f Df((float)D.X, (float)D.Y, (float)D.Z);
		Out.LightDirLocal = RotateByConjugate(PlanetRotation, Df);
		Out.ColorAttenuation = FVector3f(
			Light.Color.R * Light.Intensity,
			Light.Color.G * Light.Intensity,
			Light.Color.B * Light.Intensity);
		Out.AngularRadiusRad = Light.AngularRadiusRad;
		Out.bDrawDisk = Light.bDrawDisk;
		return Out;
	}

	// Point light: direction from planet CENTER (Case B), 1/d^2 in km.
	const FVector3f Delta = LightPosCamRelativeKm - PlanetCenterCamRelativeKm;
	const float DistSq = Delta.SizeSquared();
	const float Dist = FMath::Sqrt(DistSq);
	if (Dist < HillaireLimits::PointLightMinDistanceKm)
	{
		return Out;
	}
	const FVector3f WorldDir = Delta / Dist;
	Out.LightDirLocal = RotateByConjugate(PlanetRotation, WorldDir);
	const float Atten = Light.Intensity / DistSq;
	Out.ColorAttenuation = FVector3f(
		Light.Color.R * Atten,
		Light.Color.G * Atten,
		Light.Color.B * Atten);
	// Point lights never draw a stellar disk (local sources).
	Out.AngularRadiusRad = 0.0f;
	Out.bDrawDisk = false;
	return Out;
}

FHillaireCompactedLights HillaireCompactLightsForPlanet(
	const TArray<FHillaireLightSource>& Lights,
	const FVector& ViewOriginCm,
	const FVector3f& PlanetCenterCamRelativeKm,
	const FQuat& PlanetRotation)
{
	FHillaireCompactedLights Out;

	// Single-primary rule, verbatim from UseSinglePrimaryFastPath: valid ONLY
	// when registry slot 0 is enabled+directional and every other is disabled.
	// Slot-0 IDENTITY matters (an A-off/B-on swap must drop the fast path).
	bool bFast = false;
	if (Lights.Num() > 0 && Lights[0].bEnabled && Lights[0].bDirectional)
	{
		bFast = true;
		for (int32 i = 1; i < Lights.Num(); ++i)
		{
			if (Lights[i].bEnabled)
			{
				bFast = false;
				break;
			}
		}
	}

	for (const FHillaireLightSource& L : Lights)
	{
		if (!L.bEnabled)
		{
			continue;
		}
		if (Out.Count >= HILLAIRE_MAX_ATMOSPHERE_LIGHTS)
		{
			break;
		}
		const FVector3f LightRelKm = HillaireUnits::WorldCmToCameraRelativeKm(L.WorldPositionCm, ViewOriginCm);
		Out.Lights[Out.Count] = HillaireResolveLightForPlanet(L, PlanetCenterCamRelativeKm, LightRelKm, PlanetRotation);
		++Out.Count;
	}
	// Tail stays zero-filled from construction.
	Out.bSinglePrimary = bFast && (Out.Count == 1);
	return Out;
}

int32 HillaireEffectiveLightCount(const TArray<FHillaireLightSource>& Lights)
{
	int32 N = 0;
	for (const FHillaireLightSource& L : Lights)
	{
		if (L.bEnabled)
		{
			++N;
		}
	}
	return N;
}
