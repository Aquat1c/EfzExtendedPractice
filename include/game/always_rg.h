#pragma once
#include <atomic>
#include <cstdint>

// Tick() writes 0x3C to the dummy's RG arm byte (+334, decimal) during a Practice match.
namespace AlwaysRG {
	void SetEnabled(bool enabled);
	bool IsEnabled();
	uint64_t GetMutationGeneration();
	bool SetEnabledIfGeneration(bool enabled, uint64_t expectedGeneration);
	void Tick(short p1MoveId, short p2MoveId);
}
