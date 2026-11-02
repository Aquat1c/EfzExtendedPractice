#include "runtime/practice_frontend_case.h"
#include "runtime/practice_battle_gates.h"
#include "runtime/native_game_profile.h"
#include "runtime/practice_runtime.h"
#include "game/character_hotswap.h"
#include "utils/minhook_utils.h"
#include <windows.h>
#include <cstring>

namespace Practice {
namespace {
constexpr uintptr_t kCase=0x776352;
constexpr uint64_t kLedgerOwner=0x505243415345ull;
std::shared_ptr<void> PinCode(uintptr_t address) {
    HMODULE module=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCSTR>(address),&module))return {};
    return std::shared_ptr<void>(module,[](void* p){FreeLibrary(static_cast<HMODULE>(p));});
}
}
bool InstallTitleCaseAdapter(void (__cdecl* enter)(uint32_t)) {
    auto& registry=MinHookUtils::OwnedHooks();
    PatchModule module{};
    if(!enter || !GetQualifiedMemorialImage(module))return false;
    PracticeHooks::TargetSpec spec;
    spec.target=reinterpret_cast<void*>(kCase);
    spec.detour=reinterpret_cast<void*>(&PracticeTitleCaseAdapter);
    spec.owner="PracticeMenu";spec.abi="Memorial title case JMP; native EBP frame; AL screen";
    spec.moduleIncarnation=module.incarnation;spec.rangeStart=kCase;
    spec.preimage={0x83,0x7d,0xfc,0x01,0x0f,0x94,0xc0};
    spec.modulePin=PinCode(kCase);
    auto codePin=PinCode(reinterpret_cast<uintptr_t>(&PracticeTitleCaseAdapter));
    if(!spec.modulePin || !codePin)return false;
    PatchDescriptor patch{module,kCase,spec.preimage,{0xe9,0,0,0,0,0x90,0x90},kLedgerOwner};
    const uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(spec.detour)-(kCase+5));
    std::memcpy(patch.replacement.data()+1,&relative,sizeof(relative));
    return BindTitleCaseAdapter(registry,spec,patch,NativePatchLedger(),std::move(codePin),enter);
}
void CloseTitleCaseAdmission(){MinHookUtils::OwnedHooks().CloseAdmission("PracticeMenu");}
bool TitleCaseRetained(){return MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(kCase));}
}

namespace Practice {
namespace {
bool PrepareBoundary(uintptr_t site,uintptr_t continuation,void* adapter,FrontendBoundaryBinding& binding,
                     std::vector<unsigned char> preimage,void (__cdecl* enter)(uint32_t),const PatchModule& module) {
    if(!enter)return false;
    if(binding.ticket.state.load(std::memory_order_acquire) &&
       (binding.enter!=enter || binding.continuation!=continuation))return false;
    PracticeHooks::TargetSpec spec;
    spec.target=reinterpret_cast<void*>(site);spec.detour=adapter;
    spec.owner="PracticeFrontend";spec.abi="Memorial native selector/init EBP join";
    spec.moduleIncarnation=module.incarnation;spec.rangeStart=site;spec.preimage=std::move(preimage);
    spec.modulePin=PinCode(site);auto codePin=PinCode(reinterpret_cast<uintptr_t>(adapter));
    if(!spec.modulePin || !codePin)return false;
    PatchDescriptor patch{module,site,spec.preimage,std::vector<unsigned char>(spec.preimage.size(),0x90),0x50524600000000ull+site};
    patch.replacement[0]=0xe9;
    const uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(adapter)-(site+5));
    std::memcpy(patch.replacement.data()+1,&relative,sizeof(relative));
    binding.enter=enter;binding.continuation=continuation;
    return MinHookUtils::OwnedHooks().PrepareExternal(spec,patch,NativePatchLedger(),binding.ticket,std::move(codePin)).Complete();
}
}
bool PrepareFrontendBoundaries(void (__cdecl* selector)(uint32_t),void (__cdecl* prepare)(uint32_t),void (__cdecl* initialized)(uint32_t)) {
    PatchModule module{};if(!GetQualifiedMemorialImage(module))return false;
    return PrepareBoundary(0x753a15,0x753a1c,reinterpret_cast<void*>(&PracticeSelectorReadyAdapter),SelectorBoundary(),
               {0x8b,0x55,0xb4,0x0f,0xbe,0x42,0x2d},selector,module) &&
        PrepareBoundary(0x763d4a,0x763d4f,reinterpret_cast<void*>(&PracticeBattlePrepareAdapter),BattlePrepareBoundary(),
               {0x8b,0x45,0xd0,0x8b,0x10},prepare,module) &&
        PrepareBoundary(0x763d72,0x763ebc,reinterpret_cast<void*>(&PracticeBattleInitializedAdapter),BattleInitializedBoundary(),
               {0xe9,0x45,0x01,0x00,0x00},initialized,module);
}
}

namespace {
using InitializedFn=uint32_t (__cdecl*)(uint32_t,uint32_t,uint32_t);
InitializedFn notifyInitialized=nullptr;
uintptr_t preparedBattle=0,preparedSystem=0;
void __cdecl PrepareBattle(uint32_t battle) {
    EfzTmIdentityV1 id{};
    if(!Practice::CaptureCurrentPracticeIdentity(id) || id.battleWorld)return;
    __try {
        if(*reinterpret_cast<const uint8_t*>(0x790148)!=3 || *reinterpret_cast<const uint32_t*>(0x79011c)!=battle)return;
        preparedSystem=*reinterpret_cast<const uint32_t*>(battle+0x1c);preparedBattle=battle;
    } __except(EXCEPTION_EXECUTE_HANDLER){preparedBattle=0;preparedSystem=0;}
    uint32_t player1=0,player2=0;
    __try {
        if(preparedBattle) {
            player1=*reinterpret_cast<const uint32_t*>(battle+0x0c);
            player2=*reinterpret_cast<const uint32_t*>(battle+0x10);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){return;}
    if(preparedBattle)(void)Practice::PrepareBattleInputs(battle,preparedSystem,player1,player2);
}
void __cdecl InitializedBattle(uint32_t battle) {
    if(!notifyInitialized || preparedBattle!=battle || !preparedSystem)return;
    uint32_t player1=0,player2=0;
    __try {
        if(*reinterpret_cast<const uint32_t*>(battle+0x1c)!=preparedSystem)return;
        player1=*reinterpret_cast<const uint32_t*>(battle+0x0c);
        player2=*reinterpret_cast<const uint32_t*>(battle+0x10);
    } __except(EXCEPTION_EXECUTE_HANDLER){return;}
    EfzTmIdentityV1 identity{};
    if(!Practice::CaptureCurrentPracticeIdentity(identity))return;
    const uint32_t ticket=CharacterHotswap::CaptureInitializedRequest(identity);
    const auto result=notifyInitialized(battle,player1,player2);
    if(result==EFZ_TM_READY || result==EFZ_TM_PENDING)
        CharacterHotswap::OnBattleInitialized(identity,ticket,battle,preparedSystem);
    preparedBattle=0;preparedSystem=0;
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl EFZ_TM_InstallFrontendV1(const EfzTmEntryV1* entry,uint64_t receipt) try {
    EfzTmIdentityV1 current{};
    if(!entry || !receipt || entry->size!=sizeof(*entry) || entry->abiVersion!=EFZ_TM_LIFECYCLE_ABI ||
       entry->nativeThreadId!=GetCurrentThreadId() || entry->gameMode!=1 || (entry->screen!=1&&entry->screen!=2) ||
       !Practice::CaptureCurrentPracticeIdentity(current) || current.providerIncarnation!=entry->id.providerIncarnation ||
       current.practiceSession!=entry->id.practiceSession || current.battleWorld || entry->id.battleWorld)return EFZ_TM_UNSUPPORTED;
    auto& registry=MinHookUtils::OwnedHooks();
    bool partial=false;
    for(const auto& record:registry.Snapshot())if(record.spec.owner=="PracticeFrontend" && record.state==PracticeHooks::State::ExternalInstalling)partial=true;
    if(partial) {
        registry.CloseAdmission("PracticeFrontend");
        PracticeHooks::RetirementProof proof{true,true,true,registry.Epoch(),receipt};
        return registry.DisableOwnedTargets("PracticeFrontend",proof).Complete()?EFZ_TM_FAULTED:EFZ_TM_PENDING;
    }
    if(!notifyInitialized) {
        HMODULE provider=GetModuleHandleA("efz_netplay_mod.dll");
        if(!provider)return EFZ_TM_UNSUPPORTED;
        notifyInitialized=reinterpret_cast<InitializedFn>(GetProcAddress(provider,"EFZ_Netplay_PracticeBattleInitializedV1"));
        if(!notifyInitialized)notifyInitialized=reinterpret_cast<InitializedFn>(GetProcAddress(provider,"_EFZ_Netplay_PracticeBattleInitializedV1"));
    }
    if(!notifyInitialized || !Practice::PrepareFrontendBoundaries(CharacterHotswap::OnSelectorReady,PrepareBattle,InitializedBattle) ||
       !Practice::PrepareBattleCleanupSites())return EFZ_TM_UNSUPPORTED;
    if(registry.Enable(reinterpret_cast<void*>(0x753a15)).Complete() &&
       registry.Enable(reinterpret_cast<void*>(0x763d4a)).Complete() &&
       registry.Enable(reinterpret_cast<void*>(0x763d72)).Complete() &&
       registry.Enable(reinterpret_cast<void*>(0x763da1)).Complete() &&
       registry.Enable(reinterpret_cast<void*>(0x763f04)).Complete()) {
        CharacterHotswap::OnNativeFrontendInstalled();
        return EFZ_TM_READY;
    }
    registry.CloseAdmission("PracticeFrontend");
    // Runs on the native thread, so none of these new callbacks can have executed during install.
    PracticeHooks::RetirementProof proof{true,true,true,registry.Epoch(),receipt};
    return registry.DisableOwnedTargets("PracticeFrontend",proof).Complete()?EFZ_TM_FAULTED:EFZ_TM_PENDING;
} catch(...) {return EFZ_TM_PENDING;}
