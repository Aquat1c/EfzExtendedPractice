#include "runtime/hook_registry.h"
#include "runtime/patch_ledger.h"
#include <array>
#include <cstdio>
#include <cstring>

using namespace PracticeHooks;
namespace {
int failures=0;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(false)
struct Memory final : Practice::PatchBackend {
    std::array<unsigned char,16> bytes{};
    uint32_t protection=0x20;
    bool failFlush=false;
    int writes=0, pins=0;
    bool Pin(const Practice::PatchModule&,uintptr_t& p) noexcept override {p=1;++pins;return true;}
    void Unpin(uintptr_t) noexcept override {--pins;}
    bool SameModule(const Practice::PatchModule&,uintptr_t) noexcept override {return true;}
    bool Read(uintptr_t p,void* out,size_t n) noexcept override {
        if(p<0x1000||p+n>0x1010)return false;
        std::memcpy(out,bytes.data()+p-0x1000,n);return true;
    }
    size_t Write(uintptr_t p,const void* in,size_t n) noexcept override {
        ++writes;std::memcpy(bytes.data()+p-0x1000,in,n);return n;
    }
    bool QueryRegion(uintptr_t,Practice::PatchRegion& r) noexcept override {r={0x1000,16,protection,0x1000};return true;}
    bool Protect(const Practice::PatchRegion&,uint32_t p,uint32_t& old) noexcept override {old=protection;protection=p;return true;}
    bool Flush(uintptr_t,size_t) noexcept override {return !failFlush;}
    Backend Hooks() {
        Backend b;
        b.read=[this](uintptr_t p,size_t n,std::vector<unsigned char>& out){out.resize(n);return Read(p,out.data(),n);};
        // External JMPs must never enter MinHook, including cleanup.
        b.create=[](void*,void*,void**){CHECK(false);return 99;};
        b.enable=b.disable=b.remove=[](void*){CHECK(false);return 99;};
        return b;
    }
};
TargetSpec Spec() {
    TargetSpec s;s.target=reinterpret_cast<void*>(0x1000);s.detour=reinterpret_cast<void*>(0x3000);
    s.owner="PracticeMenu";s.abi="title case JMP";s.moduleIncarnation=7;s.rangeStart=0x1000;
    s.preimage={0,0,0,0,0};s.modulePin=std::make_shared<int>(1);return s;
}
Practice::PatchDescriptor Patch() {return {{0x1000,7},0x1000,{0,0,0,0,0},{0xe9,1,2,3,4},19};}
void ExternalSiteRetainsExecutionAfterByteRestoration() {
    Memory memory;Practice::PatchLedger ledger(memory);Registry registry(memory.Hooks());
    auto spec=Spec();auto codePin=std::make_shared<int>(2);std::weak_ptr<int> retained=codePin;CallbackTicket ticket;
    CHECK(registry.PrepareExternal(spec,Patch(),ledger,ticket,codePin).Complete());
    codePin.reset();CHECK(!retained.expired());CHECK(memory.writes==0);
    CHECK(registry.Enable(spec.target).Complete());CHECK(memory.bytes[0]==0xe9);
    {auto guard=registry.Enter(ticket);CHECK(guard.Admitted());}
    registry.CloseAdmission(spec.owner);
    CHECK(!registry.DisableOwnedTargets(spec.owner,{}).Complete());CHECK(memory.bytes[0]==0xe9);
    RetirementProof proof{true,true,true,registry.Epoch(),5};
    {auto guard=registry.Enter(ticket);CHECK(!registry.DisableOwnedTargets(spec.owner,proof).Complete());}
    CHECK(registry.DisableOwnedTargets(spec.owner,proof).Complete());CHECK(memory.bytes[0]==0);
    CHECK(!retained.expired());CHECK(ledger.Obligations()==0);
    CHECK(!registry.ReclaimDrainedTargets(spec.owner,{true,false,true,registry.Epoch(),5}).Complete());
    CHECK(!registry.ReclaimDrainedTargets(spec.owner,{true,true,false,registry.Epoch(),5}).Complete());
    {auto guard=registry.Enter(ticket);CHECK(guard&&!guard.Admitted());CHECK(!registry.ReclaimDrainedTargets(spec.owner,proof).Complete());}
    CHECK(registry.ReclaimDrainedTargets(spec.owner,proof).Complete());CHECK(retained.expired());CHECK(ticket.state.load()==nullptr);
}
void PartialInstallCannotMasqueradeAsDisabled() {
    Memory memory;Practice::PatchLedger ledger(memory);Registry registry(memory.Hooks());CallbackTicket ticket;auto spec=Spec();
    CHECK(registry.PrepareExternal(spec,Patch(),ledger,ticket,std::make_shared<int>(2)).Complete());
    memory.failFlush=true;CHECK(!registry.Enable(spec.target).Complete());CHECK(ledger.Obligations()==1);
    registry.CloseAdmission(spec.owner);RetirementProof proof{true,true,true,registry.Epoch(),5};
    CHECK(!registry.ReclaimDrainedTargets(spec.owner,proof).Complete());CHECK(registry.Snapshot().size()==1);
    CHECK(!registry.DisableOwnedTargets(spec.owner,proof).Complete());
    memory.failFlush=false;CHECK(registry.DisableOwnedTargets(spec.owner,proof).Complete());
    CHECK(registry.ReclaimDrainedTargets(spec.owner,proof).Complete());CHECK(ledger.Obligations()==0);
}
void DescriptorAndOverlapAreBoundBeforeMutation() {
    Memory memory;Practice::PatchLedger ledger(memory);Registry registry(memory.Hooks());CallbackTicket ticket;auto spec=Spec();
    auto patch=Patch();patch.address++;
    CHECK(!registry.PrepareExternal(spec,patch,ledger,ticket,std::make_shared<int>(2)).Complete());CHECK(memory.writes==0);
    CHECK(registry.PrepareExternal(spec,Patch(),ledger,ticket,std::make_shared<int>(2)).Complete());
    void* original=nullptr;CHECK(!registry.Acquire(spec,&original).Complete());
    auto overlap=spec;overlap.target=reinterpret_cast<void*>(0x1002);overlap.rangeStart=0x1002;
    CHECK(!registry.Acquire(overlap,&original).Complete());CHECK(memory.writes==0);
}
void ForeignBytesDoNotLoseCodeOrLedgerObligations() {
    Memory memory;Practice::PatchLedger ledger(memory);Registry registry(memory.Hooks());CallbackTicket ticket;auto spec=Spec();
    CHECK(registry.PrepareExternal(spec,Patch(),ledger,ticket,std::make_shared<int>(2)).Complete());
    CHECK(registry.Enable(spec.target).Complete());registry.CloseAdmission(spec.owner);
    RetirementProof proof{true,true,true,registry.Epoch(),5};
    memory.bytes[1]=0xff;const int writes=memory.writes;
    CHECK(!registry.DisableOwnedTargets(spec.owner,proof).Complete());
    CHECK(memory.writes==writes);CHECK(ledger.Obligations()==1);CHECK(registry.Snapshot().size()==1);
    CHECK(!registry.ReclaimDrainedTargets(spec.owner,proof).Complete());
    memory.bytes[1]=1;
    CHECK(registry.DisableOwnedTargets(spec.owner,proof).Complete());
    CHECK(registry.ReclaimDrainedTargets(spec.owner,proof).Complete());
}
}
int main(){ExternalSiteRetainsExecutionAfterByteRestoration();PartialInstallCannotMasqueradeAsDisabled();DescriptorAndOverlapAreBoundBeforeMutation();ForeignBytesDoNotLoseCodeOrLedgerObligations();return failures?1:0;}
