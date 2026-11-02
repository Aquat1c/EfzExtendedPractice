// Compile with the release x86 toolchain so disassembly matches production.
#include <algorithm>
#include <cmath>
#include "utils/audio_gain_core.h"
namespace {
constexpr int kMinDirectSoundVolume = -10000;
constexpr int kMaxDirectSoundVolume = 0;
#include "utils/audio_gain_conversion.inl"
}
extern "C" __declspec(dllexport) __declspec(noinline) int __cdecl EfzAudioGainLeaf(int base, int gain) noexcept {
    return EfzAudioApplyGain(base, gain);
}
extern "C" __declspec(dllexport) __declspec(noinline) int __cdecl EfzAudioResolveGain(int percent) noexcept {
    return EfzAudioGainForPercent(percent);
}
extern "C" __declspec(dllexport) __declspec(noinline) int __cdecl EfzAudioConvertGain(int percent, int base) {
    return PercentToDirectSoundVolume(percent, base);
}
