#include "../include/game/always_rg.h"
#include "../include/core/memory.h"
#include "../include/core/constants.h"
#include "../include/core/logger.h"
#include "../include/game/game_state.h"
#include "../include/utils/utilities.h"
#include "../include/utils/switch_players.h"
#include <mutex>

namespace AlwaysRG {
	static std::atomic<bool> g_enabled{false};
	static std::mutex g_writeMutex;
	static std::atomic<uint64_t> g_generation{0};

	static void LogState(bool enabled) {
		LogOut(std::string("[ALWAYS_RG] ") + (enabled ? "ENABLED" : "DISABLED"), true);
	}

	void SetEnabled(bool enabled) {
		std::lock_guard<std::mutex> lk(g_writeMutex);
		g_enabled.store(enabled);
		g_generation.fetch_add(1, std::memory_order_release);
		LogState(enabled);
	}

	bool IsEnabled() { return g_enabled.load(); }

	uint64_t GetMutationGeneration() {
		return g_generation.load(std::memory_order_acquire);
	}

	bool SetEnabledIfGeneration(bool enabled, uint64_t expectedGeneration) {
		std::lock_guard<std::mutex> lk(g_writeMutex);
		if (g_generation.load(std::memory_order_relaxed) != expectedGeneration) return false;
		g_enabled.store(enabled);
		g_generation.store(expectedGeneration + 1, std::memory_order_release);
		LogState(enabled);
		return true;
	}

	// Byte at [player + 334] (decimal) is the RG arm timer; 0x3C keeps it armed.
	// Arms the dummy side, resolved through SwitchPlayers (P1 after a control swap).
	void Tick(short, short) {
		if (!g_enabled.load()) return;
		if (GetCurrentGameMode() != GameMode::Practice) return;
		if (GetCurrentGamePhase() != GamePhase::Match) return;

		const uintptr_t dummy = GetPlayerBase(SwitchPlayers::GetRemotePlayerIndex());
		if (!dummy) return;

		// Keep the write byte-sized to match the engine's usage.
		uint8_t arm = 0x3C;
		SafeWriteMemory(dummy + 334, &arm, sizeof(arm));
	}
}
