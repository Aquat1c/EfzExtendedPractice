#include "runtime/practice_worker.h"
#include <cstdio>
namespace {
int failures=0;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(false)
struct Timing final:Practice::MonitorTimingBackend {
    int begins=0,ends=0,priorityReads=0,priorityWrites=0,priority=-1;
    int currentThread=1,otherPriority=0;
    bool beginSucceeds=true,endSucceeds=true,prioritySucceeds=true;
    bool BeginTimer() noexcept override{++begins;return beginSucceeds;}
    bool EndTimer() noexcept override{++ends;return endSucceeds;}
    uintptr_t CapturePriorityOwner() noexcept override{return currentThread;}
    void ReleasePriorityOwner(uintptr_t) noexcept override{}
    int Priority(uintptr_t owner) noexcept override{++priorityReads;return owner==1?priority:otherPriority;}
    bool SetPriority(uintptr_t owner,int value) noexcept override{++priorityWrites;if(!prioritySucceeds)return false;(owner==1?priority:otherPriority)=value;return true;}
};
void FailedTrainingTimerNeverReleasesANativeRequest(){
    Timing timing;Practice::MonitorTimingLease lease(timing);timing.beginSucceeds=false;
    CHECK(lease.EndInterval());CHECK(timing.begins==0&&timing.ends==0&&timing.priorityReads==0);
    lease.BeginInterval();lease.BeginInterval();CHECK(!lease.TimerHeld());CHECK(timing.begins==1);
    CHECK(timing.priority==2);CHECK(lease.EndInterval());CHECK(timing.ends==0&&timing.priority==-1);
}
void SuccessfulIntervalsPairOnceAndRestorePriorPriority(){
    Timing timing;Practice::MonitorTimingLease lease(timing);
    lease.BeginInterval();lease.BeginInterval();CHECK(lease.TimerHeld());CHECK(timing.begins==1&&timing.priority==2);
    CHECK(lease.EndInterval());CHECK(lease.EndInterval());CHECK(timing.ends==1&&timing.priority==-1);
    CHECK(timing.priorityReads==1&&timing.priorityWrites==2);
    lease.BeginInterval();CHECK(lease.EndInterval());CHECK(timing.begins==2&&timing.ends==2);
}
void FailedReleaseRetainsTheExactOutstandingLease(){
    Timing timing;Practice::MonitorTimingLease lease(timing);lease.BeginInterval();timing.endSucceeds=false;
    CHECK(!lease.EndInterval());CHECK(lease.TimerHeld());
    lease.BeginInterval();CHECK(timing.begins==1);
    timing.endSucceeds=true;CHECK(lease.EndInterval());CHECK(!lease.TimerHeld());CHECK(timing.ends==2);
}
void FailedPriorityRestoreCannotWriteAReplacementThread(){
    Timing timing;Practice::MonitorTimingLease lease(timing);lease.BeginInterval();
    timing.prioritySucceeds=false;CHECK(!lease.EndInterval());
    timing.currentThread=2;timing.prioritySucceeds=true;
    CHECK(lease.EndInterval());CHECK(timing.priority==-1);CHECK(timing.otherPriority==0);
}
}
int main(){
    CHECK(Practice::WaitForMonitorSignal(0)==Practice::MonitorWait::Deadline);
    Practice::NotifyMonitorScopeChanged();
    CHECK(Practice::WaitForMonitorSignal(0)==Practice::MonitorWait::Signaled);
    CHECK(Practice::WaitForMonitorSignal(0)==Practice::MonitorWait::Deadline);
    Practice::RequestMonitorStop();
    CHECK(Practice::MonitorStopRequested());
    CHECK(Practice::WaitForMonitorSignal()==Practice::MonitorWait::Stopped);
FailedTrainingTimerNeverReleasesANativeRequest();SuccessfulIntervalsPairOnceAndRestorePriorPriority();FailedReleaseRetainsTheExactOutstandingLease();FailedPriorityRestoreCannotWriteAReplacementThread();return failures?1:0;}
