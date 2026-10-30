#include "../include/game/collision_hook.h"
#include "../include/core/logger.h"
#include "../include/core/memory.h"
#include "../include/utils/utilities.h"
#include "../include/input/input_core.h"
#include "../3rdparty/minhook/include/MinHook.h"
#include "../include/game/practice_patch.h"
#include "../include/game/game_state.h"
#include "../include/core/globals.h"
#include <windows.h>
#include <atomic>
#include <array>
#include <algorithm>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include <mutex>
#include "runtime/native_game_profile.h"
#include "../include/core/constants.h"
#include "../include/gui/overlay.h"
#include "../include/utils/minhook_utils.h"

// Offset of handlePlayerToPlayerCollision relative to module base
static constexpr uintptr_t HANDLE_P2P_COLLISION_OFFSET = 0x367F60;
// Offset of processProjectileCollision (entity attack -> player) relative to
// the 0x400000 image base.  Retail VA: 0x7697D0.
static constexpr uintptr_t HANDLE_ENTITY_TO_PLAYER_COLLISION_OFFSET = 0x3697D0;

using tHandleP2PCollision = void(__thiscall*)(void* gameSystem, int attackerPtr, int defenderPtr, int attackerFrameData, const void* defenderFrameData);
static tHandleP2PCollision oHandleP2PCollision = nullptr;
using tHandleEntityToPlayerCollision = void(__thiscall*)(
    void* gameSystem, int ownerPtr, int defenderPtr, int entitySlot,
    const void* defenderFrameData);
static tHandleEntityToPlayerCollision oHandleEntityToPlayerCollision = nullptr;

static std::atomic<uintptr_t> g_lastAttackDataP1{0};
static std::atomic<uintptr_t> g_lastAttackDataP2{0};
static std::atomic<int> g_attackDataOffsetP1{-1};
static std::atomic<int> g_attackDataOffsetP2{-1};
static std::atomic<bool> s_collisionHookCreated{false};
static std::atomic<bool> s_collisionHookEnabled{false};
static uintptr_t s_collisionHookTargetAddr = 0;
static std::atomic<bool> s_entityCollisionHookCreated{false};
static std::atomic<bool> s_entityCollisionHookEnabled{false};
static uintptr_t s_entityCollisionHookTargetAddr = 0;

static Mission::Contact::Journal<128> s_contactJournal;

namespace {
bool ResolveCollisionHookTarget(uintptr_t& targetAddr) {
    uintptr_t base = GetEFZBase();
    if (!base) {
        LogOut("[COLLISION_HOOK] Failed to get game base address.", true);
        return false;
    }

    targetAddr = base + HANDLE_P2P_COLLISION_OFFSET;
    return true;
}

bool ResolveEntityCollisionHookTarget(uintptr_t& targetAddr) {
    uintptr_t base = GetEFZBase();
    if (!base) {
        LogOut("[COLLISION_HOOK] Failed to get game base address for entity contact.", true);
        return false;
    }

    targetAddr = base + HANDLE_ENTITY_TO_PLAYER_COLLISION_OFFSET;
    return true;
}

bool ValidateCollisionHookTarget(uintptr_t targetAddr) {
    // Eternal Fighter Zero 1.02e retail prologue at 0x767F60:
    //   push ebp; mov ebp,esp; sub esp,0x3B4
    static const unsigned char expected[] = {
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xB4, 0x03, 0x00, 0x00
    };
    unsigned char actual[sizeof(expected)] = {};
    if (!SafeReadMemory(targetAddr, actual, sizeof(actual))) return false;
    for (size_t i = 0; i < sizeof(expected); ++i) {
        if (actual[i] != expected[i]) return false;
    }
    return true;
}

bool ValidateEntityCollisionHookTarget(uintptr_t targetAddr) {
    // Eternal Fighter Zero 1.02e retail prologue at 0x7697D0:
    //   push ebp; mov ebp,esp; sub esp,0x23C; mov [ebp-0x1BC],ecx
    // The function ends in `ret 0x10`: four stack arguments after ECX.
    static const unsigned char expected[] = {
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x3C, 0x02, 0x00, 0x00,
        0x89, 0x8D, 0x44, 0xFE, 0xFF, 0xFF
    };
    unsigned char actual[sizeof(expected)] = {};
    if (!SafeReadMemory(targetAddr, actual, sizeof(actual))) return false;
    for (size_t i = 0; i < sizeof(expected); ++i) {
        if (actual[i] != expected[i]) return false;
    }
    return true;
}

struct ContactState {
    short move = 0;
    short frame = 0;
    short attackTimer = 0;
    int hitState = 0;
    short combo = 0;              // player+0x174 is a 16-bit counter
    int hp = 0;
};

bool ReadContactStateChecked(uintptr_t player, ContactState& s) {
    if (!player) return false;

    // Established spans: move/frame, HP, and hit-state/attack-timer/combo; read the 0x168..0x175 padding once instead of per-field probes.
    constexpr std::size_t kMoveSpanBytes =
        (CURRENT_FRAME_INDEX_OFFSET + sizeof(short)) - MOVE_ID_OFFSET;
    constexpr std::size_t kResolverSpanBytes =
        (PLAYER_COMBO_COUNTER_OFFSET + sizeof(short)) - PLAYER_HIT_STATE_OFFSET;
    std::array<uint8_t, kMoveSpanBytes> moveSpan{};
    std::array<uint8_t, kResolverSpanBytes> resolverSpan{};

    const bool moveOk = SafeReadMemory(
        player + MOVE_ID_OFFSET, moveSpan.data(), moveSpan.size());
    const bool hpOk = SafeReadMemory(player + HP_OFFSET, &s.hp, sizeof(s.hp));
    const bool resolverOk = SafeReadMemory(
        player + PLAYER_HIT_STATE_OFFSET,
        resolverSpan.data(), resolverSpan.size());

    if (moveOk) {
        std::memcpy(&s.move, moveSpan.data(), sizeof(s.move));
        std::memcpy(&s.frame,
                    moveSpan.data() + CURRENT_FRAME_INDEX_OFFSET - MOVE_ID_OFFSET,
                    sizeof(s.frame));
    }
    if (resolverOk) {
        std::memcpy(&s.hitState, resolverSpan.data(), sizeof(s.hitState));
        std::memcpy(&s.attackTimer,
                    resolverSpan.data() + PLAYER_ATTACK_TIMER_OFFSET -
                        PLAYER_HIT_STATE_OFFSET,
                    sizeof(s.attackTimer));
        std::memcpy(&s.combo,
                    resolverSpan.data() + PLAYER_COMBO_COUNTER_OFFSET -
                        PLAYER_HIT_STATE_OFFSET,
                    sizeof(s.combo));
    }
    return moveOk && hpOk && resolverOk;
}

ContactState ReadContactState(uintptr_t player) {
    ContactState s;
    (void)ReadContactStateChecked(player, s);
    return s;
}

struct EntityContactState {
    uint16_t pattern = 0;
    uint16_t frame = 0;
    int rawState = 0;
    int16_t collisionLife = 0;
};

constexpr uintptr_t kEntityEntryBase = 0x4D0;
constexpr uintptr_t kEntityEntryStride = 0x98;
constexpr uintptr_t kEntityRawStateOffset = 0x80;
constexpr uintptr_t kEntityCollisionLifeOffset = 0x84;

bool ReadEntityContactState(uintptr_t owner, int slot, EntityContactState& s) {
    if (!owner || slot < 0 || slot >= 64) return false;
    const uintptr_t entity = owner + kEntityEntryBase +
        kEntityEntryStride * static_cast<uintptr_t>(slot);

    std::array<uint8_t, 4> animationSpan{};
    constexpr std::size_t kCollisionSpanBytes =
        (kEntityCollisionLifeOffset + sizeof(int16_t)) - kEntityRawStateOffset;
    std::array<uint8_t, kCollisionSpanBytes> collisionSpan{};
    const bool animationOk = SafeReadMemory(
        entity, animationSpan.data(), animationSpan.size());
    const bool collisionOk = SafeReadMemory(
        entity + kEntityRawStateOffset,
        collisionSpan.data(), collisionSpan.size());
    if (animationOk) {
        std::memcpy(&s.pattern, animationSpan.data(), sizeof(s.pattern));
        std::memcpy(&s.frame, animationSpan.data() + sizeof(s.pattern),
                    sizeof(s.frame));
    }
    if (collisionOk) {
        std::memcpy(&s.rawState, collisionSpan.data(), sizeof(s.rawState));
        std::memcpy(&s.collisionLife,
                    collisionSpan.data() + kEntityCollisionLifeOffset -
                        kEntityRawStateOffset,
                    sizeof(s.collisionLife));
    }
    return animationOk && collisionOk;
}

struct PlayerPointerSnapshot {
    uintptr_t p1 = 0;
    uintptr_t p2 = 0;
};

bool ReadPlayerPointers(PlayerPointerSnapshot& out) {
    static_assert(EFZ_BASE_OFFSET_P2 == EFZ_BASE_OFFSET_P1 + sizeof(uint32_t),
                  "EFZ player pointers must remain adjacent 32-bit slots");
    const uintptr_t base = GetEFZBase();
    if (!base) return false;
    uint32_t raw[2] = {};
    if (!SafeReadMemory(base + EFZ_BASE_OFFSET_P1, raw, sizeof(raw))) {
        return false;
    }
    out.p1 = static_cast<uintptr_t>(raw[0]);
    out.p2 = static_cast<uintptr_t>(raw[1]);
    return true;
}

int PlayerNumberForPointer(uintptr_t player,
                           const PlayerPointerSnapshot& players) {
    if (!player) return 0;
    if (player == players.p1) return 1;
    if (player == players.p2) return 2;
    return 0;
}

struct CollisionBranchFlags {
    uint16_t attackFlags = 0;
    uint16_t defenderAttackFlags = 0;
    uint32_t defenderFrameFlags = 0;
};

bool ReadCollisionBranchFlags(const void* attackerFrameData,
                              const void* defenderFrameData,
                              CollisionBranchFlags& out) {
    if (!attackerFrameData || !defenderFrameData) return false;
    constexpr uintptr_t kAttackFlagsOffset = 170;
    constexpr uintptr_t kFrameFlagsOffset = 176;
    constexpr std::size_t kDefenderSpanBytes =
        kFrameFlagsOffset + sizeof(uint32_t) - kAttackFlagsOffset;
    std::array<uint8_t, kDefenderSpanBytes> defenderSpan{};
    const bool attackerOk = SafeReadMemory(
        reinterpret_cast<uintptr_t>(attackerFrameData) + kAttackFlagsOffset,
        &out.attackFlags, sizeof(out.attackFlags));
    const bool defenderOk = SafeReadMemory(
        reinterpret_cast<uintptr_t>(defenderFrameData) + kAttackFlagsOffset,
        defenderSpan.data(), defenderSpan.size());
    if (defenderOk) {
        std::memcpy(&out.defenderAttackFlags, defenderSpan.data(),
                    sizeof(out.defenderAttackFlags));
        std::memcpy(&out.defenderFrameFlags,
                    defenderSpan.data() + kFrameFlagsOffset - kAttackFlagsOffset,
                    sizeof(out.defenderFrameFlags));
    }
    return attackerOk && defenderOk;
}

bool ExactThrowBranch(const CollisionBranchFlags& flags) {
    if (flags.attackFlags & 0x100) return false;
    return ((flags.defenderFrameFlags & 0x1000) && (flags.attackFlags & 1)) ||
           ((flags.defenderFrameFlags & 0x0800) && (flags.attackFlags & 2));
}

bool ExactGuardPointBranch(const CollisionBranchFlags& flags,
                           bool defenderAirborne) {
    if (!(flags.defenderFrameFlags & 0x200)) return false;
    if ((flags.defenderAttackFlags & flags.attackFlags & 1) != 0) return true;
    if ((flags.defenderAttackFlags & flags.attackFlags & 2) != 0) return true;
    return defenderAirborne &&
           (flags.defenderAttackFlags & flags.attackFlags & 4) != 0;
}

} // namespace

namespace {
constexpr size_t kCollisionLayoutBytes=0x1200;
struct CapturedCollisionLayout {
    uintptr_t player=0,resource=0,frameData=0;
    short move=-1;
    bool attempted=false,pending=false;
    size_t bytes=0;
    std::array<uint8_t,kCollisionLayoutBytes> copied{};
};
std::mutex collisionLayoutMutex;
CapturedCollisionLayout collisionLayouts[2];
std::atomic<bool> collisionLayoutSupported{false};
std::atomic<CollisionLayoutStatus> collisionLayoutStatus[2]{CollisionLayoutStatus::NotAvailableYet,CollisionLayoutStatus::NotAvailableYet};

void CaptureCollisionFrameData(int playerNum,uintptr_t attacker,uintptr_t frameData,short move) {
    if(playerNum<1 || playerNum>2 || !attacker || !frameData)return;
    auto& direct=playerNum==1?g_lastAttackDataP1:g_lastAttackDataP2;
    direct.store(frameData,std::memory_order_release);
    const size_t player=static_cast<size_t>(playerNum-1);
    if(!collisionLayoutSupported.load(std::memory_order_acquire)) {
        collisionLayoutStatus[player].store(CollisionLayoutStatus::UnsupportedLayout,std::memory_order_release);
        return;
    }
    auto& offset=playerNum==1?g_attackDataOffsetP1:g_attackDataOffsetP2;
    const int known=offset.load(std::memory_order_acquire);
    uintptr_t current=0;
    if(known>=0 && SafeReadMemory(attacker+known,&current,sizeof(current)) && current==frameData)return;
    if(known>=0)offset.store(-1,std::memory_order_release);
    collisionLayoutStatus[player].store(CollisionLayoutStatus::NotAvailableYet,std::memory_order_release);
    uintptr_t resource=0;
    if(!SafeReadMemory(attacker+ANIM_TABLE_OFFSET,&resource,sizeof(resource)) || !resource)return;
    std::unique_lock<std::mutex> lock(collisionLayoutMutex,std::try_to_lock);
    if(!lock.owns_lock())return;
    auto& capture=collisionLayouts[player];
    if(known<0 && capture.attempted && capture.player==attacker && capture.resource==resource && capture.move==move)return;
    capture.player=attacker;capture.resource=resource;capture.move=move;capture.frameData=frameData;
    capture.attempted=true;capture.pending=false;capture.bytes=0;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(attacker),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
       (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return;
    const uintptr_t end=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
    if(end<=attacker)return;
    const size_t bytes=(std::min)(kCollisionLayoutBytes,static_cast<size_t>(end-attacker));
    if(!SafeReadMemory(attacker,capture.copied.data(),bytes))return;
    capture.bytes=bytes;capture.pending=true;
}
void ResolveCapturedCollisionLayout(int playerNum) {
    if(playerNum<1 || playerNum>2)return;
    std::lock_guard<std::mutex> lock(collisionLayoutMutex);
    auto& capture=collisionLayouts[playerNum-1];
    if(!capture.pending)return;
    capture.pending=false;
    int found=-1;
    for(size_t offset=0;offset+sizeof(uint32_t)<=capture.bytes;offset+=sizeof(uint32_t)) {
        uint32_t candidate=0;std::memcpy(&candidate,capture.copied.data()+offset,sizeof(candidate));
        if(candidate==capture.frameData){found=static_cast<int>(offset);break;}
    }
    auto& offset=playerNum==1?g_attackDataOffsetP1:g_attackDataOffsetP2;
    offset.store(found,std::memory_order_release);
    collisionLayoutStatus[playerNum-1].store(found>=0?CollisionLayoutStatus::Ready:CollisionLayoutStatus::NotAvailableYet,std::memory_order_release);
}
}

// We use __fastcall wrapper to intercept __thiscall
static void __fastcall HookedHandleP2PCollision(void* gameSystem, void* /*edx*/, int attackerPtr, int defenderPtr, int attackerFrameData, const void* defenderFrameData) {
    auto hookExecution = MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedHandleP2PCollision>());
    if (!hookExecution.Admitted() || !s_collisionHookEnabled.load(std::memory_order_acquire)
        || !PracticeBattleHooksOpen()) {
        oHandleP2PCollision(gameSystem, attackerPtr, defenderPtr, attackerFrameData, defenderFrameData);
        return;
    }

    const uintptr_t attacker = static_cast<uintptr_t>(attackerPtr);
    const uintptr_t defender = static_cast<uintptr_t>(defenderPtr);
    PlayerPointerSnapshot players;
    (void)ReadPlayerPointers(players);
    const int attackerPlayer = PlayerNumberForPointer(attacker, players);
    const int defenderPlayer = PlayerNumberForPointer(defender, players);
    const ContactState beforeAttacker = ReadContactState(attacker);
    const ContactState beforeDefender = ReadContactState(defender);
    double defenderY = 0.0;
    if (defender) {
        (void)SafeReadMemory(defender + YPOS_OFFSET, &defenderY,
                             sizeof(defenderY));
    }
    CollisionBranchFlags branchFlags;
    const bool branchFlagsValid = ReadCollisionBranchFlags(
        reinterpret_cast<const void*>(attackerFrameData), defenderFrameData,
        branchFlags);
    const bool throwBranch = branchFlagsValid && ExactThrowBranch(branchFlags);
    const bool guardPointBranch = branchFlagsValid &&
        ExactGuardPointBranch(branchFlags, defenderY < 0.0);

    if(attackerFrameData>=0x00400000 && attackerFrameData<=0x0fffffff)
        CaptureCollisionFrameData(attackerPlayer,attacker,static_cast<uintptr_t>(attackerFrameData),beforeAttacker.move);

    oHandleP2PCollision(gameSystem, attackerPtr, defenderPtr, attackerFrameData, defenderFrameData);

    if (attackerPlayer == 0 || defenderPlayer == 0) return;
    const ContactState afterAttacker = ReadContactState(attacker);
    const ContactState afterDefender = ReadContactState(defender);
    const bool timerConsumed = afterAttacker.attackTimer < beforeAttacker.attackTimer;
    const bool stateResolved = afterAttacker.hitState != beforeAttacker.hitState;
    const bool comboIncreased = afterAttacker.combo > beforeAttacker.combo;
    const bool comboRestarted = Mission::Contact::CorroboratesComboRestart(
        beforeAttacker.combo, afterAttacker.combo,
        beforeDefender.hp, afterDefender.hp,
        IsHitstun(afterDefender.move) || IsLaunched(afterDefender.move),
        afterAttacker.hitState);
    Mission::Contact::DirectEvidence evidence;
    evidence.resolved = timerConsumed || (stateResolved &&
        (comboIncreased || afterDefender.hp < beforeDefender.hp ||
         afterDefender.move != beforeDefender.move));
    evidence.comboIncreased = comboIncreased;
    evidence.comboRestarted = comboRestarted;
    evidence.defenderRecoilGuard = IsRecoilGuard(afterDefender.move);
    evidence.defenderBlocked = IsBlockstunState(afterDefender.move) &&
                               !evidence.defenderRecoilGuard;
    evidence.exactThrowBranch = throwBranch && afterAttacker.hitState == 6;
    evidence.exactGuardPointBranch = guardPointBranch && afterAttacker.hitState == 2 &&
                                     !evidence.defenderBlocked &&
                                     !evidence.defenderRecoilGuard;
    evidence.rawAttackerState = afterAttacker.hitState;
    const Mission::Contact::Result result = Mission::Contact::ClassifyDirect(evidence);
    if (result == Mission::Contact::Result::None) return;

    Mission::Contact::Event event;
    event.batchId = GetCurrentBattleUpdateBatch();
    event.attacker = static_cast<uint8_t>(attackerPlayer);
    event.defender = static_cast<uint8_t>(defenderPlayer);
    event.source = Mission::Contact::Source::DirectPlayer;
    event.result = result;
    event.attackerMove = beforeAttacker.move;
    event.attackerFrame = beforeAttacker.frame;
    event.defenderMoveBefore = beforeDefender.move;
    event.defenderMove = afterDefender.move;
    event.timerBefore = beforeAttacker.attackTimer;
    event.timerAfter = afterAttacker.attackTimer;
    event.rawStateBefore = beforeAttacker.hitState;
    event.rawStateAfter = afterAttacker.hitState;
    event.comboBefore = beforeAttacker.combo;
    event.comboAfter = afterAttacker.combo;
    event.defenderHpBefore = beforeDefender.hp;
    event.defenderHpAfter = afterDefender.hp;
    s_contactJournal.Publish(event);
}

// 0x7697D0 is __thiscall(gameSystem, owner, defender, slot, defenderFrameData); call sites pass a live slot from one player's 64-entry ring and the opposing player's copied 200-byte frame record.
// Capture only bounded POD state; no logging, allocation, notation lookup, or ring scan in this hook.
static void __fastcall HookedHandleEntityToPlayerCollision(
    void* gameSystem, void* /*edx*/, int ownerPtr, int defenderPtr,
    int entitySlot, const void* defenderFrameData) {
    auto hookExecution = MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedHandleEntityToPlayerCollision>());
    if (!hookExecution.Admitted() || !s_entityCollisionHookEnabled.load(std::memory_order_acquire) ||
        !PracticeBattleHooksOpen()) {
        oHandleEntityToPlayerCollision(
            gameSystem, ownerPtr, defenderPtr, entitySlot, defenderFrameData);
        return;
    }

    const uintptr_t owner = static_cast<uintptr_t>(ownerPtr);
    const uintptr_t defender = static_cast<uintptr_t>(defenderPtr);
    PlayerPointerSnapshot players;
    (void)ReadPlayerPointers(players);
    const int ownerPlayer = PlayerNumberForPointer(owner, players);
    const int defenderPlayer = PlayerNumberForPointer(defender, players);

    EntityContactState beforeEntity;
    ContactState beforeOwner;
    ContactState beforeDefender;
    const bool beforeValid = ownerPlayer != 0 && defenderPlayer != 0 &&
        ownerPlayer != defenderPlayer &&
        ReadEntityContactState(owner, entitySlot, beforeEntity) &&
        ReadContactStateChecked(owner, beforeOwner) &&
        ReadContactStateChecked(defender, beforeDefender);

    oHandleEntityToPlayerCollision(
        gameSystem, ownerPtr, defenderPtr, entitySlot, defenderFrameData);

    if (!beforeValid) return;

    EntityContactState afterEntity;
    ContactState afterOwner;
    ContactState afterDefender;
    if (!ReadEntityContactState(owner, entitySlot, afterEntity) ||
        !ReadContactStateChecked(owner, afterOwner) ||
        !ReadContactStateChecked(defender, afterDefender)) {
        return;
    }

    // Entry +0x80 must be clear for 0x7697D0 to test collision; these mutations prove a contact branch committed but not which one (Guard Point/armor/guard-break overlap).
    const bool entityStateChanged =
        afterEntity.rawState != beforeEntity.rawState;
    const bool lifeConsumed =
        afterEntity.collisionLife < beforeEntity.collisionLife;
    const bool comboChanged = afterOwner.combo != beforeOwner.combo;
    const bool hpChanged = afterDefender.hp != beforeDefender.hp;
    const bool defenderReactionChanged =
        afterDefender.move != beforeDefender.move;
    const bool playerLatchChanged =
        afterOwner.hitState != beforeOwner.hitState ||
        afterDefender.hitState != beforeDefender.hitState;

    Mission::Contact::EntityEvidence evidence;
    evidence.resolved = beforeEntity.rawState == 0 &&
        (entityStateChanged || lifeConsumed || comboChanged || hpChanged ||
         defenderReactionChanged || playerLatchChanged);
    evidence.comboIncreased = afterOwner.combo > beforeOwner.combo;
    evidence.comboRestarted = Mission::Contact::CorroboratesComboRestart(
        beforeOwner.combo, afterOwner.combo,
        beforeDefender.hp, afterDefender.hp,
        IsHitstun(afterDefender.move) || IsLaunched(afterDefender.move),
        afterEntity.rawState);
    evidence.defenderRecoilGuard = IsRecoilGuard(afterDefender.move);
    evidence.defenderBlocked = IsBlockstunState(afterDefender.move) &&
                               !evidence.defenderRecoilGuard;
    evidence.rawEntityState = afterEntity.rawState;
    const Mission::Contact::Result result =
        Mission::Contact::ClassifyEntity(evidence);
    if (result == Mission::Contact::Result::None) return;

    Mission::Contact::Event event;
    event.batchId = GetCurrentBattleUpdateBatch();
    event.attacker = static_cast<uint8_t>(ownerPlayer);
    event.defender = static_cast<uint8_t>(defenderPlayer);
    event.source = Mission::Contact::Source::Entity;
    event.result = result;
    event.attackerMove = static_cast<short>(beforeEntity.pattern);
    event.attackerFrame = static_cast<short>(beforeEntity.frame);
    event.defenderMoveBefore = beforeDefender.move;
    event.defenderMove = afterDefender.move;
    event.timerBefore = beforeEntity.collisionLife;
    event.timerAfter = afterEntity.collisionLife;
    event.rawStateBefore = beforeEntity.rawState;
    event.rawStateAfter = afterEntity.rawState;
    event.comboBefore = beforeOwner.combo;
    event.comboAfter = afterOwner.combo;
    event.defenderHpBefore = beforeDefender.hp;
    event.defenderHpAfter = afterDefender.hp;
    event.entitySlot = entitySlot;
    event.entityPattern = beforeEntity.pattern;
    s_contactJournal.Publish(event);
}

void InstallCollisionHook() {
    Practice::PatchModule layoutModule{};
    const bool layoutSupported=Practice::GetQualifiedMemorialImage(layoutModule);
    collisionLayoutSupported.store(layoutSupported,std::memory_order_release);
    if(!layoutSupported)for(auto& status:collisionLayoutStatus)status.store(CollisionLayoutStatus::UnsupportedLayout,std::memory_order_release);
    uintptr_t targetAddr = 0;
    if (!ResolveCollisionHookTarget(targetAddr)) {
        return;
    }
    if (s_collisionHookTargetAddr != targetAddr && MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_collisionHookTargetAddr))) return;
    s_collisionHookTargetAddr = targetAddr;

    if (!s_collisionHookCreated.load(std::memory_order_acquire)) {
        // Existing ownership retains its validated preimage; validate only new acquisitions.
        if (!MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(targetAddr)) && !ValidateCollisionHookTarget(targetAddr)) {
            LogOut("[COLLISION_HOOK] Refusing strict contact hook: 0x767F60 prologue mismatch", true);
            return;
        }
        if (!MinHookUtils::CreateAndEnableHook(reinterpret_cast<LPVOID>(targetAddr),
                                               reinterpret_cast<void*>(&HookedHandleP2PCollision),
                                               reinterpret_cast<void**>(&oHandleP2PCollision),
                                               "[COLLISION_HOOK]",
                                               "handleP2PCollision", nullptr, nullptr, &MinHookUtils::TicketFor<&HookedHandleP2PCollision>())) {
            return;
        }
        s_collisionHookCreated.store(true, std::memory_order_release);
        LogOut("[COLLISION_HOOK] Installed at " + FormatHexAddress(targetAddr), true);
    }

    uintptr_t entityTargetAddr = 0;
    if (ResolveEntityCollisionHookTarget(entityTargetAddr)) {
        if (s_entityCollisionHookTargetAddr != entityTargetAddr && MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(s_entityCollisionHookTargetAddr))) return;
        s_entityCollisionHookTargetAddr = entityTargetAddr;
        // Validate only before creating: after hooking, the prologue is our trampoline.
        if (!s_entityCollisionHookCreated.load(std::memory_order_acquire)) {
            if (!MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(entityTargetAddr)) && !ValidateEntityCollisionHookTarget(entityTargetAddr)) {
                LogOut("[COLLISION_HOOK] Entity contact unavailable: 0x7697D0 prologue mismatch", true);
            } else if (MinHookUtils::CreateAndEnableHook(
                    reinterpret_cast<LPVOID>(entityTargetAddr),
                    reinterpret_cast<void*>(&HookedHandleEntityToPlayerCollision),
                    reinterpret_cast<void**>(&oHandleEntityToPlayerCollision),
                    "[COLLISION_HOOK]", "handleEntityToPlayerCollision", nullptr, nullptr, &MinHookUtils::TicketFor<&HookedHandleEntityToPlayerCollision>())) {
                s_entityCollisionHookCreated.store(true, std::memory_order_release);
                LogOut("[COLLISION_HOOK] Entity contact installed at " +
                       FormatHexAddress(entityTargetAddr), true);
            }
        }
    }

    if (g_onlineModeActive.load(std::memory_order_relaxed)) {
        SetCollisionHookActive(false);
        return;
    }

    SetCollisionHookActive(true);
}

void SetCollisionHookActive(bool active) {
    if (!active) {
        MinHookUtils::CloseAdmission("[COLLISION_HOOK]");
        (void)MinHookUtils::DisableOwnedTargets("[COLLISION_HOOK]", {});
        (void)MinHookUtils::ReclaimDrainedTargets("[COLLISION_HOOK]", {});
    }
    if (!s_collisionHookCreated.load(std::memory_order_acquire)) {
        if (active) {
            InstallCollisionHook();
        }
        return;
    }

    if (active) {
        (void)PracticeHooks::PublishTargetActivation(MinHookUtils::OwnedHooks(),
            s_collisionHookTargetAddr, s_collisionHookEnabled);
        (void)PracticeHooks::PublishTargetActivation(MinHookUtils::OwnedHooks(),
            s_entityCollisionHookTargetAddr, s_entityCollisionHookEnabled);
    } else {
        s_collisionHookEnabled.store(false,std::memory_order_release);
        s_entityCollisionHookEnabled.store(false,std::memory_order_release);
    }

    LogOut(std::string("[COLLISION_HOOK] Collision hooks ") +
           (active ? "admission enabled" : "admission closed; physical retirement pending"), true);
}

void RemoveCollisionHook() {
    SetCollisionHookActive(false);
    (void)PracticeHooks::ReleaseRetiredSlot(MinHookUtils::OwnedHooks(),
        s_entityCollisionHookTargetAddr, oHandleEntityToPlayerCollision,
        s_entityCollisionHookCreated);
    (void)PracticeHooks::ReleaseRetiredSlot(MinHookUtils::OwnedHooks(),
        s_collisionHookTargetAddr, oHandleP2PCollision, s_collisionHookCreated);
}

uintptr_t GetCachedAttackDataForPlayer(int playerNum) {
    if(playerNum<1 || playerNum>2)return 0;
    ResolveCapturedCollisionLayout(playerNum);
    return (playerNum == 1) ? g_lastAttackDataP1.load() : g_lastAttackDataP2.load();
}

int GetAttackDataOffsetForPlayer(int playerNum) {
    if(playerNum<1 || playerNum>2)return -1;
    ResolveCapturedCollisionLayout(playerNum);
    return (playerNum == 1) ? g_attackDataOffsetP1.load() : g_attackDataOffsetP2.load();
}

CollisionLayoutStatus GetCollisionLayoutStatusForPlayer(int playerNum) {
    if(playerNum<1 || playerNum>2)return CollisionLayoutStatus::UnsupportedLayout;
    ResolveCapturedCollisionLayout(playerNum);
    return collisionLayoutStatus[playerNum-1].load(std::memory_order_acquire);
}

void ResetCollisionHookSessionCaches(const char* reason) {
    {
        std::lock_guard<std::mutex> lock(collisionLayoutMutex);
        collisionLayouts[0]={};collisionLayouts[1]={};
        g_attackDataOffsetP1.store(-1);g_attackDataOffsetP2.store(-1);
        for(auto& status:collisionLayoutStatus)status.store(collisionLayoutSupported.load()?CollisionLayoutStatus::NotAvailableYet:CollisionLayoutStatus::UnsupportedLayout);
    }
    const uintptr_t lastP1 = g_lastAttackDataP1.exchange(0);
    const uintptr_t lastP2 = g_lastAttackDataP2.exchange(0);

    if (lastP1 || lastP2 || detailedLogging.load()) {
        std::ostringstream oss;
        oss << "[COLLISION_HOOK] Reset session caches"
            << " reason=" << (reason ? reason : "unspecified")
            << " P1=" << FormatHexAddress(lastP1)
            << " P2=" << FormatHexAddress(lastP2)
            << " offsets=" << g_attackDataOffsetP1.load() << "/" << g_attackDataOffsetP2.load();
        LogOut(oss.str(), true);
    }
    ResetContactEventJournal(reason);
}

bool IsDirectContactHookReady() {
    return s_collisionHookCreated.load(std::memory_order_acquire) &&
           s_collisionHookEnabled.load(std::memory_order_acquire) &&
           !g_onlineModeActive.load(std::memory_order_relaxed);
}

bool IsEntityContactHookReady() {
    return s_entityCollisionHookCreated.load(std::memory_order_acquire) &&
           s_entityCollisionHookEnabled.load(std::memory_order_acquire) &&
           !g_onlineModeActive.load(std::memory_order_relaxed);
}

uint32_t GetContactEventEpoch() { return s_contactJournal.Epoch(); }
uint32_t GetContactEventWatermark() { return s_contactJournal.Watermark(); }

void ResetContactEventJournal(const char* reason) {
    const uint32_t epoch = s_contactJournal.ResetEpoch();
    if (detailedLogging.load()) {
        LogOut("[CONTACT] journal epoch=" + std::to_string(epoch) +
               " reason=" + (reason ? reason : "unspecified"), true);
    }
}

Mission::Contact::ReadResult ReadCommittedContactEvents(
    uint32_t afterSequence, uint32_t expectedEpoch, uint32_t closedBatch,
    Mission::Contact::Event* out, size_t outCapacity) {
    return s_contactJournal.ReadAfter(afterSequence, expectedEpoch, closedBatch,
                                      out, outCapacity);
}
