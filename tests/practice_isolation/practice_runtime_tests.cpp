#include "runtime/practice_contract.h"
#include "runtime/practice_runtime.h"
#include "runtime/practice_worker.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <new>
namespace { bool failAllocation=false; }
void* operator new(size_t bytes) {
    if(failAllocation){failAllocation=false;throw std::bad_alloc();}
    if(void* p=std::malloc(bytes?bytes:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
namespace {
int failures=0;
uint32_t loadingCaptures=0,loadingReturns=0;
uint32_t CaptureLoading(const EfzTmIdentityV1& id){++loadingCaptures;return id.practiceSession==7?41:0;}
void ConsumeLoading(const EfzTmIdentityV1&,uint32_t ticket,uint32_t native,uint32_t accepted){if(ticket==41&&native==3&&accepted==1)++loadingReturns;}
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(false)
void ActualLegacyResourcesCannotAppearRetired(){
    struct Timing:Practice::MonitorTimingBackend {
        bool BeginTimer() noexcept override{return true;}
        bool EndTimer() noexcept override{return true;}
        uintptr_t CapturePriorityOwner() noexcept override{return 1;}
        void ReleasePriorityOwner(uintptr_t) noexcept override{}
        int Priority(uintptr_t) noexcept override{return 2;}
        bool SetPriority(uintptr_t,int) noexcept override{return true;}
    };
    static Timing timing;static Practice::MonitorTimingLease lease(timing);
    PracticeHooks::Backend backend;
    backend.create=[](void*,void*,void** original){*original=reinterpret_cast<void*>(0x1234);return 0;};
    backend.read=[](uintptr_t,size_t,std::vector<unsigned char>& out){out={1,2};return true;};
    auto* registry=new PracticeHooks::Registry(backend);Practice::BindRuntimeResources(*registry,Practice::NativePatchLedger());
    PracticeHooks::TargetSpec spec;spec.target=reinterpret_cast<void*>(16);spec.detour=reinterpret_cast<void*>(32);
    spec.owner="legacy input";spec.abi="native function";spec.rangeStart=16;spec.preimage={1,2};
    spec.moduleIncarnation=1;spec.modulePin=std::make_shared<int>(1);void* original=nullptr;
    CHECK(registry->Acquire(spec,&original).Complete());
    lease.BeginInterval();Practice::MonitorThreadStarted(lease);
    EfzTmStatusV1 status{};status.size=sizeof(status);status.abiVersion=EFZ_TM_LIFECYCLE_ABI;
    CHECK(EFZ_TM_GetStatusV1(&status)==EFZ_TM_READY);
    CHECK(status.attachedTrainingTargets==1&&status.trainingWorkersActive==1&&status.timerLeaseHeld==1);
    Practice::MonitorThreadStopped();CHECK(lease.EndInterval());
}
void ActualEntrySeparateAttachmentAndHeldRetirement(bool duplicateBegin){
    EfzTmEntryV1 entry{};entry.size=sizeof(entry);entry.abiVersion=EFZ_TM_LIFECYCLE_ABI;
    entry.capabilities=EFZ_TM_CAP_OLD_WORLD_HOLD;entry.nativeThreadId=GetCurrentThreadId();
    entry.id={5,7,0,0};entry.efzBase=0x400000;entry.gameSystem=0x10000;
    entry.gameMode=4;entry.screen=1;
    CHECK(EFZ_TM_BeginPracticeV1(&entry)==EFZ_TM_UNSUPPORTED);
    entry.gameMode=1;CHECK(EFZ_TM_BeginPracticeV1(&entry)==EFZ_TM_READY);
    EfzTmStatusV1 status{};status.size=sizeof(status);status.abiVersion=EFZ_TM_LIFECYCLE_ABI;
    CHECK(EFZ_TM_GetStatusV1(&status)==EFZ_TM_READY);CHECK(status.state==EFZ_TM_FRONTEND);
    CHECK(status.trainingWorkersActive==0&&status.timerLeaseHeld==0);
    auto stale=entry;stale.id.practiceSession=6;CHECK(EFZ_TM_BeginPracticeV1(&stale)==EFZ_TM_STALE);
    Practice::BindLoadingRequestConsumer(CaptureLoading,ConsumeLoading);
    EfzTmIdentityV1 captured{};CHECK(Practice::CaptureCurrentPracticeIdentity(captured));
    CHECK(captured.practiceSession==7&&captured.battleWorld==0);
    auto loading=entry;loading.screen=2;uint32_t ticket=0;
    auto foreign=loading;foreign.gameSystem+=4;
    CHECK(EFZ_TM_BeginLoadingV1(&foreign,0x14000,&ticket)==EFZ_TM_STALE&&ticket==0);
    CHECK(loadingCaptures==0);
    CHECK(EFZ_TM_BeginLoadingV1(&loading,0x14000,&ticket)==EFZ_TM_READY&&ticket==41);
    CHECK(EFZ_TM_EndLoadingV1(&foreign,0x14000,ticket,3,1)==EFZ_TM_STALE);
    CHECK(EFZ_TM_EndLoadingV1(&loading,0,ticket,3,1)==EFZ_TM_UNSUPPORTED);
    CHECK(loadingReturns==0);
    CHECK(EFZ_TM_EndLoadingV1(&loading,0x14000,ticket,3,1)==EFZ_TM_READY&&loadingReturns==1);
    CHECK(EFZ_TM_GetStatusV1(&status)==EFZ_TM_READY&&status.state==EFZ_TM_FRONTEND&&status.id.battleWorld==0);
    auto battle=entry;battle.id.battleWorld=11;battle.id.timeline=1;battle.battleContext=0x11000;
    battle.player1=0x12000;battle.player2=0x13000;battle.screen=2;
    CHECK(EFZ_TM_AttachBattleV1(&battle)==EFZ_TM_UNSUPPORTED);
    battle.screen=3;CHECK(EFZ_TM_AttachBattleV1(&battle)==EFZ_TM_PENDING);
    CHECK(EFZ_TM_GetStatusV1(&status)==EFZ_TM_READY);CHECK(status.state==EFZ_TM_ARMING);
    CHECK(status.id.battleWorld==11&&status.trainingWorkersActive==0);
    EfzTmRetireV1 retire{sizeof(retire),EFZ_TM_LIFECYCLE_ABI,battle.id,EFZ_TM_RELOAD_WORLD,1};
    CHECK(EFZ_TM_BeginRetireV1(&retire)==EFZ_TM_PENDING);
    CHECK(EFZ_TM_AdvanceRetireV1(&retire)==EFZ_TM_PENDING);
    CHECK(EFZ_TM_GetStatusV1(&status)==EFZ_TM_READY);
    CHECK(status.restorationObligations!=0&&status.state==EFZ_TM_REVOKING);
    auto old=retire;old.id.battleWorld=10;CHECK(EFZ_TM_AdvanceRetireV1(&old)==EFZ_TM_STALE);
    retire.oldWorldHeld=0;
    if(duplicateBegin) {
        auto wrong=retire;wrong.reason=EFZ_TM_LEAVE_PRACTICE;
        CHECK(EFZ_TM_BeginRetireV1(&wrong)==EFZ_TM_STALE);
        CHECK(EFZ_TM_GetStatusV1(&status)==EFZ_TM_READY&&status.state==EFZ_TM_REVOKING);
        CHECK(EFZ_TM_BeginRetireV1(&retire)==EFZ_TM_FAULTED);
        retire.oldWorldHeld=1;
        CHECK(EFZ_TM_AdvanceRetireV1(&retire)==EFZ_TM_FAULTED);
    } else CHECK(EFZ_TM_AdvanceRetireV1(&retire)==EFZ_TM_FAULTED);
    CHECK(EFZ_TM_GetStatusV1(&status)==EFZ_TM_READY);CHECK(status.state==EFZ_TM_RUNTIME_FAULTED);
    retire.oldWorldHeld=1;CHECK(EFZ_TM_AdvanceRetireV1(&retire)==EFZ_TM_FAULTED);
}
void StatusFailureCannotEscapeOrCertifyEmptyResources(){
    EfzTmStatusV1 status{};status.size=sizeof(status);status.abiVersion=EFZ_TM_LIFECYCLE_ABI;
    failAllocation=true;
    bool escaped=false;uint32_t result=EFZ_TM_READY;
    try {result=EFZ_TM_GetStatusV1(&status);}catch(...){escaped=true;}
    failAllocation=false;
    CHECK(!escaped&&result==EFZ_TM_FAULTED);
    CHECK(status.state==EFZ_TM_RUNTIME_FAULTED&&status.restorationObligations==UINT32_MAX);
    CHECK(status.attachedTrainingTargets==UINT32_MAX&&status.outstandingCallbacks==UINT32_MAX);
    CHECK(EFZ_TM_GetStatusV1(&status)==EFZ_TM_FAULTED);
}
}
int main(int argc,char**){ActualLegacyResourcesCannotAppearRetired();ActualEntrySeparateAttachmentAndHeldRetirement(argc>1);StatusFailureCannotEscapeOrCertifyEmptyResources();return failures?1:0;}
