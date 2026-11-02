#include "runtime/practice_frontend_case.h"

extern "C" {
Practice::FrontendCaseBinding g_PracticeTitleCaseBinding;
}
namespace Practice {
FrontendCaseBinding& TitleCaseBinding(){return g_PracticeTitleCaseBinding;}
bool BindTitleCaseAdapter(PracticeHooks::Registry& registry,const PracticeHooks::TargetSpec& spec,
                         const PatchDescriptor& patch,PatchLedger& ledger,
                         std::shared_ptr<void> continuationPin,
                         void (__cdecl* enter)(uint32_t)) {
    auto& binding=TitleCaseBinding();
    if(binding.ticket.state.load(std::memory_order_acquire)) {
        if(binding.enter!=enter || binding.continuation!=patch.address+7 ||
           binding.epilogue!=0x776483)return false;
    } else {
        binding.enter=enter;binding.continuation=patch.address+7;binding.epilogue=0x776483;
    }
    return registry.PrepareExternal(spec,patch,ledger,binding.ticket,std::move(continuationPin)).Complete() &&
        registry.Enable(spec.target).Complete();
}
}
extern "C" unsigned char __cdecl DispatchPracticeTitleCase(uint32_t context) {
    auto& binding=Practice::TitleCaseBinding();
    auto guard=PracticeHooks::ExecutionGuard::Enter(binding.ticket);
    if(!guard.Admitted() || !binding.enter)return 0;
    binding.enter(context);
    return 1;
}
extern "C" void __declspec(naked) PracticeTitleCaseAdapter() {
    __asm {
        pushfd
        pushad
        mov eax,[ebp-8]
        push eax
        call DispatchPracticeTitleCase
        add esp,4
        test al,al
        jz originalCase
        popad
        popfd
        mov al,0
        jmp dword ptr [g_PracticeTitleCaseBinding.epilogue]
    originalCase:
        popad
        popfd
        // Exact seven bytes at Memorial 776352; replay the complete overwritten instructions.
        cmp dword ptr [ebp-4],1
        sete al
        jmp dword ptr [g_PracticeTitleCaseBinding.continuation]
    }
}

extern "C" {
Practice::FrontendBoundaryBinding g_PracticeSelectorBoundary;
Practice::FrontendBoundaryBinding g_PracticeBattlePrepareBoundary;
Practice::FrontendBoundaryBinding g_PracticeBattleInitializedBoundary;
}
namespace Practice {
FrontendBoundaryBinding& SelectorBoundary(){return g_PracticeSelectorBoundary;}
FrontendBoundaryBinding& BattlePrepareBoundary(){return g_PracticeBattlePrepareBoundary;}
FrontendBoundaryBinding& BattleInitializedBoundary(){return g_PracticeBattleInitializedBoundary;}
}
extern "C" void __cdecl DispatchPracticeFrontendBoundary(Practice::FrontendBoundaryBinding* binding,uint32_t context) {
    auto guard=PracticeHooks::ExecutionGuard::Enter(binding->ticket);
    if(guard.Admitted() && binding->enter)binding->enter(context);
}
extern "C" __declspec(naked) void PracticeSelectorReadyAdapter(){__asm {
    pushfd
    pushad
    push [ebp-4ch]
    push offset g_PracticeSelectorBoundary
    call DispatchPracticeFrontendBoundary
    add esp,8
    popad
    popfd
    mov edx,[ebp-4ch]
    movsx eax,byte ptr [edx+2dh]
    jmp dword ptr [g_PracticeSelectorBoundary.continuation]
}}

extern "C" __declspec(naked) void PracticeBattlePrepareAdapter(){__asm {
    pushfd
    pushad
    push [ebp-30h]
    push offset g_PracticeBattlePrepareBoundary
    call DispatchPracticeFrontendBoundary
    add esp,8
    popad
    popfd
    mov eax,[ebp-30h]
    mov edx,[eax]
    jmp dword ptr [g_PracticeBattlePrepareBoundary.continuation]
}}
extern "C" __declspec(naked) void PracticeBattleInitializedAdapter(){__asm {
    pushfd
    pushad
    push [ebp-30h]
    push offset g_PracticeBattleInitializedBoundary
    call DispatchPracticeFrontendBoundary
    add esp,8
    popad
    popfd
    jmp dword ptr [g_PracticeBattleInitializedBoundary.continuation]
}}
