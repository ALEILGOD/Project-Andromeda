#include "AndromedaSeedLibrary.h"

namespace
{
    uint64 HashUInt64(uint64 Seed)
    {
        Seed += 0x9E3779B97F4A7C15ULL;

        Seed = (Seed ^ (Seed >> 30))
            * 0xBF58476D1CE4E5B9ULL;

        Seed = (Seed ^ (Seed >> 27))
            * 0x94D049BB133111EBULL;

        return Seed ^ (Seed >> 31);
    }

    int64 MakePositiveSeed(uint64 Seed)
    {
        return static_cast<int64>(
            Seed & 0x7FFFFFFFFFFFFFFFULL
            );
    }
}

int64 UAndromedaSeedLibrary::HashSeed(int64 Seed)
{
    return MakePositiveSeed(
        HashUInt64(
            static_cast<uint64>(Seed)
        )
    );
}

int64 UAndromedaSeedLibrary::CombineSeeds(
    int64 ParentSeed,
    int64 ChildID
)
{
    const uint64 Parent =
        static_cast<uint64>(ParentSeed);

    const uint64 Child =
        static_cast<uint64>(ChildID);

    const uint64 Combined =
        HashUInt64(
            Parent ^
            HashUInt64(Child)
        );

    return MakePositiveSeed(Combined);
}

int64 UAndromedaSeedLibrary::GenerateGalaxySeed(
    int64 UniverseSeed,
    int64 GalaxyX,
    int64 GalaxyY,
    int64 GalaxyZ
)
{
    int64 Seed = HashSeed(UniverseSeed);

    Seed = CombineSeeds(Seed, GalaxyX);
    Seed = CombineSeeds(Seed, GalaxyY);
    Seed = CombineSeeds(Seed, GalaxyZ);

    return Seed;
}

int64 UAndromedaSeedLibrary::GenerateSystemSeed(
    int64 GalaxySeed,
    int64 SystemX,
    int64 SystemY,
    int64 SystemZ
)
{
    int64 Seed = HashSeed(GalaxySeed);

    Seed = CombineSeeds(Seed, SystemX);
    Seed = CombineSeeds(Seed, SystemY);
    Seed = CombineSeeds(Seed, SystemZ);

    return Seed;
}

int64 UAndromedaSeedLibrary::GeneratePlanetSeed(
    int64 SystemSeed,
    int64 PlanetID
)
{
    return CombineSeeds(
        SystemSeed,
        PlanetID
    );
}

int64 UAndromedaSeedLibrary::GenerateSubsystemSeed(
    int64 PlanetSeed,
    int64 SubsystemID
)
{
    return CombineSeeds(
        PlanetSeed,
        SubsystemID
    );
}