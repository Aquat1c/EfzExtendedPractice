#include "netplay/bridge/practice_runtime_bridge.h"
#include "runtime/practice_runtime.h"
#include <windows.h>
#include <cstdio>
namespace {
int failures=0;
uint32_t loadingReturns=0;
uint32_t CaptureRequest(const EfzTmIdentityV1&){return 29;}
void FinishRequest(const EfzTmIdentityV1&,uint32_t ticket,uint32_t native,uint32_t accepted){if(ticket==29&&native==3&&accepted==1)++loadingReturns;}
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(false)
void ActualProviderSourceReceiptFeedsActualRuntime(){
    using namespace netplay::bridge::practice;
    auto* registry=new PracticeHooks::Registry(PracticeHooks::Backend{});
    Practice::BindRuntimeResources(*registry,Practice::NativePatchLedger());
    Owner owner;RuntimeApi api{EFZ_TM_BeginPracticeV1,EFZ_TM_AttachBattleV1,EFZ_TM_BeginRetireV1,EFZ_TM_AdvanceRetireV1,EFZ_TM_GetStatusV1,EFZ_TM_BeginLoadingV1,EFZ_TM_EndLoadingV1};
    CHECK(owner.Register(api,17));
    Practice::BindLoadingRequestConsumer(CaptureRequest,FinishRequest);
    TitleSource source{};source.titleContext=0x12000;source.sourceScreen=0;source.sourceSelection=3;
    source.attributedLocalOwner=true;source.entry.size=sizeof(source.entry);source.entry.abiVersion=EFZ_TM_LIFECYCLE_ABI;
    source.entry.nativeThreadId=GetCurrentThreadId();source.entry.efzBase=0x400000;source.entry.gameSystem=0x10000;
    source.entry.gameMode=1;source.entry.screen=1;
    CHECK(owner.CommittedTitleReturn(source,0)==EFZ_TM_UNSUPPORTED);
    source.foreignGameplay=true;CHECK(owner.CommittedTitleReturn(source,1)==EFZ_TM_UNSUPPORTED);source.foreignGameplay=false;
    source.sourceSelection=1;CHECK(owner.CommittedTitleReturn(source,1)==EFZ_TM_UNSUPPORTED);source.sourceSelection=3;
    source.attributedLocalOwner=false;CHECK(owner.CommittedTitleReturn(source,1)==EFZ_TM_UNSUPPORTED);source.attributedLocalOwner=true;
    CHECK(owner.CommittedTitleReturn(source,3)==EFZ_TM_UNSUPPORTED);
    EfzTmStatusV1 status{};status.size=sizeof(status);status.abiVersion=EFZ_TM_LIFECYCLE_ABI;
    CHECK(EFZ_TM_GetStatusV1(&status)==EFZ_TM_READY);CHECK(status.state==EFZ_TM_DORMANT);
    CHECK(owner.CommittedTitleReturn(source,1)==EFZ_TM_READY);
    CHECK(EFZ_TM_GetStatusV1(&status)==EFZ_TM_READY);CHECK(status.state==EFZ_TM_FRONTEND);
    CHECK(status.id.providerIncarnation==17&&status.id.practiceSession==1&&status.id.battleWorld==0);
    CHECK(owner.CommittedTitleReturn(source,1)==EFZ_TM_STALE);
    CHECK(owner.Register(api,17));CHECK(!owner.Register(api,18));
    auto loading=owner.Current();loading.screen=2;
    auto wrong=loading;wrong.gameSystem+=4;
    CHECK(owner.BeginLoading(wrong,0x14000).ticket==0);
    const auto receipt=owner.BeginLoading(loading,0x14000);
    CHECK(receipt.ticket==29&&receipt.context==0x14000);
    CHECK(owner.EndLoading(receipt,3,1)==EFZ_TM_READY&&loadingReturns==1);
    CHECK(owner.Current().id.battleWorld==0);
    CHECK(EFZ_TM_GetStatusV1(&status)==EFZ_TM_READY&&status.state==EFZ_TM_FRONTEND);
    auto changed=api;changed.endLoading=nullptr;CHECK(!owner.Register(changed,17));

}
}
int main(){ActualProviderSourceReceiptFeedsActualRuntime();return failures?1:0;}
