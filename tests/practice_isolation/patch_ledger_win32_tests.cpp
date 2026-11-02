#include "runtime/patch_ledger.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#pragma section(".pfxt", read)
__declspec(allocate(".pfxt")) const unsigned char patchFixture[4] = {7, 8, 9, 10};

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); ++failures; } } while (false)

void ActualSehWriteReportsCompletedPrefix() {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, info.dwPageSize * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(memory != nullptr);
    if (!memory) return;
    DWORD previous = 0;
    CHECK(VirtualProtect(memory + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &previous));
    const unsigned char bytes[4] = {1, 2, 3, 4};
    auto& backend = Practice::NativePatchMemoryBackend();
    const size_t written = backend.Write(
        reinterpret_cast<uintptr_t>(memory + info.dwPageSize - 2), bytes, sizeof(bytes));
    CHECK(written == 2);
    CHECK(memory[info.dwPageSize - 2] == 1 && memory[info.dwPageSize - 1] == 2);
    CHECK(VirtualFree(memory, 0, MEM_RELEASE));
}

void QualificationTokenAndRealImagePageRestoration() {
    auto& backend = Practice::NativePatchMemoryBackend();
    Practice::PatchModule captured{};
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    CHECK(Practice::CaptureNativePatchModule(base, captured));
    CHECK(captured.lifetime && captured.incarnation);
    Practice::PatchModule same{};
    CHECK(Practice::CaptureNativePatchModule(base, same));
    CHECK(same.incarnation == captured.incarnation && same.lifetime == captured.lifetime);
    uintptr_t pin = 0;
    auto stale = captured;
    ++stale.incarnation;
    CHECK(!backend.Pin(stale, pin));
    const Practice::PatchModule unheld{captured.base, captured.incarnation};
    CHECK(!backend.Pin(unheld, pin));

    const auto address = reinterpret_cast<uintptr_t>(patchFixture);
    Practice::PatchRegion before{}, after{};
    CHECK(backend.QueryRegion(address, before));
    CHECK(before.protection == PAGE_READONLY);
    Practice::PatchLedger ledger(backend);
    Practice::PatchDescriptor site{captured, address, {7,8,9,10}, {11,12,13,14}, 91};
    uint64_t id = 0;
    CHECK(ledger.Acquire(site, id).complete);
    unsigned char observed[4]{};
    CHECK(backend.Read(address, observed, 4));
    CHECK(observed[0] == 11 && observed[3] == 14);
    CHECK(backend.QueryRegion(address, after) && after.protection == before.protection);
    CHECK(ledger.RestoreOwner(91).complete);
    CHECK(backend.Read(address, observed, 4));
    CHECK(observed[0] == 7 && observed[3] == 10);
    CHECK(backend.QueryRegion(address, after) && after.protection == before.protection);
}
}

int main() {
    ActualSehWriteReportsCompletedPrefix();
    QualificationTokenAndRealImagePageRestoration();
    if (failures) return 1;
    std::puts("Win32 patch backend partial-fault, module-token and protection tests passed.");
    return 0;
}
