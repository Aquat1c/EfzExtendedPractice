#pragma once
#include "runtime/hook_registry.h"

#include "../../3rdparty/minhook/include/MinHook.h"

namespace MinHookUtils {

bool CreateHook(void* target,
                void* detour,
                void** original,
                const char* category,
                const char* label,
                bool* alreadyCreated = nullptr,
                PracticeHooks::CallbackTicket* ticket = nullptr);

bool EnableHook(void* target,
                const char* category,
                const char* label,
                bool* alreadyEnabled = nullptr);

bool DisableHook(void* target,
                 const char* category,
                 const char* label,
                 bool* alreadyDisabled = nullptr);

bool RemoveHook(void* target,
                const char* category,
                const char* label,
                bool* wasMissing = nullptr);

bool CreateAndEnableHook(void* target,
                         void* detour,
                         void** original,
                         const char* category,
                         const char* label,
                         bool* alreadyCreated = nullptr,
                         bool* alreadyEnabled = nullptr,
                         PracticeHooks::CallbackTicket* ticket = nullptr);

}
// The lifecycle continuation owns physical retirement.
// The bool wrappers never produce safe-continuation or chain-drain receipts.
namespace MinHookUtils {
PracticeHooks::Registry& OwnedHooks();
PracticeHooks::Outcome CloseAdmission(const char* owner);
PracticeHooks::Outcome DisableOwnedTargets(const char* owner,const PracticeHooks::RetirementProof& proof);
PracticeHooks::Outcome ReclaimDrainedTargets(const char* owner,const PracticeHooks::RetirementProof& proof);
bool HasOwnedTarget(void* target);
bool EnableOwnedTargets(const char* owner);
inline PracticeHooks::ExecutionGuard EnterExecution(const PracticeHooks::CallbackTicket& ticket) noexcept {
    return PracticeHooks::ExecutionGuard::Enter(ticket);
}
template<auto Detour>
PracticeHooks::CallbackTicket& TicketFor() {
    static PracticeHooks::CallbackTicket ticket;
    return ticket;
}
}
