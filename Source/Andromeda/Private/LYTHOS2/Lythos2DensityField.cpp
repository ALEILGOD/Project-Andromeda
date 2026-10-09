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

    // =====================================================================
    // Phase 3.1 geomorphology.
    //
    // Erosion is a deterministic, HIERARCHICAL field evaluated in planetary
    // coordinates. The macro envelope (Phase 2) stays authoritative: erosion
    // only modifies it. The solid surface is where (depth below the macro
    // envelope) equals the local erosion depth. Because erosion also depends
    // on depth itself (undercuts and roofed void lenses), a single radial line
    // can cross solid/empty several times, which yields genuine 3D topology
    // (overhangs, undercut cliffs, natural arches/bridges and cavities).
    //
    // Pipeline:
    //   macro geography -> geological structure -> drainage -> resistance
    //     -> primary incision -> secondary -> tertiary -> fine
    //     -> terraces/benches -> talus response -> undercuts -> roofed voids.
    // =====================================================================

    struct FGeoFields
    {
        float MacroElev = 0.0f;

        // Hierarchical drainage: broad valleys -> tributaries -> gullies.
        float Primary = 0.0f;
        float Secondary = 0.0f;
        float Tertiary = 0.0f;
        float Fine = 0.0f;

        // Differential rock resistance and geological layering.
        float Resist = 0.0f;
        float Strata = 0.0f;

        // Composite surface incision (normalized by TerrainHeight).
        float Erode = 0.0f;
        // Hierarchical incision components (normalized), for diagnostics/tests.
        float IncPrimary = 0.0f;
        float IncSecondary = 0.0f;
        float IncTertiary = 0.0f;
        float IncFine = 0.0f;

        // Terraces / benches.
        float TerraceStrength = 0.0f;
        float TerraceLayer = 0.08f;

        // Undercut lens (cliff-base notch / overhang lip).
        float UnderStrength = 0.0f;
        float UnderDepth = 0.05f;
        float UnderWidth = 0.05f;

        // Roofed void lens (bridge / arch / cave / alcove).
        float VoidMask = 0.0f;
        float VoidStrength = 0.0f;
        float VoidDepth = 0.15f;
        float VoidWidth = 0.06f;

        // Deeper irregular cavity.
        float DeepMask = 0.0f;
        float DeepStrength = 0.0f;
        float DeepDepth = 0.38f;
        float DeepWidth = 0.08f;

        // 3D perturbation of the void/undercut shells (irregular, non-spherical).
        float Void3DFreq = 0.0f;

        // Phase 3.5 depth-stratified rock: near-horizontal hard/soft bands whose
        // softer layers are preferentially removed, producing coherent ledges,
        // recessed walls and layer-height overhangs/alcoves.
        float LayerThickness = 0.14f;   // normalized by TerrainHeight
        float LayerPhase = 0.0f;        // lateral phase shift (fbm)
        float LayerWarpAmp = 0.0f;      // 3D irregularity of layer boundaries
        float Layer3DFreq = 0.0f;
        float LayerAlcove = 0.0f;       // alcove depth amplitude

        float ErosionScale = 1.0f;
        float Talus = 0.0f;
        float FeatureImportance = 0.0f;

        // Phase 3.3 sparse feature selection / scale.
        float SparseField = 0.0f;
        float SparseField2 = 0.0f;
        float VolumetricCandidate = 0.0f;
        double MinFeatureWidthCm = 0.0;
        int32 VolumetricAccepted = 0;

        // Phase 3.4 per-class feature acceptance (telemetry / tests).
        int32 OverhangAccepted = 0;
        int32 CavityAccepted = 0;
        int32 BridgeAccepted = 0;
    };

    // ---------------------------------------------------------------------
    // Phase 3.6 authoritative macro geography, computed once and shared with
    // the geomorphology so smaller-scale erosion can be CONDITIONED by the
    // larger landform (mountains vs lowlands) instead of being an independent
    // field that trenches through mountains at random.
    // ---------------------------------------------------------------------
    struct FMacroFields
    {
        float Elevation = 0.0f;
        float Mountains = 0.0f;   // raw coherent mountain amplitude (~0..0.55)
        float LandMask = 0.0f;    // 1 on land, 0 in ocean
        float Valleys = 0.0f;     // regional valley belts (subtracted)
    };

    FMacroFields ComputeMacroFields(const FLythos2PlanetContext& Context, const FVector& Direction)
    {
        FMacroFields Out;
        const FVector Dir = Direction.GetSafeNormal();
        if (Dir.IsNearlyZero())
        {
            return Out;
        }

        const int64 Seed = Context.Seed;
        auto HU = [Seed](uint64 Channel) { return LythosHashUnit(Seed, Channel); };

        const FVector WarpOffset      = LythosSeedOffset(Seed, 2);
        const FVector ContinentOffset = LythosSeedOffset(Seed, 3);
        const FVector RegionalOffset  = LythosSeedOffset(Seed, 4);
        const FVector OrogenyOffset   = LythosSeedOffset(Seed, 5);
        const FVector RidgeOffset     = LythosSeedOffset(Seed, 6);
        const FVector PlateauOffset   = LythosSeedOffset(Seed, 7);
        const FVector ValleyOffset    = LythosSeedOffset(Seed, 8);
        const FVector DetailOffset    = LythosSeedOffset(Seed, 9);

        const float ContinentFreq = 1.25f + 0.95f * HU(11);
        const float WarpFreq      = 1.0f + 0.6f * HU(12);
        const float WarpAmp       = 0.35f + 0.40f * HU(13);
        const float SeaLevel      = -0.05f + 0.17f * HU(14);
        const float ShelfWidth    = 0.05f + 0.06f * HU(15);

        const float RegionalFreq  = 3.2f + 1.6f * HU(16);
        const float OrogenyFreq   = 2.4f + 1.6f * HU(17);
        const float BeltLevel     = -0.35f + 0.70f * HU(18);
        const float BeltWidth     = 0.09f + 0.10f * HU(19);
        const float RidgeFreq     = 6.0f + 3.0f * HU(20);
        const float PlateauFreq   = 4.0f + 1.5f * HU(21);
        const float PlateauEdge   = -0.10f + 0.55f * HU(22);
        const float ValleyFreq    = 3.4f + 1.6f * HU(23);
        const float ValleyLevel   = -0.40f + 0.80f * HU(24);
        const float ValleyWidth   = 0.06f + 0.08f * HU(25);
        const float DetailFreq    = 10.0f + 4.0f * HU(26);

        const FVector Warp = LythosWarp(Dir, WarpOffset, WarpFreq, WarpAmp, 2);
        const FVector ContinentCoord = Dir * ContinentFreq + Warp + ContinentOffset * 0.01f;
        const FVector MidCoord = Dir + Warp * 0.35f;

        const float Continent = LythosFbm(ContinentCoord, 4, 2.0f, 0.5f);
        const float LandMask = FMath::SmoothStep(SeaLevel - ShelfWidth, SeaLevel + ShelfWidth, Continent);
        const float OffShore = FMath::Clamp((SeaLevel - Continent) / 0.65f, 0.0f, 1.0f);
        const float OceanFloor = -(0.05f + 0.50f * OffShore);
        const float Inland = FMath::Clamp((Continent - SeaLevel) / 0.60f, 0.0f, 1.0f);
        const float LandBase = 0.03f + 0.16f * Inland + 0.10f * Inland * Inland;

        const float Regional = LythosFbm(MidCoord * RegionalFreq + RegionalOffset, 3, 2.0f, 0.5f) * 0.20f;

        const float Orogeny = LythosFbm(MidCoord * OrogenyFreq + OrogenyOffset, 4, 2.0f, 0.5f);
        const float Belt = 1.0f - FMath::SmoothStep(0.0f, BeltWidth, FMath::Abs(Orogeny - BeltLevel));
        const float Ridge = LythosRidged(MidCoord * RidgeFreq + RidgeOffset, 4, 2.0f, 0.5f);
        const float AlongRange = 0.55f + 0.45f
            * LythosFbm(MidCoord * (RidgeFreq * 0.5f) + RidgeOffset, 2, 2.0f, 0.5f);
        const float Mountains = Belt * Ridge * AlongRange * 0.55f;

        const float PlateauField = LythosFbm(MidCoord * PlateauFreq + PlateauOffset, 3, 2.0f, 0.5f);
        const float Plateau = FMath::SmoothStep(PlateauEdge, PlateauEdge + 0.10f, PlateauField) * 0.12f;

        const float ValleyField = LythosFbm(MidCoord * ValleyFreq + ValleyOffset, 3, 2.0f, 0.5f);
        const float ValleyBelt = 1.0f - FMath::SmoothStep(0.0f, ValleyWidth, FMath::Abs(ValleyField - ValleyLevel));
        const float Valleys = ValleyBelt * (0.08f + 0.08f * HU(27));

        const float Detail = LythosFbm(Dir * DetailFreq + DetailOffset, 2, 2.0f, 0.5f) * 0.03f;

        const float Base = FMath::Lerp(OceanFloor, LandBase, LandMask);
        const float LandFeatures = (Regional + Mountains + Plateau - Valleys) * LandMask;
        const float Elevation = Base + LandFeatures + Detail * LandMask;

        Out.Elevation = FMath::Clamp(Elevation, -1.0f, 1.0f);
        Out.Mountains = Mountains;
        Out.LandMask = LandMask;
        Out.Valleys = Valleys;
        return Out;
    }

    void ComputeGeoFields(const FLythos2PlanetContext& Context, const FVector& Dir, const FMacroFields& Macro, FGeoFields& Out)
    {
        const int64 Seed = Context.Seed;
        auto HU = [Seed](uint64 Channel) { return LythosHashUnit(Seed, Channel); };
        const float MacroElev = Macro.Elevation;

        const FVector PrimaryOff = LythosSeedOffset(Seed, 31);
        const FVector SecondOff  = LythosSeedOffset(Seed, 32);
        const FVector TertOff    = LythosSeedOffset(Seed, 33);
        const FVector ResistOff  = LythosSeedOffset(Seed, 34);
        const FVector StrataOff  = LythosSeedOffset(Seed, 35);
        const FVector FeatOff    = LythosSeedOffset(Seed, 36);
        const FVector DeepOff    = LythosSeedOffset(Seed, 37);
        const FVector WarpOff    = LythosSeedOffset(Seed, 38);
        const FVector LayerOff   = LythosSeedOffset(Seed, 41);

        const float BaseFreq   = 6.0f + 3.5f * HU(41);
        const float WarpF      = 1.1f + 0.8f * HU(42);
        const float WarpA      = 0.40f + 0.35f * HU(43);
        const float ResistBias = 0.42f + 0.30f * HU(45);
        const float ResistFreq = 3.2f + 2.4f * HU(46);
        const float StrataFreq = 2.2f + 1.6f * HU(47);
        const float FeatFreq   = 4.2f + 2.6f * HU(49);
        const float DeepFreq   = 5.5f + 3.0f * HU(52);

        Out = FGeoFields();
        Out.MacroElev = MacroElev;
        Out.ErosionScale = 0.45f + 0.55f * FMath::Clamp(Context.ClimateProxy, 0.0f, 1.0f);
        Out.Talus = FMath::Clamp(Context.TalusAmount, 0.0f, 1.0f);

        // --- Warped drainage coordinate (meandering, dendritic) -----------
        const FVector Warp = LythosWarp(Dir, WarpOff, WarpF, WarpA, 2);
        const FVector D = Dir + Warp * 0.55f;

        // --- Primary drainage: broad, meandering valley corridors ---------
        const float PrimField = LythosRidged(D * BaseFreq + PrimaryOff, 4, 2.0f, 0.5f);
        const float WidthVar = 0.5f + 0.5f * LythosFbm(D * (BaseFreq * 0.8f) + SecondOff, 2, 2.0f, 0.5f);
        const float PrimThreshold = 0.56f + 0.16f * WidthVar;
        Out.Primary = FMath::SmoothStep(PrimThreshold, PrimThreshold + 0.22f, PrimField);

        // Broad footprint of the primary system (drives tributary branching).
        const float PrimBroad = FMath::SmoothStep(PrimThreshold - 0.30f, PrimThreshold + 0.06f, PrimField);

        // --- Secondary: tributary valleys branching from the primary ------
        const float SecField = LythosRidged(D * (BaseFreq * 2.15f) + TertOff, 3, 2.0f, 0.5f);
        const float Branch = FMath::SmoothStep(0.28f, 0.78f, PrimBroad);
        Out.Secondary = FMath::SmoothStep(0.64f, 0.86f, SecField) * Branch;

        // --- Tertiary: restrained local gullies / benches -----------------
        const float TerField = LythosRidged(D * (BaseFreq * 4.4f) + PrimaryOff + FVector(11.0f, -7.0f, 5.0f), 3, 2.0f, 0.5f);
        const float TerGate = FMath::SmoothStep(0.12f, 0.52f, Out.Secondary + 0.65f * Out.Primary);
        Out.Tertiary = FMath::SmoothStep(0.70f, 0.92f, TerField) * TerGate;

        // --- Fine: subtle rock breakup (never dominates the silhouette) ---
        Out.Fine = 0.5f + 0.5f * LythosFbm(D * (BaseFreq * 9.0f) + FeatOff, 2, 2.0f, 0.5f);

        // --- Differential rock resistance (sharpened for geological contrast)
        const float RBase = FMath::Clamp(
            ResistBias + 0.55f * LythosFbm(D * ResistFreq + ResistOff, 3, 2.0f, 0.5f)
            + 0.18f * (Out.Primary - 0.5f), 0.0f, 1.0f);
        Out.Resist = FMath::SmoothStep(0.32f, 0.68f, RBase);

        // --- Geological layering (strata) ---------------------------------
        Out.Strata = 0.5f + 0.5f * LythosFbm(D * StrataFreq + StrataOff, 3, 2.0f, 0.5f);


        // --- Surface incision (hierarchical) ------------------------------
        const float Uplift = FMath::Clamp((MacroElev + 0.08f) / 0.62f, 0.0f, 1.0f);

        // Phase 3.6 spatial coherence: incision is conditioned by the coherent
        // mountain mass so erosion channels incise slopes and lowlands instead
        // of trenching through mountain crests. (Incised mountain valleys and
        // passes still occur where IncMask leaves a residual.)
        const float Mountainness = FMath::SmoothStep(0.12f, 0.50f, Macro.Mountains);
        const float IncMask = 1.0f - 0.70f * Mountainness;

        const float Inc1 = Out.Primary   * (0.10f + 0.20f * HU(53)) * Out.ErosionScale * (1.0f - 0.55f * Out.Resist) * IncMask;
        const float Inc2 = Out.Secondary * (0.04f + 0.08f * HU(54)) * (0.35f + 0.65f * Uplift) * Out.ErosionScale * (1.0f - 0.55f * Out.Resist) * IncMask;
        const float Inc3 = Out.Tertiary  * (0.022f + 0.032f * HU(55)) * Out.ErosionScale * (1.0f - 0.45f * Out.Resist) * (0.5f + 0.5f * IncMask);
        const float Inc4 = Out.Fine      * 0.003f * Out.ErosionScale;
        float Erode = Inc1 + Inc2 + Inc3 + Inc4;

        // Talus/debris response: partially infill narrow, over-steep gullies.
        const float Relief = FMath::Clamp((Out.Secondary + 0.8f * Out.Tertiary) - 0.45f * Out.Primary, 0.0f, 1.0f);
        Erode = FMath::Max(0.0f, Erode - Out.Talus * 0.055f * Relief);

        // Terraces/benches: only where incision is strong and rock resistant.
        Out.TerraceLayer = 0.045f + 0.075f * Out.Strata;
        Out.TerraceStrength = (0.20f + 0.50f * HU(48)) * Out.Resist;
        {
            const float Layer = FMath::Max(0.02f, Out.TerraceLayer);
            const float X = Erode / Layer;
            const float Fl = FMath::FloorToFloat(X);
            const float Fr = X - Fl;
            const float Terraced = (Fl + FMath::SmoothStep(0.36f, 0.64f, Fr)) * Layer;
            const float TStr = Out.TerraceStrength * FMath::SmoothStep(0.05f, 0.22f, Erode);
            Erode = FMath::Lerp(Erode, Terraced, TStr);
        }
        Out.Erode = Erode;
        Out.IncPrimary = Inc1;
        Out.IncSecondary = Inc2;
        Out.IncTertiary = Inc3;
        Out.IncFine = Inc4;

        // --- Undercut: weaker base below, stronger cap above --------------
        // Phase 3.3: features must be rare. A separate low-frequency sparse
        // selection field gates every volumetric feature, so the default planet
        // is eroded terrain and only occasionally contains a void/overhang.
        const FVector SparseOff  = LythosSeedOffset(Seed, 39);
        const FVector SparseOff2 = LythosSeedOffset(Seed, 40);
        const float SparseFreq = 1.05f + 0.85f * HU(57);
        Out.SparseField  = 0.5f + 0.5f * LythosFbm(Dir * SparseFreq + SparseOff, 3, 2.0f, 0.5f);
        Out.SparseField2 = 0.5f + 0.5f * LythosFbm(Dir * (SparseFreq * 1.35f) + SparseOff2, 2, 2.0f, 0.5f);
        const float VoidGate  = FMath::SmoothStep(0.60f, 0.74f, Out.SparseField);
        // Phase 3.4: overhangs get their OWN, lower sparse threshold so small /
        // medium erosion overhangs and alcoves are moderately more common,
        // while enclosed cavities and bridges (VoidGate/DeepMask) stay rare.
        const float UnderGate = FMath::SmoothStep(0.50f, 0.66f, Out.SparseField2);

        // --- Phase 3.5 depth-stratified rock layers -----------------------
        // Near-horizontal bands (function of depth below the macro envelope)
        // with laterally varying thickness/phase and 3D warp so they read as
        // tilted, folded strata rather than perfect shells. Resistant caps over
        // softer layers are removed preferentially, producing ledges, recessed
        // walls and layer-height overhangs/alcoves.
        {
            const float LayerFreq = 0.9f + 0.8f * HU(58);
            const float LayerVar = 0.5f + 0.5f * LythosFbm(D * LayerFreq + LayerOff, 2, 2.0f, 0.5f);
            Out.LayerThickness = 0.080f + 0.100f * LayerVar;
            Out.LayerPhase = 0.45f * LythosFbm(D * (LayerFreq * 0.6f) + LayerOff + FVector(4.0f, -9.0f, 3.0f), 2, 2.0f, 0.5f);
            Out.LayerWarpAmp = 0.020f + 0.030f * HU(59);
            Out.Layer3DFreq = 1.0f / FMath::Max(500.0f, Context.TerrainHeightCm * 0.22f);

            // Alcove amplitude: strongest in resistant rock, gated to a moderate
            // (not universal) spatial selection and uplifted terrain so cliffs
            // form coherent alcove bands rather than ubiquitous holes.
            const float LayerGate = FMath::SmoothStep(0.38f, 0.60f, Out.SparseField2);
            const float LayerUplift = FMath::Clamp((MacroElev + 0.10f) / 0.55f, 0.0f, 1.0f);
            Out.LayerAlcove = (0.10f + 0.16f * HU(60)) * (0.40f + 0.60f * Out.Resist)
                * LayerGate * (0.40f + 0.60f * LayerUplift);
        }

        const float WallBand = FMath::SmoothStep(0.30f, 0.62f, Out.Primary)
            * (1.0f - FMath::SmoothStep(0.80f, 0.98f, Out.Primary));
        const float Cap = FMath::SmoothStep(0.34f, 0.70f, Out.Strata);
        const float SoftBase = 1.0f - FMath::SmoothStep(0.34f, 0.70f,
            0.5f + 0.5f * LythosFbm(D * (StrataFreq * 1.7f) + StrataOff, 2, 2.0f, 0.5f));
        Out.UnderStrength = (0.17f + 0.23f * HU(56)) * WallBand
            * (0.40f + 0.60f * Cap) * (0.50f + 0.50f * SoftBase) * (0.50f + 0.50f * Out.Resist)
            * UnderGate;
        Out.UnderDepth = 0.110f + 0.070f * (0.5f + 0.5f * LythosFbm(D * (FeatFreq * 0.9f) + FeatOff, 2, 2.0f, 0.5f));
        Out.UnderWidth = 0.055f + 0.055f * WidthVar;

        // --- Roofed void lens (bridge / arch / cave / alcove) -------------
        const float Feat = LythosFbm(D * FeatFreq + FeatOff, 3, 2.0f, 0.5f); // -1..1
        const float VoidScore = 0.46f * Out.Primary + 0.42f * Out.Resist + 0.22f * Feat;
        const float LandGate = FMath::SmoothStep(-0.35f, 0.05f, MacroElev);
        Out.VoidMask = FMath::SmoothStep(0.55f, 0.73f, VoidScore) * VoidGate * LandGate; // rare
        Out.VoidStrength = 0.62f + 0.30f * (0.5f + 0.5f * Feat);
        Out.VoidDepth = 0.140f + 0.200f * (0.5f + 0.5f * LythosFbm(D * (FeatFreq * 0.7f) + SecondOff, 2, 2.0f, 0.5f));
        Out.VoidWidth = 0.060f + 0.060f * WidthVar;

        // --- Deeper irregular cavity (extremely sparse) -------------------
        const float DeepField = LythosFbm(D * DeepFreq + DeepOff, 3, 2.0f, 0.5f);
        Out.DeepMask = FMath::SmoothStep(0.86f, 0.97f,
            FMath::Clamp(0.55f * Out.VoidMask + 0.45f * (0.5f + 0.5f * DeepField), 0.0f, 1.0f));
        Out.DeepStrength = 0.35f + 0.25f * (0.5f + 0.5f * DeepField);
        Out.DeepDepth = 0.30f + 0.22f * (0.5f + 0.5f * LythosFbm(D * (DeepFreq * 0.6f) + DeepOff, 2, 2.0f, 0.5f));
        Out.DeepWidth = 0.055f + 0.050f * WidthVar;

        // Feature scale + acceptance: a volumetric feature is only accepted
        // when it has a meaningful geological size (multiple mesh cells).
        Out.MinFeatureWidthCm = static_cast<double>(Context.TerrainHeightCm) * Out.VoidWidth;
        Out.VolumetricCandidate = FMath::Clamp(VoidScore * VoidGate + 0.5f * Out.UnderStrength, 0.0f, 1.0f);
        Out.VolumetricAccepted = (Out.VoidMask > 0.05f || Out.UnderStrength > 0.05f) ? 1 : 0;

        // Phase 3.4: feature classes are tracked separately so overhangs can be
        // moderately common while cavities stay uncommon and bridges very rare.
        Out.OverhangAccepted = (Out.UnderStrength > 0.06f) ? 1 : 0;
        Out.CavityAccepted = (Out.VoidMask > 0.08f) ? 1 : 0;
        Out.BridgeAccepted = (Out.VoidMask > 0.16f && Out.DeepMask > 0.10f) ? 1 : 0;

        // 3D perturbation frequency for void shells (~0.1 * H features).
        Out.Void3DFreq = 1.0f / FMath::Max(500.0f, Context.TerrainHeightCm * 0.10f);

        // Feature importance: where 3D structure / canyons live.
        Out.FeatureImportance = FMath::Clamp(
            0.55f * Out.VoidMask + 0.35f * Out.Primary
            + 0.60f * Out.UnderStrength + 0.25f * FMath::SmoothStep(0.4f, 0.9f, Out.Primary), 0.0f, 1.0f);
    }

    double EvaluateProfileAtRadius(const FLythos2PlanetContext& Context, const FGeoFields& F, const FVector& P, double R)
    {
        const double H = static_cast<double>(Context.TerrainHeightCm);
        const double MacroSurface = static_cast<double>(Context.RadiusCm) + H * F.MacroElev;
        const double DepthNorm = (MacroSurface - R) / H;

        double Result = DepthNorm - F.Erode;

        // Undercut notch (cliff-base undercutting / overhang lip).
        if (F.UnderStrength > 1.0e-4f)
        {
            const double u = (DepthNorm - (F.Erode + F.UnderDepth)) / F.UnderWidth;
            Result -= F.UnderStrength * FMath::Exp(-u * u);
        }

        // Roofed void lens (bridge / arch / cave / alcove). The 3D perturbation
        // makes the shell irregular instead of a perfect spherical band.
        if (F.VoidMask > 1.0e-4f)
        {
            const double Depth3D = F.VoidDepth + 0.030 * FMath::PerlinNoise3D(P * F.Void3DFreq);
            const double v = (DepthNorm - (F.Erode + Depth3D)) / F.VoidWidth;
            Result -= F.VoidMask * F.VoidStrength * FMath::Exp(-v * v);
        }

        // Deeper irregular cavity.
        if (F.DeepMask > 1.0e-4f)
        {
            const double Depth3D = F.DeepDepth + 0.045 * FMath::PerlinNoise3D(P * (F.Void3DFreq * 0.6f));
            const double w = (DepthNorm - (F.Erode + Depth3D)) / F.DeepWidth;
            Result -= F.DeepMask * F.DeepStrength * FMath::Exp(-w * w);
        }

        // Phase 3.5 depth-stratified alcoves: a hard cap over a softer layer.
        // The softer band is removed preferentially, producing a recessed ledge
        // or (when strong enough) a true overhang under the cap. The layer
        // coordinate is warped in 3D so bands are irregular, not spherical.
        if (F.LayerAlcove > 1.0e-4f)
        {
            const double Warp = static_cast<double>(F.LayerWarpAmp) * FMath::PerlinNoise3D(P * F.Layer3DFreq);
            const double Coord = (DepthNorm + static_cast<double>(F.LayerPhase) + Warp)
                / FMath::Max(0.02, static_cast<double>(F.LayerThickness));
            const double FracD = Coord - FMath::FloorToDouble(Coord);
            const float Frac = static_cast<float>(FracD);
            const float Soft = FMath::SmoothStep(0.42f, 0.66f, Frac)
                * (1.0f - FMath::SmoothStep(0.86f, 0.99f, Frac));
            Result -= static_cast<double>(F.LayerAlcove) * static_cast<double>(Soft);
        }

        return Result * H;
    }
}

namespace Lythos2
{
    namespace Density
    {
        float MacroElevation(const FLythos2PlanetContext& Context, const FVector& Direction)
        {
            return ComputeMacroFields(Context, Direction).Elevation;
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
            const FMacroFields Macro = ComputeMacroFields(Context, Dir);
            const double MacroSurface = static_cast<double>(Context.RadiusCm)
                + static_cast<double>(Context.TerrainHeightCm) * Macro.Elevation;
            const double MacroDensity = MacroSurface - R;

            if (Context.GeologyAmount <= 0.0f)
            {
                return MacroDensity;
            }

            FGeoFields Fields;
            ComputeGeoFields(Context, Dir, Macro, Fields);
            const double Geology = EvaluateProfileAtRadius(Context, Fields, LocalPosition, R);
            if (Context.GeologyAmount >= 1.0f)
            {
                return Geology;
            }
            return FMath::Lerp(MacroDensity, Geology, static_cast<double>(Context.GeologyAmount));
        }

        void SampleDensityColumn(
            const FLythos2PlanetContext& Context,
            const FVector& Direction,
            const double* Radii,
            int32 Count,
            double* OutValues)
        {
            if (Count <= 0 || Radii == nullptr || OutValues == nullptr)
            {
                return;
            }

            const FVector Dir = Direction.GetSafeNormal();
            if (Dir.IsNearlyZero())
            {
                for (int32 I = 0; I < Count; ++I) { OutValues[I] = 1.0; }
                return;
            }

            const FMacroFields Macro = ComputeMacroFields(Context, Dir);
            const double MacroSurface = static_cast<double>(Context.RadiusCm)
                + static_cast<double>(Context.TerrainHeightCm) * Macro.Elevation;
            const double GeologyWeight = static_cast<double>(Context.GeologyAmount);

            if (Context.GeologyAmount <= 0.0f)
            {
                for (int32 I = 0; I < Count; ++I) { OutValues[I] = MacroSurface - Radii[I]; }
                return;
            }

            // The direction-only geomorphology is computed exactly once per
            // column; this is the single biggest mesher speed-up.
            FGeoFields Fields;
            ComputeGeoFields(Context, Dir, Macro, Fields);

            for (int32 I = 0; I < Count; ++I)
            {
                const double R = Radii[I];
                const double Geology = EvaluateProfileAtRadius(Context, Fields, Dir * R, R);
                OutValues[I] = (GeologyWeight >= 1.0)
                    ? Geology
                    : FMath::Lerp(MacroSurface - R, Geology, GeologyWeight);
            }
        }

        double FindSurfaceRadiusCm(const FLythos2PlanetContext& Context, const FVector& Direction, double SearchDepthCm)
        {
            const FVector Dir = Direction.GetSafeNormal();
            if (Dir.IsNearlyZero())
            {
                return -1.0;
            }

            const double MacroSurface = SurfaceRadiusCm(Context, Dir);
            const double Search = FMath::Max(SearchDepthCm, 1.0);
            // Fine enough to catch thin surviving roofs (~0.02 * H).
            const int32 Steps = 64;
            const double Step = Search / Steps;

            if (EvaluateDensity(Context, Dir * MacroSurface) > 0.0)
            {
                return MacroSurface;
            }

            double EmptyR = MacroSurface;
            double SolidR = -1.0;
            for (int32 I = 1; I <= Steps; ++I)
            {
                const double R = MacroSurface - Step * I;
                if (EvaluateDensity(Context, Dir * R) > 0.0)
                {
                    SolidR = R;
                    break;
                }
                EmptyR = R;
            }

            if (SolidR < 0.0)
            {
                return -1.0;
            }

            // Bisection between the last empty sample and the first solid one.
            double Lo = SolidR;
            double Hi = EmptyR;
            for (int32 B = 0; B < 14; ++B)
            {
                const double Mid = 0.5 * (Lo + Hi);
                if (EvaluateDensity(Context, Dir * Mid) > 0.0)
                {
                    Lo = Mid;
                }
                else
                {
                    Hi = Mid;
                }
            }
            return 0.5 * (Lo + Hi);
        }

        void SampleGeology(const FLythos2PlanetContext& Context, const FVector& Direction, FLythos2GeologySample& OutSample)
        {
            OutSample = FLythos2GeologySample();

            const FVector Dir = Direction.GetSafeNormal();
            if (Dir.IsNearlyZero())
            {
                return;
            }

            const FMacroFields Macro = ComputeMacroFields(Context, Dir);
            OutSample.MacroElev = Macro.Elevation;
            if (Context.GeologyAmount <= 0.0f)
            {
                return;
            }

            FGeoFields Fields;
            ComputeGeoFields(Context, Dir, Macro, Fields);

            OutSample.Resistance = Fields.Resist;
            OutSample.PrimaryDrainage = Fields.Primary;
            OutSample.SecondaryDrainage = Fields.Secondary;
            OutSample.TertiaryDrainage = Fields.Tertiary;
            OutSample.ErosionDepthCm = static_cast<double>(Fields.Erode) * Context.TerrainHeightCm
                * static_cast<double>(Context.GeologyAmount);
            OutSample.PrimaryIncisionCm = static_cast<double>(Fields.IncPrimary) * Context.TerrainHeightCm
                * static_cast<double>(Context.GeologyAmount);
            OutSample.SecondaryIncisionCm = static_cast<double>(Fields.IncSecondary) * Context.TerrainHeightCm
                * static_cast<double>(Context.GeologyAmount);
            OutSample.TertiaryIncisionCm = static_cast<double>(Fields.IncTertiary) * Context.TerrainHeightCm
                * static_cast<double>(Context.GeologyAmount);
            OutSample.FineIncisionCm = static_cast<double>(Fields.IncFine) * Context.TerrainHeightCm
                * static_cast<double>(Context.GeologyAmount);
            OutSample.TerraceStrength = Fields.TerraceStrength;
            OutSample.TerraceLayerCm = static_cast<double>(Fields.TerraceLayer) * Context.TerrainHeightCm;
            OutSample.UndercutStrength = Fields.UnderStrength;
            OutSample.VoidMask = Fields.VoidMask;
            OutSample.DeepVoidMask = Fields.DeepMask;
            OutSample.FeatureImportance = Fields.FeatureImportance;
            OutSample.SparseField = Fields.SparseField;
            OutSample.VolumetricCandidate = Fields.VolumetricCandidate;
            OutSample.MinFeatureWidthCm = Fields.MinFeatureWidthCm;
            OutSample.VolumetricAccepted = Fields.VolumetricAccepted;
            OutSample.OverhangAccepted = Fields.OverhangAccepted;
            OutSample.CavityAccepted = Fields.CavityAccepted;
            OutSample.BridgeAccepted = Fields.BridgeAccepted;
            OutSample.LayerThickness = Fields.LayerThickness;
            OutSample.LayerAlcoveStrength = Fields.LayerAlcove;
            OutSample.Mountainness = Macro.Mountains;
        }

        float FeatureImportance(const FLythos2PlanetContext& Context, const FVector& Direction)
        {
            if (Context.GeologyAmount <= 0.0f)
            {
                return 0.0f;
            }

            const FVector Dir = Direction.GetSafeNormal();
            if (Dir.IsNearlyZero())
            {
                return 0.0f;
            }

            const FMacroFields Macro = ComputeMacroFields(Context, Dir);
            FGeoFields Fields;
            ComputeGeoFields(Context, Dir, Macro, Fields);
            return Fields.FeatureImportance;
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
