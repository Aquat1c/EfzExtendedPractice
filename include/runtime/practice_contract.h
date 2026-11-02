#pragma once

#include <stdint.h>
#include <stddef.h>

#define EFZ_TM_LIFECYCLE_ABI 1u
#define EFZ_TM_CAP_OLD_WORLD_HOLD 0x01u
#define EFZ_TM_CAP_PRE_FOREIGN_HANDOFF 0x02u
#define EFZ_TM_CAP_GRAPHICS_OWNERSHIP 0x04u

#pragma pack(push, 4)
struct EfzTmIdentityV1 {
    uint64_t providerIncarnation;
    uint64_t practiceSession;
    uint64_t battleWorld;
    uint64_t timeline;
};
struct EfzTmEntryV1 {
    uint32_t size;
    uint32_t abiVersion;
    uint32_t capabilities;
    uint32_t nativeThreadId;
    EfzTmIdentityV1 id;
    uint32_t efzBase;
    uint32_t revivalBase;
    uint32_t profileId;
    uint32_t gameSystem;
    uint32_t practiceController;
    uint32_t battleContext;
    uint32_t player1;
    uint32_t player2;
    uint32_t gameMode;
    uint32_t screen;
};
struct EfzTmRetireV1 {
    uint32_t size;
    uint32_t abiVersion;
    EfzTmIdentityV1 id;
    uint32_t reason;
    uint32_t oldWorldHeld;
};
struct EfzTmStatusV1 {
    uint32_t size;
    uint32_t abiVersion;
    EfzTmIdentityV1 id;
    uint32_t state;
    uint32_t attachedTrainingTargets;
    uint32_t outstandingCallbacks;
    uint32_t restorationObligations;
    uint32_t trainingWorkersActive;
    uint32_t timerLeaseHeld;
    uint32_t faultCode;
};
#pragma pack(pop)

enum EfzTmResultV1 : uint32_t {
    EFZ_TM_READY = 0,
    EFZ_TM_PENDING = 1,
    EFZ_TM_UNSUPPORTED = 2,
    EFZ_TM_STALE = 3,
    EFZ_TM_FAULTED = 4
};
enum EfzTmRetireReasonV1 : uint32_t {
    EFZ_TM_RELOAD_WORLD = 1,
    EFZ_TM_LEAVE_PRACTICE = 2,
    EFZ_TM_FOREIGN_HANDOFF = 3,
    EFZ_TM_PROVIDER_REPLACED = 4,
    EFZ_TM_EXPLICIT_UNLOAD = 5
};
enum EfzTmRuntimeStateV1 : uint32_t {
    EFZ_TM_DORMANT = 0,
    EFZ_TM_ARMING = 1,
    EFZ_TM_FRONTEND = 2,
    EFZ_TM_BATTLE = 3,
    EFZ_TM_REVOKING = 4,
    EFZ_TM_QUIESCENT = 5,
    EFZ_TM_RUNTIME_FAULTED = 6
};

static_assert(sizeof(EfzTmIdentityV1) == 32, "identity ABI changed");
static_assert(sizeof(EfzTmEntryV1) == 88, "entry ABI changed");
static_assert(sizeof(EfzTmRetireV1) == 48, "retire ABI changed");
static_assert(sizeof(EfzTmStatusV1) == 68, "status ABI changed");
static_assert(alignof(EfzTmIdentityV1) == 4, "identity ABI alignment changed");
static_assert(offsetof(EfzTmEntryV1, id) == 16, "entry identity offset changed");
static_assert(offsetof(EfzTmEntryV1, efzBase) == 48, "entry pointer offset changed");
static_assert(offsetof(EfzTmRetireV1, oldWorldHeld) == 44, "hold offset changed");
static_assert(offsetof(EfzTmStatusV1, state) == 40, "status offset changed");

#if defined(EFZ_TM_RUNTIME_EXPORTS)
#define EFZ_TM_RUNTIME_API __declspec(dllexport)
#else
#define EFZ_TM_RUNTIME_API
#endif

extern "C" {
EFZ_TM_RUNTIME_API uint32_t __cdecl EFZ_TM_BeginPracticeV1(const EfzTmEntryV1* entry);
EFZ_TM_RUNTIME_API uint32_t __cdecl EFZ_TM_AttachBattleV1(const EfzTmEntryV1* entry);
EFZ_TM_RUNTIME_API uint32_t __cdecl EFZ_TM_BeginRetireV1(const EfzTmRetireV1* request);
EFZ_TM_RUNTIME_API uint32_t __cdecl EFZ_TM_AdvanceRetireV1(const EfzTmRetireV1* request);
EFZ_TM_RUNTIME_API uint32_t __cdecl EFZ_TM_NotifyNativeRestoreV1(
    const EfzTmIdentityV1* before, uint32_t succeeded);
EFZ_TM_RUNTIME_API uint32_t __cdecl EFZ_TM_GetStatusV1(EfzTmStatusV1* status);
}
