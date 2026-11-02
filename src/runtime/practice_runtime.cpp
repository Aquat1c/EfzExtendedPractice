#define EFZ_TM_RUNTIME_EXPORTS
#include "runtime/practice_contract.h"
#include "runtime/practice_session.h"
#include "runtime/practice_runtime.h"
#include "runtime/practice_worker.h"
#include "utils/utilities.h"
#include <windows.h>

namespace {
std::mutex ownerMutex;
Practice::WorkDomain battleWork;
constexpr bool kStrictWorkerActivation=false;
std::atomic<bool> workerLifecycleOwned{false};
Practice::MeasurementResetConsumer resetMeasurements=nullptr;
bool measurementsRetired=false;
Practice::CaptureInputBaseline captureInput=nullptr;
Practice::RetireInputBaseline retireInput=nullptr;
Practice::CancelInputWork cancelInput=nullptr;
bool inputBaselineAdopted=false;
uint32_t cleanupBranch=UINT32_MAX,cleanupAcceptedScreen=UINT32_MAX;
bool cleanupResumeEntered=false,cleanupTailReturned=false;
EfzTmEntryV1 current{};
EfzTmRetireV1 retiring{};
uint32_t state=EFZ_TM_DORMANT;
uint32_t fault=0;
std::atomic<bool> abiFault{false};
PracticeHooks::Registry* hooks=nullptr;
Practice::PatchLedger* patches=nullptr;
Practice::CaptureLoadingRequest captureLoading=nullptr;
Practice::ConsumeLoadingRequest consumeLoading=nullptr;
uint32_t loadingContext=0,loadingTicket=0;
EfzTmEntryV1 loadingEntry{};
bool LoadingSourceMatches(const EfzTmEntryV1& entry) {
    return entry.id.providerIncarnation==current.id.providerIncarnation &&
        entry.id.practiceSession==current.id.practiceSession &&
        entry.id.battleWorld==current.id.battleWorld && entry.id.timeline==current.id.timeline &&
        entry.gameSystem==current.gameSystem && entry.efzBase==current.efzBase &&
        entry.nativeThreadId==current.nativeThreadId && state==EFZ_TM_FRONTEND;
}
bool oldWorldRestored=false,inputProducersRetired=false,renderOwnersRetired=false,nativePatchOwnerVerified=false;
bool SameIdentity(const EfzTmIdentityV1& a,const EfzTmIdentityV1& b) {
    return a.providerIncarnation==b.providerIncarnation && a.practiceSession==b.practiceSession &&
        a.battleWorld==b.battleWorld && a.timeline==b.timeline;
}
bool EntryValid(const EfzTmEntryV1* entry) {
    return entry && entry->size==sizeof(*entry) && entry->abiVersion==EFZ_TM_LIFECYCLE_ABI &&
        entry->id.providerIncarnation && entry->id.practiceSession && entry->efzBase &&
        entry->gameSystem && entry->gameMode==1 && entry->nativeThreadId==GetCurrentThreadId();
}
bool RetireValid(const EfzTmRetireV1* request) {
    return request && request->size==sizeof(*request) && request->abiVersion==EFZ_TM_LIFECYCLE_ABI &&
        request->reason>=EFZ_TM_RELOAD_WORLD && request->reason<=EFZ_TM_EXPLICIT_UNLOAD;
}
bool CleanupParticipantsReady() {
    if(!oldWorldRestored || !inputProducersRetired || !renderOwnersRetired || !nativePatchOwnerVerified ||
       !measurementsRetired || !hooks || !patches || !Practice::MonitorParked() || battleWork.OutstandingWork())return false;
    const auto timing=Practice::ReadMonitorStatus();
    if(timing.timerHeld || timing.priorityHeld || timing.waitFaulted)return false;
    for(const auto& record:hooks->Snapshot()) {
        // Audio hooks are process-lifetime, not Practice-owned.
        if(record.spec.owner=="[AUDIO][HOOK]")continue;
        const bool retainedFrontend=record.spec.owner=="PracticeFrontend" ||
            (retiring.reason==EFZ_TM_RELOAD_WORLD && record.spec.owner=="[HOTSWAP][DIRECT]");
        if(!retainedFrontend || record.callbacks->outstanding.load())return false;
        if(retiring.reason!=EFZ_TM_RELOAD_WORLD &&
           (record.state==PracticeHooks::State::Enabled || record.state==PracticeHooks::State::ExternalInstalling))return false;
    }
    return true;
}

}
namespace Practice {
void BindMeasurementResetConsumer(MeasurementResetConsumer consumer) {
    std::lock_guard<std::mutex> lock(ownerMutex);if(!resetMeasurements)resetMeasurements=consumer;
}
void BindInputRetirementConsumer(CaptureInputBaseline capture,RetireInputBaseline retire,CancelInputWork cancel) {
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!captureInput && !retireInput && capture && retire && cancel){captureInput=capture;retireInput=retire;cancelInput=cancel;}
}
bool PrepareBattleInputs(uint32_t battle,uint32_t gameSystem,uint32_t player1,uint32_t player2) try {
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(state!=EFZ_TM_FRONTEND || !captureInput || current.gameSystem!=gameSystem ||
       current.nativeThreadId!=GetCurrentThreadId() || !battle || !player1 || !player2)return false;
    auto prepared=current;prepared.battleContext=battle;prepared.player1=player1;prepared.player2=player2;prepared.screen=3;
    return captureInput(prepared);
} catch(...) {abiFault.store(true,std::memory_order_release);return false;}
WorkLease TryEnterMonitorWork() noexcept {return battleWork.TryEnter();}
bool MonitorLifecycleOwned() noexcept {return workerLifecycleOwned.load(std::memory_order_acquire);}
bool LegacyMonitorStartupRequired() noexcept {return !kStrictWorkerActivation;}
void BindRuntimeResources(PracticeHooks::Registry& registry,PatchLedger& ledger) {
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!hooks && !patches) {hooks=&registry;patches=&ledger;}
}
void RegisterExistingPracticeProvider() {
    HMODULE provider=GetModuleHandleA("efz_netplay_mod.dll");
    if(!provider)return;
    using RegisterFn=uint32_t (__cdecl*)(uint32_t);
    auto bind=reinterpret_cast<RegisterFn>(GetProcAddress(provider,"EFZ_Netplay_RegisterPracticeRuntimeV1"));
    if(!bind)bind=reinterpret_cast<RegisterFn>(GetProcAddress(provider,"_EFZ_Netplay_RegisterPracticeRuntimeV1"));
    HMODULE training=nullptr;
    if(bind && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&EFZ_TM_BeginPracticeV1),&training))
        (void)bind(reinterpret_cast<uintptr_t>(training));
}
}
extern "C" uint32_t __cdecl EFZ_TM_BeginPracticeV1(const EfzTmEntryV1* entry) try {
    if(abiFault.load(std::memory_order_acquire))return EFZ_TM_FAULTED;
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!EntryValid(entry) || (entry->screen!=1 && entry->screen!=2) ||
       entry->id.battleWorld || entry->id.timeline)return EFZ_TM_UNSUPPORTED;
    if(state!=EFZ_TM_DORMANT && state!=EFZ_TM_QUIESCENT)
        return state==EFZ_TM_FRONTEND && SameIdentity(current.id,entry->id)?EFZ_TM_READY:EFZ_TM_STALE;
    current=*entry;state=EFZ_TM_FRONTEND;fault=0;
    return EFZ_TM_READY;
} catch(...) {abiFault.store(true,std::memory_order_release);return EFZ_TM_FAULTED;}
extern "C" uint32_t __cdecl EFZ_TM_AttachBattleV1(const EfzTmEntryV1* entry) try {
    if(abiFault.load(std::memory_order_acquire))return EFZ_TM_FAULTED;
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!EntryValid(entry) || entry->screen!=3 || !entry->id.battleWorld || !entry->id.timeline ||
       !entry->battleContext || !entry->player1 || !entry->player2)return EFZ_TM_UNSUPPORTED;
    if(loadingContext || state!=EFZ_TM_FRONTEND || current.id.providerIncarnation!=entry->id.providerIncarnation ||
       current.id.practiceSession!=entry->id.practiceSession)return EFZ_TM_STALE;
    current=*entry;state=EFZ_TM_ARMING;measurementsRetired=false;
    inputBaselineAdopted=captureInput && captureInput(*entry);
    if constexpr(kStrictWorkerActivation) {
        if(!(entry->capabilities&EFZ_TM_CAP_OLD_WORLD_HOLD) || !resetMeasurements || !inputBaselineAdopted)return EFZ_TM_PENDING;
        if(!battleWork.Publish(*entry))return EFZ_TM_PENDING;
        workerLifecycleOwned.store(true,std::memory_order_release);
        if(!Practice::StartMonitorThread()){battleWork.CloseAdmission();state=EFZ_TM_RUNTIME_FAULTED;fault=3;return EFZ_TM_FAULTED;}
        Practice::NotifyMonitorScopeChanged();
    }
    return EFZ_TM_PENDING;
} catch(...) {abiFault.store(true,std::memory_order_release);return EFZ_TM_FAULTED;}
extern "C" uint32_t __cdecl EFZ_TM_BeginRetireV1(const EfzTmRetireV1* request) try {
    if(abiFault.load(std::memory_order_acquire))return EFZ_TM_FAULTED;
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!RetireValid(request))return EFZ_TM_UNSUPPORTED;
    if(!SameIdentity(current.id,request->id) || state==EFZ_TM_DORMANT || state==EFZ_TM_QUIESCENT)
        return EFZ_TM_STALE;
    if(state==EFZ_TM_REVOKING || state==EFZ_TM_RUNTIME_FAULTED) {
        if(!SameIdentity(retiring.id,request->id) || retiring.reason!=request->reason)return EFZ_TM_STALE;
        if(!request->oldWorldHeld) {
            retiring.oldWorldHeld=0;fault=1;state=EFZ_TM_RUNTIME_FAULTED;
        }
        return fault?EFZ_TM_FAULTED:EFZ_TM_PENDING;
    }
    retiring=*request;battleWork.CloseAdmission();Practice::NotifyMonitorScopeChanged();state=EFZ_TM_REVOKING;
    if(cancelInput)cancelInput();
    if(!request->oldWorldHeld) {fault=1;state=EFZ_TM_RUNTIME_FAULTED;return EFZ_TM_FAULTED;}
    return EFZ_TM_PENDING;
} catch(...) {abiFault.store(true,std::memory_order_release);return EFZ_TM_FAULTED;}
extern "C" uint32_t __cdecl EFZ_TM_AdvanceRetireV1(const EfzTmRetireV1* request) try {
    if(abiFault.load(std::memory_order_acquire))return EFZ_TM_FAULTED;
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!RetireValid(request))return EFZ_TM_UNSUPPORTED;
    if(!SameIdentity(retiring.id,request->id) || retiring.reason!=request->reason ||
       (state!=EFZ_TM_REVOKING && state!=EFZ_TM_RUNTIME_FAULTED))return EFZ_TM_STALE;
    if(!request->oldWorldHeld) {fault=1;state=EFZ_TM_RUNTIME_FAULTED;}
    if(fault)return EFZ_TM_FAULTED;
    Practice::NotifyMonitorScopeChanged();
    const bool monitorDrained=workerLifecycleOwned.load(std::memory_order_acquire) && Practice::MonitorParked() && !battleWork.OutstandingWork();
    if(monitorDrained && inputBaselineAdopted && !inputProducersRetired && retireInput) {
        const auto outcome=retireInput(current,request->oldWorldHeld!=0);
        if(outcome==EFZ_TM_READY)inputProducersRetired=true;
        else if(outcome!=EFZ_TM_PENDING){fault=4;state=EFZ_TM_RUNTIME_FAULTED;return EFZ_TM_FAULTED;}
    }
    if(!measurementsRetired && resetMeasurements && oldWorldRestored && inputProducersRetired && renderOwnersRetired &&
       monitorDrained) {
        resetMeasurements(current.id,Practice::ResetReason::RetireWorld);measurementsRetired=true;
    }
    return CleanupParticipantsReady()?EFZ_TM_READY:EFZ_TM_PENDING;
} catch(...) {abiFault.store(true,std::memory_order_release);return EFZ_TM_FAULTED;}
extern "C" uint32_t __cdecl EFZ_TM_NotifyNativeRestoreV1(const EfzTmIdentityV1* before,uint32_t succeeded) try {
    if(abiFault.load(std::memory_order_acquire))return EFZ_TM_FAULTED;
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!before || !SameIdentity(current.id,*before))return EFZ_TM_STALE;
    (void)succeeded;return EFZ_TM_UNSUPPORTED;
} catch(...) {abiFault.store(true,std::memory_order_release);return EFZ_TM_FAULTED;}
extern "C" uint32_t __cdecl EFZ_TM_GetStatusV1(EfzTmStatusV1* status) try {
    if(!status || status->size!=sizeof(*status) || status->abiVersion!=EFZ_TM_LIFECYCLE_ABI)
        return EFZ_TM_UNSUPPORTED;
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!hooks || !patches)return EFZ_TM_UNSUPPORTED;
    if(abiFault.load(std::memory_order_acquire)) {
        *status={sizeof(*status),EFZ_TM_LIFECYCLE_ABI,current.id,EFZ_TM_RUNTIME_FAULTED,
            UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,1,2};
        return EFZ_TM_FAULTED;
    }
    const auto records=hooks->Snapshot();
    uint32_t callbacks=0;
    for(const auto& record:records) {
        const uint32_t count=record.callbacks->outstanding.load();
        callbacks=count>UINT32_MAX-callbacks?UINT32_MAX:callbacks+count;
    }
    const auto monitor=Practice::ReadMonitorStatus();
    uint32_t obligations=patches->Obligations();
    if(state!=EFZ_TM_DORMANT && state!=EFZ_TM_QUIESCENT)
        obligations+=!oldWorldRestored+!inputProducersRetired+!renderOwnersRetired+!nativePatchOwnerVerified;
    obligations+=monitor.priorityHeld+monitor.waitFaulted+(loadingContext?1u:0u);
    *status={sizeof(*status),EFZ_TM_LIFECYCLE_ABI,current.id,state,
        static_cast<uint32_t>(records.size()),callbacks,obligations,monitor.running,monitor.timerHeld?1u:0u,fault};
    return EFZ_TM_READY;
 } catch(...) {
    abiFault.store(true,std::memory_order_release);
    if(status && status->size==sizeof(*status) && status->abiVersion==EFZ_TM_LIFECYCLE_ABI)
        *status={sizeof(*status),EFZ_TM_LIFECYCLE_ABI,{},EFZ_TM_RUNTIME_FAULTED,
            UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,1,2};
    return EFZ_TM_FAULTED;
}

namespace Practice {
void BindLoadingRequestConsumer(CaptureLoadingRequest capture,ConsumeLoadingRequest consume) {
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!captureLoading && !consumeLoading && capture && consume) {captureLoading=capture;consumeLoading=consume;}
}
bool CaptureCurrentPracticeIdentity(EfzTmIdentityV1& identity) {
    std::lock_guard<std::mutex> lock(ownerMutex);identity={};
    if(abiFault.load(std::memory_order_acquire) || !current.id.practiceSession ||
       state==EFZ_TM_DORMANT || state==EFZ_TM_QUIESCENT || state==EFZ_TM_REVOKING || state==EFZ_TM_RUNTIME_FAULTED)return false;
    identity=current.id;return true;
}
}
extern "C" uint32_t __cdecl EFZ_TM_BeginLoadingV1(const EfzTmEntryV1* entry,uint32_t context,uint32_t* ticket) try {
    if(ticket)*ticket=0;
    if(abiFault.load(std::memory_order_acquire))return EFZ_TM_FAULTED;
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!EntryValid(entry) || entry->screen!=2 || !context || !ticket)return EFZ_TM_UNSUPPORTED;
    if(!LoadingSourceMatches(*entry) || loadingContext)return EFZ_TM_STALE;
    if(!captureLoading || !consumeLoading)return EFZ_TM_UNSUPPORTED;
    const auto captured=captureLoading(entry->id);
    if(!captured)return EFZ_TM_UNSUPPORTED;
    loadingEntry=*entry;loadingContext=context;loadingTicket=captured;*ticket=captured;
    return EFZ_TM_READY;
} catch(...) {abiFault.store(true,std::memory_order_release);return EFZ_TM_FAULTED;}
extern "C" uint32_t __cdecl EFZ_TM_EndLoadingV1(const EfzTmEntryV1* entry,uint32_t context,uint32_t ticket,
                                              uint32_t nativeResult,uint32_t acceptedResult) try {
    if(abiFault.load(std::memory_order_acquire))return EFZ_TM_FAULTED;
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!EntryValid(entry) || entry->screen!=2 || !context || !ticket || nativeResult>255 || acceptedResult>255)return EFZ_TM_UNSUPPORTED;
    if(!LoadingSourceMatches(*entry) || context!=loadingContext || ticket!=loadingTicket ||
       !SameIdentity(entry->id,loadingEntry.id))return EFZ_TM_STALE;
    consumeLoading(entry->id,ticket,nativeResult,acceptedResult);
    loadingContext=0;loadingTicket=0;loadingEntry={};
    return EFZ_TM_READY;
} catch(...) {abiFault.store(true,std::memory_order_release);return EFZ_TM_FAULTED;}

extern "C" __declspec(dllexport) uint32_t __cdecl EFZ_TM_WaitRetireProgressV1(const EfzTmRetireV1* request) try {
    {
        std::lock_guard<std::mutex> lock(ownerMutex);
        if(!RetireValid(request) || !SameIdentity(retiring.id,request->id) || retiring.reason!=request->reason ||
           current.nativeThreadId!=GetCurrentThreadId())return EFZ_TM_STALE;
        if(state==EFZ_TM_RUNTIME_FAULTED || fault || !retiring.oldWorldHeld || !request->oldWorldHeld)return EFZ_TM_FAULTED;
        if(state!=EFZ_TM_REVOKING)return EFZ_TM_STALE;
    }
    return Practice::WaitForRetirementProgress()?EFZ_TM_PENDING:EFZ_TM_FAULTED;
} catch(...) {abiFault.store(true,std::memory_order_release);return EFZ_TM_FAULTED;}

namespace Practice {
bool RecordBattleCleanupBranch(uint32_t battle,uint32_t branch) noexcept try {
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(branch>1 || !current.id.battleWorld || current.battleContext!=battle ||
       current.nativeThreadId!=GetCurrentThreadId() || (state!=EFZ_TM_BATTLE && state!=EFZ_TM_REVOKING))return false;
    if(cleanupBranch!=UINT32_MAX)return cleanupBranch==branch && !cleanupResumeEntered;
    cleanupBranch=branch;return true;
} catch(...) {abiFault.store(true,std::memory_order_release);return false;}
bool TakeBattleCleanupResume(uint32_t battle,uint32_t gameSystem,uint32_t branch) noexcept try {
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(abiFault.load() || fault || state!=EFZ_TM_REVOKING || !retiring.oldWorldHeld || cleanupResumeEntered ||
       cleanupBranch!=branch || current.battleContext!=battle || current.gameSystem!=gameSystem ||
       current.nativeThreadId!=GetCurrentThreadId() || !CleanupParticipantsReady())return false;
    cleanupResumeEntered=true;return true;
} catch(...) {abiFault.store(true,std::memory_order_release);return false;}
bool RecordBattleCleanupReturned(uint32_t battle,uint32_t gameSystem,uint32_t acceptedScreen) noexcept try {
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(!cleanupResumeEntered || cleanupTailReturned || fault || state!=EFZ_TM_REVOKING ||
       current.battleContext!=battle || current.gameSystem!=gameSystem || current.nativeThreadId!=GetCurrentThreadId() ||
       (acceptedScreen!=0 && acceptedScreen!=1 && acceptedScreen!=2 && acceptedScreen!=5 && acceptedScreen!=8))return false;
    cleanupAcceptedScreen=acceptedScreen;cleanupTailReturned=true;return true;
} catch(...) {abiFault.store(true,std::memory_order_release);return false;}
}
extern "C" __declspec(dllexport) uint32_t __cdecl EFZ_TM_CommitBattleCleanupV1(uint32_t battle,uint32_t gameSystem,uint32_t acceptedScreen) try {
    std::lock_guard<std::mutex> lock(ownerMutex);
    if(abiFault.load() || fault || !cleanupTailReturned || state!=EFZ_TM_REVOKING ||
       current.battleContext!=battle || current.gameSystem!=gameSystem || current.nativeThreadId!=GetCurrentThreadId() ||
       cleanupAcceptedScreen!=acceptedScreen)return EFZ_TM_FAULTED;
    if(!battleWork.Reclaim())return EFZ_TM_FAULTED;
    if(retiring.reason==EFZ_TM_RELOAD_WORLD && (acceptedScreen==1 || acceptedScreen==2)) {
        current.id.battleWorld=0;current.id.timeline=0;current.battleContext=0;current.player1=0;current.player2=0;
        current.screen=acceptedScreen;state=EFZ_TM_FRONTEND;
    } else {
        current.battleContext=0;current.player1=0;current.player2=0;
    }
    cleanupBranch=cleanupAcceptedScreen=UINT32_MAX;cleanupResumeEntered=cleanupTailReturned=false;
    return EFZ_TM_READY;
} catch(...) {abiFault.store(true,std::memory_order_release);return EFZ_TM_FAULTED;}
