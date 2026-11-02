#include "runtime/practice_frontend_case.h"
#include <cstdio>
#include <cstring>
#include <array>

namespace {
int failures=0,entries=0;
uint32_t received=0;
unsigned char nativeResult=0;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(false)
void __cdecl Enter(uint32_t context){++entries;received=context;}
__declspec(naked) void Epilogue(){__asm {mov esp,ebp
    pop ebp
    ret}}
// At native 776359, AL is the result of cmp [ebp-4],1 / sete al.
// Return 8 or 9 here to witness exact replay, not a fabricated Boolean return.
__declspec(naked) void Continuation(){__asm {add al,8
    mov esp,ebp
    pop ebp
    ret}}
__declspec(naked) unsigned char __cdecl InvokeCase(uint32_t context,uint32_t selected) {
    __asm {
        push ebp
        mov ebp,esp
        sub esp,0ch
        mov eax,[ebp+8]
        mov [ebp-8],eax
        mov eax,[ebp+0ch]
        mov [ebp-4],eax
        jmp PracticeTitleCaseAdapter
    }
}
__declspec(naked) int __cdecl CheckedCase(uint32_t context,uint32_t selected) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        mov ebx,012345678h
        mov esi,023456789h
        mov edi,03456789ah
        mov eax,esp
        push eax
        push [ebp+0ch]
        push [ebp+8]
        call InvokeCase
        add esp,8
        mov nativeResult,al
        pop edx
        cmp esp,edx
        jne failed
        cmp ebx,012345678h
        jne failed
        cmp esi,023456789h
        jne failed
        cmp edi,03456789ah
        jne failed
        mov eax,1
        jmp done
    failed:
        xor eax,eax
    done:
        lea esp,[ebp-0ch]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
void AdapterConsumesOrForwardsExactlyOnce(){
    auto& binding=Practice::TitleCaseBinding();PracticeHooks::CallbackState state;
    binding.enter=Enter;binding.epilogue=reinterpret_cast<uintptr_t>(&Epilogue);
    binding.continuation=reinterpret_cast<uintptr_t>(&Continuation);
    binding.ticket.state.store(&state);
    PracticeHooks::Registry registry(PracticeHooks::Backend{});
    state.admitted.store(true);
    CHECK(CheckedCase(0x12345678,1)==1);CHECK(nativeResult==0);CHECK(entries==1&&received==0x12345678);
    CHECK(state.outstanding.load()==0);
    state.admitted.store(false);
    CHECK(CheckedCase(0x87654321,1)==1);CHECK(nativeResult==9);CHECK(entries==1);
    CHECK(CheckedCase(0x87654321,2)==1);CHECK(nativeResult==8);CHECK(entries==1);
    binding.ticket.state.store(nullptr);
}
__declspec(naked) int __cdecl InvokeSelector(uint32_t context) {
    __asm {
        push ebp
        mov ebp,esp
        sub esp,74h
        mov eax,[ebp+8]
        mov [ebp-4ch],eax
        jmp PracticeSelectorReadyAdapter
    }
}
void SelectorJoinReplaysAfterAttributedCallback(){
    auto& binding=Practice::SelectorBoundary();PracticeHooks::CallbackState state;
    binding.enter=Enter;binding.continuation=reinterpret_cast<uintptr_t>(&Epilogue);binding.ticket.state.store(&state);
    unsigned char context[48]{};context[45]=0xff;const auto address=reinterpret_cast<uintptr_t>(context);
    const int before=entries;state.admitted.store(true);
    CHECK(InvokeSelector(address)==-1);CHECK(entries==before+1&&received==address);
    state.admitted.store(false);CHECK(InvokeSelector(address)==-1&&entries==before+1);
    CHECK(state.outstanding.load()==0);binding.ticket.state.store(nullptr);
}
struct CaseMemory : Practice::PatchBackend {
    std::array<unsigned char,7> bytes{{0x83,0x7d,0xfc,1,0x0f,0x94,0xc0}};
    int mutations=0;uint32_t protection=0x20;
    bool Pin(const Practice::PatchModule&,uintptr_t& pin) noexcept override{pin=1;return true;}
    void Unpin(uintptr_t) noexcept override{}
    bool SameModule(const Practice::PatchModule&,uintptr_t) noexcept override{return true;}
    bool Read(uintptr_t p,void* out,size_t n) noexcept override{if(p!=0x776352||n!=7)return false;std::memcpy(out,bytes.data(),n);return true;}
    size_t Write(uintptr_t,const void* in,size_t n) noexcept override{++mutations;std::memcpy(bytes.data(),in,n);return n;}
    bool QueryRegion(uintptr_t,Practice::PatchRegion& region) noexcept override{region={0x776000,4096,protection,0x400000};return true;}
    bool Protect(const Practice::PatchRegion&,uint32_t desired,uint32_t& old) noexcept override{old=protection;protection=desired;return true;}
    bool Flush(uintptr_t,size_t) noexcept override{return true;}
};
void RetriedInstallerCannotEnableAReclaimedAddressNewOwner(){
    CaseMemory memory;Practice::PatchLedger ledger(memory);PracticeHooks::Backend backend;
    backend.read=[&](uintptr_t p,size_t n,std::vector<unsigned char>& out){out.resize(n);return memory.Read(p,out.data(),n);};
    backend.create=[](void*,void*,void** original){*original=reinterpret_cast<void*>(0x1234);return 0;};
    backend.enable=[&](void*){++memory.mutations;return 0;};
    PracticeHooks::Registry registry(backend);
    PracticeHooks::TargetSpec spec;spec.target=reinterpret_cast<void*>(0x776352);spec.detour=reinterpret_cast<void*>(&PracticeTitleCaseAdapter);
    spec.owner="PracticeMenu";spec.abi="case";spec.rangeStart=0x776352;spec.moduleIncarnation=7;
    spec.preimage.assign(memory.bytes.begin(),memory.bytes.end());spec.modulePin=std::make_shared<int>(1);
    Practice::PatchDescriptor patch{{0x400000,7},spec.rangeStart,spec.preimage,{0xe9,1,2,3,4,0x90,0x90},19};
    CHECK(Practice::BindTitleCaseAdapter(registry,spec,patch,ledger,std::make_shared<int>(2),Enter));
    registry.CloseAdmission(spec.owner);PracticeHooks::RetirementProof proof{true,true,true,registry.Epoch(),8};
    CHECK(registry.DisableOwnedTargets(spec.owner,proof).Complete());
    CHECK(registry.ReclaimDrainedTargets(spec.owner,proof).Complete());
    CHECK(Practice::TitleCaseBinding().ticket.state.load()==nullptr);
    auto foreign=spec;foreign.owner="different owner";foreign.detour=reinterpret_cast<void*>(0x9999);void* original=nullptr;
    CHECK(registry.Acquire(foreign,&original).Complete());const int mutations=memory.mutations;
    CHECK(!Practice::BindTitleCaseAdapter(registry,spec,patch,ledger,std::make_shared<int>(2),Enter));
    CHECK(memory.mutations==mutations);CHECK(!registry.Snapshot()[0].admissionOpen);
}
}
int main(){AdapterConsumesOrForwardsExactlyOnce();SelectorJoinReplaysAfterAttributedCallback();RetriedInstallerCannotEnableAReclaimedAddressNewOwner();return failures?1:0;}
