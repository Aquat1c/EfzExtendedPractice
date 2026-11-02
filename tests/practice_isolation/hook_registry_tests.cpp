#include "runtime/hook_registry.h"
#include <cstdio>
#include <map>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <string>
#endif
static bool countAllocations=false;
static size_t allocationCount=0;
void* operator new(size_t n){if(countAllocations)++allocationCount;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
using namespace PracticeHooks;
namespace {
int failures=0;
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); ++failures;} }while(false)
struct Fake {
    std::map<void*, std::vector<unsigned char>> bytes;
    int create=0,enable=0,disable=0,remove=0;
    Backend Get() { Backend b;
        b.create=[this](void*,void*,void** original){if(!create)*original=reinterpret_cast<void*>(0x777);return create;};
        b.enable=[this](void* p){if(!enable)bytes[p]={9,9};return enable;};
        b.disable=[this](void* p){if(!disable)bytes[p]={1,2};return disable;};
        b.remove=[this](void*){return remove;};
        b.read=[this](uintptr_t p,size_t n,std::vector<unsigned char>& out){out=bytes[reinterpret_cast<void*>(p)];return out.size()==n;};
        return b;
    }
    TargetSpec Spec(uintptr_t p=16){ TargetSpec s; s.target=reinterpret_cast<void*>(p);s.detour=reinterpret_cast<void*>(123);s.owner="input";s.abi="fastcall-int";s.moduleIncarnation=1;s.rangeStart=p;s.preimage={1,2};s.modulePin=std::make_shared<int>(1);bytes[s.target]=s.preimage;return s; }
};
void FailureRetention(){
    Fake f;Registry r(f.Get());auto s=f.Spec();void* original=nullptr;CallbackTicket ticket;
    f.create=3;CHECK(!r.Acquire(s,&original).Complete());CHECK(r.Snapshot().empty());f.create=0;
    CHECK(r.Acquire(s,&original).Complete());CHECK(r.BindCallback(s.target,ticket).Complete());CHECK(original==reinterpret_cast<void*>(0x777));
    f.enable=5;CHECK(!r.Enable(s.target).Complete());CHECK(r.Snapshot()[0].state==State::CreatedDisabled);CHECK(r.Snapshot()[0].original==original);
    f.enable=0;CHECK(r.Enable(s.target).Complete());
    {auto guard=r.Enter(ticket);CHECK(guard.Admitted());}
    r.CloseAdmission("input");CHECK(!r.DisableOwnedTargets("input",{}).Complete());CHECK(r.Snapshot()[0].state==State::Enabled);
    RetirementProof proof{true,true,true,r.Epoch(),1};f.disable=6;
    CHECK(!r.DisableOwnedTargets("input",proof).Complete());CHECK(r.Snapshot()[0].original==original);
    f.disable=0;CHECK(r.DisableOwnedTargets("input",proof).Complete());
    {auto guard=r.Enter(ticket);CHECK(guard);CHECK(!guard.Admitted());CHECK(!r.ReclaimDrainedTargets("input",proof).Complete());}
    CHECK(!r.ReclaimDrainedTargets("input",{true,true,false,r.Epoch(),1}).Complete());
    f.remove=7;CHECK(!r.ReclaimDrainedTargets("input",proof).Complete());CHECK(r.Snapshot()[0].state==State::Reclaimable);CHECK(r.Snapshot()[0].original==original);
    f.remove=0;CHECK(!r.ReclaimDrainedTargets("input",{true,true,true,r.Epoch()-1,1}).Complete());
    CHECK(r.ReclaimDrainedTargets("input",proof).Complete());CHECK(r.Snapshot().empty());
}
void OwnershipAndForeignBytes(){
    Fake f;Registry r(f.Get());auto s=f.Spec();void* original=nullptr;CallbackTicket ticket;
    f.create=1;CHECK(!r.Acquire(s,&original).Complete());CHECK(r.Snapshot().empty());f.create=0;
    CHECK(r.Acquire(s,&original).Complete());CHECK(r.BindCallback(s.target,ticket).Complete());auto wrong=s;wrong.owner="foreign";CHECK(!r.Acquire(wrong,&original).Complete());
    wrong=s;wrong.abi="different";CHECK(!r.Acquire(wrong,&original).Complete());wrong=s;wrong.detour=reinterpret_cast<void*>(124);CHECK(!r.Acquire(wrong,&original).Complete());
    CHECK(r.Acquire(s,&original).Complete());CHECK(r.BindCallback(s.target,ticket).Complete());CHECK(r.Enable(s.target).Complete());r.CloseAdmission("input");
    f.bytes[s.target]={8,8};CHECK(!r.DisableOwnedTargets("input",{true,true,true,r.Epoch(),1}).Complete());CHECK(r.Snapshot()[0].original==original);
    Registry absent(Backend{});CHECK(!absent.Acquire(s,&original).Complete());CHECK(absent.Snapshot().empty());
}
void PartialInputCreate(){
    Fake f;Registry r(f.Get());auto first=f.Spec(16),second=f.Spec(32);void* original=nullptr;
    CHECK(r.Acquire(first,&original).Complete());f.create=4;CHECK(!r.Acquire(second,&original).Complete());
    CHECK(r.Snapshot().size()==1);CHECK(r.Snapshot()[0].original==original);f.create=0;
    CHECK(r.Acquire(first,&original).Complete());CHECK(r.Acquire(second,&original).Complete());CHECK(r.Snapshot().size()==2);
}
void QueuedPartialRetirementAndStaleReceipt(){
    Fake f;auto b=f.Get();int applied=0;
    b.queueDisable=[](void*){return 0;};
    b.applyQueued=[&](){++applied;f.bytes[reinterpret_cast<void*>(16)]={1,2};return 9;};
    b.disable=[&](void* target){if(target==reinterpret_cast<void*>(32))return 8;f.bytes[target]={1,2};return 0;};
    Registry r(b);auto a=f.Spec(16),c=f.Spec(32);void* original=nullptr;
    CHECK(r.Acquire(a,&original).Complete());CHECK(r.Acquire(c,&original).Complete());
    CallbackTicket ticket;CHECK(r.BindCallback(a.target,ticket).Complete());
    CHECK(r.Enable(a.target).Complete());CHECK(r.Enable(c.target).Complete());r.CloseAdmission("input");
    const RetirementProof proof{true,true,true,r.Epoch(),1};
    {auto guard=r.Enter(ticket);CHECK(!r.DisableOwnedTargets("input",proof).Complete());}
    CHECK(!r.DisableOwnedTargets("input",proof).Complete());CHECK(applied==1);
    auto records=r.Snapshot();CHECK(records[0].state==State::DisabledAwaitingDrain);CHECK(records[1].state==State::Enabled);
    CHECK(records[0].applyStatus==9);CHECK(records[1].disableStatus==8);
    CHECK(!r.ReclaimDrainedTargets("input",proof).Complete());CHECK(r.Snapshot().size()==1);
    CHECK(r.Enable(c.target).Complete());r.CloseAdmission("input");
    CHECK(!r.DisableOwnedTargets("input",proof).Complete());
}

void CollisionSlotsRetainFailedRemoval(){
    Fake f;Registry r(f.Get());auto a=f.Spec(16),b=f.Spec(32);
    a.owner=b.owner="[COLLISION_HOOK]";void* direct=nullptr;void* entity=nullptr;
    CHECK(r.Acquire(a,&direct).Complete());CHECK(r.Acquire(b,&entity).Complete());
    uintptr_t directTarget=16,entityTarget=32;std::atomic<bool> directCreated{true},entityCreated{true};
    CHECK(!ReleaseRetiredSlot(r,directTarget,direct,directCreated));
    CHECK(!ReleaseRetiredSlot(r,entityTarget,entity,entityCreated));
    CHECK(directTarget==16&&entityTarget==32);CHECK(direct&&entity);CHECK(directCreated&&entityCreated);
    f.remove=4;r.CloseAdmission("[COLLISION_HOOK]");
    CHECK(!r.ReclaimDrainedTargets("[COLLISION_HOOK]",{true,true,true,r.Epoch(),1}).Complete());
    CHECK(!ReleaseRetiredSlot(r,directTarget,direct,directCreated));
    CHECK(!ReleaseRetiredSlot(r,entityTarget,entity,entityCreated));
    f.remove=0;CHECK(r.ReclaimDrainedTargets("[COLLISION_HOOK]",{true,true,true,r.Epoch(),1}).Complete());
    CHECK(ReleaseRetiredSlot(r,directTarget,direct,directCreated));
    CHECK(ReleaseRetiredSlot(r,entityTarget,entity,entityCreated));
    CHECK(!directTarget&&!entityTarget&&!direct&&!entity&&!directCreated&&!entityCreated);
}

void CallbackEntryAllocatesNothing(){
    Fake f;Registry r(f.Get());auto s=f.Spec();void* original=nullptr;CallbackTicket ticket;
    CHECK(r.Acquire(s,&original).Complete());CHECK(r.BindCallback(s.target,ticket).Complete());CHECK(r.Enable(s.target).Complete());
    allocationCount=0;countAllocations=true;
    for(int i=0;i<1000;++i){auto outer=r.Enter(ticket);auto recursive=r.Enter(ticket);CHECK(outer.Admitted()&&recursive.Admitted());}
    countAllocations=false;CHECK(allocationCount==0);
}

int NativeTitleUpdate(int value){return value+7;}
void TitleOriginalAndRetryBinding(){
    Fake f;auto backend=f.Get();
    backend.create=[](void*,void*,void** out){*out=reinterpret_cast<void*>(&NativeTitleUpdate);return 0;};
    Registry r(backend);auto s=f.Spec();s.owner="PracticeMenu";
    using TitleFn=int(*)(int);TitleFn original=nullptr;
    CHECK(r.Acquire(s,reinterpret_cast<void**>(&original)).Complete());
    CHECK(r.Enable(s.target).Complete());r.CloseAdmission("PracticeMenu");
    CHECK(!r.ReclaimDrainedTargets("PracticeMenu",{}).Complete());
    CHECK(!ReleaseRetiredOriginal(r,reinterpret_cast<uintptr_t>(s.target),original));
    CHECK(original&&original(5)==12);
    CHECK(r.Acquire(s,reinterpret_cast<void**>(&original)).Complete());
    CHECK(r.Enable(s.target).Complete());CHECK(original&&original(8)==15);
}
void IndependentCollisionActivation(){
    Fake f;auto backend=f.Get();
    backend.enable=[&](void* p){if(p==reinterpret_cast<void*>(32))return 5;f.bytes[p]={9,9};return 0;};
    Registry r(backend);auto direct=f.Spec(16),entity=f.Spec(32);
    direct.owner=entity.owner="[COLLISION_HOOK]";void* original=nullptr;
    CHECK(r.Acquire(direct,&original).Complete());CHECK(r.Acquire(entity,&original).Complete());
    std::atomic<bool> directActive{false},entityActive{false};
    CHECK(PublishTargetActivation(r,16,directActive));
    CHECK(!PublishTargetActivation(r,32,entityActive));
    CHECK(directActive.load()&&!entityActive.load());
}
void DeferredOverlayRelatchRestoresAdmission(){
    Fake f;Registry r(f.Get());auto s=f.Spec();s.owner="[OVERLAY][D3D9]";void* original=nullptr;CallbackTicket ticket;
    CHECK(r.Acquire(s,&original).Complete());CHECK(r.BindCallback(s.target,ticket).Complete());
    CHECK(r.Enable(s.target).Complete());r.CloseAdmission(s.owner);
    CHECK(!r.DisableOwnedTargets(s.owner,{}).Complete());
    std::atomic<bool> active{false};
    CHECK(RestoreRetainedTargetAdmission(r,s.target,active));
    CHECK(active.load());auto guard=r.Enter(ticket);CHECK(guard.Admitted());
}

void OriginalRepairOnlyBeforeNativeAdmission(){
    Fake f;Registry r(f.Get());auto s=f.Spec();void* original=nullptr;
    CHECK(r.Acquire(s,&original).Complete());original=nullptr;
    CHECK(r.Acquire(s,&original).Complete());CHECK(original==reinterpret_cast<void*>(0x777));
    CHECK(r.Enable(s.target).Complete());r.CloseAdmission(s.owner);
    original=nullptr;
    CHECK(!r.Acquire(s,&original).Complete());CHECK(original==nullptr);
    CHECK(r.Snapshot()[0].original==reinterpret_cast<void*>(0x777));
}
void CallbackEntryDoesNotTakeLifecycleMutex(){
    Fake f;auto b=f.Get();CallbackTicket ticket;bool entered=false;
    b.enable=[&](void* p){
        countAllocations=true;allocationCount=0;
        {auto guard=ExecutionGuard::Enter(ticket);entered=static_cast<bool>(guard);}
        countAllocations=false;CHECK(allocationCount==0);f.bytes[p]={9,9};return 0;
    };
    Registry r(b);auto s=f.Spec();void* original=nullptr;
    CHECK(r.Acquire(s,&original).Complete());CHECK(r.BindCallback(s.target,ticket).Complete());
    CHECK(r.Enable(s.target).Complete());CHECK(entered);
    r.CloseAdmission(s.owner);
    countAllocations=true;allocationCount=0;
    {auto outer=r.Enter(ticket);auto nested=r.Enter(ticket);CHECK(outer&&!outer.Admitted()&&nested&&!nested.Admitted());}
    countAllocations=false;CHECK(allocationCount==0);
}

#ifdef _WIN32
void CallbackDepthPreservesLastError(){
    Fake f;Registry r(f.Get());auto s=f.Spec();void* original=nullptr;CallbackTicket ticket;
    CHECK(r.Acquire(s,&original).Complete());CHECK(r.BindCallback(s.target,ticket).Complete());
    CHECK(r.Enable(s.target).Complete());r.CloseAdmission(s.owner);
    SetLastError(0x1234);
    {
        auto outer=r.Enter(ticket);CHECK(GetLastError()==0x1234);CHECK(outer&&!outer.Admitted());
        SetLastError(0x4567);
        {auto recursive=r.Enter(ticket);CHECK(GetLastError()==0x4567);SetLastError(0x6789);}
        CHECK(GetLastError()==0x6789);
        CHECK(!r.DisableOwnedTargets(s.owner,{true,true,true,r.Epoch(),1}).Complete());
        SetLastError(0x9876);
    }
    CHECK(GetLastError()==0x9876);
    CHECK(r.DisableOwnedTargets(s.owner,{true,true,true,r.Epoch(),1}).Complete());
    CHECK(r.ReclaimDrainedTargets(s.owner,{true,true,true,r.Epoch(),1}).Complete());
}
    void ExhaustedFixedTlsSlotsFailClosed(){
    DWORD held[TLS_MINIMUM_AVAILABLE+1]{};size_t heldCount=0;
    while(heldCount<TLS_MINIMUM_AVAILABLE+1){
        const DWORD index=TlsAlloc();
        if(index==TLS_OUT_OF_INDEXES)break;
        if(index>=TLS_MINIMUM_AVAILABLE){TlsFree(index);break;}
        held[heldCount++]=index;
    }
    Fake f;Registry r(f.Get());auto s=f.Spec();void* original=nullptr;CallbackTicket ticket;
    CHECK(r.Acquire(s,&original).Complete());CHECK(r.BindCallback(s.target,ticket).Complete());
    CHECK(r.Enable(s.target).Complete());
    SetLastError(0x1357);
    {
        auto guard=r.Enter(ticket);CHECK(guard);CHECK(guard.Admitted());CHECK(GetLastError()==0x1357);
    }
    CHECK(GetLastError()==0x1357);
    r.CloseAdmission(s.owner);
    CHECK(!r.DisableOwnedTargets(s.owner,{true,true,true,r.Epoch(),1}).Complete());
    CHECK(!r.ReclaimDrainedTargets(s.owner,{true,true,true,r.Epoch(),1}).Complete());
    CHECK(r.Snapshot().size()==1);
    // Retirement stays blocked; freed OS slots are not a safe receipt.
    for(size_t i=0;i<heldCount;++i)TlsFree(held[i]);
    CHECK(!r.DisableOwnedTargets(s.owner,{true,true,true,r.Epoch(),1}).Complete());
}
void RunTlsExhaustionChild(){
    wchar_t path[MAX_PATH]{};CHECK(GetModuleFileNameW(nullptr,path,MAX_PATH)!=0);
    std::wstring command=L"\"";command+=path;command+=L"\" --tls-exhausted";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    const BOOL started=CreateProcessW(nullptr,&command[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);
    CHECK(started!=FALSE);if(!started)return;
    CHECK(WaitForSingleObject(process.hProcess,15000)==WAIT_OBJECT_0);
    DWORD result=1;CHECK(GetExitCodeProcess(process.hProcess,&result)!=FALSE);CHECK(result==0);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
}
#endif

}
void PeerRestoredEntryReinstallsOnEnable(){
    Fake f;Registry r(f.Get());auto s=f.Spec();s.owner="[OVERLAY][D3D9]";void* original=nullptr;CallbackTicket ticket;
    CHECK(r.Acquire(s,&original).Complete());CHECK(r.BindCallback(s.target,ticket).Complete());
    CHECK(r.Image(s.target)==EntryImage::Preimage);
    CHECK(r.Enable(s.target).Complete());CHECK(r.Image(s.target)==EntryImage::Installed);r.CloseAdmission(s.owner);
    // Peer restored the preimage over our JMP.
    f.bytes[s.target]={1,2};CHECK(r.Image(s.target)==EntryImage::Preimage);
    const uint64_t epoch=r.Epoch();
    CHECK(r.Enable(s.target).Complete());
    CHECK(f.bytes[s.target]==(std::vector<unsigned char>{9,9}));CHECK(r.Snapshot()[0].installed==(std::vector<unsigned char>{9,9}));
    CHECK(r.Snapshot()[0].state==State::Enabled);CHECK(r.Epoch()>epoch);CHECK(r.Image(s.target)==EntryImage::Installed);
    {auto guard=r.Enter(ticket);CHECK(guard.Admitted());}
    // Peer layered over our live JMP.
    r.CloseAdmission(s.owner);f.bytes[s.target]={7,7};CHECK(r.Image(s.target)==EntryImage::Foreign);
    CHECK(r.Enable(s.target).Complete());CHECK(f.bytes[s.target]==(std::vector<unsigned char>{7,7}));
    CHECK(r.Snapshot()[0].installed==(std::vector<unsigned char>{9,9}));
    {auto guard=r.Enter(ticket);CHECK(guard.Admitted());}
    r.CloseAdmission(s.owner);f.bytes[s.target]={1,2};f.enable=5;
    CHECK(!r.Enable(s.target).Complete());{auto guard=r.Enter(ticket);CHECK(!guard.Admitted());}
    CHECK(r.Snapshot()[0].installed==(std::vector<unsigned char>{9,9}));
    f.enable=0;CHECK(r.Enable(s.target).Complete());CHECK(f.bytes[s.target]==(std::vector<unsigned char>{9,9}));
    {auto guard=r.Enter(ticket);CHECK(guard.Admitted());}
    r.CloseAdmission(s.owner);f.bytes[s.target]={7,7};
    CHECK(!r.DisableOwnedTargets(s.owner,{true,true,true,r.Epoch(),1}).Complete());CHECK(r.Snapshot()[0].state==State::Enabled);
}
int main(int argc,char** argv){
#ifdef _WIN32
    if(argc==2&&std::string(argv[1])=="--tls-exhausted") {ExhaustedFixedTlsSlotsFailClosed();return failures?1:0;}
    RunTlsExhaustionChild();CallbackDepthPreservesLastError();
#endif
    FailureRetention();OwnershipAndForeignBytes();PartialInputCreate();QueuedPartialRetirementAndStaleReceipt();CollisionSlotsRetainFailedRemoval();CallbackEntryAllocatesNothing();TitleOriginalAndRetryBinding();IndependentCollisionActivation();DeferredOverlayRelatchRestoresAdmission();OriginalRepairOnlyBeforeNativeAdmission();PeerRestoredEntryReinstallsOnEnable();CallbackEntryDoesNotTakeLifecycleMutex();return failures?1:0;}

