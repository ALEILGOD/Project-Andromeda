#include "Planet/PlanetProfile.h"

namespace
{
    uint64 HashSeed64(uint64 Seed, uint64 Salt)
    {
        Seed += Salt;
        Seed = (Seed ^ (Seed >> 30)) * 0xBF58476D1CE4E5B9ULL;
        Seed = (Seed ^ (Seed >> 27)) * 0x94D049BB133111EBULL;
        return Seed ^ (Seed >> 31);
    }

    float HashToUnitFloat(uint64 Hash)
    {
        const uint32 Value = static_cast<uint32>(Hash & 0xFFFFFFULL);
        return static_cast<float>(Value) / 16777216.0f;
    }
}

FString UPlanetProfileGenerator::GetArchetypeName(
    EPlanetArchetype Archetype
)
{
    switch (Archetype)
    {
    case EPlanetArchetype::Terran:   return TEXT("Terran");
    case EPlanetArchetype::Arid:     return TEXT("Arid");
    case EPlanetArchetype::Desert:   return TEXT("Desert");
    case EPlanetArchetype::Oceanic:  return TEXT("Oceanic");
    case EPlanetArchetype::Frozen:   return TEXT("Frozen");
    case EPlanetArchetype::Tundra:   return TEXT("Tundra");
    case EPlanetArchetype::Jungle:   return TEXT("Jungle");
    case EPlanetArchetype::Volcanic: return TEXT("Volcanic");
    case EPlanetArchetype::Rocky:    return TEXT("Rocky");
    case EPlanetArchetype::Exotic:   return TEXT("Exotic");
    default:                         return TEXT("Terran");
    }
}

FPlanetProfile UPlanetProfileGenerator::GetArchetypeDefaultProfile(
    EPlanetArchetype Archetype
)
{
    FPlanetProfile P;
    P.Archetype = Archetype;
    P.ArchetypeName = GetArchetypeName(Archetype);

    switch (Archetype)
    {
    case EPlanetArchetype::Terran:
        // ====================================================================
        // TERRAN
        // Acqua medio-alta, alta biodiversita, varieta bilanciata di biomi core,
        // neve opzionale su vette e poli.
        // ====================================================================
        P.WaterCoverage = 0.58f;
        P.WaterCoverageMin = 0.50f;
        P.WaterCoverageMax = 0.65f;
        P.TemperatureBias = 0.0f;
        P.HumidityBias = 0.05f;
        P.bSnowAllowed = true;
        P.SnowPotential = 0.40f;
        P.Biodiversity = 0.90f;
        P.BiomeDiversity = 0.85f;
        P.SpecialBiomeAvailability = 0.08f;
        P.BiomeBiases.PlainsBias = 0.10f;
        P.BiomeBiases.GrasslandBias = 0.15f;
        P.BiomeBiases.ForestBias = 0.15f;
        P.BiomeBiases.DesertBias = 0.0f;
        P.BiomeBiases.TundraBias = 0.0f;
        P.BiomeBiases.MountainBias = 0.0f;
        P.BiomeBiases.SnowBias = 0.0f;
        break;

    case EPlanetArchetype::Arid:
        // ====================================================================
        // ARID
        // Acqua bassa, umidita negativa, deserti e praterie secche favorite,
        // foreste rare ma possibili, neve rara ma non impossibile.
        // ====================================================================
        P.WaterCoverage = 0.34f;
        P.WaterCoverageMin = 0.30f;
        P.WaterCoverageMax = 0.40f;
        P.TemperatureBias = 0.15f;
        P.HumidityBias = -0.25f;
        P.bSnowAllowed = true;
        P.SnowPotential = 0.15f;
        P.Biodiversity = 0.45f;
        P.BiomeDiversity = 0.60f;
        P.SpecialBiomeAvailability = 0.12f;
        P.BiomeBiases.DesertBias = 0.40f;
        P.BiomeBiases.GrasslandBias = 0.20f;
        P.BiomeBiases.PlainsBias = 0.25f;
        P.BiomeBiases.MountainBias = 0.20f;
        P.BiomeBiases.ForestBias = -0.30f;
        P.BiomeBiases.TundraBias = -0.20f;
        P.BiomeBiases.SnowBias = -0.15f;
        break;

    case EPlanetArchetype::Desert:
        // ====================================================================
        // DESERT
        // Acqua minima, caldo arido estremo, distese di sabbia e roccia.
        // ====================================================================
        P.WaterCoverage = 0.30f;
        P.WaterCoverageMin = 0.30f;
        P.WaterCoverageMax = 0.35f;
        P.TemperatureBias = 0.25f;
        P.HumidityBias = -0.40f;
        P.bSnowAllowed = false;
        P.SnowPotential = 0.05f;
        P.Biodiversity = 0.25f;
        P.BiomeDiversity = 0.40f;
        P.SpecialBiomeAvailability = 0.15f;
        P.BiomeBiases.DesertBias = 0.65f;
        P.BiomeBiases.PlainsBias = 0.15f;
        P.BiomeBiases.MountainBias = 0.20f;
        P.BiomeBiases.ForestBias = -0.60f;
        P.BiomeBiases.GrasslandBias = -0.20f;
        P.BiomeBiases.TundraBias = -0.50f;
        P.BiomeBiases.SnowBias = -0.80f;
        break;

    case EPlanetArchetype::Oceanic:
        // ====================================================================
        // OCEANIC
        // Acqua massima, umidita elevata, isole lussureggianti, ampie coste.
        // ====================================================================
        P.WaterCoverage = 0.68f;
        P.WaterCoverageMin = 0.62f;
        P.WaterCoverageMax = 0.70f;
        P.TemperatureBias = 0.0f;
        P.HumidityBias = 0.30f;
        P.bSnowAllowed = true;
        P.SnowPotential = 0.30f;
        P.Biodiversity = 0.85f;
        P.BiomeDiversity = 0.70f;
        P.SpecialBiomeAvailability = 0.15f;
        P.BiomeBiases.ForestBias = 0.30f;
        P.BiomeBiases.GrasslandBias = 0.20f;
        P.BiomeBiases.PlainsBias = 0.10f;
        P.BiomeBiases.DesertBias = -0.50f;
        P.BiomeBiases.TundraBias = -0.10f;
        break;

    case EPlanetArchetype::Frozen:
        // ====================================================================
        // FROZEN
        // Gelido, neve e calotte dominanti, taiga e tundra marginali.
        // ====================================================================
        P.WaterCoverage = 0.42f;
        P.WaterCoverageMin = 0.35f;
        P.WaterCoverageMax = 0.55f;
        P.TemperatureBias = -0.45f;
        P.HumidityBias = -0.15f;
        P.bSnowAllowed = true;
        P.SnowPotential = 0.95f;
        P.Biodiversity = 0.20f;
        P.BiomeDiversity = 0.45f;
        P.SpecialBiomeAvailability = 0.20f;
        P.BiomeBiases.SnowBias = 0.65f;
        P.BiomeBiases.TundraBias = 0.40f;
        P.BiomeBiases.MountainBias = 0.20f;
        P.BiomeBiases.ForestBias = -0.30f;
        P.BiomeBiases.DesertBias = -0.80f;
        break;

    case EPlanetArchetype::Tundra:
        // ====================================================================
        // TUNDRA
        // Freddo perenne, pianure subpolari, muschi e foreste boreali.
        // ====================================================================
        P.WaterCoverage = 0.45f;
        P.WaterCoverageMin = 0.35f;
        P.WaterCoverageMax = 0.55f;
        P.TemperatureBias = -0.25f;
        P.HumidityBias = -0.10f;
        P.bSnowAllowed = true;
        P.SnowPotential = 0.55f;
        P.Biodiversity = 0.50f;
        P.BiomeDiversity = 0.65f;
        P.SpecialBiomeAvailability = 0.10f;
        P.BiomeBiases.TundraBias = 0.55f;
        P.BiomeBiases.PlainsBias = 0.20f;
        P.BiomeBiases.ForestBias = 0.05f;
        P.BiomeBiases.SnowBias = 0.25f;
        P.BiomeBiases.DesertBias = -0.60f;
        break;

    case EPlanetArchetype::Jungle:
        // ====================================================================
        // JUNGLE
        // Caldo umido pluviale, dense foreste vergini, altissima biodiversita.
        // ====================================================================
        P.WaterCoverage = 0.56f;
        P.WaterCoverageMin = 0.48f;
        P.WaterCoverageMax = 0.65f;
        P.TemperatureBias = 0.20f;
        P.HumidityBias = 0.40f;
        P.bSnowAllowed = false;
        P.SnowPotential = 0.05f;
        P.Biodiversity = 0.95f;
        P.BiomeDiversity = 0.70f;
        P.SpecialBiomeAvailability = 0.20f;
        P.BiomeBiases.ForestBias = 0.65f;
        P.BiomeBiases.GrasslandBias = 0.20f;
        P.BiomeBiases.DesertBias = -0.70f;
        P.BiomeBiases.TundraBias = -0.80f;
        P.BiomeBiases.SnowBias = -0.90f;
        break;

    case EPlanetArchetype::Volcanic:
        // ====================================================================
        // VOLCANIC
        // Calore tellurico elevato, catene montuose vulcaniche, desolazione basaltica.
        // ====================================================================
        P.WaterCoverage = 0.32f;
        P.WaterCoverageMin = 0.30f;
        P.WaterCoverageMax = 0.40f;
        P.TemperatureBias = 0.35f;
        P.HumidityBias = -0.20f;
        P.bSnowAllowed = false;
        P.SnowPotential = 0.05f;
        P.Biodiversity = 0.15f;
        P.BiomeDiversity = 0.50f;
        P.SpecialBiomeAvailability = 0.30f;
        P.BiomeBiases.MountainBias = 0.60f;
        P.BiomeBiases.DesertBias = 0.35f;
        P.BiomeBiases.PlainsBias = 0.10f;
        P.BiomeBiases.ForestBias = -0.70f;
        break;

    case EPlanetArchetype::Rocky:
        // ====================================================================
        // ROCKY
        // Pianeta roccioso sterile, altopiani brulli e falesie.
        // ====================================================================
        P.WaterCoverage = 0.30f;
        P.WaterCoverageMin = 0.30f;
        P.WaterCoverageMax = 0.38f;
        P.TemperatureBias = 0.0f;
        P.HumidityBias = -0.30f;
        P.bSnowAllowed = true;
        P.SnowPotential = 0.20f;
        P.Biodiversity = 0.10f;
        P.BiomeDiversity = 0.40f;
        P.SpecialBiomeAvailability = 0.15f;
        P.BiomeBiases.MountainBias = 0.55f;
        P.BiomeBiases.PlainsBias = 0.35f;
        P.BiomeBiases.DesertBias = 0.20f;
        P.BiomeBiases.ForestBias = -0.60f;
        break;

    case EPlanetArchetype::Exotic:
        // ====================================================================
        // EXOTIC
        // Mondo anomalo e misterioso, elevata varieta speciale ed enigmatica.
        // ====================================================================
        P.WaterCoverage = 0.50f;
        P.WaterCoverageMin = 0.35f;
        P.WaterCoverageMax = 0.65f;
        P.TemperatureBias = 0.0f;
        P.HumidityBias = 0.0f;
        P.bSnowAllowed = true;
        P.SnowPotential = 0.50f;
        P.Biodiversity = 0.75f;
        P.BiomeDiversity = 0.95f;
        P.SpecialBiomeAvailability = 0.85f;
        break;
    }

    return P;
}

EPlanetArchetype UPlanetProfileGenerator::DetermineArchetype(
    int64 PlanetSeed,
    int64 PlanetID,
    float OrbitDistance
)
{
    // Calcolo della distanza orbitale normalizzata [0.0 = orbita piu interna, 1.0 = estremo esterno del sistema]
    // Se la distanza non e specificata (<= 0), usiamo PlanetID come proxy ordinale deterministico.
    float EffectiveDistance = OrbitDistance;
    if (EffectiveDistance <= 0.0f)
    {
        EffectiveDistance = 3500000.0f + static_cast<float>(PlanetID) * 2200000.0f;
    }

    const float NormalizedOrbit = FMath::Clamp(
        (EffectiveDistance - 3500000.0f) / 12000000.0f,
        0.0f,
        1.0f
    );

    // ========================================================================
    // TABELLA DI PESO PROBABILISTICO CON BIAS ORBITALE
    //
    // Ogni archetipo conserva sempre un peso base > 0 (ogni risultato e possibile),
    // ma la distanza orbitale modula fortemente le probabilita relative.
    // ========================================================================

    float Weights[10];

    // 0: Terran (picco nella fascia abitabile centrale ~0.45)
    Weights[0] = 1.0f + 20.0f * FMath::Max(0.0f, 1.0f - FMath::Abs(NormalizedOrbit - 0.45f) / 0.25f);

    // 1: Arid (picco nella transizione interno/temperata ~0.25)
    Weights[1] = 2.0f + 12.0f * FMath::Max(0.0f, 1.0f - FMath::Abs(NormalizedOrbit - 0.25f) / 0.25f);

    // 2: Desert (forte vicino alla stella)
    Weights[2] = 1.0f + 14.0f * FMath::Max(0.0f, 1.0f - NormalizedOrbit / 0.40f);

    // 3: Oceanic (fascia abitabile e medio-esterna ~0.52)
    Weights[3] = 1.0f + 16.0f * FMath::Max(0.0f, 1.0f - FMath::Abs(NormalizedOrbit - 0.52f) / 0.25f);

    // 4: Frozen (forte all'esterno del sistema > 0.50)
    const float FrozenOuter = FMath::Max(0.0f, (NormalizedOrbit - 0.50f) / 0.50f);
    Weights[4] = 1.0f + 22.0f * (FrozenOuter * FrozenOuter);

    // 5: Tundra (fascia esterna fredda ~0.72)
    Weights[5] = 1.0f + 15.0f * FMath::Max(0.0f, 1.0f - FMath::Abs(NormalizedOrbit - 0.72f) / 0.25f);

    // 6: Jungle (fascia abitabile calda interna ~0.38)
    Weights[6] = 1.0f + 12.0f * FMath::Max(0.0f, 1.0f - FMath::Abs(NormalizedOrbit - 0.38f) / 0.20f);

    // 7: Volcanic (molto vicino alla stella < 0.28)
    const float VolcanicInner = FMath::Max(0.0f, 1.0f - NormalizedOrbit / 0.28f);
    Weights[7] = 1.0f + 16.0f * (VolcanicInner * VolcanicInner);

    // 8: Rocky (presente ovunque, leggermente piu frequente verso l'interno)
    Weights[8] = 5.0f + 4.0f * (1.0f - NormalizedOrbit);

    // 9: Exotic (anomalia rara uniforme)
    Weights[9] = 2.5f;

    // ========================================================================
    // SELEZIONE DETERMINISTICA VIA ROULETTE WHEEL
    // ========================================================================

    float TotalWeight = 0.0f;
    for (int32 i = 0; i < 10; ++i)
    {
        TotalWeight += Weights[i];
    }

    const uint64 SelectionHash = HashSeed64(
        static_cast<uint64>(PlanetSeed) ^ (static_cast<uint64>(PlanetID) * 0x9E3779B97F4A7C15ULL),
        0x5A7C8E9B14D2F3ULL
    );

    const float Roll = HashToUnitFloat(SelectionHash) * TotalWeight;

    float Cumulative = 0.0f;
    for (int32 i = 0; i < 10; ++i)
    {
        Cumulative += Weights[i];
        if (Roll <= Cumulative || i == 9)
        {
            return static_cast<EPlanetArchetype>(i);
        }
    }

    return EPlanetArchetype::Terran;
}

FPlanetProfile UPlanetProfileGenerator::GenerateProfile(
    int64 PlanetSeed,
    int64 PlanetID,
    float OrbitDistance
)
{
    const EPlanetArchetype Archetype = DetermineArchetype(
        PlanetSeed,
        PlanetID,
        OrbitDistance
    );

    FPlanetProfile Profile = GetArchetypeDefaultProfile(Archetype);

    // Identita
    Profile.PlanetSeed = PlanetSeed;
    Profile.PlanetID = PlanetID;
    Profile.OrbitDistance = OrbitDistance;

    // ========================================================================
    // VARIAZIONI DETERMINISTICHE SPECIFICHE DEL PIANETA
    //
    // Aggiunge piccole oscillazioni plausibili rispetto al default dell'archetipo,
    // preservando l'identita deterministica unica del pianeta.
    // ========================================================================

    const uint64 VariationHash = HashSeed64(
        static_cast<uint64>(PlanetSeed) ^ (static_cast<uint64>(PlanetID) * 0xBF58476D1CE4E5B9ULL),
        0x83A16C2F7D904BULL
    );

    const float JitterWater = HashToUnitFloat(VariationHash) * 0.08f - 0.04f;
    const float JitterTemp  = HashToUnitFloat(VariationHash >> 8) * 0.06f - 0.03f;
    const float JitterHumid = HashToUnitFloat(VariationHash >> 16) * 0.06f - 0.03f;

    Profile.WaterCoverage = FMath::Clamp(
        Profile.WaterCoverage + JitterWater,
        Profile.WaterCoverageMin,
        Profile.WaterCoverageMax
    );

    Profile.TemperatureBias = FMath::Clamp(
        Profile.TemperatureBias + JitterTemp,
        -0.50f,
        0.50f
    );

    Profile.HumidityBias = FMath::Clamp(
        Profile.HumidityBias + JitterHumid,
        -0.50f,
        0.50f
    );

    return Profile;
}
