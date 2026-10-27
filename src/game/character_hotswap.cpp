#include "../include/game/character_hotswap.h"
#include "../include/game/character_hotswap_transition.h"
#include "game/character_hotswap_frontend.h"
#include "runtime/practice_runtime.h"

#include "../include/core/constants.h"
#include "../include/core/logger.h"
#include "../include/core/memory.h"
#include "../include/game/auto_action.h"
#include "../include/game/collision_hook.h"
#include "../include/game/combo_overlay.h"
#include "../include/game/character_settings.h"
#include "../include/game/frame_monitor.h"
#include "../include/game/game_state.h"
#include "../include/game/game_thread_queue.h"
#include "../include/utils/bgm_control.h"
#include "../include/utils/minhook_utils.h"
#include "../include/utils/network.h"
#include "../include/utils/pause_integration.h"
#include "../include/utils/utilities.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <sstream>
#include <string>

namespace CharacterHotswap {
namespace {
LoadingRequestOwner s_loadingRequest;
void PublishLoadingRequest() {
    EfzTmIdentityV1 identity{};
    (void)Practice::CaptureCurrentPracticeIdentity(identity);
    (void)s_loadingRequest.Publish(identity);
}

constexpr uintptr_t RVA_SCREEN_TABLE = 0x00390110;
constexpr uintptr_t RVA_INITIALIZE_SELECTED_CHARACTER = 0x003586B0;
constexpr uintptr_t RVA_UPDATE_LOADING_SCREEN = 0x0036C790;
constexpr uintptr_t RVA_CREATE_CHARACTER_FOR_PLAYER = 0x0036DA20;
constexpr uintptr_t RVA_CLEANUP_PLAYER_OBJECT = 0x00001920;

constexpr uint8_t SCREEN_TITLE = 0;
constexpr uint8_t SCREEN_CHARACTER_SELECT = 1;
constexpr uint8_t SCREEN_LOADING = 2;
constexpr uint8_t SCREEN_BATTLE = 3;

constexpr uintptr_t CS_SLOT_P1_PTR_OFFSET = 20;
constexpr uintptr_t CS_SLOT_P2_PTR_OFFSET = 24;
constexpr uintptr_t CS_LIFECYCLE_OFFSET = 44;
constexpr uintptr_t CS_P1_STATE_OFFSET = 1182;
constexpr uintptr_t CS_P2_STATE_OFFSET = 1183;
constexpr uintptr_t CS_SELECTED_P1_CHAR_OFFSET = 1340;
constexpr uintptr_t CS_SELECTED_P2_CHAR_OFFSET = 1341;
constexpr uintptr_t CS_SELECTED_P1_COLOR_OFFSET = 1342;
constexpr uintptr_t CS_SELECTED_P2_COLOR_OFFSET = 1343;
constexpr uintptr_t CS_STAGE_AUTOCONFIRM_TIMER_OFFSET = 1344;

constexpr uintptr_t GAME_SYSTEM_STAGE_OFFSET = 3890;
constexpr uintptr_t GAME_SYSTEM_SPECIAL_STAGE_OFFSET = 4928;
constexpr uintptr_t GAME_SYSTEM_P1_CUSTOM_PALETTE_FLAG_OFFSET = 4920;
constexpr uintptr_t GAME_SYSTEM_P2_CUSTOM_PALETTE_FLAG_OFFSET = 4924;
constexpr uintptr_t GAME_SYSTEM_ACTIVE_PLAYER_OFFSET = 4930;
constexpr uintptr_t GAME_SYSTEM_P1_CPU_OFFSET = 4931;
constexpr uintptr_t GAME_SYSTEM_P2_CPU_OFFSET = 4932;
constexpr uintptr_t GAME_SYSTEM_ROUND_COUNT_OFFSET = 4942;
constexpr uintptr_t GAME_SYSTEM_MATCH_INDEX_OFFSET = 4952;
constexpr uintptr_t GAME_SYSTEM_PROGRESSION_MASK_OFFSET = 4956;
constexpr uintptr_t GAME_SYSTEM_PROGRESSION_FLAGS_OFFSET = 4960;
constexpr uintptr_t GAME_SYSTEM_MODE_OFFSET = 4964;
constexpr uintptr_t GAME_SYSTEM_CONTINUE_OFFSET = 4984;
constexpr uintptr_t GAME_SYSTEM_NEXT_STAGE_OFFSET = 4985;
constexpr uintptr_t GAME_SYSTEM_REPLAY_IO_MODE_OFFSET = 82563;

constexpr uintptr_t CHARACTER_OBJECT_SELECT_ID_OFFSET = 141;
constexpr uintptr_t CHARACTER_OBJECT_PALETTE_INDEX_OFFSET = 142;

constexpr int STAGE_COUNT = 23;
constexpr int PALETTE_SLOT_COUNT = 6;
constexpr int PALETTE_INDEX_FIRST = 0;
constexpr int kWaitExitTicks = 900;
constexpr int kWaitLoadingTicks = 1800;
constexpr int kWaitMatchTicks = 1800;

using InitializeSelectedCharacterFn = int(__thiscall*)(int* screenContext,
                                                       int characterSlotPtr,
                                                       int opponentSlotPtr,
                                                       char playerIndex);
using UpdateLoadingScreenFn = char(__thiscall*)(int* loadingContext);
using CreateCharacterForPlayerFn = int(__thiscall*)(int* loadingContext,
                                                    int characterSlotPtr,
                                                    int opponentSlotPtr,
                                                    char playerIndex,
                                                    char characterSelectId);
using CleanupPlayerObjectFn = unsigned short*(__thiscall*)(unsigned short* character,
                                                          char freeMemory);

enum class RequestState : uint8_t {
    Idle = 0,
    PendingExitRequest,
    IssuingExitRequest,
    AwaitingCharacterSelect,
    PendingApply,
    ApplyingSelections,
    AwaitingLoading,
    AwaitingMatch,
    Completing,
    Failed,
};

enum class UiStatus : uint8_t {
    Ready = 0,
    Exiting,
    Applying,
    WaitingLoading,
    WaitingMatch,
    Failed,
    Completed,
};

struct RawApplyResult {
    uintptr_t screenContext = 0;
    uintptr_t gameSystem = 0;
    uintptr_t p1SlotStorage = 0;
    uintptr_t p2SlotStorage = 0;
    uintptr_t p1Character = 0;
    uintptr_t p2Character = 0;
    uint8_t p1ColorReadback = PALETTE_INDEX_FIRST;
    uint8_t p2ColorReadback = PALETTE_INDEX_FIRST;
    DWORD p1CustomFlagReadback = 0;
    DWORD p2CustomFlagReadback = 0;
    DWORD sehCode = 0;
};

struct RawDirectBootstrapResult {
    uintptr_t loadingContext = 0;
    uintptr_t gameSystem = 0;
    uintptr_t p1SlotStorage = 0;
    uintptr_t p2SlotStorage = 0;
    uintptr_t p1Character = 0;
    uintptr_t p2Character = 0;
    uint8_t p1SelectIdReadback = 0xFF;
    uint8_t p2SelectIdReadback = 0xFF;
    uint8_t p1ColorReadback = PALETTE_INDEX_FIRST;
    uint8_t p2ColorReadback = PALETTE_INDEX_FIRST;
    DWORD p1CpuReadback = 0;
    DWORD p2CpuReadback = 0;
    DWORD sehCode = 0;
    int failureStep = 0;
    bool p1Created = false;
    bool p2Created = false;
};

struct CustomPaletteLookupResult {
    bool valid = false;
    bool rootExists = false;
    bool systemExists = false;
    bool exists = false;
    std::string resourceName;
    std::string rootPath;
    std::string systemPath;
    std::string resolvedPath;
};

std::atomic<RequestState> s_state{RequestState::Idle};
std::atomic<uintptr_t> s_queuedExitBattle{0},s_queuedExitGameSystem{0};
std::atomic<UiStatus> s_uiStatus{UiStatus::Ready};
std::atomic<int> s_requestedP1Char{CHAR_ID_AKANE};
std::atomic<int> s_requestedP2Char{CHAR_ID_AKIKO};
std::atomic<int> s_requestedP1Color{PALETTE_INDEX_FIRST};
std::atomic<int> s_requestedP2Color{PALETTE_INDEX_FIRST};
std::atomic<int> s_requestedP1CustomPalette{0};
std::atomic<int> s_requestedP2CustomPalette{0};
std::atomic<int> s_requestedStage{0};
std::atomic<int> s_requestedBgmTrack{0};
std::atomic<int> s_waitTicks{0};
std::atomic<bool> s_directBootstrapInstalled{false};
std::atomic<bool> s_directBootstrapCleanupIncomplete{false};
using DirectBootstrapState = CharacterHotswap::Transition::DirectBootstrapState;
// One atomic owns the loader's whole lifecycle so the monitor and game-thread hook never observe mixed request generations.
std::atomic<DirectBootstrapState> s_directBootstrapState{DirectBootstrapState::Idle};
// Cancellation after the loader has entered is deferred until fighter/stage ownership finishes transferring to Battle.
std::atomic<bool> s_directAbortAfterHandoff{false};
// Selector reloads reuse the Loading hook; their handoff receipt stays separate from the direct bootstrap's.
std::atomic<bool> s_nativeFrontendInstalled{false};
std::atomic<bool> s_awaitNativeCompletion{false};
std::atomic<bool> s_selectorLoadingHandoffComplete{false};
std::atomic<bool> s_completedReceiptAvailable{false};
std::atomic<uint32_t> s_completedReceiptGeneration{0};
std::atomic<int> s_completedP1Char{-1};
std::atomic<int> s_completedP2Char{-1};
std::atomic<int> s_completedP1Color{0};
std::atomic<int> s_completedP2Color{0};
std::atomic<int> s_completedP1Custom{0};
std::atomic<int> s_completedP2Custom{0};
std::atomic<int> s_completedStage{-1};
std::atomic<int> s_completedBgm{-1};
UpdateLoadingScreenFn s_originalUpdateLoadingScreen = nullptr;
uintptr_t s_updateLoadingScreenTarget = 0;
GamePhase s_lastActivePhase = GamePhase::Unknown;
int s_characterSelectReadyTicks = 0;

bool IsValidSelectId(int selectId) {
    return selectId >= 0 && selectId < kCharacterSelectCount;
}

bool IsValidStageId(int stageId) {
    return stageId >= 0 && stageId < STAGE_COUNT;
}

bool IsValidPaletteIndex(int paletteIndex) {
    return paletteIndex >= 0 && paletteIndex < PALETTE_SLOT_COUNT;
}

bool DirectBootstrapOwnsTransaction() {
    return CharacterHotswap::Transition::OwnsDirectLoadingTransaction(
        s_directBootstrapState.load(std::memory_order_acquire));
}

bool DirectBootstrapHasBattleHandoff() {
    return CharacterHotswap::Transition::HasDirectLoadingBattleHandoff(
        s_directBootstrapState.load(std::memory_order_acquire));
}

bool AnyOwnedLoadingHandoffComplete() {
    return DirectBootstrapHasBattleHandoff() ||
        s_selectorLoadingHandoffComplete.load(std::memory_order_acquire);
}

bool CompletionReceiptAuthorized() {
    const DirectBootstrapState state =
        s_directBootstrapState.load(std::memory_order_acquire);
    return state == DirectBootstrapState::Idle ||
        CharacterHotswap::Transition::DirectCompletionReceiptAllowed(
            state, s_directAbortAfterHandoff.load(std::memory_order_acquire));
}

bool PaletteSelectionsEqual(const PaletteSelection& lhs, const PaletteSelection& rhs) {
    return lhs.p1Color == rhs.p1Color
        && lhs.p2Color == rhs.p2Color
        && lhs.p1UseCustomPalette == rhs.p1UseCustomPalette
        && lhs.p2UseCustomPalette == rhs.p2UseCustomPalette;
}

const char* GetCharacterSelectName(int selectId) {
    switch (selectId) {
        case 0:  return "Rumi";
        case 1:  return "Ayu";
        case 2:  return "Mai";
        case 3:  return "Makoto";
        case 4:  return "Akane";
        case 5:  return "Mayu";
        case 6:  return "Mizuka";
        case 7:  return "Misaki";
        case 8:  return "Shiori";
        case 9:  return "Sayuri";
        case 10: return "Neyuki";
        case 11: return "Mio";
        case 12: return "Doppel";
        case 13: return "Kaori";
        case 14: return "Ikumi";
        case 15: return "Mishio";
        case 16: return "Akiko";
        case 17: return "Nayuki";
        case 18: return "Unknown";
        case 19: return "Kanna";
        case 20: return "Kano";
        case 21: return "Minagi";
        case 22: return "Neyuki (Alt)";
        case 23: return "Misuzu";
        default: return "Invalid";
    }
}

const char* GetCharacterResourceName(int selectId) {
    switch (selectId) {
        case 0:  return "nanase";
        case 1:  return "ayu";
        case 2:  return "mai";
        case 3:  return "makoto";
        case 4:  return "akane";
        case 5:  return "mayu";
        case 6:  return "nagamori";
        case 7:  return "misaki";
        case 8:  return "shiori";
        case 9:  return "sayuri";
        case 10: return "nayuki";
        case 11: return "mio";
        case 12: return "exnanase";
        case 13: return "kaori";
        case 14: return "ikumi";
        case 15: return "mishio";
        case 16: return "akiko";
        case 17: return "nayukib";
        case 18: return "mizukab";
        case 19: return "kanna";
        case 20: return "kano";
        case 21: return "minagi";
        case 22: return "nayuki";
        case 23: return "misuzu";
        default: return nullptr;
    }
}

bool IsActiveState(RequestState state) {
    return state != RequestState::Idle && state != RequestState::Failed;
}

const char* StatusText(UiStatus status) {
    switch (status) {
        case UiStatus::Ready:          return "READY";
        case UiStatus::Exiting:        return "EXITING";
        case UiStatus::Applying:       return "APPLYING";
        case UiStatus::WaitingLoading: return "LOADING";
        case UiStatus::WaitingMatch:   return "WAIT MATCH";
        case UiStatus::Failed:         return "FAILED";
        case UiStatus::Completed:      return "DONE";
        default:                       return "READY";
    }
}

const char* RequestStateName(RequestState state) {
    switch (state) {
        case RequestState::Idle:                    return "Idle";
        case RequestState::PendingExitRequest:      return "PendingExitRequest";
        case RequestState::IssuingExitRequest:      return "IssuingExitRequest";
        case RequestState::AwaitingCharacterSelect: return "AwaitingCharacterSelect";
        case RequestState::PendingApply:            return "PendingApply";
        case RequestState::ApplyingSelections:      return "ApplyingSelections";
        case RequestState::AwaitingLoading:         return "AwaitingLoading";
        case RequestState::AwaitingMatch:           return "AwaitingMatch";
        case RequestState::Failed:                  return "Failed";
        default:                                    return "Unknown";
    }
}

const char* PhaseName(GamePhase phase) {
    switch (phase) {
        case GamePhase::Menu:            return "Menu";
        case GamePhase::CharacterSelect: return "CharacterSelect";
        case GamePhase::Loading:         return "Loading";
        case GamePhase::Match:           return "Match";
        default:                         return "Unknown";
    }
}

std::string Hex(uintptr_t value) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << value;
    return oss.str();
}

void AppendRequestedReloadSummary(std::ostringstream& oss) {
    const int p1CharId = s_requestedP1Char.load(std::memory_order_relaxed);
    const int p2CharId = s_requestedP2Char.load(std::memory_order_relaxed);
    const int p1Color = s_requestedP1Color.load(std::memory_order_relaxed);
    const int p2Color = s_requestedP2Color.load(std::memory_order_relaxed);
    const int p1Custom = s_requestedP1CustomPalette.load(std::memory_order_relaxed);
    const int p2Custom = s_requestedP2CustomPalette.load(std::memory_order_relaxed);
    const int stageId = s_requestedStage.load(std::memory_order_relaxed);
    const int bgmTrack = s_requestedBgmTrack.load(std::memory_order_relaxed);
    oss << " request="
        << GetCharacterSelectName(p1CharId) << "(" << p1CharId << ")/"
        << GetCharacterSelectName(p2CharId) << "(" << p2CharId << ")"
        << " stage=" << stageId
        << " bgm=" << bgmTrack
        << " palette=" << (p1Color + 1) << "/" << (p2Color + 1)
        << " custom=" << p1Custom << "/" << p2Custom;
}

std::string BuildGameRelativePath(const std::string& relativePath) {
    char exePath[MAX_PATH] = {0};
    const DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return relativePath;
    }
    std::string absolutePath(exePath, len);
    const size_t slash = absolutePath.find_last_of("\\/");
    if (slash == std::string::npos) {
        return relativePath;
    }

    absolutePath.resize(slash + 1);
    absolutePath += relativePath;
    return absolutePath;
}

bool FileExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

CustomPaletteLookupResult ProbeCharacterCustomPalette(int selectId, int paletteIndex) {
    CustomPaletteLookupResult result{};

    const char* resourceName = GetCharacterResourceName(selectId);
    if (!resourceName || !IsValidPaletteIndex(paletteIndex)) {
        return result;
    }

    result.valid = true;
    result.resourceName = resourceName;

    const std::string fileName = result.resourceName + std::to_string(paletteIndex + 1) + ".pal";
    result.rootPath = BuildGameRelativePath(result.resourceName + "\\" + fileName);
    result.systemPath = BuildGameRelativePath(std::string("system\\") + result.resourceName + "\\" + fileName);

    result.rootExists = FileExists(result.rootPath);
    result.systemExists = FileExists(result.systemPath);
    result.exists = result.rootExists || result.systemExists;

    if (result.rootExists) {
        result.resolvedPath = result.rootPath;
    } else if (result.systemExists) {
        result.resolvedPath = result.systemPath;
    } else {
        result.resolvedPath = result.rootPath;
    }

    return result;
}

struct PaletteProbeCacheEntry {
    bool populated = false;
    CustomPaletteLookupResult result;
};

constexpr int kPaletteProbeCacheChars = 24;
PaletteProbeCacheEntry g_paletteProbeCache[kPaletteProbeCacheChars][PALETTE_SLOT_COUNT];
CRITICAL_SECTION g_paletteProbeCacheCs;
std::atomic<bool> g_paletteProbeCacheCsInit{false};
std::atomic<unsigned long> g_paletteProbeCacheHits{0};
std::atomic<unsigned long> g_paletteProbeCacheMisses{0};

void EnsurePaletteProbeCacheCs() {
    bool expected = false;
    if (g_paletteProbeCacheCsInit.compare_exchange_strong(expected, true)) {
        InitializeCriticalSection(&g_paletteProbeCacheCs);
    }
}

bool TryGetCachedPaletteProbe(int selectId, int paletteIndex, CustomPaletteLookupResult& out) {
    if (selectId < 0 || selectId >= kPaletteProbeCacheChars || !IsValidPaletteIndex(paletteIndex)) {
        return false;
    }
    EnsurePaletteProbeCacheCs();
    EnterCriticalSection(&g_paletteProbeCacheCs);
    const bool hit = g_paletteProbeCache[selectId][paletteIndex].populated;
    if (hit) {
        out = g_paletteProbeCache[selectId][paletteIndex].result;
    }
    LeaveCriticalSection(&g_paletteProbeCacheCs);
    return hit;
}

void StorePaletteProbeInCache(int selectId, int paletteIndex, const CustomPaletteLookupResult& result) {
    if (selectId < 0 || selectId >= kPaletteProbeCacheChars || !IsValidPaletteIndex(paletteIndex)) {
        return;
    }
    EnsurePaletteProbeCacheCs();
    EnterCriticalSection(&g_paletteProbeCacheCs);
    g_paletteProbeCache[selectId][paletteIndex].populated = true;
    g_paletteProbeCache[selectId][paletteIndex].result = result;
    LeaveCriticalSection(&g_paletteProbeCacheCs);
}

CustomPaletteLookupResult LookupCharacterCustomPalette(int selectId, int paletteIndex) {
    CustomPaletteLookupResult cached{};
    if (TryGetCachedPaletteProbe(selectId, paletteIndex, cached)) {
        g_paletteProbeCacheHits.fetch_add(1, std::memory_order_relaxed);
        return cached;
    }
    g_paletteProbeCacheMisses.fetch_add(1, std::memory_order_relaxed);
    CustomPaletteLookupResult fresh = ProbeCharacterCustomPalette(selectId, paletteIndex);
    StorePaletteProbeInCache(selectId, paletteIndex, fresh);
    return fresh;
}

void InvalidatePaletteProbeCacheImpl() {
    EnsurePaletteProbeCacheCs();
    EnterCriticalSection(&g_paletteProbeCacheCs);
    for (int charIdx = 0; charIdx < kPaletteProbeCacheChars; ++charIdx) {
        for (int slotIdx = 0; slotIdx < PALETTE_SLOT_COUNT; ++slotIdx) {
            g_paletteProbeCache[charIdx][slotIdx].populated = false;
            g_paletteProbeCache[charIdx][slotIdx].result = CustomPaletteLookupResult{};
        }
    }
    LeaveCriticalSection(&g_paletteProbeCacheCs);
    const unsigned long hits = g_paletteProbeCacheHits.exchange(0, std::memory_order_relaxed);
    const unsigned long misses = g_paletteProbeCacheMisses.exchange(0, std::memory_order_relaxed);
    char buf[160] = {0};
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "[HOTSWAP][PAL][CACHE] invalidated (prior hits=%lu misses=%lu)",
                hits, misses);
    LogOut(buf, true);
}

void AppendCustomPaletteLookupDetails(std::ostringstream& oss, const CustomPaletteLookupResult& result) {
    oss << " resource=" << (result.resourceName.empty() ? "<invalid>" : result.resourceName)
        << " root=" << (result.rootPath.empty() ? "<none>" : result.rootPath)
        << " rootExists=" << (result.rootExists ? 1 : 0)
        << " system=" << (result.systemPath.empty() ? "<none>" : result.systemPath)
        << " systemExists=" << (result.systemExists ? 1 : 0)
        << " chosen=" << (result.exists ? result.resolvedPath : "<none>");
}

void LogCustomPaletteLookupIfChanged(const char* source,
                                     int selectId,
                                     int paletteIndex,
                                     const CustomPaletteLookupResult& result) {
    static bool s_probeStateInitialized = false;
    static int s_probeState[24][PALETTE_SLOT_COUNT];

    if (!s_probeStateInitialized) {
        for (int charIdx = 0; charIdx < 24; ++charIdx) {
            for (int slotIdx = 0; slotIdx < PALETTE_SLOT_COUNT; ++slotIdx) {
                s_probeState[charIdx][slotIdx] = -99;
            }
        }
        s_probeStateInitialized = true;
    }

    const int stateCode = result.valid
        ? ((result.rootExists ? 1 : 0) | (result.systemExists ? 2 : 0))
        : -1;
    if (selectId >= 0 && selectId < 24 && IsValidPaletteIndex(paletteIndex)) {
        if (s_probeState[selectId][paletteIndex] == stateCode) {
            return;
        }
        s_probeState[selectId][paletteIndex] = stateCode;
    }

    std::ostringstream oss;
    oss << "[HOTSWAP][PAL][FILE][" << (source ? source : "CHECK") << "]"
        << " char=" << GetCharacterSelectName(selectId) << "(" << selectId << ")"
        << " slot=" << (paletteIndex + 1);
    AppendCustomPaletteLookupDetails(oss, result);
    LogOut(oss.str(), true);
}

void LogMatchPaletteReadinessIfChanged(bool charactersInitialized,
                                       uintptr_t gameSystem,
                                       uintptr_t p1Character,
                                       uintptr_t p2Character) {
    static int s_lastCharsInitialized = -1;
    static uintptr_t s_lastGameSystem = 0;
    static uintptr_t s_lastP1Character = 0;
    static uintptr_t s_lastP2Character = 0;

    const int charsInitializedInt = charactersInitialized ? 1 : 0;
    if (charsInitializedInt == s_lastCharsInitialized
        && gameSystem == s_lastGameSystem
        && p1Character == s_lastP1Character
        && p2Character == s_lastP2Character) {
        return;
    }

    std::ostringstream oss;
    oss << "[HOTSWAP][PAL][READ][MATCH_READY]"
        << " charsInit=" << charsInitializedInt
        << " gs=" << Hex(gameSystem)
        << " p1Obj=" << Hex(p1Character)
        << " p2Obj=" << Hex(p2Character);
    LogOut(oss.str(), true);

    s_lastCharsInitialized = charsInitializedInt;
    s_lastGameSystem = gameSystem;
    s_lastP1Character = p1Character;
    s_lastP2Character = p2Character;
}

bool CharacterCustomPaletteExists(int selectId, int paletteIndex, std::string* outPath = nullptr) {
    const CustomPaletteLookupResult lookup = LookupCharacterCustomPalette(selectId, paletteIndex);
    if (!lookup.valid) {
        if (outPath) {
            outPath->clear();
        }
        return false;
    }

    if (outPath) {
        *outPath = lookup.resolvedPath;
    }

    return lookup.exists;
}

void LogPaletteSelectionSnapshot(const char* source,
                                 const PaletteSelection& selection,
                                 uintptr_t contextA,
                                 uintptr_t contextB,
                                 uintptr_t contextC = 0,
                                 uintptr_t contextD = 0) {
    std::ostringstream oss;
    oss << "[HOTSWAP][PAL][READ][" << source << "]"
        << " palette=" << (selection.p1Color + 1) << "/" << (selection.p2Color + 1)
        << " custom=" << (selection.p1UseCustomPalette ? 1 : 0) << "/" << (selection.p2UseCustomPalette ? 1 : 0)
        << " a=" << Hex(contextA)
        << " b=" << Hex(contextB);
    if (contextC) {
        oss << " c=" << Hex(contextC);
    }
    if (contextD) {
        oss << " d=" << Hex(contextD);
    }
    LogOut(oss.str(), true);
}

bool CaptureQueuedBattleExit(bool fromMatch) {
    s_queuedExitBattle.store(0,std::memory_order_relaxed);
    s_queuedExitGameSystem.store(0,std::memory_order_relaxed);
    if(!fromMatch)return true;
    if(!EnsureFrontendControlHooksInstalled())return false;
    const uintptr_t base=GetEFZBase();uintptr_t battle=0,gameSystem=0;uint8_t screen=255;
    if(!base || !SafeReadMemory(base+EFZ_BASE_OFFSET_SCREEN_STATE,&screen,sizeof(screen)) || screen!=SCREEN_BATTLE ||
       !SafeReadMemory(base+RVA_SCREEN_TABLE+4*SCREEN_BATTLE,&battle,sizeof(battle)) || !battle ||
       !SafeReadMemory(battle+0x1c,&gameSystem,sizeof(gameSystem)) || !gameSystem)return false;
    s_queuedExitGameSystem.store(gameSystem,std::memory_order_relaxed);
    s_queuedExitBattle.store(battle,std::memory_order_relaxed);
    return true;
}

void SetState(RequestState nextState, UiStatus nextStatus, const char* reason) {
    const RequestState prevState = s_state.load(std::memory_order_relaxed);
    s_state.store(nextState, std::memory_order_release);
    s_uiStatus.store(nextStatus, std::memory_order_release);
    s_waitTicks.store(0, std::memory_order_relaxed);
    if (nextState == RequestState::PendingApply) {
        s_characterSelectReadyTicks = 0;
    }

    std::ostringstream oss;
    oss << "[HOTSWAP] state " << RequestStateName(prevState)
        << "(" << static_cast<int>(prevState) << ")"
        << " -> " << RequestStateName(nextState)
        << "(" << static_cast<int>(nextState) << ")"
        << " status=" << StatusText(nextStatus);
    if (reason && reason[0] != '\0') {
        oss << " reason=" << reason;
    }
    AppendRequestedReloadSummary(oss);
    LogOut(oss.str(), true);
}

void FailRequest(const char* reason, GamePhase currentPhase) {
    std::ostringstream oss;
    oss << "[HOTSWAP] failed in phase=" << PhaseName(currentPhase)
        << " reason=" << (reason ? reason : "unknown");
    AppendRequestedReloadSummary(oss);
    LogOut(oss.str(), true);
    s_selectorLoadingHandoffComplete.store(false, std::memory_order_release);
    const DirectBootstrapState directState =
        s_directBootstrapState.load(std::memory_order_acquire);
    if (directState == DirectBootstrapState::Entered ||
        directState == DirectBootstrapState::BattleHandoff) {
        s_directAbortAfterHandoff.store(true, std::memory_order_release);
    } else {
        s_directBootstrapState.store(DirectBootstrapState::Idle,
                                     std::memory_order_release);
        s_directAbortAfterHandoff.store(false, std::memory_order_release);
    }
    s_state.store(RequestState::Failed, std::memory_order_release);
    s_uiStatus.store(UiStatus::Failed, std::memory_order_release);
    s_waitTicks.store(0, std::memory_order_relaxed);
}

bool ResolveCharacterSelectContext(uintptr_t& outScreenContext, uintptr_t& outGameSystem) {
    outScreenContext = 0;
    outGameSystem = 0;

    const uintptr_t base = GetEFZBase();
    if (!base) {
        return false;
    }

    const uintptr_t slot = base + RVA_SCREEN_TABLE + 4u * static_cast<uintptr_t>(SCREEN_CHARACTER_SELECT);
    if (!SafeReadMemory(slot, &outScreenContext, sizeof(outScreenContext)) || !outScreenContext) {
        return false;
    }

    if (!SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE, &outGameSystem, sizeof(outGameSystem)) || !outGameSystem) {
        return false;
    }

    return true;
}

void InvalidateCompletedReceipt() {
    s_completedReceiptAvailable.store(false, std::memory_order_release);
    s_completedReceiptGeneration.store(0, std::memory_order_release);
}

void PublishCompletedReceipt() {
    s_completedP1Char.store(s_requestedP1Char.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
    s_completedP2Char.store(s_requestedP2Char.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
    s_completedP1Color.store(s_requestedP1Color.load(std::memory_order_relaxed),
                             std::memory_order_relaxed);
    s_completedP2Color.store(s_requestedP2Color.load(std::memory_order_relaxed),
                             std::memory_order_relaxed);
    s_completedP1Custom.store(s_requestedP1CustomPalette.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
    s_completedP2Custom.store(s_requestedP2CustomPalette.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
    s_completedStage.store(s_requestedStage.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
    s_completedBgm.store(s_requestedBgmTrack.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
    s_completedReceiptGeneration.store(GetRuntimeLifecycleGeneration(),
                                       std::memory_order_relaxed);
    s_completedReceiptAvailable.store(true, std::memory_order_release);
}

bool ResolveLoadingContext(uintptr_t& outLoadingContext,
                           uintptr_t& outGameSystem,
                           uintptr_t& outP1SlotStorage,
                           uintptr_t& outP2SlotStorage,
                           bool requireEmptySlots) {
    outLoadingContext = 0;
    outGameSystem = 0;
    outP1SlotStorage = 0;
    outP2SlotStorage = 0;

    const uintptr_t base = GetEFZBase();
    if (!base) {
        return false;
    }

    const uintptr_t screenSlot = base + RVA_SCREEN_TABLE
        + 4u * static_cast<uintptr_t>(SCREEN_LOADING);
    uintptr_t globalGameSystem = 0;
    uintptr_t graphicsSystem = 0;
    uintptr_t inputSystem = 0;
    uintptr_t soundSystem = 0;
    uintptr_t p1Character = 0;
    uintptr_t p2Character = 0;
    if (!SafeReadMemory(screenSlot, &outLoadingContext, sizeof(outLoadingContext))
        || !outLoadingContext
        || !SafeReadMemory(outLoadingContext + CS_SLOT_P1_PTR_OFFSET,
                           &outP1SlotStorage, sizeof(outP1SlotStorage))
        || !outP1SlotStorage
        || !SafeReadMemory(outLoadingContext + CS_SLOT_P2_PTR_OFFSET,
                           &outP2SlotStorage, sizeof(outP2SlotStorage))
        || !outP2SlotStorage
        || !SafeReadMemory(outLoadingContext + 28, &outGameSystem, sizeof(outGameSystem))
        || !outGameSystem
        || !SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE,
                           &globalGameSystem, sizeof(globalGameSystem))
        || globalGameSystem != outGameSystem
        || !SafeReadMemory(outLoadingContext + 32, &graphicsSystem, sizeof(graphicsSystem))
        || !graphicsSystem
        || !SafeReadMemory(outLoadingContext + 36, &inputSystem, sizeof(inputSystem))
        || !inputSystem
        || !SafeReadMemory(outLoadingContext + 40, &soundSystem, sizeof(soundSystem))
        || !soundSystem
        || !SafeReadMemory(outP1SlotStorage, &p1Character, sizeof(p1Character))
        || !SafeReadMemory(outP2SlotStorage, &p2Character, sizeof(p2Character))) {
        return false;
    }

    // Direct construction may only claim the owner slots after the previous battle's native cleanup emptied them.
    return !requireEmptySlots || (p1Character == 0 && p2Character == 0);
}

bool CharacterSelectReadyForApply() {
    uintptr_t screenContext = 0;
    uintptr_t gameSystem = 0;
    if (!ResolveCharacterSelectContext(screenContext, gameSystem)) {
        return false;
    }

    uint8_t lifecycle = 0xFF;
    uintptr_t p1SlotStorage = 0;
    uintptr_t p2SlotStorage = 0;
    return SafeReadMemory(screenContext + CS_LIFECYCLE_OFFSET, &lifecycle, sizeof(lifecycle))
        && lifecycle == 0
        && SafeReadMemory(screenContext + CS_SLOT_P1_PTR_OFFSET, &p1SlotStorage, sizeof(p1SlotStorage))
        && p1SlotStorage != 0
        && SafeReadMemory(screenContext + CS_SLOT_P2_PTR_OFFSET, &p2SlotStorage, sizeof(p2SlotStorage))
        && p2SlotStorage != 0;
}

bool CaptureCharacterSelectPaletteSelection(PaletteSelection& outSelection) {
    uintptr_t screenContext = 0;
    uintptr_t gameSystem = 0;
    if (!ResolveCharacterSelectContext(screenContext, gameSystem)) {
        return false;
    }

    uint8_t p1Color = PALETTE_INDEX_FIRST;
    uint8_t p2Color = PALETTE_INDEX_FIRST;
    DWORD p1CustomFlag = 0;
    DWORD p2CustomFlag = 0;
    if (!SafeReadMemory(screenContext + CS_SELECTED_P1_COLOR_OFFSET, &p1Color, sizeof(p1Color))) {
        return false;
    }
    if (!SafeReadMemory(screenContext + CS_SELECTED_P2_COLOR_OFFSET, &p2Color, sizeof(p2Color))) {
        return false;
    }
    if (!SafeReadMemory(gameSystem + GAME_SYSTEM_P1_CUSTOM_PALETTE_FLAG_OFFSET, &p1CustomFlag, sizeof(p1CustomFlag))) {
        return false;
    }
    if (!SafeReadMemory(gameSystem + GAME_SYSTEM_P2_CUSTOM_PALETTE_FLAG_OFFSET, &p2CustomFlag, sizeof(p2CustomFlag))) {
        return false;
    }

    outSelection.p1Color = IsValidPaletteIndex(static_cast<int>(p1Color)) ? static_cast<int>(p1Color) : PALETTE_INDEX_FIRST;
    outSelection.p2Color = IsValidPaletteIndex(static_cast<int>(p2Color)) ? static_cast<int>(p2Color) : PALETTE_INDEX_FIRST;
    outSelection.p1UseCustomPalette = p1CustomFlag != 0;
    outSelection.p2UseCustomPalette = p2CustomFlag != 0;

    static bool s_hasSnapshot = false;
    static PaletteSelection s_lastSelection{};
    static uintptr_t s_lastScreenContext = 0;
    static uintptr_t s_lastGameSystem = 0;
    if (!s_hasSnapshot
        || !PaletteSelectionsEqual(outSelection, s_lastSelection)
        || screenContext != s_lastScreenContext
        || gameSystem != s_lastGameSystem) {
        LogPaletteSelectionSnapshot("CS", outSelection, screenContext, gameSystem);
        s_lastSelection = outSelection;
        s_lastScreenContext = screenContext;
        s_lastGameSystem = gameSystem;
        s_hasSnapshot = true;
    }
    return true;
}

bool CaptureMatchPaletteSelection(PaletteSelection& outSelection) {
    const uintptr_t base = GetEFZBase();
    const uintptr_t gameSystem = GetGameStatePtr();
    if (!base || !gameSystem) {
        return false;
    }

    uintptr_t p1Character = 0;
    uintptr_t p2Character = 0;
    uint8_t p1Color = PALETTE_INDEX_FIRST;
    uint8_t p2Color = PALETTE_INDEX_FIRST;
    DWORD p1CustomFlag = 0;
    DWORD p2CustomFlag = 0;
    if (!SafeReadMemory(base + EFZ_BASE_OFFSET_P1, &p1Character, sizeof(p1Character)) || !p1Character) {
        return false;
    }
    if (!SafeReadMemory(base + EFZ_BASE_OFFSET_P2, &p2Character, sizeof(p2Character)) || !p2Character) {
        return false;
    }
    LogMatchPaletteReadinessIfChanged(AreCharactersInitialized(), gameSystem, p1Character, p2Character);
    if (!SafeReadMemory(p1Character + CHARACTER_OBJECT_PALETTE_INDEX_OFFSET, &p1Color, sizeof(p1Color))) {
        return false;
    }
    if (!SafeReadMemory(p2Character + CHARACTER_OBJECT_PALETTE_INDEX_OFFSET, &p2Color, sizeof(p2Color))) {
        return false;
    }
    if (!SafeReadMemory(gameSystem + GAME_SYSTEM_P1_CUSTOM_PALETTE_FLAG_OFFSET, &p1CustomFlag, sizeof(p1CustomFlag))) {
        return false;
    }
    if (!SafeReadMemory(gameSystem + GAME_SYSTEM_P2_CUSTOM_PALETTE_FLAG_OFFSET, &p2CustomFlag, sizeof(p2CustomFlag))) {
        return false;
    }

    outSelection.p1Color = IsValidPaletteIndex(static_cast<int>(p1Color)) ? static_cast<int>(p1Color) : PALETTE_INDEX_FIRST;
    outSelection.p2Color = IsValidPaletteIndex(static_cast<int>(p2Color)) ? static_cast<int>(p2Color) : PALETTE_INDEX_FIRST;
    outSelection.p1UseCustomPalette = p1CustomFlag != 0;
    outSelection.p2UseCustomPalette = p2CustomFlag != 0;

    static bool s_hasSnapshot = false;
    static PaletteSelection s_lastSelection{};
    static uintptr_t s_lastGameSystem = 0;
    static uintptr_t s_lastP1Character = 0;
    static uintptr_t s_lastP2Character = 0;
    if (!s_hasSnapshot
        || !PaletteSelectionsEqual(outSelection, s_lastSelection)
        || gameSystem != s_lastGameSystem
        || p1Character != s_lastP1Character
        || p2Character != s_lastP2Character) {
        LogPaletteSelectionSnapshot("MATCH", outSelection, gameSystem, p1Character, p2Character, base);
        s_lastSelection = outSelection;
        s_lastGameSystem = gameSystem;
        s_lastP1Character = p1Character;
        s_lastP2Character = p2Character;
        s_hasSnapshot = true;
    }
    return true;
}

bool CaptureCurrentPaletteSelection(GamePhase currentPhase, PaletteSelection& outSelection) {
    outSelection = PaletteSelection{};
    if (currentPhase == GamePhase::Match) {
        return CaptureMatchPaletteSelection(outSelection);
    }
    if (currentPhase == GamePhase::CharacterSelect) {
        return CaptureCharacterSelectPaletteSelection(outSelection);
    }
    return false;
}

void ResolveRequestedCustomPalette(int selectId,
                                   const char* playerLabel,
                                   uint8_t colorIndex,
                                   bool& useCustomPalette) {
    if (!useCustomPalette) {
        return;
    }

    const CustomPaletteLookupResult lookup = LookupCharacterCustomPalette(selectId, static_cast<int>(colorIndex));
    LogCustomPaletteLookupIfChanged(playerLabel, selectId, static_cast<int>(colorIndex), lookup);
    if (lookup.exists) {
        std::ostringstream oss;
        oss << "[HOTSWAP][PAL] keeping custom palette for " << playerLabel
            << " char=" << GetCharacterSelectName(selectId) << "(" << selectId << ")"
            << " slot=" << (static_cast<int>(colorIndex) + 1);
        AppendCustomPaletteLookupDetails(oss, lookup);
        LogOut(oss.str(), true);
        return;
    }

    std::ostringstream oss;
    oss << "[HOTSWAP] custom palette disabled for " << playerLabel
        << " char=" << GetCharacterSelectName(selectId) << "(" << selectId << ")"
        << " slot=" << (static_cast<int>(colorIndex) + 1)
        << " reason=missing custom .pal";
    AppendCustomPaletteLookupDetails(oss, lookup);
    LogOut(oss.str(), true);
    useCustomPalette = false;
}

void SanitizeRequestedPaletteSelection(int p1CharId, int p2CharId, PaletteSelection& selection) {
    const PaletteSelection originalSelection = selection;
    if (!IsValidPaletteIndex(static_cast<int>(selection.p1Color))) {
        selection.p1Color = PALETTE_INDEX_FIRST;
        selection.p1UseCustomPalette = false;
    }
    if (!IsValidPaletteIndex(static_cast<int>(selection.p2Color))) {
        selection.p2Color = PALETTE_INDEX_FIRST;
        selection.p2UseCustomPalette = false;
    }

    // Character Select never allows an exact mirror to keep the same color;
    // reproduce that rule when the screen is skipped so both palette mappings
    // remain distinguishable and native resource selection sees a valid pair.
    if (p1CharId == p2CharId && selection.p1Color == selection.p2Color) {
        selection.p2Color = (selection.p2Color + 1) % PALETTE_SLOT_COUNT;
    }

    ResolveRequestedCustomPalette(p1CharId, "P1", selection.p1Color, selection.p1UseCustomPalette);
    ResolveRequestedCustomPalette(p2CharId, "P2", selection.p2Color, selection.p2UseCustomPalette);

    std::ostringstream oss;
    oss << "[HOTSWAP][PAL] sanitize chars="
        << GetCharacterSelectName(p1CharId) << "(" << p1CharId << ")/"
        << GetCharacterSelectName(p2CharId) << "(" << p2CharId << ")"
        << " before=" << (originalSelection.p1Color + 1) << "/" << (originalSelection.p2Color + 1)
        << " custom=" << (originalSelection.p1UseCustomPalette ? 1 : 0) << "/" << (originalSelection.p2UseCustomPalette ? 1 : 0)
        << " after=" << (selection.p1Color + 1) << "/" << (selection.p2Color + 1)
        << " custom=" << (selection.p1UseCustomPalette ? 1 : 0) << "/" << (selection.p2UseCustomPalette ? 1 : 0)
        << " changed=" << (!PaletteSelectionsEqual(originalSelection, selection) ? 1 : 0);
    LogOut(oss.str(), true);
}

bool SehCreateDirectPracticeFighters(RawDirectBootstrapResult& outResult) {
    outResult = RawDirectBootstrapResult{};

    __try {
        const uintptr_t base = GetEFZBase();
        if (!base) {
            outResult.failureStep = 1;
            return false;
        }

        outResult.failureStep = 2;
        if (!ResolveLoadingContext(outResult.loadingContext,
                                   outResult.gameSystem,
                                   outResult.p1SlotStorage,
                                   outResult.p2SlotStorage,
                                   true)) {
            return false;
        }

        uint8_t gameMode = 0xFF;
        uintptr_t stageHelper = 0;
        outResult.failureStep = 3;
        if (!SafeReadMemory(outResult.gameSystem + GAME_SYSTEM_MODE_OFFSET,
                            &gameMode, sizeof(gameMode))
            || gameMode != static_cast<uint8_t>(GameMode::Practice)
            || !SafeReadMemory(outResult.gameSystem + 4988,
                               &stageHelper, sizeof(stageHelper))
            || stageHelper != 0) {
            return false;
        }

        // Reproduce the Character-Select-established globals; replay I/O stays disabled.
        const uint8_t zeroByte = 0;
        const uint8_t oneByte = 1;
        const uint8_t twoRounds = 2;
        const DWORD zeroDword = 0;
        const uint8_t stage = static_cast<uint8_t>(s_requestedStage.load(std::memory_order_relaxed));
        const DWORD p1Custom = s_requestedP1CustomPalette.load(std::memory_order_relaxed) ? 1u : 0u;
        const DWORD p2Custom = s_requestedP2CustomPalette.load(std::memory_order_relaxed) ? 1u : 0u;
        const uint8_t clearedInputs[12] = {};
        outResult.failureStep = 4;
        if (!SafeWriteMemory(outResult.gameSystem + 12, clearedInputs, sizeof(clearedInputs))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_ACTIVE_PLAYER_OFFSET,
                                &zeroByte, sizeof(zeroByte))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_P1_CPU_OFFSET,
                                &zeroByte, sizeof(zeroByte))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_P2_CPU_OFFSET,
                                &oneByte, sizeof(oneByte))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_ROUND_COUNT_OFFSET,
                                &twoRounds, sizeof(twoRounds))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_MATCH_INDEX_OFFSET,
                                &oneByte, sizeof(oneByte))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_PROGRESSION_MASK_OFFSET,
                                &zeroDword, sizeof(zeroDword))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_PROGRESSION_FLAGS_OFFSET,
                                &zeroDword, sizeof(zeroDword))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_SPECIAL_STAGE_OFFSET,
                                &zeroByte, sizeof(zeroByte))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_CONTINUE_OFFSET,
                                &zeroByte, sizeof(zeroByte))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_NEXT_STAGE_OFFSET,
                                &zeroByte, sizeof(zeroByte))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_REPLAY_IO_MODE_OFFSET,
                                &zeroByte, sizeof(zeroByte))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_STAGE_OFFSET,
                                &stage, sizeof(stage))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_P1_CUSTOM_PALETTE_FLAG_OFFSET,
                                &p1Custom, sizeof(p1Custom))
            || !SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_P2_CUSTOM_PALETTE_FLAG_OFFSET,
                                &p2Custom, sizeof(p2Custom))) {
            return false;
        }

        const auto createCharacter = reinterpret_cast<CreateCharacterForPlayerFn>(
            base + RVA_CREATE_CHARACTER_FOR_PLAYER);
        if (!createCharacter) {
            outResult.failureStep = 5;
            return false;
        }

        const int p1SelectId = s_requestedP1Char.load(std::memory_order_relaxed);
        const int p2SelectId = s_requestedP2Char.load(std::memory_order_relaxed);
        outResult.failureStep = 6;
        createCharacter(reinterpret_cast<int*>(outResult.loadingContext),
                        static_cast<int>(outResult.p1SlotStorage),
                        static_cast<int>(outResult.p2SlotStorage),
                        0,
                        static_cast<char>(p1SelectId));
        if (!SafeReadMemory(outResult.p1SlotStorage,
                            &outResult.p1Character, sizeof(outResult.p1Character))
            || !outResult.p1Character) {
            return false;
        }
        outResult.p1Created = true;

        outResult.failureStep = 7;
        createCharacter(reinterpret_cast<int*>(outResult.loadingContext),
                        static_cast<int>(outResult.p2SlotStorage),
                        static_cast<int>(outResult.p1SlotStorage),
                        1,
                        static_cast<char>(p2SelectId));
        if (!SafeReadMemory(outResult.p2SlotStorage,
                            &outResult.p2Character, sizeof(outResult.p2Character))
            || !outResult.p2Character) {
            return false;
        }
        outResult.p2Created = true;

        const uint8_t p1Color = static_cast<uint8_t>(
            s_requestedP1Color.load(std::memory_order_relaxed));
        const uint8_t p2Color = static_cast<uint8_t>(
            s_requestedP2Color.load(std::memory_order_relaxed));
        outResult.failureStep = 8;
        if (!SafeWriteMemory(outResult.p1Character + CHARACTER_OBJECT_PALETTE_INDEX_OFFSET,
                             &p1Color, sizeof(p1Color))
            || !SafeWriteMemory(outResult.p2Character + CHARACTER_OBJECT_PALETTE_INDEX_OFFSET,
                                &p2Color, sizeof(p2Color))
            || !SafeReadMemory(outResult.p1Character + CHARACTER_OBJECT_SELECT_ID_OFFSET,
                               &outResult.p1SelectIdReadback,
                               sizeof(outResult.p1SelectIdReadback))
            || !SafeReadMemory(outResult.p2Character + CHARACTER_OBJECT_SELECT_ID_OFFSET,
                               &outResult.p2SelectIdReadback,
                               sizeof(outResult.p2SelectIdReadback))
            || !SafeReadMemory(outResult.p1Character + CHARACTER_OBJECT_PALETTE_INDEX_OFFSET,
                               &outResult.p1ColorReadback,
                               sizeof(outResult.p1ColorReadback))
            || !SafeReadMemory(outResult.p2Character + CHARACTER_OBJECT_PALETTE_INDEX_OFFSET,
                               &outResult.p2ColorReadback,
                               sizeof(outResult.p2ColorReadback))
            || !SafeReadMemory(outResult.p1Character + 164,
                               &outResult.p1CpuReadback, sizeof(outResult.p1CpuReadback))
            || !SafeReadMemory(outResult.p2Character + 164,
                               &outResult.p2CpuReadback, sizeof(outResult.p2CpuReadback))) {
            return false;
        }

        outResult.failureStep = 9;
        return outResult.p1SelectIdReadback == static_cast<uint8_t>(p1SelectId)
            && outResult.p2SelectIdReadback == static_cast<uint8_t>(p2SelectId)
            && outResult.p1ColorReadback == p1Color
            && outResult.p2ColorReadback == p2Color
            && outResult.p1CpuReadback == 0
            && outResult.p2CpuReadback == 1;
    } __except (outResult.sehCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void CleanupPartialDirectPracticeFighters(const RawDirectBootstrapResult& result) {
    __try {
        const uintptr_t base = GetEFZBase();
        if (!base) return;
        const auto cleanupPlayer = reinterpret_cast<CleanupPlayerObjectFn>(
            base + RVA_CLEANUP_PLAYER_OBJECT);
        if (!cleanupPlayer) return;

        uintptr_t p1Character = result.p1Character;
        uintptr_t p2Character = result.p2Character;
        if (!p1Character && result.failureStep >= 6 && result.p1SlotStorage) {
            SafeReadMemory(result.p1SlotStorage, &p1Character, sizeof(p1Character));
        }
        if (!p2Character && result.failureStep >= 7 && result.p2SlotStorage) {
            SafeReadMemory(result.p2SlotStorage, &p2Character, sizeof(p2Character));
        }

        if (p2Character && result.failureStep >= 7) {
            cleanupPlayer(reinterpret_cast<unsigned short*>(p2Character), 1);
            const uintptr_t zero = 0;
            SafeWriteMemory(result.p2SlotStorage, &zero, sizeof(zero));
            SafeWriteMemory(result.loadingContext + 16, &zero, sizeof(zero));
        }
        if (p1Character && result.failureStep >= 6) {
            cleanupPlayer(reinterpret_cast<unsigned short*>(p1Character), 1);
            const uintptr_t zero = 0;
            SafeWriteMemory(result.p1SlotStorage, &zero, sizeof(zero));
            SafeWriteMemory(result.loadingContext + 12, &zero, sizeof(zero));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool PracticeLoadingIsReadyForDirectBootstrap(int* loadingContext) {
    if (!loadingContext) return false;
    uintptr_t gameSystem = 0;
    uintptr_t p1SlotStorage = 0;
    uintptr_t p2SlotStorage = 0;
    uintptr_t p1Character = 0;
    uintptr_t p2Character = 0;
    uint8_t gameMode = 0xFF;
    const uintptr_t context = reinterpret_cast<uintptr_t>(loadingContext);
    return SafeReadMemory(context + 28, &gameSystem, sizeof(gameSystem))
        && gameSystem
        && SafeReadMemory(gameSystem + GAME_SYSTEM_MODE_OFFSET, &gameMode, sizeof(gameMode))
        && gameMode == static_cast<uint8_t>(GameMode::Practice)
        && SafeReadMemory(context + CS_SLOT_P1_PTR_OFFSET, &p1SlotStorage, sizeof(p1SlotStorage))
        && p1SlotStorage
        && SafeReadMemory(context + CS_SLOT_P2_PTR_OFFSET, &p2SlotStorage, sizeof(p2SlotStorage))
        && p2SlotStorage
        && SafeReadMemory(p1SlotStorage, &p1Character, sizeof(p1Character))
        && SafeReadMemory(p2SlotStorage, &p2Character, sizeof(p2Character))
        && !p1Character
        && !p2Character;
}

char __fastcall HookedUpdateLoadingScreen(int* loadingContext, void* /*edx*/) {
    auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedUpdateLoadingScreen>());
    if (!execution.Admitted()) return s_originalUpdateLoadingScreen
        ? s_originalUpdateLoadingScreen(loadingContext) : static_cast<char>(SCREEN_CHARACTER_SELECT);
    DirectBootstrapState directState =
        s_directBootstrapState.load(std::memory_order_acquire);
    const auto directAction =
        CharacterHotswap::Transition::DecideDirectLoadingAction(directState);
    if (directAction == CharacterHotswap::Transition::DirectLoadingAction::NativeUpdate) {
        // A canceled Armed request can still queue a delayed Practice Loading transition; two empty owner slots is its signature - redirect before the native tail dereferences nulls.
        if (PracticeLoadingIsReadyForDirectBootstrap(loadingContext)) {
            LogOut("[HOTSWAP][DIRECT] unowned Practice Loading had two empty fighter slots; returning to Character Select", true);
            return static_cast<char>(SCREEN_CHARACTER_SELECT);
        }
        const char nextScreen = s_originalUpdateLoadingScreen
            ? s_originalUpdateLoadingScreen(loadingContext)
            : static_cast<char>(SCREEN_CHARACTER_SELECT);
        const RequestState requestState = s_state.load(std::memory_order_acquire);
        if (nextScreen == SCREEN_BATTLE &&
            (requestState == RequestState::AwaitingLoading ||
             requestState == RequestState::AwaitingMatch)) {
            s_selectorLoadingHandoffComplete.store(true,
                                                    std::memory_order_release);
        }
        return nextScreen;
    }

    if (directAction == CharacterHotswap::Transition::DirectLoadingAction::ReturnBattle) {
        return static_cast<char>(SCREEN_BATTLE);
    }
    if (directAction == CharacterHotswap::Transition::DirectLoadingAction::HoldLoading) {
        return static_cast<char>(SCREEN_LOADING);
    }

    DirectBootstrapState expectedEntry = DirectBootstrapState::Armed;
    if (!s_directBootstrapState.compare_exchange_strong(
            expectedEntry, DirectBootstrapState::Entered,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        // The owned native loader is one-shot; EFZ may re-poll before committing the returned screen, so preserve the proven Battle result.
        if (expectedEntry == DirectBootstrapState::BattleHandoff) {
            return static_cast<char>(SCREEN_BATTLE);
        }
        if (expectedEntry == DirectBootstrapState::Entered) {
            return static_cast<char>(SCREEN_LOADING);
        }
            // Cancellation won the race; the slots are still empty and the native path would hit the null-fighter hazard.
        return static_cast<char>(SCREEN_CHARACTER_SELECT);
    }

    // Empty owner slots are an entry precondition only; they change during the native loader and are not a post-load guard.
    if (!PracticeLoadingIsReadyForDirectBootstrap(loadingContext)) {
        s_selectorLoadingHandoffComplete.store(false, std::memory_order_release);
        s_directBootstrapState.store(DirectBootstrapState::Idle,
                                     std::memory_order_release);
        s_directAbortAfterHandoff.store(false, std::memory_order_release);
        LogOut("[HOTSWAP][DIRECT] Loading entry precondition failed; returning to Character Select fallback", true);
        SetState(RequestState::AwaitingCharacterSelect,
                 UiStatus::Exiting,
                 "direct Loading entry was not empty/ready; selector fallback");
        return static_cast<char>(SCREEN_CHARACTER_SELECT);
    }

    if (IsNetplaySuspendActive() || IsNetplaySessionActive()) {
        s_directBootstrapState.store(DirectBootstrapState::Idle,
                                     std::memory_order_release);
        s_directAbortAfterHandoff.store(false, std::memory_order_release);
        FailRequest("direct Loading canceled because netplay became active", GamePhase::Loading);
        return static_cast<char>(SCREEN_CHARACTER_SELECT);
    }

    RawDirectBootstrapResult result{};
    if (!SehCreateDirectPracticeFighters(result)) {
        CleanupPartialDirectPracticeFighters(result);
        std::ostringstream oss;
        oss << "[HOTSWAP][DIRECT] Loading bootstrap failed"
            << " step=" << result.failureStep
            << " seh=" << result.sehCode
            << " loading=" << Hex(result.loadingContext)
            << " gs=" << Hex(result.gameSystem)
            << " slots=" << Hex(result.p1SlotStorage) << "/" << Hex(result.p2SlotStorage)
            << " chars=" << Hex(result.p1Character) << "/" << Hex(result.p2Character)
            << "; returning to Character Select fallback";
        LogOut(oss.str(), true);
        s_selectorLoadingHandoffComplete.store(false, std::memory_order_release);
        s_directBootstrapState.store(DirectBootstrapState::Idle,
                                     std::memory_order_release);
        s_directAbortAfterHandoff.store(false, std::memory_order_release);
        SetState(RequestState::AwaitingCharacterSelect,
                 UiStatus::Exiting,
                 "direct Loading bootstrap failed; selector fallback");
        return static_cast<char>(SCREEN_CHARACTER_SELECT);
    }

    {
        std::ostringstream oss;
        oss << "[HOTSWAP][DIRECT] fighters created on EFZ Loading thread"
            << " loading=" << Hex(result.loadingContext)
            << " gs=" << Hex(result.gameSystem)
            << " p1=" << GetCharacterSelectName(result.p1SelectIdReadback)
            << "(" << static_cast<int>(result.p1SelectIdReadback) << ")"
            << " p2=" << GetCharacterSelectName(result.p2SelectIdReadback)
            << "(" << static_cast<int>(result.p2SelectIdReadback) << ")"
            << " palette=" << (static_cast<int>(result.p1ColorReadback) + 1)
            << "/" << (static_cast<int>(result.p2ColorReadback) + 1)
            << " cpu=" << result.p1CpuReadback << "/" << result.p2CpuReadback;
        LogOut(oss.str(), true);
    }

    const char nextScreen = s_originalUpdateLoadingScreen
        ? s_originalUpdateLoadingScreen(loadingContext)
        : static_cast<char>(SCREEN_CHARACTER_SELECT);
    if (nextScreen != 3) {
        s_directBootstrapState.store(DirectBootstrapState::Idle,
                                     std::memory_order_release);
        s_directAbortAfterHandoff.store(false, std::memory_order_release);
        FailRequest("native Loading did not transition to Battle", GamePhase::Loading);
    } else {
        DirectBootstrapState expected = DirectBootstrapState::Entered;
        if (!s_directBootstrapState.compare_exchange_strong(
                expected, DirectBootstrapState::BattleHandoff,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            // Ownership already transferred to Battle; overriding the result would double-clean - preserve the handoff and finalize cancellation after Match.
            s_directAbortAfterHandoff.store(true, std::memory_order_release);
            s_directBootstrapState.store(DirectBootstrapState::BattleHandoff,
                                         std::memory_order_release);
            s_state.store(RequestState::Failed, std::memory_order_release);
            s_uiStatus.store(UiStatus::Failed, std::memory_order_release);
            LogOut("[HOTSWAP][DIRECT] loader completed after logical cancellation; deferring cleanup until Battle", true);
        }
    }
    return nextScreen;
}

bool SehApplySelections(uintptr_t capturedContext,uintptr_t capturedGameSystem,int p1CharId,
                        int p2CharId,
                        int stageId,
                        int p1ColorIndex,
                        int p2ColorIndex,
                        bool p1UseCustomPalette,
                        bool p2UseCustomPalette,
                        RawApplyResult& outResult) {
    outResult = RawApplyResult{};

    __try {
        const uintptr_t base = GetEFZBase();
        if (!base) {
            return false;
        }

        if (!ResolveCharacterSelectContext(outResult.screenContext, outResult.gameSystem)) {
            return false;
        }
        if(capturedContext && (outResult.screenContext!=capturedContext || outResult.gameSystem!=capturedGameSystem))return false;

        // Clear held inputs so they cannot race the synthetic selection; Character Select repolls these bytes on later frames.
        const uint8_t clearedInputs[12] = {};
        SafeWriteMemory(outResult.gameSystem + 12, clearedInputs, sizeof(clearedInputs));

        int p1SlotStorage = 0;
        int p2SlotStorage = 0;
        if (!SafeReadMemory(outResult.screenContext + CS_SLOT_P1_PTR_OFFSET, &p1SlotStorage, sizeof(p1SlotStorage)) || !p1SlotStorage) {
            return false;
        }
        if (!SafeReadMemory(outResult.screenContext + CS_SLOT_P2_PTR_OFFSET, &p2SlotStorage, sizeof(p2SlotStorage)) || !p2SlotStorage) {
            return false;
        }

        outResult.p1SlotStorage = static_cast<uintptr_t>(p1SlotStorage);
        outResult.p2SlotStorage = static_cast<uintptr_t>(p2SlotStorage);

        const uint8_t p1Char = static_cast<uint8_t>(p1CharId);
        const uint8_t p2Char = static_cast<uint8_t>(p2CharId);
        const uint8_t p1Color = static_cast<uint8_t>(IsValidPaletteIndex(p1ColorIndex) ? p1ColorIndex : PALETTE_INDEX_FIRST);
        const uint8_t p2Color = static_cast<uint8_t>(IsValidPaletteIndex(p2ColorIndex) ? p2ColorIndex : PALETTE_INDEX_FIRST);
        const uint8_t p1State = 4;
        const uint8_t p2State = 4;
        const uint8_t stage = static_cast<uint8_t>(stageId);
        const uint16_t stageAutoconfirmTicks = 1;
        const DWORD p1CustomFlag = p1UseCustomPalette ? 1u : 0u;
        const DWORD p2CustomFlag = p2UseCustomPalette ? 1u : 0u;

        if (!SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_STAGE_OFFSET, &stage, sizeof(stage))) {
            return false;
        }
        if (!SafeWriteMemory(outResult.screenContext + CS_SELECTED_P1_CHAR_OFFSET, &p1Char, sizeof(p1Char))) {
            return false;
        }
        if (!SafeWriteMemory(outResult.screenContext + CS_SELECTED_P2_CHAR_OFFSET, &p2Char, sizeof(p2Char))) {
            return false;
        }
        if (!SafeWriteMemory(outResult.screenContext + CS_SELECTED_P1_COLOR_OFFSET, &p1Color, sizeof(p1Color))) {
            return false;
        }
        if (!SafeWriteMemory(outResult.screenContext + CS_SELECTED_P2_COLOR_OFFSET, &p2Color, sizeof(p2Color))) {
            return false;
        }
        if (!SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_P1_CUSTOM_PALETTE_FLAG_OFFSET, &p1CustomFlag, sizeof(p1CustomFlag))) {
            return false;
        }
        if (!SafeWriteMemory(outResult.gameSystem + GAME_SYSTEM_P2_CUSTOM_PALETTE_FLAG_OFFSET, &p2CustomFlag, sizeof(p2CustomFlag))) {
            return false;
        }

        const auto initSelectedCharacter = reinterpret_cast<InitializeSelectedCharacterFn>(base + RVA_INITIALIZE_SELECTED_CHARACTER);
        if (!initSelectedCharacter) {
            return false;
        }

        initSelectedCharacter(reinterpret_cast<int*>(outResult.screenContext), p1SlotStorage, p2SlotStorage, 0);
        initSelectedCharacter(reinterpret_cast<int*>(outResult.screenContext), p2SlotStorage, p1SlotStorage, 1);

        if (!SafeWriteMemory(outResult.screenContext + CS_P1_STATE_OFFSET, &p1State, sizeof(p1State))) {
            return false;
        }
        if (!SafeWriteMemory(outResult.screenContext + CS_P2_STATE_OFFSET, &p2State, sizeof(p2State))) {
            return false;
        }
        if (!SafeWriteMemory(outResult.screenContext + CS_STAGE_AUTOCONFIRM_TIMER_OFFSET, &stageAutoconfirmTicks, sizeof(stageAutoconfirmTicks))) {
            return false;
        }

        if (!SafeReadMemory(outResult.p1SlotStorage, &outResult.p1Character, sizeof(outResult.p1Character))) {
            return false;
        }
        if (!SafeReadMemory(outResult.p2SlotStorage, &outResult.p2Character, sizeof(outResult.p2Character))) {
            return false;
        }
        if (!SafeWriteMemory(outResult.p1Character + CHARACTER_OBJECT_PALETTE_INDEX_OFFSET, &p1Color, sizeof(p1Color))) {
            return false;
        }
        if (!SafeWriteMemory(outResult.p2Character + CHARACTER_OBJECT_PALETTE_INDEX_OFFSET, &p2Color, sizeof(p2Color))) {
            return false;
        }
        SafeReadMemory(outResult.p1Character + CHARACTER_OBJECT_PALETTE_INDEX_OFFSET,
                       &outResult.p1ColorReadback,
                       sizeof(outResult.p1ColorReadback));
        SafeReadMemory(outResult.p2Character + CHARACTER_OBJECT_PALETTE_INDEX_OFFSET,
                       &outResult.p2ColorReadback,
                       sizeof(outResult.p2ColorReadback));
        SafeReadMemory(outResult.gameSystem + GAME_SYSTEM_P1_CUSTOM_PALETTE_FLAG_OFFSET,
                       &outResult.p1CustomFlagReadback,
                       sizeof(outResult.p1CustomFlagReadback));
        SafeReadMemory(outResult.gameSystem + GAME_SYSTEM_P2_CUSTOM_PALETTE_FLAG_OFFSET,
                       &outResult.p2CustomFlagReadback,
                       sizeof(outResult.p2CustomFlagReadback));

        return outResult.p1Character != 0 && outResult.p2Character != 0;
    } __except (outResult.sehCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void LogQueuedRequest(GamePhase phase,
                      GameMode mode,
                      int p1CharId,
                      int p2CharId,
                      int stageId,
                      int bgmTrack,
                      const PaletteSelection& paletteSelection) {
    std::ostringstream oss;
    oss << "[HOTSWAP] queued reload"
        << " phase=" << PhaseName(phase)
        << " mode=" << GetGameModeName(mode)
        << " p1=" << GetCharacterSelectName(p1CharId) << "(" << p1CharId << ")"
        << " p2=" << GetCharacterSelectName(p2CharId) << "(" << p2CharId << ")"
        << " stage=" << stageId
        << " bgm=" << bgmTrack
        << " palette=" << (static_cast<int>(paletteSelection.p1Color) + 1)
        << "/" << (static_cast<int>(paletteSelection.p2Color) + 1)
        << " custom=" << (paletteSelection.p1UseCustomPalette ? 1 : 0)
        << "/" << (paletteSelection.p2UseCustomPalette ? 1 : 0);
    LogOut(oss.str(), true);
}

void ApplyQueuedSelections(uintptr_t capturedContext,uintptr_t capturedGameSystem) {
    auto expected=RequestState::PendingApply;
    if(!s_state.compare_exchange_strong(expected,RequestState::ApplyingSelections,std::memory_order_acq_rel))return;
    if(!IsPracticeContext()) {
        FailRequest("character select apply refused outside Practice", GamePhase::CharacterSelect);
        return;
    }
            {
                RawApplyResult result{};
                const int p1CharId = s_requestedP1Char.load(std::memory_order_relaxed);
                const int p2CharId = s_requestedP2Char.load(std::memory_order_relaxed);
                const int p1ColorIndex = s_requestedP1Color.load(std::memory_order_relaxed);
                const int p2ColorIndex = s_requestedP2Color.load(std::memory_order_relaxed);
                const bool p1UseCustomPalette = s_requestedP1CustomPalette.load(std::memory_order_relaxed) != 0;
                const bool p2UseCustomPalette = s_requestedP2CustomPalette.load(std::memory_order_relaxed) != 0;
                const int stageId = s_requestedStage.load(std::memory_order_relaxed);
                if (!SehApplySelections(capturedContext,capturedGameSystem,p1CharId,
                                        p2CharId,
                                        stageId,
                                        p1ColorIndex,
                                        p2ColorIndex,
                                        p1UseCustomPalette,
                                        p2UseCustomPalette,
                                        result)) {
                    std::ostringstream oss;
                    oss << "[HOTSWAP] apply failed"
                        << " seh=" << result.sehCode
                        << " cs=" << Hex(result.screenContext)
                        << " gs=" << Hex(result.gameSystem)
                        << " slot1=" << Hex(result.p1SlotStorage)
                        << " slot2=" << Hex(result.p2SlotStorage);
                    LogOut(oss.str(), true);
                    FailRequest("failed to write character select state", GamePhase::CharacterSelect);
                    return;
                }

                std::ostringstream oss;
                oss << "[HOTSWAP] apply succeeded"
                    << " cs=" << Hex(result.screenContext)
                    << " gs=" << Hex(result.gameSystem)
                    << " p1Obj=" << Hex(result.p1Character)
                    << " p2Obj=" << Hex(result.p2Character)
                    << " stage=" << stageId
                    << " palette=" << (p1ColorIndex + 1)
                    << "/" << (p2ColorIndex + 1)
                    << " custom=" << (p1UseCustomPalette ? 1 : 0)
                    << "/" << (p2UseCustomPalette ? 1 : 0)
                    << " readbackPalette=" << (static_cast<int>(result.p1ColorReadback) + 1)
                    << "/" << (static_cast<int>(result.p2ColorReadback) + 1)
                    << " readbackCustom=" << result.p1CustomFlagReadback
                    << "/" << result.p2CustomFlagReadback;
                LogOut(oss.str(), true);
            }
            SetState(RequestState::AwaitingLoading, UiStatus::WaitingLoading, "selections injected into character select");
            return;
}

void PlayBgmOverrideOnGameThread(uintptr_t track) {
    const uintptr_t gameStatePtr = GetGameStatePtr();
    const int liveBgmBuffer = gameStatePtr ? GetBGMBufferIndex(gameStatePtr) : -1;
    if (gameStatePtr && PlayBGM(gameStatePtr, static_cast<unsigned short>(track))) {
        LogOut("[HOTSWAP] applied BGM override track=" + std::to_string(track), true);
        return;
    }
    std::ostringstream oss;
    oss << "[HOTSWAP] failed to apply BGM override track=" << track
        << " liveBufferBeforeCall=" << liveBgmBuffer;
    LogOut(oss.str(), true);
}

void CompleteReload(GamePhase currentPhase, bool publishReceipt = true, uintptr_t capturedGameSystem = 0) {
    auto expected=s_state.load(std::memory_order_acquire);
    if(expected!=RequestState::AwaitingLoading && expected!=RequestState::AwaitingMatch &&
       !(expected==RequestState::Failed && s_directAbortAfterHandoff.load(std::memory_order_acquire)))return;
    if(!s_state.compare_exchange_strong(expected,RequestState::Completing,std::memory_order_acq_rel))return;
    s_loadingRequest.Clear();
    const int requestedBgmTrack = s_requestedBgmTrack.load(std::memory_order_relaxed);
    const uintptr_t gameStatePtr = capturedGameSystem?capturedGameSystem:GetGameStatePtr();
    {
        std::ostringstream oss;
        oss << "[HOTSWAP] completing reload"
            << " phase=" << PhaseName(currentPhase)
            << " gameState=" << Hex(gameStatePtr)
            << " p1BaseBeforeInvalidate=" << Hex(GetPlayerBase(1))
            << " p2BaseBeforeInvalidate=" << Hex(GetPlayerBase(2));
        AppendRequestedReloadSummary(oss);
        LogOut(oss.str(), true);
    }
    if (!publishReceipt) {
        LogOut("[HOTSWAP][DIRECT] skipped mission BGM override for canceled handoff", true);
    } else if (gameStatePtr && requestedBgmTrack >= 0) {
        if (!GameThread::Post(&PlayBgmOverrideOnGameThread, static_cast<uintptr_t>(requestedBgmTrack))) {
            LogOut("[HOTSWAP] BGM override track=" + std::to_string(requestedBgmTrack)
                   + " refused: game-thread queue unavailable", true);
        }
    } else if (!gameStatePtr) {
        LogOut("[HOTSWAP] skipped BGM override because game state pointer was unavailable", true);
    } else {
        LogOut("[HOTSWAP] keeping native stage BGM (mission bgm=-1)", true);
    }

    InvalidateGameStatePtrCache();
    InvalidatePlayerBaseCache();
    CharacterSettings::InvalidateAllCharacterPointerCaches();
    InvalidateAutoActionCharacterCaches("character hotswap reload immediate");
    PauseIntegration::ResetCachedPointers("character hotswap reload immediate");
    ResetCollisionHookSessionCaches("character hotswap reload immediate");
    ComboOverlay::ResetState("character hotswap reload immediate");
    LogOut("[HOTSWAP] applied immediate session reset before lifecycle resync", true);
    RequestRuntimeLifecycleResync("character hotswap reload complete");
    LogOut("[HOTSWAP] reload completed and lifecycle resync requested", true);
    publishReceipt=publishReceipt && !s_directAbortAfterHandoff.load(std::memory_order_acquire);
    if (publishReceipt) PublishCompletedReceipt();
    else InvalidateCompletedReceipt();
    s_selectorLoadingHandoffComplete.store(false, std::memory_order_release);
    s_directBootstrapState.store(DirectBootstrapState::Idle,
                                 std::memory_order_release);
    s_directAbortAfterHandoff.store(false, std::memory_order_release);
    s_state.store(publishReceipt ? RequestState::Idle : RequestState::Failed,
                  std::memory_order_release);
    s_uiStatus.store(publishReceipt ? UiStatus::Completed : UiStatus::Failed,
                     std::memory_order_release);
    s_waitTicks.store(0, std::memory_order_relaxed);
    s_lastActivePhase = currentPhase;
    s_awaitNativeCompletion.store(false,std::memory_order_release);
}

} // namespace

uint32_t CaptureLoadingRequest(const EfzTmIdentityV1& identity) {
    return s_loadingRequest.Capture(identity);
}
void ConsumeLoadingRequest(const EfzTmIdentityV1& identity,uint32_t ticket,uint32_t nativeResult,uint32_t acceptedResult) {
    (void)s_loadingRequest.Consume(identity,ticket,nativeResult,acceptedResult,[](LoadingReturn result,const EfzTmIdentityV1& identity) {
        auto expected=RequestState::AwaitingLoading;
        if(result==LoadingReturn::BattleTransferred) {
            if(!identity.battleWorld && s_nativeFrontendInstalled.load(std::memory_order_acquire))
                s_awaitNativeCompletion.store(true,std::memory_order_release);
            if(s_state.compare_exchange_strong(expected,RequestState::AwaitingMatch,std::memory_order_acq_rel)) {
                s_uiStatus.store(UiStatus::WaitingMatch,std::memory_order_release);
                s_waitTicks.store(0,std::memory_order_relaxed);
            }
        } else if(result==LoadingReturn::RedirectedTransfer) {
            if(s_state.compare_exchange_strong(expected,RequestState::Failed,std::memory_order_acq_rel) ||
               (expected=RequestState::AwaitingMatch,s_state.compare_exchange_strong(expected,RequestState::Failed,std::memory_order_acq_rel))) {
                s_directAbortAfterHandoff.store(true,std::memory_order_release);
                s_uiStatus.store(UiStatus::Failed,std::memory_order_release);
            }
        }
    });
}

bool InstallDirectPracticeBootstrap() {
    if (s_directBootstrapInstalled.load(std::memory_order_acquire)) {
        return true;
    }

    const uintptr_t base = GetEFZBase();
    if (!base) {
        return false;
    }

    const uintptr_t updateTarget = base + RVA_UPDATE_LOADING_SCREEN;
    if (s_updateLoadingScreenTarget!=updateTarget &&
        MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_updateLoadingScreenTarget))) return false;
    const uintptr_t createTarget = base + RVA_CREATE_CHARACTER_FOR_PLAYER;
    const uintptr_t cleanupTarget = base + RVA_CLEANUP_PLAYER_OBJECT;
    static const uint8_t kExpectedUpdate[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x4C};
    static const uint8_t kExpectedCreate[] = {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00};
    static const uint8_t kExpectedCleanup[] = {0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC};
    uint8_t updateBytes[sizeof(kExpectedUpdate)] = {};
    uint8_t createBytes[sizeof(kExpectedCreate)] = {};
    uint8_t cleanupBytes[sizeof(kExpectedCleanup)] = {};
    if ((!MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(updateTarget)) &&
         (!SafeReadMemory(updateTarget, updateBytes, sizeof(updateBytes)) ||
          std::memcmp(updateBytes, kExpectedUpdate, sizeof(kExpectedUpdate)) != 0))
        || !SafeReadMemory(createTarget, createBytes, sizeof(createBytes))
        || std::memcmp(createBytes, kExpectedCreate, sizeof(kExpectedCreate)) != 0
        || !SafeReadMemory(cleanupTarget, cleanupBytes, sizeof(cleanupBytes))
        || std::memcmp(cleanupBytes, kExpectedCleanup, sizeof(kExpectedCleanup)) != 0) {
        LogOut("[HOTSWAP][DIRECT] executable signatures did not match; Character Select fallback retained", true);
        return false;
    }

    bool alreadyCreated = false;
    if (!MinHookUtils::CreateHook(reinterpret_cast<void*>(updateTarget),
                                  reinterpret_cast<void*>(&HookedUpdateLoadingScreen),
                                  reinterpret_cast<void**>(&s_originalUpdateLoadingScreen),
                                  "[HOTSWAP][DIRECT]",
                                  "updateLoadingScreen",
                                  &alreadyCreated, &MinHookUtils::TicketFor<&HookedUpdateLoadingScreen>())
        || !s_originalUpdateLoadingScreen) {
        LogOut("[HOTSWAP][DIRECT] Loading hook is already owned or unavailable; selector fallback retained", true);
        (void)PracticeHooks::ReleaseRetiredOriginal(MinHookUtils::OwnedHooks(), updateTarget, s_originalUpdateLoadingScreen);
        return false;
    }
    if (!MinHookUtils::EnableHook(reinterpret_cast<void*>(updateTarget),
                                  "[HOTSWAP][DIRECT]",
                                  "updateLoadingScreen")) {
        if (MinHookUtils::RemoveHook(reinterpret_cast<void*>(updateTarget),
                                     "[HOTSWAP][DIRECT]",
                                     "updateLoadingScreen")) {
            s_originalUpdateLoadingScreen = nullptr;
        } else {
            // Retain the trampoline in case removal left it callable.
            s_updateLoadingScreenTarget = updateTarget;
            s_directBootstrapCleanupIncomplete.store(true,
                                                     std::memory_order_release);
            LogOut("[HOTSWAP][DIRECT] failed hook removal retained its trampoline", true);
        }
        return false;
    }

    s_updateLoadingScreenTarget = updateTarget;
    s_directBootstrapInstalled.store(true, std::memory_order_release);
    s_directBootstrapCleanupIncomplete.store(false, std::memory_order_release);
    LogOut("[HOTSWAP][DIRECT] native Practice Loading bootstrap installed", true);
    return true;
}

void UninstallDirectPracticeBootstrap() {
    s_selectorLoadingHandoffComplete.store(false, std::memory_order_release);
    const DirectBootstrapState state =
        s_directBootstrapState.load(std::memory_order_acquire);
    if (state == DirectBootstrapState::Entered ||
        state == DirectBootstrapState::BattleHandoff) {
        s_directAbortAfterHandoff.store(true, std::memory_order_release);
    } else {
        s_directBootstrapState.store(DirectBootstrapState::Idle,
                                     std::memory_order_release);
        s_directAbortAfterHandoff.store(false, std::memory_order_release);
    }
    if (state == DirectBootstrapState::Entered) {
        LogOut("[HOTSWAP][DIRECT] uninstall deferred while native Loading callback is executing", true);
        return;
    }
    if (s_updateLoadingScreenTarget) {
        (void)MinHookUtils::DisableHook(
            reinterpret_cast<void*>(s_updateLoadingScreenTarget),
            "[HOTSWAP][DIRECT]", "updateLoadingScreen");
        if (!MinHookUtils::RemoveHook(
                reinterpret_cast<void*>(s_updateLoadingScreenTarget),
                "[HOTSWAP][DIRECT]", "updateLoadingScreen")) {
            LogOut("[HOTSWAP][DIRECT] uninstall incomplete; retaining hook trampoline", true);
            s_directBootstrapInstalled.store(false, std::memory_order_release);
            s_directBootstrapCleanupIncomplete.store(true,
                                                     std::memory_order_release);
            return;
        }
    }
    s_directBootstrapInstalled.store(false, std::memory_order_release);
    s_directBootstrapCleanupIncomplete.store(false, std::memory_order_release);
    s_updateLoadingScreenTarget = 0;
    s_originalUpdateLoadingScreen = nullptr;
}

bool QueueDirectPracticeLoad(int p1SelectId,
                             int p2SelectId,
                             int stageId,
                             const PaletteSelection& requestedPaletteSelection,
                             int bgmTrack) {
    if (!IsValidSelectId(p1SelectId) || !IsValidSelectId(p2SelectId)
        || !IsValidStageId(stageId) || bgmTrack < -1 || bgmTrack > 0xFFFF) {
        LogOut("[HOTSWAP][DIRECT] request rejected: mission selection is out of range", true);
        return false;
    }
    if (!s_directBootstrapInstalled.load(std::memory_order_acquire)) {
        LogOut("[HOTSWAP][DIRECT] request unavailable: Loading hook is not installed", true);
        return false;
    }
    if (IsNetplaySuspendActive() || IsNetplaySessionActive()) {
        LogOut("[HOTSWAP][DIRECT] request rejected while netplay suspension/session is active", true);
        return false;
    }
    if (IsActiveState(s_state.load(std::memory_order_acquire))) {
        LogOut("[HOTSWAP][DIRECT] request ignored because another reload is active", true);
        return false;
    }
    if (DirectBootstrapOwnsTransaction()) {
        LogOut("[HOTSWAP][DIRECT] request ignored until the prior loader handoff is finalized", true);
        return false;
    }

    const GamePhase phase = GetCurrentGamePhase();
    uint8_t rawScreen = 0xFF;
    const uintptr_t base = GetEFZBase();
    if (base) {
        SafeReadMemory(base + EFZ_BASE_OFFSET_SCREEN_STATE,
                       &rawScreen, sizeof(rawScreen));
    }
    const bool fromTitle = rawScreen == SCREEN_TITLE
        && (phase == GamePhase::Menu || phase == GamePhase::Unknown);
    const bool fromMatch = rawScreen == SCREEN_BATTLE && phase == GamePhase::Match
        && GetCurrentGameMode() == GameMode::Practice
        && CanRequestFrontendExit(FrontendExitTarget::Loading);
    if (!fromTitle && !fromMatch) {
        LogOut("[HOTSWAP][DIRECT] request unavailable outside Title/local Practice Match", true);
        return false;
    }

    uintptr_t loadingContext = 0;
    uintptr_t gameSystem = 0;
    uintptr_t p1SlotStorage = 0;
    uintptr_t p2SlotStorage = 0;
    if (!ResolveLoadingContext(loadingContext,
                               gameSystem,
                               p1SlotStorage,
                               p2SlotStorage,
                               fromTitle)) {
        LogOut("[HOTSWAP][DIRECT] Loading context is not ready/empty; selector fallback retained", true);
        return false;
    }

    if(!CaptureQueuedBattleExit(fromMatch))return false;
    PaletteSelection paletteSelection = requestedPaletteSelection;
    SanitizeRequestedPaletteSelection(p1SelectId, p2SelectId, paletteSelection);
    InvalidateCompletedReceipt();
    s_requestedP1Char.store(p1SelectId, std::memory_order_relaxed);
    s_requestedP2Char.store(p2SelectId, std::memory_order_relaxed);
    s_requestedP1Color.store(paletteSelection.p1Color, std::memory_order_relaxed);
    s_requestedP2Color.store(paletteSelection.p2Color, std::memory_order_relaxed);
    s_requestedP1CustomPalette.store(paletteSelection.p1UseCustomPalette ? 1 : 0,
                                     std::memory_order_relaxed);
    s_requestedP2CustomPalette.store(paletteSelection.p2UseCustomPalette ? 1 : 0,
                                     std::memory_order_relaxed);
    s_requestedStage.store(stageId, std::memory_order_relaxed);
    s_requestedBgmTrack.store(bgmTrack, std::memory_order_relaxed);
    // Publish the fully initialized transaction in one release store.
    s_selectorLoadingHandoffComplete.store(false, std::memory_order_release);
    PublishLoadingRequest();
    s_directBootstrapState.store(DirectBootstrapState::Armed,
                                 std::memory_order_release);
    s_directAbortAfterHandoff.store(false, std::memory_order_release);

    std::ostringstream oss;
    oss << "[HOTSWAP][DIRECT] queued Practice Loading"
        << " origin=" << (fromMatch ? "match" : "title")
        << " p1=" << GetCharacterSelectName(p1SelectId) << "(" << p1SelectId << ")"
        << " p2=" << GetCharacterSelectName(p2SelectId) << "(" << p2SelectId << ")"
        << " stage=" << stageId
        << " bgm=" << bgmTrack
        << " palette=" << (paletteSelection.p1Color + 1)
        << "/" << (paletteSelection.p2Color + 1)
        << " loading=" << Hex(loadingContext);
    LogOut(oss.str(), true);

    if (fromMatch) {
        SetState(RequestState::PendingExitRequest,
                 UiStatus::Exiting,
                 "leaving match through native cleanup for direct Loading");
    } else {
        SetState(RequestState::AwaitingLoading,
                 UiStatus::WaitingLoading,
                 "title will hand off directly to Loading");
    }
    return true;
}

bool IsDirectPracticeLoadPending() {
    return DirectBootstrapOwnsTransaction();
}

void CancelDirectPracticeLoad(const char* reason) {
    // Completion owns the request tuple until its side effects and receipt finish; cancellation cannot requeue here.
    if(s_state.load(std::memory_order_acquire)==RequestState::Completing) {
        s_directAbortAfterHandoff.store(true,std::memory_order_release);
        return;
    }
    const DirectBootstrapState previousState =
        s_directBootstrapState.load(std::memory_order_acquire);
    const bool deferred = previousState == DirectBootstrapState::Entered ||
                          previousState == DirectBootstrapState::BattleHandoff;
    if (!deferred && previousState != DirectBootstrapState::Idle) s_loadingRequest.Clear();
    if (deferred) {
        s_directAbortAfterHandoff.store(true, std::memory_order_release);
    } else {
        s_directBootstrapState.store(DirectBootstrapState::Idle,
                                     std::memory_order_release);
        s_directAbortAfterHandoff.store(false, std::memory_order_release);
    }
    s_selectorLoadingHandoffComplete.store(false, std::memory_order_release);
    const bool hadPending = previousState != DirectBootstrapState::Idle;
    if (!hadPending) {
        return;
    }
    s_state.store(deferred ? RequestState::Failed : RequestState::Idle,
                  std::memory_order_release);
    s_uiStatus.store(deferred ? UiStatus::Failed : UiStatus::Ready,
                     std::memory_order_release);
    s_waitTicks.store(0, std::memory_order_relaxed);
    LogOut(std::string("[HOTSWAP][DIRECT] owned Practice load canceled: ")
           + (reason ? reason : "unspecified"), true);
}

bool ConsumeCompletedPracticeLoad(int p1SelectId,
                                  int p2SelectId,
                                  int stageId,
                                  const PaletteSelection& paletteSelection,
                                  int bgmTrack) {
    if (!s_completedReceiptAvailable.exchange(false, std::memory_order_acq_rel)) {
        return false;
    }
    const uint32_t receiptGeneration =
        s_completedReceiptGeneration.exchange(0, std::memory_order_acq_rel);
    const uint32_t currentGeneration = GetRuntimeLifecycleGeneration();
    const bool generationMatches =
        CharacterHotswap::Transition::ReceiptGenerationMatches(
            receiptGeneration, currentGeneration);
    const bool matches = generationMatches &&
        s_completedP1Char.load(std::memory_order_relaxed) == p1SelectId &&
        s_completedP2Char.load(std::memory_order_relaxed) == p2SelectId &&
        s_completedP1Color.load(std::memory_order_relaxed) == paletteSelection.p1Color &&
        s_completedP2Color.load(std::memory_order_relaxed) == paletteSelection.p2Color &&
        s_completedP1Custom.load(std::memory_order_relaxed) ==
            (paletteSelection.p1UseCustomPalette ? 1 : 0) &&
        s_completedP2Custom.load(std::memory_order_relaxed) ==
            (paletteSelection.p2UseCustomPalette ? 1 : 0) &&
        s_completedStage.load(std::memory_order_relaxed) == stageId &&
        s_completedBgm.load(std::memory_order_relaxed) == bgmTrack;
    std::ostringstream receiptLog;
    receiptLog << "[HOTSWAP] completed Practice receipt "
               << (matches ? "accepted" : "rejected")
               << " generation=" << receiptGeneration
               << "/" << currentGeneration
               << " tuple=" << (generationMatches ? "checked" : "stale");
    LogOut(receiptLog.str(), true);
    return matches;
}

void InvalidateCompletedPracticeLoadReceipt() {
    InvalidateCompletedReceipt();
}

const char* GetDisplayNameForSelectId(int selectId) {
    return GetCharacterSelectName(selectId);
}

const char* GetResourceNameForSelectId(int selectId) {
    const char* resourceName = GetCharacterResourceName(selectId);
    return resourceName ? resourceName : "unknown";
}

int GetSelectIdForResourceName(const char* resourceName) {
    if (!resourceName || !resourceName[0]) {
        return -1;
    }

    for (int selectId = 0; selectId < kCharacterSelectCount; ++selectId) {
        const char* candidate = GetCharacterResourceName(selectId);
        if (candidate && _stricmp(candidate, resourceName) == 0) {
            return selectId;
        }
    }
    return -1;
}

bool QueueReload(int p1SelectId, int p2SelectId, int stageId, int bgmTrack) {
    PaletteSelection paletteSelection{};
    if (!ReadCurrentPaletteSelection(paletteSelection)) {
        LogOut("[HOTSWAP] failed to capture current palette selection; defaulting to slot 1 without custom palettes", true);
    }
    return QueueReload(p1SelectId, p2SelectId, stageId, paletteSelection, bgmTrack);
}

bool QueueReload(int p1SelectId, int p2SelectId, int stageId, const PaletteSelection& requestedPaletteSelection, int bgmTrack) {
    if (!IsValidSelectId(p1SelectId) || !IsValidSelectId(p2SelectId)
        || !IsValidStageId(stageId) || bgmTrack < -1 || bgmTrack > 0xFFFF) {
        FailRequest("menu selection out of range", GetCurrentGamePhase());
        return false;
    }
    if (IsNetplaySuspendActive() || IsNetplaySessionActive()) {
        FailRequest("reload is unavailable while netplay is active", GetCurrentGamePhase());
        return false;
    }
    if (DirectBootstrapOwnsTransaction()) {
        LogOut("[HOTSWAP] selector reload rejected until direct loader handoff is finalized", true);
        return false;
    }

    const RequestState state = s_state.load(std::memory_order_acquire);
    if (IsActiveState(state)) {
        LogOut("[HOTSWAP] request ignored because another reload is already active", true);
        return false;
    }

    const GamePhase currentPhase = GetCurrentGamePhase();
    const GameMode currentMode = GetCurrentGameMode();

    if (currentPhase == GamePhase::Match && !CanRequestFrontendExit(FrontendExitTarget::CharacterSelect)) {
        FailRequest("frontend exit to character select is unavailable", currentPhase);
        return false;
    }
    if (currentPhase != GamePhase::Match && currentPhase != GamePhase::CharacterSelect) {
        FailRequest("reload is only supported from match or character select", currentPhase);
        return false;
    }

    if(!CaptureQueuedBattleExit(currentPhase==GamePhase::Match))return false;
    PaletteSelection paletteSelection = requestedPaletteSelection;
    SanitizeRequestedPaletteSelection(p1SelectId, p2SelectId, paletteSelection);
    InvalidateCompletedReceipt();

    s_requestedP1Char.store(p1SelectId, std::memory_order_relaxed);
    s_requestedP2Char.store(p2SelectId, std::memory_order_relaxed);
    s_requestedP1Color.store(static_cast<int>(paletteSelection.p1Color), std::memory_order_relaxed);
    s_requestedP2Color.store(static_cast<int>(paletteSelection.p2Color), std::memory_order_relaxed);
    s_requestedP1CustomPalette.store(paletteSelection.p1UseCustomPalette ? 1 : 0, std::memory_order_relaxed);
    s_requestedP2CustomPalette.store(paletteSelection.p2UseCustomPalette ? 1 : 0, std::memory_order_relaxed);
    s_requestedStage.store(stageId, std::memory_order_relaxed);
    s_requestedBgmTrack.store(bgmTrack, std::memory_order_relaxed);
    s_selectorLoadingHandoffComplete.store(false, std::memory_order_release);
    s_directBootstrapState.store(DirectBootstrapState::Idle,
                                 std::memory_order_release);
    s_directAbortAfterHandoff.store(false, std::memory_order_release);

    PublishLoadingRequest();
    LogQueuedRequest(currentPhase, currentMode, p1SelectId, p2SelectId, stageId, bgmTrack, paletteSelection);

    if (currentPhase == GamePhase::Match) {
        SetState(RequestState::PendingExitRequest, UiStatus::Exiting, "leaving match for character select");
    } else {
        SetState(RequestState::PendingApply, UiStatus::Applying, "already in character select");
    }

    return true;
}

void OnSelectorReady(uintptr_t selectorContext) {
    EfzTmIdentityV1 identity{};
    if(!Practice::CaptureCurrentPracticeIdentity(identity) || !s_loadingRequest.Capture(identity))return;
    uintptr_t currentContext=0,gameSystem=0;
    if(!ResolveCharacterSelectContext(currentContext,gameSystem) || currentContext!=selectorContext)return;
    auto state=s_state.load(std::memory_order_acquire);
    if(state==RequestState::AwaitingCharacterSelect) {
        if(!s_state.compare_exchange_strong(state,RequestState::PendingApply,std::memory_order_acq_rel))return;
        state=RequestState::PendingApply;
    }
    if(state!=RequestState::PendingApply)return;
    ApplyQueuedSelections(selectorContext,gameSystem);
}

uint32_t CaptureInitializedRequest(const EfzTmIdentityV1& identity){return s_loadingRequest.CaptureInitialization(identity);}
void OnNativeFrontendInstalled(){s_nativeFrontendInstalled.store(true,std::memory_order_release);}

void OnBattleInitialized(const EfzTmIdentityV1& identity,uint32_t ticket,uintptr_t battleContext,uintptr_t gameSystem) {
    if(!ticket || s_loadingRequest.CaptureInitialization(identity)!=ticket || !battleContext || !gameSystem)return;
    uintptr_t actualSystem=0;
    if(!SafeReadMemory(battleContext+0x1c,&actualSystem,sizeof(actualSystem)) || actualSystem!=gameSystem)return;
    // Only the completed-init join authorizes completion; observing screen 3 or Loading AL3 alone is insufficient.
    CompleteReload(GamePhase::Match,CompletionReceiptAuthorized(),gameSystem);
}

void OnBattleFrontendEntry(uintptr_t battleContext) {
    auto expected=RequestState::PendingExitRequest;
    if(!s_state.compare_exchange_strong(expected,RequestState::IssuingExitRequest,std::memory_order_acq_rel))return;
    const bool direct=DirectBootstrapOwnsTransaction();
    const auto target=direct?FrontendExitTarget::Loading:FrontendExitTarget::CharacterSelect;
    const bool issued=battleContext==s_queuedExitBattle.load(std::memory_order_relaxed) &&
        RequestBattleFrontendExit(target,battleContext,s_queuedExitGameSystem.load(std::memory_order_relaxed));
    expected=RequestState::IssuingExitRequest;
    const auto next=issued?(direct?RequestState::AwaitingLoading:RequestState::AwaitingCharacterSelect):RequestState::Failed;
    if(s_state.compare_exchange_strong(expected,next,std::memory_order_acq_rel)) {
        s_uiStatus.store(issued?(direct?UiStatus::WaitingLoading:UiStatus::Exiting):UiStatus::Failed,std::memory_order_release);
        s_waitTicks.store(0,std::memory_order_relaxed);
    }
}

void Tick(GamePhase currentPhase, GameMode /*currentMode*/) {
    const RequestState state = s_state.load(std::memory_order_acquire);
    if(s_awaitNativeCompletion.load(std::memory_order_acquire) || state==RequestState::Completing)return;
    if (state == RequestState::Failed &&
        s_directAbortAfterHandoff.load(std::memory_order_acquire) &&
        DirectBootstrapHasBattleHandoff() &&
        currentPhase == GamePhase::Match) {
        LogOut("[HOTSWAP][DIRECT] canceled loader reached Battle; finalizing without completion receipt", true);
        CompleteReload(currentPhase, false);
        return;
    }
    if (!IsActiveState(state)) {
        if (state == RequestState::Idle) {
            s_lastActivePhase = GamePhase::Unknown;
        }
        return;
    }

    if (currentPhase != s_lastActivePhase) {
        std::ostringstream oss;
        oss << "[HOTSWAP] observed phase=" << PhaseName(currentPhase);
        LogOut(oss.str(), true);
        s_lastActivePhase = currentPhase;
    }

    const int waited = s_waitTicks.fetch_add(1, std::memory_order_relaxed) + 1;

    switch (state) {
        case RequestState::PendingExitRequest:
            if (currentPhase != GamePhase::Match) {
                if (DirectBootstrapOwnsTransaction()) {
                    SetState(RequestState::AwaitingLoading,
                             UiStatus::WaitingLoading,
                             "match already leaving for direct Loading");
                } else {
                    SetState(RequestState::AwaitingCharacterSelect,
                             UiStatus::Exiting,
                             "match already leaving");
                }
                return;
            }
            return;

        case RequestState::IssuingExitRequest:
            return;

        case RequestState::AwaitingCharacterSelect:
            if (currentPhase == GamePhase::CharacterSelect) {
                SetState(RequestState::PendingApply, UiStatus::Applying, "character select reached");
                return;
            }
            if (waited > kWaitExitTicks) {
                FailRequest("timed out waiting for character select", currentPhase);
            }
            return;

        case RequestState::PendingApply:
            if (!IsPracticeContext()) {
                FailRequest("left Practice before character select apply", currentPhase);
                return;
            }
            if (currentPhase != GamePhase::CharacterSelect) {
                if (waited > kWaitExitTicks) {
                    FailRequest("character select disappeared before apply", currentPhase);
                }
                return;
            }
            // The phase flips before CS finishes its +44 lifecycle init; EFZ resets +1182/+1183 and the selection timers then, so require two ready ticks before injecting.
            if (!CharacterSelectReadyForApply()) {
                s_characterSelectReadyTicks = 0;
                return;
            }
            ++s_characterSelectReadyTicks;
            if (s_characterSelectReadyTicks == 1) {
                LogOut("[HOTSWAP] character select lifecycle ready; waiting one stable tick before injection", true);
            }
            if (s_characterSelectReadyTicks < 2) {
                return;
            }
            ApplyQueuedSelections(0,0);
            return;

        case RequestState::AwaitingLoading:
            {
            using namespace CharacterHotswap::Transition;
            const ObservedPhase observed = currentPhase == GamePhase::CharacterSelect
                ? ObservedPhase::CharacterSelect
                : currentPhase == GamePhase::Loading ? ObservedPhase::Loading
                : currentPhase == GamePhase::Match ? ObservedPhase::Match
                : ObservedPhase::Other;
            const Decision decision = Decide(
                WaitState::AwaitingLoading, observed,
                DirectBootstrapOwnsTransaction(),
                AnyOwnedLoadingHandoffComplete());
            if (decision == Decision::DirectFallback) {
                s_selectorLoadingHandoffComplete.store(false,
                                                        std::memory_order_release);
                s_directBootstrapState.store(DirectBootstrapState::Idle,
                                             std::memory_order_release);
                s_directAbortAfterHandoff.store(false, std::memory_order_release);
                SetState(RequestState::PendingApply,
                         UiStatus::Applying,
                         "direct cleanup returned to Character Select; applying fallback");
                return;
            }
            if (decision == Decision::LoadingObserved) {
                SetState(RequestState::AwaitingMatch, UiStatus::WaitingMatch, "loading screen reached");
                return;
            }
            if (decision == Decision::Complete) {
                CompleteReload(currentPhase, CompletionReceiptAuthorized());
                return;
            }
            // A Match-origin request stays in the old Match during native cleanup; completion is legal only after observing Loading or a published handoff receipt.
            if (waited > kWaitLoadingTicks) {
                FailRequest("timed out waiting for loading", currentPhase);
            }
            return;
            }

        case RequestState::AwaitingMatch:
            if (CharacterHotswap::Transition::Decide(
                    CharacterHotswap::Transition::WaitState::AwaitingMatch,
                    currentPhase == GamePhase::Match
                        ? CharacterHotswap::Transition::ObservedPhase::Match
                        : CharacterHotswap::Transition::ObservedPhase::Other,
                    false,
                    AnyOwnedLoadingHandoffComplete()) ==
                CharacterHotswap::Transition::Decision::Complete) {
                CompleteReload(currentPhase, CompletionReceiptAuthorized());
                return;
            }
            if (waited > kWaitMatchTicks) {
                FailRequest("timed out waiting for match re-entry", currentPhase);
            }
            return;

        case RequestState::Idle:
        case RequestState::Failed:
        default:
            return;
    }
}

bool ReadCurrentPaletteSelection(PaletteSelection& outSelection) {
    const GamePhase phase = GetCurrentGamePhase();
    const bool ok = CaptureCurrentPaletteSelection(phase, outSelection);
    static bool s_lastOk = true;
    static GamePhase s_lastPhase = GamePhase::Unknown;
    if (!ok && (s_lastOk || s_lastPhase != phase)) {
        LogOut(std::string("[HOTSWAP][PAL][READ] failed phase=") + PhaseName(phase), true);
    }
    s_lastOk = ok;
    s_lastPhase = phase;
    return ok;
}

bool HasCustomPaletteFile(int selectId, int paletteIndex) {
    const CustomPaletteLookupResult lookup = LookupCharacterCustomPalette(selectId, paletteIndex);
    LogCustomPaletteLookupIfChanged("HAS", selectId, paletteIndex, lookup);
    return lookup.exists;
}

void SanitizePaletteSelection(int p1SelectId, int p2SelectId, PaletteSelection& selection) {
    SanitizeRequestedPaletteSelection(p1SelectId, p2SelectId, selection);
}

void InvalidateCustomPaletteCache() {
    InvalidatePaletteProbeCacheImpl();
}

bool IsBusy() {
    return IsActiveState(s_state.load(std::memory_order_acquire)) ||
           DirectBootstrapOwnsTransaction();
}

bool CanQueueReload() {
    if (IsNetplaySuspendActive() || IsNetplaySessionActive()) {
        return false;
    }
    if (IsBusy()) {
        return false;
    }

    const GamePhase phase = GetCurrentGamePhase();
    if (phase == GamePhase::CharacterSelect) {
        return true;
    }
    if (phase == GamePhase::Match) {
        return CanRequestFrontendExit(FrontendExitTarget::CharacterSelect);
    }
    return false;
}

const char* GetActionValueText() {
    const RequestState state = s_state.load(std::memory_order_acquire);
    const UiStatus status = s_uiStatus.load(std::memory_order_acquire);
    if (IsActiveState(state)) {
        return StatusText(status);
    }
    if (status == UiStatus::Failed || status == UiStatus::Completed) {
        return StatusText(status);
    }

    const GamePhase phase = GetCurrentGamePhase();
    if (phase == GamePhase::CharacterSelect) {
        return "READY";
    }
    if (phase == GamePhase::Match) {
        return CanRequestFrontendExit(FrontendExitTarget::CharacterSelect) ? "READY" : "MATCH ONLY";
    }
    return "MATCH/CS ONLY";
}

} // namespace CharacterHotswap
