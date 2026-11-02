#include "runtime/practice_battle_gates.h"
#include "runtime/practice_runtime.h"
#include "runtime/native_game_profile.h"
#include "game/character_hotswap.h"
#include "game/game_state.h"
#include "utils/minhook_utils.h"
#include <windows.h>
#include <cstring>

namespace Practice {
namespace {
using BeforeCleanupFn=uint32_t (__cdecl*)(uint32_t,uint32_t,uint32_t);
BeforeCleanupFn providerBeforeCleanup=nullptr;
std::shared_ptr<void> PinCleanupCode(uintptr_t address) {
    HMODULE module=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCSTR>(address),&module))return {};
    return std::shared_ptr<void>(module,[](void* value){FreeLibrary(static_cast<HMODULE>(value));});
}
uint32_t __cdecl BeforeCleanup(void* battle,BattleCleanupBranch branch) {
    if(!MonitorLifecycleOwned())return EFZ_TM_READY;
    if(!providerBeforeCleanup || !RecordBattleCleanupBranch(reinterpret_cast<uintptr_t>(battle),static_cast<uint32_t>(branch)))return EFZ_TM_FAULTED;
    const uint32_t reason=branch==BattleCleanupBranch::FadeToSelector && CharacterHotswap::IsBusy()?EFZ_TM_RELOAD_WORLD:EFZ_TM_LEAVE_PRACTICE;
    return providerBeforeCleanup(reinterpret_cast<uintptr_t>(battle),static_cast<uint32_t>(branch),reason);
}
bool PrepareGate(uintptr_t site,void* adapter,PracticeHooks::CallbackTicket& ticket,
                 std::vector<unsigned char> preimage,const PatchModule& module) {
    PracticeHooks::TargetSpec spec;
    spec.target=reinterpret_cast<void*>(site);spec.detour=adapter;
    spec.owner="PracticeFrontend";spec.abi="Memorial BattleLogic cleanup EBP join; retained AL continuation";
    spec.moduleIncarnation=module.incarnation;spec.rangeStart=site;spec.preimage=std::move(preimage);
    spec.modulePin=PinCleanupCode(site);auto codePin=PinCleanupCode(reinterpret_cast<uintptr_t>(adapter));
    if(!spec.modulePin || !codePin)return false;
    PatchDescriptor patch{module,site,spec.preimage,std::vector<unsigned char>(spec.preimage.size(),0x90),0x50524600000000ull+site};
    patch.replacement[0]=0xe9;
    const uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(adapter)-(site+5));
    std::memcpy(patch.replacement.data()+1,&relative,sizeof(relative));
    return MinHookUtils::OwnedHooks().PrepareExternal(spec,patch,NativePatchLedger(),ticket,std::move(codePin)).Complete();
}
}
bool PrepareBattleCleanupSites() {
    PatchModule module{};if(!GetQualifiedMemorialImage(module))return false;
    if(!providerBeforeCleanup) {
        HMODULE provider=GetModuleHandleA("efz_netplay_mod.dll");
        if(!provider)return false;
        providerBeforeCleanup=reinterpret_cast<BeforeCleanupFn>(GetProcAddress(provider,"EFZ_Netplay_PracticeBeforeCleanupV1"));
        if(!providerBeforeCleanup)providerBeforeCleanup=reinterpret_cast<BeforeCleanupFn>(GetProcAddress(provider,"_EFZ_Netplay_PracticeBeforeCleanupV1"));
    }
    if(!providerBeforeCleanup)return false;
    auto& binding=BattleGates();
    if(binding.beforeCleanup && binding.beforeCleanup!=BeforeCleanup)return false;
    binding.beforeCleanup=BeforeCleanup;binding.firstContinuation=0x763da7;binding.secondContinuation=0x763f0b;binding.epilogue=0x764291;
    return PrepareGate(0x763da1,reinterpret_cast<void*>(&PracticeBattleCleanupGateA),binding.firstTicket,{0x8b,0x4d,0xd0,0x8b,0x51,0x14},module) &&
        PrepareGate(0x763f04,reinterpret_cast<void*>(&PracticeBattleCleanupGateB),binding.secondTicket,{0x8b,0x4d,0xd0,0xc6,0x41,0x2d,0x00},module);
}
}

extern "C" __declspec(dllexport) uint32_t __cdecl EFZ_TM_ResumePracticeCleanupAV1(uint32_t battle,uint32_t gameSystem) try {
    if(!Practice::TakeBattleCleanupResume(battle,gameSystem,0))return UINT32_MAX;
    const auto nativeResult=PracticeResumeBattleCleanupA(reinterpret_cast<void*>(battle),nullptr);
    const auto accepted=ConsumeBattleFrontendResult(nativeResult,false,gameSystem);
    Practice::BattleCleanupReturned(reinterpret_cast<void*>(battle));
    return Practice::RecordBattleCleanupReturned(battle,gameSystem,accepted)?accepted:UINT32_MAX;
} catch(...) {return UINT32_MAX;}
extern "C" __declspec(dllexport) uint32_t __cdecl EFZ_TM_ResumePracticeCleanupBV1(uint32_t battle,uint32_t gameSystem) try {
    if(!Practice::TakeBattleCleanupResume(battle,gameSystem,1))return UINT32_MAX;
    const auto nativeResult=PracticeResumeBattleCleanupB(reinterpret_cast<void*>(battle),nullptr);
    const auto accepted=ConsumeBattleFrontendResult(nativeResult,false,gameSystem);
    Practice::BattleCleanupReturned(reinterpret_cast<void*>(battle));
    return Practice::RecordBattleCleanupReturned(battle,gameSystem,accepted)?accepted:UINT32_MAX;
} catch(...) {return UINT32_MAX;}
