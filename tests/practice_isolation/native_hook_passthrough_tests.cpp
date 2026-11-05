#include "utils/minhook_utils.h"
#include "utils/bgm_control.h"
#include "gui/custom_menu/sound.h"
#include "core/constants.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

void LogOut(const std::string& message, bool) { std::fprintf(stderr, "%s\n", message.c_str()); }

uintptr_t testEfzBase = 0;
uintptr_t GetEFZBase() { return testEfzBase; }
bool SafeReadMemory(uintptr_t address, void* output, size_t size) {
    if (!address || !output || !size) return false;
    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

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

int bgmCalls = 0, stopCalls = 0, soundCalls = 0;
uintptr_t playbackSystem = 0;
unsigned short playbackTrack = 0, playbackSound = 0;
bool failPlayback = false;

void __fastcall NativeBgm(uintptr_t gameSystem, void*, unsigned short track) {
    if (failPlayback) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    ++bgmCalls;
    playbackSystem = gameSystem;
    playbackTrack = track;
}
int __fastcall NativeStop(uintptr_t gameSystem, void*) {
    ++stopCalls;
    playbackSystem = gameSystem;
    return 0;
}
int __fastcall NativeSound(uintptr_t gameSystem, void*, unsigned short sound) {
    ++soundCalls;
    playbackSystem = gameSystem;
    playbackSound = sound;
    return 0;
}

template<typename T>
void Put(uintptr_t address, T value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
void Jump(uintptr_t address, void* target) {
    Put<unsigned char>(address, 0xE9);
    Put<int32_t>(address + 1, static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - address - 5));
}

// Owned hooks pin their image module; fixture entries must be in the PE image.
#pragma section(".osttest", read, execute)
__declspec(allocate(".osttest")) unsigned char testImage[0x391000]{};

void CheckOstAndMenuSound() {
    CHECK(!PlayBGM(0, 20));
    CHECK(!StopBGM(0));
    CHECK(!InstallBgmTrackObserver());
    auto* image = testImage;
    DWORD oldProtection = 0;
    CHECK(VirtualProtect(image, sizeof(testImage), PAGE_EXECUTE_READWRITE, &oldProtection) != 0);
    testEfzBase = reinterpret_cast<uintptr_t>(image);
    CHECK(!InstallBgmTrackObserver());

    unsigned char gameSystem[0x1000]{}, manager[0x800]{};
    const uintptr_t gs = reinterpret_cast<uintptr_t>(gameSystem);
    const uintptr_t sm = reinterpret_cast<uintptr_t>(manager);
    const uintptr_t entry = testEfzBase + 0x68B0;
    // The native prologue is relocated by the real hook registry and MinHook.
    const unsigned char frame[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x34, 0x8B, 0xE5, 0x5D};
    std::memcpy(reinterpret_cast<void*>(entry), frame, sizeof(frame));
    Jump(entry + sizeof(frame), reinterpret_cast<void*>(&NativeBgm));
    Jump(testEfzBase + 0x6A10, reinterpret_cast<void*>(&NativeStop));
    Jump(testEfzBase + 0x6860, reinterpret_cast<void*>(&NativeSound));
    FlushInstructionCache(GetCurrentProcess(), image, 0x7000);
    Put(testEfzBase + EFZ_BASE_OFFSET_GAME_STATE, gs);
    Put(gs + 8, sm);
    Put<uint16_t>(gs + 0xF26, 18);
    Put<int>(gs + 0xF28, -1100);
    Put<int>(gs + 0xF2C, -832);
    Put<int>(gs + 0xF30, 12);
    CHECK(PlayBGM(gs, 31));
    CHECK(bgmCalls == 1 && playbackSystem == gs && playbackTrack == 31);
    CHECK(GetLastBgmTrack() == 31 && GetBGMBufferIndex(gs) == 18);
    CHECK(GetBGMVolume(gs) == -832);
    CHECK(InstallBgmTrackObserver());
    auto play = reinterpret_cast<void(__thiscall*)(uintptr_t, unsigned short)>(entry);
    MinHookUtils::SetPracticeBattleActive(false);
    MinHookUtils::SetNativePassthrough(false);
    play(gs, 64);
    CHECK(bgmCalls == 2 && playbackTrack == 64 && GetLastBgmTrack() == 64);
    MinHookUtils::SetNativePassthrough(true);
    play(gs, 99);
    CHECK(bgmCalls == 3 && playbackTrack == 99 && GetLastBgmTrack() == 64);
    MinHookUtils::SetNativePassthrough(false);
    play(gs, 35);
    CHECK(bgmCalls == 4 && GetLastBgmTrack() == 35);
    failPlayback = true;
    CHECK(!PlayBGM(gs, 10));
    CHECK(GetLastBgmTrack() == 35);
    failPlayback = false;
    CHECK(StopBGM(gs));
    CHECK(stopCalls == 1 && playbackSystem == gs && GetLastBgmTrack() == 150);
    CHECK(GetBGMVolume(gs) == -832);
    CHECK(*reinterpret_cast<int*>(gs + 0xF28) == -1100);
    CHECK(*reinterpret_cast<int*>(gs + 0xF30) == 12);

    uintptr_t methods[19]{};
    for (auto& method : methods) method = reinterpret_cast<uintptr_t>(&NativeStop);
    uintptr_t object = reinterpret_cast<uintptr_t>(methods);
    Put(sm + 12, reinterpret_cast<uintptr_t>(&object));
    Put(sm + 1216 + 4 * 7, reinterpret_cast<uintptr_t>(&object));
    Put<uint16_t>(gs + 3748 + 2 * 6, 7);
    Put<uint16_t>(gs + 3748 + 2 * 8, 7);
    CustomMenu::Sound::PlayCursor();
    CHECK(soundCalls == 1 && playbackSound == 8 && playbackSystem == gs);
    CustomMenu::Sound::PlayDecision();
    CHECK(soundCalls == 2 && playbackSound == 6);
    Put<uint16_t>(gs + 3748 + 2 * 8, 150);
    CustomMenu::Sound::PlayCursor();
    CHECK(soundCalls == 2);
    Put<uint16_t>(gs + 3748 + 2 * 8, 7);
    methods[18] = reinterpret_cast<uintptr_t>(&object);
    CustomMenu::Sound::PlayCursor();
    CHECK(soundCalls == 2);
    methods[18] = reinterpret_cast<uintptr_t>(&NativeStop);
    Put<uintptr_t>(sm + 12, 0);
    CustomMenu::Sound::PlayDecision();
    CHECK(soundCalls == 2);
    Put<uintptr_t>(gs + 8, 0);
    CustomMenu::Sound::PlayCursor();
    CHECK(soundCalls == 2);
    testEfzBase = 0;
    CustomMenu::Sound::PlayCursor();
    CHECK(soundCalls == 2);
    MinHookUtils::SetNativePassthrough(true);
    CHECK(MH_Uninitialize() == MH_OK);
    DWORD ignored = 0;
    CHECK(VirtualProtect(image, sizeof(testImage), oldProtection, &ignored) != 0);
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
    CheckOstAndMenuSound();
    return failures ? 1 : 0;
}
