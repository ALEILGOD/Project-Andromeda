#include "HillaireTestFunctions.h"

FString FHillaireTestSnapshot::ToString() const
{
	return FString::Printf(
		TEXT("Scenario=%s;CamPos=%.6f,%.6f,%.6f;CamRot=%.6f,%.6f,%.6f;Planets=%d;Lights=%d;SnapHash=%llu;ProfileHash=%llu"),
		*ScenarioName.ToString(),
		CameraPositionCm.X, CameraPositionCm.Y, CameraPositionCm.Z,
		CameraRotation.Pitch, CameraRotation.Yaw, CameraRotation.Roll,
		PlanetCount, EnabledLightCount,
		SnapshotHash, ProfileHash);
}

bool FHillaireTestSnapshot::FromString(const FString& Str)
{
	TMap<FString, FString> Pairs;
	TArray<FString> Fields;
	Str.ParseIntoArray(Fields, TEXT(";"));
	for (const FString& F : Fields)
	{
		FString Key, Value;
		if (F.Split(TEXT("="), &Key, &Value))
		{
			Pairs.Add(Key, Value);
		}
	}
	const FString* PScenario = Pairs.Find(TEXT("Scenario"));
	const FString* PCamPos = Pairs.Find(TEXT("CamPos"));
	const FString* PCamRot = Pairs.Find(TEXT("CamRot"));
	const FString* PPlanets = Pairs.Find(TEXT("Planets"));
	const FString* PLights = Pairs.Find(TEXT("Lights"));
	const FString* PSnap = Pairs.Find(TEXT("SnapHash"));
	const FString* PProfile = Pairs.Find(TEXT("ProfileHash"));
	if (!PScenario || !PCamPos || !PCamRot || !PPlanets || !PLights || !PSnap || !PProfile)
	{
		return false;
	}
	ScenarioName = FName(**PScenario);
	TArray<FString> Pos, Rot;
	PCamPos->ParseIntoArray(Pos, TEXT(","));
	PCamRot->ParseIntoArray(Rot, TEXT(","));
	if (Pos.Num() != 3 || Rot.Num() != 3)
	{
		return false;
	}
	CameraPositionCm = FVector(FCString::Atod(*Pos[0]), FCString::Atod(*Pos[1]), FCString::Atod(*Pos[2]));
	CameraRotation = FRotator(FCString::Atod(*Rot[0]), FCString::Atod(*Rot[1]), FCString::Atod(*Rot[2]));
	PlanetCount = FCString::Atoi(**PPlanets);
	EnabledLightCount = FCString::Atoi(**PLights);
	SnapshotHash = FCString::Strtoui64(**PSnap, nullptr, 10);
	ProfileHash = FCString::Strtoui64(**PProfile, nullptr, 10);
	return true;
}

bool FHillaireTestSnapshot::operator==(const FHillaireTestSnapshot& Other) const
{
	return ScenarioName == Other.ScenarioName
		&& CameraPositionCm.Equals(Other.CameraPositionCm, 1e-6)
		&& CameraRotation.Equals(Other.CameraRotation, 1e-6)
		&& PlanetCount == Other.PlanetCount
		&& EnabledLightCount == Other.EnabledLightCount
		&& SnapshotHash == Other.SnapshotHash
		&& ProfileHash == Other.ProfileHash;
}

TArray<FHillaireScenarioDef> UHillaireAtmosphereTestLibrary::GetScenarioTable()
{
	// Core table. Lab names preserved; heights are illustrative until the
	// demo milestone wires the test map (same numbers as the lab demo then).
	TArray<FHillaireScenarioDef> Table;
	auto Add = [&](const TCHAR* Name, const TCHAR* Desc, float HeightKm, std::initializer_list<int32> Lights)
	{
		FHillaireScenarioDef D;
		D.Name = FName(Name);
		D.Description = Desc;
		D.CameraHeightKm = HeightKm;
		for (int32 L : Lights)
		{
			D.EnabledLightIndices.Add(L);
		}
		Table.Add(D);
	};
	Add(TEXT("Surface"), TEXT("Ground level, sun high."), 0.02f, { 0 });
	Add(TEXT("LowAtm"), TEXT("Inside lower atmosphere."), 10.0f, { 0 });
	Add(TEXT("HighOrbit"), TEXT("Outside, planet disc + limb."), 2000.0f, { 0 });
	Add(TEXT("Space"), TEXT("Deep space, governing = nearest."), 20000.0f, { 0 });
	Add(TEXT("SunFacing"), TEXT("Looking at the primary disk."), 100.0f, { 0 });
	Add(TEXT("SunBehind"), TEXT("Looking away from the primary."), 100.0f, { 0 });
	Add(TEXT("MultiAB"), TEXT("Two-light volumetric sum."), 100.0f, { 0, 1 });
	return Table;
}

bool UHillaireAtmosphereTestLibrary::FindScenario(const FName& Name, FHillaireScenarioDef& OutDef)
{
	for (const FHillaireScenarioDef& D : GetScenarioTable())
	{
		if (D.Name == Name)
		{
			OutDef = D;
			return true;
		}
	}
	return false;
}
