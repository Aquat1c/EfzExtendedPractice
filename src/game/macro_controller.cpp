#include "../include/game/macro_controller.h"
#include "../include/core/logger.h"
#include "../include/core/memory.h"
#include "../include/core/constants.h"
#include "../include/game/game_state.h"
#include "../include/gui/overlay.h"
#include "../include/input/immediate_input.h"
#include "../include/input/input_core.h"      // GetPlayerPointer
#include "../include/input/input_buffer.h"    // INPUT_BUFFER_* constants
#include "../include/input/injection_control.h" // g_forceBypass
#include "../include/input/input_motion.h"      // g_manualInputOverride/g_manualInputMask
#include "../include/input/input_hook.h"        // playback poll-consumption audit
#include "../include/input/auto_action_motion_transaction.h"
#include "../include/input/scoped_input_reservation.h"
#include "../include/game/auto_action.h" // Enable/Restore P2 control helpers
#include "../include/utils/switch_players.h"
#include "../include/utils/pause_integration.h"
#include "../include/game/practice_offsets.h"
#include "../include/utils/utilities.h"   // GetEFZBase, IsEFZWindowActive, etc.
#include "../include/game/frame_monitor.h" // AreCharactersInitialized()
#include "../include/runtime/practice_runtime.h"
#include "../include/runtime/practice_session.h"
#include <vector>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <algorithm>
#include <mutex>
#include <cassert>

// Forward decls in case headers aren't visible due to include order in some TU configs
extern uintptr_t GetEFZBase();
bool AreCharactersInitialized();

// From auto_action.cpp: wake-macro restore + completion flags.
extern void RestoreP2ControlFlagsPreserveBufferAndTokenForMacro();
extern std::atomic<bool> g_macroWakePreserveBuffer;
extern std::atomic<int> g_macroWakePreservePlayer;
extern std::atomic<int> g_wakeMacroPlaybackCompleted;

namespace {
    using Mask = uint8_t;
    struct RLESpan { Mask mask; Mask buf; int ticks; int8_t facing; };
    struct Slot {
        std::vector<RLESpan> spans; // RLE of immediate+buf at 64 Hz
        // one byte per 64 Hz frame; authoritative playback stream
        std::vector<uint8_t> macroStream;
        std::vector<uint8_t> bufStream; // full circular buffer stream captured during recording
        // engine buffer writes observed per recorder tick (not 1:1 with ticks)
        std::vector<uint16_t> bufCountsPerTick;
        // Snapshot of the engine's buffer index each 64 Hz recorder tick
        std::vector<uint16_t> bufIndexPerTick;
        // Optional diagnostics: reason code per tick (U=unfrozen frameDiv, B=frozen buf advance, S=frozen step, X=frozen both)
        std::vector<char> tickReason;
        // Raw immediate mask sampled at start of each recorder tick BEFORE any merging / union logic.
        std::vector<uint8_t> immPerTick;
        // Latest buffer entry value (ring[idx-1]) as observed this tick BEFORE any synthetic write insertion.
        std::vector<uint8_t> bufLatestPerTick;
        // Optional: full snapshot of the entire input buffer ring each tick (heavy, gated by constant below).
        std::vector<std::vector<uint8_t>> fullBufferSnapshots;
        uint16_t bufStartIdx = 0; // buffer index at recording start
        uint16_t bufEndIdx = 0;   // buffer index at recording end
        bool hasData = false;
    };

    constexpr int kMaxSlots = 8; // simple ring of slots
    constexpr int kBannerX = 20;
    constexpr int kBannerY = 160;
    std::atomic<MacroController::State> s_state{ MacroController::State::Idle };
    EfzTmEntryV1 s_boundWorld{}; // protected by the input/control barrier
    bool s_scopeRequired = false;
    std::atomic<bool> s_practiceWorkClosed{false};
    uintptr_t s_activePlayer = 0; // captured on admission, never a last-player hint

    bool SameMacroWorld(const EfzTmEntryV1& a, const EfzTmEntryV1& b) {
        return a.id.providerIncarnation == b.id.providerIncarnation &&
            a.id.practiceSession == b.id.practiceSession &&
            a.id.battleWorld == b.id.battleWorld && a.id.timeline == b.id.timeline &&
            a.gameSystem == b.gameSystem && a.battleContext == b.battleContext &&
            a.player1 == b.player1 && a.player2 == b.player2 &&
            a.nativeThreadId == b.nativeThreadId;
    }
    class MacroWork {
        Practice::WorkLease lease_;
        bool admitted_;
    public:
        MacroWork() : admitted_(!s_scopeRequired) {
            if (!s_scopeRequired || s_practiceWorkClosed.load(std::memory_order_acquire)) return;
            lease_ = Practice::TryEnterMonitorWork();
            admitted_ = lease_ && SameMacroWorld(lease_.Context(), s_boundWorld);
        }
        explicit operator bool() const { return admitted_; }
    };
    uintptr_t MacroPlayerPointer(int player) {
        if (!s_scopeRequired) return GetPlayerPointer(player);
        return player == 1 ? s_boundWorld.player1 : player == 2 ? s_boundWorld.player2 : 0;
    }
    std::atomic<bool> s_recordingCaptureSuspended{false};
    Slot s_slots[kMaxSlots];
    Slot s_transientPlaybackSlot; // mission demos never overwrite a user slot
    Slot s_transientRecordingSlot; // mission authoring never overwrites a user slot
    std::atomic<bool> s_useTransientPlayback{false};
    std::atomic<bool> s_useTransientRecording{false};
    std::atomic<int> s_curSlot{1}; // 1-based
    std::atomic<int> s_recordPlayer{2};
    std::atomic<bool> s_recordStopRequested{false};
    std::atomic<bool> s_recordTickAdvanced{false};
    bool s_recordOwnsP2Control = false;
    std::atomic<int> s_playPlayer{2};
    std::atomic<bool> s_exclusiveReplay{false};
    bool s_recordSwitchesLocalControl = false;
    // Monitor-thread ticks vs input-thread stop requests; the finalization gate keeps the sealed stream stable.
    std::mutex s_recordMutex;
    bool s_recordFinalizing = false;
    // Remember local side to restore after recording
    std::atomic<int> s_prevLocalSide{-1}; // -1 means unknown
    int s_macroBannerId = -1;

    int s_recLastMask = 0;
    int s_recSpanTicks = 0;    // ticks at 64 Hz-equivalent logical rate
    int s_recLastBuf = 0;      // last buffer mask captured
    int s_recPrevBufIdx = -1;  // previous observed circular buffer index
    int s_recLastFacing = 0;   // -1 left, +1 right, 0 unknown
    char s_recLastReason = '?'; // reason code for current span
    size_t s_playIndex = 0;
    int s_playSpanRemaining = 0;
    // Stream playback cursor (EfzRevival-style byte-per-frame macro buffer)
    size_t s_playStreamIndex = 0;
    // Cursor into recorded raw buffer stream for playback (writes per tick using counts-per-tick)
    size_t s_playBufStreamIndex = 0;
    std::vector<int8_t> s_streamFacingPerTick;
    // Legacy, unused: superseded by the per-tick queue.
    std::vector<uint8_t> s_bufWriteQueue; // legacy, unused
    size_t s_bufQueueHead = 0;
    // Per-tick queue: N writes spread across the 3 subframes.
    std::vector<uint8_t> s_tickBufQueue;
    size_t s_tickBufHead = 0;
    uint16_t s_writesLeftThisTick = 0; // how many buffer writes remain to issue in the current 64 Hz tick
    // Finish: inject neutral (poll override = 0) so the engine writes neutral into history.
    bool s_finishing = false;
    int  s_finishNeutralFrames = 0; // number of neutral frames to inject for a full clear tick (3 subframes)
    bool s_finishPendingClearTick = false; // wait until next tick boundary (s_frameDiv==0) to start neutral clear tick
    // Guard window: let a detected command commit to a MoveID before restoring control.
    bool s_finishGuardActive = false;
    int  s_finishGuardFramesLeft = 0;  // internal frames (192 Hz)
    uint16_t s_finishGuardStartMoveId = 0;
    // Final byte must survive one engine poll, or the same Tick replaces it with neutral.
    bool s_endStreamPending = false;
    uint32_t s_endStreamPollHitsAtLatch = 0;
    int s_endStreamWaitFrames = 0;
    // KO/round transitions can stall the input ring; bound both waits.
    int s_playNoProgressFrames = 0;
    // Baseline immediate mask for the current 64 Hz tick
    uint8_t s_baselineMask = 0;
    // Playback synchronization: track last seen buffer index to detect when engine advances
    uint16_t s_lastSeenBufIdx = 0xFFFF;  // Last buffer index we observed
    bool s_playbackBufIdxSyncInitialized = false;
    bool s_playbackPollAuditLogged = false;
    bool s_primedTickAwaitingFirstPoll = false;
    // Tick zero is prepared while the mission transition still freezes the world.
    bool s_primedTickDrainPending = false;
    int s_primedTickSubframesRemaining = 0;
    uint32_t s_primedTickPollHitsAtLastPublish = 0;
    uint32_t s_primedTickPollHitsAtLatch = 0;

    bool s_logPrevMoveIdInit = false;
    uint16_t s_logPrevMoveId = 0;

    // Track if we've seen the first attack button in the current playback
    bool s_firstAttackSeen = false;

    // Progress pacing: we step at the 64 Hz ImmediateInput cadence by counting internal frames (192 Hz)
    int s_frameDiv = 0; // 0..2 cycles; advance when hits 0
    // Diagnostics: detect abnormal cycle lengths (should always be 3 calls between div0 events if Tick called once per internal frame)
    static int s_callsSinceDiv0 = 0;
    static bool s_firstDiv0Seen = false;
    struct FreezeObservation {
        bool initialized = false;
        bool lastFrozen = false;
    } s_freezeObservation;

    constexpr bool kEnableFullBufferSnapshots = false; // set false if memory/log size becomes an issue

    // Frozen when: not in Match, Practice pause flag, or gamespeed==0. Do NOT
    // require both pause+speed: patch-based pauses and emergency freezes set one only.
    bool ReadGamespeedFrozen() {
        const GamePhase phase = GetCurrentGamePhase();
        bool phaseFrozen = (phase != GamePhase::Match);
        bool pauseFlag  = PauseIntegration::IsPracticePaused();
        bool speedFrozen = PauseIntegration::IsGameSpeedFrozen();
        bool frozen = phaseFrozen || pauseFlag || speedFrozen;
        if (!s_freezeObservation.initialized) {
            s_freezeObservation = {true, frozen};
        }
        if (frozen != s_freezeObservation.lastFrozen) {
            std::ostringstream oss;
            oss << "[MACRO][FRZ] " << (frozen ? "ENTER" : "EXIT")
                << " freeze phase=" << (int)phase
                << " phaseFrozen=" << (phaseFrozen?1:0)
                << " practicePause=" << (pauseFlag?1:0)
                << " gamespeedFrozen=" << (speedFrozen?1:0);
            LogOut(oss.str(), true);
            s_freezeObservation.lastFrozen = frozen;
        }
        return frozen;
    }

    inline int ClampSlot(int s){ if (s < 1) return 1; if (s > kMaxSlots) return kMaxSlots; return s; }
    inline int ClampPlayer(int p){ return p == 1 ? 1 : 2; }

    Slot& ActivePlaybackSlot() {
        return s_useTransientPlayback.load(std::memory_order_acquire)
            ? s_transientPlaybackSlot
            : s_slots[ClampSlot(s_curSlot.load()) - 1];
    }

    Slot& ActiveRecordingSlot() {
        return s_useTransientRecording.load(std::memory_order_acquire)
            ? s_transientRecordingSlot
            : s_slots[ClampSlot(s_curSlot.load()) - 1];
    }

    static std::string ToHexString(int value, int width = 2) {
        std::ostringstream oss;
        oss << std::hex << std::uppercase << std::setfill('0') << std::setw(width) << value;
        return oss.str();
    }

    static bool IsActivationMove(uint16_t mv) {
        if (mv >= 300) return true;           // supers
        if (mv >= 250) return true;           // specials and Kaori forward dash start (250)
        if (mv == GROUND_FORWARD_DASH_ID || mv == GROUND_BACKWARD_DASH_ID ||
            mv == AIR_FORWARD_DASH_ID || mv == AIR_BACKWARD_DASH_ID) return true;
        return false;
    }

    static char DirMaskToNumpad(uint8_t m) {
        bool u = (m & GAME_INPUT_UP) != 0;
        bool d = (m & GAME_INPUT_DOWN) != 0;
        bool l = (m & GAME_INPUT_LEFT) != 0;
        bool r = (m & GAME_INPUT_RIGHT) != 0;
        // Resolve invalid combos by neutral (5)
        if ((u && d) || (l && r)) return '5';
        if (u && r) return '9';
        if (u && l) return '7';
        if (d && r) return '3';
        if (d && l) return '1';
        if (u) return '8';
        if (d) return '2';
        if (r) return '6';
        if (l) return '4';
        return '5';
    }

    static uint8_t NumpadCharToDirMask(char c) {
        switch (c) {
            case '1': return (GAME_INPUT_DOWN | GAME_INPUT_LEFT);
            case '2': return (GAME_INPUT_DOWN);
            case '3': return (GAME_INPUT_DOWN | GAME_INPUT_RIGHT);
            case '4': return (GAME_INPUT_LEFT);
            case '5': return 0;
            case '6': return (GAME_INPUT_RIGHT);
            case '7': return (GAME_INPUT_UP | GAME_INPUT_LEFT);
            case '8': return (GAME_INPUT_UP);
            case '9': return (GAME_INPUT_UP | GAME_INPUT_RIGHT);
            case 'N': return 0; // alias
            default:  return 0xFF; // invalid sentinel
        }
    }

    static std::string MaskToToken(uint8_t m) {
        uint8_t dir = m & (GAME_INPUT_UP | GAME_INPUT_DOWN | GAME_INPUT_LEFT | GAME_INPUT_RIGHT);
        std::string t;
        t.push_back(DirMaskToNumpad(dir));
        // Append buttons in A..D order
        if (m & GAME_INPUT_A) t.push_back('A');
        if (m & GAME_INPUT_B) t.push_back('B');
        if (m & GAME_INPUT_C) t.push_back('C');
        if (m & GAME_INPUT_D) t.push_back('D');
        return t;
    }

    static bool TryTokenToMask(const std::string& tok, uint8_t& outMask) {
        if (tok.empty()) return false;
        // Accept 'N' or 'n' as neutral
        if (tok.size() == 1 && (tok[0] == 'N' || tok[0] == 'n')) { outMask = 0; return true; }
        char d = tok[0];
        if (d >= 'a' && d <= 'z') d = (char)std::toupper((unsigned char)d);
        // Bare button tokens ("A", "BC") = neutral direction (legacy mission data).
        const bool buttonOnly = (d == 'A' || d == 'B' || d == 'C' || d == 'D');
        uint8_t dir = buttonOnly ? 0 : NumpadCharToDirMask(d);
        if (dir == 0xFF) return false;
        uint8_t btn = 0;
        for (size_t i = buttonOnly ? 0 : 1; i < tok.size(); ++i) {
            char c = tok[i];
            if (c >= 'a' && c <= 'z') c = (char)std::toupper((unsigned char)c);
            if (c == 'A') btn |= GAME_INPUT_A;
            else if (c == 'B') btn |= GAME_INPUT_B;
            else if (c == 'C') btn |= GAME_INPUT_C;
            else if (c == 'D') btn |= GAME_INPUT_D;
            else return false; // unexpected char
        }
        outMask = (dir | btn);
        return true;
    }

    static std::string MaskToButtons(Mask m) {
        // Use unified GAME_INPUT_* flags (input_core.h)
        std::string out;
        auto add = [&](const char* t){ if (!out.empty()) out += ' '; out += t; };
        if (m & GAME_INPUT_UP) add("U");
        if (m & GAME_INPUT_DOWN) add("D");
        if (m & GAME_INPUT_LEFT) add("L");
        if (m & GAME_INPUT_RIGHT) add("R");
        if (m & GAME_INPUT_A) add("A");
        if (m & GAME_INPUT_B) add("B");
        if (m & GAME_INPUT_C) add("C");
        if (m & GAME_INPUT_D) add("D");
        if (out.empty()) out = "<neutral>";
        return out;
    }

    static void LogVectorHex(const char* label, int slot, const char* phase, const std::vector<uint8_t>& v, size_t perLine = 32) {
        std::ostringstream line;
        line << "[MACRO][DUMP] slot=" << slot << " " << phase << " " << label << " (" << v.size() << "):";
        LogOut(line.str(), true);
        line.str(""); line.clear();
        size_t count = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            if (count == 0) {
                line << "[MACRO][DUMP]   ";
            }
            line << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)v[i];
            if (i + 1 < v.size()) line << ' ';
            if (++count >= perLine) {
                LogOut(line.str(), true);
                line.str(""); line.clear();
                count = 0;
            }
        }
        if (count > 0) {
            LogOut(line.str(), true);
        }
    }

    static void LogVectorU16(const char* label, int slot, const char* phase, const std::vector<uint16_t>& v, size_t perLine = 32) {
        std::ostringstream line;
        line << "[MACRO][DUMP] slot=" << slot << " " << phase << " " << label << " (" << v.size() << "):";
        LogOut(line.str(), true);
        line.str(""); line.clear();
        size_t count = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            if (count == 0) {
                line << "[MACRO][DUMP]   ";
            }
            line << v[i];
            if (i + 1 < v.size()) line << ' ';
            if (++count >= perLine) {
                LogOut(line.str(), true);
                line.str(""); line.clear();
                count = 0;
            }
        }
        if (count > 0) {
            LogOut(line.str(), true);
        }
    }

    static void LogTickReasons(const char* phase, int slot, const std::vector<char>& reasons) {
        std::ostringstream line;
        line << "[MACRO][DUMP] slot=" << slot << " " << phase << " tickReason (" << reasons.size() << "):";
        LogOut(line.str(), true);
        line.str(""); line.clear();
        size_t perLine = 64; size_t count = 0;
        for (size_t i = 0; i < reasons.size(); ++i) {
            if (count == 0) line << "[MACRO][DUMP]   ";
            line << reasons[i];
            if (i + 1 < reasons.size()) line << ' ';
            if (++count >= perLine) {
                LogOut(line.str(), true);
                line.str(""); line.clear();
                count = 0;
            }
        }
        if (count > 0) LogOut(line.str(), true);
    }

    static void LogPerTickOverview(int slot, const char* phase, const std::vector<uint8_t>& macroStream,
                                   const std::vector<uint16_t>& bufCountsPerTick, const std::vector<uint16_t>& bufIndexPerTick) {
        const size_t ticks = macroStream.size();
        std::ostringstream hdr;
        hdr << "[MACRO][DUMP] slot=" << slot << " " << phase << " per-tick overview (" << ticks << "):";
        LogOut(hdr.str(), true);
        // To avoid excessively huge logs, cap detailed per-tick lines to 512 ticks.
        constexpr size_t kDetailCap = 512;
        size_t lim = ticks < kDetailCap ? ticks : kDetailCap;
        for (size_t i = 0; i < lim; ++i) {
            uint8_t m = macroStream[i];
            uint16_t c = (i < bufCountsPerTick.size()) ? bufCountsPerTick[i] : 0;
            uint16_t idx = (i < bufIndexPerTick.size()) ? bufIndexPerTick[i] : 0xFFFF;
            std::ostringstream ln;
            ln << "[MACRO][DUMP]   t=" << i
               << " macro=0x" << ToHexString((int)m, 2)
               << " (" << MaskToButtons(m) << ")"
               << " bufCount=" << c
               << " bufIdx=" << idx;
            LogOut(ln.str(), true);
        }
        if (ticks > kDetailCap) {
            std::ostringstream note;
            note << "[MACRO][DUMP]   (" << (ticks - kDetailCap) << " more ticks omitted from per-tick lines; see full streams above)";
            LogOut(note.str(), true);
        }
    }

    static int ReadFacingSign(int playerNum) {
        uintptr_t pPtr = MacroPlayerPointer(playerNum);
        if (!pPtr) return 0;
        uint8_t raw = 0;
        if (!SafeReadMemory(pPtr + FACING_DIRECTION_OFFSET, &raw, sizeof(raw))) return 0;
        if (raw == 1) return +1; // facing right
        if (raw == 255) return -1; // facing left
        return 0;
    }

    static Mask FlipMaskHoriz(Mask m) {
        // Swap left/right, preserve up/down and buttons
        bool left  = (m & GAME_INPUT_LEFT)  != 0;
        bool right = (m & GAME_INPUT_RIGHT) != 0;
        Mask out = m;
        out &= ~(GAME_INPUT_LEFT | GAME_INPUT_RIGHT);
        if (left)  out |= GAME_INPUT_RIGHT;
        if (right) out |= GAME_INPUT_LEFT;
        return out;
    }

    void LogPlayerBufferSnapshot(int playerNum, const char* label, int tail = 16) {
        const int player = ClampPlayer(playerNum);
        uintptr_t playerPtr = MacroPlayerPointer(player);
        if (!playerPtr) { LogOut(std::string("[MACRO][BUF] ") + label + ": P" + std::to_string(player) + " ptr null", true); return; }
        uint16_t idx = 0;
        if (!SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &idx, sizeof(idx))) {
            LogOut(std::string("[MACRO][BUF] ") + label + ": index read failed", true);
            return;
        }
        if (tail < 1) tail = 1; if (tail > (int)INPUT_BUFFER_SIZE) tail = (int)INPUT_BUFFER_SIZE;
        std::vector<uint8_t> vals(tail, 0);
        for (int i = 0; i < tail; ++i) {
            int w = ((int)idx - (tail - 1 - i));
            w %= (int)INPUT_BUFFER_SIZE; if (w < 0) w += (int)INPUT_BUFFER_SIZE;
            uint8_t v = 0; SafeReadMemory(playerPtr + INPUT_BUFFER_OFFSET + (uintptr_t)w, &v, sizeof(v));
            vals[i] = v;
        }
        std::ostringstream ss; ss << "[MACRO][BUF] " << label << ": P" << player << " idx=" << idx << " tail=" << tail << " vals=";
        for (int i = 0; i < tail; ++i) {
            ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)vals[i];
            if (i + 1 < tail) ss << ' ';
        }
        LogOut(ss.str(), true);
    }

    void LogPlayerImmediateSnapshot(int playerNum, const char* label) {
        const int player = ClampPlayer(playerNum);
        uintptr_t playerPtr = MacroPlayerPointer(player);
        if (!playerPtr) { LogOut(std::string("[MACRO][IMM] ") + label + ": P" + std::to_string(player) + " ptr null", true); return; }
        uint8_t h=0, v=0, a=0, b=0, c=0, d=0;
        SafeReadMemory(playerPtr + INPUT_HORIZONTAL_OFFSET, &h, sizeof(h));
        SafeReadMemory(playerPtr + INPUT_VERTICAL_OFFSET, &v, sizeof(v));
        SafeReadMemory(playerPtr + INPUT_BUTTON_A_OFFSET, &a, sizeof(a));
        SafeReadMemory(playerPtr + INPUT_BUTTON_B_OFFSET, &b, sizeof(b));
        SafeReadMemory(playerPtr + INPUT_BUTTON_C_OFFSET, &c, sizeof(c));
        SafeReadMemory(playerPtr + INPUT_BUTTON_D_OFFSET, &d, sizeof(d));
        std::ostringstream ss;
        ss << "[MACRO][IMM] " << label << ": P" << player << " H=" << (int)h << " V=" << (int)v
           << " A=" << (int)a << " B=" << (int)b << " C=" << (int)c << " D=" << (int)d;
        LogOut(ss.str(), true);
    }

    void ResetPlaybackCursor() {
        s_playIndex = 0; s_playSpanRemaining = 0; s_playStreamIndex = 0; s_playBufStreamIndex = 0; s_frameDiv = 0;
        s_callsSinceDiv0 = 0; s_firstDiv0Seen = false;
        s_streamFacingPerTick.clear();
        s_bufWriteQueue.clear(); s_bufQueueHead = 0; s_baselineMask = 0;
        s_tickBufQueue.clear(); s_tickBufHead = 0; s_writesLeftThisTick = 0;
        s_finishing = false; s_finishNeutralFrames = 0; s_finishPendingClearTick = false;
        s_finishGuardActive = false; s_finishGuardFramesLeft = 0; s_finishGuardStartMoveId = 0;
        s_endStreamPending = false; s_endStreamPollHitsAtLatch = 0;
        s_endStreamWaitFrames = 0; s_playNoProgressFrames = 0;
        s_lastSeenBufIdx = 0xFFFF; s_playbackBufIdxSyncInitialized = false;
        s_playbackPollAuditLogged = false;
        s_primedTickAwaitingFirstPoll = false;
        s_primedTickDrainPending = false;
        s_primedTickSubframesRemaining = 0;
        s_primedTickPollHitsAtLastPublish = 0;
        s_primedTickPollHitsAtLatch = 0;
        s_logPrevMoveIdInit = false; s_logPrevMoveId = 0;
        s_firstAttackSeen = false;
    }

    struct PreparedStreamTick {
        size_t tick = 0;
        uint8_t mask = 0;
        bool snapshotRebasedBufferIndex = false;
    };

    // Single load path for ordinary advancement and frame-zero admission.
    bool PrepareNextStreamTick(Slot& slot, int playPlayer,
                               uintptr_t playerPtr,
                               PreparedStreamTick* preparedOut = nullptr,
                               bool applyFullSnapshot = true) {
        if (s_playStreamIndex >= slot.macroStream.size()) return false;

        const size_t tick = s_playStreamIndex;
        uint8_t mask = slot.macroStream[tick];
        int8_t recordedFacing = 0;
        if (tick < s_streamFacingPerTick.size()) {
            recordedFacing = s_streamFacingPerTick[tick];
        }
        if (recordedFacing == 0) recordedFacing = +1;

        const int currentFacing = ReadFacingSign(playPlayer);
        if (currentFacing != 0 && recordedFacing != currentFacing) {
            mask = FlipMaskHoriz(mask);
        }
        s_baselineMask = mask;

        bool snapshotRebasedBufferIndex = false;
        if (applyFullSnapshot && kEnableFullBufferSnapshots &&
            tick < slot.fullBufferSnapshots.size() &&
            !slot.fullBufferSnapshots[tick].empty()) {
            const auto& snapshot = slot.fullBufferSnapshots[tick];
            uint16_t recordedBufferIndex = 0;
            if (tick < slot.bufIndexPerTick.size()) {
                recordedBufferIndex = slot.bufIndexPerTick[tick];
            }

            if (playerPtr && snapshot.size() == INPUT_BUFFER_SIZE) {
                std::vector<uint8_t> adjustedSnapshot = snapshot;
                if (currentFacing != 0 && recordedFacing != currentFacing) {
                    for (uint8_t& value : adjustedSnapshot) {
                        value = FlipMaskHoriz(value);
                    }
                }
                SafeWriteMemory(playerPtr + INPUT_BUFFER_OFFSET,
                                adjustedSnapshot.data(), INPUT_BUFFER_SIZE);
                SafeWriteMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET,
                                &recordedBufferIndex,
                                sizeof(recordedBufferIndex));
                s_lastSeenBufIdx = recordedBufferIndex;
                snapshotRebasedBufferIndex = true;
            }
        }

        s_tickBufQueue.clear();
        s_tickBufHead = 0;
        s_writesLeftThisTick = 0;
        if (tick < slot.bufCountsPerTick.size()) {
            const uint16_t writesThisTick = slot.bufCountsPerTick[tick];
            s_writesLeftThisTick = writesThisTick;
            for (uint16_t i = 0;
                 i < writesThisTick && s_playBufStreamIndex < slot.bufStream.size();
                 ++i) {
                uint8_t raw = slot.bufStream[s_playBufStreamIndex++];
                if (currentFacing != 0 && recordedFacing != currentFacing) {
                    raw = FlipMaskHoriz(raw);
                }
                s_tickBufQueue.push_back(raw);
            }
        }

        ++s_playStreamIndex;
        if (preparedOut) {
            preparedOut->tick = tick;
            preparedOut->mask = mask;
            preparedOut->snapshotRebasedBufferIndex =
                snapshotRebasedBufferIndex;
        }
        return true;
    }

    struct PublishedStreamFrame {
        uint8_t mask = 0;
        bool endLatched = false;
    };

    // Callable during StartPlayback: tick 0 must be authoritative before the world is released.
    PublishedStreamFrame PublishPreparedStreamFrame(const Slot& slot,
                                                    int playPlayer,
                                                    int bufferSubframesLeft,
                                                    bool allowEndLatch = true) {
        constexpr uint8_t kDirectionMask =
            GAME_INPUT_UP | GAME_INPUT_DOWN |
            GAME_INPUT_LEFT | GAME_INPUT_RIGHT;
        constexpr uint8_t kButtonMask =
            GAME_INPUT_A | GAME_INPUT_B | GAME_INPUT_C | GAME_INPUT_D;

        uint8_t frameMask = s_baselineMask;
        const int writesToDo =
            MacroController::PlaybackStartPolicy::WritesForSubframe(
                static_cast<int>(s_writesLeftThisTick),
                bufferSubframesLeft);
        for (int write = 0;
             write < writesToDo && s_tickBufHead < s_tickBufQueue.size();
             ++write) {
            const uint8_t raw = s_tickBufQueue[s_tickBufHead++];
            if (s_writesLeftThisTick > 0) --s_writesLeftThisTick;
            frameMask = MacroController::PlaybackStartPolicy::
                PublishMonitorSlice(s_baselineMask, raw,
                                    kDirectionMask, kButtonMask);
        }
        // A trailing monitor-time raw neutral must not erase one-frame attacks.

        bool suppressImmediate = false;
        if (g_macroWakePreserveBuffer.load()) {
            const bool hasButtons = (frameMask & kButtonMask) != 0;
            if (!s_firstAttackSeen) {
                if (hasButtons) s_firstAttackSeen = true;
                else suppressImmediate = true;
            }
        }

        g_pollOverrideMask[playPlayer].store(frameMask,
                                             std::memory_order_relaxed);
        g_pollOverrideActive[playPlayer].store(true,
                                               std::memory_order_release);
        g_forceBypass[playPlayer].store(false, std::memory_order_relaxed);
        if (suppressImmediate) {
            g_manualInputMask[playPlayer].store(0,
                                                std::memory_order_relaxed);
            g_manualInputOverride[playPlayer].store(
                true, std::memory_order_relaxed);
        } else {
            g_manualInputOverride[playPlayer].store(
                false, std::memory_order_relaxed);
        }
        g_injectImmediateOnly[playPlayer].store(false,
                                                std::memory_order_relaxed);

        const bool endLatched = allowEndLatch &&
            MacroController::PlaybackStartPolicy::StreamEndCanLatch(
                static_cast<int>(s_playStreamIndex),
                static_cast<int>(slot.macroStream.size()),
                static_cast<int>(s_tickBufQueue.size() -
                                 (std::min)(s_tickBufHead,
                                            s_tickBufQueue.size())),
                static_cast<int>(s_writesLeftThisTick));
        if (endLatched) {
            s_endStreamPending = true;
            s_endStreamPollHitsAtLatch =
                GetInputPollOverrideHitCount(playPlayer);
            s_endStreamWaitFrames = 0;
        }
        return {frameMask, endLatched};
    }

    void ClearPlaybackLane(int playerNum, bool clearImmediateMemory) {
        const int player = ClampPlayer(playerNum);
        if (clearImmediateMemory) ImmediateInput::Clear(player);
        g_forceBypass[player].store(false, std::memory_order_release);
        g_injectImmediateOnly[player].store(false, std::memory_order_release);
        g_manualInputOverride[player].store(false, std::memory_order_release);
        g_pollOverrideActive[player].store(false, std::memory_order_release);
        g_pollOverrideMask[player].store(0, std::memory_order_release);
    }

    void FinishRecording(bool captureUnobservedTail = true,
                         Slot* sealedCopy = nullptr, bool retiring = false) {
        std::unique_lock<std::mutex> recordLock(s_recordMutex);
        if (s_state.load(std::memory_order_acquire) != MacroController::State::Recording
            || s_recordFinalizing) {
            return;
        }
        s_recordFinalizing = true;
        const bool embeddedMissionCapture =
            s_useTransientRecording.load(std::memory_order_acquire);
        Slot& recordSlot = ActiveRecordingSlot();

        // Flush any pending span
        if (s_recSpanTicks > 0) {
            recordSlot.spans.push_back({ static_cast<Mask>(s_recLastMask & 0xFF), static_cast<Mask>(s_recLastBuf & 0xFF), s_recSpanTicks, (int8_t)s_recLastFacing });
            recordSlot.hasData = !recordSlot.spans.empty();
            LogOut(std::string("[MACRO][REC] span imm=0x") + ToHexString((int)(s_recLastMask & 0xFF), 2) +
                   " (" + MaskToButtons((Mask)(s_recLastMask & 0xFF)) + ") buf=0x" + ToHexString((int)(s_recLastBuf & 0xFF), 2) +
                   " (" + MaskToButtons((Mask)(s_recLastBuf & 0xFF)) + ") ticks=" + std::to_string(s_recSpanTicks) +
                   " facing=" + std::to_string(s_recLastFacing), true);
        }
        // Capture any remaining buffer entries up to current index at finish (diagnostic stream for engine buffer)
        const int recordPlayer = ClampPlayer(s_recordPlayer.load(std::memory_order_acquire));
        uintptr_t playerPtr = retiring ? 0 : MacroPlayerPointer(recordPlayer);
        uint16_t endIdx = 0;
        if (captureUnobservedTail && playerPtr &&
            SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &endIdx, sizeof(endIdx))) {
            // Append entries from s_recPrevBufIdx -> endIdx (exclusive of endIdx, inclusive start)
            if (s_recPrevBufIdx >= 0) {
                size_t beforeSize = recordSlot.bufStream.size();
                int cur = s_recPrevBufIdx;
                while (cur != endIdx) {
                    uint8_t v = 0;
                    SafeReadMemory(playerPtr + INPUT_BUFFER_OFFSET + (uintptr_t)cur, &v, sizeof(v));
                    recordSlot.bufStream.push_back(v);
                    cur = (cur + 1) % (int)INPUT_BUFFER_SIZE;
                }
                // Attribute any tail entries to the last recorder tick for completeness
                size_t added = recordSlot.bufStream.size() - beforeSize;
                if (added > 0) {
                    if (!recordSlot.bufCountsPerTick.empty()) {
                        recordSlot.bufCountsPerTick.back() = static_cast<uint16_t>(
                            (uint32_t)recordSlot.bufCountsPerTick.back() + (uint32_t)added);
                    } else {
                        recordSlot.bufCountsPerTick.push_back(static_cast<uint16_t>(added));
                    }
                }
            }
            recordSlot.bufEndIdx = endIdx;
        }
        s_recSpanTicks = 0;
        s_recLastMask = 0;
        s_recLastBuf = 0;
        s_recPrevBufIdx = -1;
        s_recLastFacing = 0;

        // Copy the sealed slot before unlocking; "last recording" races with new sessions.
        if (sealedCopy) {
            *sealedCopy = recordSlot;
        }

        // Sealed: release the writer lock before verbose diagnostics.
        recordLock.unlock();

        if (!retiring) {
            LogPlayerBufferSnapshot(recordPlayer, "end");
            LogPlayerImmediateSnapshot(recordPlayer, "end");
        }
        const int slotNum = s_useTransientRecording.load(std::memory_order_acquire) ? 0 : s_curSlot.load();
        int totalTicks = 0; for (auto &sp : recordSlot.spans) totalTicks += sp.ticks;
     LogOut("[MACRO][REC] finished slot=" + std::to_string(slotNum) +
               " spans=" + std::to_string((int)recordSlot.spans.size()) +
         " ticks=" + std::to_string(totalTicks) +
         " streamBytes=" + std::to_string((int)recordSlot.macroStream.size()) +
               " bufEntries=" + std::to_string((int)recordSlot.bufStream.size()) +
         " bufTicks=" + std::to_string((int)recordSlot.bufCountsPerTick.size()) +
               " bufIdxStart=" + std::to_string((int)recordSlot.bufStartIdx) +
               " bufIdxEnd=" + std::to_string((int)recordSlot.bufEndIdx), true);
      {
        LogVectorHex("macroStream", slotNum, "rec-finish", recordSlot.macroStream);
        LogVectorHex("bufStream", slotNum, "rec-finish", recordSlot.bufStream);
        LogVectorU16("bufCountsPerTick", slotNum, "rec-finish", recordSlot.bufCountsPerTick);
        LogVectorU16("bufIndexPerTick", slotNum, "rec-finish", recordSlot.bufIndexPerTick);
                LogTickReasons("rec-finish", slotNum, recordSlot.tickReason);
        LogPerTickOverview(slotNum, "rec-finish", recordSlot.macroStream, recordSlot.bufCountsPerTick, recordSlot.bufIndexPerTick);
        LogVectorHex("immPerTick(raw)", slotNum, "rec-finish", recordSlot.immPerTick);
        LogVectorHex("bufLatestPerTick", slotNum, "rec-finish", recordSlot.bufLatestPerTick);
        if (kEnableFullBufferSnapshots) {
            std::ostringstream ss; ss << "[MACRO][DUMP] slot=" << slotNum << " rec-finish fullBufferSnapshots count=" << recordSlot.fullBufferSnapshots.size() << " (each=" << (int)INPUT_BUFFER_SIZE << ")"; LogOut(ss.str(), true);
            // To avoid massive spam, dump only first and last snapshot (if distinct)
            if (!recordSlot.fullBufferSnapshots.empty()) {
                auto dumpSnap = [&](size_t i, const char* tag){
                    const auto &snap = recordSlot.fullBufferSnapshots[i];
                    std::ostringstream hdr; hdr << "[MACRO][DUMP]   snapshot[" << i << "](" << tag << "):"; LogOut(hdr.str(), true);
                    std::ostringstream line; size_t count=0; for (size_t b=0;b<snap.size();++b){ if(count==0){ line<<"[MACRO][DUMP]     "; }
                        line<< std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)snap[b]; if (b+1<snap.size()) line<<' ';
                        if(++count>=32){ LogOut(line.str(), true); line.str(""); line.clear(); count=0; }
                    }
                    if(count>0) LogOut(line.str(), true);
                };
                dumpSnap(0, "first");
                if (recordSlot.fullBufferSnapshots.size() > 1) dumpSnap(recordSlot.fullBufferSnapshots.size()-1, "last");
            }
        }
            }
        {
            std::lock_guard<std::mutex> lock(s_recordMutex);
            s_state.store(MacroController::State::Idle, std::memory_order_release);
            s_recordFinalizing = false;
        }
        // P2 recording owns the controller even without a side swap; restore independently.
        if (!retiring && recordPlayer == 2 && s_recordOwnsP2Control &&
            g_p2ControlOverridden.load(std::memory_order_acquire)) {
            RestoreP2ControlState();
        }
        if (!retiring && recordPlayer == 2 && s_recordSwitchesLocalControl) {
            // Restore the recorded-from side (not always P1); SetLocalSide updates the swap flag itself.
            const int prev = s_prevLocalSide.load();
            SwitchPlayers::SetLocalSide((prev == 0 || prev == 1) ? prev : 0);
        }
        if (!retiring) {
            s_recordOwnsP2Control = false;
            s_recordSwitchesLocalControl = false;
            s_prevLocalSide.store(-1);
        }
        LogOut("[MACRO][REC] post-finish: P" + std::to_string(recordPlayer) + " capture released", true);
        if (!retiring && s_macroBannerId != -1) { DirectDrawHook::RemovePermanentMessage(s_macroBannerId); s_macroBannerId = -1; }
        if (!retiring && !embeddedMissionCapture) {
            DirectDrawHook::AddMessage("Macro: Recording stopped", "MACRO",
                                       RGB(255,220,120), 1200, 0, 120);
        }
    }

    std::string StateName(MacroController::State st) {
        switch (st) {
            case MacroController::State::Idle: return "Idle";
            case MacroController::State::PreRecord: return "PreRecord";
            case MacroController::State::Recording: return "Recording";
            case MacroController::State::Replaying: return "Replaying";
        }
        return "?";
    }

    uint8_t ReadRecordSourceMask(int playerNum) {
        // Build a unified mask from the selected player's immediate registers.
        uintptr_t playerPtr = MacroPlayerPointer(ClampPlayer(playerNum));
        if (!playerPtr) return 0;
        uint8_t h=0, v=0, a=0, b=0, c=0, d=0;
        SafeReadMemory(playerPtr + INPUT_HORIZONTAL_OFFSET, &h, sizeof(h));
        SafeReadMemory(playerPtr + INPUT_VERTICAL_OFFSET, &v, sizeof(v));
        SafeReadMemory(playerPtr + INPUT_BUTTON_A_OFFSET, &a, sizeof(a));
        SafeReadMemory(playerPtr + INPUT_BUTTON_B_OFFSET, &b, sizeof(b));
        SafeReadMemory(playerPtr + INPUT_BUTTON_C_OFFSET, &c, sizeof(c));
        SafeReadMemory(playerPtr + INPUT_BUTTON_D_OFFSET, &d, sizeof(d));
        uint8_t mask = 0;
        if (h == 1) mask |= GAME_INPUT_RIGHT; else if (h == 255) mask |= GAME_INPUT_LEFT;
        if (v == 1) mask |= GAME_INPUT_DOWN;  else if (v == 255) mask |= GAME_INPUT_UP;
        if (a) mask |= GAME_INPUT_A;
        if (b) mask |= GAME_INPUT_B;
        if (c) mask |= GAME_INPUT_C;
        if (d) mask |= GAME_INPUT_D;
        return mask;
    }

    // Read the most recently written input-buffer value for the selected player.
    uint8_t ReadBufferLatestMask(int playerNum) {
        uintptr_t playerPtr = MacroPlayerPointer(ClampPlayer(playerNum));
        if (!playerPtr) return 0;
        uint16_t idx = 0;
        if (!SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &idx, sizeof(idx))) return 0;
        int last = ((int)idx - 1);
        last %= (int)INPUT_BUFFER_SIZE; if (last < 0) last += (int)INPUT_BUFFER_SIZE;
        uint8_t val = 0;
        SafeReadMemory(playerPtr + INPUT_BUFFER_OFFSET + (uintptr_t)last, &val, sizeof(val));
        return val;
    }
}

namespace MacroController {

void Tick() {
    s_recordTickAdvanced.store(false, std::memory_order_release);
    const State observedState = s_state.load(std::memory_order_acquire);
    if (observedState != State::Recording && observedState != State::Replaying) {
        return;
    }

    // Join the shared input/control barrier: netplay publication must not interleave with a macro tick.
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    MacroWork work;
    if (!work) return;
    if (g_onlineModeActive.load(std::memory_order_acquire)) return;
    if (s_state.load(std::memory_order_acquire) != State::Recording &&
        s_state.load(std::memory_order_acquire) != State::Replaying) {
        return;
    }
    if (s_state.load(std::memory_order_acquire) == State::Recording &&
        s_recordingCaptureSuspended.load(std::memory_order_acquire)) {
        return;
    }

    if (s_recordStopRequested.exchange(false, std::memory_order_acq_rel) &&
        s_state.load(std::memory_order_acquire) == State::Recording) {
        FinishRecording(false);
        return;
    }
    // Only operate during a valid match with characters initialized
    if (GetCurrentGamePhase() != GamePhase::Match || !AreCharactersInitialized()) return;

    // Pace counter for 64 Hz logical ticks using 192 Hz internal frames
    ++s_callsSinceDiv0;
    if (++s_frameDiv >= 3) {
        s_frameDiv = 0;
        if (s_firstDiv0Seen) {
            if (s_callsSinceDiv0 != 3) {
                LogOut("[MACRO][DIAG] frameDiv cycleLen=" + std::to_string(s_callsSinceDiv0) + " (expected 3) -- possible double Tick invocation", true);
            }
        } else {
            s_firstDiv0Seen = true;
        }
        s_callsSinceDiv0 = 0;
    }

    State st = s_state.load();
    if (st == State::Recording) {
        const int recordPlayer = ClampPlayer(s_recordPlayer.load(std::memory_order_acquire));
        Slot& recordSlot = ActiveRecordingSlot();
        std::unique_lock<std::mutex> recordLock(s_recordMutex);
        if (s_state.load(std::memory_order_acquire) != State::Recording
            || s_recordFinalizing) {
            return;
        }

        // Unfrozen: ~64 Hz (every 3rd internal frame); frozen: advance on buffer/step movement only.
    const bool frozen = ReadGamespeedFrozen();
        // Probe current buffer index once up front to decide whether to progress while frozen
        uint16_t idxProbe = 0; bool haveIdx = false; bool bufAdvanced = false;
        {
            uintptr_t playerPtrProbe = MacroPlayerPointer(recordPlayer);
            if (playerPtrProbe && SafeReadMemory(playerPtrProbe + INPUT_BUFFER_INDEX_OFFSET, &idxProbe, sizeof(idxProbe))) {
                haveIdx = true;
                if (s_recPrevBufIdx >= 0 && (uint16_t)s_recPrevBufIdx != idxProbe) bufAdvanced = true;
            }
        }
        // Step counter captures manual advances even with no buffer write (pure neutral frame).
        bool stepAdvanced = false;
        if (frozen) {
            stepAdvanced = PauseIntegration::ConsumeStepAdvance();
        }
        bool shouldAdvance = false;
        if (!frozen) {
            // Sync to buffer updates (anti-drift); fall back to s_frameDiv if the index read failed.
            if (s_recPrevBufIdx >= 0) {
                shouldAdvance = bufAdvanced;
            } else {
                shouldAdvance = (s_frameDiv == 0);
            }
        } else {
            // Frozen: advance on buffer move OR step increment (preserves pure-neutral delays).
            shouldAdvance = (bufAdvanced || stepAdvanced);
            if (shouldAdvance && stepAdvanced && !bufAdvanced) {
                LogOut("[MACRO][REC] step-advance tick (no buffer write)", true);
            }
        }
        if (!shouldAdvance) return;
        char reasonCode='?';
        if (!frozen) reasonCode='U';
        else if (bufAdvanced && stepAdvanced) reasonCode='X';
        else if (bufAdvanced) reasonCode='B';
        else if (stepAdvanced) reasonCode='S';
        // If buffer-freeze owns this player, avoid progressing to keep streams aligned.
        if (g_bufferFreezingActive.load() &&
            (g_activeFreezePlayer.load() == recordPlayer || g_activeFreezePlayer.load() == 0)) return;
        uint8_t immMask = ReadRecordSourceMask(recordPlayer);
        uint8_t immMaskRaw = immMask; // preserve original before any merging
        int facing = ReadFacingSign(recordPlayer);
        uint8_t buf  = 0;
        bool noWritesThisTick = false; // track if engine produced zero buffer writes (pre-synthesis)
        uint8_t latestBufValPreSynth = ReadBufferLatestMask(recordPlayer);
        {
            uintptr_t playerPtr = MacroPlayerPointer(recordPlayer);
            uint16_t idx = 0;
            if (playerPtr && SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &idx, sizeof(idx)) && s_recPrevBufIdx >= 0) {
                size_t beforeSize = recordSlot.bufStream.size();
                uint8_t addedBtnUnion = 0; // union of A-D seen in new entries this tick
                int cur = s_recPrevBufIdx;
                while (cur != idx) {
                    uint8_t v = 0;
                    SafeReadMemory(playerPtr + INPUT_BUFFER_OFFSET + (uintptr_t)cur, &v, sizeof(v));
                    recordSlot.bufStream.push_back(v);
                    addedBtnUnion |= (v & (GAME_INPUT_A | GAME_INPUT_B | GAME_INPUT_C | GAME_INPUT_D));
                    cur = (cur + 1) % (int)INPUT_BUFFER_SIZE;
                }
                s_recPrevBufIdx = idx;
                // Record how many entries the engine produced this recorder tick
                size_t added = recordSlot.bufStream.size() - beforeSize;
                recordSlot.bufCountsPerTick.push_back(static_cast<uint16_t>(added));
                recordSlot.bufIndexPerTick.push_back(idx);
                if (added > 0) {
                    buf = recordSlot.bufStream.back();
                    // Merge buttons seen anywhere in this tick's new entries into our immediate view
                    if (addedBtnUnion) {
                        uint8_t immButtons = (uint8_t)(immMask & (GAME_INPUT_A | GAME_INPUT_B | GAME_INPUT_C | GAME_INPUT_D));
                        immMask = (uint8_t)((immMask & (GAME_INPUT_UP | GAME_INPUT_DOWN | GAME_INPUT_LEFT | GAME_INPUT_RIGHT))
                                  | (immButtons | addedBtnUnion));
                    }
                } else {
                    noWritesThisTick = true;
                }
            }
        }
        if (buf == 0) buf = ReadBufferLatestMask(recordPlayer);
        // Step-advanced tick with no writes: immediate is authoritative (stale buffer buttons would absorb the neutral delay).
        if (noWritesThisTick) {
            uint8_t lastMask = (s_recSpanTicks > 0) ? (uint8_t)(s_recLastMask & 0xFF) : 0xFF; // 0xFF sentinel so first span always sets
            if (s_recSpanTicks == 0 || immMask != lastMask) {
                buf = immMask; // override with immediate state to create/extend proper neutral span
            }
        }
        // Combine: use buffer's directional bits (authoritative) + union of buffer/immediate button bits (A-D)
        const uint8_t DIR_MASK = (GAME_INPUT_UP | GAME_INPUT_DOWN | GAME_INPUT_LEFT | GAME_INPUT_RIGHT); // 0x0F
        const uint8_t BTN_MASK = (GAME_INPUT_A | GAME_INPUT_B | GAME_INPUT_C | GAME_INPUT_D);            // 0xF0
        uint8_t dirBits = buf & DIR_MASK;
        uint8_t btnBits = (uint8_t)((immMask | buf) & BTN_MASK);
        uint8_t mask = (dirBits | btnBits);
        // EfzRevival-style: Write one byte per logic tick into per-slot macro stream
        {
            recordSlot.macroStream.push_back(mask);
            s_recordTickAdvanced.store(true, std::memory_order_release);
            if (recordSlot.tickReason.size() < recordSlot.macroStream.size()) {
                recordSlot.tickReason.push_back(reasonCode);
            }
            // Per-tick raw immediate & latest buffer (pre-synthetic) capture
            recordSlot.immPerTick.push_back(immMaskRaw);
            recordSlot.bufLatestPerTick.push_back(latestBufValPreSynth);
            if (kEnableFullBufferSnapshots) {
                uintptr_t playerPtrSnap = MacroPlayerPointer(recordPlayer);
                std::vector<uint8_t> snap;
                if (playerPtrSnap) {
                    snap.resize(INPUT_BUFFER_SIZE, 0);
                    for (size_t bi = 0; bi < snap.size(); ++bi) {
                        uint8_t v=0; SafeReadMemory(playerPtrSnap + INPUT_BUFFER_OFFSET + (uintptr_t)bi, &v, sizeof(v)); snap[bi]=v;
                    }
                }
                recordSlot.fullBufferSnapshots.push_back(std::move(snap));
            }
        }
        // Synthesize one entry on zero-write ticks so playback reproduces per-tick cadence.
        {
            if (!recordSlot.bufCountsPerTick.empty() && recordSlot.bufCountsPerTick.back() == 0) {
                recordSlot.bufStream.push_back(mask); // synthetic neutral/held state
                recordSlot.bufCountsPerTick.back() = 1;
                buf = mask; // treat buffer value as this synthetic entry for span comparison logic
                LogOut(std::string("[MACRO][REC] synthetic-buf write (neutral tick) reason=") + reasonCode, true);
            }
        }
        if (s_recSpanTicks == 0) {
            s_recLastMask = mask; s_recLastBuf = mask; s_recLastFacing = facing; s_recSpanTicks = 1; s_recLastReason = reasonCode;
        } else if (mask == s_recLastMask && buf == s_recLastBuf && facing == s_recLastFacing) {
            // Merge across reasons; differing provenance marks 'M' (S+U delays still compress).
            if (s_recLastReason != reasonCode) s_recLastReason = 'M';
            s_recSpanTicks++;
        } else {
            recordSlot.spans.push_back({ static_cast<Mask>(s_recLastMask & 0xFF), static_cast<Mask>(s_recLastBuf & 0xFF), s_recSpanTicks, (int8_t)s_recLastFacing });
            recordSlot.hasData = true;
            LogOut(std::string("[MACRO][REC] span imm=0x") + ToHexString((int)(s_recLastMask & 0xFF), 2) +
                   " (" + MaskToButtons((Mask)(s_recLastMask & 0xFF)) + ") buf=0x" + ToHexString((int)(s_recLastBuf & 0xFF), 2) +
                   " (" + MaskToButtons((Mask)(s_recLastBuf & 0xFF)) + ") ticks=" + std::to_string(s_recSpanTicks) +
                   " facing=" + std::to_string(s_recLastFacing) + " reason=" + s_recLastReason, true);
            s_recLastMask = mask; s_recLastBuf = mask; s_recLastFacing = facing; s_recSpanTicks = 1; s_recLastReason = reasonCode;
        }
    } else if (st == State::Replaying) {
        const int playPlayer = ClampPlayer(s_playPlayer.load(std::memory_order_acquire));
        Slot& playSlot = ActivePlaybackSlot();
        // Finish guard: after neutral clear tick, hold neutral until moveID activation or timeout
        if (s_finishGuardActive) {
            g_pollOverrideMask[playPlayer].store(0, std::memory_order_relaxed);
            g_pollOverrideActive[playPlayer].store(true, std::memory_order_relaxed);
            uint16_t mv = GetPlayerMoveID(playPlayer);
            bool activated = IsActivationMove(mv);
                if (activated || s_finishGuardFramesLeft <= 0) {
                s_finishGuardActive = false;
                const bool keepExclusiveHold = s_exclusiveReplay.load(std::memory_order_acquire);
                s_state.store(State::Idle);
                ImmediateInput::Clear(playPlayer);
                (void)ClearPlayerCommandFlags(playPlayer);
                g_pollOverrideMask[playPlayer].store(0, std::memory_order_relaxed);
                g_pollOverrideActive[playPlayer].store(keepExclusiveHold, std::memory_order_release);
                g_forceBypass[playPlayer].store(false);
                g_injectImmediateOnly[playPlayer].store(false);
                if (s_macroBannerId != -1) { DirectDrawHook::RemovePermanentMessage(s_macroBannerId); s_macroBannerId = -1; }
                // Wake-prebuffered macro (armed player only): preserve the input
                // buffer + motion token; completion schedules delayed token
                // neutralization. Others: full restore clears the buffer.
                if (g_macroWakePreserveBuffer.load() &&
                    g_macroWakePreservePlayer.load() == playPlayer) {
                    g_macroWakePreserveBuffer.store(false);
                    g_macroWakePreservePlayer.store(0);
                    // Signal auto-action that a wake-prebuffered macro has
                    // fully finished so it can schedule a delayed
                    // neutralization of the motion token after wake. Carry
                    // the play player so the cleanup lands on that fighter.
                    g_wakeMacroPlaybackCompleted.store(playPlayer);
                    if (playPlayer == 2 && g_p2ControlOverridden) {
                        RestoreP2ControlFlagsPreserveBufferAndTokenForMacro();
                    }
                } else {
                    // Neutralize motion token before giving control back to AI to prevent stray motions
                    (void)NeutralizeMotionToken(playPlayer);
                    if (playPlayer == 2 && g_p2ControlOverridden) RestoreP2ControlState();
                }
                LogOut(std::string("[MACRO][PLAY] finish-guard exit ") + (activated?"on-activation":"on-timeout") +
                   " slot=" + std::to_string(s_curSlot.load()) +
                   " lastMoveID=" + std::to_string((int)mv) +
                   " player=P" + std::to_string(playPlayer) +
                   " streamBytes=" + std::to_string((int)playSlot.macroStream.size()) +
                   " bufWrites=" + std::to_string((int)playSlot.bufStream.size()) +
                   " pollHits=" + std::to_string(GetInputPollOverrideHitCount(playPlayer)), true);
                if (!keepExclusiveHold) {
                    s_exclusiveReplay.store(false, std::memory_order_release);
                    s_useTransientPlayback.store(false, std::memory_order_release);
                }
                if (!keepExclusiveHold &&
                    !s_useTransientPlayback.load(std::memory_order_acquire)) {
                    DirectDrawHook::AddMessage("Macro: Replay finished", "MACRO",
                                               RGB(180,255,180), 1200, 0, 120);
                }
                return;
            }
            if (s_finishGuardFramesLeft > 0) s_finishGuardFramesLeft--;
            return;
        }
        // If we are in or pending the finishing phase, handle the neutral clear tick.
        if (s_finishPendingClearTick && s_frameDiv == 0) {
            // Start a full neutral clear tick at the next 64 Hz boundary (3 subframes)
            s_finishPendingClearTick = false;
            s_finishing = true;
            s_finishNeutralFrames = 3; // full tick at 192 Hz pacing
            s_tickBufQueue.clear();
            s_tickBufHead = 0;
            s_writesLeftThisTick = 0;
            s_baselineMask = 0;
        }
        // While waiting for the next boundary, maintain neutral override to avoid tail holds
        if (s_finishPendingClearTick && s_frameDiv != 0) {
            g_pollOverrideMask[playPlayer].store(0, std::memory_order_relaxed);
            g_pollOverrideActive[playPlayer].store(true, std::memory_order_relaxed);
            return;
        }
        if (s_finishing) {
            g_pollOverrideMask[playPlayer].store(0, std::memory_order_relaxed);
            g_pollOverrideActive[playPlayer].store(true, std::memory_order_relaxed);
            if (s_finishNeutralFrames > 0) {
                s_finishNeutralFrames--;
                return;
            }
            s_finishing = false;
            s_finishGuardActive = true;
            // Default guard window: 6 internal frames (~2 visual frames)
            s_finishGuardFramesLeft = 6;
            s_finishGuardStartMoveId = GetPlayerMoveID(playPlayer);
            // Keep neutral override active during guard and clear command flags once
            (void)ClearPlayerCommandFlags(playPlayer);
            return;
        }
        if (s_primedTickAwaitingFirstPoll) {
            const uint32_t pollHits =
                GetInputPollOverrideHitCount(playPlayer);
            if (pollHits != s_primedTickPollHitsAtLatch) {
                s_primedTickAwaitingFirstPoll = false;
                LogOut("[MACRO][PLAY][PRIME] P" +
                           std::to_string(playPlayer) +
                           " first poll acknowledged hits=" +
                           std::to_string(pollHits),
                       true);
            } else if (ReadGamespeedFrozen()) {
                // Admission primes under a held transition pause; the watchdog must not fire yet.
                g_pollOverrideActive[playPlayer].store(
                    true, std::memory_order_release);
                return;
            }
        }
        if (s_endStreamPending) {
            const uint32_t pollHits = GetInputPollOverrideHitCount(playPlayer);
            if (pollHits == s_endStreamPollHitsAtLatch && ++s_endStreamWaitFrames < 12) {
                // Keep the final byte authoritative until a poll consumes it.
                g_pollOverrideActive[playPlayer].store(true, std::memory_order_release);
                return;
            }
            if (pollHits == s_endStreamPollHitsAtLatch) {
                LogOut("[MACRO][PLAY][WARN] final input poll was not acknowledged; "
                       "continuing neutral cleanup after bounded hold", true);
            }
            s_endStreamPending = false;
            s_endStreamWaitFrames = 0;
            s_finishPendingClearTick = true;
            s_baselineMask = 0;
            g_pollOverrideMask[playPlayer].store(0, std::memory_order_relaxed);
            g_pollOverrideActive[playPlayer].store(true, std::memory_order_release);
            (void)ClearPlayerCommandFlags(playPlayer);
            return;
        }
        // Frozen: keep the override alive but advance only on step signals (mirrors the recorder).
        const bool playbackFrozen = ReadGamespeedFrozen();
        bool playbackStepAdvanced = false;
        if (playbackFrozen) {
            playbackStepAdvanced = PauseIntegration::ConsumeStepAdvance();
        }
        // Pause while buffer-freeze owns the playback player.
        if (g_bufferFreezingActive.load() &&
            (g_activeFreezePlayer.load() == playPlayer || g_activeFreezePlayer.load() == 0)) return;
        // Prefer stream playback if present. Fallback to spans if no stream captured.
        bool useStream = !playSlot.macroStream.empty();
        if (!playSlot.hasData || (playSlot.spans.empty() && !useStream)) {
            s_state.store(State::Idle);
            ImmediateInput::Clear(playPlayer);
            (void)ClearPlayerCommandFlags(playPlayer);
            g_manualInputOverride[playPlayer].store(false);
            g_forceBypass[playPlayer].store(false);
            g_injectImmediateOnly[playPlayer].store(false);
            g_pollOverrideMask[playPlayer].store(0, std::memory_order_relaxed);
            g_pollOverrideActive[playPlayer].store(false, std::memory_order_release);
            if (s_macroBannerId != -1) {
                DirectDrawHook::RemovePermanentMessage(s_macroBannerId);
                s_macroBannerId = -1;
            }
            // If we had taken control, neutralize and restore to AI
            if (playPlayer == 2 && g_p2ControlOverridden) {
                (void)NeutralizeMotionToken(playPlayer);
                RestoreP2ControlState();
            }
            const bool embeddedPlayback = s_exclusiveReplay.load(std::memory_order_acquire) ||
                s_useTransientPlayback.load(std::memory_order_acquire);
            s_exclusiveReplay.store(false, std::memory_order_release);
            s_useTransientPlayback.store(false, std::memory_order_release);
            if (!embeddedPlayback) {
                DirectDrawHook::AddMessage("Macro: Replay empty", "MACRO",
                                           RGB(255,120,120), 1000, 0, 120);
            }
            return;
        }
        if (useStream) {
            // Advance once per engine frame (buffer index change).
            
            uintptr_t playerPtr = MacroPlayerPointer(playPlayer);
            uint16_t curBufIdx = 0;
            bool haveIdx = playerPtr && SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &curBufIdx, sizeof(curBufIdx));
            
            if (haveIdx && !s_playbackBufIdxSyncInitialized) {
                s_lastSeenBufIdx = curBufIdx;
                s_playbackBufIdxSyncInitialized = true;
                LogOut("[MACRO][SYNC] Initialized: curBufIdx=" + std::to_string(curBufIdx), true);
            }
            
            bool bufferAdvanced = false;
            bool shouldAdvanceStream = false;
            if (haveIdx && s_playbackBufIdxSyncInitialized &&
                (s_playStreamIndex < playSlot.macroStream.size() ||
                 s_primedTickDrainPending)) {
                
                // Buffer changes once per real frame: naturally handles double-Tick.
                if (curBufIdx != s_lastSeenBufIdx) {
                    bufferAdvanced = true;
                }
            }

            // Drain tick zero's remaining raw writes before tick one, even if the ring index already moved.
            if (s_primedTickDrainPending) {
                const uint32_t primedPollHits =
                    GetInputPollOverrideHitCount(playPlayer);
                const bool primedPollAdvanced =
                    primedPollHits != s_primedTickPollHitsAtLastPublish;
                if (!PlaybackStartPolicy::CanPublishNextPrimedSlice(
                        s_primedTickAwaitingFirstPoll,
                        primedPollAdvanced, bufferAdvanced,
                        playbackStepAdvanced)) {
                    // Mirror the stream watchdog: an unfrozen dead lane must not strand the demo.
                    if (!playbackFrozen && ++s_playNoProgressFrames > 384) {
                        LogOut("[MACRO][PLAY][WARN] primed input buffer stopped "
                               "advancing; ending replay after two-second "
                               "watchdog", true);
                        s_playNoProgressFrames = 0;
                        s_tickBufQueue.clear();
                        s_tickBufHead = 0;
                        s_writesLeftThisTick = 0;
                        s_primedTickDrainPending = false;
                        s_primedTickSubframesRemaining = 0;
                        s_primedTickAwaitingFirstPoll = false;
                        s_baselineMask = 0;
                        s_finishPendingClearTick = true;
                        g_pollOverrideMask[playPlayer].store(
                            0, std::memory_order_relaxed);
                        g_pollOverrideActive[playPlayer].store(
                            true, std::memory_order_release);
                        return;
                    }
                    g_pollOverrideActive[playPlayer].store(
                        true, std::memory_order_release);
                    return;
                }
                if (bufferAdvanced) s_lastSeenBufIdx = curBufIdx;
                // Anchor to admission, not s_frameDiv: frozen monitor passes must not compress raw states.
                const int subframesLeft = (std::max)(
                    1, s_primedTickSubframesRemaining);
                const bool finalPrimedSlice =
                    PlaybackStartPolicy::PrimedSliceCanLatchEnd(
                        s_primedTickSubframesRemaining);
                const PublishedStreamFrame published =
                    PublishPreparedStreamFrame(
                        playSlot, playPlayer, subframesLeft,
                        finalPrimedSlice);
                s_primedTickPollHitsAtLastPublish = primedPollHits;
                if (s_primedTickSubframesRemaining > 0) {
                    --s_primedTickSubframesRemaining;
                }
                const int queuedWrites = static_cast<int>(
                    s_tickBufQueue.size() -
                    (std::min)(s_tickBufHead, s_tickBufQueue.size()));
                s_primedTickDrainPending =
                    PlaybackStartPolicy::PrimedTickPending(
                        s_primedTickSubframesRemaining, queuedWrites,
                        static_cast<int>(s_writesLeftThisTick));
                if (!s_primedTickDrainPending) {
                    s_primedTickSubframesRemaining = 0;
                    // Re-anchor the divider so tick one starts at subframe 0.
                    s_frameDiv = 2;
                    s_callsSinceDiv0 = 2;
                    s_firstDiv0Seen = false;
                }
                if (published.endLatched) return;
                s_playNoProgressFrames = 0;
                return;
            }
            if (s_playStreamIndex < playSlot.macroStream.size()) {
                shouldAdvanceStream = playbackFrozen
                    ? (bufferAdvanced || playbackStepAdvanced)
                    : bufferAdvanced;
            }
            if (bufferAdvanced) {
                s_lastSeenBufIdx = curBufIdx;
            }

            if (shouldAdvanceStream) {
                s_playNoProgressFrames = 0;
            } else if (!playbackFrozen && s_playStreamIndex < playSlot.macroStream.size()) {
                if (++s_playNoProgressFrames > 384) {
                    LogOut("[MACRO][PLAY][WARN] input buffer stopped advancing; "
                           "ending replay after two-second watchdog", true);
                    s_playNoProgressFrames = 0;
                    s_tickBufQueue.clear();
                    s_tickBufHead = 0;
                    s_writesLeftThisTick = 0;
                    s_baselineMask = 0;
                    s_finishPendingClearTick = true;
                    g_pollOverrideMask[playPlayer].store(0, std::memory_order_relaxed);
                    g_pollOverrideActive[playPlayer].store(true, std::memory_order_release);
                    return;
                }
            } else if (playbackFrozen) {
                s_playNoProgressFrames = 0;
            }
            
            if (shouldAdvanceStream &&
                s_playStreamIndex < playSlot.macroStream.size()) {
                (void)PrepareNextStreamTick(playSlot, playPlayer, playerPtr);
            }

            if (!s_playbackPollAuditLogged) {
                const uint32_t pollHits = GetInputPollOverrideHitCount(playPlayer);
                if (pollHits != 0) {
                    LogOut("[MACRO][PLAY] Verified P" + std::to_string(playPlayer)
                        + " poll override consumption; hits="
                        + std::to_string(pollHits), true);
                    s_playbackPollAuditLogged = true;
                } else if (s_playStreamIndex >= 8) {
                    LogOut("[MACRO][PLAY][ERROR] Playback cursor advanced without target poll override consumption", true);
                    s_playbackPollAuditLogged = true;
                }
            }
            if (PublishPreparedStreamFrame(
                    playSlot, playPlayer,
                    playbackFrozen
                        ? (playbackStepAdvanced || bufferAdvanced ? 1 : 0)
                        : (std::max)(1, 3 - s_frameDiv)).endLatched) {
                return;
            }
        } else {
            // Fallback to existing RLE span playback (legacy path)
            const bool legacySpanAdvance = playbackFrozen ? playbackStepAdvanced : (s_frameDiv == 0);
            if (legacySpanAdvance && s_playSpanRemaining <= 0) {
                if (s_playIndex >= playSlot.spans.size()) {
                    // End via spans: defer to next tick boundary then perform full neutral clear tick
                    s_finishPendingClearTick = true;
                    // Early clear of command flags as we enter finish sequence (spans path)
                    (void)ClearPlayerCommandFlags(playPlayer);
                    return;
                }
                const RLESpan &sp = playSlot.spans[s_playIndex++];
                s_playSpanRemaining = sp.ticks;
                // Determine current facing and flip if needed
                int curFacing = ReadFacingSign(playPlayer);
                uint8_t maskToApply = sp.mask;
                int recFacing = (sp.facing == 0) ? +1 : sp.facing;
                if (curFacing != 0 && recFacing != curFacing) {
                    maskToApply = FlipMaskHoriz(maskToApply);
                }
                // For RLE fallback, also use poll override so engine cadence is preserved
                g_pollOverrideMask[playPlayer].store(maskToApply, std::memory_order_relaxed);
                g_pollOverrideActive[playPlayer].store(true, std::memory_order_relaxed);
                g_forceBypass[playPlayer].store(false);
                g_injectImmediateOnly[playPlayer].store(false);
                LogOut(std::string("[MACRO][PLAY] span imm=0x") + ToHexString((int)sp.mask, 2) +
                       " (" + MaskToButtons(sp.mask) + ") buf=0x" + ToHexString((int)sp.buf, 2) +
                       " (" + MaskToButtons(sp.buf) + ") ticks=" + std::to_string(sp.ticks) +
                       " recFacing=" + std::to_string((int)sp.facing) + " curFacing=" + std::to_string(curFacing) +
                       " -> applied=0x" + ToHexString((int)maskToApply, 2) + " [poll-override]", true);
            } else if (legacySpanAdvance && s_playSpanRemaining > 0) {
                s_playSpanRemaining--;
                if (s_playSpanRemaining <= 0) {
                    // Force a neutral edge between spans to ensure clean transitions via poll
                    ImmediateInput::Clear(playPlayer);
                    g_pollOverrideMask[playPlayer].store(0, std::memory_order_relaxed);
                }
            }
        }
    }
}

namespace {

struct P2MacroLaneStatus {
    bool normalPulse{false};
    bool normalRawOwner{false};
    bool immediateInput{false};
    bool scopedMotion{false};
    bool legacyMotionQueue{false};
    bool tutorialControl{false};
    bool manualOverride{false};
    bool pollOverride{false};
    bool forceBypass{false};
    bool immediateOnly{false};
    bool bufferFreeze{false};
    bool forceHumanThread{false};
    bool scopedReservation{false};

    bool HasConflict() const {
        return normalPulse || normalRawOwner || immediateInput || scopedMotion ||
               legacyMotionQueue || tutorialControl || manualOverride ||
               pollOverride || forceBypass || immediateOnly || bufferFreeze ||
               forceHumanThread || scopedReservation;
    }
};

P2MacroLaneStatus ReadP2MacroLaneStatus() {
    P2MacroLaneStatus status;
    status.normalPulse = IsAutoActionNormalPulseActive(2);
    status.normalRawOwner =
        IsAutoActionNormalPulseOwningImmediateRegisters(2);
    status.immediateInput = ImmediateInput::GetCurrentDesired(2) != 0 ||
        ImmediateInput::GetRemainingTicks(2) != 0;
    status.scopedMotion = IsP2AutoActionMotionTransactionActive();
    const MotionQueueSnapshot queue = GetMotionQueueSnapshot(2);
    status.legacyMotionQueue = queue.active || TutorialMotionQueueLeaseActive(2);
    status.tutorialControl = TutorialP2ControlLeaseActive();
    status.manualOverride =
        g_manualInputOverride[2].load(std::memory_order_acquire);
    status.pollOverride =
        g_pollOverrideActive[2].load(std::memory_order_acquire);
    status.forceBypass = g_forceBypass[2].load(std::memory_order_acquire);
    status.immediateOnly =
        g_injectImmediateOnly[2].load(std::memory_order_acquire);
    const int freezeOwner = g_activeFreezePlayer.load(std::memory_order_acquire);
    status.bufferFreeze =
        g_bufferFreezingActive.load(std::memory_order_acquire) &&
        (freezeOwner == 0 || freezeOwner == 2);
    status.forceHumanThread =
        g_forceHumanControlActive.load(std::memory_order_acquire);
    status.scopedReservation = IsScopedInputReserved(2);
    return status;
}

void LogP2MacroOwnershipFailure(const char* operation,
                                const P2MacroLaneStatus& status,
                                bool controllerTracked,
                                bool controllerHuman) {
    std::ostringstream oss;
    oss << "[MACRO][OWNER] P2 " << operation << " takeover rejected"
        << " tracked=" << (controllerTracked ? 1 : 0)
        << " human=" << (controllerHuman ? 1 : 0)
        << " normal=" << (status.normalPulse ? 1 : 0)
        << " normalRaw=" << (status.normalRawOwner ? 1 : 0)
        << " immediate=" << (status.immediateInput ? 1 : 0)
        << " scopedMotion=" << (status.scopedMotion ? 1 : 0)
        << " queue=" << (status.legacyMotionQueue ? 1 : 0)
        << " tutorial=" << (status.tutorialControl ? 1 : 0)
        << " manual=" << (status.manualOverride ? 1 : 0)
        << " poll=" << (status.pollOverride ? 1 : 0)
        << " bypass=" << (status.forceBypass ? 1 : 0)
        << " immediateOnly=" << (status.immediateOnly ? 1 : 0)
        << " freeze=" << (status.bufferFreeze ? 1 : 0)
        << " forceHuman=" << (status.forceHumanThread ? 1 : 0);
    oss << " reserved=" << (status.scopedReservation ? 1 : 0);
    LogOut(oss.str(), true);
}

// Publish states only after owning the P2 controller and every lane; async cancellation must be verified, not assumed.
bool AcquireP2MacroControlOwnership(const char* operation) {
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;

    const bool controlledBefore =
        g_p2ControlOverridden.load(std::memory_order_acquire);

    // Wait, never cancel: don't touch the AI flag while a normal owns +392..397.
    const P2MacroLaneStatus before = ReadP2MacroLaneStatus();
    if (before.normalPulse || before.normalRawOwner || before.immediateInput ||
        before.scopedMotion ||
        before.legacyMotionQueue || before.tutorialControl ||
        before.manualOverride || before.pollOverride || before.forceBypass ||
        before.immediateOnly || before.bufferFreeze || before.forceHumanThread ||
        before.scopedReservation) {
        LogP2MacroOwnershipFailure(operation, before, controlledBefore,
                                   IsAIControlFlagHuman(2));
        return false;
    }

    // Retire the completed edge latch only after admission proved non-destructive.
    ImmediateInput::Clear(2);

    // Synchronously cancels the scoped-motion owner, writes AI=0, verifies.
    EnableP2ControlForAutoAction();

    const P2MacroLaneStatus after = ReadP2MacroLaneStatus();
    const bool controllerTracked =
        g_p2ControlOverridden.load(std::memory_order_acquire);
    const bool controllerHuman = IsAIControlFlagHuman(2);
    if (!controllerTracked || !controllerHuman || after.HasConflict()) {
        LogP2MacroOwnershipFailure(operation, after, controllerTracked,
                                   controllerHuman);
        // Never leak a newly acquired flag on failure, never release an older owner's controller.
        if (!controlledBefore && controllerTracked &&
            !after.tutorialControl) {
            RestoreP2ControlState();
        }
        return false;
    }

    LogOut(std::string("[MACRO][OWNER] P2 ") + operation +
           " takeover verified", true);
    return true;
}

} // namespace

bool BeginPlayerRecording(int playerNum, bool switchLocalControl) {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    MacroWork work;
    if (!work) return false;
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (GetCurrentGamePhase() != GamePhase::Match || !AreCharactersInitialized()) {
        DirectDrawHook::AddMessage("Macro controls available only during Match", "MACRO", RGB(255, 180, 120), 900, 0, 120);
        return false;
    }
    if (s_state.load(std::memory_order_acquire) != State::Idle) return false;

    const int player = ClampPlayer(playerNum);
    if (IsScopedInputReserved(player)) return false;
    if (player == 2 && !AcquireP2MacroControlOwnership("recording")) {
        return false;
    }

    s_activePlayer = s_scopeRequired ? MacroPlayerPointer(player) : 0;
    s_recordPlayer.store(player, std::memory_order_release);
    s_recordingCaptureSuspended.store(false, std::memory_order_release);
    s_recordStopRequested.store(false, std::memory_order_release);
    s_recordOwnsP2Control = player == 2;
    s_recordSwitchesLocalControl = switchLocalControl && player == 2;
    // P1/no-swap is the mission-authoring policy and gets an ephemeral clip.
    s_useTransientRecording.store(player == 1 && !switchLocalControl, std::memory_order_release);

    if (s_recordSwitchesLocalControl && GetCurrentGameMode() == GameMode::Practice) {
        const int curLocal = SwitchPlayers::GetLocalSide();
        if (curLocal == 0 || curLocal == 1) s_prevLocalSide.store(curLocal);
        if (curLocal != 1) {
            SwitchPlayers::SetLocalSide(1);
            SwitchPlayers::MarkSwapped();
        }
    }

    s_state.store(State::PreRecord, std::memory_order_release);
    if (!s_useTransientRecording.load(std::memory_order_acquire)) {
        const std::string label = "Macro: PreRecord";
        if (s_macroBannerId == -1) {
            s_macroBannerId = DirectDrawHook::AddPermanentMessage(label, RGB(255,255,0), kBannerX, kBannerY);
        } else {
            DirectDrawHook::UpdatePermanentMessage(s_macroBannerId, label, RGB(255,255,0));
        }
    }
    LogOut("[MACRO] PreRecord: player=P" + std::to_string(player)
        + " transient=" + std::to_string(s_useTransientRecording.load() ? 1 : 0), true);
    return true;
}

bool StartPlayerRecording() {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    MacroWork work;
    if (!work) return false;
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (s_state.load(std::memory_order_acquire) != State::PreRecord) return false;
    const int player = ClampPlayer(s_recordPlayer.load(std::memory_order_acquire));
    std::lock_guard<std::mutex> lock(s_recordMutex);
    if (s_state.load(std::memory_order_acquire) != State::PreRecord) return false;

    Slot& recordSlot = ActiveRecordingSlot();
    recordSlot = Slot{};
    s_recLastMask = 0;
    s_recLastBuf = 0;
    s_recLastFacing = 0;
    s_recSpanTicks = 0;
    s_recPrevBufIdx = -1;
    s_frameDiv = 0;
    s_callsSinceDiv0 = 0;
    s_firstDiv0Seen = false;
    s_recordFinalizing = false;
    s_recordingCaptureSuspended.store(false, std::memory_order_release);

    uintptr_t playerPtr = MacroPlayerPointer(player);
    uint16_t startIdx = 0;
    if (playerPtr && SafeReadMemory(playerPtr + INPUT_BUFFER_INDEX_OFFSET, &startIdx, sizeof(startIdx))) {
        recordSlot.bufStartIdx = startIdx;
        s_recPrevBufIdx = startIdx;
    }

    s_state.store(State::Recording, std::memory_order_release);
    if (!s_useTransientRecording.load(std::memory_order_acquire)) {
        const std::string label = "Macro: Recording";
        if (s_macroBannerId == -1) {
            s_macroBannerId = DirectDrawHook::AddPermanentMessage(label, RGB(255,80,80), kBannerX, kBannerY);
        } else {
            DirectDrawHook::UpdatePermanentMessage(s_macroBannerId, label, RGB(255,80,80));
        }
    }
    LogOut("[MACRO][REC] started player=P" + std::to_string(player)
        + " slot=" + std::to_string(s_useTransientRecording.load() ? 0 : s_curSlot.load()), true);
    LogPlayerBufferSnapshot(player, "start");
    LogPlayerImmediateSnapshot(player, "start");
    return true;
}

bool FinishPlayerRecording() {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    MacroWork work;
    if (!work) return false;
    if (g_onlineModeActive.load(std::memory_order_acquire)) {
        Stop();
        return false;
    }
    if (s_state.load(std::memory_order_acquire) != State::Recording) return false;
    const bool boundaryStop = s_recordStopRequested.exchange(false, std::memory_order_acq_rel);
    FinishRecording(!boundaryStop);
    return s_state.load(std::memory_order_acquire) == State::Idle;
}

void RequestRecordingStopAtBoundary() {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    MacroWork work;
    if (!work) return;
    if (s_state.load(std::memory_order_acquire) == State::Recording) {
        s_recordStopRequested.store(true, std::memory_order_release);
    }
}

void ToggleRecord() {
    const State st = s_state.load(std::memory_order_acquire);
    if (st == State::Idle) {
        (void)BeginPlayerRecording(2, true);
    } else if (st == State::PreRecord) {
        (void)StartPlayerRecording();
    } else if (st == State::Recording) {
        (void)FinishPlayerRecording();
    } else if (st == State::Replaying) {
        Stop();
        DirectDrawHook::AddMessage("Macro: Replay stopped", "MACRO", RGB(255,200,180), 1000, 0, 120);
    }
}

static bool StartPlayback(int playerNum, bool exclusiveInput, int startTick,
                          PlaybackStartMode startMode) {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    MacroWork work;
    if (!work) return false;
    if (g_onlineModeActive.load(std::memory_order_acquire)) return false;
    if (GetCurrentGamePhase() != GamePhase::Match || !AreCharactersInitialized()) return false;
    if (s_state.load(std::memory_order_acquire) != State::Idle) return false;

    Slot& slot = ActivePlaybackSlot();
    const bool useStream = !slot.macroStream.empty();
    if (!slot.hasData || (slot.spans.empty() && !useStream)) return false;
    if (useStream && std::any_of(
            slot.bufCountsPerTick.begin(), slot.bufCountsPerTick.end(),
            [](uint16_t count) {
                return !MacroController::PlaybackStartPolicy::
                    SupportedRawWriteCount(static_cast<int>(count));
            })) {
        LogOut("[MACRO][PLAY] admission rejected: a logical tick contains "
               "more than three raw input-buffer writes", true);
        return false;
    }
    if (startTick < 0) startTick = 0;
    const PlaybackStartPolicy::Decision startDecision = useStream
        ? PlaybackStartPolicy::Decide(
              startMode, startTick, static_cast<int>(slot.macroStream.size()))
        : PlaybackStartPolicy::Decision{
              startMode == PlaybackStartMode::Ordinary,
              false, startTick, startTick};
    if (!startDecision.valid) return false;

    const int player = ClampPlayer(playerNum);
    uintptr_t primePlayerPtr = 0;
    uint16_t primeBufferIndex = 0;
    if (startDecision.prime) {
        primePlayerPtr = MacroPlayerPointer(player);
        if (!primePlayerPtr || !SafeReadMemory(
                primePlayerPtr + INPUT_BUFFER_INDEX_OFFSET,
                &primeBufferIndex, sizeof(primeBufferIndex))) {
            LogOut("[MACRO][PLAY][PRIME] P" + std::to_string(player) +
                       " admission rejected: input buffer unavailable",
                   true);
            return false;
        }
    }
    if (IsScopedInputReserved(player)) {
        LogOut("[MACRO][OWNER] P" + std::to_string(player) +
                   " playback takeover rejected: input lane reserved",
               true);
        return false;
    }
    // Validate and acquire before clearing any lane. Failed admission must not
    // destroy the competing owner it just discovered, and P1 playback must not
    // touch P2's Practice-dummy state.
    if (player == 2) {
        if (!AcquireP2MacroControlOwnership("playback")) return false;
    } else {
        if (IsAutoActionNormalPulseActive(player) ||
            IsAutoActionNormalPulseOwningImmediateRegisters(player) ||
            ImmediateInput::GetCurrentDesired(player) != 0 ||
            ImmediateInput::GetRemainingTicks(player) != 0) {
            LogOut("[MACRO][OWNER] P1 playback takeover rejected: input lane busy",
                   true);
            return false;
        }
    }
    s_activePlayer = s_scopeRequired ? MacroPlayerPointer(player) : 0;
    ResetPlaybackCursor();
    ClearPlaybackLane(player, true);
    s_playPlayer.store(player, std::memory_order_release);
    s_exclusiveReplay.store(exclusiveInput, std::memory_order_release);
    s_playStreamIndex = static_cast<size_t>(startDecision.tickToPrepare);
    if (startTick > 0 && !slot.bufCountsPerTick.empty()) {
        size_t skippedWrites = 0;
        const size_t stop = (std::min)(static_cast<size_t>(startTick),
                                       slot.bufCountsPerTick.size());
        for (size_t i = 0; i < stop; ++i) skippedWrites += slot.bufCountsPerTick[i];
        s_playBufStreamIndex = (std::min)(skippedWrites, slot.bufStream.size());
    }

    if (GetCurrentGameMode() == GameMode::Practice) {
        // Pull local control to P1 unless the target is the dummy slot after a swap.
        const int curLocal = SwitchPlayers::GetLocalSide();
        if (curLocal == 0 || curLocal == 1) s_prevLocalSide.store(curLocal);
        if (curLocal != 0 && player != SwitchPlayers::GetRemotePlayerIndex()) {
            SwitchPlayers::SetLocalSide(0);
        }
    }

    if (!slot.macroStream.empty() && !slot.spans.empty()) {
        const size_t total = slot.macroStream.size();
        s_streamFacingPerTick.reserve(total);
        for (const auto& sp : slot.spans) {
            for (int t = 0; t < sp.ticks && s_streamFacingPerTick.size() < total; ++t) {
                s_streamFacingPerTick.push_back(sp.facing);
            }
            if (s_streamFacingPerTick.size() >= total) break;
        }
        while (s_streamFacingPerTick.size() < total) s_streamFacingPerTick.push_back(0);
    }

    ResetInputPollOverrideHitCount(player);
    if (startDecision.prime) {
        PreparedStreamTick prepared;
        if (!PrepareNextStreamTick(
                slot, player, primePlayerPtr, &prepared,
                /*applyFullSnapshot=*/false) ||
            s_playStreamIndex != static_cast<size_t>(
                startDecision.cursorAfterAdmission)) {
            // Defensive fail-closed: return the acquired lane/controller.
            ClearPlaybackLane(player, true);
            ResetPlaybackCursor();
            s_exclusiveReplay.store(false, std::memory_order_release);
            if (player == 2 &&
                g_p2ControlOverridden.load(std::memory_order_acquire)) {
                RestoreP2ControlState();
            }
            {
                // Undo the side capture; don't leave s_prevLocalSide dangling.
                const int prev = s_prevLocalSide.load();
                if (prev == 0 || prev == 1) SwitchPlayers::SetLocalSide(prev);
                s_prevLocalSide.store(-1);
            }
            LogOut("[MACRO][PLAY][PRIME] internal preparation failed", true);
            return false;
        }
        if (!prepared.snapshotRebasedBufferIndex) {
            s_lastSeenBufIdx = primeBufferIndex;
        }
        s_playbackBufIdxSyncInitialized = true;
        const PublishedStreamFrame published = PublishPreparedStreamFrame(
            slot, player, /*bufferSubframesLeft=*/3,
            /*allowEndLatch=*/false);
        // Keep tick zero in control for the remaining two cadence slots.
        s_primedTickSubframesRemaining = 2;
        const int queuedWrites = static_cast<int>(
            s_tickBufQueue.size() -
            (std::min)(s_tickBufHead, s_tickBufQueue.size()));
        s_primedTickDrainPending =
            PlaybackStartPolicy::PrimedTickPending(
                s_primedTickSubframesRemaining, queuedWrites,
                static_cast<int>(s_writesLeftThisTick));
        s_primedTickPollHitsAtLatch =
            GetInputPollOverrideHitCount(player);
        s_primedTickPollHitsAtLastPublish =
            s_primedTickPollHitsAtLatch;
        s_primedTickAwaitingFirstPoll = true;
        LogOut("[MACRO][PLAY][PRIME] P" + std::to_string(player) +
                   " tick=" + std::to_string(prepared.tick) +
                   " mask=0x" + ToHexString(published.mask, 2) +
                   " next=" + std::to_string(s_playStreamIndex) +
                   " bufIdx=" + std::to_string(s_lastSeenBufIdx) +
                   " pollHits=" + std::to_string(
                       GetInputPollOverrideHitCount(player)),
               true);
    } else {
        // Neutral owner first; Tick loads byte 0 after the ring advances.
        g_pollOverrideMask[player].store(0, std::memory_order_relaxed);
        g_pollOverrideActive[player].store(true, std::memory_order_release);
        g_forceBypass[player].store(false, std::memory_order_relaxed);
        g_injectImmediateOnly[player].store(false,
                                             std::memory_order_relaxed);
    }
    s_state.store(State::Replaying, std::memory_order_release);

    const std::string label = exclusiveInput
        ? "DEMONSTRATION | ESC / MENU = STOP"
        : "Macro: Replaying";
    if (s_macroBannerId == -1) {
        s_macroBannerId = DirectDrawHook::AddPermanentMessage(label, RGB(120,255,120), kBannerX, kBannerY);
    } else {
        DirectDrawHook::UpdatePermanentMessage(s_macroBannerId, label, RGB(120,255,120));
    }

    int totalTicks = 0;
    for (const auto& sp : slot.spans) totalTicks += sp.ticks;
    const int slotNum = s_useTransientPlayback.load(std::memory_order_acquire) ? 0 : s_curSlot.load();
    LogOut("[MACRO][PLAY] started slot=" + std::to_string(slotNum)
        + " player=P" + std::to_string(player)
        + " ticks=" + std::to_string(totalTicks)
        + " streamBytes=" + std::to_string(slot.macroStream.size())
        + " startMode=" +
            std::string(startDecision.prime ? "prime" : "ordinary")
        + " exclusive=" + std::to_string(exclusiveInput ? 1 : 0), true);
    return true;
}

void Play() {
    if (s_state.load(std::memory_order_acquire) == State::PreRecord) {
        UnswapThenStop();
        DirectDrawHook::AddMessage("Macro: PreRecord canceled", "MACRO", RGB(255, 200, 120), 900, 0, 120);
        return;
    }
    if (s_state.load(std::memory_order_acquire) == State::Recording) {
        DirectDrawHook::AddMessage("Macro: Cannot play while recording", "MACRO", RGB(255, 180, 120), 900, 0, 120);
        return;
    }
    if (s_state.load(std::memory_order_acquire) != State::Idle) return;
    if (!PlayForPlayer(2, 0, false)) {
        DirectDrawHook::AddMessage("Macro: Slot empty or unavailable", "MACRO", RGB(255,120,120), 1000, 0, 120);
    }
}

void PlayFromTick(int startTick) {
    if (s_state.load(std::memory_order_acquire) != State::Idle) return;
    (void)PlayForPlayer(2, startTick, false);
}

bool PlayForPlayer(int playerNum, int startTick, bool exclusiveInput) {
    if (s_state.load(std::memory_order_acquire) != State::Idle) return false;
    s_useTransientPlayback.store(false, std::memory_order_release);
    return StartPlayback(playerNum, exclusiveInput, startTick,
                         PlaybackStartMode::Ordinary);
}

void Stop() {
    // Before online publication: full cleanup; after: bookkeeping-only.
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    MacroWork work;
    if (!work) return;
    const bool online = g_onlineModeActive.load(std::memory_order_acquire);
    State st = s_state.load();
    if (st == State::Idle) {
        // Idle Stop is not an ownership claim; never clear another producer's lane.
        s_recordStopRequested.store(false, std::memory_order_release);
        s_exclusiveReplay.store(false, std::memory_order_release);
        s_useTransientPlayback.store(false, std::memory_order_release);
        s_recordingCaptureSuspended.store(false, std::memory_order_release);
        if (s_macroBannerId != -1) {
            DirectDrawHook::RemovePermanentMessage(s_macroBannerId);
            s_macroBannerId = -1;
        }
        return;
    }
    const int playPlayer = ClampPlayer(s_playPlayer.load(std::memory_order_acquire));
    const int recordPlayer = ClampPlayer(s_recordPlayer.load(std::memory_order_acquire));
    const bool ownedP2Playback = st == State::Replaying && playPlayer == 2;
    const bool ownedP2Recording =
        (st == State::PreRecord || st == State::Recording) &&
        recordPlayer == 2 && s_recordOwnsP2Control;
    if (st == State::Recording && !online) FinishRecording();
    s_state.store(State::Idle);
    s_recordingCaptureSuspended.store(false, std::memory_order_release);
    s_recordStopRequested.store(false, std::memory_order_release);
    ResetPlaybackCursor();
    ClearPlaybackLane(playPlayer, !online);
    if ((st == State::Recording || st == State::PreRecord) &&
        recordPlayer != playPlayer) {
        ClearPlaybackLane(recordPlayer, !online);
    }
    if (!online) (void)ClearPlayerCommandFlags(playPlayer);
    if (!online && (st == State::Recording || st == State::PreRecord)) {
        (void)ClearPlayerCommandFlags(recordPlayer);
    }
    s_exclusiveReplay.store(false, std::memory_order_release);
    s_useTransientPlayback.store(false, std::memory_order_release);
    if (s_macroBannerId != -1) { DirectDrawHook::RemovePermanentMessage(s_macroBannerId); s_macroBannerId = -1; }
    if (g_p2ControlOverridden.load(std::memory_order_acquire) &&
        (ownedP2Playback || ownedP2Recording)) {
        RestoreP2ControlState();
    }
    int prev = s_prevLocalSide.load();
    if (!online && (prev == 0 || prev == 1)) {
        SwitchPlayers::SetLocalSide(prev);
    }
    s_prevLocalSide.store(-1);
    s_recordOwnsP2Control = false;
    s_recordSwitchesLocalControl = false;
}

bool BindPracticeWorld(const EfzTmEntryV1& world) {
    std::lock_guard<std::recursive_mutex> lock(g_p2ControlMutex);
    if (s_boundWorld.id.battleWorld) return SameMacroWorld(s_boundWorld, world) &&
        !s_practiceWorkClosed.load(std::memory_order_acquire);
    if (world.size != sizeof(world) || world.abiVersion != EFZ_TM_LIFECYCLE_ABI ||
        !world.id.battleWorld || !world.id.timeline || !world.player1 || !world.player2 ||
        world.nativeThreadId != GetCurrentThreadId() ||
        s_state.load() != State::Idle || s_exclusiveReplay.load()) return false;
    s_boundWorld = world;
    s_activePlayer = 0;
    s_scopeRequired = true;
    s_practiceWorkClosed.store(false, std::memory_order_release);
    return true;
}

void CancelPracticeWork() {
    // Only BindPracticeWorld reopens work; never close it without a bound world.
    if (s_boundWorld.id.battleWorld) s_practiceWorkClosed.store(true, std::memory_order_release);
}

uint32_t RetirePracticeWorld(const EfzTmEntryV1& world) {
    std::lock_guard<std::recursive_mutex> lock(g_p2ControlMutex);
    if (!s_boundWorld.id.battleWorld) return s_state.load() == State::Idle &&
        !s_exclusiveReplay.load() ? EFZ_TM_READY : EFZ_TM_FAULTED;
    if (!SameMacroWorld(s_boundWorld, world) || world.nativeThreadId != GetCurrentThreadId() ||
        g_onlineModeActive.load()) return EFZ_TM_FAULTED;
    if (!s_practiceWorkClosed.load()) return EFZ_TM_PENDING;
    const State state = s_state.load();
    const bool replay = state == State::Replaying || s_exclusiveReplay.load();
    const bool record = state == State::Recording || state == State::PreRecord;
    const int player = replay ? ClampPlayer(s_playPlayer.load()) : ClampPlayer(s_recordPlayer.load());
    if ((replay || record) && s_activePlayer != (player == 1 ? world.player1 : world.player2))
        return EFZ_TM_FAULTED;
    // Release only this still-active owner.
    if (replay || (record && player == 2 && s_recordOwnsP2Control)) {
        const bool flags = ClearPlayerCommandFlagsAt(s_activePlayer);
        const bool input = FullCleanupAfterToggleAt(s_activePlayer);
        if (!flags || !input) return EFZ_TM_PENDING;
    }
    if (state == State::Recording) FinishRecording(false, nullptr, true);
    if (replay) ClearPlaybackLane(player, false);
    s_state.store(State::Idle, std::memory_order_release);
    s_exclusiveReplay.store(false, std::memory_order_release);
    s_recordingCaptureSuspended.store(false, std::memory_order_release);
    s_recordStopRequested.store(false, std::memory_order_release);
    ResetPlaybackCursor();
    s_activePlayer = 0;
    s_boundWorld = {};
    // Saved side/banner cleanup is owned by controller/render receipts.
    return EFZ_TM_READY;
}

void ResetTimeline() {
    assert(s_state.load(std::memory_order_acquire) == State::Idle);
    ResetPlaybackCursor();
    s_recLastMask = 0;
    s_recSpanTicks = 0;
    s_recLastBuf = 0;
    s_recPrevBufIdx = -1;
    s_recLastFacing = 0;
    s_recLastReason = '?';
    s_recordStopRequested.store(false, std::memory_order_release);
    s_recordTickAdvanced.store(false, std::memory_order_release);
    s_recordingCaptureSuspended.store(false, std::memory_order_release);
    s_useTransientPlayback.store(false, std::memory_order_release);
    s_useTransientRecording.store(false, std::memory_order_release);
    s_recordFinalizing = false;
    s_freezeObservation = {};
    s_recordOwnsP2Control = false;
    s_recordSwitchesLocalControl = false;
    s_prevLocalSide.store(-1);
}

// Restore default Practice mapping first, then stop.
void UnswapThenStop() {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    MacroWork work;
    if (!work) return;
    // Prefer explicit unswap/reset in Practice mode so CS/menus are consistent
    if (!g_onlineModeActive.load(std::memory_order_acquire) &&
        GetCurrentGameMode() == GameMode::Practice) {
        SwitchPlayers::ResetControlMappingForMenusToP1();
    }
    Stop();
}

int GetCurrentSlot() { return s_curSlot.load(); }
void SetCurrentSlot(int slot) { s_curSlot.store(ClampSlot(slot)); }
int GetSlotCount() { return kMaxSlots; }
bool IsSlotEmpty(int slot) {
    slot = ClampSlot(slot) - 1;
    const Slot& s = s_slots[slot];
    bool hasAny = (!s.spans.empty() || !s.macroStream.empty());
    return !(s.hasData && hasAny);
}
MacroController::State GetState() { return s_state.load(); }
bool DidAdvanceRecordingTick() { return s_recordTickAdvanced.load(std::memory_order_acquire); }
void SetRecordingCaptureSuspended(bool suspended) {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    MacroWork work;
    if (!work) return;
    const bool previous = s_recordingCaptureSuspended.exchange(
        suspended, std::memory_order_acq_rel);
    if (previous == suspended) return;

    s_recordTickAdvanced.store(false, std::memory_order_release);
    if (!suspended && s_state.load(std::memory_order_acquire) == State::Recording) {
        // Rebase to the current head before the poll lease is released.
        std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
        const int player = ClampPlayer(
            s_recordPlayer.load(std::memory_order_acquire));
        uintptr_t playerPtr = MacroPlayerPointer(player);
        uint16_t index = 0;
        if (playerPtr && SafeReadMemory(
                playerPtr + INPUT_BUFFER_INDEX_OFFSET, &index, sizeof(index))) {
            s_recPrevBufIdx = index;
        }
        s_frameDiv = 0;
        s_callsSinceDiv0 = 0;
        s_firstDiv0Seen = false;
    }
    LogOut(std::string("[MACRO][REC] capture ") +
               (suspended ? "suspended" : "resumed at current buffer head"),
           true);
}
bool IsRecordingCaptureSuspended() {
    return s_recordingCaptureSuspended.load(std::memory_order_acquire);
}
bool IsExclusivePlayback() {
    return s_exclusiveReplay.load(std::memory_order_acquire);
}
int GetPlaybackPlayer() { return ClampPlayer(s_playPlayer.load(std::memory_order_acquire)); }
void ReleaseExclusivePlaybackHold() {
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    MacroWork work;
    if (!work) return;
    if (!s_exclusiveReplay.exchange(false, std::memory_order_acq_rel)) return;
    const int player = ClampPlayer(s_playPlayer.load(std::memory_order_acquire));
    g_pollOverrideMask[player].store(0, std::memory_order_relaxed);
    g_pollOverrideActive[player].store(false, std::memory_order_release);
    s_useTransientPlayback.store(false, std::memory_order_release);
}

std::string GetStatusLine() {
    std::ostringstream os;
    os << "Macro " << StateName(s_state.load()) << " | Slot " << s_curSlot.load();
    return os.str();
}

SlotStats GetSlotStats(int slot) {
    SlotStats stats{};
    slot = ClampSlot(slot) - 1;
    if (slot < 0) slot = 0; if (slot >= kMaxSlots) slot = kMaxSlots - 1;
    const Slot& s = s_slots[slot];
    stats.spanCount = static_cast<int>(s.spans.size());
    stats.totalTicks = 0; for (const auto& sp : s.spans) stats.totalTicks += sp.ticks;
    stats.bufEntries = static_cast<int>(s.bufStream.size());
    stats.bufTicks = static_cast<int>(s.bufCountsPerTick.size());
    stats.bufIndexTicks = static_cast<int>(s.bufIndexPerTick.size());
    stats.bufStartIdx = s.bufStartIdx;
    stats.bufEndIdx = s.bufEndIdx;
    stats.hasData = s.hasData && (!s.spans.empty() || !s.macroStream.empty());
    return stats;
}

int GetEffectiveTicks(int slot) {
    slot = ClampSlot(slot) - 1;
    if (slot < 0) slot = 0; if (slot >= kMaxSlots) slot = kMaxSlots - 1;
    const Slot& s = s_slots[slot];
    if (s.macroStream.empty()) return 0;
    // Scan backwards from end to find last non-neutral tick
    int lastAction = 0;
    for (int i = static_cast<int>(s.macroStream.size()) - 1; i >= 0; --i) {
        if (s.macroStream[i] != 0x00) {
            lastAction = i + 1; // tick count = last index + 1
            break;
        }
    }
    return lastAction;
}

int GetFirstButtonTick(int slot) {
    // 0-based tick of the first attack button; -1 if none. Wake timing must land it in the buffer window.
    constexpr uint8_t BUTTON_MASK = 0xF0; // A(0x10) | B(0x20) | C(0x40) | D(0x80)
    slot = ClampSlot(slot) - 1;
    if (slot < 0) slot = 0; if (slot >= kMaxSlots) slot = kMaxSlots - 1;
    const Slot& s = s_slots[slot];
    if (s.macroStream.empty()) return -1;
    for (int i = 0; i < static_cast<int>(s.macroStream.size()); ++i) {
        if (s.macroStream[i] & BUTTON_MASK) {
            return i;
        }
    }
    return -1;
}

uint8_t GetFirstAttackInput(int slot) {
    // Full mask at the first attack tick; 0 if none.
    constexpr uint8_t BUTTON_MASK = 0xF0; // A(0x10) | B(0x20) | C(0x40) | D(0x80)
    slot = ClampSlot(slot) - 1;
    if (slot < 0) slot = 0; if (slot >= kMaxSlots) slot = kMaxSlots - 1;
    const Slot& s = s_slots[slot];
    if (s.macroStream.empty()) return 0;
    for (size_t i = 0; i < s.macroStream.size(); ++i) {
        if (s.macroStream[i] & BUTTON_MASK) {
            return s.macroStream[i];
        }
    }
    return 0;
}

void NextSlot() {
    if (GetCurrentGamePhase() != GamePhase::Match || !AreCharactersInitialized()) return;
    int cur = s_curSlot.load();
    cur++; if (cur > kMaxSlots) cur = 1; s_curSlot.store(cur);
}
void PrevSlot() {
    if (GetCurrentGamePhase() != GamePhase::Match || !AreCharactersInitialized()) return;
    int cur = s_curSlot.load();
    cur--; if (cur < 1) cur = kMaxSlots; s_curSlot.store(cur);
}

} // namespace MacroController

namespace MacroController {

static std::string SerializeClip(const Slot& s, bool includeBuffers) {
    std::vector<uint8_t> ticks;
    std::vector<int8_t> faces; // recorded facing per tick (-1 left, +1 right, 0 unknown)
    if (!s.macroStream.empty()) {
        ticks = s.macroStream;
        if (!s.spans.empty()) {
            faces.reserve(ticks.size());
            for (const auto& sp : s.spans) {
                for (int t = 0; t < sp.ticks && faces.size() < ticks.size(); ++t) faces.push_back(sp.facing);
                if (faces.size() >= ticks.size()) break;
            }
            while (faces.size() < ticks.size()) faces.push_back(0);
        } else {
            faces.assign(ticks.size(), 0);
        }
    } else {
        for (const auto& sp : s.spans) {
            for (int t = 0; t < sp.ticks; ++t) {
                ticks.push_back(sp.mask);
                faces.push_back(sp.facing);
            }
        }
        if (faces.size() < ticks.size()) faces.resize(ticks.size(), 0);
    }
    std::ostringstream out;
    out << "EFZMACRO 1";
    if (ticks.empty()) return out.str();

    size_t bufPos = 0;
    auto buildBufToken = [&](size_t tickIndex) {
        std::ostringstream bt;
        uint16_t k = 0;
        if (tickIndex < s.bufCountsPerTick.size()) k = s.bufCountsPerTick[tickIndex];
        bt << "{" << k << ":";
        if (k > 0) bt << ' ';
        for (uint16_t i = 0; i < k && bufPos < s.bufStream.size(); ++i) {
            uint8_t v = s.bufStream[bufPos++];
            // Normalize buffer value to P1-facing if we know recorded facing for this tick
            int8_t f = (tickIndex < faces.size()) ? faces[tickIndex] : 0;
            if (f == -1) v = FlipMaskHoriz(v);
            std::string valTok = MaskToToken(v);
            // Hex for impossible combos (both U&D or L&R).
            bool u = (v & GAME_INPUT_UP) != 0, d = (v & GAME_INPUT_DOWN) != 0, l = (v & GAME_INPUT_LEFT) != 0, r = (v & GAME_INPUT_RIGHT) != 0;
            if ((u && d) || (l && r)) {
                bt << "0x" << ToHexString((int)v, 2);
            } else {
                bt << valTok;
            }
            if (i + 1 < k) bt << ' ';
        }
        bt << "}";
        return bt.str();
    };

    std::vector<std::string> perTick;
    perTick.reserve(ticks.size());
    for (size_t i = 0; i < ticks.size(); ++i) {
        uint8_t m = ticks[i];
        // Normalize to P1-facing in text: if recorded facing was left, flip 4/6 (and diagonals)
        int8_t f = (i < faces.size()) ? faces[i] : 0;
        if (f == -1) m = FlipMaskHoriz(m);
        std::string tok = MaskToToken(m);
        if (includeBuffers) {
            tok += ' ';
            tok += buildBufToken(i);
        }
        perTick.push_back(std::move(tok));
    }
    // RLE compress only identical full tokens
    std::ostringstream seq;
    seq << ' ';
    size_t i = 0;
    while (i < perTick.size()) {
        size_t j = i + 1;
        while (j < perTick.size() && perTick[j] == perTick[i]) ++j;
        size_t run = j - i;
        seq << perTick[i];
        if (run > 1) seq << 'x' << run;
        if (j < perTick.size()) seq << ' ';
        i = j;
    }
    out << seq.str();
    return out.str();
}

std::string SerializeSlot(int slot, bool includeBuffers) {
    slot = ClampSlot(slot);
    return SerializeClip(s_slots[slot - 1], includeBuffers);
}

std::string SerializeLastPlayerRecording(bool includeBuffers) {
    const Slot& clip = s_useTransientRecording.load(std::memory_order_acquire)
        ? s_transientRecordingSlot
        : s_slots[ClampSlot(s_curSlot.load()) - 1];
    return SerializeClip(clip, includeBuffers);
}

bool FinishPlayerRecordingAndSerialize(std::string& serializedOut,
                                       bool includeBuffers) {
    serializedOut.clear();
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    MacroWork work;
    if (!work) return false;
    if (g_onlineModeActive.load(std::memory_order_acquire)) {
        Stop();
        return false;
    }
    if (s_state.load(std::memory_order_acquire) != State::Recording) {
        return false;
    }

    const bool boundaryStop =
        s_recordStopRequested.exchange(false, std::memory_order_acq_rel);
    Slot sealedClip;
    FinishRecording(!boundaryStop, &sealedClip);
    if (s_state.load(std::memory_order_acquire) != State::Idle) {
        return false;
    }

    // Serialize the sealed copy, not the mutable slot selector.
    serializedOut = SerializeClip(sealedClip, includeBuffers);
    return true;
}

static void ClearSlotForImport(Slot& s) {
    s.spans.clear();
    s.macroStream.clear();
    s.bufStream.clear();
    s.bufCountsPerTick.clear();
    s.bufIndexPerTick.clear();
    s.tickReason.clear();
    s.immPerTick.clear();
    s.bufLatestPerTick.clear();
    s.fullBufferSnapshots.clear();
    s.bufStartIdx = 0;
    s.bufEndIdx = 0;
    s.hasData = false;
}

static void BuildSpansFromStream(Slot& s) {
    s.spans.clear();
    if (s.macroStream.empty()) return;
    uint8_t last = s.macroStream[0];
    int count = 1;
    for (size_t i = 1; i < s.macroStream.size(); ++i) {
        if (s.macroStream[i] == last) {
            ++count;
        } else {
            s.spans.push_back({ last, last, count, 0 });
            last = s.macroStream[i];
            count = 1;
        }
    }
    s.spans.push_back({ last, last, count, 0 });
}

// Imported macros: synthesize ring snapshots from index 0 (original indices unknown).
static void BuildSyntheticSnapshotsFromBuf(Slot& s) {
    s.fullBufferSnapshots.clear();
    s.bufIndexPerTick.clear();

    if (s.macroStream.empty()) return;

    // Counts must match the stream; else fall back to stream-only.
    if (s.bufCountsPerTick.size() != s.macroStream.size()) return;

    const size_t kBufSize = INPUT_BUFFER_SIZE;
    std::vector<uint8_t> cur(kBufSize, 0);
    uint16_t idx = 0; // synthetic tail index
    size_t bufPos = 0;

    s.fullBufferSnapshots.reserve(s.macroStream.size());
    s.bufIndexPerTick.reserve(s.macroStream.size());

    for (size_t t = 0; t < s.macroStream.size(); ++t) {
        const uint16_t k = s.bufCountsPerTick[t];
        for (uint16_t i = 0; i < k && bufPos < s.bufStream.size(); ++i) {
            cur[idx] = s.bufStream[bufPos++];
            idx = static_cast<uint16_t>((idx + 1) % kBufSize);
        }
        s.fullBufferSnapshots.push_back(cur);
        s.bufIndexPerTick.push_back(idx);
    }
}

static bool DeserializeClip(Slot& dst, const std::string& text, std::string& errorOut) {
    errorOut.clear();

    // Tokenize with brace-aware scanning
    std::vector<std::string> tokens;
    tokens.reserve(256);
    size_t n = text.size();
    size_t p = 0;
    auto skipSpace = [&](size_t& i){ while (i < n && std::isspace((unsigned char)text[i])) ++i; };
    skipSpace(p);
    // Optional header "EFZMACRO 1"
    if (p < n) {
        size_t hdrEnd = p;
        std::string t1, t2;
        while (hdrEnd < n && !std::isspace((unsigned char)text[hdrEnd])) ++hdrEnd;
        t1 = text.substr(p, hdrEnd - p);
        p = hdrEnd; skipSpace(p);
        hdrEnd = p; while (hdrEnd < n && !std::isspace((unsigned char)text[hdrEnd])) ++hdrEnd;
        t2 = text.substr(p, hdrEnd - p);
        if (!t1.empty() && !t2.empty() && (_stricmp(t1.c_str(), "EFZMACRO") == 0)) {
            if (t2 != "1") { errorOut = "Unsupported macro version: " + t2; return false; }
            p = hdrEnd;
        } else {
            p = 0;
        }
    }
    skipSpace(p);
    while (p < n) {
        if (std::isspace((unsigned char)text[p])) { ++p; continue; }
        size_t start = p;
        if (text[p] == '{') {
            // Should not start with group; groups attach to preceding tick token
            errorOut = "Unexpected '{' without preceding tick token";
            return false;
        }
        // Read until whitespace OR brace start (we'll include following group as part of this token pack)
        while (p < n && !std::isspace((unsigned char)text[p])) {
            if (text[p] == '{') break;
            ++p;
        }
        size_t baseEnd = p;
        // "5 {3: ...}" and "5{3: ...}" are equivalent packs.
        if (p < n && std::isspace((unsigned char)text[p])) {
            size_t q = p;
            while (q < n && std::isspace((unsigned char)text[q])) ++q;
            if (q < n && text[q] == '{') {
                p = q;
            }
        }
        // Capture any attached brace group including spaces inside until matching '}'
        int brace = 0;
        if (p < n && text[p] == '{') {
            brace = 1; ++p;
            while (p < n && brace > 0) {
                if (text[p] == '{') ++brace;
                else if (text[p] == '}') --brace;
                ++p;
            }
            if (brace != 0) { errorOut = "Unterminated buffer group"; return false; }
        }
        // Repeat suffix xN, attached or space-separated.
        if (p < n) {
            size_t qx = p;
            while (qx < n && std::isspace((unsigned char)text[qx])) ++qx;
            if (qx < n && (text[qx] == 'x' || text[qx] == 'X')) {
                size_t r = qx + 1;
                size_t rStart = r;
                while (r < n && std::isdigit((unsigned char)text[r])) ++r;
                if (r > rStart) {
                    p = r;
                }
            }
        }
        size_t end = p;
        tokens.push_back(text.substr(start, end - start));
        skipSpace(p);
    }

    if (tokens.empty()) {
        ClearSlotForImport(dst);
        return true;
    }

    std::vector<uint8_t> macro;
    std::vector<uint16_t> counts;
    std::vector<uint8_t> buf;

    auto parseUInt = [](const std::string& s, size_t i, uint32_t& out)->size_t{
        out = 0; size_t start = i; while (i < s.size() && std::isdigit((unsigned char)s[i])) { out = out*10 + (s[i]-'0'); ++i; }
        return (i > start) ? i : start;
    };

    for (size_t iTok = 0; iTok < tokens.size(); ++iTok) {
        const std::string& packFull = tokens[iTok];
        // Handle trailing repeat suffix xN or XN at end of pack (applies to both base-only and base+group forms)
        uint32_t repeat = 1;
        std::string pack = packFull;
        if (!pack.empty()) {
            size_t end = pack.size();
            size_t j = end;
            while (j > 0 && std::isdigit((unsigned char)pack[j - 1])) --j;
            if (j > 0 && j < end && (pack[j - 1] == 'x' || pack[j - 1] == 'X')) {
                uint32_t val = 0; size_t k = j; k = parseUInt(pack, k, val);
                if (k == j || val == 0) { errorOut = "Invalid repeat suffix in '" + pack + "'"; return false; }
                repeat = val;
                pack = pack.substr(0, j - 1);
                while (!pack.empty() && std::isspace((unsigned char)pack.back())) pack.pop_back();
            }
        }
        // Split into base and optional group on the adjusted pack (without trailing xN)
        size_t bracePos = pack.find('{');
        std::string base = (bracePos == std::string::npos) ? pack : pack.substr(0, bracePos);
        std::string group = (bracePos == std::string::npos) ? std::string() : pack.substr(bracePos);
        while (!base.empty() && std::isspace((unsigned char)base.back())) base.pop_back();
        std::string baseTok = base;
        uint8_t baseMask = 0;
        if (!TryTokenToMask(baseTok, baseMask)) { errorOut = "Bad tick token: '" + baseTok + "'"; return false; }

        // Optional group parsing {k: v1 v2 ...}
        std::vector<uint8_t> thisTickBuf;
        uint16_t thisTickK = 0;
        if (!group.empty()) {
            if (group.front() != '{') { errorOut = "Malformed buffer group in '" + packFull + "'"; return false; }
            if (group.back() != '}') { errorOut = "Malformed buffer group in '" + packFull + "'"; return false; }
            std::string inner = group.substr(1, group.size()-2);
            size_t q = 0; while (q < inner.size() && std::isspace((unsigned char)inner[q])) ++q;
            uint32_t kVal = 0; size_t q2 = parseUInt(inner, q, kVal);
            if (q2 == q) { errorOut = "Buffer group missing count in '" + pack + "'"; return false; }
            if (kVal > 65535 ||
                !MacroController::PlaybackStartPolicy::
                    SupportedRawWriteCount(static_cast<int>(kVal))) {
                errorOut = "Buffer group has more than three raw writes in '" +
                           pack + "'";
                return false;
            }
            while (q2 < inner.size() && std::isspace((unsigned char)inner[q2])) ++q2;
            if (q2 >= inner.size() || inner[q2] != ':') { errorOut = "Buffer group missing ':' in '" + pack + "'"; return false; }
            q = q2 + 1;
            while (q < inner.size()) {
                while (q < inner.size() && std::isspace((unsigned char)inner[q])) ++q;
                if (q >= inner.size()) break;
                size_t start = q; while (q < inner.size() && !std::isspace((unsigned char)inner[q])) ++q;
                std::string vtok = inner.substr(start, q - start);
                if (vtok.empty()) break;
                uint8_t vmask = 0;
                if (vtok.size() >= 3 && (vtok[0] == '0') && (vtok[1] == 'x' || vtok[1] == 'X')) {
                    uint32_t vv = 0;
                    std::stringstream ss; ss << std::hex << vtok; ss >> vv;
                    vmask = (uint8_t)(vv & 0xFF);
                } else {
                    if (!TryTokenToMask(vtok, vmask)) { errorOut = "Bad buffer value token: '" + vtok + "'"; return false; }
                }
                thisTickBuf.push_back(vmask);
            }
            thisTickK = (uint16_t)kVal;
            if (thisTickK != thisTickBuf.size()) {
                errorOut = "Buffer group count mismatch (k!=values) in '" + pack + "'";
                return false;
            }
        } else {
            // Default: one write equal to tick mask
            thisTickK = 1; thisTickBuf.push_back(baseMask);
        }

        for (uint32_t r = 0; r < repeat; ++r) {
            macro.push_back(baseMask);
            counts.push_back(thisTickK);
            buf.insert(buf.end(), thisTickBuf.begin(), thisTickBuf.end());
        }
    }

    ClearSlotForImport(dst);
    dst.macroStream = std::move(macro);
    dst.bufCountsPerTick = std::move(counts);
    dst.bufStream = std::move(buf);
    dst.hasData = !dst.macroStream.empty();
    BuildSpansFromStream(dst);
    if (kEnableFullBufferSnapshots) {
        BuildSyntheticSnapshotsFromBuf(dst);
    }
    return true;
}

bool DeserializeSlot(int slot, const std::string& text, std::string& errorOut) {
    slot = ClampSlot(slot);
    return DeserializeClip(s_slots[slot - 1], text, errorOut);
}

bool ValidateSerialized(const std::string& text, std::string& errorOut) {
    Slot parsed;
    if (!DeserializeClip(parsed, text, errorOut)) return false;
    if (!parsed.hasData) {
        errorOut = "Macro is empty";
        return false;
    }
    return true;
}

bool PlaySerializedForPlayer(const std::string& text, int playerNum,
                             bool exclusiveInput, std::string& errorOut,
                             PlaybackStartMode startMode) {
    Slot parsed;
    if (!DeserializeClip(parsed, text, errorOut)) return false;
    if (!parsed.hasData) {
        errorOut = "Demonstration is empty";
        return false;
    }
    std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex);
    if (g_onlineModeActive.load(std::memory_order_acquire)) {
        errorOut = "Demonstration cannot start during netplay";
        return false;
    }
    if (s_state.load(std::memory_order_acquire) != State::Idle) {
        errorOut = "Another macro session is active";
        return false;
    }

    s_transientPlaybackSlot = std::move(parsed);
    s_useTransientPlayback.store(true, std::memory_order_release);
    if (!StartPlayback(playerNum, exclusiveInput, 0, startMode)) {
        s_useTransientPlayback.store(false, std::memory_order_release);
        errorOut = "Demonstration cannot start in the current game state";
        return false;
    }
    return true;
}

} // namespace MacroController
