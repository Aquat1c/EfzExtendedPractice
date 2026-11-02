#include "runtime/practice_battle_gates.h"
extern "C" { Practice::BattleGateBinding g_PracticeBattleGateBinding; }
namespace Practice { BattleGateBinding& BattleGates(){return g_PracticeBattleGateBinding;} }
namespace Practice {
namespace { std::atomic<void*> heldBattle{nullptr}; }
bool BattleCleanupHeld(void* battle) noexcept{return heldBattle.load(std::memory_order_acquire)==battle;}
void BattleCleanupReturned(void* battle) noexcept{
    heldBattle.compare_exchange_strong(battle,nullptr,std::memory_order_acq_rel);
}
}
extern "C" uint32_t __cdecl DispatchPracticeBattleGate(void* battle,uint32_t branch) {
    auto& binding=Practice::BattleGates();
    const auto& ticket=branch==0?binding.firstTicket:binding.secondTicket;
    auto guard=PracticeHooks::ExecutionGuard::Enter(ticket);
    const auto result=guard && binding.beforeCleanup?
        binding.beforeCleanup(battle,static_cast<Practice::BattleCleanupBranch>(branch)):EFZ_TM_FAULTED;
    if(result!=EFZ_TM_READY)Practice::heldBattle.store(battle,std::memory_order_release);
    return result;
}
extern "C" __declspec(naked) void PracticeBattleCleanupGateA(){
    __asm {
        pushfd
        pushad
        push 0
        push [ebp-30h]
        call DispatchPracticeBattleGate
        add esp,8
        cmp eax,0 // EFZ_TM_READY; never treat incidental AL as Boolean
        jne pending
        popad
        popfd
        mov ecx,[ebp-30h]
        mov edx,[ecx+14h]
        jmp dword ptr [g_PracticeBattleGateBinding.firstContinuation]
    pending:
        popad
        popfd
        mov al,3
        jmp dword ptr [g_PracticeBattleGateBinding.epilogue]
    }
}
extern "C" __declspec(naked) void PracticeBattleCleanupGateB(){
    __asm {
        pushfd
        pushad
        push 1
        push [ebp-30h]
        call DispatchPracticeBattleGate
        add esp,8
        cmp eax,0
        jne pending
        popad
        popfd
        mov ecx,[ebp-30h]
        mov byte ptr [ecx+2dh],0
        jmp dword ptr [g_PracticeBattleGateBinding.secondContinuation]
    pending:
        popad
        popfd
        mov al,3
        jmp dword ptr [g_PracticeBattleGateBinding.epilogue]
    }
}
// Rebuilds a frame for the native tail, which uses only the battle pointer, not the old EBP/ESP.
extern "C" __declspec(naked) unsigned char __fastcall PracticeResumeBattleCleanupA(void*,void*){
    __asm {
        push ebp
        mov ebp,esp
        sub esp,40h
        mov [ebp-30h],ecx
        mov ecx,[ebp-30h]
        mov edx,[ecx+14h]
        jmp dword ptr [g_PracticeBattleGateBinding.firstContinuation]
    }
}
extern "C" __declspec(naked) unsigned char __fastcall PracticeResumeBattleCleanupB(void*,void*){
    __asm {
        push ebp
        mov ebp,esp
        sub esp,40h
        mov [ebp-30h],ecx
        mov ecx,[ebp-30h]
        mov byte ptr [ecx+2dh],0
        jmp dword ptr [g_PracticeBattleGateBinding.secondContinuation]
    }
}
