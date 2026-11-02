#include "netplay/bridge/practice_runtime_bridge.h"
#include <cstdio>
#include <initializer_list>
using namespace netplay::bridge::practice;
namespace {
int failures=0,retireCalls=0,resumeCalls=0,commitCalls=0;
bool commitAllowed=true;
uint32_t retirementResult=EFZ_TM_PENDING;
EfzTmRetireV1 retired{};
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(false)
uint32_t __cdecl Begin(const EfzTmEntryV1*){return EFZ_TM_READY;}
uint32_t __cdecl Attach(const EfzTmEntryV1*){return EFZ_TM_READY;}
uint32_t __cdecl Retire(const EfzTmRetireV1* r){++retireCalls;retired=*r;return EFZ_TM_PENDING;}
uint32_t __cdecl Advance(const EfzTmRetireV1* r){retired=*r;return r->oldWorldHeld?retirementResult:EFZ_TM_FAULTED;}
uint32_t __cdecl Status(EfzTmStatusV1*){return EFZ_TM_READY;}
uint32_t __cdecl ResumeA(uint32_t battle){CHECK(battle==0x123400);++resumeCalls;return 2;}
uint32_t __cdecl ResumeB(uint32_t battle){CHECK(battle==0x123400);++resumeCalls;return 5;}
bool __cdecl Commit(uint32_t battle,unsigned char result){CHECK(battle==0x123400);CHECK(result==5||result==2||result==8);++commitCalls;return commitAllowed;}
void Enter(Owner& owner){
    CHECK(owner.Register({Begin,Attach,Retire,Advance,Status},19));
    TitleSource source{};source.titleContext=0x10000;source.sourceScreen=0;source.sourceSelection=3;
    source.attributedLocalOwner=true;source.entry.gameMode=1;source.entry.screen=1;
    CHECK(owner.CommittedTitleReturn(source,1)==EFZ_TM_READY);
    auto battle=owner.Current();battle.screen=3;battle.battleContext=0x123400;battle.player1=0x120000;battle.player2=0x130000;
    CHECK(owner.CompletedBattleInitialization(battle)==EFZ_TM_READY);
}
void PendingHoldDoesNotReenterNativeAndConsumesResultOnce(){
    Owner owner;Enter(owner);
    CHECK(owner.HoldBattleCleanup(0x123400,0,EFZ_TM_RELOAD_WORLD)==EFZ_TM_PENDING);
    CHECK(owner.Pending());CHECK(retireCalls==1&&retired.oldWorldHeld==1);
    CHECK(owner.HoldBattleCleanup(0x123400,0,EFZ_TM_RELOAD_WORLD)==EFZ_TM_PENDING);CHECK(retireCalls==1);
    CHECK(owner.HoldBattleCleanup(0x999999,0,EFZ_TM_RELOAD_WORLD)==EFZ_TM_STALE);
    auto result=owner.AdvanceBattleCleanup(ResumeA,ResumeB,Commit);CHECK(result.result==EFZ_TM_PENDING&&!result.nativeReturned&&resumeCalls==0);
    retirementResult=EFZ_TM_READY;result=owner.AdvanceBattleCleanup(ResumeA,ResumeB,Commit);
    CHECK(result.result==EFZ_TM_READY&&result.nativeReturned&&result.nativeResult==2&&resumeCalls==1);
    CHECK(commitCalls==1);CHECK(!owner.Pending());CHECK(owner.Current().id.practiceSession==1&&owner.Current().id.battleWorld==0);
    result=owner.AdvanceBattleCleanup(ResumeA,ResumeB,Commit);CHECK(!result.nativeReturned&&resumeCalls==1);
}
void NativeEscapeCannotForgeNormalReturnOrRetryTheTail(){
    Owner owner;Enter(owner);retirementResult=EFZ_TM_PENDING;
    CHECK(owner.HoldBattleCleanup(0x123400,0,EFZ_TM_LEAVE_PRACTICE)==EFZ_TM_PENDING);
    owner.NativeEscape();CHECK(retired.oldWorldHeld==0);
    const int before=resumeCalls;retirementResult=EFZ_TM_READY;
    const auto result=owner.AdvanceBattleCleanup(ResumeA,ResumeB,Commit);
    CHECK(result.result==EFZ_TM_FAULTED&&!result.nativeReturned&&resumeCalls==before&&owner.Pending());
}
uint32_t __cdecl NativeMenuReturn(uint32_t){return 8;}
uint32_t __cdecl FailedResume(uint32_t){return 0xffffffffu;}
void UnexpectedNativeDestinationCannotRetainFrontendOwnership(){
    Owner owner;Enter(owner);retirementResult=EFZ_TM_READY;
    CHECK(owner.HoldBattleCleanup(0x123400,0,EFZ_TM_RELOAD_WORLD)==EFZ_TM_PENDING);
    const auto result=owner.AdvanceBattleCleanup(NativeMenuReturn,ResumeB,Commit);
    CHECK(result.nativeReturned&&result.nativeResult==8);
    CHECK(owner.Current().id.practiceSession==0);
}
void ResumeFailureCannotBecomeANativeScreenByte(){
    Owner owner;Enter(owner);retirementResult=EFZ_TM_READY;
    CHECK(owner.HoldBattleCleanup(0x123400,0,EFZ_TM_RELOAD_WORLD)==EFZ_TM_PENDING);
    const int before=commitCalls;
    const auto result=owner.AdvanceBattleCleanup(FailedResume,ResumeB,Commit);
    CHECK(result.result==EFZ_TM_FAULTED&&!result.nativeReturned&&commitCalls==before);
    CHECK(owner.Pending()&&retired.oldWorldHeld==0);
}

void AdvanceErrorsRemainLatchedAcrossLaterReady(){
    for(const uint32_t error:{EFZ_TM_FAULTED,EFZ_TM_STALE,EFZ_TM_UNSUPPORTED}) {
        Owner owner;Enter(owner);
        CHECK(owner.HoldBattleCleanup(0x123400,0,EFZ_TM_RELOAD_WORLD)==EFZ_TM_PENDING);
        const int beforeResume=resumeCalls,beforeCommit=commitCalls;
        retirementResult=error;
        const auto failed=owner.AdvanceBattleCleanup(ResumeA,ResumeB,Commit);
        CHECK(failed.result==error&&!failed.nativeReturned);
        retirementResult=EFZ_TM_READY;
        const auto retried=owner.AdvanceBattleCleanup(ResumeA,ResumeB,Commit);
        CHECK(retried.result==EFZ_TM_FAULTED&&!retried.nativeReturned);
        CHECK(owner.Pending()&&resumeCalls==beforeResume&&commitCalls==beforeCommit);
    }
}

}
int main(){
    volatile uint8_t screen=3;volatile uint32_t context=0x123400;
    CHECK(CommitBattleScreen(0x123400,2,&screen,&context)&&screen==2);
    CHECK(!CommitBattleScreen(0x123400,0,&screen,&context)&&screen==2);
    screen=3;context=0x999999;
    CHECK(!CommitBattleScreen(0x123400,1,&screen,&context)&&screen==3);

PendingHoldDoesNotReenterNativeAndConsumesResultOnce();NativeEscapeCannotForgeNormalReturnOrRetryTheTail();UnexpectedNativeDestinationCannotRetainFrontendOwnership();ResumeFailureCannotBecomeANativeScreenByte();AdvanceErrorsRemainLatchedAcrossLaterReady();return failures?1:0;}
