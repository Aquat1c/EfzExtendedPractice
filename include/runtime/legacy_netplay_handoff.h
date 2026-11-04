#pragma once
#include <atomic>
#include <mutex>

namespace Practice {
template<class Retire>
bool RetireLegacyPractice(std::recursive_mutex& boundary, std::atomic<bool>& online, Retire&& retire) {
    std::lock_guard<std::recursive_mutex> lock(boundary);
    if (online.load(std::memory_order_acquire)) return false;
    retire();
    online.store(true, std::memory_order_release);
    return true;
}
}
