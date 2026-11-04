#include "runtime/legacy_netplay_handoff.h"
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <thread>
#include <chrono>

int main() {
    std::recursive_mutex boundary;
    std::mutex signal;
    std::condition_variable cv;
    std::atomic<bool> online{false};
    bool writerEntered=false, releaseWriter=false;
    int writes=0, cleanup=0;
    std::thread writer([&] {
        std::lock_guard<std::recursive_mutex> work(boundary);
        std::unique_lock<std::mutex> lock(signal);
        writerEntered=true;cv.notify_all();
        cv.wait(lock,[&]{return releaseWriter;});
        if(!online.load())++writes;
    });
    {
        std::unique_lock<std::mutex> lock(signal);
        cv.wait(lock,[&]{return writerEntered;});
    }
    std::promise<void> started;
    auto prepared=std::async(std::launch::async,[&] {
        started.set_value();
        return Practice::RetireLegacyPractice(boundary,online,[&] {
            if(online.load() || writes!=1)std::abort();
            ++cleanup;
        });
    });
    started.get_future().wait();
    const bool blocked=prepared.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout;
    {
        std::lock_guard<std::mutex> lock(signal);
        releaseWriter=true;cv.notify_all();
    }
    writer.join();
    if(!blocked || !prepared.get() || !online.load() || cleanup!=1)return 1;
    {
        std::lock_guard<std::recursive_mutex> work(boundary);
        if(!online.load())++writes;
    }
    if(writes!=1 || Practice::RetireLegacyPractice(boundary,online,[&]{++cleanup;}) || cleanup!=1)return 2;
    online.store(false);
    if(!Practice::RetireLegacyPractice(boundary,online,[&]{++cleanup;}) || cleanup!=2)return 3;
    online.store(false);
    std::recursive_mutex buffer;
    std::unique_lock<std::recursive_mutex> bufferWriter(buffer);
    std::promise<void> cleanupStarted;
    auto bufferRetired=std::async(std::launch::async,[&] {
        return Practice::RetireLegacyPractice(boundary,online,[&] {
            cleanupStarted.set_value();
            std::lock_guard<std::recursive_mutex> drain(buffer);
            if(online.load())std::abort();
            ++cleanup;
        });
    });
    cleanupStarted.get_future().wait();
    if(online.load())return 4;
    const bool bufferBlocked=bufferRetired.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout;
    bufferWriter.unlock();
    if(!bufferBlocked || !bufferRetired.get() || !online.load() || cleanup!=3)return 5;
    online.store(false);
    try {
        Practice::RetireLegacyPractice(boundary,online,[]{throw 7;});
        return 6;
    } catch(int) {}
    if(online.load())return 7;
    std::puts("legacy retirement drains monitor and buffer writers before publishing");
}
