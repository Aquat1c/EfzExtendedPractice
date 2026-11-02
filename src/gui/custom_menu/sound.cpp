#include "../include/gui/custom_menu/sound.h"
#include "../include/core/constants.h"
#include "../include/core/memory.h"
#include "../include/utils/audio_control.h"
#include "../include/utils/utilities.h"

#include <windows.h>
#include <cstdint>

namespace CustomMenu::Sound {

namespace {

constexpr uintptr_t kPlaySoundEffectRva = 0x6860;    // efz.c: playSoundEffect
constexpr unsigned short kSeDecision    = 6;
constexpr unsigned short kSeCursor      = 8;

using PlaySoundEffectFn = int(__thiscall*)(void* soundManager, unsigned short soundIndex);

void Play(unsigned short soundIndex) {
    const uintptr_t base = GetEFZBase();
    if (!base) return;

    uintptr_t gameSystem = 0;
    if (!SafeReadMemory(base + EFZ_BASE_OFFSET_GAME_STATE, &gameSystem, sizeof(gameSystem)) || !gameSystem) {
        return;
    }

    if (!AudioControl::IsCommonSoundEffectReady(gameSystem, soundIndex)) {
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
