#pragma once

#include "CoreMinimal.h"

/**
 * HILLAIRE ATMOSPHERE - CONTENT HASHING (Phase 1).
 *
 * Mirrors the reference hashing contract (PlanetState.cpp):
 * FNV-1a over bytes, seeded with the 64-bit offset basis; floats are
 * quantized so sub-authoring-noise jitter does not invalidate LUTs.
 *
 * Hash VALUES are not bit-compared across implementations (the UE profile
 * struct names fields explicitly while the reference hashes raw layer bytes);
 * what is preserved is the CONTRACT: which fields participate, at which
 * quanta, and the rule "hashes are COMPARED, never just gated on Valid flags".
 */
namespace HillaireHash
{
	constexpr uint64 OffsetBasis = 1469598103934665603ull;
	constexpr uint64 Prime = 1099511628211ull;

	FORCEINLINE uint64 HashBytes(const void* Data, SIZE_T Size, uint64 Seed)
	{
		const uint8* Bytes = reinterpret_cast<const uint8*>(Data);
		uint64 H = Seed;
		for (SIZE_T i = 0; i < Size; ++i)
		{
			H ^= uint64(Bytes[i]);
			H *= Prime;
		}
		return H;
	}

	FORCEINLINE uint64 HashFloat(float Value, float Quantum, uint64 Seed)
	{
		const int64 Q = (int64)FMath::FloorToDouble((double)Value / (double)Quantum + 0.5);
		return HashBytes(&Q, sizeof(Q), Seed);
	}

	FORCEINLINE uint64 HashFloat3(float X, float Y, float Z, float Quantum, uint64 Seed)
	{
		Seed = HashFloat(X, Quantum, Seed);
		Seed = HashFloat(Y, Quantum, Seed);
		Seed = HashFloat(Z, Quantum, Seed);
		return Seed;
	}

	FORCEINLINE uint64 HashVector(const FVector& V, double Quantum, uint64 Seed)
	{
		Seed = HashFloat((float)V.X, (float)Quantum, Seed);
		Seed = HashFloat((float)V.Y, (float)Quantum, Seed);
		Seed = HashFloat((float)V.Z, (float)Quantum, Seed);
		return Seed;
	}
}
