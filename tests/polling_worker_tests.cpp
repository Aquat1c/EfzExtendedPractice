#include <windows.h>
#include <process.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

static std::atomic<bool> failAllocation{false};
void* operator new(size_t size) {
    if (failAllocation.exchange(false)) throw std::bad_alloc();
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }

static bool failLaunch = false;
static bool suspendLaunch = false;
static HANDLE launchedThread = nullptr;
static void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
}
static uintptr_t __cdecl ControlledBeginThread(void* security, unsigned stack,
    unsigned (__stdcall* entry)(void*), void* context, unsigned flags, unsigned* id) {
    if (failLaunch) { failLaunch = false; return 0; }
    const uintptr_t handle = _beginthreadex(security, stack, entry, context,
        flags | (suspendLaunch ? CREATE_SUSPENDED : 0), id);
    if (handle) Check(DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(handle),
        GetCurrentProcess(), &launchedThread, 0, FALSE, DUPLICATE_SAME_ACCESS) != FALSE,
        "retain worker handle for joining the real thread");
    return handle;
}
#define _beginthreadex ControlledBeginThread
#include "../src/input/polling_worker.cpp"
#undef _beginthreadex

static HANDLE entered = nullptr;
static HANDLE releaseBody = nullptr;
static HANDLE completed = nullptr;
static std::atomic<int> calls{0};
static void Body() {
    ++calls;
    SetEvent(entered);
    Check(WaitForSingleObject(releaseBody, 5000) == WAIT_OBJECT_0, "body released before timeout");
}
static void Completed() { SetEvent(completed); }
static void JoinThread(HANDLE handle) {
    Check(WaitForSingleObject(handle, 5000) == WAIT_OBJECT_0, "worker exited before timeout");
    CloseHandle(handle);
}
static void Join() { JoinThread(launchedThread); launchedThread = nullptr; }
static InputPolling::Worker* restartOwner = nullptr;
static HANDLE replacementEntered = nullptr;
static HANDLE releaseReplacement = nullptr;
static HANDLE restartPublished = nullptr;
static InputPolling::StartResult restartResult = InputPolling::StartResult::Failed;
static void ReplacementBody() {
    SetEvent(replacementEntered);
    Check(WaitForSingleObject(releaseReplacement, 5000) == WAIT_OBJECT_0, "replacement released before timeout");
}
static void RestartAfterExit() {
    restartResult = restartOwner->Start(ReplacementBody);
    SetEvent(restartPublished);
}

int main() {
    entered = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    releaseBody = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    completed = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    Check(entered && releaseBody && completed, "create synchronization events");
    InputPolling::Worker worker;
    using Result = InputPolling::StartResult;

    failAllocation.store(true);
    Check(worker.Start(Body, Completed) == Result::Failed && !worker.IsRunning(),
        "allocation failure releases startup ownership");
    failLaunch = true;
    Check(worker.Start(Body, Completed) == Result::Failed && !worker.IsRunning(),
        "native launch failure releases startup ownership");

    suspendLaunch = true;
    Check(worker.Start(Body, Completed) == Result::Started, "retry starts after failed launch");
    worker.RequestStop();
    Check(!worker.IsRunning(), "cancel closes input admission before thread entry");
    Check(worker.Start(Body, Completed) == Result::Busy, "cancelled startup retains ownership until exit");
    ResumeThread(launchedThread);
    Join();
    Check(calls.load() == 0, "cancelled startup never executes the polling body");
    Check(WaitForSingleObject(completed, 0) == WAIT_OBJECT_0, "cancelled startup still publishes completion");

    suspendLaunch = false;
    ResetEvent(completed);
    Check(worker.Start(Body, Completed) == Result::Started, "restart after cancelled worker exit");
    Check(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0, "real polling body started");
    Check(worker.Start(Body, Completed) == Result::Busy, "duplicate start cannot create a second body");
    worker.RequestStop();
    Check(worker.Start(Body, Completed) == Result::Busy, "stop retains ownership through body cleanup");
    SetEvent(releaseBody);
    Join();
    Check(calls.load() == 1 && !worker.IsRunning(), "body completed exactly once before retirement");

    Check(worker.Start(Body, Completed) == Result::Started, "completed worker can restart");
    Join();
    Check(calls.load() == 2 && !worker.IsRunning(), "replacement completes without stale ownership");
    replacementEntered = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    releaseReplacement = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    restartPublished = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    Check(replacementEntered && releaseReplacement && restartPublished, "create restart events");
    restartOwner = &worker;
    ResetEvent(entered);
    ResetEvent(releaseBody);
    Check(worker.Start(Body, RestartAfterExit) == Result::Started, "start worker with controller restart callback");
    const HANDLE retiringThread = launchedThread;
    Check(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0, "old controller body entered");
    worker.RequestStop();
    SetEvent(releaseBody);
    Check(WaitForSingleObject(restartPublished, 5000) == WAIT_OBJECT_0, "exit callback attempted restart");
    Check(restartResult == Result::Started, "exit callback can claim the replacement after cleanup");
    Check(WaitForSingleObject(replacementEntered, 5000) == WAIT_OBJECT_0, "replacement body entered");
    JoinThread(retiringThread);
    Check(worker.IsRunning(), "old thread return cannot retire its replacement");
    worker.RequestStop();
    SetEvent(releaseReplacement);
    Join();
    CloseHandle(replacementEntered);
    CloseHandle(releaseReplacement);
    CloseHandle(restartPublished);
    CloseHandle(entered);
    CloseHandle(releaseBody);
    CloseHandle(completed);
    return 0;
}
