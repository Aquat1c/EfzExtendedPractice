#include "../include/gui/custom_menu/sound.h"
#include "../include/core/constants.h"
#include "../include/core/memory.h"
#include "../include/utils/utilities.h"

#include <windows.h>
#include <cstdint>

namespace CustomMenu::Sound {

namespace {

constexpr uintptr_t kPlaySoundEffectRva = 0x6860;    // efz.c: playSoundEffect
constexpr unsigned short kSeDecision    = 6;
constexpr unsigned short kSeCursor      = 8;

using PlaySoundEffectFn = int(__thiscall*)(void* soundManager, unsigned short soundIndex);

template<typename T>
bool Read(uintptr_t address, T& value) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!address || !VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory))
        || memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        || !(memory.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                              | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    const uintptr_t end = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    return end >= address && end - address >= sizeof(value)
        && SafeReadMemory(address, &value, sizeof(value));
}

bool HasMethod(uintptr_t object, uintptr_t offset) {
    uintptr_t vtable = 0, method = 0;
    MEMORY_BASIC_INFORMATION memory{};
    return Read(object, vtable) && vtable && Read(vtable + offset, method) && method
        && VirtualQuery(reinterpret_cast<void*>(method), &memory, sizeof(memory))
        && memory.State == MEM_COMMIT && !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        && (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

bool SoundReady(uintptr_t gameSystem, unsigned short soundIndex) {
    if (!gameSystem || soundIndex >= 65) return false;
    uintptr_t manager = 0, directSound = 0, buffer = 0;
    uint16_t bufferIndex = 150;
    // Native GameSystem and SoundManager tables hold allocated buffer indices and COM objects.
    return Read(gameSystem + 8, manager) && manager
        && Read(manager + 12, directSound) && HasMethod(directSound, 24)
        && Read(gameSystem + 3748 + sizeof(uint16_t) * soundIndex, bufferIndex) && bufferIndex < 150
        && Read(manager + 1216 + sizeof(uintptr_t) * bufferIndex, buffer) && buffer
        && HasMethod(buffer, 48) && HasMethod(buffer, 52) && HasMethod(buffer, 60) && HasMethod(buffer, 72);
}

void Play(unsigned short soundIndex) {
    const uintptr_t base = GetEFZBase();
    if (!base) return;

    uintptr_t gameSystem = 0;
    if (!SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE, &gameSystem, sizeof(gameSystem)) || !gameSystem) {
        return;
    }

    if (!SoundReady(gameSystem, soundIndex)) {
        return;
    }

    auto playSound = reinterpret_cast<PlaySoundEffectFn>(base + kPlaySoundEffectRva);
    if (!playSound) return;

    __try {
        playSound(reinterpret_cast<void*>(gameSystem), soundIndex);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}

void PlayCursor() {
    Play(kSeCursor);
}

void PlayDecision() {
    Play(kSeDecision);
}

}
