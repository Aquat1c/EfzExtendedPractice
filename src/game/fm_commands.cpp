#include "../include/game/game_state.h"
#include "../include/core/constants.h"
#include "../include/core/memory.h"
#include "../include/core/logger.h"
#include "../include/input/input_core.h"
#include "../include/input/input_freeze.h"
#include "../include/input/auto_action_motion_transaction.h"
#include "../include/game/fm_commands.h"
#include "../include/utils/utilities.h"
#include <vector>
#include <string>
#include <sstream>

static std::string MaskToToken(uint8_t m) {
    bool up = (m & GAME_INPUT_UP) != 0;
    bool down = (m & GAME_INPUT_DOWN) != 0;
    bool left = (m & GAME_INPUT_LEFT) != 0;
    bool right = (m & GAME_INPUT_RIGHT) != 0;
    int num = 5;
    if (down && left)  num = 1;
    else if (down && right) num = 3;
    else if (up && left)    num = 7;
    else if (up && right)   num = 9;
    else if (down)          num = 2;
    else if (up)            num = 8;
    else if (left)          num = 4;
    else if (right)         num = 6;
    std::string out = std::to_string(num);
    if (m & GAME_INPUT_A) out += 'A';
    if (m & GAME_INPUT_B) out += 'B';
    if (m & GAME_INPUT_C) out += 'C';
    if (m & GAME_INPUT_D) out += 'D';
    return out;
}

static std::string DescribePattern(const std::vector<uint8_t>& pat) {
    std::ostringstream oss; oss << '[';
    for (size_t i=0;i<pat.size();++i) {
        if (i) oss << ' ';
        oss << MaskToToken(pat[i]);
    }
    oss << ']';
    return oss.str();
}

static bool GateAlways(int) { return true; }
static bool GateKanoRG(int playerNum) {
    uintptr_t base = GetPlayerPointer(playerNum);
    if (!base) return false;
    short moveId = 0; SafeReadMemory(ResolvePointer(GetEFZBase(), (playerNum==1?EFZ_BASE_OFFSET_P1:EFZ_BASE_OFFSET_P2), MOVE_ID_OFFSET), &moveId, sizeof(moveId));
    return (moveId == RG_STAND_ID || moveId == RG_CROUCH_ID || moveId == RG_AIR_ID);
}

// Placeholder: always allows; Mai awakening state not yet read.
static bool GateMaiAwakening(int) { return true; }

static const int DEFAULT_DIR_FRAMES = 4;
static const int DEFAULT_BTN_FRAMES = 6;

struct ParsedToken {
    uint8_t mask{0};
    int repeats{0};
};

static ParsedToken ParseTokenSpec(const std::string& raw, bool facingRight) {
    ParsedToken pt; if (raw.empty()) { pt.mask = GAME_INPUT_NEUTRAL; pt.repeats = DEFAULT_DIR_FRAMES; return pt; }
    std::string core = raw; std::string repeatStr;
    size_t star = raw.find('*');
    if (star != std::string::npos) { core = raw.substr(0, star); repeatStr = raw.substr(star+1); }
    uint8_t dir = GAME_INPUT_NEUTRAL;
    if (!core.empty()) {
        char c = core[0];
        switch (c) {
            case '1': dir = GAME_INPUT_DOWN | (facingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT); break;
            case '2': dir = GAME_INPUT_DOWN; break;
            case '3': dir = GAME_INPUT_DOWN | (facingRight ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT); break;
            case '4': dir = (facingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT); break;
            case '5': dir = GAME_INPUT_NEUTRAL; break;
            case '6': dir = (facingRight ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT); break;
            case '7': dir = GAME_INPUT_UP | (facingRight ? GAME_INPUT_LEFT : GAME_INPUT_RIGHT); break;
            case '8': dir = GAME_INPUT_UP; break;
            case '9': dir = GAME_INPUT_UP | (facingRight ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT); break;
            case 'N': dir = GAME_INPUT_NEUTRAL; break;
            default: break;
        }
    }
    uint8_t btn = 0;
    for (size_t i = 0; i < core.size(); ++i) {
        char c = core[i];
        if (c >= '1' && c <= '9') continue;
        if (c == 'N' || c=='n') continue;
        if (c=='A') btn |= GAME_INPUT_A; else if (c=='B') btn |= GAME_INPUT_B; else if (c=='C') btn |= GAME_INPUT_C; else if (c=='S' || c=='D') btn |= GAME_INPUT_D;
    }
    pt.mask = dir | btn;
    if (!repeatStr.empty()) {
        int parsed = 1;
        try { parsed = std::stoi(repeatStr); } catch (...) { parsed = 1; }
        if (parsed < 1) parsed = 1;
        pt.repeats = parsed;
    } else {
        bool hasBtn = (btn != 0);
        pt.repeats = hasBtn ? DEFAULT_BTN_FRAMES : DEFAULT_DIR_FRAMES;
    }
    return pt;
}

std::vector<uint8_t> BuildPattern(const std::vector<const char*>& tokens, bool facingRight) {
    std::vector<uint8_t> out; out.reserve(tokens.size()*DEFAULT_BTN_FRAMES);
    for (auto* rawTok : tokens) {
        std::string t(rawTok);
        ParsedToken pt = ParseTokenSpec(t, facingRight);
        for (int i=0;i<pt.repeats;++i) out.push_back(pt.mask);
    }
    return out;
}

const std::vector<FinalMemoryCommand>& GetFinalMemoryCommands() {
    static std::vector<FinalMemoryCommand> cmds; static bool built=false; if (built) return cmds; built=true;
    bool facingRight = true;
    cmds.push_back({CHAR_ID_AKANE, "Akane", BuildPattern({"5A","5*3","5A","5*3","6*3","5B","5*3","5C"}, facingRight), GateAlways, nullptr});
    // Akiko: needs neutral release windows before each neutral button.
    cmds.push_back({CHAR_ID_AKIKO, "Akiko", BuildPattern({"2*6","5*3","5A*6","2*6","5*3","5B*6","5*3","5C*6"}, facingRight), GateAlways, "Provisional release windows"});
    cmds.push_back({CHAR_ID_AYU, "Ayu", BuildPattern({"5A*2","5*3","5A*2","5*3","4*3","5*3","5B*2","5*3","5C*2"}, facingRight), GateAlways, nullptr});
    cmds.push_back({CHAR_ID_IKUMI, "Ikumi", BuildPattern({"2","3","6","2","3","6","2","3","6","5C"}, facingRight), GateAlways, nullptr});
    // Kaori 666S: sustained forward holds required for recognition.
        cmds.push_back({CHAR_ID_KAORI, "Kaori", BuildPattern({"6*6","5*3","6*6","5*3","6*6","6S*6"}, facingRight), GateAlways, nullptr});
    cmds.push_back({CHAR_ID_KANNA, "Kanna", BuildPattern({"2","3","6","2","3","6","5C"}, facingRight), GateAlways, nullptr});
    cmds.push_back({CHAR_ID_KANO, "Kano", BuildPattern({"2","1","4","1","2","3","6","S"}, facingRight), GateKanoRG, "Requires recoil guard state"});
    // Mai: two awakening contexts, same input (context not enforced).
    cmds.push_back({CHAR_ID_MAI, "Mai", BuildPattern({"2","3","6","2","3","6","S"}, facingRight), GateMaiAwakening, "Awakening context not enforced"});
    cmds.push_back({CHAR_ID_MAKOTO, "Makoto", BuildPattern({"2","6","3","S"}, facingRight), GateAlways, nullptr});
    cmds.push_back({CHAR_ID_MAYU, "Mayu", BuildPattern({"2","3","6","9","3","S"}, facingRight), GateAlways, nullptr});
    cmds.push_back({CHAR_ID_MINAGI, "Minagi", BuildPattern({"2*3","5*3","2*3","5*3","2*3","2S*6"}, facingRight), GateAlways, "Provisional hold timing"});
    cmds.push_back({CHAR_ID_MIO, "MioShort", BuildPattern({
        "6*3","4*3","1*3","2*3","3*3","6*3",
        "5C*2"
    }, facingRight), GateAlways, "Short variant refined from buffer"});
    cmds.push_back({CHAR_ID_MIO, "MioLong", BuildPattern({
        "5B*2","5*3","5A*2","5*3","2*3","5*3","5A*2","5*3","5B*2"
    }, facingRight), GateAlways, "Long stance variant"});
    cmds.push_back({CHAR_ID_MISAKI, "Misaki", BuildPattern({"2*3","5*3","2*3","5*3","2*3","2S*6"}, facingRight), GateAlways, nullptr});
    cmds.push_back({CHAR_ID_MISHIO, "Mishio", BuildPattern({
        "5B*2","5*3","5B*2","5*3","2*3","5*3","5C*2","5*3","5A*2"
    }, facingRight), GateAlways, "Refined from buffer"});
    cmds.push_back({CHAR_ID_MISUZU, "Misuzu", BuildPattern({"5A","5*3","5A","5*3","2B","5*3","2C"}, facingRight), GateAlways, nullptr});
    // *1 = single-frame button presses; trailing neutrals keep the final
    // sequence recent for the FM detector.
    {
        FinalMemoryCommand fm {CHAR_ID_MIZUKA, "Mizuka", BuildPattern({
            "6*3","5*2","5B*1","5*5","5A*1","5*5","5A*1","5*5","6*3","5*5","5A*1"
        }, facingRight), GateAlways, "Refined from CE loop (single-frame presses)", 4};
        cmds.push_back(fm);
    }
    cmds.push_back({CHAR_ID_NAYUKI, "NeyukiSleep", BuildPattern({"5C","5*3","2","3","6","2","3","6"}, facingRight), GateAlways, nullptr});
    cmds.push_back({CHAR_ID_NAYUKIB, "NayukiAwake", BuildPattern({"2","3","6","2","3","6","S"}, facingRight), GateAlways, nullptr});
    cmds.push_back({CHAR_ID_NANASE, "Rumi", BuildPattern({"4","1","2","3","6","4","1","2","3","6","S"}, facingRight), GateAlways, nullptr});
    cmds.push_back({CHAR_ID_SAYURI, "Sayuri", BuildPattern({"2*6","5*3","2A*6","5*3","5S*6"}, facingRight), GateAlways, "Added neutral gaps"});
    cmds.push_back({CHAR_ID_SHIORI, "Shiori", BuildPattern({"2","3","6","2","3","6","5B","5C"}, facingRight), GateAlways, nullptr});
    cmds.push_back({CHAR_ID_UNKNOWN, "Unknown", BuildPattern({
        "5C*2","5*3","5B*2","5*3","6*3","5*3","5A*2","5*3","5A*2"
    }, facingRight), GateAlways, "Refined from buffer"});
    return cmds;
}

bool ExecuteFinalMemory(int playerNum, int characterId,
                        int consumerWaitPasses, uint64_t* generationOut,
                        P2AutoActionMotionSubmitResult* submitResultOut) {
    if (generationOut) *generationOut = 0;
    if (submitResultOut) {
        *submitResultOut = P2AutoActionMotionSubmitResult::Invalid;
    }
    uintptr_t playerPtr = GetPlayerPointer(playerNum);
    if (!playerPtr) return false;
    bool facingRight = GetPlayerFacingDirection(playerNum);
    std::string forcedName;
    if (characterId == CHAR_ID_MIO) {
        uintptr_t efzBase = GetEFZBase();
        uintptr_t playerBase = 0;
        SafeReadMemory(efzBase + (playerNum==1?EFZ_BASE_OFFSET_P1:EFZ_BASE_OFFSET_P2), &playerBase, sizeof(uintptr_t));
        if (playerBase) {
            int stance=0; SafeReadMemory(playerBase + MIO_STANCE_OFFSET, &stance, sizeof(int));
            stance = (stance==MIO_STANCE_LONG)?MIO_STANCE_LONG:MIO_STANCE_SHORT;
            forcedName = (stance==MIO_STANCE_LONG)?"MioLong":"MioShort";
        }
    }
    const auto& list = GetFinalMemoryCommands();
    for (const auto& c : list) {
        if (c.characterId == characterId) {
            if (!forcedName.empty() && c.name != forcedName) continue;
            if (c.gate && !c.gate(playerNum)) {
                std::string msg = std::string("[FM] Gate failed for ") + c.name;
                if (c.gateDesc) { msg += " ("; msg += c.gateDesc; msg += ")"; }
                LogOut(msg, true);
                return false;
            }
            std::vector<uint8_t> pat = c.pattern;
            std::string preMirrorDesc = DescribePattern(pat);
            if (!facingRight) {
                for (auto &b : pat) {
                    bool leftOnly = (b & GAME_INPUT_LEFT) && !(b & GAME_INPUT_RIGHT);
                    bool rightOnly = (b & GAME_INPUT_RIGHT) && !(b & GAME_INPUT_LEFT);
                    if (leftOnly || rightOnly) {
                        b ^= (GAME_INPUT_LEFT | GAME_INPUT_RIGHT);
                    }
                }
            }
            std::string postMirrorDesc = facingRight ? preMirrorDesc : DescribePattern(pat);
            LogOut(std::string("[FM] Executing ") + c.name +
                   " (charId=" + std::to_string(characterId) + ") P" + std::to_string(playerNum) +
                   (facingRight ? " FR" : " FL") +
                   " pattern=" + postMirrorDesc +
                   (facingRight ? "" : std::string(" (src=") + preMirrorDesc + ")"), true);
            bool ok = false;
            const bool useTransaction =
                (playerNum == 2) ||
                (playerNum == ResolveAutoActionTargetPlayer());
            if (useTransaction) {
                // The FM detector expects the terminating button a few history
                // entries old; append those neutrals (last one via native poll).
                if (c.indexAdvance > 0) {
                    pat.insert(pat.end(), static_cast<size_t>(c.indexAdvance), 0);
                }
                const AutoActionMotionSubmitResult submit =
                    SubmitAutoActionPatternTransaction(
                        playerNum, pat, facingRight, consumerWaitPasses,
                        generationOut);
                if (submitResultOut) *submitResultOut = submit;
                ok = submit == AutoActionMotionSubmitResult::Accepted;
            } else {
                ok = (c.indexAdvance > 0)
                    ? FreezeBufferWithPattern(playerNum, pat, c.indexAdvance)
                    : FreezeBufferWithPattern(playerNum, pat);
                if (submitResultOut) {
                    *submitResultOut = ok
                        ? P2AutoActionMotionSubmitResult::Accepted
                        : P2AutoActionMotionSubmitResult::Busy;
                }
            }
            if (!ok) {
                LogOut(std::string("[FM] Failed to freeze buffer for ") + c.name, true);
            } else {
                LogOut(std::string("[FM] Dispatched pattern len=") + std::to_string(pat.size()), detailedLogging.load());
            }
            return ok;
        }
    }
    LogOut(std::string("[FM] Character ID ") + std::to_string(characterId) + " not found in FM table", true);
    return false;
}
