#include "LYTHOS2/Lythos2CubeSphere.h"

namespace Lythos2
{
    namespace CubeSphere
    {
        FVector FaceUVToDirection(int32 Face, float U, float V)
        {
            const float X = U * 2.0f - 1.0f;
            const float Y = V * 2.0f - 1.0f;

            FVector Direction;
            switch (Face)
            {
            case 0: Direction = FVector(1.0f, Y, -X); break;
            case 1: Direction = FVector(-1.0f, Y, X); break;
            case 2: Direction = FVector(X, 1.0f, -Y); break;
            case 3: Direction = FVector(X, -1.0f, Y); break;
            case 4: Direction = FVector(X, Y, 1.0f); break;
            case 5: Direction = FVector(X, -Y, -1.0f); break;
            default: Direction = FVector::UpVector; break;
            }

            return Direction.GetSafeNormal();
        }

        void DirectionToFaceUV(const FVector& Direction, int32& OutFace, float& OutU, float& OutV)
        {
            const FVector D = Direction.GetSafeNormal();
            const float AX = FMath::Abs(D.X);
            const float AY = FMath::Abs(D.Y);
            const float AZ = FMath::Abs(D.Z);

            if (AX >= AY && AX >= AZ)
            {
                if (D.X >= 0.0f)
                {
                    OutFace = 0;
                    const float A = D.X;
                    OutU = 0.5f * (1.0f - D.Z / A);
                    OutV = 0.5f * (1.0f + D.Y / A);
                }
                else
                {
                    OutFace = 1;
                    const float A = -D.X;
                    OutU = 0.5f * (1.0f + D.Z / A);
                    OutV = 0.5f * (1.0f + D.Y / A);
                }
            }
            else if (AY >= AX && AY >= AZ)
            {
                if (D.Y >= 0.0f)
                {
                    OutFace = 2;
                    const float A = D.Y;
                    OutU = 0.5f * (1.0f + D.X / A);
                    OutV = 0.5f * (1.0f - D.Z / A);
                }
                else
                {
                    OutFace = 3;
                    const float A = -D.Y;
                    OutU = 0.5f * (1.0f + D.X / A);
                    OutV = 0.5f * (1.0f + D.Z / A);
                }
            }
            else
            {
                if (D.Z >= 0.0f)
                {
                    OutFace = 4;
                    const float A = D.Z;
                    OutU = 0.5f * (1.0f + D.X / A);
                    OutV = 0.5f * (1.0f + D.Y / A);
                }
                else
                {
                    OutFace = 5;
                    const float A = -D.Z;
                    OutU = 0.5f * (1.0f + D.X / A);
                    OutV = 0.5f * (1.0f - D.Y / A);
                }
            }

            OutU = FMath::Clamp(OutU, 0.0f, 1.0f);
            OutV = FMath::Clamp(OutV, 0.0f, 1.0f);
        }

        FVector RegionCenterDirection(const FLythos2RegionKey& Key)
        {
            const int32 Side = Key.GetSide();
            const float U = (Key.X + 0.5f) / static_cast<float>(Side);
            const float V = (Key.Y + 0.5f) / static_cast<float>(Side);
            return FaceUVToDirection(Key.Face, U, V);
        }

        FVector RegionSampleDirection(const FLythos2RegionKey& Key, float LocalU, float LocalV)
        {
            const int32 Side = Key.GetSide();
            const float U = (Key.X + LocalU) / static_cast<float>(Side);
            const float V = (Key.Y + LocalV) / static_cast<float>(Side);
            return FaceUVToDirection(Key.Face, U, V);
        }

        double RegionWorldSizeCm(const FLythos2RegionKey& Key, const FLythos2PlanetContext& Context)
        {
            const double FaceArc = (PI * 0.5) * static_cast<double>(Context.RadiusCm);
            return FaceArc / static_cast<double>(Key.GetSide());
        }

        FVector RegionCenterLocal(const FLythos2RegionKey& Key, const FLythos2PlanetContext& Context)
        {
            return RegionCenterDirection(Key) * Context.RadiusCm;
        }
    }
}
