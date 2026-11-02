#include "runtime/practice_session.h"

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <type_traits>

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); ++failures; \
} } while (false)

EfzTmEntryV1 World(uint64_t world, uint64_t timeline) {
    EfzTmEntryV1 entry{};
    entry.size = sizeof(entry);
    entry.abiVersion = EFZ_TM_LIFECYCLE_ABI;
    entry.id = {9, 12, world, timeline};
    entry.gameSystem = 0x100000;
    entry.battleContext = 0x200000;
    entry.player1 = 0x300000;
    entry.player2 = 0x400000;
    return entry;
}

void RetirementRetainsCapturedWorld() {
    Practice::WorkDomain domain;
    CHECK(!domain.TryEnter());
    const auto oldWorld = World(31, 1);
    CHECK(domain.Publish(oldWorld));
    auto lease = domain.TryEnter();
    CHECK(lease);
    CHECK(lease.Context().id.battleWorld == 31);
    const auto generation = lease.Generation();
    CHECK(domain.CloseAdmission() == generation);
    CHECK(!domain.TryEnter());
    CHECK(domain.OutstandingWork() == 1);
    CHECK(!domain.Reclaim());
    CHECK(!domain.Publish(World(32, 1)));
    CHECK(lease.Context().player1 == oldWorld.player1);
    CHECK(!lease.IsCurrent());
    auto moved = std::move(lease);
    CHECK(!lease);
    CHECK(domain.OutstandingWork() == 1);
    moved = {};
    CHECK(domain.OutstandingWork() == 0);
    CHECK(domain.Reclaim());
    CHECK(domain.Publish(World(32, 1)));
    auto next = domain.TryEnter();
    CHECK(next.Generation() != generation);
    CHECK(next.Context().id.battleWorld == 32);
    domain.CloseAdmission();
    next = {};
    CHECK(domain.Reclaim());
}

void NativeRestoreCannotOverwriteAnOutstandingReader() {
    Practice::WorkDomain domain;
    CHECK(domain.Publish(World(31, 7)));
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
    uint64_t observedTimeline = 0;
    std::thread worker([&] {
        auto lease = domain.TryEnter();
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        changed.notify_one();
        changed.wait(lock, [&] { return release; });
        observedTimeline = lease.Context().id.timeline;
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return entered; });
    }
    domain.CloseAdmission();
    CHECK(!domain.Publish(World(31, 8)));
    CHECK(!domain.Reclaim());
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    changed.notify_one();
    worker.join();
    CHECK(observedTimeline == 7);
    CHECK(domain.Reclaim());
    CHECK(domain.Publish(World(31, 8)));
    auto restored = domain.TryEnter();
    CHECK(restored.Context().id.timeline == 8);
    domain.CloseAdmission();
    restored = {};
    CHECK(domain.Reclaim());
}

void InvalidPublicationAndReplacementAreRejected() {
    Practice::WorkDomain domain;
    auto entry = World(31, 1);
    entry.abiVersion = 99;
    CHECK(!domain.Publish(entry));
    entry = World(31, 1);
    entry.size = 0;
    CHECK(!domain.Publish(entry));
    entry = World(31, 1);
    entry.id.providerIncarnation = 0;
    CHECK(!domain.Publish(entry));
    CHECK(domain.Publish(World(31, 1)));
    CHECK(!domain.Publish(World(99, 1)));
    domain.CloseAdmission();
    CHECK(!domain.Publish(World(99, 1)));
    CHECK(domain.Reclaim());
}
}

int main() {
    static_assert(!std::is_copy_constructible<Practice::WorkLease>::value,
                  "a work lease has exactly one releasing owner");
    RetirementRetainsCapturedWorld();
    NativeRestoreCannotOverwriteAnOutstandingReader();
    InvalidPublicationAndReplacementAreRejected();
    if (failures) return 1;
    std::puts("Practice work admission and immutable-context tests passed.");
    return 0;
}
