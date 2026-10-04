#include "LYTHOS/LythosDebugVisualization.h"
#include "LYTHOS/LythosCoordinates.h"
#include "LYTHOS/LythosMacroTerrain.h"
#include "Planet/PlanetTerrainGenerator.h"

FColor LythosDebugVisualization::LandformToDebugColor(ELythosLandform Landform)
{
	switch (Landform)
	{
	case ELythosLandform::Ocean:    return FColor( 30,  60, 180, 255); // deep blue
	case ELythosLandform::Coast:    return FColor( 80, 160, 255, 255); // light blue
	case ELythosLandform::Lowland:  return FColor( 60, 150,  60, 255); // green
	case ELythosLandform::Plains:   return FColor(140, 190,  80, 255); // yellow-green
	case ELythosLandform::Hills:    return FColor(160, 130,  70, 255); // brown
	case ELythosLandform::Highlands:return FColor(180, 160, 110, 255); // tan
	case ELythosLandform::Mountain: return FColor(210, 200, 190, 255); // gray-white
	case ELythosLandform::Basin:    return FColor( 80,  70,  50, 255); // dark brown
	default:                        return FColor(128, 128, 128, 255); // gray
	}
}

FLythosDebugMesh LythosDebugVisualization::BuildRegionDebugMesh(
	const FLythosRegionData& Region,
	const FLythosPlanetContext& Context)
{
	FLythosDebugMesh Mesh;

	if (!Region.IsValid())
	{
		return Mesh;
	}

	const int32 Grid = Region.GridSize;
	const int32 VertCount = Grid * Grid;

	Mesh.Vertices.Empty(VertCount);
	Mesh.Normals.Empty(VertCount);
	Mesh.UVs.Empty(VertCount);
	Mesh.VertexColors.Empty(VertCount);
	Mesh.Triangles.Empty((Grid - 1) * (Grid - 1) * 6);

	const float Radius = Context.PlanetRadius;
	const float TerrainHeight = FMath::Max(Context.TerrainHeight, KINDA_SMALL_NUMBER);

	for (int32 I = 0; I < Region.Samples.Num(); ++I)
	{
		const FLythoTerrainSample& S = Region.Samples[I];

		const FVector Pos = S.Direction * (Radius + S.Elevation);

		Mesh.Vertices.Add(Pos);
		Mesh.Normals.Add(S.Normal);

		const float U = (I % Grid) / static_cast<float>(Grid - 1);
		const float V = (I / Grid) / static_cast<float>(Grid - 1);
		Mesh.UVs.Add(FVector2D(U, V));

		Mesh.VertexColors.Add(LandformToDebugColor(S.Landform));
	}

	for (int32 Y = 0; Y < Grid - 1; ++Y)
	{
		for (int32 X = 0; X < Grid - 1; ++X)
		{
			const int32 A = Y * Grid + X;
			const int32 B = A + 1;
			const int32 C = A + Grid;
			const int32 D = C + 1;

			const int32 Base = Mesh.Triangles.Num();

			Mesh.Triangles.Add(A);
			Mesh.Triangles.Add(C);
			Mesh.Triangles.Add(B);

			Mesh.Triangles.Add(B);
			Mesh.Triangles.Add(C);
			Mesh.Triangles.Add(D);
		}
	}

	return Mesh;
}

FLythosDebugMesh LythosDebugVisualization::BuildDirectionDebugMesh(
	const FLythosPlanetContext& Context,
	const TArray<FVector>& Directions)
{
	FLythosDebugMesh Mesh;

	if (Directions.IsEmpty())
	{
		return Mesh;
	}

	// We don't have region structure, so treat as a single strip.
	Mesh.Vertices.Empty(Directions.Num());
	Mesh.Normals.Empty(Directions.Num());
	Mesh.UVs.Empty(Directions.Num());
	Mesh.VertexColors.Empty(Directions.Num());

	for (int32 I = 0; I < Directions.Num(); ++I)
	{
		const FLythoTerrainSample S = FLythosMacroTerrain::Evaluate(Context, Directions[I]);

		const FVector Pos = S.Direction * (Context.PlanetRadius + S.Elevation);

		Mesh.Vertices.Add(Pos);
		Mesh.Normals.Add(S.Normal);
		Mesh.UVs.Add(FVector2D(0.f, 0.f));
		Mesh.VertexColors.Add(LandformToDebugColor(S.Landform));
	}

	return Mesh;
}
