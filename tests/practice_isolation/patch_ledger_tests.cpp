#include "runtime/patch_ledger.h"
#include "game/final_memory_patch_owner.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

// Fail one real production allocation at a controlled backend boundary.
static bool failNextAllocation = false;
void* operator new(std::size_t size) {
    if (failNextAllocation) { failNextAllocation = false; throw std::bad_alloc(); }
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); ++failures; } } while (false)

class MemoryBackend final : public Practice::PatchBackend {
public:
    std::array<uint8_t, 32> memory{};
    std::array<uint32_t, 2> protection{{0x20, 0x02}};
    int pins = 0, reads = 0, writes = 0, queries = 0, flushes = 0;
    bool failWritable = false, failRestore = false, failFlush = false;
    bool sameModule = true;
    size_t writeLimit = 32;
    bool failAllocationAfterFirstWrite = false;
    bool Pin(const Practice::PatchModule&, uintptr_t& pin) noexcept override {
        pin = 1; ++pins; return true;
    }
    void Unpin(uintptr_t) noexcept override { --pins; }
    bool SameModule(const Practice::PatchModule&, uintptr_t) noexcept override { return sameModule; }
    bool Read(uintptr_t address, void* out, size_t size) noexcept override {
        ++reads;
        if (address < 0x1000 || address + size > 0x1020) return false;
        std::memcpy(out, memory.data() + address - 0x1000, size); return true;
    }
    size_t Write(uintptr_t address, const void* in, size_t size) noexcept override {
        ++writes;
        const size_t count = size < writeLimit ? size : writeLimit;
        std::memcpy(memory.data() + address - 0x1000, in, count);
        if (failAllocationAfterFirstWrite && writes == 1) failNextAllocation = true;
        return count;
    }
    bool QueryRegion(uintptr_t address, Practice::PatchRegion& region) noexcept override {
        ++queries;
        if (address < 0x1000 || address >= 0x1020) return false;
        const auto page = (address - 0x1000) / 16;
        region = {0x1000 + page * 16, 16, protection[page], 0x1000}; return true;
    }
    bool Protect(const Practice::PatchRegion& region, uint32_t desired,
                 uint32_t& previous) noexcept override {
        if ((desired == 0x40 && failWritable) || (desired != 0x40 && failRestore)) return false;
        auto& actual = protection[(region.address - 0x1000) / 16];
        previous = actual; actual = desired; return true;
    }
    bool Flush(uintptr_t, size_t) noexcept override { ++flushes; return !failFlush; }
};

Practice::PatchDescriptor Site(uintptr_t address = 0x1004) {
    return {{0x1000, 7}, address, {0, 0, 0, 0}, {1, 2, 3, 4}, 15};
}

void EmptyRestoreHasNoDiscovery() {
    MemoryBackend backend;
    Practice::PatchLedger ledger(backend);
    CHECK(ledger.RestoreOwner(15).complete);
    CHECK(ledger.Obligations() == 0);
    CHECK(backend.reads == 0 && backend.queries == 0 && backend.writes == 0);
}

void CrossPageProtectionsAreRestoredIndividually() {
    MemoryBackend backend;
    Practice::PatchLedger ledger(backend);
    uint64_t id = 0;
    CHECK(ledger.Acquire(Site(0x100E), id).complete);
    CHECK(id != 0 && backend.pins == 1);
    CHECK(backend.protection[0] == 0x20 && backend.protection[1] == 0x02);
    CHECK(ledger.RestoreOwner(15).complete);
    CHECK((backend.memory == std::array<uint8_t, 32>{}));
    CHECK(backend.pins == 0 && ledger.Obligations() == 0);
}

void FailedProtectionAndFlushRemainRetryable() {
    MemoryBackend backend;
    Practice::PatchLedger ledger(backend);
    backend.failRestore = true;
    backend.failFlush = true;
    uint64_t id = 0;
    CHECK(!ledger.Acquire(Site(), id).complete);
    CHECK(ledger.Obligations() != 0 && backend.pins == 1);
    CHECK(!ledger.RestoreOwner(15).complete);
    backend.failRestore = false;
    backend.failFlush = false;
    CHECK(ledger.RestoreOwner(15).complete);
    CHECK(backend.protection[0] == 0x20);
    CHECK(backend.pins == 0 && ledger.Obligations() == 0);
}

void PeerExecutableWritableProtectionStillRestores() {
    for (const uint32_t peer : {0x40u, 0x80u}) {
        MemoryBackend backend;
        Practice::PatchLedger ledger(backend);
        uint64_t id = 0;
        CHECK(ledger.Acquire(Site(), id).complete);
        backend.protection[0] = peer;
        CHECK(ledger.RestoreOwner(15).complete);
        CHECK((backend.memory == std::array<uint8_t, 32>{}));
        CHECK(backend.protection[0] == peer && ledger.Obligations() == 0);
    }
    MemoryBackend backend;
    Practice::PatchLedger ledger(backend);
    uint64_t id = 0;
    CHECK(ledger.Acquire(Site(), id).complete);
    backend.protection[0] = 0x02;
    const int writes = backend.writes;
    CHECK(!ledger.RestoreOwner(15).complete);
    CHECK(backend.writes == writes && ledger.Obligations() != 0);
    backend.protection[0] = 0x20;
    CHECK(ledger.RestoreOwner(15).complete);
}

void ForeignReplacementIsNeverOverwritten() {
    MemoryBackend backend;
    Practice::PatchLedger ledger(backend);
    uint64_t id = 0;
    CHECK(ledger.Acquire(Site(), id).complete);
    backend.memory[4] = 0xE9;
    const int writes = backend.writes;
    CHECK(!ledger.RestoreOwner(15).complete);
    CHECK(backend.writes == writes && backend.memory[4] == 0xE9);
    CHECK(backend.pins == 1 && ledger.Obligations() != 0);
    backend.memory[4] = 1;
    CHECK(ledger.RestoreOwner(15).complete);
}

void PartialBytesAndModuleConflictAreRetained() {
    MemoryBackend backend;
    Practice::PatchLedger ledger(backend);
    backend.writeLimit = 2;
    backend.failRestore = true;
    uint64_t id = 0;
    CHECK(!ledger.Acquire(Site(), id).complete);
    CHECK(ledger.Obligations() != 0);
    backend.sameModule = false;
    const int writes = backend.writes;
    CHECK(!ledger.RestoreOwner(15).complete);
    CHECK(backend.writes == writes);
    backend.sameModule = true;
    backend.writeLimit = 32;
    backend.failRestore = false;
    CHECK(ledger.RestoreOwner(15).complete);
    CHECK((backend.memory == std::array<uint8_t, 32>{}));
}

void NoAdoptionAndNoConflictingOwners() {
    MemoryBackend backend;
    Practice::PatchLedger ledger(backend);
    auto descriptor = Site();
    std::memcpy(backend.memory.data() + 4, descriptor.replacement.data(), 4);
    uint64_t id = 0;
    CHECK(!ledger.Acquire(descriptor, id).complete);
    CHECK(ledger.Obligations() == 0 && backend.pins == 0);
    backend.memory.fill(0);
    CHECK(ledger.Acquire(descriptor, id).complete);
    auto foreign = descriptor;
    foreign.owner = 16;
    CHECK(!ledger.Acquire(foreign, id).complete);
    CHECK(ledger.RestoreOwner(16).complete);
    CHECK(ledger.Obligations() == 1);
    CHECK(ledger.RestoreOwner(15).complete);
}

void FinalMemoryGroupRollsBackAndRetainsOnlyOwnedFailures() {
    MemoryBackend backend;
    Practice::PatchLedger ledger(backend);
    FinalMemory::PatchOwner owner(ledger);
    CHECK(owner.Restore() == 0 && !owner.HasObligations());
    CHECK(backend.reads == 0 && backend.queries == 0 && backend.writes == 0);
    auto first = Site(0x1000);
    first.owner = FinalMemory::kPatchOwner;
    auto second = Site(0x1004);
    second.owner = FinalMemory::kPatchOwner;
    backend.memory[4] = 0xE9; // another owner; never adopt it
    CHECK(owner.Apply({first, second}) == 0);
    CHECK(!owner.HasObligations());
    CHECK(backend.memory[0] == 0 && backend.memory[4] == 0xE9);
    backend.memory[4] = 0;
    CHECK(owner.Apply({first, second}) == 2);
    CHECK(owner.Apply({first, second}) == 0);
    backend.failRestore = true;
    CHECK(owner.Restore() == 0 && owner.HasObligations());
    backend.failRestore = false;
    CHECK(owner.Restore() == 2 && !owner.HasObligations());
    CHECK(backend.pins == 0);
}

void AllocationFailureRollsBackAlreadyInstalledFinalMemorySites() {
    MemoryBackend backend;
    Practice::PatchLedger ledger(backend);
    FinalMemory::PatchOwner owner(ledger);
    auto first = Site(0x1000); first.owner = FinalMemory::kPatchOwner;
    auto second = Site(0x1010); second.owner = FinalMemory::kPatchOwner;
    const std::vector<Practice::PatchDescriptor> group{first, second};
    backend.failAllocationAfterFirstWrite = true;
    bool escaped = false;
    try { CHECK(owner.Apply(group) == 0); }
    catch (const std::bad_alloc&) { escaped = true; }
    failNextAllocation = false;
    CHECK(!escaped);
    CHECK(backend.writes >= 2);
    CHECK((backend.memory == std::array<uint8_t, 32>{}));
    CHECK(!owner.HasObligations() && backend.pins == 0);
    owner.Restore();
}
}

int main() {
    EmptyRestoreHasNoDiscovery();
    CrossPageProtectionsAreRestoredIndividually();
    FailedProtectionAndFlushRemainRetryable();
    PeerExecutableWritableProtectionStillRestores();
    ForeignReplacementIsNeverOverwritten();
    PartialBytesAndModuleConflictAreRetained();
    NoAdoptionAndNoConflictingOwners();
    FinalMemoryGroupRollsBackAndRetainsOnlyOwnedFailures();
    AllocationFailureRollsBackAlreadyInstalledFinalMemorySites();
    if (failures) return 1;
    std::puts("Patch ownership, partial mutation and protection tests passed.");
    return 0;
}
