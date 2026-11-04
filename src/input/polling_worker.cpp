#include "input/polling_worker.h"
#include <windows.h>
#include <process.h>
#include <memory>
#include <new>

namespace InputPolling {
struct Worker::RunContext {
    Worker& owner;
    Callback body;
    Callback afterExit;
};

StartResult Worker::Start(Callback body, Callback afterExit) noexcept {
    auto expected = Phase::Stopped;
    if (!phase_.compare_exchange_strong(expected, Phase::Starting,
            std::memory_order_acq_rel)) return StartResult::Busy;
    auto* context = new (std::nothrow) RunContext{*this, body, afterExit};
    if (context) {
        const uintptr_t thread = _beginthreadex(nullptr, 0, Run, context, 0, nullptr);
        if (thread) {
            CloseHandle(reinterpret_cast<HANDLE>(thread));
            return StartResult::Started;
        }
        delete context;
    }
    phase_.store(Phase::Stopped, std::memory_order_release);
    return StartResult::Failed;
}

bool Worker::IsRunning() const {
    const auto phase = phase_.load(std::memory_order_acquire);
    return phase == Phase::Starting || phase == Phase::Running;
}

void Worker::RequestStop() {
    auto phase = phase_.load(std::memory_order_acquire);
    while (phase == Phase::Starting || phase == Phase::Running) {
        if (phase_.compare_exchange_weak(phase, Phase::Stopping,
                std::memory_order_acq_rel)) return;
    }
}

unsigned __stdcall Worker::Run(void* argument) {
    std::unique_ptr<RunContext> context(static_cast<RunContext*>(argument));
    auto expected = Phase::Starting;
    if (context->owner.phase_.compare_exchange_strong(expected, Phase::Running,
            std::memory_order_acq_rel)) context->body();
    context->owner.phase_.store(Phase::Stopped, std::memory_order_release);
    if (context->afterExit) context->afterExit();
    return 0;
}
}
