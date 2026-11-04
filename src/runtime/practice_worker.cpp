#include "runtime/practice_worker.h"
#include <mutex>
#include <condition_variable>
#include <chrono>
namespace Practice {
namespace {
std::atomic<uint32_t> runningMonitors{0};
std::atomic<bool> monitorParked{true};
std::atomic<MonitorTimingLease*> monitorTiming{nullptr};
std::mutex signalMutex;
std::condition_variable signalChanged;
bool pendingSignal=false;
std::condition_variable retirementChanged;
bool pendingRetirementProgress=false;
enum class MonitorPhase { Running, StopRequested, Faulted };
std::atomic<MonitorPhase> monitorPhase{MonitorPhase::Running};
}
void MonitorThreadStarted(MonitorTimingLease& timing) noexcept {
    monitorTiming.store(&timing,std::memory_order_release);runningMonitors.fetch_add(1);monitorParked.store(false,std::memory_order_release);
}
void MonitorThreadStopped() noexcept {runningMonitors.fetch_sub(1);monitorParked.store(true,std::memory_order_release);}
void SetMonitorParked(bool parked) noexcept {
    const bool was=monitorParked.exchange(parked,std::memory_order_acq_rel);
    if(parked && !was)NotifyRetirementProgress();
}
bool MonitorParked() noexcept {return monitorParked.load(std::memory_order_acquire);}
MonitorStatus ReadMonitorStatus() noexcept {
    const auto* timing=monitorTiming.load(std::memory_order_acquire);
    return {runningMonitors.load(),timing&&timing->TimerHeld(),timing&&timing->PriorityHeld(),monitorPhase.load()==MonitorPhase::Faulted};
}
void MonitorTimingLease::BeginInterval() noexcept {
    if(interval_)return;
    interval_=true;
    if(!timerHeld_.load() && backend_.BeginTimer())timerHeld_.store(true);
    if(!priorityHeld_) {
        const uintptr_t owner=backend_.CapturePriorityOwner();
        if(!owner)return;
        const int previous=backend_.Priority(owner);
        // 0x7fffffff is THREAD_PRIORITY_ERROR_RETURN, not a priority that can be restored.
        if(previous!=0x7fffffff && previous!=2 && backend_.SetPriority(owner,2)) {
            priorityOwner_=owner;previousPriority_=previous;priorityHeld_=true;
        } else backend_.ReleasePriorityOwner(owner);
    }
}
bool MonitorTimingLease::EndInterval() noexcept {
    interval_=false;
    if(timerHeld_.load() && backend_.EndTimer())timerHeld_.store(false);
    if(priorityHeld_ && backend_.SetPriority(priorityOwner_,previousPriority_)) {
        priorityHeld_=false;backend_.ReleasePriorityOwner(priorityOwner_);priorityOwner_=0;
    }
    return !timerHeld_.load() && !priorityHeld_;
}
}

namespace Practice {
void RecordMonitorFailure() noexcept {monitorPhase.store(MonitorPhase::Faulted,std::memory_order_release);signalChanged.notify_all();retirementChanged.notify_all();}
void NotifyMonitorScopeChanged() noexcept {
    try {{std::lock_guard<std::mutex> lock(signalMutex);pendingSignal=true;}signalChanged.notify_one();}
    catch(...) {RecordMonitorFailure();}
}
void RequestMonitorStop() noexcept {
    auto expected=MonitorPhase::Running;
    (void)monitorPhase.compare_exchange_strong(expected,MonitorPhase::StopRequested,std::memory_order_release,std::memory_order_relaxed);
    NotifyMonitorScopeChanged();
}
bool MonitorStopRequested() noexcept {return monitorPhase.load(std::memory_order_acquire)!=MonitorPhase::Running;}
MonitorWait WaitForMonitorSignal(uint64_t nanoseconds) noexcept {
    try {
        std::unique_lock<std::mutex> lock(signalMutex);
        const auto ready=[] {return pendingSignal || monitorPhase.load(std::memory_order_acquire)!=MonitorPhase::Running;};
        if(nanoseconds==UINT64_MAX)signalChanged.wait(lock,ready);
        else if(!signalChanged.wait_for(lock,std::chrono::nanoseconds(nanoseconds),ready))return MonitorWait::Deadline;
        if(monitorPhase.load(std::memory_order_acquire)==MonitorPhase::Faulted)return MonitorWait::Failed;
        if(monitorPhase.load(std::memory_order_acquire)!=MonitorPhase::Running)return MonitorWait::Stopped;
        pendingSignal=false;return MonitorWait::Signaled;
    }catch(...) {RecordMonitorFailure();return MonitorWait::Failed;}
}
}

namespace Practice {
void NotifyRetirementProgress() noexcept {
    try {{std::lock_guard<std::mutex> lock(signalMutex);pendingRetirementProgress=true;}retirementChanged.notify_one();}
    catch(...) {RecordMonitorFailure();}
}
bool WaitForRetirementProgress() noexcept {
    try {
        std::unique_lock<std::mutex> lock(signalMutex);
        // Bounded: this blocks the native thread, and the caller re-polls on a timeout.
        retirementChanged.wait_for(lock,std::chrono::milliseconds(250),[]{return pendingRetirementProgress || monitorPhase.load(std::memory_order_acquire)==MonitorPhase::Faulted;});
        if(monitorPhase.load(std::memory_order_acquire)==MonitorPhase::Faulted)return false;
        pendingRetirementProgress=false;return true;
    }catch(...) {RecordMonitorFailure();return false;}
}
}
