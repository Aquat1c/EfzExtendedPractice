#include "netplay/bridge/practice_runtime_bridge.h"
#include "netplay/bridge/takeover_internal.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace netplay { namespace bridge { namespace takeover {
const RevivalAddressProfile* g_activeRevival=nullptr;
HMODULE g_localRevivalModule=nullptr;
int g_localRoleFlag=kLocalRoleLocalPlay;
uint32_t RunPracticeFrontendMutation(const EfzTmEntryV1&,uint64_t,uint32_t (__cdecl*)(const EfzTmEntryV1*,uint64_t)){return EFZ_TM_UNSUPPORTED;}
}}}
namespace {bool failAllocation=false,allocationFailed=false;size_t allocationCalls=0,failAt=0;}
void* operator new(size_t bytes) {
    ++allocationCalls;
    if(failAllocation || (failAt && allocationCalls==failAt)){failAllocation=false;allocationFailed=true;throw std::bad_alloc();}
    if(void* p=std::malloc(bytes?bytes:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}

extern "C" uint32_t __cdecl TestBegin(const EfzTmEntryV1*){return EFZ_TM_READY;}
extern "C" uint32_t __cdecl TestAttach(const EfzTmEntryV1*){return EFZ_TM_PENDING;}
extern "C" uint32_t __cdecl TestRetire(const EfzTmRetireV1*){return EFZ_TM_PENDING;}
extern "C" uint32_t __cdecl TestAdvance(const EfzTmRetireV1*){return EFZ_TM_PENDING;}
extern "C" uint32_t __cdecl TestStatus(EfzTmStatusV1*){return EFZ_TM_READY;}
extern "C" uint32_t __cdecl TestBeginLoading(const EfzTmEntryV1*,uint32_t,uint32_t*){return EFZ_TM_UNSUPPORTED;}
extern "C" uint32_t __cdecl TestEndLoading(const EfzTmEntryV1*,uint32_t,uint32_t,uint32_t,uint32_t){return EFZ_TM_UNSUPPORTED;}
// Expose only decorated lifecycle names. The real production Resolve must
// allocate its fallback string; GetProcAddress and module pinning are real.
#pragma comment(linker,"/EXPORT:__EFZ_TM_BeginPracticeV1=_TestBegin")
#pragma comment(linker,"/EXPORT:__EFZ_TM_AttachBattleV1=_TestAttach")
#pragma comment(linker,"/EXPORT:__EFZ_TM_BeginRetireV1=_TestRetire")
#pragma comment(linker,"/EXPORT:__EFZ_TM_AdvanceRetireV1=_TestAdvance")
#pragma comment(linker,"/EXPORT:__EFZ_TM_GetStatusV1=_TestStatus")
#pragma comment(linker,"/EXPORT:__EFZ_TM_BeginLoadingV1=_TestBeginLoading")
#pragma comment(linker,"/EXPORT:__EFZ_TM_EndLoadingV1=_TestEndLoading")
extern "C" uint32_t __cdecl EFZ_Netplay_RegisterPracticeRuntimeV1(uint32_t module);

int main(){
    int failures=0;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(false)
    const auto module=GetModuleHandleW(nullptr);
    CHECK(GetProcAddress(module,"EFZ_TM_BeginPracticeV1")==nullptr);
    CHECK(GetProcAddress(module,"_EFZ_TM_BeginPracticeV1")!=nullptr);
    failAllocation=true;
    const auto failed=EFZ_Netplay_RegisterPracticeRuntimeV1(reinterpret_cast<uintptr_t>(module));
    failAllocation=false;
    CHECK(allocationFailed&&failed==EFZ_TM_FAULTED);
    allocationFailed=false;failAllocation=true;
    const auto failedAgain=EFZ_Netplay_RegisterPracticeRuntimeV1(reinterpret_cast<uintptr_t>(module));
    failAllocation=false;
    CHECK(allocationFailed&&failedAgain==EFZ_TM_FAULTED);
    // Allocations 6 and 7 are the Loading fallbacks; both must fail before pins are acquired or published.
    for(size_t boundary=6;boundary<=7;++boundary) {
        allocationCalls=0;allocationFailed=false;failAt=boundary;
        const auto rejected=EFZ_Netplay_RegisterPracticeRuntimeV1(reinterpret_cast<uintptr_t>(module));
        failAt=0;
        CHECK(rejected==EFZ_TM_FAULTED&&allocationFailed);
    }
    // Allocation 9 is never reached: the disabled native-continuation exports are not resolved.
    allocationCalls=0;allocationFailed=false;failAt=9;
    const auto completed=EFZ_Netplay_RegisterPracticeRuntimeV1(reinterpret_cast<uintptr_t>(module));
    failAt=0;
    CHECK(completed==EFZ_TM_READY&&!allocationFailed&&allocationCalls==8);
    CHECK(EFZ_Netplay_RegisterPracticeRuntimeV1(reinterpret_cast<uintptr_t>(module))==EFZ_TM_READY);
    CHECK(EFZ_Netplay_RegisterPracticeRuntimeV1(reinterpret_cast<uintptr_t>(module))==EFZ_TM_READY);
    CHECK(EFZ_Netplay_RegisterPracticeRuntimeV1(reinterpret_cast<uintptr_t>(GetModuleHandleW(L"kernel32.dll")))==EFZ_TM_UNSUPPORTED);
    return failures?1:0;
}
