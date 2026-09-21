#include "HillaireShaders.h"

// Phase 1: permutation domains stay minimal (no MULTISCATAPPROX/FASTSKY-style
// explosion). Permutation dimensions for quality/feature branches arrive with
// the passes that need them; keep the DDC/PSO surface flat until then.
// Phase 2A: P0 moved from raster (MainPS) to per-texel compute (MainCS); the
// math is unchanged (documented in HillaireShaders.h).

void HillaireFillAtmosphereUniforms(
	FHillaireAtmosphereMediumParams& Out,
	const FHillaireAtmosphereProfile& Profile)
{
	Out.BottomRadiusKm = Profile.BottomRadiusKm;
	Out.TopRadiusKm = Profile.TopRadiusKm;
	Out.RayleighExpScale = Profile.RayleighExpScale;
	Out.MieExpScale = Profile.MieExpScale;
	Out.RayleighScatteringKm = FVector3f((float)Profile.RayleighScatteringKm.X, (float)Profile.RayleighScatteringKm.Y, (float)Profile.RayleighScatteringKm.Z);
	Out.AbsorptionWidthKm = Profile.AbsorptionWidthKm;
	Out.MieScatteringKm = FVector3f((float)Profile.MieScatteringKm.X, (float)Profile.MieScatteringKm.Y, (float)Profile.MieScatteringKm.Z);
	Out.AbsorptionLinear0 = Profile.AbsorptionLinear0;
	Out.MieExtinctionKm = FVector3f((float)Profile.MieExtinctionKm.X, (float)Profile.MieExtinctionKm.Y, (float)Profile.MieExtinctionKm.Z);
	Out.AbsorptionConstant0 = Profile.AbsorptionConstant0;
	Out.MieAbsorptionKm = FVector3f((float)Profile.MieAbsorptionKm.X, (float)Profile.MieAbsorptionKm.Y, (float)Profile.MieAbsorptionKm.Z);
	Out.AbsorptionLinear1 = Profile.AbsorptionLinear1;
	Out.AbsorptionExtinctionKm = FVector3f((float)Profile.AbsorptionExtinctionKm.X, (float)Profile.AbsorptionExtinctionKm.Y, (float)Profile.AbsorptionExtinctionKm.Z);
	Out.AbsorptionConstant1 = Profile.AbsorptionConstant1;
}

IMPLEMENT_GLOBAL_SHADER(FHillaireTransmittanceLutCS,
	"/Plugin/HillaireAtmosphere/HillaireTransmittanceLut.usf", "MainCS", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FHillaireMultiScatteringCS,
	"/Plugin/HillaireAtmosphere/HillaireMultiScattering.usf", "MainCS", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FHillaireSkyViewLutCS,
	"/Plugin/HillaireAtmosphere/HillaireSkyViewLut.usf", "MainCS", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FHillaireAerialPerspectiveCS,
	"/Plugin/HillaireAtmosphere/HillaireAerialPerspective.usf", "MainCS", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FHillaireAerialCompositeCS,
	"/Plugin/HillaireAtmosphere/HillaireAerialComposite.usf", "MainCS", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FHillaireSkyBackgroundCS,
	"/Plugin/HillaireAtmosphere/HillaireSkyBackground.usf", "MainCS", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FHillaireAtmosphereDebugCS,
	"/Plugin/HillaireAtmosphere/HillaireAtmosphereDebug.usf", "MainCS", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FHillaireDebugLutPS,
	"/Plugin/HillaireAtmosphere/HillaireDebugLut.usf", "MainPS", SF_Pixel);

bool FHillaireTransmittanceLutCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return true;
}

bool FHillaireMultiScatteringCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return true;
}

bool FHillaireSkyViewLutCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return true;
}

bool FHillaireAerialPerspectiveCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return true;
}

bool FHillaireAerialCompositeCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return true;
}

bool FHillaireSkyBackgroundCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return true;
}

bool FHillaireAtmosphereDebugCS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return true;
}

bool FHillaireDebugLutPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	// Test-only viewer: kept compilable everywhere in Phase 1; shipping
	// exclusion (test-only permutation gating) arrives with the debug pass.
	return true;
}
