#include "HillaireAtmosphereProfile.h"

#include "HillaireHash.h"
#include "HillaireLimits.h"

bool FHillaireAtmosphereProfile::IsValid(FString* OutError) const
{
	auto Fail = [&](const TCHAR* Msg) -> bool
	{
		if (OutError)
		{
			*OutError = Msg;
		}
		return false;
	};

	if (!(BottomRadiusKm > 0.0f))
	{
		return Fail(TEXT("BottomRadiusKm must be positive."));
	}
	if (!(TopRadiusKm > BottomRadiusKm))
	{
		return Fail(TEXT("TopRadiusKm must be greater than BottomRadiusKm."));
	}
	auto NonNegative3 = [](const FVector& V) -> bool
	{
		return V.X >= 0.0 && V.Y >= 0.0 && V.Z >= 0.0
			&& FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
	};
	if (!NonNegative3(RayleighScatteringKm))
	{
		return Fail(TEXT("RayleighScatteringKm must be finite and non-negative."));
	}
	if (!NonNegative3(MieScatteringKm) || !NonNegative3(MieExtinctionKm) || !NonNegative3(MieAbsorptionKm))
	{
		return Fail(TEXT("Mie sigma vectors must be finite and non-negative."));
	}
	if (!NonNegative3(AbsorptionExtinctionKm))
	{
		return Fail(TEXT("AbsorptionExtinctionKm must be finite and non-negative."));
	}
	if (!NonNegative3(GroundAlbedo) || !NonNegative3(SolarIrradiance))
	{
		return Fail(TEXT("GroundAlbedo and SolarIrradiance must be finite and non-negative."));
	}
	if (!(RayleighExpScale < 0.0f) || !(MieExpScale < 0.0f))
	{
		return Fail(TEXT("Density exp scales must be negative (density falls with height)."));
	}
	if (MiePhaseG <= -1.0f || MiePhaseG >= 1.0f)
	{
		return Fail(TEXT("MiePhaseG must be inside (-1, 1)."));
	}
	if (MuSMin < -1.0f || MuSMin > 1.0f)
	{
		return Fail(TEXT("MuSMin must be inside [-1, 1]."));
	}
	if (!(AbsorptionWidthKm > 0.0f))
	{
		return Fail(TEXT("AbsorptionWidthKm must be positive."));
	}
	return true;
}

uint64 FHillaireAtmosphereProfile::ComputeContentHash() const
{
	// Contract mirror of ComputeAtmosphereProfileHash: radii first (a radius
	// change re-parameterizes every LUT), then sigmas, albedo, phase, cutoff,
	// density terms, solar. Quanta mirror the reference choices.
	uint64 H = HillaireHash::OffsetBasis;
	H = HillaireHash::HashFloat(BottomRadiusKm, 1e-3f, H);
	H = HillaireHash::HashFloat(TopRadiusKm, 1e-3f, H);
	H = HillaireHash::HashFloat3((float)RayleighScatteringKm.X, (float)RayleighScatteringKm.Y, (float)RayleighScatteringKm.Z, 1e-6f, H);
	H = HillaireHash::HashFloat(RayleighExpScale, 1e-6f, H);
	H = HillaireHash::HashFloat3((float)MieScatteringKm.X, (float)MieScatteringKm.Y, (float)MieScatteringKm.Z, 1e-6f, H);
	H = HillaireHash::HashFloat3((float)MieExtinctionKm.X, (float)MieExtinctionKm.Y, (float)MieExtinctionKm.Z, 1e-6f, H);
	H = HillaireHash::HashFloat3((float)MieAbsorptionKm.X, (float)MieAbsorptionKm.Y, (float)MieAbsorptionKm.Z, 1e-6f, H);
	H = HillaireHash::HashFloat(MieExpScale, 1e-6f, H);
	H = HillaireHash::HashFloat(MiePhaseG, 1e-4f, H);
	H = HillaireHash::HashFloat3((float)AbsorptionExtinctionKm.X, (float)AbsorptionExtinctionKm.Y, (float)AbsorptionExtinctionKm.Z, 1e-6f, H);
	H = HillaireHash::HashFloat(AbsorptionWidthKm, 1e-3f, H);
	H = HillaireHash::HashFloat(AbsorptionLinear0, 1e-6f, H);
	H = HillaireHash::HashFloat(AbsorptionConstant0, 1e-6f, H);
	H = HillaireHash::HashFloat(AbsorptionLinear1, 1e-6f, H);
	H = HillaireHash::HashFloat(AbsorptionConstant1, 1e-6f, H);
	H = HillaireHash::HashFloat3((float)GroundAlbedo.X, (float)GroundAlbedo.Y, (float)GroundAlbedo.Z, 1e-4f, H);
	H = HillaireHash::HashFloat(MuSMin, 1e-5f, H);
	H = HillaireHash::HashFloat3((float)SolarIrradiance.X, (float)SolarIrradiance.Y, (float)SolarIrradiance.Z, 1e-5f, H);
	return H;
}

FHillaireAtmosphereProfile FHillaireAtmosphereProfile::MakeReferenceProfile()
{
	// Values transcribed from SetupEarthAtmosphere (km units). This is the
	// VALIDATED reference profile, not a hardcoded-Earth assumption: real
	// planets always supply their own profile; this exists for tests and as
	// a known-good authoring starting point.
	FHillaireAtmosphereProfile P;
	P.BottomRadiusKm = 6360.0f;
	P.TopRadiusKm = 6460.0f;
	P.RayleighScatteringKm = FVector(0.005802, 0.013558, 0.033100);
	P.RayleighExpScale = -1.0f / 8.0f;
	P.MieScatteringKm = FVector(0.003996, 0.003996, 0.003996);
	P.MieExtinctionKm = FVector(0.004440, 0.004440, 0.004440);
	P.MieAbsorptionKm = FVector(0.000444, 0.000444, 0.000444);
	P.MieExpScale = -1.0f / 1.2f;
	P.MiePhaseG = 0.8f;
	P.AbsorptionExtinctionKm = FVector(0.000650, 0.001881, 0.000085);
	P.AbsorptionWidthKm = 25.0f;
	P.AbsorptionLinear0 = 1.0f / 15.0f;
	P.AbsorptionConstant0 = -2.0f / 3.0f;
	P.AbsorptionLinear1 = -1.0f / 15.0f;
	P.AbsorptionConstant1 = 8.0f / 3.0f;
	P.GroundAlbedo = FVector::ZeroVector;
	P.MuSMin = FMath::Cos(PI * 120.0 / 180.0);
	P.SolarIrradiance = FVector(1.0, 1.0, 1.0);
	check(P.IsValid());
	return P;
}

bool FHillaireAtmosphereProfile::operator==(const FHillaireAtmosphereProfile& Other) const
{
	return BottomRadiusKm == Other.BottomRadiusKm
		&& TopRadiusKm == Other.TopRadiusKm
		&& RayleighScatteringKm.Equals(Other.RayleighScatteringKm)
		&& RayleighExpScale == Other.RayleighExpScale
		&& MieScatteringKm.Equals(Other.MieScatteringKm)
		&& MieExtinctionKm.Equals(Other.MieExtinctionKm)
		&& MieAbsorptionKm.Equals(Other.MieAbsorptionKm)
		&& MieExpScale == Other.MieExpScale
		&& MiePhaseG == Other.MiePhaseG
		&& AbsorptionExtinctionKm.Equals(Other.AbsorptionExtinctionKm)
		&& AbsorptionWidthKm == Other.AbsorptionWidthKm
		&& AbsorptionLinear0 == Other.AbsorptionLinear0
		&& AbsorptionConstant0 == Other.AbsorptionConstant0
		&& AbsorptionLinear1 == Other.AbsorptionLinear1
		&& AbsorptionConstant1 == Other.AbsorptionConstant1
		&& GroundAlbedo.Equals(Other.GroundAlbedo)
		&& MuSMin == Other.MuSMin
		&& SolarIrradiance.Equals(Other.SolarIrradiance);
}

float HillaireThicknessNormalizationFactor(
	float AtmosphereHeightKm)
{
	// K = 100 / T: the density is normalized to the ENVELOPE so the reference
	// density shape and the vertical optical depth are preserved on any size.
	const float T = FMath::Max(AtmosphereHeightKm, HillaireLimits::MinAtmosphereThicknessKm);
	return HillaireLimits::ReferenceAtmosphereThicknessKm / T;
}

FHillaireAtmosphereProfile HillaireBuildNormalizedProfile(
	const FHillaireAtmosphereProfile& BaseProfile,
	float GroundRadiusKm,
	float AtmosphereHeightKm)
{
	// ATMOS VOLUMETRIC ATMOSPHERE:
	// - AtmosphereBottom = PlanetReferenceRadius
	// - AtmosphereTop = PlanetReferenceRadius + AtmosphericThickness (~1.10x ground)
	// - Density distribution redistributed over the expanded volume (H_R = T/4, H_M = T/12)
	// - Vertical optical depth tau = sigma * H is preserved versus the reference profile
	const float Ground = FMath::Max(GroundRadiusKm, HillaireLimits::MinAtmosphereThicknessKm);
	const float TargetThicknessKm = Ground * HillaireLimits::PlanetaryAtmosphereThicknessRatio;
	const float EnvelopeT = FMath::Max(
		AtmosphereHeightKm > 0.0f ? AtmosphereHeightKm : TargetThicknessKm,
		HillaireLimits::MinAtmosphereThicknessKm);

	FHillaireAtmosphereProfile Out = BaseProfile;
	Out.BottomRadiusKm = Ground;
	Out.TopRadiusKm = Ground + EnvelopeT;

	// Volumetric scale heights: density occupies the expanded planetary volume
	const float HRayleighKm = EnvelopeT / HillaireLimits::RayleighVolumeScaleHeights;
	const float HMieKm = EnvelopeT / HillaireLimits::MieVolumeScaleHeights;

	Out.RayleighExpScale = -1.0f / HRayleighKm;
	Out.MieExpScale = -1.0f / HMieKm;

	// Optical depth preservation:
	// tau_R = sigma_R * H_R * (1 - exp(-T / H_R)) == Base.RayleighScatteringKm * Base.H_R
	const float BaseHRayleighKm = -1.0f / FMath::Min(BaseProfile.RayleighExpScale, -1e-6f);
	const float RayleighIntegralFraction = 1.0f - FMath::Exp(-HillaireLimits::RayleighVolumeScaleHeights);
	const float RayleighNormalization = BaseHRayleighKm / (HRayleighKm * RayleighIntegralFraction);

	Out.RayleighScatteringKm = BaseProfile.RayleighScatteringKm * RayleighNormalization;

	// Mie optical depth preservation:
	// tau_M = sigma_M * H_M * (1 - exp(-T / H_M)) == Base.MieExtinctionKm * Base.H_M
	const float BaseHMieKm = -1.0f / FMath::Min(BaseProfile.MieExpScale, -1e-6f);
	const float MieIntegralFraction = 1.0f - FMath::Exp(-HillaireLimits::MieVolumeScaleHeights);
	const float MieNormalization = BaseHMieKm / (HMieKm * MieIntegralFraction);

	Out.MieScatteringKm = BaseProfile.MieScatteringKm * MieNormalization;
	Out.MieExtinctionKm = BaseProfile.MieExtinctionKm * MieNormalization;
	Out.MieAbsorptionKm = Out.MieExtinctionKm - Out.MieScatteringKm;

	// Ozone tent layer: spans 10% to 40% of the expanded volume, peaking at 25%
	const float OzoneNormalization = HillaireThicknessNormalizationFactor(EnvelopeT);
	Out.AbsorptionExtinctionKm = BaseProfile.AbsorptionExtinctionKm * OzoneNormalization;
	Out.AbsorptionWidthKm = 0.25f * EnvelopeT;
	Out.AbsorptionLinear0 = 1.0f / (0.15f * EnvelopeT);
	Out.AbsorptionConstant0 = -2.0f / 3.0f;
	Out.AbsorptionLinear1 = -1.0f / (0.15f * EnvelopeT);
	Out.AbsorptionConstant1 = 8.0f / 3.0f;

	// Untouched by construction: GroundAlbedo, MiePhaseG, MuSMin,
	// SolarIrradiance, AbsorptionConstant0/1.
	check(Out.IsValid());
	return Out;
}

FHillaireAtmosphereProfile HillaireBuildPlanetaryProfile(
	const FHillaireAtmosphereProfile& BaseProfile,
	float GroundRadiusKm,
	float TerrainHeadroomKm)
{
	(void)TerrainHeadroomKm; // LYTHOS does not exist yet; persistent planetary sphere
	const float TargetThicknessKm = GroundRadiusKm * HillaireLimits::PlanetaryAtmosphereThicknessRatio;
	return HillaireBuildNormalizedProfile(BaseProfile, GroundRadiusKm, TargetThicknessKm);
}
