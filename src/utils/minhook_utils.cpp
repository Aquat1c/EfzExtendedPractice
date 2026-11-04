#include "utils/minhook_utils.h"
#include "core/logger.h"
#include <Windows.h>
#include <array>
#include <map>
#include <cstring>
#include <new>
#include <sstream>
#include <utility>

namespace {
constexpr size_t kNativeGateCount=128;
alignas(void*) void* volatile nativeGateDestinations[kNativeGateCount]{};
template<size_t Index> __declspec(naked) void NativeGateCode(){
    enum { slotOffset=Index*sizeof(void*) };
    // Preserve native stack residue; netplay requires this E9 destination to be MEM_IMAGE.
    __asm jmp dword ptr [nativeGateDestinations+slotOffset]
}
template<size_t... Indices>
constexpr std::array<void(*)(),sizeof...(Indices)> MakeGateCodes(std::index_sequence<Indices...>){
    return {{&NativeGateCode<Indices>...}};
}
constexpr auto nativeGateCodes=MakeGateCodes(std::make_index_sequence<kNativeGateCount>{});
struct NativeEntryGate {
    void* detour=nullptr;
    void* original=nullptr;
    size_t slot=0;
    bool admitted=false;
    bool offlineFrontend=false;
};
struct NativeGates {
    std::mutex mutex;
    std::map<void*,std::unique_ptr<NativeEntryGate>> entries;
    std::array<bool,kNativeGateCount> occupied{};
    bool passthrough=true;
    bool practiceBattle=false;
};
NativeGates& Gates(){static auto* gates=new NativeGates;return *gates;}
void UpdateDestination(NativeEntryGate& gate,bool passthrough){
    InterlockedExchangePointer(&nativeGateDestinations[gate.slot],
        gate.admitted&&!passthrough&&(gate.offlineFrontend||Gates().practiceBattle)?gate.detour:gate.original);
}
int CreateNativeHook(void* target,void* detour,void** original) try {
    static_assert(sizeof(void*)==4,"Native entry gates require x86");
    auto& gates=Gates();
    std::lock_guard<std::mutex> lock(gates.mutex);
    if(gates.entries.find(target)!=gates.entries.end())return MH_ERROR_ALREADY_CREATED;
    size_t slot=0;
    while(slot<kNativeGateCount && gates.occupied[slot])++slot;
    if(slot==kNativeGateCount)return MH_ERROR_MEMORY_ALLOC;
    auto gate=std::make_unique<NativeEntryGate>();
    gate->detour=detour;
    gate->slot=slot;
    const auto inserted=gates.entries.emplace(target,std::move(gate));
    auto& owned=*inserted.first->second;
    const int status=MH_CreateHook(target,reinterpret_cast<void*>(nativeGateCodes[slot]),&owned.original);
    if(status!=MH_OK){gates.entries.erase(inserted.first);return status;}
    gates.occupied[slot]=true;
    UpdateDestination(owned,gates.passthrough);
    if(original)*original=owned.original;
    return MH_OK;
} catch(const std::bad_alloc&){return MH_ERROR_MEMORY_ALLOC;}
void SetNativeAdmission(void* target,bool admitted){
    auto& gates=Gates();
    std::lock_guard<std::mutex> lock(gates.mutex);
    const auto found=gates.entries.find(target);
    if(found==gates.entries.end())return;
    found->second->admitted=admitted;
    UpdateDestination(*found->second,gates.passthrough);
}
int RemoveNativeHook(void* target){
    auto& gates=Gates();
    std::lock_guard<std::mutex> lock(gates.mutex);
    const int status=MH_RemoveHook(target);
    if(status==MH_OK){
        const auto found=gates.entries.find(target);
        if(found!=gates.entries.end()){
            const size_t slot=found->second->slot;
            InterlockedExchangePointer(&nativeGateDestinations[slot],nullptr);
            gates.occupied[slot]=false;
            gates.entries.erase(found);
        }
    }
    return status;
}
bool ReadImage(uintptr_t address,size_t size,std::vector<unsigned char>& out) {
    if(!address || !size || address+size<address)return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)) || info.State!=MEM_COMMIT || info.Type!=MEM_IMAGE || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const DWORD protection=info.Protect&0xff;
    if(protection!=PAGE_EXECUTE && protection!=PAGE_EXECUTE_READ && protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY)return false;
    if(address+size>reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize)return false;
    out.resize(size);SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out.data(),size,&read)&&read==size;
}
PracticeHooks::Backend NativeBackend(){
    PracticeHooks::Backend b;
    b.create=CreateNativeHook;
    b.enable=[](void* t){return static_cast<int>(MH_EnableHook(t));};
    b.disable=[](void* t){return static_cast<int>(MH_DisableHook(t));};
    b.remove=RemoveNativeHook;
    b.setAdmission=SetNativeAdmission;
#ifndef EFZ_MINHOOK_NO_QUEUE_API
    b.queueDisable=[](void* t){return static_cast<int>(MH_QueueDisableHook(t));};
    b.applyQueued=[](){return static_cast<int>(MH_ApplyQueued());};
#endif
    b.read=ReadImage;b.ok=MH_OK;b.alreadyCreated=MH_ERROR_ALREADY_CREATED;
    b.alreadyEnabled=MH_ERROR_ENABLED;b.alreadyDisabled=MH_ERROR_DISABLED;
    return b;
}
void LogOutcome(const char* action,const char* category,const char* label,const PracticeHooks::Outcome& result){
    if(result.Complete())return;
    std::ostringstream os;os<<(category?category:"[HOOK]")<<" "<<action<<" "<<(label?label:"hook")
        <<" retained/pending result="<<static_cast<int>(result.result)<<" backend="<<result.backendStatus<<" remaining="<<result.remaining;
    LogOut(os.str(),true);
}
}
namespace MinHookUtils {
void SetNativePassthrough(bool enabled){
    auto& gates=Gates();
    std::lock_guard<std::mutex> lock(gates.mutex);
    gates.passthrough=enabled;
    for(auto& entry:gates.entries)UpdateDestination(*entry.second,enabled);
}
void SetPracticeBattleActive(bool active){
    auto& gates=Gates();
    std::lock_guard<std::mutex> lock(gates.mutex);
    gates.practiceBattle=active;
    for(auto& entry:gates.entries)UpdateDestination(*entry.second,gates.passthrough);
}
PracticeHooks::Registry& OwnedHooks(){
    // Resident lifetime: never destroy while callbacks or foreign chains hold
    // execution obligations.
    static auto* registry=new PracticeHooks::Registry(NativeBackend());return *registry;
}
PracticeHooks::Outcome CloseAdmission(const char* owner){return OwnedHooks().CloseAdmission(owner?owner:"");}
PracticeHooks::Outcome DisableOwnedTargets(const char* owner,const PracticeHooks::RetirementProof& proof){return OwnedHooks().DisableOwnedTargets(owner?owner:"",proof);}
PracticeHooks::Outcome ReclaimDrainedTargets(const char* owner,const PracticeHooks::RetirementProof& proof){return OwnedHooks().ReclaimDrainedTargets(owner?owner:"",proof);}
bool HasOwnedTarget(void* target){for(const auto& r:OwnedHooks().Snapshot())if(r.spec.target==target)return true;return false;}
bool EnableOwnedTargets(const char* owner){
    bool result=true;
    for(const auto& r:OwnedHooks().Snapshot()){
        if(owner&&r.spec.owner!=owner)continue;
        const auto image=OwnedHooks().Image(r.spec.target);
        const auto outcome=OwnedHooks().Enable(r.spec.target);
        LogOutcome("enable",r.spec.owner.c_str(),r.spec.abi.c_str(),outcome);
        if(outcome.Complete()&&r.state==PracticeHooks::State::Enabled&&image!=PracticeHooks::EntryImage::Installed){
            std::ostringstream os;os<<r.spec.owner<<" enable "<<r.spec.abi
                <<(image==PracticeHooks::EntryImage::Preimage?" re-installed over a peer-restored entry":" admission reopened over a foreign entry image");
            LogOut(os.str(),true);
        }
        result=outcome.Complete()&&result;
    }
    return result;
}
bool CreateHook(void* target,void* detour,void** original,const char* category,const char* label,bool* alreadyCreated,PracticeHooks::CallbackTicket* ticket,bool offlineFrontend){
    if(alreadyCreated)*alreadyCreated=false;
    PracticeHooks::TargetSpec spec;
    for(const auto& r:OwnedHooks().Snapshot())if(r.spec.target==target){spec=r.spec;if(alreadyCreated)*alreadyCreated=true;break;}
    const bool existing=spec.target!=nullptr;
    // One original slot per detour; a second target cannot replace it before reclaim.
    for(const auto& record:OwnedHooks().Snapshot())
        if(record.spec.detour==detour && record.spec.target!=target) return false;
    spec.target=target;spec.detour=detour;spec.owner=category?category:"[HOOK]";
    spec.abi=label?label:"hook";
    if(!existing){
        HMODULE module=nullptr;
        if(!target || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCSTR>(target),&module))return false;
        spec.modulePin=std::shared_ptr<void>(module,[](void* p){FreeLibrary(static_cast<HMODULE>(p));});
        static std::atomic<uint64_t> nextIncarnation{1};
        uint64_t incarnation=nextIncarnation.load();
        do { if(!incarnation || incarnation==UINT64_MAX) return false; }
        while(!nextIncarnation.compare_exchange_weak(incarnation,incarnation+1));
        spec.moduleIncarnation=incarnation;
        // Include MinHook's possible five-byte hotpatch area above the entry.
        spec.rangeStart=reinterpret_cast<uintptr_t>(target)-5;
        if(!ReadImage(spec.rangeStart,21,spec.preimage))return false;
    }
    const auto result=OwnedHooks().Acquire(spec,original);
    LogOutcome("create",category,label,result);
    if (!result.Complete()) return false;
    {
        auto& gates=Gates();
        std::lock_guard<std::mutex> lock(gates.mutex);
        const auto gate=gates.entries.find(target);
        if(gate!=gates.entries.end()) {
            gate->second->offlineFrontend=offlineFrontend;
            UpdateDestination(*gate->second,gates.passthrough);
        }
    }
    if (ticket) {
        const auto bound=OwnedHooks().BindCallback(target,*ticket);
        LogOutcome("bind callback",category,label,bound);
        return bound.Complete();
    }
    return true;
}
bool EnableHook(void* target,const char* category,const char* label,bool* alreadyEnabled){
    if(alreadyEnabled)*alreadyEnabled=false;
    const auto result=OwnedHooks().Enable(target);LogOutcome("enable",category,label,result);return result.Complete();
}
bool DisableHook(void* target,const char* category,const char* label,bool* alreadyDisabled){
    if(alreadyDisabled)*alreadyDisabled=false;
    // Legacy callers have no safe continuation; retain the executable attachment.
    CloseAdmission(category);
    const auto result=DisableOwnedTargets(category,{});LogOutcome("disable",category,label,result);
    return result.Complete() && (!target || HasOwnedTarget(target));
}
bool RemoveHook(void* target,const char* category,const char* label,bool* wasMissing){
    if(wasMissing)*wasMissing=false;
    if(!target)return true;
    if(!HasOwnedTarget(target))return false;
    CloseAdmission(category);
    const auto result=ReclaimDrainedTargets(category,{});LogOutcome("remove",category,label,result);return result.Complete()&&!HasOwnedTarget(target);
}
bool CreateAndEnableHook(void* target,void* detour,void** original,const char* category,const char* label,bool* alreadyCreated,bool* alreadyEnabled,PracticeHooks::CallbackTicket* ticket,bool offlineFrontend){
    return CreateHook(target,detour,original,category,label,alreadyCreated,ticket,offlineFrontend)&&EnableHook(target,category,label,alreadyEnabled);
}
}
