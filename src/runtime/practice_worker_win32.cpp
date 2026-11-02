#include "runtime/practice_worker.h"
#include <windows.h>
#include <mmsystem.h>
#include <mutex>
extern void FrameDataMonitor();
namespace Practice {
namespace {
class NativeTiming final:public MonitorTimingBackend {
    bool BeginTimer() noexcept override{return timeBeginPeriod(1)==TIMERR_NOERROR;}
    bool EndTimer() noexcept override{return timeEndPeriod(1)==TIMERR_NOERROR;}
    uintptr_t CapturePriorityOwner() noexcept override {
        HANDLE held=nullptr;
        if(!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&held,
            THREAD_QUERY_INFORMATION|THREAD_SET_INFORMATION,FALSE,0))return 0;
        return reinterpret_cast<uintptr_t>(held);
    }
    void ReleasePriorityOwner(uintptr_t owner) noexcept override {CloseHandle(reinterpret_cast<HANDLE>(owner));}
    int Priority(uintptr_t owner) noexcept override{return GetThreadPriority(reinterpret_cast<HANDLE>(owner));}
    bool SetPriority(uintptr_t owner,int priority) noexcept override{return SetThreadPriority(reinterpret_cast<HANDLE>(owner),priority)!=FALSE;}
};
}
MonitorTimingLease& NativeMonitorTiming() {
    static auto* backend=new NativeTiming;
    static auto* lease=new MonitorTimingLease(*backend);
    return *lease;
}
}

namespace Practice {
namespace {
std::mutex monitorThreadMutex;
HANDLE monitorThread=nullptr;
DWORD WINAPI MonitorMain(void* module) {
    try {FrameDataMonitor();}catch(...) {RecordMonitorFailure();(void)NativeMonitorTiming().EndInterval();}
    FreeLibraryAndExitThread(static_cast<HMODULE>(module),0);
    return 0;
}
}
bool StartMonitorThread() {
    std::lock_guard<std::mutex> lock(monitorThreadMutex);
    if(monitorThread)return WaitForSingleObject(monitorThread,0)==WAIT_TIMEOUT;
    if(MonitorStopRequested())return false;
    HMODULE module=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCSTR>(&MonitorMain),&module)) {RecordMonitorFailure();return false;}
    monitorThread=CreateThread(nullptr,0,MonitorMain,module,0,nullptr);
    if(!monitorThread) {FreeLibrary(module);RecordMonitorFailure();return false;}
    return true;
}
bool JoinStoppedMonitorThread() {
    std::lock_guard<std::mutex> lock(monitorThreadMutex);
    if(!monitorThread)return true;
    if(WaitForSingleObject(monitorThread,0)!=WAIT_OBJECT_0)return false;
    if(!CloseHandle(monitorThread)){RecordMonitorFailure();return false;}
    monitorThread=nullptr;return true;
}
}
