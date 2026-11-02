#include "utils/audio_runtime_state.h"
#include "utils/audio_owner_provider.h"
#include "utils/audio_callback_dispatch.inl"
#include <atomic>
#include <thread>
#include <cstdio>
using namespace AudioControl;
namespace {
int failures;
constexpr int kSoundManagerBufferCount=150;
int memoryReads=0, fileCalls=0;
namespace ExtendedConfigBridge {
bool IsSharedAudioActive() { ++fileCalls; return true; }
bool Refresh(bool) { ++fileCalls; return true; }
}
bool memoryReady=true, bgmLane=true;
bool IsSoundBufferReadyForOps(uintptr_t,unsigned short,bool,bool) { ++memoryReads; return memoryReady; }
bool IsCurrentBgmBuffer(void*,unsigned short,uintptr_t&) { ++memoryReads; return bgmLane; }
#include "utils/audio_lane_adjustment.inl"
int NativeLaneCallback(void* manager,unsigned short index,int volume) {
    EFZ_AUDIO_CALLBACK(AdjustVolumeForBuffer(manager,index,volume,ReadAudioSettings()));
}
int FaultCallbackBody() { RaiseException(0xe0000041,0,0,nullptr); return 0; }
int FaultCallback() { EFZ_AUDIO_CALLBACK(FaultCallbackBody()); }
bool CatchNativeFault() {
    __try { FaultCallback(); return false; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
}
void Check(bool ok, const char* label) { if (!ok) { ++failures; std::printf("FAIL %s\n",label); } }
EfzAudioOwnerProvider provider(0x123456789ULL);
void __cdecl Enter() { provider.Enter(); }
void __cdecl Leave() { provider.Leave(); }
bool __cdecl Query(EfzAudioOwnerV1* r) { return provider.Query(r); }
bool __cdecl Retire(const EfzAudioOwnerV1* r,uint32_t l,EfzAudioOwnerCommitV1 c,void* x) { return provider.Retire(r,l,c,x); }
const EfzAudioOwnerApiV1 api{sizeof(api),1,0x123456789ULL,Enter,Leave,Query,Retire};
void __cdecl Commit(const EfzAudioOwnerV1* record, void* context) { *static_cast<bool*>(context) = AcknowledgeAudioOwner(*record); }
}
int main() {
    AudioSettingsView v{50,50,true,true,0};
    Check(ApplyAudioLaneGain(v,true,-604)-ApplyAudioLaneGain(v,true,-605)==1,"50% one-unit raw change");
    Check(ApplyAudioLaneGain(v,false,-500)==-1102,"native SE base retained");
    v.trainingOwnsBgmGain=false;
    Check(ApplyAudioLaneGain(v,true,-605)==-605,"external input forwarded unchanged");
    PublishAudioPercents(-30,999);
    auto view=ReadAudioSettings();
    Check(view.bgmPercent==0 && view.sePercent==100,"publication clamps signed inputs");
    SetLastError(0x1234); EnterAudioBoundary(); LeaveAudioBoundary();
    Check(GetLastError()==0x1234,"local boundary preserves native last error");
    provider.ActivateAtInitialization(EfzAudioLaneBgm);
    BindAudioBoundary(&api);
    SetLastError(0x5678); Enter(); Leave();
    Check(GetLastError()==0x5678,"provider boundary preserves native last error");
    EfzAudioOwnerV1 record{};
    Check(Query(&record),"real provider registration");
    Enter(); Check(AcknowledgeAudioOwner(record),"acknowledge actual external owner"); Leave();
    PublishAudioSettingsView({50,25,true,false,0});
    view=ReadAudioSettings();
    Check(!view.trainingOwnsBgmGain && view.trainingOwnsSeGain,"caller cannot forge ownership flags");
    auto bad=record; bad.ownedLaneMask=0; bad.inputsAreRawLaneMask=3;
    Enter(); Check(!AcknowledgeAudioOwner(bad),"same generation cannot change owner"); Leave();
    Check(!ReadAudioSettings().trainingOwnsBgmGain,"pending transfer retains last owner");
    bool committed=false;
    Enter(); Check(!Retire(&record,1,Commit,&committed),"self-drain rejected"); Leave();
    std::atomic<bool> entered{false}, release{false}, transferStarted{false}, finished{false};
    std::thread callback([&] { Enter(); entered=true; while(!release.load()) SwitchToThread(); Leave(); });
    while(!entered.load()) SwitchToThread();
    std::thread transfer([&] { transferStarted=true; Retire(&record,1,Commit,&committed); finished=true; });
    while(!transferStarted.load()) SwitchToThread();
    Check(!finished.load() && !ReadAudioSettings().trainingOwnsBgmGain,"active transform retains ownership until drained");
    release=true; callback.join(); transfer.join();
    Check(committed && ReadAudioSettings().trainingOwnsBgmGain,"ack publishes new owner before unlock");
    Check(!Retire(&record,1,Commit,&committed),"stale exact generation rejected");
    provider.ActivateAtInitialization(3);
    EfzAudioOwnerV1 after{}; Query(&after);
    Check(after.ownedLaneMask==0,"late initialization cannot reactivate retired transform");
    std::atomic<bool> done{false}, torn{false};
    PublishAudioPercents(20,80);
    std::thread writer([&] { for(int i=0;i<100000;++i) PublishAudioPercents(i&1?20:80,i&1?80:20); done=true; });
    while(!done.load()) { auto s=ReadAudioSettings(); if(s.bgmPercent+s.sePercent!=100 || !s.trainingOwnsBgmGain || !s.trainingOwnsSeGain) torn=true; }
    writer.join(); Check(!torn.load(),"concurrent publications never tear complete tuple");
    Check(ConsumeAudioApplyRequest() && !ConsumeAudioApplyRequest(),"requests coalesce through publication wrap");
    ExtendedConfigBridge::IsSharedAudioActive();
    Check(fileCalls==1,"file/discovery spy positive control"); fileCalls=0;
    PublishAudioPercents(50,50);
    Check(NativeLaneCallback(reinterpret_cast<void*>(1),1,-604)-NativeLaneCallback(reinterpret_cast<void*>(1),1,-605)==1,"production classifier/scoped callback continuity");
    Check(fileCalls==0 && memoryReads==4,"production callback uses memory boundary and no file/discovery dependencies");
    memoryReady=false; memoryReads=0;
    Check(NativeLaneCallback(reinterpret_cast<void*>(1),1,-605)==-605 && memoryReads==1,"unavailable memory forwards unchanged");
    Check(NativeLaneCallback(nullptr,1,-605)==-605 && memoryReads==1,"invalid manager touches no memory");
    Check(CatchNativeFault(),"native callback fault caught outside scope");
    std::atomic<bool> afterFault{false};
    std::thread faultFollower([&] { Enter(); afterFault=true; Leave(); });
    faultFollower.join(); Check(afterFault,"SEH finally releases callback boundary");
    MarkIncompatibleAudioOwner(EfzAudioLaneSe);
    view=ReadAudioSettings();
    Check(view.trainingOwnsBgmGain && !view.trainingOwnsSeGain,"incompatible disables affected lane only");
    BindAudioBoundary(nullptr);
    std::printf("audio runtime state: %d failures\n",failures);
    return failures?1:0;
}
