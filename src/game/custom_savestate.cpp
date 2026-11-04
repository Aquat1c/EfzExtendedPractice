#include "../../include/game/custom_savestate.h"

#include "../../include/game/auto_action.h"
#include "../../include/game/character_hotswap.h"
#include "../../include/game/character_settings.h"
#include "../../include/game/collision_hook.h"
#include "../../include/game/combo_overlay.h"
#include "../../include/game/savestate_hook.h"
#include "../../include/game/efzrevival_addrs.h"
#include "../../include/game/game_state.h"
#include "../../include/game/mission/savestate_entity_layout.h"
#include "../../include/game/macro_controller.h"
#include "../../include/game/practice_offsets.h"
#include "../../include/core/constants.h"
#include "../../include/core/logger.h"
#include "../../include/core/memory.h"
#include "../../include/input/framestep.h"
#include "../../include/gui/overlay.h"
#include "../../include/utils/bgm_control.h"
#include "../../include/utils/config.h"
#include "../../include/utils/network.h"
#include "../../include/utils/pause_integration.h"
#include "../../include/utils/switch_players.h"
#include "../../include/utils/utilities.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr uintptr_t kBattleContextArrayOffset = 0x00390110;
constexpr uintptr_t kBattleContextArrayIndex = 3;
constexpr uintptr_t kFKeyLatchOffset = 0x003B1290;
constexpr uintptr_t kRenderBitmapOffset = 0x003B0964;
constexpr uintptr_t kCharacterIdOffset = 141;
// Revival's second battle-context region begins at BYTE +1088 (not dword-index 272).
constexpr uintptr_t kCameraSubfieldOffset = 1088;
constexpr uintptr_t kBattleContextGameSpeedOffset = 1400;
constexpr uintptr_t kGameStateStageOffset = 3890;
constexpr size_t kFpuStateSize = 108;
constexpr size_t kBattleContextSize = 0x434;
constexpr size_t kBattleContextRuntimeControlHeaderOffset = 4;
constexpr size_t kBattleContextRuntimeControlHeaderSize = sizeof(uint32_t) * 2;
constexpr size_t kBattleContextRuntimePointerBlockOffset = 12;
constexpr size_t kBattleContextRuntimePointerBlockSize = sizeof(uint32_t) * 6;
constexpr size_t kBattleContextInitFlagOffset = 44;
constexpr size_t kBattleContextCleanupFlagOffset = 45;
constexpr size_t kBattleContextRuntimeFlagsSize = 2;
constexpr size_t kBattleContextFrameCounterOffset = 1076;
constexpr size_t kBattleContextCameraXOffset = 1088;
constexpr size_t kBattleContextCameraYOffset = 1092;
constexpr size_t kBattleContextRoundCountOffset = 1396;
constexpr size_t kBattleContextRoundTimerOffset = 1398;
constexpr size_t kBattleContextPauseFlagOffset = 1416;
constexpr size_t kCameraSubfieldSize = 0x150;
constexpr size_t kGameStateSize = 0x142F0;
constexpr size_t kGameStateManagerPointerRangeSize = sizeof(uint32_t) * 3;
constexpr size_t kGameStateResourcePointerTableOffset = 56;
constexpr size_t kGameStateResourcePointerTableSize = (920u * sizeof(uint32_t)) + (3u * sizeof(uint32_t));
constexpr size_t kRenderBitmapSize = 0x400;
constexpr bool kSelectiveDiskSnapshotGameStateRestore = true;
constexpr size_t kGameStateRuntimeOwnedPrefixSize = 3748;
constexpr size_t kGameStateSoundIdTableOffset = 3748;
constexpr size_t kGameStateSoundIdTableSize = 132; // 65 system SE words + BGM word
constexpr size_t kGameStateHeapBufferOffset = 4988;
constexpr size_t kGameStateHeapBufferSize = sizeof(uint32_t);
constexpr size_t kGameStateRoundEventOffset = 4944;
constexpr size_t kGameStateRoundDurationOffset = 4946;
constexpr size_t kGameStateRoundGateOffset = 4948;
constexpr size_t kGameStateResourceObjectOffset = 82440;
constexpr size_t kGameStateResourceObjectSize = sizeof(uint32_t);
constexpr size_t kGameStateRoundUiCounterOffset = 82444;
constexpr size_t kGameStateRoundUiCounter2Offset = 82446;
constexpr size_t kGameStateExternalSpeedTriggerOffset = 82556;
constexpr size_t kGameStateExternalEventTriggerOffset = 82560;
constexpr size_t kGameStateReplayIoOffset = 82563;
constexpr size_t kGameStateReplayIoSize = 5;
constexpr size_t kGameStateP1CustomPaletteFlagOffset = 4920;
constexpr size_t kGameStateP2CustomPaletteFlagOffset = 4924;
constexpr size_t kPlayerStateOpponentPtrOffset = sizeof(uint32_t) * 30;
constexpr size_t kPlayerStateGameStatePtrOffset = sizeof(uint32_t) * 31;
constexpr size_t kPlayerStateRenderPtrOffset = sizeof(uint32_t) * 32;
constexpr size_t kPlayerStateInputPtrOffset = sizeof(uint32_t) * 33;
constexpr size_t kPlayerStateSoundPtrOffset = sizeof(uint32_t) * 34;
constexpr size_t kPlayerStateRuntimePointerBlockOffset = kPlayerStateOpponentPtrOffset;
constexpr size_t kPlayerStateRuntimePointerBlockSize = (kPlayerStateSoundPtrOffset + sizeof(uint32_t)) - kPlayerStateOpponentPtrOffset;
constexpr size_t kPlayerStateAnimationDataTableOffset = 16;
constexpr size_t kPlayerStateImageSurfaceArrayOffset = 20;
constexpr size_t kPlayerStatePlayerIndexOffset = 140;
constexpr size_t kPlayerStateIdentityByteOffset = 141;
constexpr size_t kPlayerStatePaletteIndexOffset = 142;
constexpr size_t kPlayerStateCollisionDataTableOffset = 356;
constexpr size_t kPlayerStateSoundIdTableOffset = 612;
constexpr size_t kPlayerStateSoundIdTableSize = 100; // 50 character SE words
constexpr uintptr_t kGameStateSoundManagerOffset = 8;
constexpr uintptr_t kCharacterSoundManagerOffset = 136;
constexpr uintptr_t kSoundManagerBufferTableOffset = 1216;
constexpr uintptr_t kSoundManagerBgmBufferIndexOffset = 0xF26;
constexpr size_t kSoundManagerBufferCount = 150;
constexpr uint16_t kInvalidSoundBufferIndex = 150;
constexpr uint32_t kDirectSoundStatusPlaying = 0x1;
constexpr uint32_t kDirectSoundStatusBufferLost = 0x2;
constexpr uint32_t kDirectSoundStatusLooping = 0x4;
constexpr size_t kMaxSerializedSoundEntries = kSoundManagerBufferCount * 3;
constexpr uint16_t kActiveRoundDurationValue = 300;
constexpr int kInitialSnapshotSlot = 0;
constexpr int kMaxDiskSlots = 8;
constexpr int kPostRestoreStabilizationFrames = 6;
constexpr int kDeferredPracticeSideRestoreDelayFrames = 24;
constexpr int kDeferredPracticeSideRestoreRetryFrames = 48;
constexpr uint32_t kFileVersion1 = 1;
constexpr uint32_t kFileVersion2 = 2;
constexpr uint32_t kFileVersion3 = 3;
constexpr uint32_t kFileVersion4 = 4;

constexpr size_t kP1MaxHpOffset = HP_BAR_OFFSET;
constexpr size_t kP1MeterSize = sizeof(uint16_t);

constexpr uint32_t kFileMagicA = 0x43535A45; // EZSC
constexpr uint32_t kFileMagicB = 0x3156544D; // MTV1

static const uint32_t kCharacterStateSizes[25] = {
    0x3470, 0x3450, 0x3458, 0x3450, 0x3450,
    0x3448, 0x34B0, 0x3448, 0x3458, 0x3458,
    0x3460, 0x3458, 0x3468, 0x3450, 0x3478,
    0x3478, 0x3468, 0x3458, 0x3460, 0x3458,
    0x3478, 0x3458, 0x3460, 0x3488, 0x3460,
};

const char* const kStageFileTokens[] = {
    "courtyard_full_moon",
    "snowy_park_night",
    "lunch_break_courtyard",
    "school_road_park_day",
    "school_road_park_night",
    "sunset_rooftop",
    "shopping_street",
    "tree_of_beginnings",
    "minase_house_day",
    "gymnasium",
    "rainy_field",
    "behind_the_school",
    "world_of_eternity",
    "abandoned_station_day",
    "shrine_near_sky_day",
    "kamio_house",
    "infinite_sky",
    "minase_house_night",
    "abandoned_station_dusk",
    "fargo_research_facility",
    "monomi_hill",
    "shrine_near_sky_night",
    "snowy_park_day",
};

#pragma pack(push, 1)
struct PersistedModState {
    uint32_t valid;
    uint32_t p2ControlWasOverridden;
    uint32_t originalP2ControlFlag;
    uint8_t p1CpuFlag;
    uint8_t p2CpuFlag;
    int32_t localSide;
    int32_t macroState;
    int32_t macroSlot;
};

enum class SoundOwner : uint8_t {
    Common = 0,
    P1 = 1,
    P2 = 2,
};

struct SoundPlaybackEntry {
    uint8_t owner;
    uint8_t reserved;
    uint16_t bufferIndex;
    uint32_t status;
    uint32_t playCursor;
};

struct SnapshotFileHeader {
    uint32_t magicA;
    uint32_t magicB;
    uint32_t version;
    uint32_t savedFKeyLatch;
    uint32_t p1CharId;
    uint32_t p2CharId;
    uint32_t fpuStateSize;
    uint32_t battleContextSize;
    uint32_t cameraSubfieldSize;
    uint32_t gameStateSize;
    uint32_t p1StateSize;
    uint32_t p2StateSize;
    uint32_t renderBitmapSize;
    uint32_t modStateSize;
};
#pragma pack(pop)

struct Snapshot {
    bool valid = false;
    bool dirty = false;
    bool fromDisk = false;
    bool hasSoundState = false;
    uint8_t p1CharId = 0xFF;
    uint8_t p2CharId = 0xFF;
    uint8_t stageId = 0xFF;
    uint16_t bgmTrack = 0;
    uint32_t revivalVersion = 0;
    uint32_t savedFKeyLatch = 0;
    uint8_t fpuState[kFpuStateSize] = {};
    PersistedModState modState{};
    std::vector<uint8_t> battleContext;
    std::vector<uint8_t> cameraSubfield;
    std::vector<uint8_t> gameState;
    std::vector<uint8_t> p1State;
    std::vector<uint8_t> p2State;
    std::vector<uint8_t> renderBitmap;
    std::vector<SoundPlaybackEntry> soundState;
};

std::atomic<bool> s_installed{false};
std::atomic<unsigned int> s_saveCount{0};
std::atomic<unsigned int> s_loadCount{0};
std::atomic<unsigned int> s_workingSnapshotStamp{0};
std::atomic<int> s_activeDiskSlot{kInitialSnapshotSlot};
std::atomic<bool> s_hotswapRestoreApplied{false};
std::atomic<bool> s_restoreInProgress{false};
std::atomic<int> s_postRestoreStabilizationFrames{0};
enum class SideRestorePhase { Idle, Delaying, Retrying };
std::atomic<SideRestorePhase> s_sideRestorePhase{SideRestorePhase::Idle};
std::atomic<int> s_deferredPracticeSideRestoreLocalSide{-1};
std::atomic<int> s_deferredPracticeSideRestoreDelay{0};
std::atomic<int> s_deferredPracticeSideRestoreRetries{0};
std::mutex s_snapshotMutex;
std::mutex s_statusMutex;
Snapshot s_initialSnapshot;
Snapshot s_workingSnapshot;
Snapshot s_pendingHotswapRestoreSnapshot;
std::string s_lastStatus{"Working snapshot is empty"};

enum class SnapshotActionResult {
    Success = 0,
    NoSnapshot,
    GuardBlocked,
    RuntimeFailure,
};

enum class DeferredRestoreKind : int {
    None = 0,
    WorkingSnapshot = 1,
    SelectedSlot = 2,
};

std::atomic<DeferredRestoreKind> s_deferredRestoreKind{DeferredRestoreKind::None};
std::atomic<int> s_deferredRestoreSlot{kInitialSnapshotSlot};

struct SehFailure {
    DWORD code = 0;
    uintptr_t exceptionAddress = 0;
    DWORD numberParameters = 0;
    ULONG_PTR info[EXCEPTION_MAXIMUM_PARAMETERS] = {};
};

void RestoreFpuState(const uint8_t* src);
void RestoreModState(const PersistedModState& state, bool engineOnlyLocalSideRestore = false);
void LogSavestateTrace(const char* step, const std::string& detail);

void BeginDeferredPracticeSideRestoreRetry() {
    auto expected = SideRestorePhase::Delaying;
    (void)s_sideRestorePhase.compare_exchange_strong(
        expected, SideRestorePhase::Retrying, std::memory_order_release,
        std::memory_order_relaxed);
}

void ClearDeferredPracticeSideRestoreState() {
    s_sideRestorePhase.store(SideRestorePhase::Idle, std::memory_order_release);
    s_deferredPracticeSideRestoreLocalSide.store(-1, std::memory_order_release);
    s_deferredPracticeSideRestoreDelay.store(0, std::memory_order_release);
    s_deferredPracticeSideRestoreRetries.store(0, std::memory_order_release);
}

bool ShouldDeferPracticeSideRestore() {
    return GetModuleHandleA("EfzRevival.dll") != nullptr && IsEfzRevivalVersionSupported();
}

void ArmDeferredPracticeSideRestore(int localSide) {
    if (localSide != 0 && localSide != 1) {
        ClearDeferredPracticeSideRestoreState();
        return;
    }

    if (!ShouldDeferPracticeSideRestore()) {
        ClearDeferredPracticeSideRestoreState();
        return;
    }

    s_deferredPracticeSideRestoreLocalSide.store(localSide, std::memory_order_release);
    s_deferredPracticeSideRestoreDelay.store(kDeferredPracticeSideRestoreDelayFrames, std::memory_order_release);
    s_deferredPracticeSideRestoreRetries.store(kDeferredPracticeSideRestoreRetryFrames, std::memory_order_release);
    s_sideRestorePhase.store(SideRestorePhase::Delaying, std::memory_order_release);

    std::ostringstream oss;
    oss << "localSide=" << localSide
        << " delay=" << kDeferredPracticeSideRestoreDelayFrames
        << " retries=" << kDeferredPracticeSideRestoreRetryFrames;
    LogSavestateTrace("restore practice reconcile arm", oss.str());
}

bool IsOnlineBlocked() {
    return g_onlineModeActive.load(std::memory_order_relaxed);
}

uint32_t CurrentRevivalVersionValue() {
    return static_cast<uint32_t>(GetEfzRevivalVersion());
}

void SetLastStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(s_statusMutex);
    s_lastStatus = status;
}

void MarkWorkingSnapshotChangedLocked() {
    s_workingSnapshotStamp.fetch_add(1, std::memory_order_relaxed);
}

void AssignWorkingSnapshotLocked(Snapshot snapshot) {
    s_workingSnapshot = std::move(snapshot);
    s_pendingHotswapRestoreSnapshot = Snapshot{};
    MarkWorkingSnapshotChangedLocked();
}

void ClearWorkingSnapshotLocked() {
    s_workingSnapshot = Snapshot{};
    s_pendingHotswapRestoreSnapshot = Snapshot{};
    MarkWorkingSnapshotChangedLocked();
}

void ShowSnapshotMessage(const char* text, COLORREF color) {
    DirectDrawHook::AddMessage(text, "savestate", color, 1500, 0, 100);
}

void LogSnapshotFailure(const char* operation, const std::string& reason) {
    std::ostringstream oss;
    oss << "[SAVESTATE] " << operation << " failed: " << reason;
    SetLastStatus(oss.str());
    LogOut(oss.str(), true);
}

void LogSnapshotStatus(const char* operation, const std::string& detail) {
    std::ostringstream oss;
    oss << "[SAVESTATE] " << operation << ": " << detail;
    SetLastStatus(oss.str());
    LogOut(oss.str(), true);
}

const char* StageTokenForId(uint8_t stageId) {
    constexpr size_t kStageTokenCount = sizeof(kStageFileTokens) / sizeof(kStageFileTokens[0]);
    return stageId < kStageTokenCount ? kStageFileTokens[stageId] : "unknown_stage";
}

std::string PointerHex(uintptr_t value) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << value;
    return oss.str();
}

std::string DescribeSnapshot(const Snapshot& snapshot) {
    std::ostringstream oss;
    oss << "valid=" << (snapshot.valid ? 1 : 0)
        << " dirty=" << (snapshot.dirty ? 1 : 0)
        << " p1=" << CharacterHotswap::GetDisplayNameForSelectId(snapshot.p1CharId) << "(" << static_cast<unsigned int>(snapshot.p1CharId)
        << ",size=" << snapshot.p1State.size() << ")"
        << " p2=" << CharacterHotswap::GetDisplayNameForSelectId(snapshot.p2CharId) << "(" << static_cast<unsigned int>(snapshot.p2CharId)
        << ",size=" << snapshot.p2State.size() << ")"
        << " stage=" << StageTokenForId(snapshot.stageId) << "(" << static_cast<unsigned int>(snapshot.stageId) << ")"
        << " bgm=" << snapshot.bgmTrack
        << " ver=" << snapshot.revivalVersion
        << " bcSize=" << snapshot.battleContext.size()
        << " gsSize=" << snapshot.gameState.size()
        << " rbSize=" << snapshot.renderBitmap.size()
        << " sound=";
    if (snapshot.hasSoundState) {
        oss << snapshot.soundState.size();
    } else {
        oss << "n/a";
    }
    return oss.str();
}

std::string DescribeResolvedPair(uintptr_t p1Base,
                                 uintptr_t p2Base,
                                 uint8_t p1CharId,
                                 uint8_t p2CharId,
                                 size_t p1Size,
                                 size_t p2Size) {
    std::ostringstream oss;
    oss << "p1Base=" << PointerHex(p1Base)
        << " p2Base=" << PointerHex(p2Base)
        << " p1=" << CharacterHotswap::GetDisplayNameForSelectId(p1CharId) << "(" << static_cast<unsigned int>(p1CharId)
        << ",size=" << p1Size << ")"
        << " p2=" << CharacterHotswap::GetDisplayNameForSelectId(p2CharId) << "(" << static_cast<unsigned int>(p2CharId)
        << ",size=" << p2Size << ")";
    return oss.str();
}

void LogSavestateTrace(const char* step, const std::string& detail) {
    std::ostringstream oss;
    oss << "[SAVESTATE][TRACE] " << step;
    if (!detail.empty()) {
        oss << ": " << detail;
    }
    LogOut(oss.str(), true);
}

const char* DeferredRestoreKindName(DeferredRestoreKind kind) {
    switch (kind) {
    case DeferredRestoreKind::WorkingSnapshot:
        return "working";
    case DeferredRestoreKind::SelectedSlot:
        return "slot";
    case DeferredRestoreKind::None:
    default:
        return "none";
    }
}

std::string Hex32(uint32_t value) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << value;
    return oss.str();
}

int CaptureSehFailure(SehFailure* outFailure, EXCEPTION_POINTERS* exceptionPointers) {
    if (!outFailure) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    *outFailure = SehFailure{};
    if (!exceptionPointers || !exceptionPointers->ExceptionRecord) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    const EXCEPTION_RECORD* record = exceptionPointers->ExceptionRecord;
    outFailure->code = record->ExceptionCode;
    outFailure->exceptionAddress = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
    outFailure->numberParameters = std::min<DWORD>(record->NumberParameters, EXCEPTION_MAXIMUM_PARAMETERS);
    for (DWORD index = 0; index < outFailure->numberParameters; ++index) {
        outFailure->info[index] = record->ExceptionInformation[index];
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

const char* SavestateExceptionCodeName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND: return "EXCEPTION_FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
    case EXCEPTION_FLT_UNDERFLOW: return "EXCEPTION_FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION: return "EXCEPTION_INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP: return "EXCEPTION_SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    default: return "UNKNOWN_EXCEPTION";
    }
}

const char* BaseName(const char* path) {
    if (!path || !*path) {
        return "<unknown>";
    }

    const char* base = path;
    for (const char* cursor = path; *cursor; ++cursor) {
        if (*cursor == '\\' || *cursor == '/') {
            base = cursor + 1;
        }
    }
    return base;
}

std::string DescribeModuleAddress(uintptr_t address) {
    if (!address) {
        return PointerHex(address);
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) || !mbi.AllocationBase) {
        return PointerHex(address);
    }

    const uintptr_t allocationBase = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
    char modulePath[MAX_PATH] = {};
    if (!GetModuleFileNameA(static_cast<HMODULE>(mbi.AllocationBase), modulePath, MAX_PATH)) {
        std::ostringstream oss;
        oss << PointerHex(address) << " (base=" << PointerHex(allocationBase) << ')';
        return oss.str();
    }

    std::ostringstream oss;
    oss << PointerHex(address)
        << " (" << BaseName(modulePath)
        << "+" << PointerHex(address - allocationBase)
        << " base=" << PointerHex(allocationBase) << ')';
    return oss.str();
}

std::string DescribeSehFailure(const SehFailure& failure) {
    std::ostringstream oss;
    oss << "code=" << Hex32(failure.code)
        << " (" << SavestateExceptionCodeName(failure.code) << ")";
    if (failure.exceptionAddress) {
        oss << " exception=" << DescribeModuleAddress(failure.exceptionAddress);
    }

    if ((failure.code == EXCEPTION_ACCESS_VIOLATION || failure.code == EXCEPTION_IN_PAGE_ERROR)
        && failure.numberParameters >= 2) {
        const char* accessType = "unknown";
        if (failure.info[0] == 0) {
            accessType = "read";
        } else if (failure.info[0] == 1) {
            accessType = "write";
        } else if (failure.info[0] == 8) {
            accessType = "execute";
        }
        oss << " access=" << accessType
            << " target=" << DescribeModuleAddress(static_cast<uintptr_t>(failure.info[1]));
    }

    const DWORD infoCount = std::min<DWORD>(failure.numberParameters, 3);
    for (DWORD index = 0; index < infoCount; ++index) {
        oss << " info[" << index << "]=" << PointerHex(static_cast<uintptr_t>(failure.info[index]));
    }
    return oss.str();
}

void LogSavestateSehFailure(const char* step, const SehFailure& failure, const std::string& detail) {
    std::ostringstream oss;
    oss << "[SAVESTATE][SEH] " << (step ? step : "unknown") << ": " << DescribeSehFailure(failure);
    if (!detail.empty()) {
        oss << " | " << detail;
    }
    SetLastStatus(oss.str());
    LogOut(oss.str(), true);
}

bool SehInvalidateAutoActionCharacterCaches(const char* reason, SehFailure& outFailure) {
    outFailure = SehFailure{};
    __try {
        InvalidateAutoActionCharacterCaches(reason);
        return true;
    } __except (CaptureSehFailure(&outFailure, GetExceptionInformation())) {
        return false;
    }
}

bool SehPauseResetCachedPointers(const char* reason, SehFailure& outFailure) {
    outFailure = SehFailure{};
    __try {
        PauseIntegration::ResetCachedPointers(reason);
        return true;
    } __except (CaptureSehFailure(&outFailure, GetExceptionInformation())) {
        return false;
    }
}

bool SehResetCollisionHookSessionCaches(const char* reason, SehFailure& outFailure) {
    outFailure = SehFailure{};
    __try {
        ResetCollisionHookSessionCaches(reason);
        return true;
    } __except (CaptureSehFailure(&outFailure, GetExceptionInformation())) {
        return false;
    }
}

bool SehComboOverlayResetState(const char* reason, SehFailure& outFailure) {
    outFailure = SehFailure{};
    __try {
        ComboOverlay::ResetState(reason);
        return true;
    } __except (CaptureSehFailure(&outFailure, GetExceptionInformation())) {
        return false;
    }
}

bool SehRequestRuntimeLifecycleResync(const std::string* reason, SehFailure& outFailure) {
    outFailure = SehFailure{};
    __try {
        if (!reason) {
            return false;
        }
        RequestRuntimeLifecycleResync(*reason);
        return true;
    } __except (CaptureSehFailure(&outFailure, GetExceptionInformation())) {
        return false;
    }
}

bool SehCancelAutoActionsAndMacros(SehFailure& outFailure) {
    outFailure = SehFailure{};
    __try {
        CancelAutoActionsAndMacros();
        return true;
    } __except (CaptureSehFailure(&outFailure, GetExceptionInformation())) {
        return false;
    }
}

bool SehRestoreFpuState(const uint8_t* src, SehFailure& outFailure) {
    outFailure = SehFailure{};
    __try {
        RestoreFpuState(src);
        return true;
    } __except (CaptureSehFailure(&outFailure, GetExceptionInformation())) {
        return false;
    }
}

bool SehRestoreModState(const PersistedModState& state, bool engineOnlyLocalSideRestore, SehFailure& outFailure) {
    outFailure = SehFailure{};
    __try {
        RestoreModState(state, engineOnlyLocalSideRestore);
        return true;
    } __except (CaptureSehFailure(&outFailure, GetExceptionInformation())) {
        return false;
    }
}

bool SehTickCharacterEnforcements(uintptr_t base, SehFailure& outFailure) {
    outFailure = SehFailure{};
    __try {
        CharacterSettings::TickCharacterEnforcements(base, displayData);
        return true;
    } __except (CaptureSehFailure(&outFailure, GetExceptionInformation())) {
        return false;
    }
}

uint32_t HashBytes(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t hash = 2166136261u;
    for (size_t index = 0; index < size; ++index) {
        hash ^= static_cast<uint32_t>(bytes[index]);
        hash *= 16777619u;
    }
    return hash;
}

void AppendHashedSection(std::ostringstream& oss, const char* label, const void* data, size_t size) {
    oss << ' ' << label << '=' << size << '@' << Hex32(HashBytes(data, size));
}

void AppendHashedSection(std::ostringstream& oss, const char* label, const std::vector<uint8_t>& bytes) {
    AppendHashedSection(oss, label, bytes.empty() ? nullptr : bytes.data(), bytes.size());
}

size_t SoundStateSizeBytes(const std::vector<SoundPlaybackEntry>& soundState) {
    return soundState.size() * sizeof(SoundPlaybackEntry);
}

const char* SoundOwnerName(SoundOwner owner) {
    switch (owner) {
    case SoundOwner::Common:
        return "common";
    case SoundOwner::P1:
        return "p1";
    case SoundOwner::P2:
        return "p2";
    default:
        return "unknown";
    }
}

bool TryReadSavedGameSpeed(const Snapshot& snapshot, uint8_t& outGameSpeed) {
    if (snapshot.battleContext.size() <= kBattleContextGameSpeedOffset) {
        return false;
    }
    outGameSpeed = snapshot.battleContext[kBattleContextGameSpeedOffset];
    return true;
}

std::string DescribeBattleContextKeyFields(const std::vector<uint8_t>& bytes);
std::string DescribeGameStateKeyFields(const std::vector<uint8_t>& bytes);
std::string DescribeSoundState(const std::vector<SoundPlaybackEntry>& soundState);

std::string DescribePersistedModState(const PersistedModState& state) {
    std::ostringstream oss;
    oss << "valid=" << state.valid
        << " p2Override=" << state.p2ControlWasOverridden
        << " origP2Flag=" << state.originalP2ControlFlag
        << " p1Cpu=" << static_cast<unsigned int>(state.p1CpuFlag)
        << " p2Cpu=" << static_cast<unsigned int>(state.p2CpuFlag)
        << " localSide=" << state.localSide
        << " macroState=" << state.macroState
        << " macroSlot=" << state.macroSlot;
    return oss.str();
}

bool ReadSnapshotPaletteState(const Snapshot& snapshot,
                             uint8_t& outP1PaletteIndex,
                             uint8_t& outP2PaletteIndex,
                             uint32_t& outP1CustomFlag,
                             uint32_t& outP2CustomFlag) {
    if (snapshot.p1State.size() <= kPlayerStatePaletteIndexOffset
        || snapshot.p2State.size() <= kPlayerStatePaletteIndexOffset
        || snapshot.gameState.size() < (kGameStateP1CustomPaletteFlagOffset + sizeof(outP1CustomFlag))
        || snapshot.gameState.size() < (kGameStateP2CustomPaletteFlagOffset + sizeof(outP2CustomFlag))) {
        return false;
    }

    outP1PaletteIndex = snapshot.p1State[kPlayerStatePaletteIndexOffset];
    outP2PaletteIndex = snapshot.p2State[kPlayerStatePaletteIndexOffset];
    std::memcpy(&outP1CustomFlag,
                snapshot.gameState.data() + kGameStateP1CustomPaletteFlagOffset,
                sizeof(outP1CustomFlag));
    std::memcpy(&outP2CustomFlag,
                snapshot.gameState.data() + kGameStateP2CustomPaletteFlagOffset,
                sizeof(outP2CustomFlag));

    return true;
}

std::string DescribeSnapshotPaletteState(const Snapshot& snapshot) {
    uint8_t p1PaletteIndex = 0;
    uint8_t p2PaletteIndex = 0;
    uint32_t p1CustomFlag = 0;
    uint32_t p2CustomFlag = 0;
    if (!ReadSnapshotPaletteState(snapshot,
                                  p1PaletteIndex,
                                  p2PaletteIndex,
                                  p1CustomFlag,
                                  p2CustomFlag)) {
        return "unavailable";
    }

    std::ostringstream oss;
    oss << "palette=" << (static_cast<unsigned int>(p1PaletteIndex) + 1)
        << '/' << (static_cast<unsigned int>(p2PaletteIndex) + 1)
        << " custom=" << (p1CustomFlag != 0 ? 1 : 0)
        << '/' << (p2CustomFlag != 0 ? 1 : 0);
    return oss.str();
}

std::string DescribeSnapshotPayload(const Snapshot& snapshot) {
    uint8_t savedGameSpeed = 0;
    const bool haveSavedGameSpeed = TryReadSavedGameSpeed(snapshot, savedGameSpeed);

    std::ostringstream oss;
    oss << "latch=" << Hex32(snapshot.savedFKeyLatch)
        << " savedSpeed=";
    if (haveSavedGameSpeed) {
        oss << static_cast<unsigned int>(savedGameSpeed);
    } else {
        oss << "n/a";
    }
    oss << " mod={" << DescribePersistedModState(snapshot.modState) << '}';
    if (!snapshot.battleContext.empty()) {
        oss << " battleKeys={" << DescribeBattleContextKeyFields(snapshot.battleContext) << '}';
    }
    if (!snapshot.gameState.empty()) {
        oss << " gameKeys={" << DescribeGameStateKeyFields(snapshot.gameState) << '}';
    }
    oss << " paletteKeys={" << DescribeSnapshotPaletteState(snapshot) << '}';
    if (snapshot.hasSoundState) {
        oss << " soundKeys={" << DescribeSoundState(snapshot.soundState) << '}';
    } else {
        oss << " soundKeys={unavailable}";
    }
    AppendHashedSection(oss, "fpu", snapshot.fpuState, sizeof(snapshot.fpuState));
    AppendHashedSection(oss, "mod", &snapshot.modState, sizeof(snapshot.modState));
    if (snapshot.hasSoundState) {
        AppendHashedSection(oss,
                            "sound",
                            snapshot.soundState.empty() ? nullptr : snapshot.soundState.data(),
                            SoundStateSizeBytes(snapshot.soundState));
    }
    AppendHashedSection(oss, "battle", snapshot.battleContext);
    AppendHashedSection(oss, "camera", snapshot.cameraSubfield);
    AppendHashedSection(oss, "gameState", snapshot.gameState);
    AppendHashedSection(oss, "p1", snapshot.p1State);
    AppendHashedSection(oss, "p2", snapshot.p2State);
    AppendHashedSection(oss, "render", snapshot.renderBitmap);
    return oss.str();
}

std::string DescribeFileHeader(const SnapshotFileHeader& header) {
    std::ostringstream oss;
    oss << "magicA=" << Hex32(header.magicA)
        << " magicB=" << Hex32(header.magicB)
        << " version=" << header.version
        << " latch=" << Hex32(header.savedFKeyLatch)
        << " p1=" << CharacterHotswap::GetDisplayNameForSelectId(static_cast<uint8_t>(header.p1CharId))
        << '(' << header.p1CharId << ",size=" << header.p1StateSize << ')'
        << " p2=" << CharacterHotswap::GetDisplayNameForSelectId(static_cast<uint8_t>(header.p2CharId))
        << '(' << header.p2CharId << ",size=" << header.p2StateSize << ')'
        << " fpu=" << header.fpuStateSize
        << " battle=" << header.battleContextSize
        << " camera=" << header.cameraSubfieldSize
        << " gameState=" << header.gameStateSize
        << " render=" << header.renderBitmapSize
        << " mod=" << header.modStateSize;
    return oss.str();
}

unsigned long long PersistedSnapshotSizeBytes(const Snapshot& snapshot) {
    return static_cast<unsigned long long>(sizeof(SnapshotFileHeader))
        + sizeof(snapshot.revivalVersion)
        + sizeof(uint32_t)
        + sizeof(uint32_t)
        + sizeof(uint32_t)
        + sizeof(snapshot.fpuState)
        + sizeof(snapshot.modState)
        + static_cast<unsigned long long>(SoundStateSizeBytes(snapshot.soundState))
        + static_cast<unsigned long long>(snapshot.battleContext.size())
        + static_cast<unsigned long long>(snapshot.cameraSubfield.size())
        + static_cast<unsigned long long>(snapshot.gameState.size())
        + static_cast<unsigned long long>(snapshot.p1State.size())
        + static_cast<unsigned long long>(snapshot.p2State.size())
        + static_cast<unsigned long long>(snapshot.renderBitmap.size());
}

unsigned long long PersistedSnapshotSizeBytes(const SnapshotFileHeader& header, uint32_t soundStateBytes = 0) {
    unsigned long long total = static_cast<unsigned long long>(sizeof(SnapshotFileHeader))
        + static_cast<unsigned long long>(header.fpuStateSize)
        + static_cast<unsigned long long>(header.modStateSize)
        + static_cast<unsigned long long>(soundStateBytes)
        + static_cast<unsigned long long>(header.battleContextSize)
        + static_cast<unsigned long long>(header.cameraSubfieldSize)
        + static_cast<unsigned long long>(header.gameStateSize)
        + static_cast<unsigned long long>(header.p1StateSize)
        + static_cast<unsigned long long>(header.p2StateSize)
        + static_cast<unsigned long long>(header.renderBitmapSize);

    if (header.version == kFileVersion2 || header.version == kFileVersion3 || header.version == kFileVersion4) {
        total += sizeof(uint32_t);
    }
    if (header.version == kFileVersion3 || header.version == kFileVersion4) {
        total += sizeof(uint32_t);
        total += sizeof(uint32_t);
    }
    if (header.version == kFileVersion4) {
        total += sizeof(uint32_t);
    }
    return total;
}

long QueryFileSize(FILE* file) {
    if (!file) {
        return -1;
    }

    const long currentOffset = std::ftell(file);
    if (currentOffset < 0) {
        return -1;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        return -1;
    }

    const long size = std::ftell(file);
    if (std::fseek(file, currentOffset, SEEK_SET) != 0) {
        return -1;
    }
    return size;
}

bool ReadMemoryBlock(uintptr_t address, std::vector<uint8_t>& outBytes, size_t size);
size_t RefreshRestoreTargetGameStatePointers(std::vector<uint8_t>& snapshotGameState,
                                            const std::vector<uint8_t>& liveGameState,
                                            std::ostringstream& sampleLog,
                                            size_t& sampleCount,
                                            size_t maxSamples);

bool RefreshRestoreSessionScratch(Snapshot& snapshot, std::string& outDetail) {
    if (snapshot.renderBitmap.size() != kRenderBitmapSize) {
        outDetail = "render bitmap size mismatch";
        return false;
    }

    const uintptr_t base = GetEFZBase();
    if (!base) {
        outDetail = "EFZ base not available";
        return false;
    }

    std::vector<uint8_t> liveRenderBitmap;
    liveRenderBitmap.reserve(kRenderBitmapSize);
    if (!ReadMemoryBlock(base + kRenderBitmapOffset, liveRenderBitmap, kRenderBitmapSize)) {
        outDetail = "failed to read live render bitmap";
        return false;
    }

    const uint32_t fileHash = snapshot.renderBitmap.empty()
        ? 0
        : HashBytes(snapshot.renderBitmap.data(), snapshot.renderBitmap.size());
    const uint32_t liveHash = liveRenderBitmap.empty()
        ? 0
        : HashBytes(liveRenderBitmap.data(), liveRenderBitmap.size());

    snapshot.renderBitmap.swap(liveRenderBitmap);

    std::ostringstream oss;
    oss << "render=" << snapshot.renderBitmap.size()
        << '@' << Hex32(fileHash)
        << "->" << Hex32(liveHash)
        << " addr=" << PointerHex(base + kRenderBitmapOffset);
    outDetail = oss.str();
    return true;
}

bool ApplyImmediatePostRestoreReset(const char* reason, std::string* outReason) {
    const char* resetReason = reason ? reason : "custom savestate restore";
    const std::string lifecycleReason = "custom savestate restore complete";

    InvalidateGameStatePtrCache();
    InvalidatePlayerBaseCache();
    CharacterSettings::InvalidateAllCharacterPointerCaches();

    SehFailure failure;
    if (!SehInvalidateAutoActionCharacterCaches(resetReason, failure)) {
        if (outReason) {
            *outReason = "exception during immediate auto-action cache reset";
        }
        LogSavestateSehFailure("restore immediate reset auto-action caches",
                               failure,
                               std::string("reason=") + resetReason);
        return false;
    }
    if (!SehPauseResetCachedPointers(resetReason, failure)) {
        if (outReason) {
            *outReason = "exception during pause pointer reset";
        }
        LogSavestateSehFailure("restore immediate reset pause pointers",
                               failure,
                               std::string("reason=") + resetReason);
        return false;
    }
    if (!SehResetCollisionHookSessionCaches(resetReason, failure)) {
        if (outReason) {
            *outReason = "exception during collision cache reset";
        }
        LogSavestateSehFailure("restore immediate reset collision caches",
                               failure,
                               std::string("reason=") + resetReason);
        return false;
    }
    if (!SehComboOverlayResetState(resetReason, failure)) {
        if (outReason) {
            *outReason = "exception during combo overlay reset";
        }
        LogSavestateSehFailure("restore immediate reset combo overlay",
                               failure,
                               std::string("reason=") + resetReason);
        return false;
    }

    LogSavestateTrace("restore immediate reset", resetReason);
    if (!SehRequestRuntimeLifecycleResync(&lifecycleReason, failure)) {
        if (outReason) {
            *outReason = "exception during runtime lifecycle resync";
        }
        LogSavestateSehFailure("restore immediate reset lifecycle resync",
                               failure,
                               std::string("reason=") + resetReason);
        return false;
    }
    return true;
}

size_t CharacterStateSizeForId(uint8_t charId) {
    if (charId >= (sizeof(kCharacterStateSizes) / sizeof(kCharacterStateSizes[0]))) {
        return 0;
    }
    return static_cast<size_t>(kCharacterStateSizes[charId]);
}

bool ValidateSnapshotFileHeader(const SnapshotFileHeader& header, std::string& outReason) {
    if (header.magicA != kFileMagicA || header.magicB != kFileMagicB) {
        outReason = "unexpected snapshot magic";
        return false;
    }
    if (header.version != kFileVersion1
        && header.version != kFileVersion2
        && header.version != kFileVersion3
        && header.version != kFileVersion4) {
        std::ostringstream oss;
        oss << "unsupported snapshot file version " << header.version;
        outReason = oss.str();
        return false;
    }
    if (header.fpuStateSize != kFpuStateSize) {
        std::ostringstream oss;
        oss << "unexpected FPU state size " << header.fpuStateSize << " (expected " << kFpuStateSize << ')';
        outReason = oss.str();
        return false;
    }
    if (header.battleContextSize != kBattleContextSize) {
        std::ostringstream oss;
        oss << "unexpected battle context size " << header.battleContextSize << " (expected " << kBattleContextSize << ')';
        outReason = oss.str();
        return false;
    }
    if (header.cameraSubfieldSize != kCameraSubfieldSize) {
        std::ostringstream oss;
        oss << "unexpected camera subfield size " << header.cameraSubfieldSize << " (expected " << kCameraSubfieldSize << ')';
        outReason = oss.str();
        return false;
    }
    if (header.gameStateSize != kGameStateSize) {
        std::ostringstream oss;
        oss << "unexpected game state size " << header.gameStateSize << " (expected " << kGameStateSize << ')';
        outReason = oss.str();
        return false;
    }
    if (header.renderBitmapSize != kRenderBitmapSize) {
        std::ostringstream oss;
        oss << "unexpected render bitmap size " << header.renderBitmapSize << " (expected " << kRenderBitmapSize << ')';
        outReason = oss.str();
        return false;
    }
    if (header.modStateSize != sizeof(PersistedModState)) {
        std::ostringstream oss;
        oss << "unexpected mod state size " << header.modStateSize << " (expected " << sizeof(PersistedModState) << ')';
        outReason = oss.str();
        return false;
    }
    if (header.p1StateSize == 0 || header.p2StateSize == 0) {
        outReason = "character state size is zero";
        return false;
    }
    if (header.p1StateSize > 0x4000 || header.p2StateSize > 0x4000) {
        outReason = "character state size exceeds hard limit";
        return false;
    }

    const size_t expectedP1Size = CharacterStateSizeForId(static_cast<uint8_t>(header.p1CharId));
    const size_t expectedP2Size = CharacterStateSizeForId(static_cast<uint8_t>(header.p2CharId));
    if (header.p1StateSize != expectedP1Size || header.p2StateSize != expectedP2Size) {
        std::ostringstream oss;
        oss << "character state sizes do not match character IDs"
            << " p1Expected=" << expectedP1Size
            << " p1Actual=" << header.p1StateSize
            << " p2Expected=" << expectedP2Size
            << " p2Actual=" << header.p2StateSize;
        outReason = oss.str();
        return false;
    }
    return true;
}

bool QueueDeferredRestoreRequest(DeferredRestoreKind kind,
                                 int slot,
                                 const char* source,
                                 std::string& outReason) {
    if (kind == DeferredRestoreKind::None) {
        outReason = "invalid restore request";
        return false;
    }

    if (kind == DeferredRestoreKind::WorkingSnapshot) {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        if (!s_workingSnapshot.valid) {
            outReason = "working snapshot is empty";
            return false;
        }
    }

    s_deferredRestoreSlot.store(slot, std::memory_order_release);
    s_deferredRestoreKind.store(kind, std::memory_order_release);

    std::ostringstream oss;
    oss << "kind=" << DeferredRestoreKindName(kind)
        << " slot=" << slot
        << " source=" << (source ? source : "unknown")
        << " mode=" << GetGameModeName(GetCurrentGameMode())
        << " phase=" << static_cast<int>(GetCurrentGamePhase());
    LogSavestateTrace("restore queue", oss.str());

    if (kind == DeferredRestoreKind::SelectedSlot) {
        SetLastStatus("[SAVESTATE] slot restore queued for next match frame");
    } else {
        SetLastStatus("[SAVESTATE] live restore queued for next match frame");
    }
    return true;
}

bool ResolveBattleContext(uintptr_t& outBattleContext) {
    outBattleContext = 0;
    uintptr_t base = GetEFZBase();
    if (!base) {
        return false;
    }

    const uintptr_t slot = base + kBattleContextArrayOffset + (kBattleContextArrayIndex * sizeof(uintptr_t));
    return SafeReadMemory(slot, &outBattleContext, sizeof(outBattleContext)) && outBattleContext != 0;
}

bool ResolveCurrentCharacterPair(uintptr_t& outP1Base,
                                 uintptr_t& outP2Base,
                                 uint8_t& outP1CharId,
                                 uint8_t& outP2CharId,
                                 size_t& outP1Size,
                                 size_t& outP2Size) {
    outP1Base = GetPlayerBase(1);
    outP2Base = GetPlayerBase(2);
    outP1CharId = 0xFF;
    outP2CharId = 0xFF;
    outP1Size = 0;
    outP2Size = 0;

    if (!outP1Base || !outP2Base) {
        return false;
    }

    if (!SafeReadMemory(outP1Base + kCharacterIdOffset, &outP1CharId, sizeof(outP1CharId))
        || !SafeReadMemory(outP2Base + kCharacterIdOffset, &outP2CharId, sizeof(outP2CharId))) {
        return false;
    }

    outP1Size = CharacterStateSizeForId(outP1CharId);
    outP2Size = CharacterStateSizeForId(outP2CharId);
    return outP1Size != 0 && outP2Size != 0;
}

bool ReadMemoryBlock(uintptr_t address, std::vector<uint8_t>& outBytes, size_t size) {
    outBytes.assign(size, 0);
    return SafeReadMemory(address, outBytes.data(), size);
}

bool WriteMemoryBlock(uintptr_t address, const std::vector<uint8_t>& bytes) {
    return !bytes.empty() && SafeWriteMemory(address, bytes.data(), bytes.size());
}

std::string DescribeEntityInventory(
    const ::Mission::SavestateEntityLayout::Inventory& inventory) {
    if (!inventory.valid) {
        return "invalid";
    }

    std::ostringstream oss;
    oss << "tail=" << inventory.tail
        << " next=" << inventory.next
        << " alive=" << inventory.aliveCount
        << " mask=0x" << std::hex << std::uppercase << inventory.aliveMask
        << " checksum=0x" << inventory.checksum;
    return oss.str();
}

bool VerifyEntityRingsAtMemory(
    const char* phase,
    uintptr_t p1Base,
    uintptr_t p2Base,
    const std::vector<uint8_t>& expectedP1State,
    const std::vector<uint8_t>& expectedP2State,
    std::string& outReason) {
    namespace EntityLayout = ::Mission::SavestateEntityLayout;

    const EntityLayout::Inventory expectedP1 =
        EntityLayout::Inspect(expectedP1State.data(), expectedP1State.size());
    const EntityLayout::Inventory expectedP2 =
        EntityLayout::Inspect(expectedP2State.data(), expectedP2State.size());
    if (!expectedP1.valid || !expectedP2.valid) {
        outReason = std::string(phase) +
            " entity-ring verification failed: player-state payload is truncated";
        LogSavestateTrace("entity ring verify", outReason);
        return false;
    }

    std::vector<uint8_t> liveP1State;
    std::vector<uint8_t> liveP2State;
    if (!ReadMemoryBlock(p1Base, liveP1State, EntityLayout::kEntityStateEnd) ||
        !ReadMemoryBlock(p2Base, liveP2State, EntityLayout::kEntityStateEnd)) {
        outReason = std::string(phase) +
            " entity-ring verification failed: could not read live player state";
        LogSavestateTrace("entity ring verify", outReason);
        return false;
    }

    const EntityLayout::Inventory liveP1 =
        EntityLayout::Inspect(liveP1State.data(), liveP1State.size());
    const EntityLayout::Inventory liveP2 =
        EntityLayout::Inspect(liveP2State.data(), liveP2State.size());
    const bool p1Matches = EntityLayout::Equivalent(expectedP1, liveP1);
    const bool p2Matches = EntityLayout::Equivalent(expectedP2, liveP2);

    std::ostringstream detail;
    detail << "phase=" << phase
           << " p1=" << (p1Matches ? "ok" : "mismatch")
           << " expected{" << DescribeEntityInventory(expectedP1) << "}"
           << " live{" << DescribeEntityInventory(liveP1) << "}"
           << " p2=" << (p2Matches ? "ok" : "mismatch")
           << " expected{" << DescribeEntityInventory(expectedP2) << "}"
           << " live{" << DescribeEntityInventory(liveP2) << "}";
    LogSavestateTrace("entity ring verify", detail.str());

    if (!p1Matches || !p2Matches) {
        outReason = std::string(phase) +
            " entity-ring verification failed; see [SAVESTATE][TRACE] for details";
        return false;
    }
    return true;
}

bool IsCommittedPrivatePointerValue(uint32_t value) {
    if (value < 0x01000000u || value >= 0x7FFF0000u) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(value)), &mbi, sizeof(mbi))) {
        return false;
    }
    if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE) {
        return false;
    }
    if ((mbi.Protect & PAGE_GUARD) != 0 || (mbi.Protect & PAGE_NOACCESS) != 0) {
        return false;
    }
    return true;
}

size_t RehydrateCommittedPrivatePointers(std::vector<uint8_t>& snapshotBytes,
                                        const std::vector<uint8_t>& liveBytes,
                                        const char* label,
                                        std::ostringstream& sampleLog,
                                        size_t& sampleCount,
                                        size_t maxSamples) {
    const size_t limit = (std::min)(snapshotBytes.size(), liveBytes.size());
    size_t replaced = 0;

    for (size_t offset = 0; offset + sizeof(uint32_t) <= limit; offset += sizeof(uint32_t)) {
        uint32_t savedValue = 0;
        uint32_t liveValue = 0;
        std::memcpy(&savedValue, snapshotBytes.data() + offset, sizeof(savedValue));
        std::memcpy(&liveValue, liveBytes.data() + offset, sizeof(liveValue));

        if (savedValue == liveValue || savedValue == 0 || liveValue == 0) {
            continue;
        }
        if (!IsCommittedPrivatePointerValue(savedValue) || !IsCommittedPrivatePointerValue(liveValue)) {
            continue;
        }

        std::memcpy(snapshotBytes.data() + offset, &liveValue, sizeof(liveValue));
        ++replaced;

        if (sampleCount < maxSamples) {
            if (sampleCount != 0) {
                sampleLog << "; ";
            }
            sampleLog << label << "+0x" << std::hex << std::uppercase << offset << std::dec
                      << ':' << Hex32(savedValue) << "->" << Hex32(liveValue);
            ++sampleCount;
        }
    }

    return replaced;
}

size_t RehydrateCommittedPrivatePointersRange(
    std::vector<uint8_t>& snapshotBytes,
    const std::vector<uint8_t>& liveBytes,
    size_t begin,
    size_t end,
    const char* label,
    std::ostringstream& sampleLog,
    size_t& sampleCount,
    size_t maxSamples) {
    const size_t limit = (std::min)(snapshotBytes.size(), liveBytes.size());
    begin = (std::min)(begin, limit);
    end = (std::min)(end, limit);
    size_t replaced = 0;
    for (size_t offset = begin; offset + sizeof(uint32_t) <= end;
         offset += sizeof(uint32_t)) {
        uint32_t savedValue = 0;
        uint32_t liveValue = 0;
        std::memcpy(&savedValue, snapshotBytes.data() + offset, sizeof(savedValue));
        std::memcpy(&liveValue, liveBytes.data() + offset, sizeof(liveValue));
        if (savedValue == liveValue || savedValue == 0 || liveValue == 0 ||
            !IsCommittedPrivatePointerValue(savedValue) ||
            !IsCommittedPrivatePointerValue(liveValue)) {
            continue;
        }
        std::memcpy(snapshotBytes.data() + offset, &liveValue, sizeof(liveValue));
        ++replaced;
        if (sampleCount < maxSamples) {
            if (sampleCount != 0) sampleLog << "; ";
            sampleLog << label << "+0x" << std::hex << std::uppercase << offset
                      << std::dec << ':' << Hex32(savedValue) << "->"
                      << Hex32(liveValue);
            ++sampleCount;
        }
    }
    return replaced;
}

size_t RefreshExplicitPointerFields(std::vector<uint8_t>& snapshotBytes,
                                   const std::vector<uint8_t>& liveBytes,
                                   size_t offset,
                                   size_t size,
                                   const char* label,
                                   std::ostringstream& sampleLog,
                                   size_t& sampleCount,
                                   size_t maxSamples) {
    const size_t limit = (std::min)(snapshotBytes.size(), liveBytes.size());
    if (offset >= limit || size < sizeof(uint32_t)) {
        return 0;
    }

    const size_t end = (std::min)(limit, offset + size);
    size_t replaced = 0;
    for (size_t fieldOffset = offset; fieldOffset + sizeof(uint32_t) <= end; fieldOffset += sizeof(uint32_t)) {
        uint32_t savedValue = 0;
        uint32_t liveValue = 0;
        std::memcpy(&savedValue, snapshotBytes.data() + fieldOffset, sizeof(savedValue));
        std::memcpy(&liveValue, liveBytes.data() + fieldOffset, sizeof(liveValue));

        if (savedValue == liveValue) {
            continue;
        }

        std::memcpy(snapshotBytes.data() + fieldOffset, &liveValue, sizeof(liveValue));
        ++replaced;

        if (sampleCount < maxSamples) {
            if (sampleCount != 0) {
                sampleLog << "; ";
            }
            sampleLog << label << "+0x" << std::hex << std::uppercase << fieldOffset << std::dec
                      << ':' << Hex32(savedValue) << "->" << Hex32(liveValue);
            ++sampleCount;
        }
    }

    return replaced;
}

size_t PreserveExplicitByteRange(std::vector<uint8_t>& snapshotBytes,
                                 const std::vector<uint8_t>& liveBytes,
                                 size_t offset,
                                 size_t size,
                                 const char* label,
                                 std::ostringstream& sampleLog,
                                 size_t& sampleCount,
                                 size_t maxSamples) {
    const size_t limit = (std::min)(snapshotBytes.size(), liveBytes.size());
    if (offset >= limit || size == 0) {
        return 0;
    }

    const size_t end = (std::min)(limit, offset + size);
    if (end <= offset) {
        return 0;
    }

    if (std::memcmp(snapshotBytes.data() + offset, liveBytes.data() + offset, end - offset) == 0) {
        return 0;
    }

    std::memcpy(snapshotBytes.data() + offset, liveBytes.data() + offset, end - offset);

    if (sampleCount < maxSamples) {
        if (sampleCount != 0) {
            sampleLog << "; ";
        }
        sampleLog << label << "+0x" << std::hex << std::uppercase << offset << std::dec
                  << " size=" << (end - offset);
        ++sampleCount;
    }

    return 1;
}

size_t RefreshRestoreTargetBattleContextPointers(std::vector<uint8_t>& snapshotBattleContext,
                                                const std::vector<uint8_t>& liveBattleContext,
                                                std::ostringstream& sampleLog,
                                                size_t& sampleCount,
                                                size_t maxSamples) {
    size_t replaced = RefreshExplicitPointerFields(snapshotBattleContext,
                                                  liveBattleContext,
                                                  kBattleContextRuntimeControlHeaderOffset,
                                                  kBattleContextRuntimeControlHeaderSize,
                                                  "battle",
                                                  sampleLog,
                                                  sampleCount,
                                                  maxSamples);
    replaced += RefreshExplicitPointerFields(snapshotBattleContext,
                                             liveBattleContext,
                                             kBattleContextRuntimePointerBlockOffset,
                                             kBattleContextRuntimePointerBlockSize,
                                             "battle",
                                             sampleLog,
                                             sampleCount,
                                             maxSamples);
    replaced += PreserveExplicitByteRange(snapshotBattleContext,
                                          liveBattleContext,
                                          kBattleContextInitFlagOffset,
                                          kBattleContextRuntimeFlagsSize,
                                          "battle",
                                          sampleLog,
                                          sampleCount,
                                          maxSamples);
    replaced += RehydrateCommittedPrivatePointers(snapshotBattleContext,
                                                  liveBattleContext,
                                                  "battle",
                                                  sampleLog,
                                                  sampleCount,
                                                  maxSamples);
    return replaced;
}

size_t RefreshRestoreTargetPlayerStatePointers(std::vector<uint8_t>& snapshotPlayerState,
                                              const std::vector<uint8_t>& livePlayerState,
                                              const char* label,
                                              std::ostringstream& sampleLog,
                                              size_t& sampleCount,
                                              size_t maxSamples,
                                              bool preserveStancePointers) {
    size_t replaced = RefreshExplicitPointerFields(snapshotPlayerState,
                                                  livePlayerState,
                                                  kPlayerStateRuntimePointerBlockOffset,
                                                  kPlayerStateRuntimePointerBlockSize,
                                                  label,
                                                  sampleLog,
                                                  sampleCount,
                                                  maxSamples);
    // +0x10 (animation table) and +0x164 (collision/move table) are what Rumi's stance toggle swaps.
    // Keep captured pointers on same-session restores; refresh on disk restores.
    if (!preserveStancePointers) {
        replaced += RefreshExplicitPointerFields(snapshotPlayerState,
                                                 livePlayerState,
                                                 kPlayerStateAnimationDataTableOffset,
                                                 sizeof(uint32_t),
                                                 label,
                                                 sampleLog,
                                                 sampleCount,
                                                 maxSamples);
    }
    // DirectSound buffer IDs are session-ordered integer handles; VirtualQuery can't rehydrate them.
    replaced += PreserveExplicitByteRange(snapshotPlayerState,
                                          livePlayerState,
                                          kPlayerStateSoundIdTableOffset,
                                          kPlayerStateSoundIdTableSize,
                                          label,
                                          sampleLog,
                                          sampleCount,
                                          maxSamples);
    // Each entity slot caches its animation-table pointer at slot+8; refresh all 64 explicitly.
    for (size_t slot = 0;
         slot < ::Mission::SavestateEntityLayout::kSlotCount; ++slot) {
        replaced += RefreshExplicitPointerFields(
            snapshotPlayerState,
            livePlayerState,
            ::Mission::SavestateEntityLayout::SlotAnimPointerField(slot),
            sizeof(uint32_t),
            label,
            sampleLog,
            sampleCount,
            maxSamples);
    }
    replaced += RefreshExplicitPointerFields(snapshotPlayerState,
                                             livePlayerState,
                                             kPlayerStateImageSurfaceArrayOffset,
                                             sizeof(uint32_t),
                                             label,
                                             sampleLog,
                                             sampleCount,
                                             maxSamples);
    if (!preserveStancePointers) {
        replaced += RefreshExplicitPointerFields(snapshotPlayerState,
                                                 livePlayerState,
                                                 kPlayerStateCollisionDataTableOffset,
                                                 sizeof(uint32_t),
                                                 label,
                                                 sampleLog,
                                                 sampleCount,
                                                 maxSamples);
    }
    uint32_t savedAnimTablePtr = 0;
    uint32_t savedMoveTablePtr = 0;
    bool haveSavedAnim = false;
    bool haveSavedMove = false;
    if (preserveStancePointers) {
        if (kPlayerStateAnimationDataTableOffset + sizeof(uint32_t) <= snapshotPlayerState.size()) {
            std::memcpy(&savedAnimTablePtr, snapshotPlayerState.data() + kPlayerStateAnimationDataTableOffset, sizeof(savedAnimTablePtr));
            haveSavedAnim = true;
        }
        if (kPlayerStateCollisionDataTableOffset + sizeof(uint32_t) <= snapshotPlayerState.size()) {
            std::memcpy(&savedMoveTablePtr, snapshotPlayerState.data() + kPlayerStateCollisionDataTableOffset, sizeof(savedMoveTablePtr));
            haveSavedMove = true;
        }
    }

    // The rest of the ring is opaque; never rewrite pointer-shaped fields via the VirtualQuery heuristic.
    replaced += RehydrateCommittedPrivatePointersRange(
        snapshotPlayerState, livePlayerState, 0,
        ::Mission::SavestateEntityLayout::kEntityStateOffset,
        label, sampleLog, sampleCount, maxSamples);
    replaced += RehydrateCommittedPrivatePointersRange(
        snapshotPlayerState, livePlayerState,
        ::Mission::SavestateEntityLayout::kEntityStateEnd,
        (std::min)(snapshotPlayerState.size(), livePlayerState.size()),
        label, sampleLog, sampleCount, maxSamples);

    if (haveSavedAnim) {
        std::memcpy(snapshotPlayerState.data() + kPlayerStateAnimationDataTableOffset, &savedAnimTablePtr, sizeof(savedAnimTablePtr));
    }
    if (haveSavedMove) {
        std::memcpy(snapshotPlayerState.data() + kPlayerStateCollisionDataTableOffset, &savedMoveTablePtr, sizeof(savedMoveTablePtr));
    }
    return replaced;
}

bool RefreshRestoreTargetSessionPointers(Snapshot& snapshot, std::string& outDetail) {
    uintptr_t battleContext = 0;
    const uintptr_t gameStatePtr = GetGameStatePtr();
    uintptr_t p1Base = 0;
    uintptr_t p2Base = 0;
    uint8_t p1CharId = 0xFF;
    uint8_t p2CharId = 0xFF;
    size_t p1Size = 0;
    size_t p2Size = 0;

    if (!ResolveBattleContext(battleContext) || !battleContext) {
        outDetail = "battle context unavailable";
        return false;
    }
    if (!gameStatePtr) {
        outDetail = "game state unavailable";
        return false;
    }
    if (!ResolveCurrentCharacterPair(p1Base, p2Base, p1CharId, p2CharId, p1Size, p2Size)) {
        outDetail = "character pair unavailable";
        return false;
    }

    if (p1CharId != snapshot.p1CharId || p2CharId != snapshot.p2CharId
        || p1Size != snapshot.p1State.size() || p2Size != snapshot.p2State.size()) {
        outDetail = "current character pair does not match snapshot";
        return false;
    }

    std::vector<uint8_t> liveBattleContext;
    std::vector<uint8_t> liveCameraSubfield;
    std::vector<uint8_t> liveGameState;
    std::vector<uint8_t> liveP1State;
    std::vector<uint8_t> liveP2State;

    if (!ReadMemoryBlock(battleContext, liveBattleContext, snapshot.battleContext.size())
        || !ReadMemoryBlock(battleContext + kCameraSubfieldOffset, liveCameraSubfield, snapshot.cameraSubfield.size())
        || !ReadMemoryBlock(gameStatePtr, liveGameState, snapshot.gameState.size())
        || !ReadMemoryBlock(p1Base, liveP1State, snapshot.p1State.size())
        || !ReadMemoryBlock(p2Base, liveP2State, snapshot.p2State.size())) {
        outDetail = "failed to read live restore target regions";
        return false;
    }

    std::ostringstream samples;
    size_t sampleCount = 0;
    const size_t maxSamples = 8;
    const size_t battleReplaced = RefreshRestoreTargetBattleContextPointers(snapshot.battleContext, liveBattleContext, samples, sampleCount, maxSamples);
    const size_t cameraReplaced = RehydrateCommittedPrivatePointers(snapshot.cameraSubfield, liveCameraSubfield, "camera", samples, sampleCount, maxSamples);
    const size_t gameStateReplaced = RefreshRestoreTargetGameStatePointers(snapshot.gameState, liveGameState, samples, sampleCount, maxSamples);
    const bool preserveStancePointers = !snapshot.fromDisk;
    const size_t p1Replaced = RefreshRestoreTargetPlayerStatePointers(snapshot.p1State, liveP1State, "p1", samples, sampleCount, maxSamples, preserveStancePointers);
    const size_t p2Replaced = RefreshRestoreTargetPlayerStatePointers(snapshot.p2State, liveP2State, "p2", samples, sampleCount, maxSamples, preserveStancePointers);

    std::ostringstream oss;
    oss << "battle=" << battleReplaced
        << " camera=" << cameraReplaced
        << " gameState=" << gameStateReplaced
        << " p1=" << p1Replaced
        << " p2=" << p2Replaced;
    if (sampleCount != 0) {
        oss << " samples=" << samples.str();
    }
    outDetail = oss.str();
    return true;
}

template <typename T>
bool ReadStructValue(const std::vector<uint8_t>& bytes, size_t offset, T& outValue) {
    if (offset + sizeof(T) > bytes.size()) {
        return false;
    }
    std::memcpy(&outValue, bytes.data() + offset, sizeof(T));
    return true;
}

template <typename T>
bool WriteStructValue(std::vector<uint8_t>& bytes, size_t offset, const T& value) {
    if (offset + sizeof(T) > bytes.size()) {
        return false;
    }
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
    return true;
}

size_t CountChangedBytes(const std::vector<uint8_t>& beforeBytes,
                        const std::vector<uint8_t>& afterBytes) {
    const size_t limit = (std::min)(beforeBytes.size(), afterBytes.size());
    size_t changed = 0;
    for (size_t index = 0; index < limit; ++index) {
        if (beforeBytes[index] != afterBytes[index]) {
            ++changed;
        }
    }
    changed += (beforeBytes.size() > limit) ? (beforeBytes.size() - limit) : 0;
    changed += (afterBytes.size() > limit) ? (afterBytes.size() - limit) : 0;
    return changed;
}

size_t CountChangedDwordsWithSamples(const std::vector<uint8_t>& beforeBytes,
                                     const std::vector<uint8_t>& afterBytes,
                                     std::ostringstream& outSamples,
                                     size_t maxSamples) {
    const size_t limit = (std::min)(beforeBytes.size(), afterBytes.size());
    size_t changed = 0;
    size_t sampleCount = 0;
    for (size_t offset = 0; offset + sizeof(uint32_t) <= limit; offset += sizeof(uint32_t)) {
        uint32_t beforeValue = 0;
        uint32_t afterValue = 0;
        std::memcpy(&beforeValue, beforeBytes.data() + offset, sizeof(beforeValue));
        std::memcpy(&afterValue, afterBytes.data() + offset, sizeof(afterValue));
        if (beforeValue == afterValue) {
            continue;
        }

        ++changed;
        if (sampleCount < maxSamples) {
            if (sampleCount != 0) {
                outSamples << "; ";
            }
            outSamples << "+0x" << std::hex << std::uppercase << offset << std::dec
                       << ':' << Hex32(beforeValue) << "->" << Hex32(afterValue);
            ++sampleCount;
        }
    }
    return changed;
}

std::string DescribeBattleContextKeyFields(const std::vector<uint8_t>& bytes) {
    uint32_t p1Ptr = 0;
    uint32_t p2Ptr = 0;
    uint32_t p1Slot = 0;
    uint32_t p2Slot = 0;
    uint32_t gameStatePtr = 0;
    uint32_t graphicsPtr = 0;
    uint32_t frameCounter = 0;
    uint32_t cameraX = 0;
    uint32_t cameraY = 0;
    uint16_t roundCount = 0;
    uint16_t roundTimer = 0;
    uint8_t initFlag = 0;
    uint8_t cleanupFlag = 0;
    uint8_t gameSpeed = 0;
    uint32_t pauseFlag = 0;

    ReadStructValue(bytes, 12, p1Ptr);
    ReadStructValue(bytes, 16, p2Ptr);
    ReadStructValue(bytes, 20, p1Slot);
    ReadStructValue(bytes, 24, p2Slot);
    ReadStructValue(bytes, 28, gameStatePtr);
    ReadStructValue(bytes, 32, graphicsPtr);
    ReadStructValue(bytes, kBattleContextFrameCounterOffset, frameCounter);
    ReadStructValue(bytes, kBattleContextCameraXOffset, cameraX);
    ReadStructValue(bytes, kBattleContextCameraYOffset, cameraY);
    ReadStructValue(bytes, kBattleContextRoundCountOffset, roundCount);
    ReadStructValue(bytes, kBattleContextRoundTimerOffset, roundTimer);
    ReadStructValue(bytes, kBattleContextInitFlagOffset, initFlag);
    ReadStructValue(bytes, kBattleContextCleanupFlagOffset, cleanupFlag);
    ReadStructValue(bytes, kBattleContextGameSpeedOffset, gameSpeed);
    ReadStructValue(bytes, kBattleContextPauseFlagOffset, pauseFlag);

    std::ostringstream oss;
    oss << "p1=" << Hex32(p1Ptr)
        << " p2=" << Hex32(p2Ptr)
        << " p1Slot=" << Hex32(p1Slot)
        << " p2Slot=" << Hex32(p2Slot)
        << " gs=" << Hex32(gameStatePtr)
        << " gfx=" << Hex32(graphicsPtr)
        << " init=" << static_cast<unsigned int>(initFlag)
        << " cleanup=" << static_cast<unsigned int>(cleanupFlag)
        << " frame=" << frameCounter
        << " cam=" << static_cast<int32_t>(cameraX) << '/' << static_cast<int32_t>(cameraY)
        << " round=" << roundCount
        << " timer=" << roundTimer
        << " speed=" << static_cast<unsigned int>(gameSpeed)
        << " pause=" << pauseFlag;
    return oss.str();
}

std::string DescribeGameStateKeyFields(const std::vector<uint8_t>& bytes) {
    uint8_t roundEvent = 0;
    uint16_t roundDuration = 0;
    uint32_t roundGate = 0;
    uint32_t heapPtr = 0;
    uint32_t resourcePtr = 0;
    uint16_t roundUiCounter = 0;
    uint16_t roundUiCounter2 = 0;
    uint32_t extSpeed = 0;
    uint32_t extEvent = 0;
    uint8_t replayActive = 0;
    uint32_t replayHandle = 0;

    ReadStructValue(bytes, kGameStateRoundEventOffset, roundEvent);
    ReadStructValue(bytes, kGameStateRoundDurationOffset, roundDuration);
    ReadStructValue(bytes, kGameStateRoundGateOffset, roundGate);
    ReadStructValue(bytes, kGameStateHeapBufferOffset, heapPtr);
    ReadStructValue(bytes, kGameStateResourceObjectOffset, resourcePtr);
    ReadStructValue(bytes, kGameStateRoundUiCounterOffset, roundUiCounter);
    ReadStructValue(bytes, kGameStateRoundUiCounter2Offset, roundUiCounter2);
    ReadStructValue(bytes, kGameStateExternalSpeedTriggerOffset, extSpeed);
    ReadStructValue(bytes, kGameStateExternalEventTriggerOffset, extEvent);
    ReadStructValue(bytes, kGameStateReplayIoOffset, replayActive);
    ReadStructValue(bytes, kGameStateReplayIoOffset + 1, replayHandle);

    std::ostringstream oss;
    oss << "roundEvent=" << static_cast<unsigned int>(roundEvent)
        << " roundDuration=" << roundDuration
        << " roundGate=" << Hex32(roundGate)
        << " heap=" << Hex32(heapPtr)
        << " resource=" << Hex32(resourcePtr)
        << " ui=" << roundUiCounter << '/' << roundUiCounter2
        << " ext=" << Hex32(extSpeed) << '/' << Hex32(extEvent)
        << " replayActive=" << static_cast<unsigned int>(replayActive)
        << " replayHandle=" << Hex32(replayHandle);
    return oss.str();
}

bool NormalizeInitialSnapshotRoundStartState(Snapshot& snapshot, std::string& outDetail) {
    uint8_t roundEvent = 0;
    uint16_t roundDuration = 0;
    uint32_t roundGate = 0;
    uint16_t roundUiCounter = 0;
    uint16_t roundUiCounter2 = 0;

    if (!ReadStructValue(snapshot.gameState, kGameStateRoundEventOffset, roundEvent)
        || !ReadStructValue(snapshot.gameState, kGameStateRoundDurationOffset, roundDuration)
        || !ReadStructValue(snapshot.gameState, kGameStateRoundGateOffset, roundGate)
        || !ReadStructValue(snapshot.gameState, kGameStateRoundUiCounterOffset, roundUiCounter)
        || !ReadStructValue(snapshot.gameState, kGameStateRoundUiCounter2Offset, roundUiCounter2)) {
        outDetail = "missing gameState round-start fields";
        return false;
    }

    if (roundEvent == 0 && roundDuration != 0) {
        return false;
    }

    const std::string before = DescribeGameStateKeyFields(snapshot.gameState);
    const uint8_t activeRoundEvent = 0;
    const uint32_t clearedRoundGate = 0;
    const uint16_t clearedRoundUiCounter = 0;

    const bool ok = WriteStructValue(snapshot.gameState, kGameStateRoundEventOffset, activeRoundEvent)
        && WriteStructValue(snapshot.gameState, kGameStateRoundDurationOffset, kActiveRoundDurationValue)
        && WriteStructValue(snapshot.gameState, kGameStateRoundGateOffset, clearedRoundGate)
        && WriteStructValue(snapshot.gameState, kGameStateRoundUiCounterOffset, clearedRoundUiCounter)
        && WriteStructValue(snapshot.gameState, kGameStateRoundUiCounter2Offset, clearedRoundUiCounter);

    if (!ok) {
        outDetail = "failed to rewrite gameState round-start fields";
        return false;
    }

    outDetail = "before={" + before + "} after={" + DescribeGameStateKeyFields(snapshot.gameState) + "}";
    return true;
}

std::string DescribePlayerStateKeyFields(const std::vector<uint8_t>& bytes) {
    uint32_t opponentPtr = 0;
    uint32_t gameStatePtr = 0;
    uint32_t renderPtr = 0;
    uint32_t inputPtr = 0;
    uint32_t soundPtr = 0;
    uint32_t animationDataPtr = 0;
    uint32_t imageSurfacePtr = 0;
    uint32_t collisionDataPtr = 0;
    uint8_t playerIndex = 0;
    uint8_t identityByte = 0;

    ReadStructValue(bytes, kPlayerStateAnimationDataTableOffset, animationDataPtr);
    ReadStructValue(bytes, kPlayerStateImageSurfaceArrayOffset, imageSurfacePtr);
    ReadStructValue(bytes, kPlayerStateOpponentPtrOffset, opponentPtr);
    ReadStructValue(bytes, kPlayerStateGameStatePtrOffset, gameStatePtr);
    ReadStructValue(bytes, kPlayerStateRenderPtrOffset, renderPtr);
    ReadStructValue(bytes, kPlayerStateInputPtrOffset, inputPtr);
    ReadStructValue(bytes, kPlayerStateSoundPtrOffset, soundPtr);
    ReadStructValue(bytes, kPlayerStateCollisionDataTableOffset, collisionDataPtr);
    ReadStructValue(bytes, kPlayerStatePlayerIndexOffset, playerIndex);
    ReadStructValue(bytes, kPlayerStateIdentityByteOffset, identityByte);

    std::ostringstream oss;
    oss << "anim=" << Hex32(animationDataPtr)
        << " image=" << Hex32(imageSurfacePtr)
        << " opp=" << Hex32(opponentPtr)
        << " gs=" << Hex32(gameStatePtr)
        << " gfx=" << Hex32(renderPtr)
        << " input=" << Hex32(inputPtr)
        << " sound=" << Hex32(soundPtr)
        << " collision=" << Hex32(collisionDataPtr)
        << " idx=" << static_cast<unsigned int>(playerIndex)
        << " idByte=" << static_cast<unsigned int>(identityByte);
    return oss.str();
}

bool IsRestorableSoundStatus(uint32_t status) {
    return (status & kDirectSoundStatusPlaying) != 0
        && (status & kDirectSoundStatusBufferLost) == 0;
}

std::string DescribeSoundState(const std::vector<SoundPlaybackEntry>& soundState) {
    size_t commonCount = 0;
    size_t p1Count = 0;
    size_t p2Count = 0;
    size_t sampleCount = 0;
    std::ostringstream samples;

    for (const SoundPlaybackEntry& entry : soundState) {
        const SoundOwner owner = static_cast<SoundOwner>(entry.owner);
        switch (owner) {
        case SoundOwner::Common:
            ++commonCount;
            break;
        case SoundOwner::P1:
            ++p1Count;
            break;
        case SoundOwner::P2:
            ++p2Count;
            break;
        default:
            break;
        }

        if (sampleCount < 4) {
            if (sampleCount != 0) {
                samples << "; ";
            }
            samples << SoundOwnerName(owner)
                    << '#' << entry.bufferIndex
                    << '@' << entry.playCursor
                    << ':' << Hex32(entry.status);
            ++sampleCount;
        }
    }

    std::ostringstream oss;
    oss << "count=" << soundState.size()
        << " common=" << commonCount
        << " p1=" << p1Count
        << " p2=" << p2Count;
    if (sampleCount != 0) {
        oss << " samples=" << samples.str();
    }
    return oss.str();
}

bool ResolveSoundManagerPtr(uintptr_t ownerBase, uintptr_t offset, uintptr_t& outSoundManagerPtr) {
    outSoundManagerPtr = 0;
    return ownerBase != 0
        && SafeReadMemory(ownerBase + offset, &outSoundManagerPtr, sizeof(outSoundManagerPtr))
        && outSoundManagerPtr != 0;
}

bool ReadSoundBufferPtr(uintptr_t soundManagerPtr, uint16_t bufferIndex, uintptr_t& outSoundBufferPtr) {
    outSoundBufferPtr = 0;
    if (!soundManagerPtr || bufferIndex >= kSoundManagerBufferCount) {
        return false;
    }
    const uintptr_t bufferSlot = soundManagerPtr + kSoundManagerBufferTableOffset + (sizeof(uint32_t) * bufferIndex);
    return SafeReadMemory(bufferSlot, &outSoundBufferPtr, sizeof(outSoundBufferPtr)) && outSoundBufferPtr != 0;
}

bool QueryDirectSoundStatus(uintptr_t soundBufferPtr, uint32_t& outStatus) {
    outStatus = 0;
    if (!soundBufferPtr) {
        return false;
    }

    using GetStatusFn = HRESULT(__stdcall*)(void*, DWORD*);
    __try {
        const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(soundBufferPtr);
        if (!vtable) {
            return false;
        }
        const GetStatusFn getStatus = *reinterpret_cast<const GetStatusFn*>(vtable + 36);
        if (!getStatus) {
            return false;
        }

        DWORD status = 0;
        const HRESULT hr = getStatus(reinterpret_cast<void*>(soundBufferPtr), &status);
        if (FAILED(hr)) {
            return false;
        }
        outStatus = status;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool QueryDirectSoundCurrentPosition(uintptr_t soundBufferPtr, uint32_t& outPlayCursor) {
    outPlayCursor = 0;
    if (!soundBufferPtr) {
        return false;
    }

    using GetCurrentPositionFn = HRESULT(__stdcall*)(void*, DWORD*, DWORD*);
    __try {
        const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(soundBufferPtr);
        if (!vtable) {
            return false;
        }
        const GetCurrentPositionFn getCurrentPosition = *reinterpret_cast<const GetCurrentPositionFn*>(vtable + 16);
        if (!getCurrentPosition) {
            return false;
        }

        DWORD playCursor = 0;
        const HRESULT hr = getCurrentPosition(reinterpret_cast<void*>(soundBufferPtr), &playCursor, nullptr);
        if (FAILED(hr)) {
            return false;
        }
        outPlayCursor = playCursor;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool StopDirectSoundBuffer(uintptr_t soundBufferPtr) {
    if (!soundBufferPtr) {
        return false;
    }

    using StopFn = HRESULT(__stdcall*)(void*);
    __try {
        const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(soundBufferPtr);
        if (!vtable) {
            return false;
        }
        const StopFn stopBuffer = *reinterpret_cast<const StopFn*>(vtable + 72);
        return stopBuffer != nullptr && SUCCEEDED(stopBuffer(reinterpret_cast<void*>(soundBufferPtr)));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetDirectSoundCurrentPosition(uintptr_t soundBufferPtr, uint32_t playCursor) {
    if (!soundBufferPtr) {
        return false;
    }

    using SetCurrentPositionFn = HRESULT(__stdcall*)(void*, DWORD);
    __try {
        const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(soundBufferPtr);
        if (!vtable) {
            return false;
        }
        const SetCurrentPositionFn setCurrentPosition = *reinterpret_cast<const SetCurrentPositionFn*>(vtable + 52);
        return setCurrentPosition != nullptr
            && SUCCEEDED(setCurrentPosition(reinterpret_cast<void*>(soundBufferPtr), playCursor));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PlayDirectSoundBuffer(uintptr_t soundBufferPtr, bool loopPlayback) {
    if (!soundBufferPtr) {
        return false;
    }

    using PlayFn = HRESULT(__stdcall*)(void*, DWORD, DWORD, DWORD);
    __try {
        const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(soundBufferPtr);
        if (!vtable) {
            return false;
        }
        const PlayFn playBuffer = *reinterpret_cast<const PlayFn*>(vtable + 48);
        const DWORD flags = loopPlayback ? 1u : 0u;
        return playBuffer != nullptr
            && SUCCEEDED(playBuffer(reinterpret_cast<void*>(soundBufferPtr), 0, 0, flags));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void CaptureSoundEntriesForManager(uintptr_t soundManagerPtr,
                                   SoundOwner owner,
                                   uint16_t excludedBufferIndex,
                                   std::vector<SoundPlaybackEntry>& outSoundState) {
    if (!soundManagerPtr) {
        return;
    }

    for (uint16_t bufferIndex = 0; bufferIndex < kSoundManagerBufferCount; ++bufferIndex) {
        if (bufferIndex == excludedBufferIndex) {
            continue;
        }

        uintptr_t soundBufferPtr = 0;
        if (!ReadSoundBufferPtr(soundManagerPtr, bufferIndex, soundBufferPtr)) {
            continue;
        }

        uint32_t status = 0;
        uint32_t playCursor = 0;
        if (!QueryDirectSoundStatus(soundBufferPtr, status)
            || !IsRestorableSoundStatus(status)
            || !QueryDirectSoundCurrentPosition(soundBufferPtr, playCursor)) {
            continue;
        }

        SoundPlaybackEntry entry{};
        entry.owner = static_cast<uint8_t>(owner);
        entry.bufferIndex = bufferIndex;
        entry.status = status;
        entry.playCursor = playCursor;
        outSoundState.push_back(entry);
    }
}

void CaptureSoundState(uintptr_t gameStatePtr,
                       uintptr_t p1Base,
                       uintptr_t p2Base,
                       Snapshot& outSnapshot,
                       std::string& outDetail) {
    outSnapshot.soundState.clear();
    outSnapshot.hasSoundState = true;

    uint16_t bgmBufferIndex = kInvalidSoundBufferIndex;
    SafeReadMemory(gameStatePtr + kSoundManagerBgmBufferIndexOffset, &bgmBufferIndex, sizeof(bgmBufferIndex));

    uintptr_t commonSoundManagerPtr = 0;
    uintptr_t p1SoundManagerPtr = 0;
    uintptr_t p2SoundManagerPtr = 0;
    ResolveSoundManagerPtr(gameStatePtr, kGameStateSoundManagerOffset, commonSoundManagerPtr);
    ResolveSoundManagerPtr(p1Base, kCharacterSoundManagerOffset, p1SoundManagerPtr);
    ResolveSoundManagerPtr(p2Base, kCharacterSoundManagerOffset, p2SoundManagerPtr);

    CaptureSoundEntriesForManager(commonSoundManagerPtr, SoundOwner::Common, bgmBufferIndex, outSnapshot.soundState);
    CaptureSoundEntriesForManager(p1SoundManagerPtr, SoundOwner::P1, kInvalidSoundBufferIndex, outSnapshot.soundState);
    CaptureSoundEntriesForManager(p2SoundManagerPtr, SoundOwner::P2, kInvalidSoundBufferIndex, outSnapshot.soundState);

    if (outSnapshot.soundState.size() > kMaxSerializedSoundEntries) {
        outSnapshot.soundState.resize(kMaxSerializedSoundEntries);
    }

    std::ostringstream oss;
    oss << DescribeSoundState(outSnapshot.soundState)
        << " managers={common=" << PointerHex(commonSoundManagerPtr)
        << " p1=" << PointerHex(p1SoundManagerPtr)
        << " p2=" << PointerHex(p2SoundManagerPtr)
        << " bgmBuffer=" << bgmBufferIndex << '}';
    outDetail = oss.str();
}

size_t StopSoundEntriesForManager(uintptr_t soundManagerPtr, uint16_t excludedBufferIndex) {
    if (!soundManagerPtr) {
        return 0;
    }

    size_t stopped = 0;
    for (uint16_t bufferIndex = 0; bufferIndex < kSoundManagerBufferCount; ++bufferIndex) {
        if (bufferIndex == excludedBufferIndex) {
            continue;
        }

        uintptr_t soundBufferPtr = 0;
        uint32_t status = 0;
        if (!ReadSoundBufferPtr(soundManagerPtr, bufferIndex, soundBufferPtr)
            || !QueryDirectSoundStatus(soundBufferPtr, status)
            || !IsRestorableSoundStatus(status)) {
            continue;
        }

        if (StopDirectSoundBuffer(soundBufferPtr)) {
            ++stopped;
        }
    }
    return stopped;
}

void RestoreSoundState(const Snapshot& snapshot,
                       uintptr_t gameStatePtr,
                       uintptr_t p1Base,
                       uintptr_t p2Base,
                       std::string& outDetail) {
    uintptr_t commonSoundManagerPtr = 0;
    uintptr_t p1SoundManagerPtr = 0;
    uintptr_t p2SoundManagerPtr = 0;
    ResolveSoundManagerPtr(gameStatePtr, kGameStateSoundManagerOffset, commonSoundManagerPtr);
    ResolveSoundManagerPtr(p1Base, kCharacterSoundManagerOffset, p1SoundManagerPtr);
    ResolveSoundManagerPtr(p2Base, kCharacterSoundManagerOffset, p2SoundManagerPtr);

    uint16_t bgmBufferIndex = kInvalidSoundBufferIndex;
    SafeReadMemory(gameStatePtr + kSoundManagerBgmBufferIndexOffset, &bgmBufferIndex, sizeof(bgmBufferIndex));

    const size_t stoppedCount = StopSoundEntriesForManager(commonSoundManagerPtr, bgmBufferIndex)
        + StopSoundEntriesForManager(p1SoundManagerPtr, kInvalidSoundBufferIndex)
        + StopSoundEntriesForManager(p2SoundManagerPtr, kInvalidSoundBufferIndex);

    size_t restoredCount = 0;
    size_t failedCount = 0;
    size_t skippedCount = 0;
    size_t sampleCount = 0;
    std::ostringstream samples;

    for (const SoundPlaybackEntry& entry : snapshot.soundState) {
        const SoundOwner owner = static_cast<SoundOwner>(entry.owner);
        uintptr_t soundManagerPtr = 0;
        switch (owner) {
        case SoundOwner::Common:
            soundManagerPtr = commonSoundManagerPtr;
            break;
        case SoundOwner::P1:
            soundManagerPtr = p1SoundManagerPtr;
            break;
        case SoundOwner::P2:
            soundManagerPtr = p2SoundManagerPtr;
            break;
        default:
            ++skippedCount;
            continue;
        }

        if (!soundManagerPtr
            || (owner == SoundOwner::Common && entry.bufferIndex == bgmBufferIndex)) {
            ++skippedCount;
            continue;
        }

        uintptr_t soundBufferPtr = 0;
        const bool restored = ReadSoundBufferPtr(soundManagerPtr, entry.bufferIndex, soundBufferPtr)
            && StopDirectSoundBuffer(soundBufferPtr)
            && SetDirectSoundCurrentPosition(soundBufferPtr, entry.playCursor)
            && PlayDirectSoundBuffer(soundBufferPtr, (entry.status & kDirectSoundStatusLooping) != 0);

        if (restored) {
            ++restoredCount;
            if (sampleCount < 4) {
                if (sampleCount != 0) {
                    samples << "; ";
                }
                samples << SoundOwnerName(owner)
                        << '#' << entry.bufferIndex
                        << '@' << entry.playCursor
                        << ':' << Hex32(entry.status);
                ++sampleCount;
            }
        } else {
            ++failedCount;
        }
    }

    std::ostringstream oss;
    oss << "available=" << (snapshot.hasSoundState ? 1 : 0)
        << " stopped=" << stoppedCount
        << " requested=" << snapshot.soundState.size()
        << " restored=" << restoredCount
        << " failed=" << failedCount
        << " skipped=" << skippedCount
        << " managers={common=" << PointerHex(commonSoundManagerPtr)
        << " p1=" << PointerHex(p1SoundManagerPtr)
        << " p2=" << PointerHex(p2SoundManagerPtr)
        << " bgmBuffer=" << bgmBufferIndex << '}';
    if (sampleCount != 0) {
        oss << " samples=" << samples.str();
    }
    outDetail = oss.str();
}

void LogRestoreWriteDump(const char* label,
                         uintptr_t address,
                         const std::vector<uint8_t>& beforeBytes,
                         const std::vector<uint8_t>& afterBytes,
                         const std::string& beforeKeys,
                         const std::string& afterKeys) {
    std::ostringstream sampleDiffs;
    const size_t changedDwords = CountChangedDwordsWithSamples(beforeBytes, afterBytes, sampleDiffs, 8);
    const size_t changedBytes = CountChangedBytes(beforeBytes, afterBytes);

    const uint32_t beforeHash = beforeBytes.empty() ? 0 : HashBytes(beforeBytes.data(), beforeBytes.size());
    const uint32_t afterHash = afterBytes.empty() ? 0 : HashBytes(afterBytes.data(), afterBytes.size());

    std::ostringstream oss;
    oss << "label=" << label
        << " addr=" << PointerHex(address)
        << " size=" << afterBytes.size()
        << " hash=" << Hex32(beforeHash) << "->" << Hex32(afterHash)
        << " changedBytes=" << changedBytes
        << " changedDwords=" << changedDwords;
    if (!beforeKeys.empty()) {
        oss << " before={" << beforeKeys << '}';
    }
    if (!afterKeys.empty()) {
        oss << " after={" << afterKeys << '}';
    }
    if (!sampleDiffs.str().empty()) {
        oss << " samples=" << sampleDiffs.str();
    }
    LogSavestateTrace("restore write dump", oss.str());
}

struct GameStateLivePreserveRange {
    size_t offset;
    size_t size;
    const char* label;
};

const GameStateLivePreserveRange* GetGameStatePointerPreserveRanges(size_t& outCount) {
    static const GameStateLivePreserveRange kRanges[] = {
        {0, kGameStateRuntimeOwnedPrefixSize, "prefix"},
        {kGameStateHeapBufferOffset, kGameStateHeapBufferSize, "heap"},
        {kGameStateResourceObjectOffset, kGameStateResourceObjectSize, "resource"},
    };
    outCount = sizeof(kRanges) / sizeof(kRanges[0]);
    return kRanges;
}

const GameStateLivePreserveRange* GetDiskRestoreGameStatePreserveRanges(size_t& outCount) {
    static const GameStateLivePreserveRange kRanges[] = {
        {0, kGameStateRuntimeOwnedPrefixSize, "prefix"},
        {kGameStateHeapBufferOffset, kGameStateHeapBufferSize, "heap"},
        {kGameStateResourceObjectOffset, kGameStateResourceObjectSize, "resource"},
        {kGameStateReplayIoOffset, kGameStateReplayIoSize, "replay"},
    };
    outCount = sizeof(kRanges) / sizeof(kRanges[0]);
    return kRanges;
}

void PreserveLiveGameStateRange(std::vector<uint8_t>& mergedBytes,
                                const std::vector<uint8_t>& liveBytes,
                                size_t offset,
                                size_t size,
                                const char* label,
                                size_t& outPreservedBytes,
                                std::ostringstream& outSamples,
                                size_t& sampleCount,
                                size_t maxSamples) {
    if (offset >= mergedBytes.size() || offset >= liveBytes.size() || size == 0) {
        return;
    }

    const size_t available = (std::min)(size,
                                        (std::min)(mergedBytes.size() - offset,
                                                   liveBytes.size() - offset));
    if (available == 0) {
        return;
    }

    if (std::memcmp(mergedBytes.data() + offset, liveBytes.data() + offset, available) != 0) {
        outPreservedBytes += available;
        if (sampleCount < maxSamples) {
            if (sampleCount != 0) {
                outSamples << "; ";
            }
            outSamples << label << "+0x" << std::hex << std::uppercase << offset << std::dec
                       << " size=" << available;
            ++sampleCount;
        }
    }

    std::memcpy(mergedBytes.data() + offset, liveBytes.data() + offset, available);
}

bool BuildDiskRestoreGameStateBytes(const Snapshot& snapshot,
                                    const std::vector<uint8_t>& liveGameState,
                                    std::vector<uint8_t>& outGameState,
                                    std::string& outDetail) {
    if (snapshot.gameState.size() != liveGameState.size()) {
        std::ostringstream oss;
        oss << "gameState size mismatch snapshot=" << snapshot.gameState.size()
            << " live=" << liveGameState.size();
        outDetail = oss.str();
        return false;
    }

    outGameState = snapshot.gameState;

    size_t preserveRangeCount = 0;
    const GameStateLivePreserveRange* preserveRanges = GetDiskRestoreGameStatePreserveRanges(preserveRangeCount);

    size_t preservedBytes = 0;
    size_t sampleCount = 0;
    constexpr size_t kMaxSamples = 8;
    std::ostringstream samples;

    for (size_t index = 0; index < preserveRangeCount; ++index) {
        const GameStateLivePreserveRange& range = preserveRanges[index];
        PreserveLiveGameStateRange(outGameState,
                                   liveGameState,
                                   range.offset,
                                   range.size,
                                   range.label,
                                   preservedBytes,
                                   samples,
                                   sampleCount,
                                   kMaxSamples);
    }

    std::ostringstream oss;
    oss << "disk snapshot detected; preserving live runtime-owned gameState ranges"
        << " preservedBytes=" << preservedBytes
        << " rangeCount=" << preserveRangeCount;
    if (sampleCount != 0) {
        oss << " samples=" << samples.str();
    }
    outDetail = oss.str();
    return true;
}

size_t RefreshRestoreTargetGameStatePointers(std::vector<uint8_t>& snapshotGameState,
                                            const std::vector<uint8_t>& liveGameState,
                                            std::ostringstream& sampleLog,
                                            size_t& sampleCount,
                                            size_t maxSamples) {
    size_t replaced = PreserveExplicitByteRange(snapshotGameState,
                                                liveGameState,
                                                kGameStateSoundIdTableOffset,
                                                kGameStateSoundIdTableSize,
                                                "game",
                                                sampleLog,
                                                sampleCount,
                                                maxSamples);
    replaced += RehydrateCommittedPrivatePointers(snapshotGameState,
                                                  liveGameState,
                                                  "game",
                                                  sampleLog,
                                                  sampleCount,
                                                  maxSamples);
    return replaced;
}

bool ReadSnapshotRuntimeMetadata(uintptr_t gameStatePtr,
                                 uint8_t& outStageId,
                                 uint16_t& outBgmTrack,
                                 std::string& outReason);

enum class SnapshotVersionFamily {
    Unknown = 0,
    Vanilla,
    Revival102x,
    Other,
};

SnapshotVersionFamily ClassifySnapshotVersionFamily(uint32_t versionValue) {
    switch (static_cast<EfzRevivalVersion>(versionValue)) {
    case EfzRevivalVersion::Unknown:
        return SnapshotVersionFamily::Unknown;
    case EfzRevivalVersion::Vanilla:
        return SnapshotVersionFamily::Vanilla;
    case EfzRevivalVersion::Revival102e:
    case EfzRevivalVersion::Revival102f:
    case EfzRevivalVersion::Revival102g:
    case EfzRevivalVersion::Revival102h:
    case EfzRevivalVersion::Revival102i:
    case EfzRevivalVersion::Revival102j:
        return SnapshotVersionFamily::Revival102x;
    default:
        return SnapshotVersionFamily::Other;
    }
}

bool SnapshotVersionsAreCompatible(uint32_t currentVersion, uint32_t savedVersion) {
    if (currentVersion == 0 || savedVersion == 0) {
        return true;
    }

    if (currentVersion == savedVersion) {
        return true;
    }

    const SnapshotVersionFamily currentFamily = ClassifySnapshotVersionFamily(currentVersion);
    const SnapshotVersionFamily savedFamily = ClassifySnapshotVersionFamily(savedVersion);
    return currentFamily == SnapshotVersionFamily::Revival102x
        && savedFamily == SnapshotVersionFamily::Revival102x;
}

bool CurrentVersionMatchesSnapshot(const Snapshot& snapshot, std::string* outReason = nullptr) {
    if (!snapshot.valid || snapshot.revivalVersion == 0) {
        return true;
    }

    const uint32_t currentVersion = CurrentRevivalVersionValue();
    if (!SnapshotVersionsAreCompatible(currentVersion, snapshot.revivalVersion)) {
        if (outReason) {
            std::ostringstream oss;
            const EfzRevivalVersion current = static_cast<EfzRevivalVersion>(currentVersion);
            const EfzRevivalVersion saved = static_cast<EfzRevivalVersion>(snapshot.revivalVersion);
            oss << "current Revival version " << currentVersion
                << " (" << EfzRevivalVersionName(current) << ")"
                << " is not compatible with saved snapshot version " << snapshot.revivalVersion
                << " (" << EfzRevivalVersionName(saved) << ")";
            *outReason = oss.str();
        }
        return false;
    }
    return true;
}

bool CurrentStageMatchesSnapshot(const Snapshot& snapshot, std::string* outReason = nullptr) {
    if (!snapshot.valid) {
        if (outReason) {
            *outReason = "working snapshot is empty";
        }
        return false;
    }
    if (snapshot.stageId == 0xFF) {
        return true;
    }

    uint8_t currentStageId = 0xFF;
    uint16_t currentBgmTrack = 0;
    std::string reason;
    if (!ReadSnapshotRuntimeMetadata(GetGameStatePtr(), currentStageId, currentBgmTrack, reason)) {
        if (outReason) {
            *outReason = reason;
        }
        return false;
    }

    if (currentStageId != snapshot.stageId) {
        if (outReason) {
            std::ostringstream oss;
            oss << "current stage " << StageTokenForId(currentStageId) << "(" << static_cast<unsigned int>(currentStageId)
                << ") does not match saved snapshot stage " << StageTokenForId(snapshot.stageId)
                << "(" << static_cast<unsigned int>(snapshot.stageId) << ")";
            *outReason = oss.str();
        }
        return false;
    }
    return true;
}

bool ReadSnapshotRuntimeMetadata(uintptr_t gameStatePtr,
                                 uint8_t& outStageId,
                                 uint16_t& outBgmTrack,
                                 std::string& outReason) {
    outStageId = 0xFF;
    outBgmTrack = 0;

    if (!gameStatePtr) {
        outReason = "game state not available";
        return false;
    }

    if (!SafeReadMemory(gameStatePtr + kGameStateStageOffset, &outStageId, sizeof(outStageId))) {
        outReason = "failed to read current stage";
        return false;
    }

    // Disk field stores the active sound-buffer handle, not a BGM track; never passed to PlayBGM.
    outBgmTrack = static_cast<uint16_t>(GetBGMBufferIndex(gameStatePtr));
    return true;
}

std::string SanitizeFileToken(const std::string& raw) {
    std::string token;
    token.reserve(raw.size());

    bool lastWasUnderscore = false;
    for (unsigned char ch : raw) {
        if (std::isalnum(ch)) {
            token.push_back(static_cast<char>(std::tolower(ch)));
            lastWasUnderscore = false;
        } else if (!lastWasUnderscore) {
            token.push_back('_');
            lastWasUnderscore = true;
        }
    }

    while (!token.empty() && token.front() == '_') {
        token.erase(token.begin());
    }
    while (!token.empty() && token.back() == '_') {
        token.pop_back();
    }

    return token.empty() ? "unknown" : token;
}

std::string CharacterFileToken(uint8_t charId) {
    return SanitizeFileToken(CharacterHotswap::GetResourceNameForSelectId(charId));
}

std::string TimestampFileToken() {
    SYSTEMTIME localTime{};
    GetLocalTime(&localTime);

    char token[48] = {};
    _snprintf_s(token,
                sizeof(token),
                _TRUNCATE,
                "%04u%02u%02u_%02u%02u%02u_%03u",
                static_cast<unsigned int>(localTime.wYear),
                static_cast<unsigned int>(localTime.wMonth),
                static_cast<unsigned int>(localTime.wDay),
                static_cast<unsigned int>(localTime.wHour),
                static_cast<unsigned int>(localTime.wMinute),
                static_cast<unsigned int>(localTime.wSecond),
                static_cast<unsigned int>(localTime.wMilliseconds));
    return token;
}

void CaptureFpuState(uint8_t* dest) {
    if (!dest) {
        return;
    }
    __asm {
        mov eax, dest
        fsave byte ptr [eax]
        frstor byte ptr [eax]
    }
}

void RestoreFpuState(const uint8_t* src) {
    if (!src) {
        return;
    }
    __asm {
        mov eax, src
        frstor byte ptr [eax]
    }
}

void CanonicalizeControlState(PersistedModState& state) {
    if (state.localSide != 0 && state.localSide != 1) {
        state.localSide = 0;
    }

    state.p1CpuFlag = static_cast<uint8_t>((state.localSide == 1) ? 1u : 0u);
    state.p2CpuFlag = static_cast<uint8_t>((state.localSide == 1) ? 0u : 1u);
}

void ForceSnapshotControlState(Snapshot& snapshot, int localSide) {
    if (!snapshot.valid) {
        return;
    }

    snapshot.modState.valid = 1;
    snapshot.modState.localSide = (localSide == 1) ? 1 : 0;
    CanonicalizeControlState(snapshot.modState);

    const uint8_t activePlayer = static_cast<uint8_t>(snapshot.modState.localSide);
    WriteStructValue(snapshot.gameState, GAMESTATE_OFF_ACTIVE_PLAYER, activePlayer);
    WriteStructValue(snapshot.gameState, GAMESTATE_OFF_P1_CPU_FLAG, snapshot.modState.p1CpuFlag);
    WriteStructValue(snapshot.gameState, GAMESTATE_OFF_P2_CPU_FLAG, snapshot.modState.p2CpuFlag);
}

void CaptureModState(PersistedModState& outState) {
    std::memset(&outState, 0, sizeof(outState));
    outState.valid = 1;
    outState.p2ControlWasOverridden = g_p2ControlOverridden ? 1u : 0u;
    outState.originalP2ControlFlag = g_originalP2ControlFlag;
    outState.localSide = SwitchPlayers::GetLocalSide();
    if (outState.localSide < 0) {
        outState.localSide = 0;
    }
    outState.macroState = static_cast<int32_t>(MacroController::GetState());
    outState.macroSlot = MacroController::GetCurrentSlot();

    if (uintptr_t gameStatePtr = GetGameStatePtr()) {
        SafeReadMemory(gameStatePtr + GAMESTATE_OFF_P1_CPU_FLAG, &outState.p1CpuFlag, sizeof(outState.p1CpuFlag));
        SafeReadMemory(gameStatePtr + GAMESTATE_OFF_P2_CPU_FLAG, &outState.p2CpuFlag, sizeof(outState.p2CpuFlag));
    }
    CanonicalizeControlState(outState);
}

void RestoreModState(const PersistedModState& state, bool engineOnlyLocalSideRestore) {
    if (!state.valid) {
        ClearDeferredPracticeSideRestoreState();
        return;
    }

    PersistedModState restoreState = state;
    CanonicalizeControlState(restoreState);

    g_p2ControlOverridden = restoreState.p2ControlWasOverridden != 0;
    g_originalP2ControlFlag = restoreState.originalP2ControlFlag;

    bool restoredEngineControlState = false;

    if (restoreState.localSide >= 0) {
        if (engineOnlyLocalSideRestore) {
            restoredEngineControlState = SwitchPlayers::RestoreEngineControlState(restoreState.localSide,
                                                                                  restoreState.p1CpuFlag,
                                                                                  restoreState.p2CpuFlag,
                                                                                  false);
            if (restoredEngineControlState) {
                ArmDeferredPracticeSideRestore(restoreState.localSide);
            } else {
                ClearDeferredPracticeSideRestoreState();
            }
        } else {
            ClearDeferredPracticeSideRestoreState();
            const int currentPracticeSide = SwitchPlayers::GetLocalSide();
            const bool practiceSideAlreadyMatches = currentPracticeSide == restoreState.localSide;
            if (practiceSideAlreadyMatches) {
                std::ostringstream oss;
                oss << "localSide=" << restoreState.localSide
                    << " currentSide=" << currentPracticeSide
                    << " skippedRemap=1";
                LogSavestateTrace("restore control reconcile already satisfied", oss.str());
            } else if (!SwitchPlayers::ReapplyLocalSide(restoreState.localSide)) {
                restoredEngineControlState = SwitchPlayers::RestoreEngineControlState(restoreState.localSide,
                                                                                      restoreState.p1CpuFlag,
                                                                                      restoreState.p2CpuFlag);
                if (restoredEngineControlState) {
                    ArmDeferredPracticeSideRestore(restoreState.localSide);
                }
            }
            if (!restoredEngineControlState && restoreState.localSide >= 0) {
                if (restoreState.localSide == 1) {
                    SwitchPlayers::MarkSwapped();
                } else {
                    SwitchPlayers::ClearSwapFlag();
                }
            }
        }
    } else {
        ClearDeferredPracticeSideRestoreState();
    }

    if (!restoredEngineControlState) {
        if (uintptr_t gameStatePtr = GetGameStatePtr()) {
            const uint8_t activePlayer = static_cast<uint8_t>(restoreState.localSide);
            SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_ACTIVE_PLAYER, &activePlayer, sizeof(activePlayer));
            SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_P1_CPU_FLAG, &restoreState.p1CpuFlag, sizeof(restoreState.p1CpuFlag));
            SafeWriteMemory(gameStatePtr + GAMESTATE_OFF_P2_CPU_FLAG, &restoreState.p2CpuFlag, sizeof(restoreState.p2CpuFlag));
        }
    }
}

bool ExtractEditableFields(const Snapshot& snapshot, CustomSavestate::EditableFields& outFields) {
    if (!snapshot.valid) {
        return false;
    }

    uint16_t p1Meter = 0;
    uint16_t p2Meter = 0;
    if (!ReadStructValue(snapshot.p1State, HP_OFFSET, outFields.p1Hp)
        || !ReadStructValue(snapshot.p2State, HP_OFFSET, outFields.p2Hp)
        || !ReadStructValue(snapshot.p1State, METER_OFFSET, p1Meter)
        || !ReadStructValue(snapshot.p2State, METER_OFFSET, p2Meter)
        || !ReadStructValue(snapshot.p1State, RF_OFFSET, outFields.p1Rf)
        || !ReadStructValue(snapshot.p2State, RF_OFFSET, outFields.p2Rf)
        || !ReadStructValue(snapshot.p1State, XPOS_OFFSET, outFields.p1X)
        || !ReadStructValue(snapshot.p1State, YPOS_OFFSET, outFields.p1Y)
        || !ReadStructValue(snapshot.p2State, XPOS_OFFSET, outFields.p2X)
        || !ReadStructValue(snapshot.p2State, YPOS_OFFSET, outFields.p2Y)
        || !ReadStructValue(snapshot.p1State, XVEL_OFFSET, outFields.p1XVel)
        || !ReadStructValue(snapshot.p1State, YVEL_OFFSET, outFields.p1YVel)
        || !ReadStructValue(snapshot.p2State, XVEL_OFFSET, outFields.p2XVel)
        || !ReadStructValue(snapshot.p2State, YVEL_OFFSET, outFields.p2YVel)) {
        return false;
    }

    outFields.p1Meter = static_cast<int>(p1Meter);
    outFields.p2Meter = static_cast<int>(p2Meter);
    outFields.p1CpuFlag = snapshot.modState.p1CpuFlag ? 1 : 0;
    outFields.p2CpuFlag = snapshot.modState.p2CpuFlag ? 1 : 0;
    outFields.localSide = snapshot.modState.localSide;
    return true;
}

bool ApplyEditableFields(Snapshot& snapshot, const CustomSavestate::EditableFields& fields) {
    if (!snapshot.valid) {
        return false;
    }

    const int clampedP1Hp = (std::max)(0, (std::min)(9999, fields.p1Hp));
    const int clampedP2Hp = (std::max)(0, (std::min)(9999, fields.p2Hp));
    const uint16_t clampedP1Meter = static_cast<uint16_t>((std::max)(0, (std::min)(1000, fields.p1Meter)));
    const uint16_t clampedP2Meter = static_cast<uint16_t>((std::max)(0, (std::min)(1000, fields.p2Meter)));
    const uint8_t clampedP1Cpu = static_cast<uint8_t>((std::max)(0, (std::min)(1, fields.p1CpuFlag)));
    const uint8_t clampedP2Cpu = static_cast<uint8_t>((std::max)(0, (std::min)(1, fields.p2CpuFlag)));
    const int clampedLocalSide = (std::max)(0, (std::min)(1, fields.localSide));
    const double clampedP1Rf = (std::max)(0.0, (std::min)(2000.0, fields.p1Rf));
    const double clampedP2Rf = (std::max)(0.0, (std::min)(2000.0, fields.p2Rf));

    bool ok = true;
    ok = ok && WriteStructValue(snapshot.p1State, HP_OFFSET, clampedP1Hp);
    ok = ok && WriteStructValue(snapshot.p1State, kP1MaxHpOffset, clampedP1Hp);
    ok = ok && WriteStructValue(snapshot.p2State, HP_OFFSET, clampedP2Hp);
    ok = ok && WriteStructValue(snapshot.p2State, kP1MaxHpOffset, clampedP2Hp);
    ok = ok && WriteStructValue(snapshot.p1State, METER_OFFSET, clampedP1Meter);
    ok = ok && WriteStructValue(snapshot.p2State, METER_OFFSET, clampedP2Meter);
    ok = ok && WriteStructValue(snapshot.p1State, RF_OFFSET, clampedP1Rf);
    ok = ok && WriteStructValue(snapshot.p2State, RF_OFFSET, clampedP2Rf);
    ok = ok && WriteStructValue(snapshot.p1State, XPOS_OFFSET, fields.p1X);
    ok = ok && WriteStructValue(snapshot.p1State, YPOS_OFFSET, fields.p1Y);
    ok = ok && WriteStructValue(snapshot.p2State, XPOS_OFFSET, fields.p2X);
    ok = ok && WriteStructValue(snapshot.p2State, YPOS_OFFSET, fields.p2Y);
    ok = ok && WriteStructValue(snapshot.p1State, XVEL_OFFSET, fields.p1XVel);
    ok = ok && WriteStructValue(snapshot.p1State, YVEL_OFFSET, fields.p1YVel);
    ok = ok && WriteStructValue(snapshot.p2State, XVEL_OFFSET, fields.p2XVel);
    ok = ok && WriteStructValue(snapshot.p2State, YVEL_OFFSET, fields.p2YVel);
    snapshot.modState.p1CpuFlag = clampedP1Cpu;
    snapshot.modState.p2CpuFlag = clampedP2Cpu;
    snapshot.modState.localSide = clampedLocalSide;
    ForceSnapshotControlState(snapshot, clampedLocalSide);
    if (ok) {
        snapshot.dirty = true;
    }
    return ok;
}

bool CaptureSnapshot(Snapshot& outSnapshot, std::string& outReason) {
    outSnapshot = Snapshot{};

    if (IsOnlineBlocked()) {
        outReason = "online mode is active";
        return false;
    }
    if (GetCurrentGameMode() != GameMode::Practice) {
        outReason = "not in practice mode";
        return false;
    }

    uintptr_t base = GetEFZBase();
    uintptr_t battleContext = 0;
    uintptr_t gameStatePtr = GetGameStatePtr();
    uintptr_t p1Base = 0;
    uintptr_t p2Base = 0;
    uint8_t p1CharId = 0xFF;
    uint8_t p2CharId = 0xFF;
    size_t p1Size = 0;
    size_t p2Size = 0;

    if (!base) {
        outReason = "EFZ base not available";
        return false;
    }
    if (!ResolveBattleContext(battleContext) || !battleContext) {
        outReason = "battle context not available";
        return false;
    }
    if (!gameStatePtr) {
        outReason = "game state not available";
        return false;
    }
    if (!ResolveCurrentCharacterPair(p1Base, p2Base, p1CharId, p2CharId, p1Size, p2Size)) {
        outReason = "character state pointers not available";
        return false;
    }

    outSnapshot.p1CharId = p1CharId;
    outSnapshot.p2CharId = p2CharId;
    outSnapshot.revivalVersion = CurrentRevivalVersionValue();
    outSnapshot.battleContext.reserve(kBattleContextSize);
    outSnapshot.cameraSubfield.reserve(kCameraSubfieldSize);
    outSnapshot.gameState.reserve(kGameStateSize);
    outSnapshot.p1State.reserve(p1Size);
    outSnapshot.p2State.reserve(p2Size);
    outSnapshot.renderBitmap.reserve(kRenderBitmapSize);

    CaptureFpuState(outSnapshot.fpuState);
    SafeReadMemory(base + kFKeyLatchOffset, &outSnapshot.savedFKeyLatch, sizeof(outSnapshot.savedFKeyLatch));
    CaptureModState(outSnapshot.modState);
    if (!ReadSnapshotRuntimeMetadata(gameStatePtr, outSnapshot.stageId, outSnapshot.bgmTrack, outReason)) {
        return false;
    }

    {
        std::ostringstream oss;
        oss << "base=" << PointerHex(base)
            << " battleContext=" << PointerHex(battleContext)
            << " gameState=" << PointerHex(gameStatePtr)
            << " renderBitmap=" << PointerHex(base + kRenderBitmapOffset)
            << " | current=" << DescribeResolvedPair(p1Base, p2Base, p1CharId, p2CharId, p1Size, p2Size)
            << " stage=" << StageTokenForId(outSnapshot.stageId) << '(' << static_cast<unsigned int>(outSnapshot.stageId) << ')'
            << " bgm=" << outSnapshot.bgmTrack
            << " latch=" << Hex32(outSnapshot.savedFKeyLatch)
            << " mod={" << DescribePersistedModState(outSnapshot.modState) << '}';
        LogSavestateTrace("capture target", oss.str());
    }

    if (!ReadMemoryBlock(battleContext, outSnapshot.battleContext, kBattleContextSize)
        || !ReadMemoryBlock(battleContext + kCameraSubfieldOffset, outSnapshot.cameraSubfield, kCameraSubfieldSize)
        || !ReadMemoryBlock(gameStatePtr, outSnapshot.gameState, kGameStateSize)
        || !ReadMemoryBlock(p1Base, outSnapshot.p1State, p1Size)
        || !ReadMemoryBlock(p2Base, outSnapshot.p2State, p2Size)
        || !ReadMemoryBlock(base + kRenderBitmapOffset, outSnapshot.renderBitmap, kRenderBitmapSize)) {
        outReason = "failed to read one or more snapshot regions";
        return false;
    }

    if (!VerifyEntityRingsAtMemory("capture",
                                   p1Base,
                                   p2Base,
                                   outSnapshot.p1State,
                                   outSnapshot.p2State,
                                   outReason)) {
        return false;
    }

    std::string capturedSoundDetail;
    CaptureSoundState(gameStatePtr, p1Base, p2Base, outSnapshot, capturedSoundDetail);
    LogSavestateTrace("capture sound", capturedSoundDetail);

    outSnapshot.valid = true;
    outSnapshot.dirty = false;
    ForceSnapshotControlState(outSnapshot, outSnapshot.modState.localSide);
    LogSavestateTrace("capture payload", DescribeSnapshotPayload(outSnapshot));
    LogSnapshotStatus("capture", "working snapshot captured | " + DescribeSnapshot(outSnapshot));
    return true;
}

bool CurrentPairMatchesSnapshot(const Snapshot& snapshot, std::string* outReason = nullptr) {
    if (!snapshot.valid) {
        if (outReason) {
            *outReason = "working snapshot is empty";
        }
        return false;
    }

    uintptr_t p1Base = 0;
    uintptr_t p2Base = 0;
    uint8_t p1CharId = 0xFF;
    uint8_t p2CharId = 0xFF;
    size_t p1Size = 0;
    size_t p2Size = 0;
    if (!ResolveCurrentCharacterPair(p1Base, p2Base, p1CharId, p2CharId, p1Size, p2Size)) {
        if (outReason) {
            *outReason = "current character state pointers are not available";
        }
        return false;
    }

    if (p1CharId != snapshot.p1CharId || p2CharId != snapshot.p2CharId) {
        if (outReason) {
            std::ostringstream oss;
            oss << "current pair " << DescribeResolvedPair(p1Base, p2Base, p1CharId, p2CharId, p1Size, p2Size)
                << " does not match saved snapshot " << DescribeSnapshot(snapshot);
            *outReason = oss.str();
        }
        return false;
    }
    if (p1Size != snapshot.p1State.size() || p2Size != snapshot.p2State.size()) {
        if (outReason) {
            std::ostringstream oss;
            oss << "current pair sizes " << DescribeResolvedPair(p1Base, p2Base, p1CharId, p2CharId, p1Size, p2Size)
                << " do not match saved snapshot " << DescribeSnapshot(snapshot);
            *outReason = oss.str();
        }
        return false;
    }
    return true;
}

bool CurrentMatchMatchesSnapshot(const Snapshot& snapshot, std::string* outReason = nullptr) {
    return CurrentPairMatchesSnapshot(snapshot, outReason)
        && CurrentStageMatchesSnapshot(snapshot, outReason);
}

bool RestoreSnapshot(const Snapshot& snapshot, std::string& outReason, bool engineOnlyLocalSideRestore = false) {
    if (!snapshot.valid) {
        outReason = "working snapshot is empty";
        return false;
    }
    if (IsOnlineBlocked()) {
        outReason = "online mode is active";
        return false;
    }
    if (GetCurrentGameMode() != GameMode::Practice) {
        outReason = "not in practice mode";
        return false;
    }
    if (!CurrentVersionMatchesSnapshot(snapshot, &outReason)) {
        return false;
    }
    if (!CurrentMatchMatchesSnapshot(snapshot, &outReason)) {
        return false;
    }

    uintptr_t base = GetEFZBase();
    uintptr_t battleContext = 0;
    uintptr_t gameStatePtr = GetGameStatePtr();
    uintptr_t p1Base = 0;
    uintptr_t p2Base = 0;
    uint8_t p1CharId = 0xFF;
    uint8_t p2CharId = 0xFF;
    size_t p1Size = 0;
    size_t p2Size = 0;
    if (!base) {
        outReason = "EFZ base not available";
        return false;
    }
    if (!ResolveBattleContext(battleContext) || !battleContext) {
        outReason = "battle context not available";
        return false;
    }
    if (!gameStatePtr) {
        outReason = "game state not available";
        return false;
    }
    if (!ResolveCurrentCharacterPair(p1Base, p2Base, p1CharId, p2CharId, p1Size, p2Size)) {
        outReason = "character state pointers not available";
        return false;
    }

    LogSavestateTrace("restore target",
                      "base=" + PointerHex(base)
                          + " battleContext=" + PointerHex(battleContext)
                          + " gameState=" + PointerHex(gameStatePtr)
                          + " renderBitmap=" + PointerHex(base + kRenderBitmapOffset)
                          + " | current=" + DescribeResolvedPair(p1Base, p2Base, p1CharId, p2CharId, p1Size, p2Size)
                          + " | snapshot=" + DescribeSnapshot(snapshot));

    LogSavestateTrace("restore payload", DescribeSnapshotPayload(snapshot));

    const std::string restoreContext = DescribeResolvedPair(p1Base, p2Base, p1CharId, p2CharId, p1Size, p2Size)
        + " | snapshot=" + DescribeSnapshot(snapshot);

    std::vector<uint8_t> gameStateBytes = snapshot.gameState;

    uint8_t currentGameSpeed = 3;
    SafeReadMemory(battleContext + kBattleContextGameSpeedOffset, &currentGameSpeed, sizeof(currentGameSpeed));

    {
        uint8_t savedGameSpeed = 0;
        const bool haveSavedGameSpeed = TryReadSavedGameSpeed(snapshot, savedGameSpeed);
        std::ostringstream oss;
        oss << "currentGameSpeed=" << static_cast<unsigned int>(currentGameSpeed)
            << " savedGameSpeed=";
        if (haveSavedGameSpeed) {
            oss << static_cast<unsigned int>(savedGameSpeed);
        } else {
            oss << "n/a";
        }
        oss << " savedLatch=" << Hex32(snapshot.savedFKeyLatch)
            << " mod={" << DescribePersistedModState(snapshot.modState) << '}';
        LogSavestateTrace("restore pre-write state", oss.str());
    }

    const bool useSelectiveGameStateRestore = snapshot.fromDisk && kSelectiveDiskSnapshotGameStateRestore;
    if (useSelectiveGameStateRestore) {
        std::vector<uint8_t> liveGameState;
        if (!ReadMemoryBlock(gameStatePtr, liveGameState, snapshot.gameState.size())) {
            outReason = "failed to read live gameState for disk restore merge";
            return false;
        }

        std::string mergeDetail;
        if (!BuildDiskRestoreGameStateBytes(snapshot, liveGameState, gameStateBytes, mergeDetail)) {
            outReason = mergeDetail;
            return false;
        }

        LogSavestateTrace("restore write policy", mergeDetail);
    }

    {
        const bool loadCustomPalettes = Config::GetSettings().savestateLoadCustomPalettes;
        const bool canClearCustomPaletteFlags =
            gameStateBytes.size() >= (kGameStateP2CustomPaletteFlagOffset + sizeof(uint32_t));
        bool clearedCustomPaletteFlags = false;
        if (!loadCustomPalettes && canClearCustomPaletteFlags) {
            const uint32_t zeroCustomPaletteFlag = 0;
            std::memcpy(gameStateBytes.data() + kGameStateP1CustomPaletteFlagOffset,
                        &zeroCustomPaletteFlag,
                        sizeof(zeroCustomPaletteFlag));
            std::memcpy(gameStateBytes.data() + kGameStateP2CustomPaletteFlagOffset,
                        &zeroCustomPaletteFlag,
                        sizeof(zeroCustomPaletteFlag));
            clearedCustomPaletteFlags = true;
        }

        std::ostringstream oss;
        oss << "source=" << (snapshot.fromDisk ? "disk" : "working")
            << " loadCustomPalettes=" << (loadCustomPalettes ? 1 : 0)
            << " applied=";
        if (loadCustomPalettes) {
            oss << "snapshot";
        } else {
            oss << (clearedCustomPaletteFlags ? "default" : "unchanged");
        }
        oss << " | " << DescribeSnapshotPaletteState(snapshot);
        LogSavestateTrace("restore palette policy", oss.str());
    }

    SehFailure failure;
    if (!SehCancelAutoActionsAndMacros(failure)) {
        outReason = "exception during auto action cancel";
        LogSavestateSehFailure("restore cancel auto actions", failure, restoreContext);
        return false;
    }

    std::vector<uint8_t> liveBattleBeforeWrite;
    std::vector<uint8_t> liveCameraBeforeWrite;
    std::vector<uint8_t> liveGameStateBeforeWrite;
    std::vector<uint8_t> liveP1BeforeWrite;
    std::vector<uint8_t> liveP2BeforeWrite;
    std::vector<uint8_t> liveRenderBeforeWrite;

    if (ReadMemoryBlock(battleContext, liveBattleBeforeWrite, snapshot.battleContext.size())) {
        LogRestoreWriteDump("battle",
                            battleContext,
                            liveBattleBeforeWrite,
                            snapshot.battleContext,
                            DescribeBattleContextKeyFields(liveBattleBeforeWrite),
                            DescribeBattleContextKeyFields(snapshot.battleContext));
    } else {
        LogSavestateTrace("restore write dump", "label=battle skipped=read-failed addr=" + PointerHex(battleContext));
    }

    if (ReadMemoryBlock(battleContext + kCameraSubfieldOffset, liveCameraBeforeWrite, snapshot.cameraSubfield.size())) {
        LogRestoreWriteDump("camera",
                            battleContext + kCameraSubfieldOffset,
                            liveCameraBeforeWrite,
                            snapshot.cameraSubfield,
                            std::string(),
                            std::string());
    } else {
        LogSavestateTrace("restore write dump", "label=camera skipped=read-failed addr=" + PointerHex(battleContext + kCameraSubfieldOffset));
    }

    if (ReadMemoryBlock(gameStatePtr, liveGameStateBeforeWrite, gameStateBytes.size())) {
        LogRestoreWriteDump("gameState",
                            gameStatePtr,
                            liveGameStateBeforeWrite,
                            gameStateBytes,
                            DescribeGameStateKeyFields(liveGameStateBeforeWrite),
                            DescribeGameStateKeyFields(gameStateBytes));
    } else {
        LogSavestateTrace("restore write dump", "label=gameState skipped=read-failed addr=" + PointerHex(gameStatePtr));
    }

    if (ReadMemoryBlock(p1Base, liveP1BeforeWrite, snapshot.p1State.size())) {
        LogRestoreWriteDump("p1",
                            p1Base,
                            liveP1BeforeWrite,
                            snapshot.p1State,
                            DescribePlayerStateKeyFields(liveP1BeforeWrite),
                            DescribePlayerStateKeyFields(snapshot.p1State));
    } else {
        LogSavestateTrace("restore write dump", "label=p1 skipped=read-failed addr=" + PointerHex(p1Base));
    }

    if (ReadMemoryBlock(p2Base, liveP2BeforeWrite, snapshot.p2State.size())) {
        LogRestoreWriteDump("p2",
                            p2Base,
                            liveP2BeforeWrite,
                            snapshot.p2State,
                            DescribePlayerStateKeyFields(liveP2BeforeWrite),
                            DescribePlayerStateKeyFields(snapshot.p2State));
    } else {
        LogSavestateTrace("restore write dump", "label=p2 skipped=read-failed addr=" + PointerHex(p2Base));
    }

    if (ReadMemoryBlock(base + kRenderBitmapOffset, liveRenderBeforeWrite, snapshot.renderBitmap.size())) {
        LogRestoreWriteDump("render",
                            base + kRenderBitmapOffset,
                            liveRenderBeforeWrite,
                            snapshot.renderBitmap,
                            std::string(),
                            std::string());
    } else {
        LogSavestateTrace("restore write dump", "label=render skipped=read-failed addr=" + PointerHex(base + kRenderBitmapOffset));
    }

    const bool battleOk = WriteMemoryBlock(battleContext, snapshot.battleContext);
    const bool cameraOk = WriteMemoryBlock(battleContext + kCameraSubfieldOffset, snapshot.cameraSubfield);
    const bool gameStateOk = WriteMemoryBlock(gameStatePtr, gameStateBytes);
    const bool p1Ok = WriteMemoryBlock(p1Base, snapshot.p1State);
    const bool p2Ok = WriteMemoryBlock(p2Base, snapshot.p2State);
    // Revival never restores render surfaces; sprites re-derive from logic state and a stale bitmap
    // bleeds. Captured for diagnostics, never written on restore.
    const bool renderOk = true;

    {
        std::ostringstream oss;
        oss << "battle=" << (battleOk ? 1 : 0)
            << " camera=" << (cameraOk ? 1 : 0)
            << " gameState=" << (useSelectiveGameStateRestore ? (gameStateOk ? "merge" : "0") : (gameStateOk ? "1" : "0"))
            << " p1=" << (p1Ok ? 1 : 0)
            << " p2=" << (p2Ok ? 1 : 0)
            << " render=" << (renderOk ? 1 : 0);
        LogSavestateTrace("restore writes", oss.str());
    }

    if (!(battleOk && cameraOk && gameStateOk && p1Ok && p2Ok && renderOk)) {
        outReason = "failed to write one or more snapshot regions";
        return false;
    }

    if (!VerifyEntityRingsAtMemory("restore",
                                   p1Base,
                                   p2Base,
                                   snapshot.p1State,
                                   snapshot.p2State,
                                   outReason)) {
        return false;
    }

    const uint32_t clearLatch = 0;
    const bool clearLatchOk = SafeWriteMemory(base + kFKeyLatchOffset, &clearLatch, sizeof(clearLatch));
    const bool restoreSpeedOk = SafeWriteMemory(battleContext + kBattleContextGameSpeedOffset, &currentGameSpeed, sizeof(currentGameSpeed));
    if (!SehRestoreFpuState(snapshot.fpuState, failure)) {
        outReason = "exception during FPU state restore";
        LogSavestateSehFailure("restore FPU state", failure, restoreContext);
        return false;
    }
    if (!SehRestoreModState(snapshot.modState, engineOnlyLocalSideRestore, failure)) {
        outReason = "exception during mod state restore";
        LogSavestateSehFailure("restore mod state", failure, restoreContext);
        return false;
    }
    if (snapshot.hasSoundState) {
        std::string restoredSoundDetail;
        RestoreSoundState(snapshot, gameStatePtr, p1Base, p2Base, restoredSoundDetail);
        LogSavestateTrace("restore sound", restoredSoundDetail);
    } else {
        LogSavestateTrace("restore sound", "available=0 skipped=legacy snapshot without serialized sound state");
    }
    if (engineOnlyLocalSideRestore) {
        LogSavestateTrace("restore post-write refresh",
                          "skipped during control reconcile; lifecycle reset will refresh session caches");
    } else {
        if (!SehComboOverlayResetState("custom savestate load", failure)) {
            outReason = "exception during combo overlay reset";
            LogSavestateSehFailure("restore combo overlay reset", failure, restoreContext);
            return false;
        }
        if (!SehTickCharacterEnforcements(base, failure)) {
            outReason = "exception during character enforcement refresh";
            LogSavestateSehFailure("restore character enforcements", failure, restoreContext);
            return false;
        }
    }
    {
        uint8_t savedGameSpeed = 0;
        const bool haveSavedGameSpeed = TryReadSavedGameSpeed(snapshot, savedGameSpeed);
        std::ostringstream oss;
        oss << "clearLatch=" << (clearLatchOk ? 1 : 0)
            << " restoreGameSpeed=" << (restoreSpeedOk ? 1 : 0)
            << " gameSpeed=" << static_cast<unsigned int>(currentGameSpeed)
            << " savedGameSpeed=";
        if (haveSavedGameSpeed) {
            oss << static_cast<unsigned int>(savedGameSpeed);
        } else {
            oss << "n/a";
        }
        oss << " savedLatch=" << Hex32(snapshot.savedFKeyLatch)
            << " restoredMod={" << DescribePersistedModState(snapshot.modState) << '}';
        LogSavestateTrace("restore finalize", oss.str());
    }
    LogSnapshotStatus("restore", "working snapshot restored | " + DescribeResolvedPair(p1Base, p2Base, p1CharId, p2CharId, p1Size, p2Size));
    return true;
}

bool WriteFileBytes(FILE* file, const void* data, size_t size) {
    if (!file) {
        return false;
    }
    return size == 0 ? true : std::fwrite(data, 1, size, file) == size;
}

bool ReadFileBytes(FILE* file, void* data, size_t size) {
    if (!file) {
        return false;
    }
    return size == 0 ? true : std::fread(data, 1, size, file) == size;
}

std::string BuildSavestateDirectoryPath() {
    char modulePath[MAX_PATH] = {};
    HMODULE moduleHandle = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&BuildSavestateDirectoryPath),
                           &moduleHandle)
        && moduleHandle != nullptr) {
        GetModuleFileNameA(moduleHandle, modulePath, MAX_PATH);
    }
    if (modulePath[0] == '\0' && GetModuleFileNameA(nullptr, modulePath, MAX_PATH) == 0) {
        return "savestates";
    }

    char* slash = std::strrchr(modulePath, '\\');
    if (slash) {
        *slash = '\0';
    }

    return std::string(modulePath) + "\\savestates";
}

bool EnsureSavestateDirectoryExists() {
    const std::string directory = BuildSavestateDirectoryPath();
    const DWORD attrs = GetFileAttributesA(directory.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    if (CreateDirectoryA(directory.c_str(), nullptr) != 0) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

int ClampDiskSlot(int slot) {
    if (slot < kInitialSnapshotSlot) {
        return kInitialSnapshotSlot;
    }
    if (slot > kMaxDiskSlots) {
        return kMaxDiskSlots;
    }
    return slot;
}

int WrapDiskSlot(int slot) {
    const int slotCount = kMaxDiskSlots - kInitialSnapshotSlot + 1;
    while (slot < kInitialSnapshotSlot) {
        slot += slotCount;
    }
    while (slot > kMaxDiskSlots) {
        slot -= slotCount;
    }
    return slot;
}

std::string BuildLegacySlotPath(int slot) {
    const std::string directory = BuildSavestateDirectoryPath();

    char fileName[96] = {};
    _snprintf_s(fileName, sizeof(fileName), _TRUNCATE, "\\efz_tm_savestate_slot_%02d.bin", slot);
    return directory + fileName;
}

std::string BuildSlotSearchPattern(int slot) {
    const std::string directory = BuildSavestateDirectoryPath();

    char fileName[96] = {};
    _snprintf_s(fileName, sizeof(fileName), _TRUNCATE, "\\efz_tm_savestate_slot_%02d*.bin", slot);
    return directory + fileName;
}

std::string ResolveExistingSlotPath(int slot) {
    const std::string searchPattern = BuildSlotSearchPattern(slot);
    WIN32_FIND_DATAA findData{};
    HANDLE findHandle = FindFirstFileA(searchPattern.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return BuildLegacySlotPath(slot);
    }

    ULARGE_INTEGER bestWriteTime{};
    std::string bestPath;
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }

        ULARGE_INTEGER writeTime{};
        writeTime.LowPart = findData.ftLastWriteTime.dwLowDateTime;
        writeTime.HighPart = findData.ftLastWriteTime.dwHighDateTime;
        if (!bestPath.empty() && writeTime.QuadPart <= bestWriteTime.QuadPart) {
            continue;
        }

        bestWriteTime = writeTime;
        bestPath = BuildSavestateDirectoryPath() + "\\" + findData.cFileName;
    } while (FindNextFileA(findHandle, &findData) != 0);

    FindClose(findHandle);
    return bestPath.empty() ? BuildLegacySlotPath(slot) : bestPath;
}

struct SlotFileDeletionSummary {
    int deleted = 0;
    int failed = 0;
};

SlotFileDeletionSummary DeleteExistingSlotFiles(int slot) {
    SlotFileDeletionSummary summary;
    const std::string searchPattern = BuildSlotSearchPattern(slot);
    WIN32_FIND_DATAA findData{};
    HANDLE findHandle = FindFirstFileA(searchPattern.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return summary;
    }

    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        const std::string filePath = BuildSavestateDirectoryPath() + "\\" + findData.cFileName;
        if (DeleteFileA(filePath.c_str()) != 0) {
            ++summary.deleted;
        } else {
            ++summary.failed;
        }
    } while (FindNextFileA(findHandle, &findData) != 0);

    FindClose(findHandle);
    return summary;
}

std::string BuildSlotPathForWrite(int slot, const Snapshot& snapshot) {
    const std::string directory = BuildSavestateDirectoryPath();
    const std::string p1Token = CharacterFileToken(snapshot.p1CharId);
    const std::string p2Token = CharacterFileToken(snapshot.p2CharId);
    const std::string timestampToken = TimestampFileToken();

    char fileName[256] = {};
    _snprintf_s(fileName,
                sizeof(fileName),
                _TRUNCATE,
                "\\efz_tm_savestate_slot_%02d__%s_vs_%s__%s.bin",
                slot,
                p1Token.c_str(),
                p2Token.c_str(),
                timestampToken.c_str());
    return directory + fileName;
}

std::string BuildSlotPath(int slot) {
    if (slot == kInitialSnapshotSlot) {
        return "Initial practice snapshot (memory only)";
    }
    return ResolveExistingSlotPath(slot);
}

bool ValidDiskSlot(int slot) {
    return slot >= 1 && slot <= kMaxDiskSlots;
}

}

namespace CustomSavestate {

BackendMode GetConfiguredBackendMode() {
    const int rawMode = Config::GetSettings().savestateBackendMode;
    if (rawMode < 0 || rawMode > 2) {
        return BackendMode::CustomWithRevivalFallback;
    }
    return static_cast<BackendMode>(rawMode);
}

const char* BackendModeName(BackendMode mode) {
    switch (mode) {
    case BackendMode::Custom:
        return "CUSTOM";
    case BackendMode::Revival:
        return "REVIVAL";
    case BackendMode::CustomWithRevivalFallback:
        return "CUSTOM+FALLBACK";
    default:
        return "UNKNOWN";
    }
}

bool Install() {
    s_installed.store(true, std::memory_order_release);
    s_activeDiskSlot.store(kInitialSnapshotSlot, std::memory_order_relaxed);
    s_hotswapRestoreApplied.store(false, std::memory_order_release);
    ClearDeferredPracticeSideRestoreState();
    SetLastStatus("[SAVESTATE] custom backend installed");
    return true;
}

void Uninstall() {
    {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        s_initialSnapshot = Snapshot{};
        ClearWorkingSnapshotLocked();
    }
    s_activeDiskSlot.store(kInitialSnapshotSlot, std::memory_order_relaxed);
    s_hotswapRestoreApplied.store(false, std::memory_order_release);
    ClearDeferredPracticeSideRestoreState();
    s_installed.store(false, std::memory_order_release);
    SetLastStatus("[SAVESTATE] custom backend uninstalled");
}

bool IsInstalled() {
    return s_installed.load(std::memory_order_acquire);
}

bool IsRestoreInProgress() {
    return s_restoreInProgress.load(std::memory_order_acquire);
}

bool ConsumePostRestoreStabilizationFrame() {
    int remaining = s_postRestoreStabilizationFrames.load(std::memory_order_acquire);
    if (remaining <= 0) {
        return false;
    }

    s_postRestoreStabilizationFrames.store(remaining - 1, std::memory_order_release);

    if (remaining == kPostRestoreStabilizationFrames || remaining == 1) {
        std::ostringstream oss;
        oss << "remainingBefore=" << remaining
            << " remainingAfter=" << (remaining - 1);
        LogSavestateTrace("restore stabilize consume", oss.str());
    }
    return true;
}

unsigned int GetSaveCount() {
    return s_saveCount.load(std::memory_order_relaxed);
}

unsigned int GetLoadCount() {
    return s_loadCount.load(std::memory_order_relaxed);
}

SnapshotActionResult RestoreWorkingSnapshotInternalImpl(std::string& outReason,
                                                       const char* postRestoreResetReason = nullptr,
                                                       bool preserveCurrentLocalSide = false,
                                                       bool engineOnlyLocalSideRestore = false) {
    Snapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        snapshot = s_workingSnapshot;
    }

    if (!snapshot.valid) {
        outReason = "working snapshot is empty";
        return SnapshotActionResult::NoSnapshot;
    }

    if (preserveCurrentLocalSide) {
        const int liveLocalSide = SwitchPlayers::GetLocalSide();
        uint8_t liveP1CpuFlag = snapshot.modState.p1CpuFlag;
        uint8_t liveP2CpuFlag = snapshot.modState.p2CpuFlag;
        if (uintptr_t liveGameStatePtr = GetGameStatePtr()) {
            SafeReadMemory(liveGameStatePtr + GAMESTATE_OFF_P1_CPU_FLAG, &liveP1CpuFlag, sizeof(liveP1CpuFlag));
            SafeReadMemory(liveGameStatePtr + GAMESTATE_OFF_P2_CPU_FLAG, &liveP2CpuFlag, sizeof(liveP2CpuFlag));
        }
        if (liveLocalSide >= 0 && snapshot.modState.localSide != liveLocalSide) {
            LogSavestateTrace("restore local side policy",
                              "preserving live localSide=" + std::to_string(liveLocalSide)
                                  + " over snapshot localSide=" + std::to_string(snapshot.modState.localSide)
                                  + " for savestate restore");
            snapshot.modState.localSide = liveLocalSide;
        }
        if (snapshot.modState.p1CpuFlag != liveP1CpuFlag || snapshot.modState.p2CpuFlag != liveP2CpuFlag) {
            LogSavestateTrace("restore control policy",
                              "preserving live CPU flags p1=" + std::to_string(static_cast<unsigned int>(liveP1CpuFlag))
                                  + " p2=" + std::to_string(static_cast<unsigned int>(liveP2CpuFlag))
                                  + " over snapshot p1=" + std::to_string(static_cast<unsigned int>(snapshot.modState.p1CpuFlag))
                                  + " p2=" + std::to_string(static_cast<unsigned int>(snapshot.modState.p2CpuFlag))
                                  + " for savestate restore");
            snapshot.modState.p1CpuFlag = liveP1CpuFlag;
            snapshot.modState.p2CpuFlag = liveP2CpuFlag;
        }
    }

    LogSavestateTrace("restore begin", DescribeSnapshot(snapshot));

    const uintptr_t cachedP1Base = GetPlayerBase(1);
    const uintptr_t cachedP2Base = GetPlayerBase(2);
    {
        std::ostringstream oss;
        oss << "mode=" << GetGameModeName(GetCurrentGameMode())
            << " phase=" << static_cast<int>(GetCurrentGamePhase())
            << " cachedBaseBeforeRefresh p1=" << PointerHex(cachedP1Base)
            << " p2=" << PointerHex(cachedP2Base);
        LogSavestateTrace("restore preflight", oss.str());
    }

    InvalidatePlayerBaseCache();
    CharacterSettings::InvalidateAllCharacterPointerCaches();

    uintptr_t resolvedP1Base = 0;
    uintptr_t resolvedP2Base = 0;
    uint8_t resolvedP1CharId = 0xFF;
    uint8_t resolvedP2CharId = 0xFF;
    size_t resolvedP1Size = 0;
    size_t resolvedP2Size = 0;
    if (ResolveCurrentCharacterPair(resolvedP1Base,
                                    resolvedP2Base,
                                    resolvedP1CharId,
                                    resolvedP2CharId,
                                    resolvedP1Size,
                                    resolvedP2Size)) {
        LogSavestateTrace("restore re-resolve",
                          DescribeResolvedPair(resolvedP1Base,
                                              resolvedP2Base,
                                              resolvedP1CharId,
                                              resolvedP2CharId,
                                              resolvedP1Size,
                                              resolvedP2Size));
    } else {
        LogSavestateTrace("restore re-resolve", "current character pair unavailable after cache refresh");
    }

    if (!CurrentVersionMatchesSnapshot(snapshot, &outReason)
        || !CurrentMatchMatchesSnapshot(snapshot, &outReason)) {
        LogSavestateTrace("restore blocked", outReason);
        return SnapshotActionResult::GuardBlocked;
    }

    const bool restoreStartedFromOwnedFramestepPause = Framestep::OwnsPauseState();
    Framestep::CancelActiveState(engineOnlyLocalSideRestore
                                     ? "savestate restore control reconcile"
                                     : "savestate restore");

    const bool shouldRefreshSessionScratch = snapshot.fromDisk || engineOnlyLocalSideRestore;
    if (shouldRefreshSessionScratch) {
        std::string sessionScratchDetail;
        const char* sessionScratchSource = snapshot.fromDisk
            ? "disk"
            : (engineOnlyLocalSideRestore ? "control-reconcile" : "memory");
        if (RefreshRestoreSessionScratch(snapshot, sessionScratchDetail)) {
            LogSavestateTrace("restore session scratch refresh",
                              std::string("source=") + sessionScratchSource + " | " + sessionScratchDetail);
        } else {
            LogSavestateTrace("restore session scratch refresh",
                              std::string("source=") + sessionScratchSource + " | skipped | " + sessionScratchDetail);
        }
    }

    // Pointer rehydration runs for every restore; skipping it would corrupt stage/effect/bullet
    // pointers. The Rumi carve-out is field-selective in RefreshRestoreTargetPlayerStatePointers.
    std::string pointerRefreshDetail;
    if (RefreshRestoreTargetSessionPointers(snapshot, pointerRefreshDetail)) {
        LogSavestateTrace("restore pointer refresh", pointerRefreshDetail);
    } else {
        LogSavestateTrace("restore pointer refresh", "skipped | " + pointerRefreshDetail);
    }

    if (!RestoreSnapshot(snapshot, outReason, engineOnlyLocalSideRestore)) {
        LogSavestateTrace("restore failed", outReason);
        if (outReason == "online mode is active" || outReason == "not in practice mode") {
            return SnapshotActionResult::GuardBlocked;
        }
        return SnapshotActionResult::RuntimeFailure;
    }
    if (postRestoreResetReason && postRestoreResetReason[0] != '\0') {
        if (!ApplyImmediatePostRestoreReset(postRestoreResetReason, &outReason)) {
            LogSavestateTrace("restore failed", outReason);
            return SnapshotActionResult::RuntimeFailure;
        }
    }
    Framestep::FinishSavestateRestore(restoreStartedFromOwnedFramestepPause);
    s_postRestoreStabilizationFrames.store(kPostRestoreStabilizationFrames, std::memory_order_release);
    LogSavestateTrace("restore stabilize arm",
                      "skipHeavyFrames=" + std::to_string(kPostRestoreStabilizationFrames));
    LogSavestateTrace("restore success", DescribeSnapshot(snapshot));
    return SnapshotActionResult::Success;
}

bool SehRestoreWorkingSnapshotInternalCall(std::string* outReason,
                                           const char* postRestoreResetReason,
                                           bool preserveCurrentLocalSide,
                                           bool engineOnlyLocalSideRestore,
                                           SnapshotActionResult* outResult,
                                           SehFailure* outFailure) {
    if (!outReason || !outResult || !outFailure) {
        return false;
    }

    *outFailure = SehFailure{};
    *outResult = SnapshotActionResult::RuntimeFailure;
    __try {
        *outResult = RestoreWorkingSnapshotInternalImpl(*outReason,
                                                       postRestoreResetReason,
                                                       preserveCurrentLocalSide,
                                                       engineOnlyLocalSideRestore);
        return true;
    } __except (CaptureSehFailure(outFailure, GetExceptionInformation())) {
        return false;
    }
}

SnapshotActionResult RestoreWorkingSnapshotInternal(std::string& outReason,
                                                   const char* postRestoreResetReason = nullptr,
                                                   bool preserveCurrentLocalSide = false,
                                                   bool engineOnlyLocalSideRestore = false) {
    SehFailure failure;
    s_restoreInProgress.store(true, std::memory_order_release);
    SnapshotActionResult result = SnapshotActionResult::RuntimeFailure;
    const bool completed = SehRestoreWorkingSnapshotInternalCall(&outReason,
                                                                 postRestoreResetReason,
                                                                 preserveCurrentLocalSide,
                                                                 engineOnlyLocalSideRestore,
                                                                 &result,
                                                                 &failure);
    if (!completed) {
        outReason = "structured exception during savestate restore";
    }
    s_restoreInProgress.store(false, std::memory_order_release);

    if (!completed) {
        std::ostringstream detail;
         detail << "postReset=" << (postRestoreResetReason ? postRestoreResetReason : "")
             << " preserveCurrentLocalSide=" << (preserveCurrentLocalSide ? 1 : 0)
                         << " engineOnlyLocalSideRestore=" << (engineOnlyLocalSideRestore ? 1 : 0)
               << " mode=" << GetGameModeName(GetCurrentGameMode())
               << " phase=" << static_cast<int>(GetCurrentGamePhase());
        LogSavestateSehFailure("restore top-level", failure, detail.str());
        return SnapshotActionResult::RuntimeFailure;
    }

    return result;
}

bool LoadSnapshotFromSlotIntoWorkingImpl(int slot, std::string& outReason, bool emitFeedback) {
    if (slot == kInitialSnapshotSlot) {
        Snapshot snapshot;
        {
            std::lock_guard<std::mutex> lock(s_snapshotMutex);
            snapshot = s_initialSnapshot;
        }

        if (!snapshot.valid) {
            outReason = "initial practice snapshot is empty";
            if (emitFeedback) {
                LogSnapshotFailure("slot load", outReason);
                ShowSnapshotMessage("Initial Slot Empty", RGB(255, 180, 120));
            }
            return false;
        }

        std::string slot0NormalizeDetail;
        if (NormalizeInitialSnapshotRoundStartState(snapshot, slot0NormalizeDetail)) {
            LogSavestateTrace("disk load slot0 normalize", slot0NormalizeDetail);
        }

        LogSavestateTrace("disk load slot0", DescribeSnapshot(snapshot));
        LogSavestateTrace("disk load slot0 payload", DescribeSnapshotPayload(snapshot));

        {
            std::lock_guard<std::mutex> lock(s_snapshotMutex);
            AssignWorkingSnapshotLocked(std::move(snapshot));
        }

        if (emitFeedback) {
            LogSnapshotStatus("slot load", "initial slot loaded into working snapshot");
            ShowSnapshotMessage("Initial State Loaded", RGB(180, 255, 180));
        }
        return true;
    }

    if (!ValidDiskSlot(slot)) {
        outReason = "invalid slot";
        if (emitFeedback) {
            LogSnapshotFailure("disk load", outReason);
        }
        return false;
    }

    const std::string path = BuildSlotPath(slot);
    FILE* file = nullptr;
    const errno_t openError = fopen_s(&file, path.c_str(), "rb");
    if (openError != 0 || !file) {
        outReason = "slot file does not exist";
        LogSavestateTrace("disk load open",
                          "slot=" + std::to_string(slot)
                              + " path=" + path
                              + " errno=" + std::to_string(static_cast<int>(openError)));
        if (emitFeedback) {
            LogSnapshotFailure("disk load", outReason);
            ShowSnapshotMessage("Slot File Missing", RGB(255, 180, 120));
        }
        return false;
    }

    const long fileSize = QueryFileSize(file);
    LogSavestateTrace("disk load begin",
                      "slot=" + std::to_string(slot)
                          + " path=" + path
                          + " fileSize=" + std::to_string(fileSize)
                          + " emitFeedback=" + std::to_string(emitFeedback ? 1 : 0));

    SnapshotFileHeader header{};
    if (!ReadFileBytes(file, &header, sizeof(header))) {
        const int readError = std::ferror(file);
        std::fclose(file);
        outReason = "failed to read file header";
        LogSavestateTrace("disk load header",
                          "slot=" + std::to_string(slot)
                              + " path=" + path
                              + " bytesRead=0/" + std::to_string(sizeof(header))
                              + " ferror=" + std::to_string(readError));
        if (emitFeedback) {
            LogSnapshotFailure("disk load", outReason);
            ShowSnapshotMessage("Disk Load Failed", RGB(255, 120, 120));
        }
        return false;
    }

    LogSavestateTrace("disk load header", DescribeFileHeader(header));

    uint32_t savedRevivalVersion = 0;
    uint32_t savedStageId = 0xFF;
    uint32_t savedBgmTrack = 0;
    uint32_t savedSoundStateBytes = 0;
    if (header.version == kFileVersion2 || header.version == kFileVersion3 || header.version == kFileVersion4) {
        if (!ReadFileBytes(file, &savedRevivalVersion, sizeof(savedRevivalVersion))) {
            std::fclose(file);
            outReason = "failed to read snapshot version metadata";
            if (emitFeedback) {
                LogSnapshotFailure("disk load", outReason);
                ShowSnapshotMessage("Disk Load Failed", RGB(255, 120, 120));
            }
            return false;
        }
        if (header.version == kFileVersion3 || header.version == kFileVersion4) {
            if (!ReadFileBytes(file, &savedStageId, sizeof(savedStageId))
                || !ReadFileBytes(file, &savedBgmTrack, sizeof(savedBgmTrack))) {
                std::fclose(file);
                outReason = "failed to read snapshot stage metadata";
                if (emitFeedback) {
                    LogSnapshotFailure("disk load", outReason);
                    ShowSnapshotMessage("Disk Load Failed", RGB(255, 120, 120));
                }
                return false;
            }
        }
        if (header.version == kFileVersion4) {
            if (!ReadFileBytes(file, &savedSoundStateBytes, sizeof(savedSoundStateBytes))) {
                std::fclose(file);
                outReason = "failed to read snapshot sound metadata";
                if (emitFeedback) {
                    LogSnapshotFailure("disk load", outReason);
                    ShowSnapshotMessage("Disk Load Failed", RGB(255, 120, 120));
                }
                return false;
            }
        }
    }

    if (header.version == kFileVersion4) {
        if ((savedSoundStateBytes % sizeof(SoundPlaybackEntry)) != 0) {
            std::fclose(file);
            outReason = "snapshot sound state size is misaligned";
            if (emitFeedback) {
                LogSnapshotFailure("disk load", outReason);
                ShowSnapshotMessage("Unsupported Snapshot", RGB(255, 120, 120));
            }
            return false;
        }
        if ((savedSoundStateBytes / sizeof(SoundPlaybackEntry)) > kMaxSerializedSoundEntries) {
            std::fclose(file);
            outReason = "snapshot sound state exceeds hard limit";
            if (emitFeedback) {
                LogSnapshotFailure("disk load", outReason);
                ShowSnapshotMessage("Unsupported Snapshot", RGB(255, 120, 120));
            }
            return false;
        }
    }

    {
        std::ostringstream oss;
        oss << "savedRevivalVersion=" << savedRevivalVersion
            << " savedStageId=" << savedStageId
            << " savedBgmTrack=" << savedBgmTrack
            << " soundStateBytes=" << savedSoundStateBytes
            << " expectedFileBytes=" << PersistedSnapshotSizeBytes(header, savedSoundStateBytes);
        LogSavestateTrace("disk load metadata", oss.str());
    }

    std::string headerValidationReason;
    if (!ValidateSnapshotFileHeader(header, headerValidationReason)) {
        std::fclose(file);
        outReason = headerValidationReason;
        LogSavestateTrace("disk load invalid header",
                          "slot=" + std::to_string(slot)
                              + " path=" + path
                              + " reason=" + outReason
                              + " | " + DescribeFileHeader(header));
        if (emitFeedback) {
            LogSnapshotFailure("disk load", outReason);
            ShowSnapshotMessage("Unsupported Snapshot", RGB(255, 120, 120));
        }
        return false;
    }

    Snapshot snapshot;
    snapshot.valid = true;
    snapshot.dirty = false;
    snapshot.fromDisk = true;
    snapshot.p1CharId = static_cast<uint8_t>(header.p1CharId);
    snapshot.p2CharId = static_cast<uint8_t>(header.p2CharId);
    snapshot.stageId = savedStageId <= 0xFFu ? static_cast<uint8_t>(savedStageId) : 0xFF;
    snapshot.bgmTrack = static_cast<uint16_t>(savedBgmTrack);
    snapshot.revivalVersion = savedRevivalVersion;
    snapshot.savedFKeyLatch = header.savedFKeyLatch;
    snapshot.hasSoundState = header.version == kFileVersion4;
    snapshot.battleContext.assign(header.battleContextSize, 0);
    snapshot.cameraSubfield.assign(header.cameraSubfieldSize, 0);
    snapshot.gameState.assign(header.gameStateSize, 0);
    snapshot.p1State.assign(header.p1StateSize, 0);
    snapshot.p2State.assign(header.p2StateSize, 0);
    snapshot.renderBitmap.assign(header.renderBitmapSize, 0);
    snapshot.soundState.assign(savedSoundStateBytes / sizeof(SoundPlaybackEntry), SoundPlaybackEntry{});

    const bool ok = ReadFileBytes(file, snapshot.fpuState, sizeof(snapshot.fpuState))
        && ReadFileBytes(file, &snapshot.modState, sizeof(snapshot.modState))
        && ReadFileBytes(file,
                         snapshot.soundState.empty() ? nullptr : snapshot.soundState.data(),
                         savedSoundStateBytes)
        && ReadFileBytes(file, snapshot.battleContext.data(), snapshot.battleContext.size())
        && ReadFileBytes(file, snapshot.cameraSubfield.data(), snapshot.cameraSubfield.size())
        && ReadFileBytes(file, snapshot.gameState.data(), snapshot.gameState.size())
        && ReadFileBytes(file, snapshot.p1State.data(), snapshot.p1State.size())
        && ReadFileBytes(file, snapshot.p2State.data(), snapshot.p2State.size())
        && ReadFileBytes(file, snapshot.renderBitmap.data(), snapshot.renderBitmap.size());
    const long finalOffset = std::ftell(file);
    const int readError = std::ferror(file);
    const long trailingBytes = (fileSize >= 0 && finalOffset >= 0) ? (fileSize - finalOffset) : -1;
    std::fclose(file);

    std::string sessionScratchDetail;
    const bool refreshedSessionScratch = ok && RefreshRestoreSessionScratch(snapshot, sessionScratchDetail);
    if (ok) {
        std::ostringstream oss;
        oss << "slot=" << slot
            << " refreshed=" << (refreshedSessionScratch ? 1 : 0);
        if (!sessionScratchDetail.empty()) {
            oss << " detail=" << sessionScratchDetail;
        }
        LogSavestateTrace("disk load session refresh", oss.str());
    }

    LogSavestateTrace("disk load payload", DescribeSnapshot(snapshot));
    LogSavestateTrace("disk load payload detail", DescribeSnapshotPayload(snapshot));
    {
        std::ostringstream oss;
        oss << "slot=" << slot
            << " path=" << path
            << " ok=" << (ok ? 1 : 0)
            << " fileSize=" << fileSize
            << " finalOffset=" << finalOffset
            << " trailingBytes=" << trailingBytes
            << " expectedFileBytes=" << PersistedSnapshotSizeBytes(header, savedSoundStateBytes)
            << " ferror=" << readError;
        LogSavestateTrace("disk load result", oss.str());
    }

    if (!ok) {
        outReason = "failed while reading file contents";
        if (emitFeedback) {
            LogSnapshotFailure("disk load", outReason);
            ShowSnapshotMessage("Disk Load Failed", RGB(255, 120, 120));
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        AssignWorkingSnapshotLocked(std::move(snapshot));
    }

    if (emitFeedback) {
        LogSnapshotStatus("disk load", "disk slot loaded into working snapshot");
        ShowSnapshotMessage("State Loaded From Disk", RGB(180, 255, 180));
    }
    return true;
}

bool SehLoadSnapshotFromSlotIntoWorkingCall(int slot,
                                            std::string* outReason,
                                            bool emitFeedback,
                                            bool* outLoaded,
                                            SehFailure* outFailure) {
    if (!outReason || !outLoaded || !outFailure) {
        return false;
    }

    *outFailure = SehFailure{};
    *outLoaded = false;
    __try {
        *outLoaded = LoadSnapshotFromSlotIntoWorkingImpl(slot, *outReason, emitFeedback);
        return true;
    } __except (CaptureSehFailure(outFailure, GetExceptionInformation())) {
        return false;
    }
}

bool LoadSnapshotFromSlotIntoWorking(int slot, std::string& outReason, bool emitFeedback) {
    SehFailure failure;
    bool loaded = false;
    const bool completed = SehLoadSnapshotFromSlotIntoWorkingCall(slot,
                                                                  &outReason,
                                                                  emitFeedback,
                                                                  &loaded,
                                                                  &failure);
    if (!completed) {
        outReason = "structured exception during disk snapshot load";
    }

    if (!completed) {
        std::ostringstream detail;
        detail << "slot=" << slot
               << " emitFeedback=" << (emitFeedback ? 1 : 0)
               << " activeSlot=" << s_activeDiskSlot.load(std::memory_order_relaxed);
        LogSavestateSehFailure("disk load top-level", failure, detail.str());
        if (emitFeedback) {
            LogSnapshotFailure("disk load", outReason);
        }
        return false;
    }

    return loaded;
}

HotkeyHandleResult HandlePracticeHotkey(void* practiceController, int keyCode) {
    if (!practiceController || !IsInstalled() || IsOnlineBlocked()) {
        return HotkeyHandleResult::NotHandled;
    }
    if (GetCurrentGameMode() != GameMode::Practice) {
        return HotkeyHandleResult::NotHandled;
    }

    const BackendMode backendMode = GetConfiguredBackendMode();
    if (backendMode == BackendMode::Revival) {
        return HotkeyHandleResult::NotHandled;
    }

    const uintptr_t practice = reinterpret_cast<uintptr_t>(practiceController);
    const uintptr_t saveOffset = EFZ_Practice_SaveHotkeyOffset();
    const uintptr_t loadOffset = EFZ_Practice_LoadHotkeyOffset();
    int saveKey = 0;
    int loadKey = 0;

    if (!saveOffset || !loadOffset
        || !SafeReadMemory(practice + saveOffset, &saveKey, sizeof(saveKey))
        || !SafeReadMemory(practice + loadOffset, &loadKey, sizeof(loadKey))) {
        return HotkeyHandleResult::NotHandled;
    }

    if (saveKey != 0 && keyCode == saveKey) {
        SaveSelectedSlot();
        return HotkeyHandleResult::Consumed;
    }
    if (loadKey != 0 && keyCode == loadKey) {
        LoadSelectedSlot();
        return HotkeyHandleResult::Consumed;
    }
    return HotkeyHandleResult::NotHandled;
}

bool CapturePracticeEntrySnapshot() {
    if (!IsInstalled() || GetConfiguredBackendMode() == BackendMode::Revival || IsOnlineBlocked()) {
        return false;
    }

    Snapshot snapshot;
    std::string reason;
    if (!CaptureSnapshot(snapshot, reason)) {
        return false;
    }
    ForceSnapshotControlState(snapshot, 0);

    LogSavestateTrace("practice entry capture", DescribeSnapshot(snapshot));

    Snapshot pendingRestoreSnapshot;
    bool hasPendingRestore = false;

    {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        s_initialSnapshot = snapshot;
        s_initialSnapshot.dirty = false;
        if (s_pendingHotswapRestoreSnapshot.valid) {
            pendingRestoreSnapshot = s_pendingHotswapRestoreSnapshot;
            hasPendingRestore = true;
        } else {
            AssignWorkingSnapshotLocked(std::move(snapshot));
        }
    }

    s_activeDiskSlot.store(kInitialSnapshotSlot, std::memory_order_relaxed);

    if (!hasPendingRestore) {
        s_hotswapRestoreApplied.store(false, std::memory_order_release);
        LogSavestateTrace("practice entry handoff", "no pending hotswap restore; round-start snapshot stored to slot 0 and working snapshot refreshed");
        SetLastStatus("[SAVESTATE] practice-entry snapshot captured into slot 0");
        return true;
    }

    LogSavestateTrace("practice entry handoff",
                      "pending hotswap restore detected | roundStart=" + DescribeSnapshot(snapshot)
                          + " | pending=" + DescribeSnapshot(pendingRestoreSnapshot));

    {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        AssignWorkingSnapshotLocked(std::move(pendingRestoreSnapshot));
    }

    s_hotswapRestoreApplied.store(false, std::memory_order_release);

    std::string restoreReason;
    const SnapshotActionResult result = RestoreWorkingSnapshotInternal(restoreReason,
                                                                      nullptr,
                                                                      false,
                                                                      true);
    if (result != SnapshotActionResult::Success) {
        LogSnapshotFailure("hotswap restore", restoreReason);
        ShowSnapshotMessage(result == SnapshotActionResult::GuardBlocked ? "State Load Blocked" : "State Load Failed",
                            RGB(255, 120, 120));
        return false;
    }

    s_hotswapRestoreApplied.store(true, std::memory_order_release);
    s_loadCount.fetch_add(1, std::memory_order_relaxed);
    LogSnapshotStatus("hotswap restore", "working snapshot restored after hotswap | " + DescribeSnapshot(s_workingSnapshot));
    ShowSnapshotMessage("State Loaded", RGB(100, 255, 100));
    return true;
}

bool CaptureWorkingSnapshot() {
    Snapshot snapshot;
    std::string reason;
    if (!CaptureSnapshot(snapshot, reason)) {
        LogSnapshotFailure("capture", reason);
        ShowSnapshotMessage("State Save Failed", RGB(255, 120, 120));
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        AssignWorkingSnapshotLocked(std::move(snapshot));
    }

    s_saveCount.fetch_add(1, std::memory_order_relaxed);
    ShowSnapshotMessage("State Saved", RGB(255, 255, 100));
    return true;
}

bool RestoreWorkingSnapshot() {
    std::string reason;
    if (!QueueDeferredRestoreRequest(DeferredRestoreKind::WorkingSnapshot,
                                     GetActiveDiskSlot(),
                                     "RestoreWorkingSnapshot",
                                     reason)) {
        LogSnapshotFailure("restore queue", reason);
        ShowSnapshotMessage("State Load Failed", RGB(255, 120, 120));
        return false;
    }

    ShowSnapshotMessage("State Load Queued", RGB(180, 220, 255));
    return true;
}

bool ProcessQueuedRestoreAtFrameBoundaryImpl(bool inPracticeMatch, bool charactersInitialized) {
    const DeferredRestoreKind kind = s_deferredRestoreKind.load(std::memory_order_acquire);
    if (kind == DeferredRestoreKind::None) {
        return false;
    }

    if (!inPracticeMatch || !charactersInitialized) {
        return false;
    }

    const int slot = s_deferredRestoreSlot.load(std::memory_order_acquire);
    s_deferredRestoreKind.store(DeferredRestoreKind::None, std::memory_order_release);
    s_deferredRestoreSlot.store(kInitialSnapshotSlot, std::memory_order_release);

    {
        std::ostringstream oss;
        oss << "kind=" << DeferredRestoreKindName(kind)
            << " slot=" << slot
            << " mode=" << GetGameModeName(GetCurrentGameMode())
            << " phase=" << static_cast<int>(GetCurrentGamePhase())
            << " charsInit=" << (charactersInitialized ? 1 : 0);
        LogSavestateTrace("restore queue consume", oss.str());
    }

    std::string reason;
    const char* resetReason = (kind == DeferredRestoreKind::SelectedSlot)
        ? "custom savestate slot restore immediate"
        : "custom savestate restore immediate";
    const bool preserveCurrentLocalSide = false;
    const bool restoreInitialSlotControlViaDeferredPracticeRemap =
        kind == DeferredRestoreKind::SelectedSlot && slot == kInitialSnapshotSlot;
    const SnapshotActionResult result = RestoreWorkingSnapshotInternal(reason,
                                                                       resetReason,
                                                                       preserveCurrentLocalSide,
                                                                       restoreInitialSlotControlViaDeferredPracticeRemap);
    if (result != SnapshotActionResult::Success) {
        LogSnapshotFailure(kind == DeferredRestoreKind::SelectedSlot ? "slot restore" : "restore", reason);
        ShowSnapshotMessage(result == SnapshotActionResult::GuardBlocked ? "State Load Blocked" : "State Load Failed",
                            RGB(255, 120, 120));
        return true;
    }

    s_loadCount.fetch_add(1, std::memory_order_relaxed);
    if (kind == DeferredRestoreKind::SelectedSlot) {
        if (slot == kInitialSnapshotSlot) {
            LogSnapshotStatus("slot restore", "initial slot restored to live state");
            ShowSnapshotMessage("Initial State Loaded", RGB(100, 255, 100));
        } else {
            LogSnapshotStatus("slot restore", "slot " + std::to_string(slot) + " restored to live state");
            ShowSnapshotMessage("State Loaded", RGB(100, 255, 100));
        }
    } else {
        ShowSnapshotMessage("State Loaded", RGB(100, 255, 100));
    }
    return true;
}

bool SehProcessQueuedRestoreAtFrameBoundaryCall(bool inPracticeMatch,
                                                bool charactersInitialized,
                                                bool* outConsumed,
                                                SehFailure* outFailure) {
    if (!outConsumed || !outFailure) {
        return false;
    }

    *outFailure = SehFailure{};
    *outConsumed = false;
    __try {
        *outConsumed = ProcessQueuedRestoreAtFrameBoundaryImpl(inPracticeMatch, charactersInitialized);
        return true;
    } __except (CaptureSehFailure(outFailure, GetExceptionInformation())) {
        return false;
    }
}

bool ProcessQueuedRestoreAtFrameBoundary(bool inPracticeMatch, bool charactersInitialized) {
    SehFailure failure;
    bool consumed = false;
    const bool completed = SehProcessQueuedRestoreAtFrameBoundaryCall(inPracticeMatch,
                                                                      charactersInitialized,
                                                                      &consumed,
                                                                      &failure);

    if (!completed) {
        std::ostringstream detail;
        detail << "inPracticeMatch=" << (inPracticeMatch ? 1 : 0)
               << " charactersInitialized=" << (charactersInitialized ? 1 : 0)
               << " queuedKind=" << DeferredRestoreKindName(s_deferredRestoreKind.load(std::memory_order_acquire))
               << " queuedSlot=" << s_deferredRestoreSlot.load(std::memory_order_acquire);
        LogSavestateSehFailure("restore queue boundary", failure, detail.str());
        s_deferredRestoreKind.store(DeferredRestoreKind::None, std::memory_order_release);
        s_deferredRestoreSlot.store(kInitialSnapshotSlot, std::memory_order_release);
        return true;
    }

    return consumed;
}

void TickDeferredPracticeSideRestore(bool charactersInitialized) {
    if (s_sideRestorePhase.load(std::memory_order_acquire) == SideRestorePhase::Idle) return;
    const int localSide = s_deferredPracticeSideRestoreLocalSide.load(std::memory_order_acquire);
    if (localSide != 0 && localSide != 1) {
        return;
    }

    if (s_postRestoreStabilizationFrames.load(std::memory_order_acquire) > 0) {
        return;
    }

    if (GetCurrentGameMode() != GameMode::Practice || !IsMatchPhase()) {
        LogSavestateTrace("restore practice reconcile cancel",
                          "aborted because practice match is no longer active");
        ClearDeferredPracticeSideRestoreState();
        return;
    }

    if (!charactersInitialized) {
        return;
    }

    const int delay = s_deferredPracticeSideRestoreDelay.load(std::memory_order_acquire);
    if (s_sideRestorePhase.load(std::memory_order_acquire) == SideRestorePhase::Delaying && delay > 0) {
        s_deferredPracticeSideRestoreDelay.store(delay - 1, std::memory_order_release);
        if (delay == 1) BeginDeferredPracticeSideRestoreRetry();
        if (delay == kDeferredPracticeSideRestoreDelayFrames || delay == 1) {
            std::ostringstream oss;
            oss << "localSide=" << localSide
                << " delayBefore=" << delay
                << " delayAfter=" << (delay - 1);
            LogSavestateTrace("restore practice reconcile wait", oss.str());
        }
        return;
    }

    BeginDeferredPracticeSideRestoreRetry();
    int retries = s_deferredPracticeSideRestoreRetries.load(std::memory_order_acquire);
    if (retries <= 0) {
        LogSavestateTrace("restore practice reconcile fail",
                          "retry budget exhausted before Practice remap completed");
        ClearDeferredPracticeSideRestoreState();
        return;
    }

    PauseIntegration::EnsurePracticePointerCapture();
    void* cachedPractice = PauseIntegration::GetPracticeControllerPtr();
    void* resolvedPractice = cachedPractice;
    if (!cachedPractice) {
        resolvedPractice = PauseIntegration::ResolvePracticeControllerPtrNow("CustomSavestate::TickDeferredPracticeSideRestore");
    } else {
        resolvedPractice = PauseIntegration::ResolvePracticeControllerPtrNow("CustomSavestate::TickDeferredPracticeSideRestore verify");
    }

    if (resolvedPractice && resolvedPractice != cachedPractice) {
        std::ostringstream oss;
        oss << "localSide=" << localSide
            << " cachedPractice=" << PointerHex(reinterpret_cast<uintptr_t>(cachedPractice))
            << " resolvedPractice=" << PointerHex(reinterpret_cast<uintptr_t>(resolvedPractice))
            << " action=refresh-cache";
        LogSavestateTrace("restore practice reconcile pointer refresh", oss.str());
        PauseIntegration::NotePracticeControllerCandidate(resolvedPractice,
                                                          "CustomSavestate::TickDeferredPracticeSideRestore");
    }

    void* practice = resolvedPractice ? resolvedPractice : cachedPractice;

    if (!practice) {
        s_deferredPracticeSideRestoreRetries.store(retries - 1, std::memory_order_release);
        if (retries == kDeferredPracticeSideRestoreRetryFrames || retries == 1 || (retries % 8) == 0) {
            std::ostringstream oss;
            oss << "localSide=" << localSide
                << " waitingForPractice=1 retriesRemaining=" << (retries - 1);
            LogSavestateTrace("restore practice reconcile retry", oss.str());
        }
        return;
    }

    const int currentPracticeSide = SwitchPlayers::GetLocalSide();
    if (currentPracticeSide == localSide) {
        std::ostringstream oss;
        oss << "localSide=" << localSide
            << " currentPracticeSide=" << currentPracticeSide
            << " cachedPractice=" << PointerHex(reinterpret_cast<uintptr_t>(cachedPractice))
            << " resolvedPractice=" << PointerHex(reinterpret_cast<uintptr_t>(resolvedPractice))
            << " chosenPractice=" << PointerHex(reinterpret_cast<uintptr_t>(practice))
            << " skippedRemap=1";
        LogSavestateTrace("restore practice reconcile already satisfied", oss.str());
        ClearDeferredPracticeSideRestoreState();
        return;
    }
    {
        std::ostringstream oss;
        oss << "localSide=" << localSide
            << " currentPracticeSide=" << currentPracticeSide
            << " cachedPractice=" << PointerHex(reinterpret_cast<uintptr_t>(cachedPractice))
            << " resolvedPractice=" << PointerHex(reinterpret_cast<uintptr_t>(resolvedPractice))
            << " chosenPractice=" << PointerHex(reinterpret_cast<uintptr_t>(practice));
        LogSavestateTrace("restore practice reconcile apply", oss.str());
    }
    if (SwitchPlayers::ReapplyLocalSide(localSide)) {
        std::ostringstream oss;
        oss << "localSide=" << localSide
            << " previousSide=" << currentPracticeSide
            << " practice=" << PointerHex(reinterpret_cast<uintptr_t>(practice));
        LogSavestateTrace("restore practice reconcile success", oss.str());
        ClearDeferredPracticeSideRestoreState();
        return;
    }

    s_deferredPracticeSideRestoreRetries.store(retries - 1, std::memory_order_release);
    if (retries == kDeferredPracticeSideRestoreRetryFrames || retries == 1 || (retries % 8) == 0) {
        std::ostringstream oss;
        oss << "localSide=" << localSide
            << " previousSide=" << currentPracticeSide
            << " practice=" << PointerHex(reinterpret_cast<uintptr_t>(practice))
            << " retriesRemaining=" << (retries - 1);
        LogSavestateTrace("restore practice reconcile retry", oss.str());
    }

    if (retries == 1) {
        ClearDeferredPracticeSideRestoreState();
    }
}

bool QueueWorkingSnapshotRestoreAfterHotswap() {
    Snapshot pendingSnapshot;
    std::lock_guard<std::mutex> lock(s_snapshotMutex);
    if (!s_workingSnapshot.valid) {
        return false;
    }

    s_hotswapRestoreApplied.store(false, std::memory_order_release);
    s_pendingHotswapRestoreSnapshot = s_workingSnapshot;
    pendingSnapshot = s_pendingHotswapRestoreSnapshot;
    SetLastStatus("[SAVESTATE] queued working snapshot restore after hotswap");
    LogSavestateTrace("hotswap queue", "queued pending restore | " + DescribeSnapshot(pendingSnapshot));
    return true;
}

bool ConsumeHotswapRestoreApplied() {
    return s_hotswapRestoreApplied.exchange(false, std::memory_order_acq_rel);
}

bool SaveSelectedSlot() {
    Snapshot snapshot;
    std::string reason;
    if (!CaptureSnapshot(snapshot, reason)) {
        LogSnapshotFailure("slot save", reason);
        ShowSnapshotMessage("State Save Failed", RGB(255, 120, 120));
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        AssignWorkingSnapshotLocked(snapshot);
    }

    if (!SaveWorkingSnapshotToDisk(GetActiveDiskSlot())) {
        return false;
    }

    s_saveCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool LoadSelectedSlot() {
    const int slot = GetActiveDiskSlot();
    std::string reason;
    if (!LoadSnapshotFromSlotIntoWorking(slot, reason, false)) {
        LogSnapshotFailure("slot load", reason);
        if (reason == "initial practice snapshot is empty") {
            ShowSnapshotMessage("Initial Slot Empty", RGB(255, 180, 120));
        } else if (reason == "slot file does not exist") {
            ShowSnapshotMessage("Slot File Missing", RGB(255, 180, 120));
        } else if (reason == "file header is invalid or unsupported") {
            ShowSnapshotMessage("Unsupported Snapshot", RGB(255, 120, 120));
        } else {
            ShowSnapshotMessage("State Load Failed", RGB(255, 120, 120));
        }
        return false;
    }

    if (!QueueDeferredRestoreRequest(DeferredRestoreKind::SelectedSlot,
                                     slot,
                                     "LoadSelectedSlot",
                                     reason)) {
        LogSnapshotFailure("slot restore queue", reason);
        ShowSnapshotMessage("State Load Failed", RGB(255, 120, 120));
        return false;
    }

    if (slot == kInitialSnapshotSlot) {
        ShowSnapshotMessage("Initial State Load Queued", RGB(180, 220, 255));
    } else {
        ShowSnapshotMessage("State Load Queued", RGB(180, 220, 255));
    }
    return true;
}

bool SaveWorkingSnapshotToDisk(int slot) {
    int targetSlot = slot;
    if (targetSlot == kInitialSnapshotSlot) {
        targetSlot = 1;
        s_activeDiskSlot.store(targetSlot, std::memory_order_relaxed);
        LogSavestateTrace("disk save redirect",
                          "requested slot 0 redirected to disk slot 1 for write safety");
    }

    if (!ValidDiskSlot(targetSlot)) {
        LogSnapshotFailure("disk save", "invalid slot");
        return false;
    }

    if (!EnsureSavestateDirectoryExists()) {
        LogSnapshotFailure("disk save", "failed to create savestates directory");
        ShowSnapshotMessage("Disk Save Failed", RGB(255, 120, 120));
        return false;
    }

    Snapshot snapshot;
    const unsigned int snapshotStampBeforeWrite = s_workingSnapshotStamp.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        snapshot = s_workingSnapshot;
    }

    if (!snapshot.valid) {
        LogSnapshotFailure("disk save", "working snapshot is empty");
        ShowSnapshotMessage("No Working Snapshot", RGB(255, 180, 120));
        return false;
    }

    SnapshotFileHeader header{};
    header.magicA = kFileMagicA;
    header.magicB = kFileMagicB;
    header.version = kFileVersion4;
    header.savedFKeyLatch = snapshot.savedFKeyLatch;
    header.p1CharId = snapshot.p1CharId;
    header.p2CharId = snapshot.p2CharId;
    header.fpuStateSize = static_cast<uint32_t>(kFpuStateSize);
    header.battleContextSize = static_cast<uint32_t>(snapshot.battleContext.size());
    header.cameraSubfieldSize = static_cast<uint32_t>(snapshot.cameraSubfield.size());
    header.gameStateSize = static_cast<uint32_t>(snapshot.gameState.size());
    header.p1StateSize = static_cast<uint32_t>(snapshot.p1State.size());
    header.p2StateSize = static_cast<uint32_t>(snapshot.p2State.size());
    header.renderBitmapSize = static_cast<uint32_t>(snapshot.renderBitmap.size());
    header.modStateSize = static_cast<uint32_t>(sizeof(snapshot.modState));

    const SlotFileDeletionSummary deletionSummary = DeleteExistingSlotFiles(targetSlot);

    const std::string path = BuildSlotPathForWrite(targetSlot, snapshot);
    LogSavestateTrace("disk save begin",
                      "slot=" + std::to_string(targetSlot)
                          + " path=" + path
                          + " snapshotStamp=" + std::to_string(snapshotStampBeforeWrite)
                          + " expectedFileBytes=" + std::to_string(PersistedSnapshotSizeBytes(snapshot))
                          + " | " + DescribeSnapshot(snapshot));
    LogSavestateTrace("disk save cleanup",
                      "slot=" + std::to_string(targetSlot)
                          + " deleted=" + std::to_string(deletionSummary.deleted)
                          + " failed=" + std::to_string(deletionSummary.failed));
    LogSavestateTrace("disk save header", DescribeFileHeader(header));
    LogSavestateTrace("disk save payload", DescribeSnapshotPayload(snapshot));

    FILE* file = nullptr;
    const errno_t openError = fopen_s(&file, path.c_str(), "wb");
    if (openError != 0 || !file) {
        LogSavestateTrace("disk save open",
                          "slot=" + std::to_string(targetSlot)
                              + " path=" + path
                              + " errno=" + std::to_string(static_cast<int>(openError)));
        LogSnapshotFailure("disk save", "failed to open output file");
        ShowSnapshotMessage("Disk Save Failed", RGB(255, 120, 120));
        return false;
    }

    LogSavestateTrace("disk save open",
                      "slot=" + std::to_string(targetSlot)
                          + " path=" + path
                          + " errno=" + std::to_string(static_cast<int>(openError)));

    const uint32_t savedStageId = snapshot.stageId;
    const uint32_t savedBgmTrack = snapshot.bgmTrack;
    const uint32_t savedSoundStateBytes = static_cast<uint32_t>(SoundStateSizeBytes(snapshot.soundState));

    {
        std::ostringstream oss;
        oss << "savedRevivalVersion=" << snapshot.revivalVersion
            << " savedStageId=" << savedStageId
            << " savedBgmTrack=" << savedBgmTrack
            << " soundStateBytes=" << savedSoundStateBytes
            << " expectedFileBytes=" << PersistedSnapshotSizeBytes(snapshot);
        LogSavestateTrace("disk save metadata", oss.str());
    }

    const bool ok = WriteFileBytes(file, &header, sizeof(header))
        && WriteFileBytes(file, &snapshot.revivalVersion, sizeof(snapshot.revivalVersion))
        && WriteFileBytes(file, &savedStageId, sizeof(savedStageId))
        && WriteFileBytes(file, &savedBgmTrack, sizeof(savedBgmTrack))
        && WriteFileBytes(file, &savedSoundStateBytes, sizeof(savedSoundStateBytes))
        && WriteFileBytes(file, snapshot.fpuState, sizeof(snapshot.fpuState))
        && WriteFileBytes(file, &snapshot.modState, sizeof(snapshot.modState))
        && WriteFileBytes(file,
                          snapshot.soundState.empty() ? nullptr : snapshot.soundState.data(),
                          savedSoundStateBytes)
        && WriteFileBytes(file, snapshot.battleContext.data(), snapshot.battleContext.size())
        && WriteFileBytes(file, snapshot.cameraSubfield.data(), snapshot.cameraSubfield.size())
        && WriteFileBytes(file, snapshot.gameState.data(), snapshot.gameState.size())
        && WriteFileBytes(file, snapshot.p1State.data(), snapshot.p1State.size())
        && WriteFileBytes(file, snapshot.p2State.data(), snapshot.p2State.size())
        && WriteFileBytes(file, snapshot.renderBitmap.data(), snapshot.renderBitmap.size());
    const long finalOffset = std::ftell(file);
    const int writeError = std::ferror(file);
    std::fclose(file);

    {
        std::ostringstream oss;
        oss << "slot=" << targetSlot
            << " path=" << path
            << " ok=" << (ok ? 1 : 0)
            << " finalOffset=" << finalOffset
            << " expectedFileBytes=" << PersistedSnapshotSizeBytes(snapshot)
            << " ferror=" << writeError;
        LogSavestateTrace("disk save result", oss.str());
    }

    if (!ok) {
        LogSnapshotFailure("disk save", "failed while writing file contents");
        ShowSnapshotMessage("Disk Save Failed", RGB(255, 120, 120));
        return false;
    }

    const unsigned int snapshotStampAfterWrite = s_workingSnapshotStamp.load(std::memory_order_relaxed);
    if (snapshotStampAfterWrite != snapshotStampBeforeWrite) {
        LogSavestateTrace("disk save race",
                          "working snapshot changed during write savedStamp="
                              + std::to_string(snapshotStampBeforeWrite)
                              + " currentStamp=" + std::to_string(snapshotStampAfterWrite)
                              + " dirty flag clear may apply to a newer snapshot");
    }

    {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        s_workingSnapshot.dirty = false;
    }

    LogSnapshotStatus("disk save", "working snapshot saved to disk slot " + std::to_string(targetSlot));
    ShowSnapshotMessage("State Saved To Disk", RGB(180, 255, 180));
    return true;
}

bool LoadWorkingSnapshotFromDisk(int slot) {
    std::string reason;
    return LoadSnapshotFromSlotIntoWorking(slot, reason, true);
}

void ClearWorkingSnapshot() {
    std::lock_guard<std::mutex> lock(s_snapshotMutex);
    ClearWorkingSnapshotLocked();
    ClearDeferredPracticeSideRestoreState();
    SetLastStatus("[SAVESTATE] working snapshot cleared");
}

bool HasWorkingSnapshot() {
    std::lock_guard<std::mutex> lock(s_snapshotMutex);
    return s_workingSnapshot.valid;
}

struct SummaryCompatibilityEvaluation {
    bool pairCompatible = false;
    bool stageCompatible = false;
    bool versionCompatible = false;
};

bool SehEvaluateSummaryCompatibility(const Snapshot& snapshot,
                                     SummaryCompatibilityEvaluation& outEvaluation,
                                     SehFailure& outFailure) {
    outEvaluation = SummaryCompatibilityEvaluation{};
    outFailure = SehFailure{};
    __try {
        outEvaluation.pairCompatible = CurrentPairMatchesSnapshot(snapshot);
        outEvaluation.stageCompatible = CurrentStageMatchesSnapshot(snapshot);
        outEvaluation.versionCompatible = CurrentVersionMatchesSnapshot(snapshot);
        return true;
    } __except (CaptureSehFailure(&outFailure, GetExceptionInformation())) {
        return false;
    }
}

bool GetSummary(Summary& outSummary) {
    outSummary = Summary{};
    outSummary.installed = IsInstalled();
    outSummary.saveCount = GetSaveCount();
    outSummary.loadCount = GetLoadCount();
    outSummary.workingSnapshotStamp = s_workingSnapshotStamp.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(s_snapshotMutex);
    outSummary.hasWorkingSnapshot = s_workingSnapshot.valid;
    outSummary.workingSnapshotDirty = s_workingSnapshot.dirty;
    outSummary.savedP1CharId = s_workingSnapshot.p1CharId;
    outSummary.savedP2CharId = s_workingSnapshot.p2CharId;
    outSummary.savedStageId = s_workingSnapshot.stageId;
    outSummary.savedP1CpuFlag = s_workingSnapshot.modState.p1CpuFlag;
    outSummary.savedP2CpuFlag = s_workingSnapshot.modState.p2CpuFlag;
    outSummary.savedLocalSide = s_workingSnapshot.modState.localSide;
    outSummary.savedBgmTrack = s_workingSnapshot.bgmTrack;
    outSummary.savedRevivalVersion = s_workingSnapshot.revivalVersion;
    outSummary.savedP1StateSize = static_cast<unsigned int>(s_workingSnapshot.p1State.size());
    outSummary.savedP2StateSize = static_cast<unsigned int>(s_workingSnapshot.p2State.size());
    outSummary.savedBattleContextSize = static_cast<unsigned int>(s_workingSnapshot.battleContext.size());
    outSummary.savedGameStateSize = static_cast<unsigned int>(s_workingSnapshot.gameState.size());
    outSummary.savedRenderBitmapSize = static_cast<unsigned int>(s_workingSnapshot.renderBitmap.size());

    SummaryCompatibilityEvaluation compatibility{};
    SehFailure compatibilityFailure{};
    if (!SehEvaluateSummaryCompatibility(s_workingSnapshot, compatibility, compatibilityFailure)) {
        std::ostringstream detail;
        detail << "stamp=" << outSummary.workingSnapshotStamp
               << " working=" << (outSummary.hasWorkingSnapshot ? 1 : 0)
               << " dirty=" << (outSummary.workingSnapshotDirty ? 1 : 0)
               << " rawChars=" << static_cast<unsigned int>(outSummary.savedP1CharId)
               << "/" << static_cast<unsigned int>(outSummary.savedP2CharId)
               << " stage=" << static_cast<unsigned int>(outSummary.savedStageId)
               << " sizes=" << outSummary.savedP1StateSize
               << "/" << outSummary.savedP2StateSize
               << " bc=" << outSummary.savedBattleContextSize
               << " gs=" << outSummary.savedGameStateSize
               << " rb=" << outSummary.savedRenderBitmapSize;
        LogSavestateSehFailure("summary compatibility", compatibilityFailure, detail.str());
        outSummary.currentPairCompatible = false;
        outSummary.currentStageCompatible = false;
        outSummary.currentVersionCompatible = false;
        outSummary.currentRestoreAllowed = false;
        return true;
    }

    outSummary.currentPairCompatible = compatibility.pairCompatible;
    outSummary.currentStageCompatible = compatibility.stageCompatible;
    outSummary.currentVersionCompatible = compatibility.versionCompatible;
    outSummary.currentRestoreAllowed = outSummary.currentPairCompatible
        && outSummary.currentStageCompatible
        && outSummary.currentVersionCompatible;
    return true;
}

bool GetWorkingEditableFields(EditableFields& outFields) {
    std::lock_guard<std::mutex> lock(s_snapshotMutex);
    return ExtractEditableFields(s_workingSnapshot, outFields);
}

bool SetWorkingEditableFields(const EditableFields& fields) {
    std::lock_guard<std::mutex> lock(s_snapshotMutex);
    return ApplyEditableFields(s_workingSnapshot, fields);
}

int GetActiveDiskSlot() {
    return ClampDiskSlot(s_activeDiskSlot.load(std::memory_order_relaxed));
}

void SetActiveDiskSlot(int slot) {
    s_activeDiskSlot.store(ClampDiskSlot(slot), std::memory_order_relaxed);
}

int CycleActiveDiskSlot(int delta) {
    const int nextSlot = WrapDiskSlot(GetActiveDiskSlot() + delta);
    s_activeDiskSlot.store(nextSlot, std::memory_order_relaxed);
    return nextSlot;
}

bool DoesDiskSlotExist(int slot) {
    if (slot == kInitialSnapshotSlot) {
        std::lock_guard<std::mutex> lock(s_snapshotMutex);
        return s_initialSnapshot.valid;
    }

    if (!ValidDiskSlot(slot)) {
        return false;
    }
    const DWORD attrs = GetFileAttributesA(BuildSlotPath(slot).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string GetDiskSlotPath(int slot) {
    return BuildSlotPath(slot);
}

std::string GetLastStatus() {
    std::lock_guard<std::mutex> lock(s_statusMutex);
    return s_lastStatus;
}

}
