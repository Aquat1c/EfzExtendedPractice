#include "utils/minhook_utils.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

void LogOut(const std::string&, bool) {}

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); ++failures; } } while (false)

struct Entry {
    uintptr_t stack;
    uintptr_t returnAddress;
    uint32_t flags;
    uint32_t ecx;
    uint32_t edx;
    uint32_t residue;
};
Entry observed{};
uintptr_t expectedStack = 0;
int callbackCalls = 0;
using NativeFn = int(__cdecl*)(int);
NativeFn original = nullptr;
PracticeHooks::CallbackTicket ticket;
NativeFn secondOriginal = nullptr;
PracticeHooks::CallbackTicket secondTicket;

__declspec(naked) int __cdecl NativeTarget(int) {
    __asm {
        nop
        nop
        nop
        nop
        nop
        mov observed.stack, esp
        mov eax, [esp]
        mov observed.returnAddress, eax
        mov observed.ecx, ecx
        mov observed.edx, edx
        pushfd
        pop observed.flags
        mov eax, [esp-100h]
        mov observed.residue, eax
        mov eax, [esp+4]
        ret
    }
}

int __cdecl HookedTarget(int value) {
    auto execution = MinHookUtils::EnterExecution(ticket);
    ++callbackCalls;
    return original(value) + (execution.Admitted() ? 10 : 0);
}

__declspec(naked) int __cdecl SecondNativeTarget(int) {
    __asm {
        nop
        nop
        nop
        nop
        nop
        mov eax, [esp+4]
        ret
    }
}

int __cdecl SecondHookedTarget(int value) {
    auto execution = MinHookUtils::EnterExecution(secondTicket);
    ++callbackCalls;
    return secondOriginal(value) + (execution.Admitted() ? 20 : 0);
}

__declspec(naked) int Invoke() {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 400h
        mov dword ptr [esp-108h], 12345678h
        mov ecx, 11223344h
        mov edx, 55667788h
        push 7
        lea eax, [esp-4]
        mov expectedStack, eax
        cmp ecx, ecx
        call NativeTarget
        mov esp, ebp
        pop ebp
        ret
    }
}

void CheckNativeEntry(const Entry& expected) {
    CHECK(Invoke() == 7);
    CHECK(callbackCalls == 0);
    CHECK(observed.stack == expectedStack);
    CHECK(observed.returnAddress == expected.returnAddress);
    CHECK(observed.ecx == expected.ecx);
    CHECK(observed.edx == expected.edx);
    CHECK(observed.flags == expected.flags);
    CHECK(observed.residue == expected.residue);
}
}

int main() {
    CHECK(Invoke() == 7);
    const Entry expected = observed;
    CHECK(MH_Initialize() == MH_OK);
    CHECK(MinHookUtils::CreateAndEnableHook(
        reinterpret_cast<void*>(&NativeTarget), reinterpret_cast<void*>(&HookedTarget),
        reinterpret_cast<void**>(&original), "test", "native", nullptr, nullptr, &ticket));
    const auto* entry = reinterpret_cast<const unsigned char*>(&NativeTarget);
    CHECK(entry[0] == 0xe9);
    int32_t displacement = 0;
    std::memcpy(&displacement, entry + 1, sizeof(displacement));
    MEMORY_BASIC_INFORMATION gateMemory{};
    CHECK(VirtualQuery(entry + 5 + displacement, &gateMemory, sizeof(gateMemory)) != 0);
    CHECK(gateMemory.Type == MEM_IMAGE);
    CheckNativeEntry(expected);
    MinHookUtils::SetNativePassthrough(false);
    CheckNativeEntry(expected);
    MinHookUtils::SetPracticeBattleActive(true);
    CHECK(Invoke() == 17);
    CHECK(callbackCalls == 1);
    MinHookUtils::CloseAdmission("test");
    callbackCalls = 0;
    CheckNativeEntry(expected);
    CHECK(MinHookUtils::EnableOwnedTargets("test"));
    CHECK(Invoke() == 17);
    CHECK(callbackCalls == 1);
    callbackCalls = 0;
    MinHookUtils::SetNativePassthrough(true);
    CheckNativeEntry(expected);
    CHECK(MinHookUtils::EnableOwnedTargets("test"));
    CheckNativeEntry(expected);

    CHECK(MinHookUtils::CreateAndEnableHook(
        reinterpret_cast<void*>(&SecondNativeTarget), reinterpret_cast<void*>(&SecondHookedTarget),
        reinterpret_cast<void**>(&secondOriginal), "second", "native", nullptr, nullptr, &secondTicket, true));
    CHECK(SecondNativeTarget(7) == 7);
    CHECK(callbackCalls == 0);
    MinHookUtils::CloseAdmission("test");
    MinHookUtils::SetPracticeBattleActive(false);
    MinHookUtils::SetNativePassthrough(false);
    CheckNativeEntry(expected);
    CHECK(SecondNativeTarget(7) == 27);
    CHECK(callbackCalls == 1);
    callbackCalls = 0;
    CHECK(MinHookUtils::EnableOwnedTargets("test"));
    CheckNativeEntry(expected);
    MinHookUtils::SetPracticeBattleActive(true);
    CHECK(Invoke() == 17);
    CHECK(callbackCalls == 1);
    callbackCalls = 0;
    MinHookUtils::SetPracticeBattleActive(false);
    CheckNativeEntry(expected);
    CHECK(SecondNativeTarget(7) == 27);
    callbackCalls = 0;
    MinHookUtils::SetNativePassthrough(true);
    CHECK(SecondNativeTarget(7) == 7);
    CHECK(callbackCalls == 0);
    return failures ? 1 : 0;
}
