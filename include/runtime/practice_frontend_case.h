#pragma once
#include "hook_registry.h"

namespace Practice {
// Inline site, not a function hook. Published before the JMP is written and retained until
// native entry is unreachable and callbacks have drained.
struct FrontendCaseBinding {
    PracticeHooks::CallbackTicket ticket;
    void (__cdecl* enter)(uint32_t)=nullptr;
    uintptr_t continuation=0;
    uintptr_t epilogue=0;
};
FrontendCaseBinding& TitleCaseBinding();
bool BindTitleCaseAdapter(PracticeHooks::Registry&,const PracticeHooks::TargetSpec&,
                          const PatchDescriptor&,PatchLedger&,
                          std::shared_ptr<void> continuationPin,
                          void (__cdecl* enter)(uint32_t));
bool InstallTitleCaseAdapter(void (__cdecl* enter)(uint32_t));
void CloseTitleCaseAdmission();
bool TitleCaseRetained();
}
extern "C" void PracticeTitleCaseAdapter();

namespace Practice {
struct FrontendBoundaryBinding {
    PracticeHooks::CallbackTicket ticket;
    void (__cdecl* enter)(uint32_t)=nullptr;
    uintptr_t continuation=0;
};
FrontendBoundaryBinding& SelectorBoundary();
FrontendBoundaryBinding& BattlePrepareBoundary();
FrontendBoundaryBinding& BattleInitializedBoundary();
// Allocates/binds only; no physical mutation without the provider exclusion.
bool PrepareFrontendBoundaries(void (__cdecl* selector)(uint32_t),void (__cdecl* prepare)(uint32_t),void (__cdecl* initialized)(uint32_t));
}
extern "C" void PracticeSelectorReadyAdapter();
extern "C" void PracticeBattlePrepareAdapter();
extern "C" void PracticeBattleInitializedAdapter();
