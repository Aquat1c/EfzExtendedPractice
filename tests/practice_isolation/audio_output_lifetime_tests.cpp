#include "utils/audio_output_lifetime.h"
#include <cstdio>
using namespace AudioControl;
namespace {
int failures, refs, sets, level, result;
bool current=true;
void Check(bool value,const char* name) { if(!value){++failures;std::printf("FAIL %s\n",name);} }
bool Retain(uintptr_t) noexcept { ++refs; return true; }
void Release(uintptr_t) noexcept { --refs; }
bool Current(const AudioOutputBinding&) noexcept { return current; }
int Set(const AudioOutputBinding&,int value) noexcept {++sets;level=value;return result;}
AudioOutputOperations ops{Retain,Release,Current,Set};
}
int main() {
 AudioOutputRegistry registry(ops);
 AudioSettingsView half{50,50,true,true,1};
 auto* binding=registry.Created(0x1000,3,0x2000);
 Check(binding && refs==1,"creation owns callable reference");
 if(!binding) return 1;
 registry.Observe(*binding,AudioOutputLane::Bgm,-600);
 Check(registry.Prepare(*binding,half) && sets==1 && level==-1202,"first accepted output prepares raw gain");
 registry.Prepare(*binding,half);
 Check(sets==1,"unchanged accepted output has no duplicate setter");
 registry.Observe(*binding,AudioOutputLane::Bgm,-700);
 registry.Prepare(*binding,half);
 Check(sets==2 && level==-1302,"native raw fade invalidates output");
 AudioSettingsView unity{100,100,true,true,2};
 registry.ApplyLatest(unity);
 Check(sets==3 && level==-700,"already playing unity restores raw output");
 result=-1;registry.ApplyLatest(half);
 Check(sets==4 && registry.Failures()==1,"gain failure reported outside callback");
 result=0;registry.ApplyLatest(half);
 Check(sets==5,"failed gain remains retryable");
 auto incarnation=binding->incarnation;
 registry.Retire(0x1000,3);
 Check(refs==0,"private reference retired before native release");
 binding=registry.Created(0x1000,3,0x2000);
 Check(binding && binding->incarnation!=incarnation,"same slot and pointer gets fresh incarnation");
 registry.Observe(*binding,AudioOutputLane::Se,-500);registry.Prepare(*binding,half);
 Check(sets==6 && level==-1102,"rebound buffer does not reuse applied receipt");
 auto* graph=registry.GraphReady(0x3000,0x4000,0x5000);
 registry.Observe(*graph,AudioOutputLane::Bgm,-100);registry.Prepare(*graph,half);
 Check(sets==7 && level==-702 && refs==2,"graph gain before initial Run");
 Check(registry.GraphReady(0x3000,0x4000,0x5000)==graph,"same-file restart retains incarnation");
 registry.Prepare(*graph,half);Check(sets==7,"clean graph restart has no setter");
 registry.RebaseBgm(-900);registry.ApplyLatest(half);
 Check(sets==8 && level==-1502,"snapshot rebase updates decoded raw envelope");
 registry.RetireGraph(0x3000);Check(refs==1,"graph references released before native retirement");
 current=false;registry.ApplyLatest(unity);Check(refs==0 && sets==8,"stale slot never receives control update");
 std::printf("audio output lifetime: %d failures\n",failures);return failures?1:0;
}
