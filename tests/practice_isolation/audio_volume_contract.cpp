#include <windows.h>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <float.h>
#include <map>
#include "utils/audio_gain_core.h"
#include "utils/audio_runtime_state.h"
#include <cstring>
#include <fstream>

namespace {
constexpr int kMinDirectSoundVolume = -10000;
constexpr int kMaxDirectSoundVolume = 0;
constexpr int kSoundManagerBufferCount = 150;
constexpr uintptr_t kDsBufferSetVolumeVtableOffset = 60;
constexpr uintptr_t kSoundManagerBufferTableOffset = 1216;
using DirectSoundBufferSetVolumeFn = int(__stdcall*)(void*, int);
std::map<uintptr_t, uintptr_t> memory;
int reads, queries, resolutions, calls, comResult, lastVolume, failures;
int actualVolume, getResult, getCalls;
bool applyRequested;
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
    if (comResult == 0) actualVolume = volume;
    return comResult;
}
int __stdcall FakeGetVolume(void*, int* volume) {
    ++getCalls;
    if (fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    *volume = actualVolume;
    return getResult;
}
void RequestAudioApply() { applyRequested = true; }

#include "utils/audio_gain_conversion.inl"
#include "utils/audio_buffer_volume.inl"

using AudioControl::AudioSettingsView;
using AudioControl::ApplyAudioLaneGain;
constexpr uintptr_t kSoundManagerOffset = 8;
constexpr uintptr_t kBgmBufferOffset = 0xf26;
constexpr uintptr_t kBgmTargetVolumeOffset = 0xf28;
constexpr uintptr_t kBgmCurrentVolumeOffset = 0xf2c;
constexpr uintptr_t kBgmAdjustRateOffset = 0xf30;
constexpr uint16_t kInvalidBufferIndex = 150;
constexpr int kDefaultSeDirectSoundVolume = -500;
constexpr uintptr_t kTestGameSystem = 0x40000;

bool SafeReadMemory(uintptr_t address, void* value, size_t size) {
    auto found = memory.find(address);
    if (found == memory.end() || size > sizeof(found->second)) return false;
    std::memcpy(value, &found->second, size);
    return true;
}
bool SafeWriteMemory(uintptr_t address, const void* value, size_t size) {
    if (size > sizeof(uintptr_t)) return false;
    std::memcpy(&memory[address], value, size);
    return true;
}
bool ReadPointer(uintptr_t address, void*& value) {
    return SafeReadMemory(address, &value, sizeof(value)) && value;
}
bool IsCurrentBgmBuffer(void* manager, unsigned short index, uintptr_t& gameSystem) {
    gameSystem = kTestGameSystem;
    return memory[gameSystem + kSoundManagerOffset] == reinterpret_cast<uintptr_t>(manager)
        && memory[gameSystem + kBgmBufferOffset] == index;
}
#include "utils/audio_bgm_volume.inl"
bool IsSoundBufferReadyForOps(uintptr_t, unsigned short, bool, bool) { return true; }
#include "utils/audio_lane_adjustment.inl"

void Reset() {
    memory.clear(); reads = queries = resolutions = calls = comResult = 0;
    lastVolume = 12345; lastBuffer = nullptr;
    actualVolume = getResult = getCalls = 0;
    applyRequested = false;
    ResetBgmOutputVolume();
    suppressed = fault = false; executable = true;
    memory[0x10000 + 1216 + sizeof(uintptr_t) * 3] = 0x20000;
    memory[0x20000] = 0x30000;
    memory[0x30000 + 60] = reinterpret_cast<uintptr_t>(&FakeSetVolume);
    memory[0x30000 + 24] = reinterpret_cast<uintptr_t>(&FakeGetVolume);
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
void SetFadeState(int target, int current, uint16_t rate) {
    memory[kTestGameSystem + kSoundManagerOffset] = 0x10000;
    memory[kTestGameSystem + kBgmBufferOffset] = 3;
    memory[kTestGameSystem + kBgmTargetVolumeOffset] = uintptr_t(target);
    memory[kTestGameSystem + kBgmCurrentVolumeOffset] = uintptr_t(current);
    memory[kTestGameSystem + kBgmAdjustRateOffset] = rate;
}

void CheckFadeState(int target, int current, uint16_t rate) {
    Check(memory[kTestGameSystem + kBgmTargetVolumeOffset] == uintptr_t(target)
        && memory[kTestGameSystem + kBgmCurrentVolumeOffset] == uintptr_t(current)
        && memory[kTestGameSystem + kBgmAdjustRateOffset] == rate,
        "training volume operations preserve native fade state");
}

void NativeVolumeSample(int raw, const AudioSettingsView& view) {
    void* manager = reinterpret_cast<void*>(0x10000);
    const int volume = AdjustVolumeForBuffer(manager, 3, raw, view);
    Check(SetBufferVolume(manager, 3, volume), "native volume sample reaches output");
}

void FadeStateContract() {
    Reset();
    SetFadeState(-10000, -2400, 100);
    actualVolume = -2400;
    AudioSettingsView view{50, 25, true, true, 1};
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(actualVolume == -3002, "late startup applies gain to the existing native level");
    CheckFadeState(-10000, -2400, 100);
    ApplyVolumeBeforePlayback(reinterpret_cast<void*>(0x10000), 3, view);
    Check(actualVolume == -3002 && getCalls == 1, "repeated playback never compounds gain");
    CheckFadeState(-10000, -2400, 100);

    for (int raw = -2500; raw >= -10000; raw -= 100) {
        SetFadeState(-10000, raw, raw == -10000 ? 0 : 100);
        NativeVolumeSample(raw, view);
        ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
        Check(actualVolume == ApplyAudioLaneGain(view, true, raw), "fade-out continues through reapply");
        CheckFadeState(-10000, raw, raw == -10000 ? 0 : 100);
    }
    for (int raw = -10000; raw <= 0; raw += 100) {
        SetFadeState(0, raw, raw == 0 ? 0 : 100);
        view.bgmPercent = raw < -5000 ? 0 : 50;
        NativeVolumeSample(raw, view);
        ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
        Check(actualVolume == ApplyAudioLaneGain(view, true, raw), "mute/unmute preserves fade-in progress");
        CheckFadeState(0, raw, raw == 0 ? 0 : 100);
    }

    SetFadeState(0, 0, 0);
    for (int raw : {-50, -700, -1600, -4950}) {
        NativeVolumeSample(raw, view);
        ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
        ApplyVolumeBeforePlayback(reinterpret_cast<void*>(0x10000), 3, view);
        Check(actualVolume == raw - 602, "screen fade follows raw samples despite stale native current field");
        CheckFadeState(0, 0, 0);
    }
    view.bgmPercent = 0;
    NativeVolumeSample(-3600, view);
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(actualVolume == -10000, "muted output retains the incoming raw sample");
    view.bgmPercent = 100;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(actualVolume == -3600, "unmute restores the current fade sample");

    ResetBgmOutputVolume();
    actualVolume = 0;
    view.bgmPercent = 50;
    ApplyVolumeBeforePlayback(reinterpret_cast<void*>(0x10000), 3, view);
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(actualVolume == -602, "new track resets reused slot and buffer address to its new raw volume");

    NativeVolumeSample(-1500, view);
    RetireBgmVolume(view);
    Check(actualVolume == -1500 && applyRequested, "handoff restores raw BGM and requests offline reapply");
    const int callsAfterRetirement = calls;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(calls == callsAfterRetirement, "resume waits for an unambiguous native sample");
    NativeVolumeSample(-1500, view);
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(actualVolume == -2102, "fresh sample after canceled handoff reapplies gain once");

    const int pendingNativeVolume = AdjustVolumeForBuffer(reinterpret_cast<void*>(0x10000), 3, -1500, view);
    RetireBgmVolume(view);
    SetBufferVolume(reinterpret_cast<void*>(0x10000), 3, pendingNativeVolume);
    const int callsAfterLateContinuation = calls;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    ApplyVolumeBeforePlayback(reinterpret_cast<void*>(0x10000), 3, view);
    Check(calls == callsAfterLateContinuation && actualVolume == -2102,
          "native write after callback retirement cannot become a new raw baseline");
    view.bgmPercent = 0;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(actualVolume == -10000, "saved mute applies immediately even with an uncertain raw baseline");
    view.bgmPercent = 50;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(actualVolume == -10000, "unmute waits for a fresh sample instead of scaling a muted baseline");
    actualVolume = -700;
    NativeVolumeSample(-700, view);
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(actualVolume == -1302, "fresh offline sample adopts native online volume changes");

    comResult = -1;
    RetireBgmVolume(view);
    RetireBgmVolume(view);
    comResult = 0;
    const int callsAfterFailedRestore = calls;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(calls == callsAfterFailedRestore && applyRequested,
          "failed retirement waits for native evidence instead of compounding an uncertain output");
    NativeVolumeSample(-500, view);
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(actualVolume == -1102, "fresh native sample recovers failed retirement");

    view.trainingOwnsBgmGain = false;
    const int callsBeforeExternalOwner = calls;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    ApplyVolumeBeforePlayback(reinterpret_cast<void*>(0x10000), 3, view);
    RetireBgmVolume(view);
    Check(calls == callsBeforeExternalOwner, "external BGM owner prevents training output writes");
    ObserveBgmVolume(reinterpret_cast<void*>(0x10000), 3, -2600, view);
    view.trainingOwnsBgmGain = true;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(calls == callsBeforeExternalOwner, "foreign-owned samples cannot become a raw baseline");
    NativeVolumeSample(-1800, view);
    Check(actualVolume == -2402, "fresh training-owned sample restores gain after foreign ownership");

    Reset();
    SetFadeState(0, 0, 0);
    view.trainingOwnsBgmGain = true;
    getResult = -1;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(calls == 0 && applyRequested, "failed initial output read defers saved gain");
    getResult = 0;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(actualVolume == -602, "initial output read retries with the saved gain");
    ResetBgmOutputVolume();
    getResult = -1;
    view.bgmPercent = 0;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(actualVolume == -10000, "mute applies despite a failed initial volume read");
    getResult = 0;
    view.bgmPercent = 50;
    const int readsAfterUnknownMute = getCalls;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(getCalls == readsAfterUnknownMute, "muted output cannot replace the missing native baseline");
    NativeVolumeSample(-200, view);
    Check(actualVolume == -802, "fresh sample recovers an initially unknown muted output");
    memory[kTestGameSystem + kBgmBufferOffset] = 150;
    const int callsBeforeMissingTrack = calls;
    ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
    Check(calls == callsBeforeMissingTrack, "missing track produces no output write");
}

void NativeFadeTraceContract(const char* path) {
    std::ifstream input(path);
    Check(input.is_open(), "native fade trace opens");
    int scenario, tick, raw, target, current, rate, previousScenario = -1;
    unsigned samples = 0;
    while (input >> scenario >> tick >> raw >> target >> current >> rate) {
        if (scenario != previousScenario) {
            Reset();
            previousScenario = scenario;
        }
        SetFadeState(target, current, uint16_t(rate));
        AudioSettingsView view{50, 25, true, true, 1};
        NativeVolumeSample(raw, view);
        for (uint8_t percent : {0, 23, 50, 100}) {
            view.bgmPercent = percent;
            ApplyBgmVolumeToGameSystem(kTestGameSystem, view);
            ApplyVolumeBeforePlayback(reinterpret_cast<void*>(0x10000), 3, view);
            Check(actualVolume == ApplyAudioLaneGain(view, true, raw), "native x86 trace retains exact fade sample");
            CheckFadeState(target, current, uint16_t(rate));
        }
        ++samples;
    }
    Check(input.eof() && samples > 0, "complete native trace was consumed");
    std::printf("native fade samples checked: %u\n", samples);
}

int main(int argc, char** argv) {
    GainContract(); BufferContract(); FadeStateContract();
    if (argc > 1) NativeFadeTraceContract(argv[1]);
    std::printf("audio volume contract: %d failures\n", failures);
    return failures ? 1 : 0;
}
