#include "runtime/patch_ledger.h"

#include <windows.h>
#include <cstring>
#include <limits>
#include <map>

namespace Practice {

class NativePatchModuleLease {
public:
    HMODULE held = nullptr;
    uint64_t incarnation = 0;
    ~NativePatchModuleLease() { if (held) FreeLibrary(held); }
};

namespace {
std::mutex moduleMutex;
std::map<uintptr_t, std::weak_ptr<const NativePatchModuleLease>> modules;
uint64_t nextModuleIncarnation = 0;

class Win32PatchBackend final : public PatchBackend {
public:
    bool Pin(const PatchModule& module, uintptr_t& pin) noexcept override {
        if (!module.lifetime ||
            reinterpret_cast<uintptr_t>(module.lifetime->held) != module.base ||
            module.lifetime->incarnation != module.incarnation) return false;
        HMODULE held = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<const char*>(module.base), &held)) return false;
        if (reinterpret_cast<uintptr_t>(held) != module.base) {
            FreeLibrary(held);
            return false;
        }
        pin = reinterpret_cast<uintptr_t>(held);
        return true;
    }
    void Unpin(uintptr_t pin) noexcept override {
        if (pin) FreeLibrary(reinterpret_cast<HMODULE>(pin));
    }
    bool SameModule(const PatchModule& module, uintptr_t pin) noexcept override {
        if (!pin || pin != module.base || !module.lifetime ||
            reinterpret_cast<uintptr_t>(module.lifetime->held) != module.base ||
            module.lifetime->incarnation != module.incarnation) return false;
        MEMORY_BASIC_INFORMATION memory{};
        return VirtualQuery(reinterpret_cast<void*>(module.base), &memory, sizeof(memory)) &&
            memory.State == MEM_COMMIT && memory.Type == MEM_IMAGE &&
            reinterpret_cast<uintptr_t>(memory.AllocationBase) == module.base;
    }
    bool Read(uintptr_t address, void* out, size_t size) noexcept override {
        __try {
            std::memcpy(out, reinterpret_cast<const void*>(address), size);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    size_t Write(uintptr_t address, const void* bytes, size_t size) noexcept override {
        size_t written = 0;
        __try {
            auto* destination = reinterpret_cast<volatile uint8_t*>(address);
            const auto* source = static_cast<const uint8_t*>(bytes);
            for (; written < size; ++written) destination[written] = source[written];
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return written;
    }
    bool QueryRegion(uintptr_t address, PatchRegion& region) noexcept override {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE ||
            (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        const uintptr_t page = address - address % info.dwPageSize;
        region = {page, info.dwPageSize, memory.Protect,
                  reinterpret_cast<uintptr_t>(memory.AllocationBase)};
        return true;
    }
    bool Protect(const PatchRegion& region, uint32_t desired,
                 uint32_t& previous) noexcept override {
        DWORD original = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(region.address), region.size,
                            desired, &original)) return false;
        previous = original;
        return true;
    }
    bool Flush(uintptr_t address, size_t size) noexcept override {
        return FlushInstructionCache(GetCurrentProcess(),
            reinterpret_cast<const void*>(address), size) != FALSE;
    }
};

// Heap-owned: avoids /Zc:threadSafeInit- lazy-init races; records survive loader-lock teardown.
Win32PatchBackend* const nativeBackend = new Win32PatchBackend;
PatchLedger* const nativeLedger = new PatchLedger(*nativeBackend);

}

bool CaptureNativePatchModule(uintptr_t base, PatchModule& module) {
    module = {};
    if (!base) return false;
    std::lock_guard<std::mutex> lock(moduleMutex);
    auto found = modules.find(base);
    if (found != modules.end()) {
        if (auto existing = found->second.lock()) {
            module = {base, existing->incarnation, std::move(existing)};
            return true;
        }
    }
    if (nextModuleIncarnation == (std::numeric_limits<uint64_t>::max)()) return false;
    auto captured = std::make_shared<NativePatchModuleLease>();
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<const char*>(base), &captured->held)) return false;
    if (reinterpret_cast<uintptr_t>(captured->held) != base) return false;
    captured->incarnation = ++nextModuleIncarnation;
    modules[base] = captured;
    module = {base, captured->incarnation, std::move(captured)};
    return true;
}

PatchBackend& NativePatchMemoryBackend() {
    return *nativeBackend;
}

PatchLedger& NativePatchLedger() {
    return *nativeLedger;
}

}
