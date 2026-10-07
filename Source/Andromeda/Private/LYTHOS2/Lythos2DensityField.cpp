#include "LYTHOS2/Lythos2DensityField.h"

namespace
{
    uint64 LythosMix64(uint64 X)
    {
        X += 0x9E3779B97F4A7C15ULL;
        X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ULL;
        X = (X ^ (X >> 27)) * 0x94D049BB133111EBULL;
        return X ^ (X >> 31);
    }

    float LythosHashUnit(int64 Seed, uint64 Channel)
    {
        const uint64 H = LythosMix64(static_cast<uint64>(Seed) ^ (Channel * 0xD1B54A32D192ED03ULL));
        return static_cast<float>(H & 0xFFFFFFULL) / static_cast<float>(0xFFFFFF);
    }

    FVector LythosSeedOffset(int64 Seed, uint64 Channel)
    {
        const uint64 H = LythosMix64(static_cast<uint64>(Seed) + Channel * 0x9E3779B97F4A7C15ULL);
        const float A = static_cast<float>((H >> 0) & 0x3FFFULL) / 16383.0f * 2.0f - 1.0f;
        const float B = static_cast<float>((H >> 16) & 0x3FFFULL) / 16383.0f * 2.0f - 1.0f;
        const float C = static_cast<float>((H >> 32) & 0x3FFFULL) / 16383.0f * 2.0f - 1.0f;
        return FVector(A, B, C) * 96.0f;
    }

    float LythosFbm(const FVector& P, int32 Octaves, float Lacunarity, float Gain)
    {
        float Amplitude = 1.0f;
        float Frequency = 1.0f;
        float Sum = 0.0f;
        float Norm = 0.0f;
        for (int32 i = 0; i < Octaves; ++i)
        {
            Sum += Amplitude * FMath::PerlinNoise3D(P * Frequency);
            Norm += Amplitude;
            Amplitude *= Gain;
            Frequency *= Lacunarity;
        }
        return Norm > 0.0f ? Sum / Norm : 0.0f;
    }

    float LythosRidged(const FVector& P, int32 Octaves, float Lacunarity, float Gain)
    {
        float Amplitude = 1.0f;
        float Frequency = 1.0f;
        float Sum = 0.0f;
        float Norm = 0.0f;
        for (int32 i = 0; i < Octaves; ++i)
        {
            const float N = 1.0f - FMath::Abs(FMath::PerlinNoise3D(P * Frequency));
            Sum += Amplitude * N * N;
            Norm += Amplitude;
            Amplitude *= Gain;
            Frequency *= Lacunarity;
        }
        return Norm > 0.0f ? Sum / Norm : 0.0f;
    }

    /** Three-channel domain warp used to turn blobs into irregular coastlines. */
    FVector LythosWarp(const FVector& P, const FVector& Offset, float Frequency, float Amplitude, int32 Octaves)
    {
        const FVector Base = P * Frequency + Offset;
        return FVector(
            LythosFbm(Base, Octaves, 2.0f, 0.5f),
            LythosFbm(Base + FVector(19.7f, -11.3f, 7.1f), Octaves, 2.0f, 0.5f),
            LythosFbm(Base + FVector(-8.4f, 23.9f, -14.6f), Octaves, 2.0f, 0.5f)) * Amplitude;
    }
}

namespace Lythos2
{
    namespace Density
    {
        float MacroElevation(const FLythos2PlanetContext& Context, const FVector& Direction)
        {
            const FVector Dir = Direction.GetSafeNormal();
            if (Dir.IsNearlyZero())
            {
                return 0.0f;
            }

            const int64 Seed = Context.Seed;

            // --- Deterministic seed-derived geography profile -----------------
            auto HU = [Seed](uint64 Channel) { return LythosHashUnit(Seed, Channel); };

            const FVector WarpOffset      = LythosSeedOffset(Seed, 2);
            const FVector ContinentOffset = LythosSeedOffset(Seed, 3);
            const FVector RegionalOffset  = LythosSeedOffset(Seed, 4);
            const FVector OrogenyOffset   = LythosSeedOffset(Seed, 5);
            const FVector RidgeOffset     = LythosSeedOffset(Seed, 6);
            const FVector PlateauOffset   = LythosSeedOffset(Seed, 7);
            const FVector ValleyOffset    = LythosSeedOffset(Seed, 8);
            const FVector DetailOffset    = LythosSeedOffset(Seed, 9);

            // Planetary scale: continent/ocean distribution.
            const float ContinentFreq = 1.25f + 0.95f * HU(11);
            const float WarpFreq      = 1.0f + 0.6f * HU(12);
            const float WarpAmp       = 0.35f + 0.40f * HU(13);
            const float SeaLevel      = -0.05f + 0.17f * HU(14);
            const float ShelfWidth    = 0.05f + 0.06f * HU(15);

            // Continental scale: broad highlands/lowlands.
            const float RegionalFreq  = 3.2f + 1.6f * HU(16);

            // Mountain-system scale: coherent ranges along orogeny contours.
            const float OrogenyFreq   = 2.4f + 1.6f * HU(17);
            const float BeltLevel     = -0.35f + 0.70f * HU(18);
            const float BeltWidth     = 0.09f + 0.10f * HU(19);
            const float RidgeFreq     = 6.0f + 3.0f * HU(20);

            // Regional scale: plateaus, escarpments, secondary valleys.
            const float PlateauFreq   = 4.0f + 1.5f * HU(21);
            const float PlateauEdge   = -0.10f + 0.55f * HU(22);
            const float ValleyFreq    = 3.4f + 1.6f * HU(23);
            const float ValleyLevel   = -0.40f + 0.80f * HU(24);
            const float ValleyWidth   = 0.06f + 0.08f * HU(25);

            // Local scale: restrained surface variation.
            const float DetailFreq    = 10.0f + 4.0f * HU(26);

            // --- Coordinate fields --------------------------------------------
            // Strong warp -> irregular coastlines / continental silhouettes.
            const FVector Warp = LythosWarp(Dir, WarpOffset, WarpFreq, WarpAmp, 2);
            const FVector ContinentCoord = Dir * ContinentFreq + Warp + ContinentOffset * 0.01f;

            // Mild warp reused for every finer scale (no extra evaluation cost).
            const FVector MidCoord = Dir + Warp * 0.35f;

            // --- 1. Continents / ocean basins (planetary scale) ---------------
            const float Continent = LythosFbm(ContinentCoord, 4, 2.0f, 0.5f); // -1..1

            // Smooth land mask; the ocean side is broader to form a shelf.
            const float LandMask = FMath::SmoothStep(SeaLevel - ShelfWidth, SeaLevel + ShelfWidth, Continent);

            // Ocean floor deepens offshore (continental shelf near the coast).
            const float OffShore = FMath::Clamp((SeaLevel - Continent) / 0.65f, 0.0f, 1.0f);
            const float OceanFloor = -(0.05f + 0.50f * OffShore);

            // Land rises from the coast toward stable continental interiors.
            const float Inland = FMath::Clamp((Continent - SeaLevel) / 0.60f, 0.0f, 1.0f);
            const float LandBase = 0.03f + 0.16f * Inland + 0.10f * Inland * Inland;

            // --- 2. Regional elevation (continental scale) --------------------
            const float Regional = LythosFbm(MidCoord * RegionalFreq + RegionalOffset, 3, 2.0f, 0.5f) * 0.20f;

            // --- 3. Mountain systems (mountain scale) -------------------------
            // Ranges follow the iso-contours of a smooth orogeny potential, so
            // they are coherent, oriented belts instead of isolated spikes.
            const float Orogeny = LythosFbm(MidCoord * OrogenyFreq + OrogenyOffset, 4, 2.0f, 0.5f);
            const float Belt = 1.0f - FMath::SmoothStep(0.0f, BeltWidth, FMath::Abs(Orogeny - BeltLevel));
            const float Ridge = LythosRidged(MidCoord * RidgeFreq + RidgeOffset, 4, 2.0f, 0.5f);
            const float AlongRange = 0.55f + 0.45f
                * LythosFbm(MidCoord * (RidgeFreq * 0.5f) + RidgeOffset, 2, 2.0f, 0.5f);
            const float Mountains = Belt * Ridge * AlongRange * 0.55f;

            // --- 4. Plateaus / highlands with steep edges (escarpments) -------
            const float PlateauField = LythosFbm(MidCoord * PlateauFreq + PlateauOffset, 3, 2.0f, 0.5f);
            const float Plateau = FMath::SmoothStep(PlateauEdge, PlateauEdge + 0.10f, PlateauField) * 0.12f;

            // --- 5. Secondary valley systems (contour belts, subtracted) ------
            const float ValleyField = LythosFbm(MidCoord * ValleyFreq + ValleyOffset, 3, 2.0f, 0.5f);
            const float ValleyBelt = 1.0f - FMath::SmoothStep(0.0f, ValleyWidth, FMath::Abs(ValleyField - ValleyLevel));
            const float Valleys = ValleyBelt * (0.08f + 0.08f * HU(27));

            // --- 6. Local detail (restrained, land only) ----------------------
            const float Detail = LythosFbm(Dir * DetailFreq + DetailOffset, 2, 2.0f, 0.5f) * 0.03f;

            // --- Composite ----------------------------------------------------
            const float Base = FMath::Lerp(OceanFloor, LandBase, LandMask);
            const float LandFeatures = (Regional + Mountains + Plateau - Valleys) * LandMask;
            const float Elevation = Base + LandFeatures + Detail * LandMask;

            return FMath::Clamp(Elevation, -1.0f, 1.0f);
        }

        double SurfaceRadiusCm(const FLythos2PlanetContext& Context, const FVector& Direction)
        {
            return static_cast<double>(Context.RadiusCm)
                + static_cast<double>(Context.TerrainHeightCm) * MacroElevation(Context, Direction);
        }

        double EvaluateDensity(const FLythos2PlanetContext& Context, const FVector& LocalPosition)
        {
            const double R = LocalPosition.Size();
            if (R < 1.0)
            {
                return 1.0;
            }

            const FVector Dir = LocalPosition / R;

            double Density = SurfaceRadiusCm(Context, Dir) - R;

            if (Context.VolumetricDetailAmount > 0.0f)
            {
                const FVector N = LocalPosition / Context.RadiusCm;
                const FVector Offset = LythosSeedOffset(Context.Seed, 21);
                const float V = LythosFbm(N * 9.0f + Offset, 3, 2.0f, 0.5f);
                Density += static_cast<double>(Context.TerrainHeightCm)
                    * static_cast<double>(Context.VolumetricDetailAmount) * V;
            }

            return Density;
        }

        FVector EvaluateGradient(const FLythos2PlanetContext& Context, const FVector& LocalPosition)
        {
            const double Step = static_cast<double>(Context.RadiusCm) * 1.0e-4;

            const double DX = EvaluateDensity(Context, LocalPosition + FVector(Step, 0.0, 0.0))
                - EvaluateDensity(Context, LocalPosition - FVector(Step, 0.0, 0.0));
            const double DY = EvaluateDensity(Context, LocalPosition + FVector(0.0, Step, 0.0))
                - EvaluateDensity(Context, LocalPosition - FVector(0.0, Step, 0.0));
            const double DZ = EvaluateDensity(Context, LocalPosition + FVector(0.0, 0.0, Step))
                - EvaluateDensity(Context, LocalPosition - FVector(0.0, 0.0, Step));

            return FVector(DX, DY, DZ);
        }
    }
}
