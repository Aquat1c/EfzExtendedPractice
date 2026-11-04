#pragma once
#include <atomic>

namespace InputPolling {
enum class Phase { Stopped, Starting, Running, Stopping };
enum class StartResult { Started, Busy, Failed };

// The owner must outlive its detached thread.
class Worker {
public:
    using Callback = void (*)();
    StartResult Start(Callback body, Callback afterExit = nullptr) noexcept;
    bool IsRunning() const;
    void RequestStop();
private:
    struct RunContext;
    static unsigned __stdcall Run(void* context);
    std::atomic<Phase> phase_{Phase::Stopped};
};
}
