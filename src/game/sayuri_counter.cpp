#include "../include/game/sayuri_counter.h"

#include "../include/core/constants.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/game/frame_advantage.h"
#include "../include/game/game_state.h"
#include "../include/utils/utilities.h"   // GetEFZBase, g_featuresEnabled
#include "../include/utils/network.h"     // IsNetplaySuspendActive
#include "../include/input/input_hook.h"  // QueueAutoActionNormalPulse
#include "../include/input/normal_input_policy.h"

// Sole TU that includes the notation catalogue; every including TU copies its ~24KB tables.
#include "../include/game/mission/move_notation_tables.h"

#include <atomic>
#include <cstdint>
#include <cstring>   // _strnicmp, strcmp
#include <string>

// The modal popup keeps the raw items pointer across frames; never rebuild the list under an open popup.
namespace CustomMenu { namespace Screens { bool IsPopupActive(); } }

namespace SayuriCounter {

namespace {

// Index 0 unused; callers pass playerNum 1/2.
// File-scope atomics: the v141_xp build uses /Zc:threadSafeInit-, so function-local statics on monitor threads are not thread-safe.
// Zero-init lands on the wanted defaults; braced init of std::atomic arrays is not portable pre-C++17.
std::atomic<int> g_memoryChoice[3];   // MEMORY_OFF - presentation index only
std::atomic<int> g_cutterMode[3];     // CUTTER_NORMAL
std::atomic<int> g_autoCutter[3];     // AUTO_CUTTER_OFF
std::atomic<int> g_pickedMove[3];     // resolved opponent move ID, 0 = none

// One press per blockstun instance; a press consumed inside a freeze leaves no edge, so retry while the window is open.
int      g_cutterPressAttempts[3];
uint64_t g_cutterPressGeneration[3];
constexpr int kAutoCutterMaxAttempts = 3;

// Delay in visual frames (3 internal ticks each) from the first tick the window is seen open; the engine would accept a press on the very first tick.
std::atomic<int> g_autoCutterDelay[3];   // visual frames, 0 = earliest
int g_cutterWindowOpenTick[3] = { -1, -1, -1 };  // engine tick at first open
constexpr int kInternalTicksPerVisualFrame = 3;

// Biased +1 so 0 = nothing pinned; never write a pick unless the live opponent resolves to the same table.
std::atomic<int> g_pickedTableBias[3];

// Set on opponent change; stale indices at/past the fixed head read as OFF until the next publish.
std::atomic<int> g_selectionStale[3];

// LAST BLOCKED samples the opponent once per blocked hit (later states would overwrite the memory).
std::atomic<int> g_prevMoveId[3];
std::atomic<int> g_prevBlockstop[3];

// Menu/render thread only; Tick() never touches this.
constexpr int kFixedChoices = 3;    // OFF / NOTHING / LAST BLOCKED
constexpr int kMaxMoveChoices = 96;
constexpr int kMaxChoices = kFixedChoices + kMaxMoveChoices;
constexpr int kLabelLen = 64;       // longest catalogue notation is 54 chars

// 200..399 = the opponent's own attacks; below is movement/system states, above is the entity ring.
constexpr int kOpponentMoveIdMin = 200;
constexpr int kOpponentMoveIdMax = 399;

char        s_labels[3][kMaxChoices][kLabelLen];
const char* s_items[3][kMaxChoices];
int         s_moveId[3][kMaxChoices];
int         s_count[3];
int         s_tableIndex[3];        // index into kCharTables, or -1
bool        s_listAvailable[3];
bool        s_truncated[3];
char        s_oppName[3][20];       // opponent resource name the list was built from
bool        s_built[3];

const char* const kFixedLabels[kFixedChoices] = { "OFF", "NOTHING", "LAST BLOCKED" };

inline int ClampPlayer(int playerNum) {
    return (playerNum == 2) ? 2 : 1;
}

// Grounded blockstun only; air blockstun (156) and RG states sit outside the range.
inline bool IsGroundedBlockstun(int moveID) {
    return moveID >= STANDING_BLOCK_LVL1 && moveID <= CROUCHING_BLOCK_LVL2_B;
}

// Stricter than CharacterSettings::GetCharacterID (no partial-match fallback); no other resource name starts with "sayuri".
bool SideIsSayuri(uintptr_t base, uintptr_t baseOffset) {
    uintptr_t nameAddr = ResolvePointer(base, baseOffset, CHARACTER_NAME_OFFSET);
    if (!nameAddr) return false;
    char name[16] = {0};
    if (!SafeReadMemory(nameAddr, name, sizeof(name) - 1)) return false;
    name[sizeof(name) - 1] = '\0';
    return _strnicmp(name, "sayuri", 6) == 0;
}

// Read the name directly; the DisplayData mirror is only refreshed when the menu opens.
bool ReadOpponentName(uintptr_t base, uintptr_t oppOffset, char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    uintptr_t nameAddr = ResolvePointer(base, oppOffset, CHARACTER_NAME_OFFSET);
    if (!nameAddr) return false;
    char name[16] = {0};
    if (!SafeReadMemory(nameAddr, name, sizeof(name) - 1)) return false;
    name[sizeof(name) - 1] = '\0';
    if (name[0] == '\0') return false;
    strncpy_s(out, outSize, name, _TRUNCATE);
    return true;
}

int TableIndexForName(const char* name) {
    if (!name || !name[0]) return -1;
    for (int t = 0; t < Mission::MoveNames::kCharTableCount; ++t) {
        if (_stricmp(Mission::MoveNames::kCharTables[t].name, name) == 0) return t;
    }
    return -1;
}

int LiveOpponentTableIndex(uintptr_t base, int sayuriSide) {
    const uintptr_t oppOffset = (sayuriSide == 1) ? EFZ_BASE_OFFSET_P2
                                                  : EFZ_BASE_OFFSET_P1;
    char name[20] = {0};
    if (!ReadOpponentName(base, oppOffset, name, sizeof(name))) return -1;
    return TableIndexForName(name);
}

bool NoteContains(const char* note, const char* needle) {
    const size_t len = std::strlen(needle);
    for (const char* p = note; *p; ++p) {
        if (_strnicmp(p, needle, len) == 0) return true;
    }
    return false;
}

// Throws/command grabs produce no blockstun; they can never arm the memory.
bool NoteIsThrowOrGrab(const char* note) {
    return NoteContains(note, "throw") || NoteContains(note, "grab");
}

// Command token = text before the first space or bracket: "236A (S)" -> "236A".
void CommandToken(const char* note, char* out, size_t outSize) {
    size_t n = 0;
    for (const char* p = note; *p && n + 1 < outSize; ++p) {
        if (*p == ' ' || *p == '(') break;
        out[n++] = *p;
    }
    out[n] = '\0';
}

bool TokenEndsWith(const char* token, const char* suffix) {
    const size_t tl = std::strlen(token);
    const size_t sl = std::strlen(suffix);
    return tl >= sl && _stricmp(token + (tl - sl), suffix) == 0;
}

// S is the system button (stance/summon/jam/charge/FM/feint-roll): never causes blockstun; token-suffix match keeps Mio's "(S)" stance moves.
bool CommandUsesSystemButton(const char* token) {
    return TokenEndsWith(token, "S");
}

bool CommandIsMovement(const char* token) {
    return TokenEndsWith(token, "backdash") || TokenEndsWith(token, "fwddash");
}

// Leading motion digits: "41236" out of "41236A"; empty for "FM" or "throw".
void MotionDigits(const char* token, char* out, size_t outSize) {
    size_t n = 0;
    for (const char* p = token; *p >= '0' && *p <= '9' && n + 1 < outSize; ++p) {
        out[n++] = *p;
    }
    out[n] = '\0';
}

// A command throw has an automatic "success"/"catch" phase on the same motion; strikes spell it "hit", counter/parry follow-ups say "successful", rekka siblings carry ~ or /.
bool MotionHasThrowSuccessPhase(const Mission::MoveNames::CharTable& ct,
                                const char* digits) {
    using Mission::MoveNames::MoveRole;
    using Mission::MoveNames::HasRole;
    if (!digits[0]) return false;
    for (int i = 0; i < ct.count; ++i) {
        const Mission::MoveNames::MoveNote& s = ct.entries[i];
        if (!s.note || !HasRole(s.role, MoveRole::AutomaticPhase)) continue;
        if (NoteContains(s.note, "successful")) continue;
        if (!NoteContains(s.note, "success") && !NoteContains(s.note, "catch")) continue;
        char token[64];
        CommandToken(s.note, token, sizeof(token));
        if (std::strchr(token, '~') || std::strchr(token, '/')) continue;
        char sibling[32];
        MotionDigits(token, sibling, sizeof(sibling));
        if (sibling[0] && std::strcmp(sibling, digits) == 0) return true;
    }
    return false;
}

// Only entries that can put her in grounded blockstun are offered; projectile casts stay armable (arming reads the move ID on the blockstun entry frame).
bool EntryIsOfferable(const Mission::MoveNames::CharTable& ct,
                      const Mission::MoveNames::MoveNote& e) {
    using Mission::MoveNames::MoveRole;
    using Mission::MoveNames::HasRole;
    if (!HasRole(e.role, MoveRole::DirectInput) &&
        !HasRole(e.role, MoveRole::InputFollowup)) return false;
    if (e.id < kOpponentMoveIdMin || e.id > kOpponentMoveIdMax) return false;
    if (!e.note || !e.note[0]) return false;
    if (NoteIsThrowOrGrab(e.note)) return false;

    char token[64];
    CommandToken(e.note, token, sizeof(token));
    if (!token[0]) return false;
    if (CommandUsesSystemButton(token)) return false;
    if (CommandIsMovement(token)) return false;

    char digits[32];
    MotionDigits(token, digits, sizeof(digits));
    if (MotionHasThrowSuccessPhase(ct, digits)) return false;
    return true;
}

void BuildFixedOnly(int side) {
    for (int i = 0; i < kFixedChoices; ++i) {
        strncpy_s(s_labels[side][i], kLabelLen, kFixedLabels[i], _TRUNCATE);
        s_items[side][i] = s_labels[side][i];
        s_moveId[side][i] = 0;
    }
    s_count[side] = kFixedChoices;
    s_truncated[side] = false;
}

void BuildListForSide(int side, int tableIndex) {
    BuildFixedOnly(side);
    s_tableIndex[side] = tableIndex;
    s_listAvailable[side] = (tableIndex >= 0);
    s_built[side] = true;
    if (tableIndex < 0) return;

    const Mission::MoveNames::CharTable& ct = Mission::MoveNames::kCharTables[tableIndex];
    int n = kFixedChoices;
    for (int i = 0; i < ct.count; ++i) {
        const Mission::MoveNames::MoveNote& e = ct.entries[i];
        if (!EntryIsOfferable(ct, e)) continue;
        // Some characters have two moves under one notation (Ikumi's supers, Kanna's 236236B); the first wins and the row reports truncation.
        bool dupe = false;
        for (int k = kFixedChoices; k < n; ++k) {
            if (std::strcmp(s_items[side][k], e.note) == 0) { dupe = true; break; }
        }
        if (dupe) { s_truncated[side] = true; continue; }
        if (n >= kMaxChoices) { s_truncated[side] = true; break; }
        strncpy_s(s_labels[side][n], kLabelLen, e.note, _TRUNCATE);
        s_items[side][n] = s_labels[side][n];
        s_moveId[side][n] = (int)e.id;
        ++n;
    }
    s_count[side] = n;
}

void RefreshSide(int side, uintptr_t base) {
    const uintptr_t oppOffset = (side == 1) ? EFZ_BASE_OFFSET_P2
                                            : EFZ_BASE_OFFSET_P1;
    char name[20] = {0};
    if (!base || !ReadOpponentName(base, oppOffset, name, sizeof(name))) {
        // Drop the cached name so a degraded list cannot persist on a stale match.
        if (!s_built[side] || s_listAvailable[side] || s_oppName[side][0]) {
            BuildFixedOnly(side);
            s_oppName[side][0] = 0;
            s_tableIndex[side] = -1;
            s_listAvailable[side] = false;
            s_built[side] = true;
        }
        return;
    }
    if (s_built[side] && _stricmp(name, s_oppName[side]) == 0) return;

    strncpy_s(s_oppName[side], sizeof(s_oppName[side]), name, _TRUNCATE);
    BuildListForSide(side, TableIndexForName(name));

    // A pick only means something against the opponent it was made for; "236A" is a different attack on every character.
    g_pickedMove[side].store(0, std::memory_order_release);
    g_pickedTableBias[side].store(0, std::memory_order_release);
    g_selectionStale[side].store(1, std::memory_order_release);
    const int choice = g_memoryChoice[side].load(std::memory_order_acquire);
    if (choice >= kFirstMoveChoice) {
        g_memoryChoice[side].store(MEMORY_OFF, std::memory_order_release);
    }
}

bool ReadWord(uintptr_t base, uintptr_t baseOffset, uintptr_t offset, int& out) {
    uintptr_t addr = ResolvePointer(base, baseOffset, offset);
    if (!addr) return false;
    uint16_t v = 0;
    if (!SafeReadMemory(addr, &v, sizeof(v))) return false;
    out = (int)v;
    return true;
}

bool ReadDword(uintptr_t base, uintptr_t baseOffset, uintptr_t offset, uint32_t& out) {
    uintptr_t addr = ResolvePointer(base, baseOffset, offset);
    if (!addr) return false;
    return SafeReadMemory(addr, &out, sizeof(out));
}

bool WriteDword(uintptr_t base, uintptr_t baseOffset, uintptr_t offset, uint32_t value) {
    uintptr_t addr = ResolvePointer(base, baseOffset, offset);
    if (!addr) return false;
    return SafeWriteMemory(addr, &value, sizeof(value));
}

// Mirror of the engine's own arm flash; always use the longer of the engine's two countdown lengths.
void MirrorReadyFlash(uintptr_t base, uintptr_t baseOffset) {
    uintptr_t colorAddr = ResolvePointer(base, baseOffset, SAYURI_FLASH_COLOR_OFFSET);
    if (colorAddr) {
        uint8_t color = (uint8_t)SAYURI_FLASH_COLOR_WHITE;
        SafeWriteMemory(colorAddr, &color, sizeof(color));
    }
    WriteDword(base, baseOffset, SAYURI_FLASH_TIMER_OFFSET, (uint32_t)SAYURI_FLASH_TICKS);
}

void TickSide(int playerNum, uintptr_t base) {
    const int choice = g_memoryChoice[playerNum].load(std::memory_order_acquire);
    const int cutter = g_cutterMode[playerNum].load(std::memory_order_acquire);
    const int autoCutter = g_autoCutter[playerNum].load(std::memory_order_acquire);
    if (choice <= MEMORY_OFF && cutter == CUTTER_NORMAL && autoCutter == AUTO_CUTTER_OFF) return;

    const uintptr_t baseOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P1
                                                  : EFZ_BASE_OFFSET_P2;

    // SAFETY: both offsets are shared slots with different meanings on other characters; nothing below may run without this gate.
    if (!SideIsSayuri(base, baseOffset)) return;

    // Re-read the move ID here; hundreds of lines of other monitoring ran since the top of the iteration.
    int moveID = 0;
    if (!ReadWord(base, baseOffset, MOVE_ID_OFFSET, moveID)) return;
    const bool inBlockstun = IsGroundedBlockstun(moveID);

    // The poll runs several times per internal frame; the blockstun entry edge is never missed.
    const int prevMove = g_prevMoveId[playerNum].exchange(moveID, std::memory_order_acq_rel);
    const bool blockstunEntry = inBlockstun && !IsGroundedBlockstun(prevMove);
    int blockstopNow = 0;
    (void)ReadWord(base, baseOffset, BLOCKSTUN_OFFSET, blockstopNow);
    const int prevBlockstop = g_prevBlockstop[playerNum].exchange(blockstopNow, std::memory_order_acq_rel);
    const bool freshBlock = inBlockstun && blockstopNow > prevBlockstop;

    if (choice == MEMORY_NOTHING) {
        uint32_t stored = 0;
        if (ReadDword(base, baseOffset, SAYURI_COUNTER_MEMORY_OFFSET, stored) &&
            stored != (uint32_t)SAYURI_COUNTER_MEMORY_EMPTY) {
            WriteDword(base, baseOffset, SAYURI_COUNTER_MEMORY_OFFSET,
                       (uint32_t)SAYURI_COUNTER_MEMORY_EMPTY);
        }
    } else if (choice == MEMORY_LAST_BLOCKED) {
        // The engine compares on frame 0 of every blocked hit: a re-block in a string resets the animation, so it is a fresh entry too, seen here as blockstop rising.
        if (blockstunEntry || freshBlock) {
            const uintptr_t oppOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P2
                                                         : EFZ_BASE_OFFSET_P1;
            int oppMove = 0;
            if (ReadWord(base, oppOffset, MOVE_ID_OFFSET, oppMove) &&
                oppMove >= kOpponentMoveIdMin && oppMove <= kOpponentMoveIdMax) {
                uint32_t stored = 0;
                if (ReadDword(base, baseOffset, SAYURI_COUNTER_MEMORY_OFFSET, stored) &&
                    stored != (uint32_t)oppMove) {
                    WriteDword(base, baseOffset, SAYURI_COUNTER_MEMORY_OFFSET,
                               (uint32_t)oppMove);
                }
            }
        }
    } else if (choice >= kFirstMoveChoice) {
        const int picked = g_pickedMove[playerNum].load(std::memory_order_acquire);
        const int pickedTable = g_pickedTableBias[playerNum].load(std::memory_order_acquire) - 1;
        if (picked > 0 && pickedTable >= 0 &&
            pickedTable == LiveOpponentTableIndex(base, playerNum)) {
            uint32_t stored = 0;
            if (ReadDword(base, baseOffset, SAYURI_COUNTER_MEMORY_OFFSET, stored) &&
                stored != (uint32_t)picked) {
                WriteDword(base, baseOffset, SAYURI_COUNTER_MEMORY_OFFSET,
                           (uint32_t)picked);
            }
        }
    }

    // Outside these six states the armed flag is generic per-move scratch; only a gated write is safe.
    if (cutter == CUTTER_ALWAYS_READY && inBlockstun) {
        // Re-prove the gate with a fresh move ID immediately before the write.
        int guardMove = 0;
        if (!ReadWord(base, baseOffset, MOVE_ID_OFFSET, guardMove)) return;
        if (!IsGroundedBlockstun(guardMove)) return;

        uint32_t armed = 0;
        if (ReadDword(base, baseOffset, SAYURI_CUTTER_ARMED_OFFSET, armed) && armed != 1u) {
            if (WriteDword(base, baseOffset, SAYURI_CUTTER_ARMED_OFFSET, 1u)) {
                MirrorReadyFlash(base, baseOffset);
            }
        }
    }

    // The engine needs a RISING A/B/C edge while armed, blockstop over, and no opponent superflash; a press consumed inside a freeze leaves no edge.
    // Queue via the auto-action normal pulse (after EFZ's producer, before her consumer); the witnessed move is Magical Cutter (258).
    if (autoCutter == AUTO_CUTTER_ON) {
        int& attempts = g_cutterPressAttempts[playerNum];
        uint64_t& generation = g_cutterPressGeneration[playerNum];
        if (!inBlockstun) {
            attempts = 0;
            generation = 0;
            g_cutterWindowOpenTick[playerNum] = -1;
        } else {
            if (blockstunEntry) {
                attempts = 0;
                generation = 0;
                g_cutterWindowOpenTick[playerNum] = -1;
            }
            bool pulsePending = false;
            if (generation != 0) {
                if (GetAutoActionNormalPulseOutcome(playerNum, generation)
                        == AutoActionNormalPulseOutcome::Pending) {
                    pulsePending = true;
                } else {
                    // Accepted means she is in 258 and out of blockstun; reaching here means a lost press.
                    generation = 0;
                }
            }
            const uintptr_t oppOffset = (playerNum == 1) ? EFZ_BASE_OFFSET_P2
                                                         : EFZ_BASE_OFFSET_P1;
            uint32_t armed = 0;
            int blockstop = 0;
            int oppSuperflash = 0;
            const bool windowOpen =
                ReadDword(base, baseOffset, SAYURI_CUTTER_ARMED_OFFSET, armed) && armed == 1u &&
                ReadWord(base, baseOffset, BLOCKSTUN_OFFSET, blockstop) && blockstop == 0 &&
                ReadWord(base, oppOffset, SUPERFLASH_FREEZE_OFFSET, oppSuperflash) && oppSuperflash == 0;
            int& openTick = g_cutterWindowOpenTick[playerNum];
            const int nowTick = GetEngineTick();
            if (windowOpen && openTick < 0) openTick = nowTick;
            const int delayVisual = g_autoCutterDelay[playerNum].load(std::memory_order_acquire);
            const bool delayElapsed = windowOpen && openTick >= 0 &&
                (nowTick - openTick) >= delayVisual * kInternalTicksPerVisualFrame;
            if (!pulsePending && attempts < kAutoCutterMaxAttempts) {
                if (delayElapsed) {
                    NormalInputPolicy::Intent intent{};
                    intent.direction = NormalInputPolicy::RelativeDirection::Neutral;
                    intent.button = NormalInputPolicy::kInputA;
                    intent.airborne = false;
                    intent.expectedMove = SAYURI_MOVE_MAGICAL_CUTTER;
                    uint64_t submitted = 0;
                    const auto submit = QueueAutoActionNormalPulse(
                        playerNum, intent, NormalInputPolicy::Timing::Immediate, &submitted);
                    if (submit == NormalInputPolicy::SubmitResult::Accepted) {
                        generation = submitted;
                        ++attempts;
                        if (detailedLogging.load()) {
                            LogOut(std::string("[SAYURI] P") + std::to_string(playerNum) +
                                   " auto cutter press queued attempt=" + std::to_string(attempts) +
                                   " move=" + std::to_string(moveID) +
                                   " delayFrames=" + std::to_string(delayVisual) +
                                   " waitedTicks=" + std::to_string(nowTick - openTick), true);
                        }
                    } else if (submit == NormalInputPolicy::SubmitResult::Invalid) {
                        attempts = kAutoCutterMaxAttempts;
                    }
                }
            }
        }
    }
}

} // namespace

void RefreshMoveChoices() {
    if (CustomMenu::Screens::IsPopupActive()) return;
    uintptr_t base = GetEFZBase();
    RefreshSide(1, base);
    RefreshSide(2, base);
}

const char* const* MoveChoiceItems(int playerNum) {
    const int p = ClampPlayer(playerNum);
    if (!s_built[p]) BuildFixedOnly(p);
    return s_items[p];
}

int MoveChoiceCount(int playerNum) {
    const int p = ClampPlayer(playerNum);
    if (!s_built[p]) BuildFixedOnly(p);
    return (s_count[p] < kFixedChoices) ? kFixedChoices : s_count[p];
}

int MoveIdForChoice(int playerNum, int idx) {
    const int p = ClampPlayer(playerNum);
    if (idx < kFixedChoices || idx >= s_count[p]) return 0;
    return s_moveId[p][idx];
}

bool OpponentListAvailable(int playerNum) {
    return s_listAvailable[ClampPlayer(playerNum)];
}

bool MoveListTruncated(int playerNum) {
    return s_truncated[ClampPlayer(playerNum)];
}

int ValidateChoiceIndex(int playerNum, int idx) {
    const int p = ClampPlayer(playerNum);
    if (idx < 0) return MEMORY_OFF;
    if (idx < kFixedChoices) {
        // Clearing stale at the fixed head is what makes the first pick after a rebuild stick.
        g_selectionStale[p].store(0, std::memory_order_release);
        return idx;
    }
    // Past the fixed head, invalid indices collapse to OFF, never another move; the stale flag stays raised until the caller writes back.
    if (g_selectionStale[p].load(std::memory_order_acquire) != 0) return MEMORY_OFF;
    if (idx >= s_count[p]) return MEMORY_OFF;
    if (s_moveId[p][idx] == 0) return MEMORY_OFF;
    return idx;
}

void SetMemory(int playerNum, int choiceIndex, int resolvedMoveId) {
    const int p = ClampPlayer(playerNum);
    if (choiceIndex < MEMORY_OFF) choiceIndex = MEMORY_OFF;
    // No resolved move ID means the list changed underneath; fall back to OFF.
    if (choiceIndex >= kFirstMoveChoice && resolvedMoveId <= 0) {
        choiceIndex = MEMORY_OFF;
        resolvedMoveId = 0;
    }
    if (choiceIndex < kFirstMoveChoice) resolvedMoveId = 0;

    const int table = (resolvedMoveId > 0) ? s_tableIndex[p] : -1;
    g_pickedMove[p].store(resolvedMoveId, std::memory_order_release);
    g_pickedTableBias[p].store(table + 1, std::memory_order_release);
    g_selectionStale[p].store(0, std::memory_order_release);

    const int prev = g_memoryChoice[p].exchange(choiceIndex, std::memory_order_acq_rel);
    if (prev != choiceIndex) {
        LogOut(std::string("[SAYURI] P") + std::to_string(p) +
               " remembered move -> index " + std::to_string(choiceIndex) +
               " (moveID " + std::to_string(resolvedMoveId) + ")", true);
    }
}

void SetCutter(int playerNum, int mode) {
    const int p = ClampPlayer(playerNum);
    if (mode < CUTTER_NORMAL || mode >= CUTTER_COUNT) mode = CUTTER_NORMAL;
    const int prev = g_cutterMode[p].exchange(mode, std::memory_order_acq_rel);
    if (prev != mode) {
        LogOut(std::string("[SAYURI] P") + std::to_string(p) +
               " magical cutter -> " +
               (mode == CUTTER_ALWAYS_READY ? "ALWAYS READY" : "NORMAL"), true);
    }
}

void SetAutoCutter(int playerNum, int mode) {
    const int p = ClampPlayer(playerNum);
    if (mode < AUTO_CUTTER_OFF || mode >= AUTO_CUTTER_COUNT) mode = AUTO_CUTTER_OFF;
    const int prev = g_autoCutter[p].exchange(mode, std::memory_order_acq_rel);
    if (prev != mode) {
        LogOut(std::string("[SAYURI] P") + std::to_string(p) +
               " auto cutter -> " + (mode == AUTO_CUTTER_ON ? "ON" : "OFF"), true);
    }
}

void SetAutoCutterDelay(int playerNum, int visualFrames) {
    const int p = ClampPlayer(playerNum);
    if (visualFrames < 0) visualFrames = 0;
    if (visualFrames > kAutoCutterDelayMax) visualFrames = kAutoCutterDelayMax;
    const int prev = g_autoCutterDelay[p].exchange(visualFrames, std::memory_order_acq_rel);
    if (prev != visualFrames) {
        LogOut(std::string("[SAYURI] P") + std::to_string(p) +
               " auto cutter delay -> " + std::to_string(visualFrames) + " frames", true);
    }
}

int GetAutoCutterDelay(int playerNum) {
    return g_autoCutterDelay[ClampPlayer(playerNum)].load(std::memory_order_acquire);
}

int GetMemory(int playerNum) {
    return g_memoryChoice[ClampPlayer(playerNum)].load(std::memory_order_acquire);
}

int GetAutoCutter(int playerNum) {
    return g_autoCutter[ClampPlayer(playerNum)].load(std::memory_order_acquire);
}

int GetCutter(int playerNum) {
    return g_cutterMode[ClampPlayer(playerNum)].load(std::memory_order_acquire);
}

void ResetState() {
    for (int p = 1; p <= 2; ++p) {
        g_pickedMove[p].store(0, std::memory_order_release);
        g_pickedTableBias[p].store(0, std::memory_order_release);
        g_selectionStale[p].store(0, std::memory_order_release);
        g_prevMoveId[p].store(0, std::memory_order_release);
        g_cutterPressAttempts[p] = 0;
        g_cutterPressGeneration[p] = 0;
        g_cutterWindowOpenTick[p] = -1;
    }
}

void Tick() {
    // Same offline-Practice gates as auto_airtech and doppel_tech.
    if (!g_featuresEnabled.load()) return;
    if (GetCurrentGameMode() != GameMode::Practice) return;
    if (IsNetplaySuspendActive()) return;

    if (g_memoryChoice[1].load(std::memory_order_acquire) == MEMORY_OFF &&
        g_memoryChoice[2].load(std::memory_order_acquire) == MEMORY_OFF &&
        g_cutterMode[1].load(std::memory_order_acquire) == CUTTER_NORMAL &&
        g_cutterMode[2].load(std::memory_order_acquire) == CUTTER_NORMAL &&
        g_autoCutter[1].load(std::memory_order_acquire) == AUTO_CUTTER_OFF &&
        g_autoCutter[2].load(std::memory_order_acquire) == AUTO_CUTTER_OFF) {
        return;
    }

    uintptr_t base = GetEFZBase();
    if (!base) return;

    // Switching players is a CONTROL swap only; a Sayuri in the P1 slot keeps P1 behaviour.
    // A suspected engine P1/P2 arming asymmetry (docs/SAYURI_COUNTER_MEMORY.md S5) is unresolved; do not model it without a live trace.
    TickSide(1, base);
    TickSide(2, base);
}

} // namespace SayuriCounter
