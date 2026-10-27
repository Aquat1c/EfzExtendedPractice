#include "../include/gui/framebar.h"
#include "../include/utils/utilities.h"
#include "../include/utils/config.h"
#include "../include/utils/i18n.h"
#include "../include/core/constants.h"
#include "../include/core/memory.h"
#include "../include/game/frame_advantage.h"
#include "../include/game/frame_monitor.h"
#include "../include/input/framestep.h"
#include "../include/utils/pause_integration.h"
#include "../3rdparty/imgui/imgui.h"

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace FrameBar {

std::atomic<bool> g_enabled{false};

namespace {

struct Cell {
    Cat   cat       = Cat::None;
    short moveID    = 0;
    // +0x14A - multi-purpose state timer. On the *attacker* this is hit-hitstop
    // remaining (set from attack_data+194 the moment of resolution); on the
    // *defender* it is blockstun / hitstun freeze remaining. Field name kept
    // as `blockstun` for compatibility with existing code; rendered as "ST".
    short blockstun = 0;
    // +0x124 - untech / recovery-cooldown counter. Set on hit to a function of
    // attack_data+178 and combo damage scaling. Defender can air-tech once
    // this drops below the move's hit-floor.
    short untech    = 0;
    // +0x0A - current frame index within the active move's animation.
    short frameIdx  = 0;
    // +0x14C - *local* superflash freeze. Only the activator of an IC/super
    // has this > 0; while non-zero on either player the engine pauses the
    // flash counter (+0x30C4) and gameplay timers stop ticking. Rendered "SF".
    short hitstop   = 0;
    // +0x14E - frames since leaving ground. RG locks at >= 30 internal.
    short airTime   = 0;
    // +0x174 - combo length the attacker has on opponent.
    short combo     = 0;
    // +0x104 - combo drop window (180 → 0).
    short comboTimer= 0;
    // +0x138 - RG-attempt cooldown (engine's actual 10F lock-out).
    short rgCooldown= 0;
    // +0x130 / +0x140 - decomp-derived collision/air-interaction lockouts.
    unsigned char frameLockout = 0;
    short collisionLockout = 0;
    // +0x16C - attacker move countdown; engine decrements after each resolve.
    short attackTimer = 0;
    // +0x168 - raw producer-dependent collision/result latch. It is useful as
    // a diagnostic marker, but its numeric values are not typed outcomes.
    int   hitState  = 0;
    // +0x170 - guard / RC-marked flag (defender in guard, or attacker
    // marked-as-RGd by defender's RG).
    int   guardFlag = 0;
    // +0x144 - counter-hit confirmed this frame (set when both attack flag
    // 0x2000 and defender hit-flag 0x2000 align).
    int   counterHit= 0;
    // +0x134 - Guard Gauge (0..360, depletes per subframe at wiki rates).
    float guardGauge= 0;
    // +0x30C4 - flash overlay visual counter (counts down per non-frozen
    // frame). Different from `hitstop` above.
    int   superflash= 0;
    // frame_data + 0xAA / + 0xB0 - attack and hit property bitfields read
    // from the current 200-byte frame data block.
    unsigned short atkFlags = 0;
    unsigned short hitFlags = 0;
    unsigned short guardFlags = 0;
    short stateLockout = 0;      // +0x13A, blocks normal guard/RG in collision code
    short specialState = 0;      // +0x13C, collision early-out / special lockout
    short baseDamage = 0;        // frame_data +0xA0, attack-data presence check
    short chipDamage = 0;        // frame_data +0xA8, block chip / guard data
    short attackerFreeze = 0;    // frame_data +0xC2, copied into attacker +0x14A on contact
    short defenderFreeze = 0;    // frame_data +0xC4, copied into defender +0x14A on contact
    unsigned char airMobility1 = 0;
    unsigned char airMobility2 = 0;
    char  attackBoxes = 0;       // 0..4 non-empty character attack boxes at frame_data+80
    short projectileBoxes = 0;   // non-empty attack boxes on active projectile frame data
    char  projectiles = 0;       // active projectile slot count for this side
    bool  activeHitbox = false;  // character attack passes EFZ collision-active gate
    bool  projectileHitbox = false;
    bool  inHitstop   = false;   // shared hit-hitstop, not superflash
    bool  attackData  = false;   // current frame has EFZ attack-data, independent of moveID
    bool  engineBusy  = false;   // engine timers/flags say this side is not fully neutral
    bool  canBlockNow = false;   // decomp-style block check against opponent's current attack
    bool  canRGNow    = false;   // decomp-style canPerformRecoilGuard check
    bool  firstActiveMarker = false;
    int   firstActive = -1;
    int   activeCount = 0;
    int   totalBusyFrames = 0;
};

struct Side {
    Cell cells[kBarMemory];
    short prevMoveID = 0;
    bool  prevAirborne = false;
    bool  dashSequenceActive = false;
    bool  dashSequenceStartedAirborne = false;
    int   activeRunFrames = 0;       // frames since attack started
    bool  haveSeenActiveBox = false; // attack box was real this attack run
    int   activeCounter = 0;         // MBAACC-style active duration counter
    int   firstActiveCounter = 0;    // frames from action start to first active frame
    int   firstActive = -1;
    int   totalBusyCounter = 0;
    int   totalBusyMemory = 0;
    bool  firstActiveCaptured = false;
    bool  firstActiveEdge = false;
};

Side g_p1;
Side g_p2;

int g_writeIdx = 0;        // next slot to write (mod kBarMemory)
int g_filledFrames = 0;    // total frames written (capped at large int)
std::mutex g_lock;

struct PausedStepState {
    bool wasPaused = false;
    unsigned int lastStep = 0;
};
PausedStepState g_pausedStep{};

// MBAACC's BarHandling has three states:
//   - Active   : at least one player inactionable / has projectile / can't
//                block. Bar advances and samples each frame.
//   - Tail     : Active just ended; keep advancing for `tailMax` frames so the
//                last few post-action frames stay visible.
//   - Latched  : Tail expired. Bar STOPS advancing but the existing cells
//                remain on screen. The next time activity resumes the bar is
//                cleared and a fresh sequence starts.
constexpr int kIdleTailSubframes = 180;     // roughly one second at subframe cadence
int  g_idleTailFrames = kIdleTailSubframes; // start at tail-max so first activity triggers a reset
bool g_latched = true;                            // bar held in place; clear on next active edge

// Shared-hitstop detector. EFZ's per-player +0x14A counter is set on both
// attacker and defender from attack_data +194/+196 the moment a hit lands.
// While both are >0 the engine effectively pauses gameplay (no move advance).
// MBAACC tracks the same condition as `nSharedHitstop` and skips bar advance
// while it persists for more than one frame.
int g_sharedHitstopRun = 0;

enum : int {
    kTimingSubframes = 0,
    kTimingVisualFrames = 1,
};

enum : int {
    kDetailFull = 0,
    kDetailCompact = 1,
    kDetailBarsOnly = 2,
};

int TimingMode() {
    const int mode = Config::GetSettings().frameBarTimingMode;
    return mode == kTimingVisualFrames ? kTimingVisualFrames : kTimingSubframes;
}

int DetailMode() {
    const int mode = Config::GetSettings().frameBarDetailMode;
    return (mode >= kDetailFull && mode <= kDetailBarsOnly) ? mode : kDetailFull;
}

bool VisualFrameSampling() {
    return TimingMode() == kTimingVisualFrames;
}

int IdleTailMaxFrames() {
    return kIdleTailSubframes;
}

// Decompilation ground truth from efz.c:
//   handlePlayerToPlayerCollision checks frame_data+0xE9 == 0 and
//   *(short*)(frame_data+0xEA) > 0 before character attack boxes may collide.
//   processProjectileCollision walks 64 projectile entries at player+0x4D0,
//   resolves each projectile's current frame data through player+0x164, and
//   only accepts entries that are not destroyed and still have life.
constexpr uintptr_t kFrameCollisionDisabledOffset = 0xE9;
constexpr uintptr_t kFrameActiveRemainingOffset   = 0xEA;
constexpr uintptr_t kAnimVisualTableOffset        = 0x10;
constexpr uintptr_t kVisualFrameStride            = 12;
constexpr uintptr_t kVisualFrameOriginOffset      = 4;    // two shorts: x-origin, y-origin
constexpr uintptr_t kSpecialCollisionBoxOffset    = 144;  // defender body/special box
constexpr uintptr_t kProjectileHeadOffset         = 0x2CC;
constexpr uintptr_t kProjectileTailOffset         = 0x2CA;
constexpr uintptr_t kProjectileAliveFlagsOffset   = 0x3D0;
constexpr uintptr_t kProjectileEntryBaseOffset    = 0x4D0;
constexpr uintptr_t kProjectileEntryStride        = 0x98;
constexpr uintptr_t kProjectilePatternOffset      = 0x00;
constexpr uintptr_t kProjectileFrameOffset        = 0x02;
constexpr uintptr_t kProjectileVisualTableOffset  = 0x08;
constexpr uintptr_t kProjectileXOffset            = 0x18;
constexpr uintptr_t kProjectileYOffset            = 0x20;
constexpr uintptr_t kProjectileFacingOffset       = 0x48;
constexpr uintptr_t kProjectileDestroyedOffset    = 0x80;
constexpr uintptr_t kProjectileLifeOffset         = 0x84;
constexpr int       kProjectileSlotCount          = 64;
constexpr int       kAttackBoxCount               = 4;
constexpr int       kHurtBoxCount                 = 5;
constexpr int       kProjectileThreatFlagCapacity = 16;

struct RawBox {
    int l = 0;
    int t = 0;
    int r = 0;
    int b = 0;
};

struct RectI {
    int l = 0;
    int t = 0;
    int r = 0;
    int b = 0;
};

struct VisualPose {
    short originX = 0;
    short originY = 0;
    bool ok = false;
};

struct ProjectileActivity {
    int slots = 0;
    int boxes = 0;
    unsigned short atkFlags = 0;
    unsigned short threatFlags[kProjectileThreatFlagCapacity]{};
    int threatFlagCount = 0;
};

struct Sample {
    uintptr_t base = 0;
    uintptr_t frameData = 0;
    short moveID = 0;
    short frameIdx = 0;
    short stateTimer = 0;
    short untech = 0;
    short superFreeze = 0;
    short airTime = 0;
    short combo = 0;
    short comboTimer = 0;
    short rgCooldown = 0;
    unsigned char frameLockout = 0;
    short stateLockout = 0;
    short specialState = 0;
    short cooldown3 = 0;
    short cooldown4 = 0;
    short attackTimer = 0;
    int hitState = 0;
    int guardFlag = 0;
    int counterHit = 0;
    int superflash = 0;
    float guardGauge = 0.0f;
    double x = 0.0;
    double y = 0.0;
    double xVel = 0.0;
    double yVel = 0.0;
    signed char facing = 1;
    signed char blockDir = 0;
    unsigned char blockStance = 0;
    unsigned char airMobility1 = 0;
    unsigned char airMobility2 = 0;
    unsigned short atkFlags = 0;
    unsigned short hitFlags = 0;
    unsigned short guardFlags = 0;
    unsigned char frameCollisionDisabled = 0; // frame_data +0xE9
    short activeFramesRemaining = 0;          // frame_data +0xEA
    short baseDamage = 0;
    short chipDamage = 0;
    short attackerFreeze = 0;
    short defenderFreeze = 0;
    int attackBoxes = 0;
    int projectileBoxes = 0;
    int projectiles = 0;
    unsigned short projectileAtkFlags = 0;
    unsigned short projectileThreatFlags[kProjectileThreatFlagCapacity]{};
    int projectileThreatFlagCount = 0;
    bool activeHitbox = false;
    bool projectileHitbox = false;
    bool attackData = false;
    bool engineBusy = false;
    bool canBlockOpponent = false;
    bool canRGOpponent = false;
};

// Both timers >0 for at least 2 consecutive frames; one-frame transients don't count.
bool IsSharedHitstop(short timer1, short timer2) {
    if (timer1 > 0 && timer2 > 0) {
        if (g_sharedHitstopRun < 1000) g_sharedHitstopRun++;
    } else {
        g_sharedHitstopRun = 0;
    }
    return g_sharedHitstopRun > 1;
}

enum class MoveDir : uint8_t {
    Neutral,
    Forward,
    Back
};

bool IsDoubleJumpState(short m) {
    return m == DOUBLE_JUMP_NEUTRAL_ID ||
           m == DOUBLE_JUMP_FWD_ID ||
           m == DOUBLE_JUMP_BACK_ID;
}

bool IsJumpState(short m) {
    return m == STRAIGHT_JUMP_ID ||
           m == FORWARD_JUMP_ID ||
           m == BACKWARD_JUMP_ID;
}

bool IsLandingState(short m) {
    return m == LANDING_ID ||
           m == LANDING_1_ID ||
           m == LANDING_2_ID ||
           m == LANDING_3_ID;
}

bool IsDashMoveID(short m) {
    return IsDashState(m) ||
           m == FORWARD_DASH_RECOVERY_SENTINEL_ID ||
           m == KAORI_FORWARD_DASH_START_ID;
}

bool IsAirborneForMovement(const Sample& s) {
    return s.y < 0.0 || s.airTime > 0;
}

MoveDir MoveDirectionFromVelocity(const Sample& s) {
    const signed char facing = (s.facing < 0) ? -1 : 1;
    if (s.xVel > 0.001 || s.xVel < -0.001) {
        return (s.xVel * (double)facing > 0.0) ? MoveDir::Forward : MoveDir::Back;
    }
    return MoveDir::Neutral;
}

MoveDir MoveDirectionFromState(const Sample& s) {
    switch (s.moveID) {
        case WALK_FWD_ID:
        case FORWARD_JUMP_ID:
        case DOUBLE_JUMP_FWD_ID:
        case FORWARD_DASH_START_ID:
        case FORWARD_DASH_RECOVERY_ID:
        case FORWARD_DASH_RECOVERY_SENTINEL_ID:
        case KAORI_FORWARD_DASH_START_ID:
            return MoveDir::Forward;
        case WALK_BACK_ID:
        case BACKWARD_JUMP_ID:
        case DOUBLE_JUMP_BACK_ID:
        case BACKWARD_DASH_START_ID:
        case BACKWARD_DASH_RECOVERY_ID:
            return MoveDir::Back;
        default:
            return MoveDir::Neutral;
    }
}

MoveDir MoveDirectionFromInput(const Sample& s) {
    const signed char facing = (s.facing < 0) ? -1 : 1;
    if (s.blockDir == 0) return MoveDir::Neutral;
    return (s.blockDir * facing > 0) ? MoveDir::Forward : MoveDir::Back;
}

MoveDir MovementDirection(const Sample& s) {
    const MoveDir byState = MoveDirectionFromState(s);
    if (byState != MoveDir::Neutral) return byState;
    const MoveDir byVelocity = MoveDirectionFromVelocity(s);
    if (byVelocity != MoveDir::Neutral) return byVelocity;
    return MoveDirectionFromInput(s);
}

Cat DashCatFor(Side& side, const Sample& s) {
    const MoveDir d = MovementDirection(s);
    if (!side.dashSequenceActive || !IsDashMoveID(side.prevMoveID)) {
        side.dashSequenceActive = true;
        side.dashSequenceStartedAirborne = IsAirborneForMovement(s) && side.prevAirborne;
    }

    const bool airborne = side.dashSequenceStartedAirborne;
    if (airborne) {
        if (d == MoveDir::Forward) return Cat::AirDashFwd;
        if (d == MoveDir::Back) return Cat::AirDashBack;
        return Cat::AirDashNeutral;
    }
    return d == MoveDir::Back ? Cat::DashBack : Cat::DashFwd;
}

// Order matters: specific tests first; frame-data/timer fields drive
// attack/block/RG before move IDs.
Cat ClassifySample(Side& side, const Sample& s) {
    const short m = s.moveID;

    if (!IsDashMoveID(m)) {
        side.dashSequenceActive = false;
        side.dashSequenceStartedAirborne = false;
    }

    // Hard freeze states (own-screen-pause moves)
    if (s.superFreeze > 0 || m == GROUND_IC_ID || m == AIR_IC_ID) return Cat::SuperFlash;

    // Defensive states first (priority over move ID ranges)
    if (IsRecoilGuard(m))    return Cat::RG;
    if (m == FORWARD_AIRTECH) return Cat::AirtechFwd;
    if (m == BACKWARD_AIRTECH) return Cat::AirtechBack;
    if (IsGroundtech(m))     return Cat::Groundtech;
    // Raw 6 is retained as a legacy visual hint, but counter paths can write it
    // too; unlike IsThrown(m), it is not an authoritative throw classification.
    if (s.hitState == 6 || IsThrown(m)) return Cat::Thrown;
    if (m == STAND_GUARD_ID || m == CROUCH_GUARD_ID || m == AIR_GUARD_ID
        || m == CROUCH_GUARD_STUN1 || m == CROUCH_GUARD_STUN2)
        return Cat::Blockstun;
    if (IsBlockstun(m))      return Cat::Blockstun;
    if (IsHitstun(m))        return Cat::Hitstun;
    if (IsLaunched(m))       return Cat::Launched;
    if (IsSpecialStun(m))    return Cat::SpecialStun;

    // Air-vs-ground comes from the sequence edge, not per-sample y-position.
    if (IsDashMoveID(m)) {
        return DashCatFor(side, s);
    }

    // EFZ writes block/hit timers independently of the animation ID. Prefer
    // those when a state transition is mid-freeze and the move ID has not yet
    // settled into its eventual stun animation. Combo timer is intentionally
    // not used as stun proof: it can linger on an attacker while they move.
    if (s.stateTimer > 0 && s.guardFlag && !s.attackData && !IsAttackMove(m)) {
        return Cat::Blockstun;
    }
    if ((s.stateTimer > 0 || s.untech > 0) &&
        !s.guardFlag && !s.attackData && !IsAttackMove(m)) {
        return IsAirborneForMovement(s) ? Cat::Launched : Cat::Hitstun;
    }

    // Offensive frames. Frame data is the engine source of truth; IsAttackMove
    // only covers startup/recovery frames where the current frame block does
    // not yet contain active attack properties.
    if (s.attackData || IsAttackMove(m)) return Cat::AttackStartup;

    if (m == PREJUMP_ID) return Cat::PreJump;
    if (m == STRAIGHT_JUMP_ID) return Cat::JumpNeutral;
    if (m == FORWARD_JUMP_ID) return Cat::JumpFwd;
    if (m == BACKWARD_JUMP_ID) return Cat::JumpBack;
    if (m == DOUBLE_JUMP_NEUTRAL_ID) return Cat::DoubleJumpNeutral;
    if (m == DOUBLE_JUMP_FWD_ID) return Cat::DoubleJumpFwd;
    if (m == DOUBLE_JUMP_BACK_ID) return Cat::DoubleJumpBack;
    if (m == FALLING_ID) return Cat::Falling;
    if (IsLandingState(m)) return Cat::Landing;
    if (m == WALK_FWD_ID) return Cat::WalkFwd;
    if (m == WALK_BACK_ID) return Cat::WalkBack;
    if (m == CROUCH_ID || m == CROUCH_TO_STAND_ID) return Cat::Crouch;
    if (m == IDLE_MOVE_ID) return Cat::Neutral;
    if (IsActionable(m))   return Cat::Neutral;
    return Cat::Neutral;
}

ImU32 ColorFor(Cat c) {
    switch (c) {
        case Cat::None:             return IM_COL32(  0,   0,   0,   0);
        case Cat::Neutral:          return IM_COL32( 32,  90,   0, 220);
        case Cat::WalkFwd:          return IM_COL32( 72, 185,  60, 220);
        case Cat::WalkBack:         return IM_COL32( 45, 145,  88, 220);
        case Cat::PreJump:          return IM_COL32(210, 188,  84, 225);
        case Cat::JumpNeutral:      return IM_COL32(241, 224, 132, 220);
        case Cat::JumpFwd:          return IM_COL32(232, 196,  72, 225);
        case Cat::JumpBack:         return IM_COL32(196, 168,  64, 225);
        case Cat::DoubleJumpNeutral:return IM_COL32(245, 228, 168, 230);
        case Cat::DoubleJumpFwd:    return IM_COL32(255, 205, 104, 235);
        case Cat::DoubleJumpBack:   return IM_COL32(205, 180,  92, 235);
        case Cat::Falling:          return IM_COL32(155, 180, 115, 215);
        case Cat::Landing:          return IM_COL32(120, 150,  88, 225);
        case Cat::Crouch:           return IM_COL32( 50, 130,   0, 220);
        case Cat::DashFwd:          return IM_COL32(  0, 200, 255, 230);
        case Cat::DashBack:         return IM_COL32(  0, 140, 220, 230);
        case Cat::AirDashNeutral:   return IM_COL32(125, 205, 255, 230);
        case Cat::AirDashFwd:       return IM_COL32( 80, 220, 255, 238);
        case Cat::AirDashBack:      return IM_COL32( 70, 150, 255, 238);
        case Cat::AttackStartup:    return IM_COL32(180,  40,  40, 220);
        case Cat::AttackActive:     return IM_COL32(255,   0,   0, 240);
        case Cat::AttackRecovery:   return IM_COL32(120,  40,  40, 220);
        case Cat::Blockstun:        return IM_COL32(180, 180, 180, 220);
        case Cat::Hitstun:          return IM_COL32(140, 140, 140, 220);
        case Cat::Launched:         return IM_COL32(110, 110, 110, 220);
        case Cat::AirtechFwd:       return IM_COL32(255, 210,  75, 225);
        case Cat::AirtechBack:      return IM_COL32(225, 175,  45, 225);
        case Cat::Groundtech:       return IM_COL32(225, 184,   0, 220);
        case Cat::SpecialStun:      return IM_COL32(255, 255, 255, 230);
        case Cat::Thrown:           return IM_COL32(110,  20, 110, 220);
        case Cat::RG:               return IM_COL32(145, 194, 255, 230);
        case Cat::SuperFlash:       return IM_COL32(255,  90, 230, 230);
        case Cat::HitstopShared:    return IM_COL32( 60,  80, 128, 230);
        default:                    return IM_COL32( 60,  60,  60, 200);
    }
}

const char* CatName(Cat c) {
    switch (c) {
        case Cat::Neutral:        return "NEUTRAL";
        case Cat::WalkFwd:        return "WALK-F";
        case Cat::WalkBack:       return "WALK-B";
        case Cat::PreJump:        return "PREJUMP";
        case Cat::JumpNeutral:    return "JUMP";
        case Cat::JumpFwd:        return "JUMP-F";
        case Cat::JumpBack:       return "JUMP-B";
        case Cat::DoubleJumpNeutral:return "DJUMP";
        case Cat::DoubleJumpFwd:  return "DJUMP-F";
        case Cat::DoubleJumpBack: return "DJUMP-B";
        case Cat::Falling:        return "FALL";
        case Cat::Landing:        return "LAND";
        case Cat::Crouch:         return "CROUCH";
        case Cat::DashFwd:        return "DASH-F";
        case Cat::DashBack:       return "DASH-B";
        case Cat::AirDashNeutral: return "ADASH";
        case Cat::AirDashFwd:     return "ADASH-F";
        case Cat::AirDashBack:    return "ADASH-B";
        case Cat::AttackStartup:  return "STARTUP";
        case Cat::AttackActive:   return "ACTIVE";
        case Cat::AttackRecovery: return "RECOVERY";
        case Cat::Blockstun:      return "BLOCK";
        case Cat::Hitstun:        return "HIT";
        case Cat::Launched:       return "LAUNCH";
        case Cat::AirtechFwd:     return "TECH-F";
        case Cat::AirtechBack:    return "TECH-B";
        case Cat::Groundtech:     return "GND-TECH";
        case Cat::SpecialStun:    return "STUN";
        case Cat::Thrown:         return "THROWN";
        case Cat::RG:             return "RG";
        case Cat::SuperFlash:     return "FLASH";
        case Cat::HitstopShared:  return "HITSTOP";
        default:                  return "";
    }
}

// Read the blockstun counter (short at base+0x14A). Returns 0 on failure.
short ReadBlockstun(uintptr_t base) {
    if (!base) return 0;
    short v = 0;
    if (!SafeReadMemory(base + BLOCKSTUN_OFFSET, &v, sizeof(v))) return 0;
    return v;
}

short ReadUntech(uintptr_t base) {
    if (!base) return 0;
    short v = 0;
    if (!SafeReadMemory(base + UNTECH_OFFSET, &v, sizeof(v))) return 0;
    return v;
}

short ReadMoveId(uintptr_t base) {
    if (!base) return 0;
    short v = 0;
    if (!SafeReadMemory(base + MOVE_ID_OFFSET, &v, sizeof(v))) return 0;
    return v;
}

short ReadFrameIdx(uintptr_t base) {
    if (!base) return 0;
    short v = 0;
    if (!SafeReadMemory(base + CURRENT_FRAME_INDEX_OFFSET, &v, sizeof(v))) return 0;
    return v;
}

short ReadShort(uintptr_t base, uintptr_t off) {
    if (!base) return 0;
    short v = 0;
    if (!SafeReadMemory(base + off, &v, sizeof(v))) return 0;
    return v;
}

int ReadDword(uintptr_t base, uintptr_t off) {
    if (!base) return 0;
    int v = 0;
    if (!SafeReadMemory(base + off, &v, sizeof(v))) return 0;
    return v;
}

float ReadFloat(uintptr_t base, uintptr_t off) {
    if (!base) return 0.0f;
    float v = 0.0f;
    if (!SafeReadMemory(base + off, &v, sizeof(v))) return 0.0f;
    return v;
}

double ReadDouble(uintptr_t base, uintptr_t off) {
    if (!base) return 0.0;
    double v = 0.0;
    if (!SafeReadMemory(base + off, &v, sizeof(v))) return 0.0;
    return v;
}

unsigned char ReadByte(uintptr_t base, uintptr_t off) {
    if (!base) return 0;
    unsigned char v = 0;
    if (!SafeReadMemory(base + off, &v, sizeof(v))) return 0;
    return v;
}

signed char ReadSignedByte(uintptr_t base, uintptr_t off) {
    if (!base) return 0;
    signed char v = 0;
    if (!SafeReadMemory(base + off, &v, sizeof(v))) return 0;
    return v;
}

unsigned short ReadFrameWord(uintptr_t frameData, uintptr_t off) {
    if (!frameData) return 0;
    unsigned short v = 0;
    if (!SafeReadMemory(frameData + off, &v, sizeof(v))) return 0;
    return v;
}

short ReadFrameShort(uintptr_t frameData, uintptr_t off) {
    if (!frameData) return 0;
    short v = 0;
    if (!SafeReadMemory(frameData + off, &v, sizeof(v))) return 0;
    return v;
}

unsigned char ReadFrameByte(uintptr_t frameData, uintptr_t off) {
    if (!frameData) return 0;
    unsigned char v = 0;
    if (!SafeReadMemory(frameData + off, &v, sizeof(v))) return 0;
    return v;
}

bool FrameCollisionActive(uintptr_t frameData, int attackBoxes) {
    if (!frameData || attackBoxes <= 0) return false;
    const unsigned char disabled = ReadFrameByte(frameData, kFrameCollisionDisabledOffset);
    const short activeRemaining = ReadFrameShort(frameData, kFrameActiveRemainingOffset);
    return disabled == 0 && activeRemaining > 0;
}

bool ReadRawBox(uintptr_t frameData, uintptr_t off, RawBox& out) {
    if (!frameData) return false;
    return SafeReadMemory(frameData + off, &out, sizeof(out));
}

bool BoxNonEmpty(const RawBox& box) {
    return box.r > box.l && box.b > box.t;
}

bool RectIntersects(const RectI& a, const RectI& b) {
    if (a.r <= a.l || a.b <= a.t || b.r <= b.l || b.b <= b.t) return false;
    return a.b > b.t && b.b > a.t && a.r > b.l && b.r > a.l;
}

int ToCollisionCoord(double value) {
    return static_cast<int>(value);
}

bool ReadVisualPoseFromTable(uintptr_t visualTable,
                             unsigned short patternId,
                             unsigned short frameIdx,
                             VisualPose& pose) {
    pose = VisualPose{};
    if (!visualTable) return false;
    uintptr_t framesArr = 0;
    const uintptr_t entry = visualTable + ANIM_ENTRY_STRIDE * (uintptr_t)patternId
                          + ANIM_ENTRY_FRAMES_PTR_OFFSET;
    if (!SafeReadMemory(entry, &framesArr, sizeof(framesArr)) || !framesArr) return false;
    const uintptr_t visualFrame = framesArr + kVisualFrameStride * (uintptr_t)frameIdx;
    if (!SafeReadMemory(visualFrame + kVisualFrameOriginOffset,
                        &pose.originX,
                        sizeof(pose.originX))) {
        return false;
    }
    if (!SafeReadMemory(visualFrame + kVisualFrameOriginOffset + sizeof(short),
                        &pose.originY,
                        sizeof(pose.originY))) {
        return false;
    }
    pose.ok = true;
    return true;
}

bool ReadCharacterVisualPose(uintptr_t base,
                             unsigned short patternId,
                             unsigned short frameIdx,
                             VisualPose& pose) {
    if (!base) return false;
    uintptr_t visualTable = 0;
    if (!SafeReadMemory(base + kAnimVisualTableOffset, &visualTable, sizeof(visualTable))) {
        return false;
    }
    return ReadVisualPoseFromTable(visualTable, patternId, frameIdx, pose);
}

bool BuildCharacterRect(const Sample& sample, const RawBox& box, RectI& out) {
    if (!BoxNonEmpty(box)) return false;
    VisualPose pose{};
    if (!ReadCharacterVisualPose(sample.base,
                                 static_cast<unsigned short>(sample.moveID),
                                 static_cast<unsigned short>(sample.frameIdx),
                                 pose)) {
        return false;
    }
    if (sample.facing == 1) {
        out.l = ToCollisionCoord(sample.x - 0.5 - (double)pose.originX + (double)box.l);
        out.r = ToCollisionCoord(sample.x - 0.5 - (double)pose.originX + (double)box.r);
    } else {
        out.l = ToCollisionCoord(sample.x + 0.5 + (double)pose.originX - (double)box.r);
        out.r = ToCollisionCoord(sample.x + 0.5 + (double)pose.originX - (double)box.l);
    }
    out.t = ToCollisionCoord(sample.y - (double)pose.originY + (double)box.t);
    out.b = ToCollisionCoord(sample.y - (double)pose.originY + (double)box.b);
    return out.r > out.l && out.b > out.t;
}

bool ReadProjectileVisualPose(uintptr_t entryBase,
                              unsigned short patternId,
                              unsigned short frameIdx,
                              VisualPose& pose) {
    uintptr_t visualTable = 0;
    if (!SafeReadMemory(entryBase + kProjectileVisualTableOffset, &visualTable, sizeof(visualTable))) {
        return false;
    }
    return ReadVisualPoseFromTable(visualTable, patternId, frameIdx, pose);
}

bool BuildProjectileRect(uintptr_t entryBase,
                         unsigned short patternId,
                         unsigned short frameIdx,
                         const RawBox& box,
                         RectI& out) {
    if (!BoxNonEmpty(box)) return false;
    VisualPose pose{};
    if (!ReadProjectileVisualPose(entryBase, patternId, frameIdx, pose)) return false;
    double x = 0.0;
    double y = 0.0;
    signed char facing = 1;
    if (!SafeReadMemory(entryBase + kProjectileXOffset, &x, sizeof(x))) return false;
    if (!SafeReadMemory(entryBase + kProjectileYOffset, &y, sizeof(y))) return false;
    if (!SafeReadMemory(entryBase + kProjectileFacingOffset, &facing, sizeof(facing))) return false;

    if (facing == 1) {
        out.l = ToCollisionCoord(x - 0.5 - (double)pose.originX + (double)box.l);
        out.r = ToCollisionCoord(x - 0.5 - (double)pose.originX + (double)box.r);
    } else {
        out.l = ToCollisionCoord(x + 0.5 + (double)pose.originX - (double)box.r);
        out.r = ToCollisionCoord(x + 0.5 + (double)pose.originX - (double)box.l);
    }
    out.t = ToCollisionCoord(y - (double)pose.originY + (double)box.t);
    out.b = ToCollisionCoord(y - (double)pose.originY + (double)box.b);
    return out.r > out.l && out.b > out.t;
}

// Walk the player → animation table → frame data chain. The 200-byte frame
// data block contains:
//   bytes  0..79  : 5 hurtboxes (16 bytes each: int l, t, r, b)
//   bytes 80..143 : 4 attack boxes (16 bytes each: int l, t, r, b)
//   word  170     : attack-property flags
//   byte  233     : collision-disabled/recovery gate used by EFZ collision
//   short 234     : active-frames-remaining gate used by EFZ collision
// Returns the byte offset of the frame data block, or 0 if unreachable.
uintptr_t LookupFrameDataPtr(uintptr_t base) {
    if (!base) return 0;
    uintptr_t animTable = 0;
    if (!SafeReadMemory(base + ANIM_TABLE_OFFSET, &animTable, sizeof(animTable)) || !animTable) return 0;

    short patternId = 0;
    short frameIdx  = 0;
    if (!SafeReadMemory(base + MOVE_ID_OFFSET, &patternId, sizeof(patternId))) return 0;
    if (!SafeReadMemory(base + CURRENT_FRAME_INDEX_OFFSET, &frameIdx, sizeof(frameIdx))) return 0;

    // table entry at animTable + 8 * patternId, with frames-array pointer at +4
    const uintptr_t entry = animTable + ANIM_ENTRY_STRIDE * (uintptr_t)(unsigned short)patternId
                          + ANIM_ENTRY_FRAMES_PTR_OFFSET;
    uintptr_t framesArr = 0;
    if (!SafeReadMemory(entry, &framesArr, sizeof(framesArr)) || !framesArr) return 0;

    return framesArr + (uintptr_t)FRAME_BLOCK_STRIDE * (uintptr_t)(unsigned short)frameIdx;
}

// Reads the 4 attack boxes from frame_data + 80. Returns count of boxes
// that are non-empty (right > left && bottom > top).
int CountAttackBoxes(uintptr_t frameData) {
    if (!frameData) return 0;
    int n = 0;
    for (int i = 0; i < kAttackBoxCount; ++i) {
        RawBox box{};
        if (ReadRawBox(frameData, 80 + 16 * (uintptr_t)i, box) && BoxNonEmpty(box)) ++n;
    }
    return n;
}

void AddProjectileThreatFlags(ProjectileActivity& out, unsigned short atkFlags) {
    out.atkFlags |= atkFlags;
    if ((atkFlags & FRAME_ATTACK_FLAG_BLOCK_DISABLE) != 0) return;
    for (int i = 0; i < out.threatFlagCount; ++i) {
        if (out.threatFlags[i] == atkFlags) return;
    }
    if (out.threatFlagCount < kProjectileThreatFlagCapacity) {
        out.threatFlags[out.threatFlagCount++] = atkFlags;
    }
}

// Reads the projectile ring exactly like processProjectileCollision:
//   player+0x4D0 + 0x98*i is the projectile entity
//   player+0x164 resolves its pattern/frame to attack frame data
// Slot presence is kept separate from collision-capable projectile boxes so
// inert setplay objects can still be shown without being counted as active.
ProjectileActivity ReadProjectileActivity(uintptr_t base) {
    ProjectileActivity out{};
    if (!base) return out;
    short head = 0, tail = 0;
    if (!SafeReadMemory(base + kProjectileHeadOffset, &head, sizeof(head))) return out;
    if (!SafeReadMemory(base + kProjectileTailOffset, &tail, sizeof(tail))) return out;
    if (head < 0 || tail < 0 || head >= kProjectileSlotCount || tail >= kProjectileSlotCount) return out;

    uintptr_t animTable = 0;
    SafeReadMemory(base + ANIM_TABLE_OFFSET, &animTable, sizeof(animTable));

    int i = head;
    int safety = kProjectileSlotCount;
    while (i != tail && safety-- > 0) {
        unsigned int alive = 0;
        const bool slotAlive =
            SafeReadMemory(base + kProjectileAliveFlagsOffset + 4 * i, &alive, sizeof(alive)) && alive != 0;
        if (slotAlive) {
            ++out.slots;
        }

        const uintptr_t entryBase = base + kProjectileEntryBaseOffset
                                  + kProjectileEntryStride * (uintptr_t)i;
        unsigned int destroyed = 0;
        short life = 0;
        unsigned short patternId = 0;
        unsigned short frameIdx = 0;
        if (slotAlive && animTable &&
            SafeReadMemory(entryBase + kProjectileDestroyedOffset, &destroyed, sizeof(destroyed)) &&
            SafeReadMemory(entryBase + kProjectileLifeOffset, &life, sizeof(life)) &&
            SafeReadMemory(entryBase + kProjectilePatternOffset, &patternId, sizeof(patternId)) &&
            SafeReadMemory(entryBase + kProjectileFrameOffset, &frameIdx, sizeof(frameIdx)) &&
            destroyed == 0 && life > 0) {
            uintptr_t framesArr = 0;
            const uintptr_t animEntry = animTable + ANIM_ENTRY_STRIDE * (uintptr_t)patternId
                                      + ANIM_ENTRY_FRAMES_PTR_OFFSET;
            if (SafeReadMemory(animEntry, &framesArr, sizeof(framesArr)) && framesArr) {
                const uintptr_t frameData = framesArr + (uintptr_t)FRAME_BLOCK_STRIDE * (uintptr_t)frameIdx;
                const int boxes = CountAttackBoxes(frameData);
                if (boxes > 0) {
                    const unsigned short atkFlags = ReadFrameWord(frameData, FRAME_ATTACK_PROPS_OFFSET);
                    out.boxes += boxes;
                    AddProjectileThreatFlags(out, atkFlags);
                }
            }
        }
        i = (i + 1) & (kProjectileSlotCount - 1);
    }
    return out;
}

bool IsSpecialBlockState(short moveID) {
    return (moveID >= STANDING_BLOCK_LVL1 && moveID <= AIR_GUARD_ID) ||
           (moveID >= RG_STAND_ID && moveID <= AIR_IC_ID);
}

bool FrameHasAttackData(const Sample& s) {
    if (s.attackBoxes > 0) return true;
    if (s.baseDamage > 0 || s.chipDamage > 0) return true;
    if (s.guardFlags != 0) return true;
    if (s.attackerFreeze > 0 || s.defenderFreeze > 0) return true;
    const unsigned short attackBits =
        FRAME_ATTACK_FLAG_STAND_BLOCKABLE |
        FRAME_ATTACK_FLAG_CROUCH_BLOCKABLE |
        FRAME_ATTACK_FLAG_THROW_ATTACK |
        FRAME_ATTACK_FLAG_WALLBOUNCE |
        FRAME_ATTACK_FLAG_GROUND_BOUNCE |
        FRAME_ATTACK_FLAG_COUNTER_MOVE |
        FRAME_ATTACK_FLAG_BLOCK_DISABLE;
    return (s.atkFlags & attackBits) != 0;
}

bool ValidateBlockStanceLikeEFZ(const Sample& defender, const Sample& attacker, unsigned short attackFlags) {
    // Decompilation: validateBlockStance compares signed input direction
    // (+0x188) against 1 - 2 * (opponentX > selfX), with guard/RG states
    // allowed to keep blocking if the direction byte is momentarily neutral.
    const signed char requiredDirection =
        static_cast<signed char>(1 - 2 * (attacker.x - defender.x > 0.0));
    if (defender.blockDir != requiredDirection &&
        (!IsSpecialBlockState(defender.moveID) || defender.blockDir == 0)) {
        return false;
    }

    const bool standBlockable  = (attackFlags & FRAME_ATTACK_FLAG_STAND_BLOCKABLE) != 0;
    const bool crouchBlockable = (attackFlags & FRAME_ATTACK_FLAG_CROUCH_BLOCKABLE) != 0;
    if (defender.y == 0.0) {
        if (!crouchBlockable && defender.blockStance == 1) return false;
        if (!standBlockable && defender.blockStance != 1) return false;
    }
    return true;
}

bool CollisionCanReachDefenderLikeEFZ(const Sample& defender, unsigned short attackFlags) {
    if ((defender.hitFlags & FRAME_HIT_FLAG_DEFENDER_IMMUNE) != 0) return false;
    if (defender.frameLockout > 0 && (attackFlags & FRAME_ATTACK_FLAG_AIRBORNE) != 0) return false;
    if (defender.specialState != 0) return false;
    return true;
}

bool CharacterAttackIntersectsDefenderLikeEFZ(const Sample& attacker, const Sample& defender) {
    if (!attacker.activeHitbox) return false;
    if (!CollisionCanReachDefenderLikeEFZ(defender, attacker.atkFlags)) return false;

    const bool useSpecialBox = (attacker.atkFlags & FRAME_ATTACK_FLAG_AIRBORNE) != 0;
    RawBox defenderSpecial{};
    RectI defenderSpecialRect{};
    bool defenderSpecialValid = false;
    if (useSpecialBox) {
        defenderSpecialValid =
            ReadRawBox(defender.frameData, kSpecialCollisionBoxOffset, defenderSpecial) &&
            BuildCharacterRect(defender, defenderSpecial, defenderSpecialRect);
    }

    for (int i = 0; i < kAttackBoxCount; ++i) {
        RawBox attackBox{};
        RectI attackRect{};
        if (!ReadRawBox(attacker.frameData, 80 + 16 * (uintptr_t)i, attackBox) ||
            !BuildCharacterRect(attacker, attackBox, attackRect)) {
            continue;
        }

        if (useSpecialBox) {
            if (defenderSpecialValid && RectIntersects(attackRect, defenderSpecialRect)) return true;
            continue;
        }

        for (int j = 0; j < kHurtBoxCount; ++j) {
            RawBox hurtBox{};
            RectI hurtRect{};
            if (ReadRawBox(defender.frameData, 16 * (uintptr_t)j, hurtBox) &&
                BuildCharacterRect(defender, hurtBox, hurtRect) &&
                RectIntersects(attackRect, hurtRect)) {
                return true;
            }
        }
    }
    return false;
}

bool ProjectileAttackIntersectsDefenderLikeEFZ(const Sample& defender,
                                               uintptr_t entryBase,
                                               uintptr_t projectileFrameData,
                                               unsigned short patternId,
                                               unsigned short frameIdx,
                                               unsigned short attackFlags) {
    if (!CollisionCanReachDefenderLikeEFZ(defender, attackFlags)) return false;

    const bool useSpecialBox = (attackFlags & FRAME_ATTACK_FLAG_AIRBORNE) != 0;
    RawBox defenderSpecial{};
    RectI defenderSpecialRect{};
    bool defenderSpecialValid = false;
    if (useSpecialBox) {
        defenderSpecialValid =
            ReadRawBox(defender.frameData, kSpecialCollisionBoxOffset, defenderSpecial) &&
            BuildCharacterRect(defender, defenderSpecial, defenderSpecialRect);
    }

    for (int i = 0; i < kAttackBoxCount; ++i) {
        RawBox attackBox{};
        RectI attackRect{};
        if (!ReadRawBox(projectileFrameData, 80 + 16 * (uintptr_t)i, attackBox) ||
            !BuildProjectileRect(entryBase, patternId, frameIdx, attackBox, attackRect)) {
            continue;
        }

        if (useSpecialBox) {
            if (defenderSpecialValid && RectIntersects(attackRect, defenderSpecialRect)) return true;
            continue;
        }

        for (int j = 0; j < kHurtBoxCount; ++j) {
            RawBox hurtBox{};
            RectI hurtRect{};
            if (ReadRawBox(defender.frameData, 16 * (uintptr_t)j, hurtBox) &&
                BuildCharacterRect(defender, hurtBox, hurtRect) &&
                RectIntersects(attackRect, hurtRect)) {
                return true;
            }
        }
    }
    return false;
}

bool CanBlockThreatLikeEFZ(const Sample& defender, const Sample& attacker, unsigned short attackFlags) {
    if ((attackFlags & FRAME_ATTACK_FLAG_BLOCK_DISABLE) != 0) return false;
    return defender.guardFlag != 0 &&
           defender.airTime >= 30 &&
           defender.stateLockout <= 0 &&
           ValidateBlockStanceLikeEFZ(defender, attacker, attackFlags);
}

bool CanRecoilGuardThreatLikeEFZ(const Sample& defender, const Sample& attacker, unsigned short attackFlags) {
    if (defender.stateLockout > 0) return false;
    if ((attackFlags & FRAME_ATTACK_FLAG_BLOCK_DISABLE) != 0) return false;
    if ((defender.hitFlags & FRAME_HIT_FLAG_BLOCKABLE) == 0) return false;
    if ((attackFlags & FRAME_ATTACK_FLAG_SPECIAL_ANIM) == 0 &&
        defender.y < 0.0 &&
        defender.airTime < 30) {
        return false;
    }
    return ValidateBlockStanceLikeEFZ(defender, attacker, attackFlags);
}

enum class ThreatCheckKind : uint8_t {
    Block,
    RecoilGuard
};

bool AnyProjectileThreatMatchesLikeEFZ(const Sample& defender,
                                       const Sample& attacker,
                                       ThreatCheckKind kind) {
    if (!attacker.base || !attacker.projectileHitbox) return false;
    short head = 0, tail = 0;
    if (!SafeReadMemory(attacker.base + kProjectileHeadOffset, &head, sizeof(head))) return false;
    if (!SafeReadMemory(attacker.base + kProjectileTailOffset, &tail, sizeof(tail))) return false;
    if (head < 0 || tail < 0 || head >= kProjectileSlotCount || tail >= kProjectileSlotCount) return false;

    uintptr_t animTable = 0;
    if (!SafeReadMemory(attacker.base + ANIM_TABLE_OFFSET, &animTable, sizeof(animTable)) || !animTable) {
        return false;
    }

    int i = head;
    int safety = kProjectileSlotCount;
    while (i != tail && safety-- > 0) {
        unsigned int alive = 0;
        const bool slotAlive =
            SafeReadMemory(attacker.base + kProjectileAliveFlagsOffset + 4 * i, &alive, sizeof(alive)) &&
            alive != 0;
        const uintptr_t entryBase = attacker.base + kProjectileEntryBaseOffset
                                  + kProjectileEntryStride * (uintptr_t)i;
        unsigned int destroyed = 0;
        short life = 0;
        unsigned short patternId = 0;
        unsigned short frameIdx = 0;
        if (slotAlive &&
            SafeReadMemory(entryBase + kProjectileDestroyedOffset, &destroyed, sizeof(destroyed)) &&
            SafeReadMemory(entryBase + kProjectileLifeOffset, &life, sizeof(life)) &&
            SafeReadMemory(entryBase + kProjectilePatternOffset, &patternId, sizeof(patternId)) &&
            SafeReadMemory(entryBase + kProjectileFrameOffset, &frameIdx, sizeof(frameIdx)) &&
            destroyed == 0 && life > 0) {
            uintptr_t framesArr = 0;
            const uintptr_t animEntry = animTable + ANIM_ENTRY_STRIDE * (uintptr_t)patternId
                                      + ANIM_ENTRY_FRAMES_PTR_OFFSET;
            if (SafeReadMemory(animEntry, &framesArr, sizeof(framesArr)) && framesArr) {
                const uintptr_t frameData = framesArr + (uintptr_t)FRAME_BLOCK_STRIDE * (uintptr_t)frameIdx;
                const int boxes = CountAttackBoxes(frameData);
                if (boxes > 0) {
                    const unsigned short atkFlags = ReadFrameWord(frameData, FRAME_ATTACK_PROPS_OFFSET);
                    const bool eligible =
                        (kind == ThreatCheckKind::Block)
                            ? CanBlockThreatLikeEFZ(defender, attacker, atkFlags)
                            : CanRecoilGuardThreatLikeEFZ(defender, attacker, atkFlags);
                    if (eligible &&
                        ProjectileAttackIntersectsDefenderLikeEFZ(defender, entryBase, frameData,
                                                                  patternId, frameIdx, atkFlags)) {
                        return true;
                    }
                }
            }
        }
        i = (i + 1) & (kProjectileSlotCount - 1);
    }
    return false;
}

bool CanBlockLikeEFZ(const Sample& defender, const Sample& attacker) {
    if (attacker.activeHitbox &&
        CharacterAttackIntersectsDefenderLikeEFZ(attacker, defender) &&
        CanBlockThreatLikeEFZ(defender, attacker, attacker.atkFlags)) {
        return true;
    }
    return AnyProjectileThreatMatchesLikeEFZ(defender, attacker, ThreatCheckKind::Block);
}

bool CanRecoilGuardLikeEFZ(const Sample& defender, const Sample& attacker) {
    if (attacker.activeHitbox &&
        CharacterAttackIntersectsDefenderLikeEFZ(attacker, defender) &&
        CanRecoilGuardThreatLikeEFZ(defender, attacker, attacker.atkFlags)) {
        return true;
    }
    return AnyProjectileThreatMatchesLikeEFZ(defender, attacker, ThreatCheckKind::RecoilGuard);
}

bool IsEngineBusy(const Sample& s) {
    return s.attackData ||
           s.attackBoxes > 0 ||
           s.activeHitbox ||
           s.projectileHitbox ||
           s.projectiles > 0 ||
           s.stateTimer > 0 ||
           s.superFreeze > 0 ||
           s.superflash > 0 ||
           s.hitState != 0 ||
            s.attackTimer > 0 ||
            s.frameLockout > 0 ||
            s.rgCooldown > 0 ||
           s.stateLockout > 0 ||
           s.specialState > 0 ||
           s.cooldown3 > 0 ||
           s.cooldown4 > 0 ||
           !IsActionable(s.moveID);
}

Sample ReadSample(uintptr_t base) {
    Sample s{};
    s.base = base;
    s.moveID = ReadMoveId(base);
    s.frameIdx = ReadFrameIdx(base);
    s.frameData = LookupFrameDataPtr(base);
    s.attackBoxes = CountAttackBoxes(s.frameData);
    s.frameCollisionDisabled = ReadFrameByte(s.frameData, kFrameCollisionDisabledOffset);
    s.activeFramesRemaining = ReadFrameShort(s.frameData, kFrameActiveRemainingOffset);
    s.activeHitbox = FrameCollisionActive(s.frameData, s.attackBoxes);
    const ProjectileActivity projectiles = ReadProjectileActivity(base);
    s.projectiles = projectiles.slots;
    s.projectileBoxes = projectiles.boxes;
    s.projectileAtkFlags = projectiles.atkFlags;
    s.projectileThreatFlagCount = projectiles.threatFlagCount;
    for (int i = 0; i < s.projectileThreatFlagCount; ++i) {
        s.projectileThreatFlags[i] = projectiles.threatFlags[i];
    }
    s.projectileHitbox = s.projectileBoxes > 0;
    s.stateTimer = ReadShort(base, BLOCKSTUN_OFFSET);
    s.untech = ReadShort(base, UNTECH_OFFSET);
    s.superFreeze = ReadShort(base, SUPERFLASH_FREEZE_OFFSET);
    s.airTime = ReadShort(base, AIRTIME_OFFSET);
    s.combo = ReadShort(base, PLAYER_COMBO_COUNTER_OFFSET);
    s.comboTimer = ReadShort(base, PLAYER_COMBO_TIMER_OFFSET);
    s.frameLockout = ReadByte(base, PLAYER_FRAME_LOCKOUT_OFFSET);
    s.rgCooldown = ReadShort(base, PLAYER_RG_COOLDOWN_OFFSET);
    s.stateLockout = ReadShort(base, PLAYER_STATE_LOCKOUT_OFFSET);
    s.specialState = ReadShort(base, PLAYER_SPECIAL_STATE_OFFSET);
    s.cooldown3 = ReadShort(base, PLAYER_STATE_COOLDOWN3_OFFSET);
    s.cooldown4 = ReadShort(base, PLAYER_STATE_COOLDOWN4_OFFSET);
    s.attackTimer = ReadShort(base, PLAYER_ATTACK_TIMER_OFFSET);
    s.hitState = ReadDword(base, PLAYER_HIT_STATE_OFFSET);
    s.guardFlag = ReadDword(base, PLAYER_GUARD_FLAG_OFFSET);
    s.counterHit = ReadDword(base, PLAYER_COUNTER_HIT_FLAG_OFFSET);
    s.guardGauge = ReadFloat(base, PLAYER_GUARD_GAUGE_OFFSET);
    s.superflash = ReadDword(base, PLAYER_SUPERFLASH_COUNTER_OFFSET);
    s.x = ReadDouble(base, XPOS_OFFSET);
    s.y = ReadDouble(base, YPOS_OFFSET);
    s.xVel = ReadDouble(base, XVEL_OFFSET);
    s.yVel = ReadDouble(base, YVEL_OFFSET);
    s.facing = ReadSignedByte(base, FACING_DIRECTION_OFFSET);
    s.blockDir = ReadSignedByte(base, BLOCK_DIRECTION_OFFSET);
    s.blockStance = ReadByte(base, BLOCK_STANCE_OFFSET);
    s.airMobility1 = ReadByte(base, AIR_MOBILITY_COUNTER1_OFFSET);
    s.airMobility2 = ReadByte(base, AIR_MOBILITY_COUNTER2_OFFSET);
    s.atkFlags = ReadFrameWord(s.frameData, FRAME_ATTACK_PROPS_OFFSET);
    s.hitFlags = ReadFrameWord(s.frameData, FRAME_HIT_PROPS_OFFSET);
    s.guardFlags = ReadFrameWord(s.frameData, FRAME_GUARD_PROPS_OFFSET);
    s.baseDamage = ReadFrameShort(s.frameData, ATTACK_DATA_BASE_DAMAGE_OFFSET);
    s.chipDamage = ReadFrameShort(s.frameData, ATTACK_DATA_CHIP_DAMAGE_OFFSET);
    s.attackerFreeze = ReadFrameShort(s.frameData, ATTACK_DATA_ATTACKER_HITSTOP);
    s.defenderFreeze = ReadFrameShort(s.frameData, ATTACK_DATA_DEFENDER_HITSTOP);
    s.attackData = FrameHasAttackData(s);
    s.engineBusy = IsEngineBusy(s);
    return s;
}

bool ConsiderActive(const Sample& p1, const Sample& p2) {
    if (p1.engineBusy || p2.engineBusy) return true;
    if (p1.canBlockOpponent || p2.canBlockOpponent) return true;
    if (p1.canRGOpponent || p2.canRGOpponent) return true;
    return false;
}

// Refine an attacker's frame category using EFZ's real collision gate.
//   - Active when frame_data has non-empty boxes AND the collision code's
//     frame_data+0xE9/+0xEA gates allow those boxes to collide.
//   - Projectile active is treated like MBAACC's sub-active/projectile track:
//     it can produce first-active/active counts even when the character's own
//     frame block is not active.
//   - Recovery once we've already seen an active box this attack run and
//     the boxes are now empty or collision-disabled.
//   - Startup otherwise
//
// See decompilations/efz/efz.c `handlePlayerToPlayerCollision` and
// `processProjectileCollision`.
Cat RefineAttackerCat(Side& self, Cat selfCat, const Sample& sample) {
    if (selfCat != Cat::AttackStartup) return selfCat;
    if (sample.activeHitbox || sample.projectileHitbox) {
        self.haveSeenActiveBox = true;
        return Cat::AttackActive;
    }
    if (self.haveSeenActiveBox || sample.attackTimer > 0) return Cat::AttackRecovery;
    return Cat::AttackStartup;
}

void ResetSideTracking(Side& s) {
    s.prevMoveID = 0;
    s.prevAirborne = false;
    s.dashSequenceActive = false;
    s.dashSequenceStartedAirborne = false;
    s.activeRunFrames = 0;
    s.haveSeenActiveBox = false;
    s.activeCounter = 0;
    s.firstActiveCounter = 0;
    s.firstActive = -1;
    s.totalBusyCounter = 0;
    s.totalBusyMemory = 0;
    s.firstActiveCaptured = false;
    s.firstActiveEdge = false;
}

void ClearBarsNoLock(bool latched) {
    const short p1PrevMoveID = g_p1.prevMoveID;
    const bool p1PrevAirborne = g_p1.prevAirborne;
    const short p2PrevMoveID = g_p2.prevMoveID;
    const bool p2PrevAirborne = g_p2.prevAirborne;
    for (int i = 0; i < kBarMemory; ++i) {
        g_p1.cells[i] = Cell{};
        g_p2.cells[i] = Cell{};
    }
    ResetSideTracking(g_p1);
    ResetSideTracking(g_p2);
    if (!latched) {
        g_p1.prevMoveID = p1PrevMoveID;
        g_p1.prevAirborne = p1PrevAirborne;
        g_p2.prevMoveID = p2PrevMoveID;
        g_p2.prevAirborne = p2PrevAirborne;
    }
    g_writeIdx = 0;
    g_filledFrames = 0;
    g_idleTailFrames = latched ? IdleTailMaxFrames() : 0;
    g_latched = latched;
    g_sharedHitstopRun = 0;
}

void UpdateSideCounters(Side& s, const Sample& sample, Cat rawCat, bool anyFreeze) {
    s.firstActiveEdge = false;
    const bool isAttackState =
        rawCat == Cat::AttackStartup ||
        rawCat == Cat::AttackActive ||
        rawCat == Cat::AttackRecovery;
    const bool newMove = sample.moveID != s.prevMoveID;

    if (newMove) {
        s.activeRunFrames = 0;
        s.haveSeenActiveBox = false;
        if (isAttackState || sample.engineBusy) {
            s.activeCounter = 0;
            s.firstActiveCounter = 0;
            s.firstActive = -1;
            s.firstActiveCaptured = false;
        }
    }

    if (!isAttackState && !sample.engineBusy) {
        s.activeRunFrames = 0;
        s.haveSeenActiveBox = false;
        s.activeCounter = 0;
        s.firstActiveCounter = 0;
        s.firstActiveCaptured = false;
        s.firstActive = -1;
        s.totalBusyCounter = 0;
    } else if (!anyFreeze) {
        if (sample.engineBusy) {
            ++s.totalBusyCounter;
            s.totalBusyMemory = s.totalBusyCounter;
            if (!s.firstActiveCaptured) ++s.firstActiveCounter;
        } else {
            s.totalBusyCounter = 0;
        }

        if (isAttackState) ++s.activeRunFrames;
    }

    const bool activeNow = sample.activeHitbox || sample.projectileHitbox;
    if (!anyFreeze && activeNow) {
        ++s.activeCounter;
        if (!s.firstActiveCaptured) {
            s.firstActive = s.firstActiveCounter;
            s.firstActiveCaptured = true;
            s.firstActiveEdge = true;
        }
    } else if (!isAttackState && !sample.projectileHitbox) {
        s.activeCounter = 0;
    }

}

Cat PrevCat(const Side& side, int slot) {
    const int prev = (slot - 1 + kBarMemory) % kBarMemory;
    return side.cells[prev].cat;
}

int CatVisualPriority(Cat cat) {
    switch (cat) {
        case Cat::AttackActive: return 100;
        case Cat::Thrown: return 95;
        case Cat::Hitstun:
        case Cat::Launched:
        case Cat::Blockstun:
        case Cat::SpecialStun: return 90;
        case Cat::SuperFlash: return 88;
        case Cat::HitstopShared: return 86;
        case Cat::RG: return 84;
        case Cat::AttackStartup:
        case Cat::AttackRecovery: return 80;
        case Cat::AirtechFwd:
        case Cat::AirtechBack:
        case Cat::Groundtech: return 76;
        case Cat::DashFwd:
        case Cat::DashBack:
        case Cat::AirDashNeutral:
        case Cat::AirDashFwd:
        case Cat::AirDashBack: return 70;
        case Cat::PreJump:
        case Cat::JumpNeutral:
        case Cat::JumpFwd:
        case Cat::JumpBack:
        case Cat::DoubleJumpNeutral:
        case Cat::DoubleJumpFwd:
        case Cat::DoubleJumpBack:
        case Cat::Falling:
        case Cat::Landing: return 60;
        case Cat::WalkFwd:
        case Cat::WalkBack:
        case Cat::Crouch:
        case Cat::Neutral: return 40;
        default: return 0;
    }
}

void MergeVisualCell(Cell& out, const Cell& in) {
    if (in.cat == Cat::None) return;
    const int inPriority = CatVisualPriority(in.cat);
    const int outPriority = CatVisualPriority(out.cat);
    if (out.cat == Cat::None || inPriority >= outPriority) {
        const Cat chosenCat = in.cat;
        out.moveID = in.moveID;
        out.frameIdx = in.frameIdx;
        out.cat = chosenCat;
    }

    if (in.attackBoxes > out.attackBoxes) out.attackBoxes = in.attackBoxes;
    if (in.projectileBoxes > out.projectileBoxes) out.projectileBoxes = in.projectileBoxes;
    if (in.projectiles > out.projectiles) out.projectiles = in.projectiles;
    if (in.blockstun > out.blockstun) out.blockstun = in.blockstun;
    if (in.untech > out.untech) out.untech = in.untech;
    if (in.hitstop > out.hitstop) out.hitstop = in.hitstop;
    if (in.airTime > out.airTime) out.airTime = in.airTime;
    if (in.combo > out.combo) out.combo = in.combo;
    if (in.comboTimer > out.comboTimer) out.comboTimer = in.comboTimer;
    if (in.rgCooldown > out.rgCooldown) out.rgCooldown = in.rgCooldown;
    if (in.frameLockout > out.frameLockout) out.frameLockout = in.frameLockout;
    if (in.collisionLockout > out.collisionLockout) out.collisionLockout = in.collisionLockout;
    if (in.attackTimer > out.attackTimer) out.attackTimer = in.attackTimer;
    if (in.superflash > out.superflash) out.superflash = in.superflash;
    if (in.stateLockout > out.stateLockout) out.stateLockout = in.stateLockout;
    if (in.specialState > out.specialState) out.specialState = in.specialState;
    if (in.baseDamage > out.baseDamage) out.baseDamage = in.baseDamage;
    if (in.chipDamage > out.chipDamage) out.chipDamage = in.chipDamage;
    if (in.attackerFreeze > out.attackerFreeze) out.attackerFreeze = in.attackerFreeze;
    if (in.defenderFreeze > out.defenderFreeze) out.defenderFreeze = in.defenderFreeze;
    if (in.airMobility1 > out.airMobility1) out.airMobility1 = in.airMobility1;
    if (in.airMobility2 > out.airMobility2) out.airMobility2 = in.airMobility2;
    if (in.activeCount > out.activeCount) out.activeCount = in.activeCount;
    if (in.totalBusyFrames > out.totalBusyFrames) out.totalBusyFrames = in.totalBusyFrames;
    if (in.firstActive >= 0 && (out.firstActive < 0 || in.firstActive < out.firstActive)) out.firstActive = in.firstActive;
    if (in.guardGauge > out.guardGauge) out.guardGauge = in.guardGauge;

    out.atkFlags |= in.atkFlags;
    out.hitFlags |= in.hitFlags;
    out.guardFlags |= in.guardFlags;
    out.activeHitbox = out.activeHitbox || in.activeHitbox;
    out.projectileHitbox = out.projectileHitbox || in.projectileHitbox;
    out.inHitstop = out.inHitstop || in.inHitstop;
    out.attackData = out.attackData || in.attackData;
    out.engineBusy = out.engineBusy || in.engineBusy;
    out.canBlockNow = out.canBlockNow || in.canBlockNow;
    out.canRGNow = out.canRGNow || in.canRGNow;
    out.firstActiveMarker = out.firstActiveMarker || in.firstActiveMarker;
    if (in.hitState != 0) out.hitState = in.hitState;
    if (in.guardFlag != 0) out.guardFlag = in.guardFlag;
    if (in.counterHit != 0) out.counterHit = in.counterHit;
}

} // namespace

void Reset() {
    std::lock_guard<std::mutex> lock(g_lock);
    ClearBarsNoLock(true);
}

void ResetTimeline() {
    std::lock_guard<std::mutex> lock(g_lock);
    ClearBarsNoLock(true);
    g_pausedStep = PausedStepState{};
}

void TickSample() {
    if (!g_enabled.load()) return;

    if (!AreCharactersInitialized()) {
        Reset();
        return;
    }

    const uintptr_t b1 = GetPlayerBase(1);
    const uintptr_t b2 = GetPlayerBase(2);
    if (!b1 || !b2) {
        Reset();
        return;
    }

    auto& s_wasPaused = g_pausedStep.wasPaused;
    auto& s_lastPausedStep = g_pausedStep.lastStep;
    const bool paused = Framestep::IsPaused() || PauseIntegration::IsPausedOrFrozen();
    if (paused) {
        const unsigned int step = Framestep::GetStepCounter();
        if (!s_wasPaused) {
            s_wasPaused = true;
            s_lastPausedStep = step;
            return;
        }
        if (step == s_lastPausedStep) {
            return;
        }
        s_lastPausedStep = step;
    } else {
        s_wasPaused = false;
        s_lastPausedStep = Framestep::GetStepCounter();
    }

    Sample p1 = ReadSample(b1);
    Sample p2 = ReadSample(b2);
    p1.canBlockOpponent = CanBlockLikeEFZ(p1, p2);
    p2.canBlockOpponent = CanBlockLikeEFZ(p2, p1);
    p1.canRGOpponent = CanRecoilGuardLikeEFZ(p1, p2);
    p2.canRGOpponent = CanRecoilGuardLikeEFZ(p2, p1);

    const bool sharedHitstop = IsSharedHitstop(p1.stateTimer, p2.stateTimer);
    const bool screenFreeze = (p1.superFreeze > 0) || (p2.superFreeze > 0);
    const bool anyFreeze = screenFreeze || sharedHitstop;

    const bool active = ConsiderActive(p1, p2);

    std::lock_guard<std::mutex> lock(g_lock);

    // MBAACC-style activity gating with deferred reset:
    //
    //   active && latched   → wipe the bar and start a new sequence (reset edge)
    //   active && !latched  → idle counter = 0, advance and sample
    //   !active && idle<max → tail period, advance and sample
    //   !active && idle>=max→ latch: keep current cells frozen on screen,
    //                          stop advancing until next active arrives
    if (active) {
        if (g_latched) {
            ClearBarsNoLock(false);
        }
        g_idleTailFrames = 0;
    } else {
        if (g_idleTailFrames < IdleTailMaxFrames()) {
            ++g_idleTailFrames;
        } else {
            g_latched = true;
            g_p1.prevMoveID = p1.moveID;
            g_p1.prevAirborne = IsAirborneForMovement(p1);
            g_p2.prevMoveID = p2.moveID;
            g_p2.prevAirborne = IsAirborneForMovement(p2);
            return;
        }
    }

    Cat raw1 = ClassifySample(g_p1, p1);
    Cat raw2 = ClassifySample(g_p2, p2);

    UpdateSideCounters(g_p1, p1, raw1, anyFreeze);
    UpdateSideCounters(g_p2, p2, raw2, anyFreeze);

    Cat cat1 = RefineAttackerCat(g_p1, raw1, p1);
    Cat cat2 = RefineAttackerCat(g_p2, raw2, p2);

    // EFZ superflash is not hit-hitstop; keep the two visually separate.
    auto applyFreeze = [](Cat& cat, bool isSharedHitstop, bool isScreenFreeze) {
        if (cat == Cat::AttackActive) return;       // keep red so "frozen on active" is obvious
        if (isScreenFreeze) {
            cat = Cat::SuperFlash;
            return;
        }
        if (isSharedHitstop) {
            cat = Cat::HitstopShared;
        }
    };
    applyFreeze(cat1, sharedHitstop, screenFreeze);
    applyFreeze(cat2, sharedHitstop, screenFreeze);

    Cell& c1 = g_p1.cells[g_writeIdx];
    Cell& c2 = g_p2.cells[g_writeIdx];
    auto fillCell = [&](Cell& c, const Sample& s, const Side& side, Cat cat) {
        c.cat = cat;
        c.moveID = s.moveID;
        c.frameIdx = s.frameIdx;
        c.attackBoxes = (char)s.attackBoxes;
        c.projectileBoxes = (short)s.projectileBoxes;
        c.projectiles = (char)s.projectiles;
        c.activeHitbox = s.activeHitbox;
        c.projectileHitbox = s.projectileHitbox;
        c.blockstun   = s.stateTimer;     // +0x14A: hitstop / blockstun (multi-purpose)
        c.untech      = s.untech;         // +0x124: untech / stun-duration
        c.hitstop     = s.superFreeze;    // +0x14C: own superflash freeze
        c.airTime     = s.airTime;
        c.combo       = s.combo;
        c.comboTimer  = s.comboTimer;
        c.rgCooldown  = s.rgCooldown;
        c.frameLockout= s.frameLockout;
        c.stateLockout= s.stateLockout;
        c.specialState= s.specialState;
        c.collisionLockout = s.cooldown4;
        c.attackTimer = s.attackTimer;
        c.hitState    = s.hitState;
        c.guardFlag   = s.guardFlag;
        c.counterHit  = s.counterHit;
        c.guardGauge  = s.guardGauge;
        c.superflash  = s.superflash;
        c.atkFlags    = s.atkFlags;
        c.hitFlags    = s.hitFlags;
        c.guardFlags  = s.guardFlags;
        c.baseDamage  = s.baseDamage;
        c.chipDamage  = s.chipDamage;
        c.attackerFreeze = s.attackerFreeze;
        c.defenderFreeze = s.defenderFreeze;
        c.airMobility1 = s.airMobility1;
        c.airMobility2 = s.airMobility2;
        c.inHitstop   = sharedHitstop;
        c.attackData  = s.attackData;
        c.engineBusy  = s.engineBusy;
        c.canBlockNow = s.canBlockOpponent;
        c.canRGNow    = s.canRGOpponent;
        c.firstActive = side.firstActive;
        c.activeCount = side.activeCounter;
        c.totalBusyFrames = side.totalBusyMemory;
        c.firstActiveMarker = side.firstActiveEdge;
    };
    fillCell(c1, p1, g_p1, cat1);
    fillCell(c2, p2, g_p2, cat2);

    g_p1.prevMoveID = p1.moveID;
    g_p1.prevAirborne = IsAirborneForMovement(p1);
    g_p2.prevMoveID = p2.moveID;
    g_p2.prevAirborne = IsAirborneForMovement(p2);

    g_writeIdx = (g_writeIdx + 1) % kBarMemory;
    if (g_filledFrames < kBarMemory * 8) g_filledFrames++;
}

void Render(const DrawCtx& ctx) {
    if (!g_enabled.load()) return;
    if (!AreCharactersInitialized()) return;
    if (!GetPlayerBase(1) || !GetPlayerBase(2)) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    const int detailMode = DetailMode();
    const bool showStatus = detailMode != kDetailBarsOnly;
    const bool compactStatus = detailMode == kDetailCompact;
    const bool showCoreMarkers = detailMode != kDetailBarsOnly;
    const bool showAdvancedMarkers = detailMode == kDetailFull;
    const int statusLineCount = showStatus ? (compactStatus ? 2 : 4) : 0;

    // Layout in 640x480 virtual space, transformed via ctx into RT space.
    constexpr int   kVisible    = 80;
    constexpr float baseW       = 480.0f;
    constexpr float baseRowH    = 9.0f;
    constexpr float rowGap      = 2.0f;
    constexpr float baseX       = (640.0f - baseW) * 0.5f;
    constexpr float lineHBase   = 11.0f;
    constexpr float kStatusY    = 360.0f;                       // text top
    const float kBarY = kStatusY + (float)statusLineCount * lineHBase + (showStatus ? 6.0f : 0.0f);
    const float baseY = kBarY;

    auto tx = [&](float x) { return ctx.ox + x * ctx.scale; };
    auto ty = [&](float y) { return ctx.oy + y * ctx.scale; };

    const float cellW = baseW / (float)kVisible;

    std::lock_guard<std::mutex> lock(g_lock);

    if (g_filledFrames == 0) return;

    // Visual-frame mode folds each group of three subframes into one displayed
    // cell so 1-subframe events are not lost.
    const int total = (g_filledFrames < kBarMemory) ? g_filledFrames : kBarMemory;
    const bool visualFrameCells = VisualFrameSampling();
    const int stride = visualFrameCells ? static_cast<int>(SUBFRAMES_PER_VISUAL_FRAME) : 1;
    const int maxSamples = kVisible * stride;
    const int samplesToShow = (total < maxSamples) ? total : maxSamples;
    const int show = visualFrameCells
        ? ((samplesToShow + stride - 1) / stride)
        : samplesToShow;

    auto drawRow = [&](const Side& side, float y) {
        dl->AddRectFilled(
            ImVec2(tx(baseX - 2.0f), ty(y - 1.0f)),
            ImVec2(tx(baseX + baseW + 2.0f), ty(y + baseRowH + 1.0f)),
            IM_COL32(0, 0, 0, 180));

        for (int i = 0; i < show; ++i) {
            Cell visualCell{};
            const Cell* cellPtr = nullptr;
            if (visualFrameCells) {
                const int groupOldestOffset = samplesToShow - i * stride;
                const int groupNewestOffsetExclusive = groupOldestOffset - stride > 0
                    ? groupOldestOffset - stride
                    : 0;
                for (int offset = groupOldestOffset; offset > groupNewestOffsetExclusive; --offset) {
                    const int slot = (g_writeIdx - offset + kBarMemory) % kBarMemory;
                    MergeVisualCell(visualCell, side.cells[slot]);
                }
                cellPtr = &visualCell;
            } else {
                const int slot = (g_writeIdx - show + i + kBarMemory) % kBarMemory;
                cellPtr = &side.cells[slot];
            }
            const Cell& cell = *cellPtr;
            if (cell.cat == Cat::None) continue;
            const float cx = baseX + (float)i * cellW;
            dl->AddRectFilled(
                ImVec2(tx(cx),         ty(y)),
                ImVec2(tx(cx + cellW - 0.5f), ty(y + baseRowH)),
                ColorFor(cell.cat));
            // Defender frame-state markers from frame_data +0xB0 and the
            // +0x130/+0x140 lockouts the collision code checks.
            if (showAdvancedMarkers &&
                ((cell.hitFlags & FRAME_HIT_FLAG_DEFENDER_IMMUNE) != 0 ||
                 cell.frameLockout > 0 || cell.collisionLockout > 0)) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),         ty(y)),
                    ImVec2(tx(cx + 1.0f),  ty(y + baseRowH)),
                    IM_COL32(230, 230, 230, 220));
            }
            if (showAdvancedMarkers && (cell.hitFlags & FRAME_HIT_FLAG_COUNTER_VULN) != 0) {
                dl->AddRectFilled(
                    ImVec2(tx(cx + 1.0f),  ty(y)),
                    ImVec2(tx(cx + 2.0f),  ty(y + baseRowH)),
                    IM_COL32(255, 90, 210, 190));
            }
            if (showAdvancedMarkers &&
                (cell.hitFlags & (FRAME_HIT_FLAG_AIRTHROW_VULN | FRAME_HIT_FLAG_GROUND_THROW_VULN)) != 0) {
                dl->AddRectFilled(
                    ImVec2(tx(cx + 2.0f),  ty(y)),
                    ImVec2(tx(cx + 3.0f),  ty(y + baseRowH)),
                    IM_COL32(180, 90, 255, 190));
            }
            // Hitstop stripe - shared hit-hitstop only. Super/IC flash uses
            // its own magenta category so it does not read as hitstop.
            if (showAdvancedMarkers && cell.inHitstop) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),                ty(y + baseRowH * 0.35f)),
                    ImVec2(tx(cx + cellW - 0.5f), ty(y + baseRowH * 0.65f)),
                    IM_COL32(60, 80, 128, 180));
            }
            // Collision-active character box indicator. Raw attack boxes that
            // fail EFZ's frame_data+0xE9/+0xEA gate are shown separately below.
            if (showCoreMarkers && cell.activeHitbox && cell.cat != Cat::AttackActive) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),                ty(y + baseRowH - 1.5f)),
                    ImVec2(tx(cx + cellW - 0.5f), ty(y + baseRowH)),
                    IM_COL32(255, 50, 50, 230));
            }
            if (showCoreMarkers && cell.projectiles > 0) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),                ty(y)),
                    ImVec2(tx(cx + cellW - 0.5f), ty(y + 1.0f)),
                    IM_COL32(255, 140, 0, 230));
            }
            // Projectile frame data has active attack boxes, unlike slot presence.
            if (showCoreMarkers && cell.projectileHitbox) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),                ty(y + 1.0f)),
                    ImVec2(tx(cx + cellW - 0.5f), ty(y + 2.0f)),
                    IM_COL32(255, 170, 40, 235));
            }
            // Attack-data present with no collision-capable box out
            // (MBAACC attackDataPtr strip).
            if (showAdvancedMarkers &&
                cell.attackData && !cell.activeHitbox && !cell.projectileHitbox &&
                (cell.cat == Cat::AttackStartup || cell.cat == Cat::AttackRecovery)) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),                ty(y + baseRowH - 1.5f)),
                    ImVec2(tx(cx + cellW - 0.5f), ty(y + baseRowH)),
                    IM_COL32(170, 70, 70, 220));
            }
            // +0x16C is a post-resolution countdown; it is not an active-box
            // proof, so keep it muted.
            if (showAdvancedMarkers && cell.attackTimer > 0 && !cell.activeHitbox) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),                ty(y + baseRowH - 2.5f)),
                    ImVec2(tx(cx + cellW - 0.5f), ty(y + baseRowH - 1.5f)),
                    IM_COL32(150, 100, 70, 180));
            }
            // Pairwise engine checks from validateBlockStance/canPerformRG:
            // blue = this side can block the current opposing attack,
            // cyan = this side can RG it.
            if (showAdvancedMarkers && cell.canBlockNow) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),                ty(y + 2.0f)),
                    ImVec2(tx(cx + cellW - 0.5f), ty(y + 3.0f)),
                    IM_COL32(80, 150, 255, 220));
            }
            if (showAdvancedMarkers && cell.canRGNow) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),                ty(y + 3.0f)),
                    ImVec2(tx(cx + cellW - 0.5f), ty(y + 4.0f)),
                    IM_COL32(120, 230, 255, 235));
            }
            if (showAdvancedMarkers && (cell.airMobility1 || cell.airMobility2)) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),                ty(y + baseRowH * 0.45f)),
                    ImVec2(tx(cx + cellW - 0.5f), ty(y + baseRowH * 0.45f + 1.0f)),
                    IM_COL32(190, 130, 255, 210));
            }
            if (showCoreMarkers && cell.firstActiveMarker) {
                dl->AddLine(
                    ImVec2(tx(cx + 0.5f), ty(y - 1.0f)),
                    ImVec2(tx(cx + 0.5f), ty(y + baseRowH + 1.0f)),
                    IM_COL32(255, 255, 255, 240), 1.0f);
            }
            // Raw +0x168 activity marker. Colors distinguish observed numeric
            // values only; they must not be read as authoritative outcomes.
            if (showAdvancedMarkers && cell.hitState != 0) {
                ImU32 col = IM_COL32(255, 220, 0, 230);     // default raw value
                if (cell.hitState == 2) col = IM_COL32(80, 200, 255, 230); // raw 2
                if (cell.hitState == 6) col = IM_COL32(180, 60, 200, 230); // raw 6
                dl->AddRectFilled(
                    ImVec2(tx(cx + cellW - 1.5f), ty(y - 1.0f)),
                    ImVec2(tx(cx + cellW),         ty(y + baseRowH + 1.0f)),
                    col);
            }
            if (showAdvancedMarkers && cell.counterHit) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),                ty(y + 1.0f)),
                    ImVec2(tx(cx + cellW - 0.5f), ty(y + baseRowH * 0.45f)),
                    IM_COL32(255, 60, 200, 220));
            }
            if (showAdvancedMarkers &&
                cell.untech > 0 && (cell.cat == Cat::Hitstun || cell.cat == Cat::Launched)) {
                dl->AddRectFilled(
                    ImVec2(tx(cx),                ty(y + baseRowH * 0.5f)),
                    ImVec2(tx(cx + cellW - 0.5f), ty(y + baseRowH * 0.5f + 1.0f)),
                    IM_COL32(255, 255, 100, 230));
            }
        }
        dl->AddLine(
            ImVec2(tx(baseX + baseW), ty(y - 2.0f)),
            ImVec2(tx(baseX + baseW), ty(y + baseRowH + 2.0f)),
            IM_COL32(255, 255, 255, 220), 1.5f);
    };

    auto latest = [&](const Side& s) -> const Cell& {
        const int slot = (g_writeIdx - 1 + kBarMemory) % kBarMemory;
        return s.cells[slot];
    };

    const Cell& cl1 = latest(g_p1);
    const Cell& cl2 = latest(g_p2);
    // Field legend:
    //   ST  = +0x14A engine state-timer (hit-hitstop on attacker, blockstun/
    //         hitstun freeze on defender) - 0 means the engine is ticking.
    //   UT  = +0x124 untech / stun-duration set by the most-recent hit.
    //   SF  = +0x14C super-flash freeze on this player (only the activator).
    //   AT  = +0x14E airTime counter (RG locks at >= 30 internal frames).
    //   AM  = +0x159/+0x15A air-mobility counters reset by the engine on land.
    //   FA  = first active frame in this sequence, MBAACC-style.
    //   ACT = consecutive active/projectile frames.
    //   BX  = character boxes that are actually collision-active / raw boxes.
    //   PB  = projectile attack boxes that are actually collision-active.
    //   P   = alive projectile slots.
    //   ATK = +0x16C post-resolution attacker countdown.
    //   FL/CL = +0x130/+0x140 decomp-derived frame/collision lockouts.
    //   TOT = total engine-busy frames seen in this sequence.
    //   GG  = +0x134 Guard Gauge (0..360).
    //   B/R = decomp-style can-block/can-RG result vs opponent current attack.
    //   HitS= raw +0x168 producer latch (diagnostic, not a typed result).
    //   CH  = +0x144 counter-hit flag this frame.
    //   HST = shared hit-hitstop. SF still reports the local superflash freeze.
    char buf[160] = {}, buf2[160] = {}, buf3[160] = {}, buf4[160] = {};
    if (showStatus && compactStatus) {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "P1 %-8s ID:%-4d F:%-3d FA:%-3d ACT:%-3d BX:%d/%d PB:%d P:%d",
                    I18n::Tr(CatName(cl1.cat)), (int)cl1.moveID, (int)cl1.frameIdx,
                    cl1.firstActive, cl1.activeCount,
                    cl1.activeHitbox ? (int)cl1.attackBoxes : 0, (int)cl1.attackBoxes,
                    (int)cl1.projectileBoxes, (int)cl1.projectiles);
        _snprintf_s(buf3, sizeof(buf3), _TRUNCATE,
                    "P2 %-8s ID:%-4d F:%-3d FA:%-3d ACT:%-3d BX:%d/%d PB:%d P:%d",
                    I18n::Tr(CatName(cl2.cat)), (int)cl2.moveID, (int)cl2.frameIdx,
                    cl2.firstActive, cl2.activeCount,
                    cl2.activeHitbox ? (int)cl2.attackBoxes : 0, (int)cl2.attackBoxes,
                    (int)cl2.projectileBoxes, (int)cl2.projectiles);
    } else if (showStatus) {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "P1 %-8s ID:%-4d F:%-3d BX:%d/%d PB:%d P:%d AM:%d/%d ST:%-3d UT:%-3d ATK:%-3d",
                    I18n::Tr(CatName(cl1.cat)), (int)cl1.moveID, (int)cl1.frameIdx,
                    cl1.activeHitbox ? (int)cl1.attackBoxes : 0, (int)cl1.attackBoxes,
                    (int)cl1.projectileBoxes, (int)cl1.projectiles,
                    (int)cl1.airMobility1, (int)cl1.airMobility2,
                    (int)cl1.blockstun, (int)cl1.untech,
                    (int)cl1.attackTimer);
        _snprintf_s(buf2, sizeof(buf2), _TRUNCATE,
                    "   FA:%-3d ACT:%-3d TOT:%-3d B:%d RG:%d G:%d HS:%d CH:%d GG:%5.1f SF:%-3d HST:%d FL:%-2d CL:%-2d",
                    cl1.firstActive, cl1.activeCount, cl1.totalBusyFrames,
                    cl1.canBlockNow ? 1 : 0, cl1.canRGNow ? 1 : 0,
                    cl1.guardFlag, cl1.hitState, cl1.counterHit, cl1.guardGauge,
                    (int)cl1.hitstop, cl1.inHitstop ? 1 : 0,
                    (int)cl1.frameLockout, (int)cl1.collisionLockout);
        _snprintf_s(buf3, sizeof(buf3), _TRUNCATE,
                    "P2 %-8s ID:%-4d F:%-3d BX:%d/%d PB:%d P:%d AM:%d/%d ST:%-3d UT:%-3d ATK:%-3d",
                    I18n::Tr(CatName(cl2.cat)), (int)cl2.moveID, (int)cl2.frameIdx,
                    cl2.activeHitbox ? (int)cl2.attackBoxes : 0, (int)cl2.attackBoxes,
                    (int)cl2.projectileBoxes, (int)cl2.projectiles,
                    (int)cl2.airMobility1, (int)cl2.airMobility2,
                    (int)cl2.blockstun, (int)cl2.untech,
                    (int)cl2.attackTimer);
        _snprintf_s(buf4, sizeof(buf4), _TRUNCATE,
                    "   FA:%-3d ACT:%-3d TOT:%-3d B:%d RG:%d G:%d HS:%d CH:%d GG:%5.1f SF:%-3d HST:%d FL:%-2d CL:%-2d",
                    cl2.firstActive, cl2.activeCount, cl2.totalBusyFrames,
                    cl2.canBlockNow ? 1 : 0, cl2.canRGNow ? 1 : 0,
                    cl2.guardFlag, cl2.hitState, cl2.counterHit, cl2.guardGauge,
                    (int)cl2.hitstop, cl2.inHitstop ? 1 : 0,
                    (int)cl2.frameLockout, (int)cl2.collisionLockout);
    }

    const float lineH = lineHBase * ctx.scale;
    const float textPad = 4.0f;

    const float blockTop    = ty((showStatus ? kStatusY : kBarY) - 4.0f);
    const float blockBottom = ty(kBarY + (baseRowH + rowGap) * 2.0f + 4.0f);
    dl->AddRectFilled(
        ImVec2(tx(baseX - 4.0f), blockTop),
        ImVec2(tx(baseX + baseW + 4.0f), blockBottom),
        IM_COL32(0, 0, 0, 200));

    if (showStatus) {
        const float textY0 = ty(kStatusY + textPad);
        dl->AddText(ImVec2(tx(baseX), textY0 + 0.0f * lineH),
                    IM_COL32(220, 220, 255, 230), buf);
        if (compactStatus) {
            dl->AddText(ImVec2(tx(baseX), textY0 + 1.0f * lineH),
                        IM_COL32(255, 220, 220, 230), buf3);
        } else {
            dl->AddText(ImVec2(tx(baseX), textY0 + 1.0f * lineH),
                        IM_COL32(180, 180, 220, 220), buf2);
            dl->AddText(ImVec2(tx(baseX), textY0 + 2.0f * lineH),
                        IM_COL32(255, 220, 220, 230), buf3);
            dl->AddText(ImVec2(tx(baseX), textY0 + 3.0f * lineH),
                        IM_COL32(220, 180, 180, 220), buf4);
        }
    }

    drawRow(g_p1, baseY);
    drawRow(g_p2, baseY + baseRowH + rowGap);
}

} // namespace FrameBar
