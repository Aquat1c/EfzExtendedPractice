#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include "efz_netplay_state.h"
#include "efz_revival_profile.h"

extern std::atomic<bool> isOnlineMatch;

enum class NetplayStateSource : int {
	None = 0,
	ExportSharedMemory,
	ExportDll,
	LegacyRevival
};

enum class EfzRevivalVersion : int {
	Unknown = 0,
	Vanilla,
	Revival102f,
	Revival102e,
	Revival102g,
	Revival102h,
	Revival102i,
	Other,       // Some other Revival build. Numeric values are persisted: never reorder or renumber.
	Revival102j
};

enum class EfzRevivalDllFlavor : int {
	Unknown = 0,
	Standard,
	Revival102fClassic,
	Revival102fSubframe
};

// Online state reported by EfzRevival.dll flag (0=netplay, 1=spectating, 2=offline)
enum class OnlineState : int {
	Netplay = 0,
	Spectating = 1,
	Offline = 2,
	Tournament = 3,
	Unknown = -1
};

struct NetplayRuntimeState {
	DWORD refreshTick;
	bool exportAvailable;
	bool sessionActive;
	bool suspendTraining;
	bool inNetplayMenu;
	bool inNetplayFlow;
	bool inNetplayCharacterSelect;
	bool inNetplayMatch;
	NetplayStateSource source;
	OnlineState legacyOnlineState;
	EFZNetplayState exportState;
};

bool DetectOnlineMatch();

EfzRevivalVersion GetEfzRevivalVersion();
EfzRevivalDllFlavor GetEfzRevivalDllFlavor();
bool IsEfzRevival102fSubframeBuild();
bool IsEfzRevival102fClassicBuild();
EfzRevival102jBuild GetEfzRevival102jBuild();
// True only for a supported exact 1.02j PE profile. J moved code, globals, vtable slots and
// Practice fields, so title detection alone does not permit using its RVAs.
bool IsEfzRevival102jVerifiedBuild();
const char* EfzRevivalVersionName(EfzRevivalVersion v);
const char* EfzRevivalDllFlavorName(EfzRevivalDllFlavor flavor);
const char* NetplayStateSourceName(NetplayStateSource source);
// v = 0 uses the detected version
bool IsEfzRevivalVersionSupported(EfzRevivalVersion v = (EfzRevivalVersion)0);


OnlineState ReadEfzRevivalOnlineState();
const char* OnlineStateName(OnlineState st);

void RefreshNetplayRuntimeState();
NetplayRuntimeState GetNetplayRuntimeState();
bool IsNetplaySuspendActive();
bool IsNetplaySessionActive();
bool IsNetplayMenuActive();
bool IsNetplayFlowActive();
bool IsNetplayExportAvailable();

std::string GetLastOnlineDetectionReason();
