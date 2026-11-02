#include "runtime/practice_battle_gates.h"
#include <cstdio>
#include <cstring>
namespace {
int failures=0,prefixCalls=0,cleanupCalls=0,notifications=0;
uint32_t decision=EFZ_TM_PENDING,observedBranch=99,observedSlot=0;
void* observedBattle=nullptr;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(false)
uint32_t __cdecl Before(void* battle,Practice::BattleCleanupBranch branch){
    ++notifications;observedBattle=battle;observedBranch=static_cast<uint32_t>(branch);return decision;
}
__declspec(naked) void Epilogue(){__asm {mov esp,ebp
    pop ebp
    ret}}
__declspec(naked) void TailA(){__asm {inc cleanupCalls
    mov observedSlot,edx
    mov al,1
    jmp Epilogue}}
__declspec(naked) void TailB(){__asm {inc cleanupCalls
    mov al,5
    jmp Epilogue}}
__declspec(naked) unsigned char __fastcall NativeA(void*,void*){__asm {
    push ebp
    mov ebp,esp
    sub esp,40h
    mov [ebp-30h],ecx
    inc prefixCalls
    jmp PracticeBattleCleanupGateA}}
__declspec(naked) unsigned char __fastcall NativeB(void*,void*){__asm {
    push ebp
    mov ebp,esp
    sub esp,40h
    mov [ebp-30h],ecx
    inc prefixCalls
    jmp PracticeBattleCleanupGateB}}
void ExactNativeRetirementResultsAndResume(){
    PracticeHooks::Registry registry(PracticeHooks::Backend{});
    PracticeHooks::CallbackState first,second;
    auto& binding=Practice::BattleGates();binding.firstTicket.state.store(&first);
    binding.secondTicket.state.store(&second);first.admitted.store(true);second.admitted.store(true);
    binding.beforeCleanup=Before;binding.firstContinuation=reinterpret_cast<uintptr_t>(&TailA);
    binding.secondContinuation=reinterpret_cast<uintptr_t>(&TailB);binding.epilogue=reinterpret_cast<uintptr_t>(&Epilogue);
    unsigned char battle[0x40]{};uint32_t slot=0x12345678;std::memcpy(battle+0x14,&slot,4);battle[0x2d]=2;
    decision=EFZ_TM_PENDING;
    CHECK(NativeA(battle,nullptr)==3);CHECK(prefixCalls==1&&cleanupCalls==0&&notifications==1);
    CHECK(Practice::BattleCleanupHeld(battle));
    CHECK(observedBattle==battle&&observedBranch==0);CHECK(first.outstanding.load()==0);
    CHECK(PracticeResumeBattleCleanupA(battle,nullptr)==1);
    Practice::BattleCleanupReturned(battle);CHECK(!Practice::BattleCleanupHeld(battle));
    CHECK(prefixCalls==1&&cleanupCalls==1&&notifications==1&&observedSlot==0x12345678);
    CHECK(NativeB(battle,nullptr)==3);CHECK(battle[0x2d]==2);CHECK(observedBranch==1);
    CHECK(PracticeResumeBattleCleanupB(battle,nullptr)==5);CHECK(battle[0x2d]==0);
    CHECK(prefixCalls==2&&cleanupCalls==2&&notifications==2);
    decision=EFZ_TM_READY;
    CHECK(NativeA(battle,nullptr)==1);CHECK(cleanupCalls==3);
    for(uint32_t denied: {EFZ_TM_UNSUPPORTED,EFZ_TM_STALE,EFZ_TM_FAULTED}) {
        decision=denied;CHECK(NativeA(battle,nullptr)==3);CHECK(cleanupCalls==3);
    }
    first.admitted.store(false);int previous=notifications;
    decision=EFZ_TM_PENDING;
    CHECK(NativeA(battle,nullptr)==3);CHECK(cleanupCalls==3&&notifications==previous+1);
    decision=EFZ_TM_READY;
    CHECK(NativeA(battle,nullptr)==1);CHECK(cleanupCalls==4&&notifications==previous+2);
    binding.firstTicket.state.store(nullptr);binding.secondTicket.state.store(nullptr);
    CHECK(NativeA(battle,nullptr)==3);CHECK(cleanupCalls==4);
}
}
int main(){ExactNativeRetirementResultsAndResume();return failures?1:0;}
