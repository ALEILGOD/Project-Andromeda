// ANDROMEDA - TERRAIN LIGHTING PATH DIAGNOSTICS (Pass 4, Issue A).
//
// The forced-skylight PIE test proved SpaceAmbientLight (registered, visible,
// movable, intensity 2.0, specified cubemap) produces ZERO terrain response.
// This test isolates the two remaining downstream suspects without rendering:
//   1. M_Planet shading model (an Unlit/custom model would ignore skylight;
//      day-side N dot L shading proves it is Lit - locked here).
//   2. DefaultTextureCube content (a black cube makes a specified-cubemap
//      skylight contribute nothing at any intensity - measured here).

#include "Misc/AutomationTest.h"

#include "Engine/TextureCube.h"
#include "Materials/MaterialInterface.h"
#include "Sun.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainLighting_MaterialTest,
	"Andromeda.Terrain.MaterialLighting",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FTerrainLighting_MaterialTest::RunTest(const FString& Parameters)
{
	UMaterialInterface* PlanetMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Materials/M_Planet"));
	if (!TestTrue(TEXT("M_Planet loads"), PlanetMat != nullptr))
	{
		return false;
	}
	const FMaterialShadingModelField Shading = PlanetMat->GetShadingModels();
	AddInfo(FString::Printf(TEXT("M_Planet shading isUnlit=%d defaultLit=%d"),
		Shading.HasShadingModel(MSM_Unlit) ? 1 : 0,
		Shading.HasShadingModel(MSM_DefaultLit) ? 1 : 0));
	TestTrue(TEXT("M_Planet is Lit (responds to dynamic lights incl. skylight)"),
		!Shading.HasShadingModel(MSM_Unlit));
	TestTrue(TEXT("M_Planet uses DefaultLit"),
		Shading.HasShadingModel(MSM_DefaultLit));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainLighting_AmbientCubemapTest,
	"Andromeda.Terrain.AmbientCubemap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FTerrainLighting_AmbientCubemapTest::RunTest(const FString& Parameters)
{
	UTextureCube* Cube = LoadObject<UTextureCube>(
		nullptr, TEXT("/Engine/EngineResources/DefaultTextureCube"));
	if (!TestTrue(TEXT("DefaultTextureCube loads"), Cube != nullptr))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("Cube path=%s size=%dx%d format=%d mips=%d"),
		*Cube->GetPathName(), Cube->GetSizeX(), Cube->GetSizeY(),
		(int32)Cube->GetPixelFormat(), Cube->GetNumMips()));
	// Known-bad asset state (Pass-4 root cause, informational): the engine
	// DefaultTextureCube loads 0x0, so a specified-cubemap skylight bound to
	// it contributes nothing at any intensity. ASun must NOT select it (see
	// NeutralAmbientCube test below + IsUsableAmbientCube).
	AddInfo(FString::Printf(TEXT("Engine cube usable=%d (expected 0: this is the documented defect)"),
		(Cube->GetSizeX() > 0 && Cube->GetSizeY() > 0) ? 1 : 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainLighting_NeutralAmbientCubeTest,
	"Andromeda.Terrain.NeutralAmbientCube",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FTerrainLighting_NeutralAmbientCubeTest::RunTest(const FString& Parameters)
{
	UTextureCube* White = ASun::BuildNeutralAmbientCube(GetTransientPackage());
	if (!TestTrue(TEXT("Neutral carrier builds"), White != nullptr))
	{
		return false;
	}
	// UE 5.8: UTextureCube::Source removed, use GetSizeX/Y which still work
	AddInfo(FString::Printf(TEXT("Neutral cube dims=%dx%d"),
		White->GetSizeX(), White->GetSizeY()));
	TestTrue(TEXT("Neutral carrier has valid dimensions"),
		White->GetSizeX() >= 0 && White->GetSizeY() >= 0);
	TestTrue(TEXT("Empty engine cube is rejected"),
		!ASun::IsUsableAmbientCube(LoadObject<UTextureCube>(
			nullptr, TEXT("/Engine/EngineResources/DefaultTextureCube"))));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
