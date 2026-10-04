#include "LYTHOS/LythosCoordinates.h"
#include "Planet/PlanetProfile.h"

FVector LythosCoordinates::FaceToDirection(int32 FaceIndex, float U, float V)
{
	const float X = U * 2.0f - 1.0f;
	const float Y = V * 2.0f - 1.0f;

	switch (FaceIndex)
	{
	case 0:  return FVector(1.0f,  Y, -X);
	case 1:  return FVector(-1.0f, Y,  X);
	case 2:  return FVector(X,  1.0f, -Y);
	case 3:  return FVector(X, -1.0f,  Y);
	case 4:  return FVector(X,  Y,  1.0f);
	case 5:  return FVector(X, -Y, -1.0f);
	default: return FVector::UpVector;
	}
}

bool LythosCoordinates::DirectionToFace(const FVector& Direction, int32 FaceIndex, float& OutU, float& OutV)
{
	const FVector D = Direction.GetSafeNormal();

	float FaceX = 0.f, FaceY = 0.f;
	float FaceAxis = 0.f;

	switch (FaceIndex)
	{
	case 0:  FaceAxis = D.X;  FaceX = -D.Z; FaceY =  D.Y; break;
	case 1:  FaceAxis = -D.X; FaceX =  D.Z; FaceY =  D.Y; break;
	case 2:  FaceAxis = D.Y;  FaceX =  D.X; FaceY = -D.Z; break;
	case 3:  FaceAxis = -D.Y; FaceX =  D.X; FaceY =  D.Z; break;
	case 4:  FaceAxis = D.Z;  FaceX =  D.X; FaceY =  D.Y; break;
	case 5:  FaceAxis = -D.Z; FaceX =  D.X; FaceY = -D.Y; break;
	default: return false;
	}

	if (FaceAxis < 0.5f)
	{
		return false;
	}

	OutU = (FaceX + 1.0f) * 0.5f;
	OutV = (FaceY + 1.0f) * 0.5f;
	return true;
}

int32 LythosCoordinates::DirectionToNearestFace(const FVector& Direction, float& OutU, float& OutV)
{
	const FVector D = Direction.GetSafeNormal();

	float AbsX = FMath::Abs(D.X);
	float AbsY = FMath::Abs(D.Y);
	float AbsZ = FMath::Abs(D.Z);

	int32 Face;
	float U, V;

	if (AbsX >= AbsY && AbsX >= AbsZ)
	{
		if (D.X > 0)
		{
			Face = 0; U = (-D.Z + 1.f) * 0.5f; V = (D.Y + 1.f) * 0.5f;
		}
		else
		{
			Face = 1; U = ( D.Z + 1.f) * 0.5f; V = (D.Y + 1.f) * 0.5f;
		}
	}
	else if (AbsY >= AbsX && AbsY >= AbsZ)
	{
		if (D.Y > 0)
		{
			Face = 2; U = ( D.X + 1.f) * 0.5f; V = (-D.Z + 1.f) * 0.5f;
		}
		else
		{
			Face = 3; U = ( D.X + 1.f) * 0.5f; V = ( D.Z + 1.f) * 0.5f;
		}
	}
	else
	{
		if (D.Z > 0)
		{
			Face = 4; U = ( D.X + 1.f) * 0.5f; V = ( D.Y + 1.f) * 0.5f;
		}
		else
		{
			Face = 5; U = ( D.X + 1.f) * 0.5f; V = (-D.Y + 1.f) * 0.5f;
		}
	}

	OutU = U;
	OutV = V;
	return Face;
}

float LythosCoordinates::LatitudeDegrees(const FVector& Direction)
{
	const FVector D = Direction.GetSafeNormal();
	return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(D.Z, -1.f, 1.f)));
}

float LythosCoordinates::LongitudeDegrees(const FVector& Direction)
{
	const FVector D = Direction.GetSafeNormal();
	float Lon = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
	if (Lon < 0.f)
	{
		Lon += 360.f;
	}
	return Lon;
}

float LythosCoordinates::NormalisedLatitude(const FVector& Direction)
{
	const FVector D = Direction.GetSafeNormal();
	return FMath::Abs(D.Z);
}

FLythosRegionCoord LythosCoordinates::DirectionToRegion(
	const FVector& Direction,
	int32 RegionGridPerSide,
	int32 RegionLOD)
{
	FLythosRegionCoord Region;
	Region.RegionLOD = RegionLOD;
	Region.GridResolution = FMath::Max(2, RegionGridPerSide >> RegionLOD);

	float U, V;
	Region.FaceIndex = DirectionToNearestFace(Direction, U, V);

	const int32 Grid = Region.GridResolution;
	Region.RegionX = FMath::Clamp(FMath::FloorToInt(U * Grid), 0, Grid - 1);
	Region.RegionY = FMath::Clamp(FMath::FloorToInt(V * Grid), 0, Grid - 1);

	return Region;
}

FVector LythosCoordinates::RegionCenterDirection(const FLythosRegionCoord& Region)
{
	const int32 Grid = FMath::Max(1, Region.GridResolution);

	const float U = (static_cast<float>(Region.RegionX) + 0.5f) / static_cast<float>(Grid);
	const float V = (static_cast<float>(Region.RegionY) + 0.5f) / static_cast<float>(Grid);

	return FaceToDirection(Region.FaceIndex, U, V).GetSafeNormal();
}

void LythosCoordinates::SampleRegionDirections(
	const FLythosRegionCoord& Region,
	TArray<FVector>& OutDirections)
{
	const int32 Grid = FMath::Max(2, Region.GridResolution);

	OutDirections.Empty(Grid * Grid);

	const float Inv = (Grid > 1) ? (1.0f / static_cast<float>(Grid - 1)) : 1.f;

	for (int32 Y = 0; Y < Grid; ++Y)
	{
		const float V = static_cast<float>(Y) * Inv;

		for (int32 X = 0; X < Grid; ++X)
		{
			const float U = static_cast<float>(X) * Inv;
			const float CellU = (static_cast<float>(Region.RegionX) + U) / static_cast<float>(Grid);
			const float CellV = (static_cast<float>(Region.RegionY) + V) / static_cast<float>(Grid);

			OutDirections.Add(FaceToDirection(Region.FaceIndex, CellU, CellV).GetSafeNormal());
		}
	}
}

FVector LythosCoordinates::LocalToWorld(const FLythosPlanetContext& Context, const FVector& LocalPosition)
{
	return Context.WorldPosition + LocalPosition;
}

FVector LythosCoordinates::WorldToLocalDirection(const FLythosPlanetContext& Context, const FVector& WorldPosition)
{
	const FVector ToPoint = WorldPosition - Context.WorldPosition;
	const double DistSq = ToPoint.SizeSquared();

	if (DistSq < KINDA_SMALL_NUMBER)
	{
		return FVector::ForwardVector;
	}

	return ToPoint / FMath::Sqrt(DistSq);
}
