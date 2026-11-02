#include <windows.h>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <float.h>
#include <map>
#include "utils/audio_gain_core.h"

namespace {
constexpr int kMinDirectSoundVolume = -10000;
constexpr int kMaxDirectSoundVolume = 0;
constexpr int kSoundManagerBufferCount = 150;
constexpr uintptr_t kDsBufferSetVolumeVtableOffset = 60;
constexpr uintptr_t kSoundManagerBufferTableOffset = 1216;
using DirectSoundBufferSetVolumeFn = int(__stdcall*)(void*, int);
std::map<uintptr_t, uintptr_t> memory;
int reads, queries, resolutions, calls, comResult, lastVolume, failures;
void* lastBuffer;
bool suppressed, executable, fault;

void Check(bool passed, const char* message) {
    if (!passed) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
bool RuntimeAudioControlSuppressed(const char*) { return suppressed; }
bool ReadChecked(uintptr_t address, uintptr_t& value) {
    ++reads; ++queries;
    if (address == 0x10000 + 1216 + sizeof(uintptr_t) * 3) ++resolutions;
    auto found = memory.find(address);
    if (found == memory.end()) { value = 0; return false; }
    value = found->second;
    return true;
}
bool IsExecutableAddress(uintptr_t address) {
    if (!address) return false;
    ++queries;
    return executable;
}
int __stdcall FakeSetVolume(void* buffer, int volume) {
    ++calls; lastBuffer = buffer; lastVolume = volume;
    if (fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    return comResult;
}

#include "utils/audio_gain_conversion.inl"
#include "utils/audio_buffer_volume.inl"

void Reset() {
    memory.clear(); reads = queries = resolutions = calls = comResult = 0;
    lastVolume = 12345; lastBuffer = nullptr;
    suppressed = fault = false; executable = true;
    memory[0x10000 + 1216 + sizeof(uintptr_t) * 3] = 0x20000;
    memory[0x20000] = 0x30000;
    memory[0x30000 + 60] = reinterpret_cast<uintptr_t>(&FakeSetVolume);
}
int LegacyVolume(int percent, int base) {
    if (percent <= 0) return -10000;
    const double scaled = std::pow(10.0, static_cast<double>(base) / 2000.0) * (static_cast<double>(percent) / 100.0);
    if (scaled <= 0.0) return -10000;
    return std::clamp(static_cast<int>(std::lround(2000.0 * std::log10(scaled))), -10000, 0);
}
void GainContract() {
    volatile int half = 50, base = -500;
    _clearfp();
    const int value = PercentToDirectSoundVolume(half, base);
    const unsigned status = _statusfp();
    Check(value == -1102, "SE native/default -500 base plus half gain");
    Check((status & (_SW_INEXACT | _SW_UNDERFLOW | _SW_OVERFLOW | _SW_INVALID | _SW_ZERODIVIDE)) == 0,
          "supported converter leaves FP status untouched");
    for (int b = -10000; b <= 0; ++b) {
        Check(PercentToDirectSoundVolume(100, b) == b, "exact unity");
        Check(PercentToDirectSoundVolume(0, b) == -10000, "explicit silence");
        for (int p = 1; p <= 100; ++p)
            Check(PercentToDirectSoundVolume(p, b) == LegacyVolume(p, b), "supported domain legacy equivalence");
    }
    for (int p : {-1, 0, 1, 50, 100, 101, 200})
        for (int b : {-20000, -10001, -10000, -500, 0, 1, 500, 10000})
            Check(PercentToDirectSoundVolume(p, b) == LegacyVolume(p, b), "out-of-domain legacy fallback");
    Check(EfzAudioGainForPercent(-1) == -10000, "gain low clamp");
    Check(EfzAudioGainForPercent(101) == 0, "gain high clamp");
    Check(EfzAudioGainForPercent(1) == -4000, "one percent gain");
    Check(EfzAudioGainForPercent(50) == -602, "half amplitude gain");
    Check(EfzAudioApplyGain(INT_MIN, -602) == -10000, "invalid base cannot underflow");
    Check(EfzAudioApplyGain(INT_MAX, -602) == -602, "invalid positive base clamps");
    Check(EfzAudioApplyGain(-500, INT_MIN) == -10000, "invalid gain clamps");
    for (int p = 1; p <= 100; ++p) {
        Check(EfzAudioGainForPercent(p) >= EfzAudioGainForPercent(p - 1), "monotonic gain");
        for (int b = -10000; b <= 0; ++b) {
            const int attenuated = EfzAudioApplyGain(b, EfzAudioGainForPercent(p));
            Check(attenuated >= -10000 && attenuated <= b, "bounded attenuation");
        }
    }
}
void BufferContract() {
    const uintptr_t entry = 0x10000 + 1216 + sizeof(uintptr_t) * 3;
    const char* names[] = {"success", "suppressed", "null manager", "invalid index", "missing buffer", "null buffer",
        "missing vtable", "null vtable", "missing method", "null method", "nonexecutable", "COM failure", "COM SEH fault"};
    for (int n = 0; n < 13; ++n) {
        Reset(); void* manager = reinterpret_cast<void*>(0x10000); unsigned short index = 3;
        if (n == 1) suppressed = true;
        if (n == 2) manager = nullptr;
        if (n == 3) index = 150;
        if (n == 4) memory.erase(entry);
        if (n == 5) memory[entry] = 0;
        if (n == 6) memory.erase(0x20000);
        if (n == 7) { memory[0x20000] = 0; memory[60] = reinterpret_cast<uintptr_t>(&FakeSetVolume); }
        if (n == 8) memory.erase(0x30000 + 60);
        if (n == 9) memory[0x30000 + 60] = 0;
        if (n == 10) executable = false;
        if (n == 11) comResult = 1;
        if (n == 12) fault = true;
        Check(SetBufferVolume(manager, index, -602) == (n == 0), names[n]);
        Check(calls == ((n == 0 || n >= 11) ? 1 : 0), "single COM invocation only after validation");
        Check(resolutions <= 1, "one concrete buffer resolution");
        Check(queries <= 4, "at most three checked reads plus executable validation");
        if (calls) {
            Check(lastBuffer == reinterpret_cast<void*>(0x20000), "resolved buffer forwarded unchanged");
            Check(lastVolume == -602, "volume forwarded unchanged");
        }
        if (n >= 1 && n <= 3) Check(reads == 0, "suppression and invalid arguments fail before memory access");
    }
    Reset(); comResult = -1;
    Check(!SetBufferVolume(reinterpret_cast<void*>(0x10000), 3, -602), "negative COM failure");
    for (int volume : {INT_MIN, -10000, -500, 0, 1, INT_MAX}) {
        Reset();
        Check(SetBufferVolume(reinterpret_cast<void*>(0x10000), 3, volume), "setter preserves general caller success");
        Check(lastVolume == volume, "setter forwards arbitrary volume unchanged");
    }
}
}
int main() {
    GainContract(); BufferContract();
    std::printf("audio volume contract: %d failures\n", failures);
    return failures ? 1 : 0;
}
