#pragma once
#include <atomic>
#include <cstdint>

namespace Practice {
struct MonitorTimingBackend {
    virtual ~MonitorTimingBackend()=default;
    virtual bool BeginTimer() noexcept=0;
    virtual bool EndTimer() noexcept=0;
    virtual uintptr_t CapturePriorityOwner() noexcept=0;
    virtual void ReleasePriorityOwner(uintptr_t) noexcept=0;
    virtual int Priority(uintptr_t owner) noexcept=0;
    virtual bool SetPriority(uintptr_t owner,int) noexcept=0;
};
class MonitorTimingLease {
public:
    explicit MonitorTimingLease(MonitorTimingBackend& backend):backend_(backend){}
    void BeginInterval() noexcept;
    bool EndInterval() noexcept;
    bool TimerHeld() const noexcept{return timerHeld_.load();}
    bool PriorityHeld() const noexcept{return priorityHeld_.load();}
private:
    MonitorTimingBackend& backend_;
    bool interval_=false;
    std::atomic<bool> priorityHeld_{false};
    uintptr_t priorityOwner_=0;
    int previousPriority_=0;
    std::atomic<bool> timerHeld_{false};
};
MonitorTimingLease& NativeMonitorTiming();
enum class MonitorWait { Signaled, Deadline, Stopped, Failed };
void RecordMonitorFailure() noexcept;
void NotifyMonitorScopeChanged() noexcept;
void RequestMonitorStop() noexcept;
bool MonitorStopRequested() noexcept;
MonitorWait WaitForMonitorSignal(uint64_t nanoseconds=UINT64_MAX) noexcept;
bool StartMonitorThread();
bool JoinStoppedMonitorThread();
struct MonitorStatus {uint32_t running=0;bool timerHeld=false;bool priorityHeld=false;bool waitFaulted=false;};
void MonitorThreadStarted(MonitorTimingLease&) noexcept;
void MonitorThreadStopped() noexcept;
void SetMonitorParked(bool parked) noexcept;
bool MonitorParked() noexcept;
void NotifyRetirementProgress() noexcept;
bool WaitForRetirementProgress() noexcept;
MonitorStatus ReadMonitorStatus() noexcept;
}
