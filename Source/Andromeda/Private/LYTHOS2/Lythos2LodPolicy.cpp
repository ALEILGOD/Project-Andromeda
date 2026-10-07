#include "LYTHOS2/Lythos2LodPolicy.h"

#include "LYTHOS2/Lythos2CubeSphere.h"

namespace
{
    double IdealLodForFactor(double DistanceCm, double RadiusCm, double Factor, int32 MaxLod)
    {
        double Size = (PI * 0.5) * RadiusCm;
        int32 Lod = 0;
        const double SafeDistance = FMath::Max(0.0, DistanceCm);
        while (Lod < MaxLod && SafeDistance < Size * Factor)
        {
            Size *= 0.5;
            ++Lod;
        }
        return Lod;
    }

    struct FLeafCandidate
    {
        FLythos2RegionKey Key;
        double Score = 0.0;

        bool operator<(const FLeafCandidate& Other) const
        {
            if (Score != Other.Score)
            {
                return Score < Other.Score;
            }
            if (Key.PlanetID != Other.Key.PlanetID) { return Key.PlanetID < Other.Key.PlanetID; }
            if (Key.Face != Other.Key.Face) { return Key.Face < Other.Key.Face; }
            if (Key.Lod != Other.Key.Lod) { return Key.Lod < Other.Key.Lod; }
            if (Key.X != Other.Key.X) { return Key.X < Other.Key.X; }
            return Key.Y < Other.Key.Y;
        }
    };
}

namespace Lythos2
{
    namespace Lod
    {
        double RegionSizeCm(const FLythos2RegionKey& Key, const FLythos2PlanetContext& Context)
        {
            return CubeSphere::RegionWorldSizeCm(Key, Context);
        }

        double DistanceToRegionCm(
            const FVector& ViewerLocalCm,
            const FLythos2RegionKey& Key,
            const FLythos2PlanetContext& Context)
        {
            const FVector Center = CubeSphere::RegionCenterLocal(Key, Context);
            const double ApproxRadius = RegionSizeCm(Key, Context) * 0.75;
            const double Raw = FVector::Dist(ViewerLocalCm, Center);
            return FMath::Max(0.0, Raw - ApproxRadius);
        }

        int32 IdealLodForDistanceCm(
            double DistanceCm,
            const FLythos2PlanetContext& Context,
            const FLythos2Settings& Settings)
        {
            return static_cast<int32>(IdealLodForFactor(
                DistanceCm,
                Context.RadiusCm,
                Settings.LodDetailFactor,
                Settings.MaxTerrainLOD));
        }

        int32 ApplyHysteresis(
            int32 CurrentLod,
            int32 DesiredLod,
            double DistanceCm,
            const FLythos2PlanetContext& Context,
            const FLythos2Settings& Settings)
        {
            const double Hyst = FMath::Clamp(Settings.LodHysteresis, 0.0f, 0.5f);

            if (DesiredLod > CurrentLod)
            {
                // Refining: require the viewer to be clearly closer.
                const double Allowed = IdealLodForFactor(
                    DistanceCm, Context.RadiusCm,
                    Settings.LodDetailFactor / (1.0 + Hyst),
                    Settings.MaxTerrainLOD);
                return FMath::Min(DesiredLod, FMath::Max(CurrentLod, static_cast<int32>(Allowed)));
            }

            if (DesiredLod < CurrentLod)
            {
                // Coarsening: keep detail a little longer.
                const double Allowed = IdealLodForFactor(
                    DistanceCm, Context.RadiusCm,
                    Settings.LodDetailFactor * (1.0 + Hyst),
                    Settings.MaxTerrainLOD);
                return FMath::Max(DesiredLod, FMath::Min(CurrentLod, static_cast<int32>(Allowed)));
            }

            return CurrentLod;
        }

        double RegionPriorityCm(
            const FVector& ViewerLocalCm,
            const FLythos2RegionKey& Key,
            const FLythos2PlanetContext& Context)
        {
            return DistanceToRegionCm(ViewerLocalCm, Key, Context);
        }

        void SelectRegions(
            int64 PlanetID,
            const FVector& ViewerLocalCm,
            const FLythos2PlanetContext& Context,
            const FLythos2Settings& Settings,
            TArray<FLythos2RegionKey>& OutLeaves,
            const TSet<FLythos2RegionKey>* PreviouslyRefinedNodes)
        {
            OutLeaves.Reset();

            const int32 Capacity = FMath::Max(CubeSphere::NumRootRegions, Settings.MaxActiveRegions);
            const double Hyst = FMath::Clamp(Settings.LodHysteresis, 0.0f, 0.5f);

            auto MakeCandidate = [&](const FLythos2RegionKey& Key) -> FLeafCandidate
            {
                FLeafCandidate Candidate;
                Candidate.Key = Key;
                const double Size = FMath::Max(1.0, RegionSizeCm(Key, Context));
                const double Distance = DistanceToRegionCm(ViewerLocalCm, Key, Context);
                Candidate.Score = Distance / Size;
                return Candidate;
            };

            auto Less = [](const FLeafCandidate& A, const FLeafCandidate& B) { return A < B; };

            TArray<FLeafCandidate> Pending;
            Pending.Reserve(64);
            for (int32 Face = 0; Face < CubeSphere::NumFaces; ++Face)
            {
                FLythos2RegionKey Root(Face, 0, 0, 0);
                Root.PlanetID = PlanetID;
                Pending.Add(MakeCandidate(Root));
            }
            Pending.Heapify(Less);

            OutLeaves.Reserve(Capacity);

            while (Pending.Num() > 0)
            {
                FLeafCandidate Top;
                Pending.HeapPop(Top, Less);

                // LOD hysteresis: a node that was already refined stays refined
                // unless the viewer clearly leaves; a leaf only refines once the
                // viewer clearly enters. This prevents N <-> N+1 thrashing.
                const bool bWasRefined =
                    PreviouslyRefinedNodes != nullptr && PreviouslyRefinedNodes->Contains(Top.Key);
                const double Threshold = Settings.LodDetailFactor
                    * (bWasRefined ? (1.0 + Hyst) : (1.0 - Hyst));

                const bool bWantsDetail =
                    Top.Key.Lod < Settings.MaxTerrainLOD
                    && Top.Score < Threshold;

                const bool bBudgetAllows =
                    (OutLeaves.Num() + Pending.Num() + 3) <= Capacity;

                if (bWantsDetail && bBudgetAllows)
                {
                    for (int32 CY = 0; CY < 2; ++CY)
                    {
                        for (int32 CX = 0; CX < 2; ++CX)
                        {
                            FLythos2RegionKey Child(Top.Key.Face, Top.Key.Lod + 1, Top.Key.X * 2 + CX, Top.Key.Y * 2 + CY);
                            Child.PlanetID = Top.Key.PlanetID;
                            Pending.HeapPush(MakeCandidate(Child), Less);
                        }
                    }
                }
                else
                {
                    OutLeaves.Add(Top.Key);
                }
            }
        }
    }
}
