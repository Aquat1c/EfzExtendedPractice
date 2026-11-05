# EFZ audio volume implementation guide

The training mod's volume feature gave the player separate BGM and sound-effect sliders. EFZ already loaded, played and faded its sounds. Our hooks applied the selected volume when those sounds were loaded, when they played and when the game changed their volume.

This guide explains how to build volume controls around vanilla Memorial EFZ. It covers shared sounds, each fighter's sound bank, music, fades, settings and the supporting implementation. The Revival section explains its rollback audio constraints and the compatibility work a native implementation would require.

The feature was removed because our volume handling conflicted with Revival's rollback audio handling. Our workarounds introduced the fade problems. The section [Why Revival should own its volume controls](#why-revival-should-own-its-volume-controls) explains the known behavior and the reason for that decision.

## Start here

- [What we hooked](#what-we-hooked) shows the six vanilla entry points and what each hook contributed.
- [How character sounds were handled](#how-character-sounds-were-handled) explains the per-fighter sound tables and load/play order.
- [How volume and fades were calculated](#how-volume-and-fades-were-calculated) explains the raw-volume cache.
- [Reproducing the vanilla feature](#reproducing-the-vanilla-feature) puts the hooks, settings and lifetime handling together.
- [Revival compatibility](#why-revival-should-own-its-volume-controls) explains rollback audio behavior and the desync risk.
- [Technical reference](#technical-reference) contains the vanilla addresses, signatures and layouts.
- [Settings shared with another mod](#settings-shared-with-another-mod), [tests](#tests-and-evidence) and [source inventory](#removed-files-and-their-responsibilities) preserve the remaining implementation details.

## About the sources

This guide describes the implementation immediately before the volume feature was removed. Code citations identify the historical files, functions and relevant lines. The inline excerpts preserve the key operations for reuse, while the source inventory records how the removed components fitted together. Native examples use refactored names for objects and fields, with their verified layouts in the technical reference.

Removal covered the sliders, volume config keys, hooks, automatic application, shared-settings bridge and volume-specific tests. Practice's OST selector uses a small offline track observer, and menu sounds use validated native playback. A replacement DLL takes effect on the next game launch.

## What we hooked

The vanilla implementation used six shared engine functions. Three handled loading music or effects. Two handled playback and volume writes. The sixth wrapped a screen fade.

| Native function | What EFZ supplied | What our hook did |
|---|---|---|
| `playBackgroundMusic` | GameSystem and a logical track ID. | Reset the music cache, call the original loader/player, then apply the BGM setting to the resulting buffer. |
| `loadSoundEffects` | GameSystem containing the common sound table. | Call the original loader, then apply SE volume to its 65 buffer indices. |
| `loadCharacterSounds` | The character being loaded. | Call the original loader, then apply SE volume through that character's manager and 50 buffer indices. |
| `playSoundBuffer` | SoundManager, allocated buffer index and loop flag. | Apply the appropriate setting to that buffer immediately before calling native playback. |
| `setSoundVolume` | SoundManager, buffer index and the game's requested level. | Save raw BGM intent where relevant, calculate the adjusted level and write it to the buffer. |
| `fadeWithSoundAdjustment` | ScreenEffect, palette, direction, base volume and adjustment. | Pass the native arguments through to the original fade. Its volume calls supplied the samples that our setter hook adjusted. |

All six original functions used `__thiscall`. The C++ detours used `__fastcall`, receiving the original object in ECX and an unused EDX parameter. They were registered under `[AUDIO][HOOK]` with offline frontend admission, allowing menu audio as well as Practice.

The main reason for covering both loading and playback was timing. A setting loaded at startup had to reach existing sounds. A newly loaded fighter or music track needed the same setting. A later native volume write could otherwise replace an earlier adjustment. `audio_control.cpp`, lines 1296–1412

## How character sounds were handled

### One loader hook covered the roster

Every character passed through the shared `loadCharacterSounds` function. Its `this` pointer identified the fighter currently being initialized. Character-specific handling came from that object's data.

Each fighter stored a SoundManager reference at `Character + 0x88` and its own table of 50 unsigned 16-bit sound-buffer indices at `Character + 0x264`. Native character construction supplied the screen's shared manager. The separate character tables pointed into that manager's 150-slot buffer table. A character's logical sound number and the allocated buffer index were separate values.

The loader read the character folder name at `+0x94` and attempted the character's WAV files numbered `00.wav` through `49.wav`. For each file it stored the index returned by `loadWaveFile` and requested native volume `-500`. Missing files could return sentinel 150, so the table described 50 possible sounds. The meaning of each WAV depended on that character's assets. Source `loadCharacterSounds`, corroborated by its native instruction sequence.

The refactored native flow is shown below. Named fields replace memory offsets. `MakeCharacterWaveName` abbreviates the original filename concatenation, using the character's wave subdirectory and a two-digit WAV filename.

```cpp
void LoadCharacterSoundBank(Character& character)
{
    for (int sound = 0; sound < 50; ++sound) {
        const auto filename = MakeCharacterWaveName(character.folder, sound);
        const uint16_t buffer = LoadWaveFile(character.soundManager, filename);
        character.soundBuffers[sound] = buffer;
        SetSoundVolume(character.soundManager, buffer, -500);
    }
}
```

Our detour called the original loader first. After it returned, `ImplHookedLoadCharacterSounds` checked sound readiness and passed the same character pointer to `ApplyCharacterSeVolume`. That helper read the character's manager, walked all 50 indices, skipped unreadable or invalid entries and applied the SE setting to each valid buffer. Native exceptions during loading stopped the follow-up.

Calling the original first was essential. It populated the manager's buffers and the character's index table. Applying to the previous table before that load would target old allocations or unused slots. `audio_control.cpp`, lines 1398–1412, `audio_control.cpp`, lines 1036–1061

After its ownership, character and manager checks, `ApplyCharacterSeVolume` performed this loop. The excerpt omits the discarded success counter. `SetBufferVolume` supplied the receiver checks and native-exception handling described later.

```cpp
const int volume = ApplyAudioLaneGain(view, false, kDefaultSeDirectSoundVolume);
for (int sound = 0; sound < kCharacterSeCount; ++sound) {
    uint16_t buffer = kInvalidBufferIndex;
    const uintptr_t entry = characterPtr + kCharacterSeBufferTableOffset
        + sizeof(uint16_t) * sound;
    if (SafeReadMemory(entry, &buffer, sizeof(buffer))) {
        SetBufferVolume(soundManagerPtr, buffer, volume);
    }
}
```

The detour read one settings view inside the audio boundary. Source `audio_control.cpp`, `HookedLoadCharacterSounds`.

```cpp
void __fastcall HookedLoadCharacterSounds(uintptr_t characterPtr, void* unused)
{
    EFZ_AUDIO_CALLBACK(ImplHookedLoadCharacterSounds(
        characterPtr, unused, ReadAdmittedAudioSettings()));
}
```

### Both fighters received setting changes

The post-load hook handled the character that had just loaded. A later slider change used `ApplyCharacterSeVolumesNow` to resolve the current P1 and P2 objects and apply to each character separately. It also updated the common sounds and current BGM in the same application pass.

Playback supplied another application point. `playSoundBuffer` received the actual manager and allocated index chosen by EFZ. The hook treated it as BGM only when both matched the current GameSystem BGM. Other valid buffers used the SE lane, covering character sounds through the same shared playback function.

The old bulk and pre-play paths used raw SE level `-500`. A native `setSoundVolume` call instead supplied its own raw level. Consequently a bulk update or replay could replace a special sound's previous level with `-500`. A reconstruction should retain native raw levels per output wherever special levels need to survive setting changes.

### From a character's sound request to playback

During battle, `playPlayerSoundEffects` processed both fighters. A byte at `Character + 0x2C8` requested a character sound numbered 0–49. The byte at `+0x2C9` requested a common sound numbered 0–64. EFZ looked up the appropriate buffer index, called `playSoundBuffer` with loop flag zero, then cleared the request to -1. In the inspected screen state equal to two, it cleared both requests directly.

Our hooks operated after this selection, at the loader, volume and playback functions. The game's request selection and consumption continued to decide which sound played. The playback function stopped the selected buffer, reset its cursor to the beginning and played it. Applying gain immediately before that function covered the sound the character actually requested. Sources `playPlayerSoundEffects` and `playSoundBuffer`, corroborated by their native instruction sequences.

This refactored excerpt shows normal battle sound processing. The separate screen-state-two branch clears both request bytes for both fighters.

```cpp
for (Character* fighter : battle.fighters) {
    const int common = fighter->pendingCommonSound;
    if (common >= 0 && common < 65) {
        PlaySoundBuffer(battle.soundManager,
            battle.gameSystem->commonSoundBuffers[common], 0);
        fighter->pendingCommonSound = -1;
    }

    const int characterSound = fighter->pendingCharacterSound;
    if (characterSound >= 0 && characterSound < 50) {
        PlaySoundBuffer(battle.soundManager,
            fighter->soundBuffers[characterSound], 0);
        fighter->pendingCharacterSound = -1;
    }
}
```

### Loading and releasing a character's bank

Native resource initialization called graphics, animations and then character sounds while `Character + 0x30E8` was zero. It then marked the resources initialized. Slider updates therefore operated on existing buffers. Calling the loader again just to change volume would allocate another sound bank and overwrite the table before releasing its old entries.

Character cleanup released each table entry other than sentinel 150. Buffer release also cleared the manager's slot and freed its sample memory. Later loads could reuse those slots. Refresh operations consequently resolved the current fighter, table entry and receiver each time. Sources `initializeCharacterResources`, `cleanupCharacterObject`, `releaseSoundBufferAndMemory` and `releaseDirectSoundBuffer`, corroborated by their native instruction sequences.

### Shared sounds had their own table

GameSystem held 65 common-SE indices at `+0xEA4`, with its manager at `+0x8`. The native loader attempted the shared SE WAV files numbered `00.wav` through `64.wav`, stored each buffer index and supplied native level `-500`. `loadSoundEffects` followed the same original-first sequence and applied the SE setting after loading. Common playback mapped the requested sound through this table, including environmental sound requests. Sources `loadSoundEffects` and `playSoundEffect`, corroborated by their native instruction sequences.

The implementation validated the concrete receiver on every operation. Index 150 marked an unused slot. Accepted indices were 0–149, followed by checks for a buffer pointer, readable vtable and executable COM methods. Loading a different character could reuse the same numeric slot, so the current manager/table supplied the current receiver.

## How volume and fades were calculated

EFZ supplies a volume level for each sound. Music fades change this level over time. This guide calls that value the **raw level**. The **output level** is the value sent to Windows after applying the player's setting. BGM and SE are the two audio categories, called **lanes** in the old code.

DirectSound expresses volume in hundredths of a decibel. Zero means the sound's original level, and `-10000` is the bottom of the supported range. The training sliders ran from 0 to 100 percent. Their conversion was `round(2000 × log10(percent / 100))`, with zero percent handled as mute. At 23 percent the adjustment was `-1277`, and at 50 percent it was `-602`. The final level was the raw level plus this adjustment, clamped to `[-10000, 0]`. Sources `audio_gain_core.h` and `PercentToDirectSoundVolume`, matching the `IDirectSoundBuffer::SetVolume` contract.

For example, an EFZ fade sample of `-700` at 23 percent became `-1977`. The next native fade sample still supplied the next raw level. This let the game control the shape and timing of the fade while the user setting reduced its output.

The callback path used a precomputed 101-entry gain table and integer arithmetic. The compatibility converter used the same table for percentages up to 100 and raw levels within the native range. Its out-of-range fallback used `pow`, `log10` and `lround`. Zero or negative percentages produced mute. A lane owned by another provider passed through its supplied integer. An owned lane clamped raw input to the native range before applying gain, including at 100 percent. The gain lookup treated percentages of 100 or more as zero attenuation.

The integer gain operation below is the removed implementation. Source `audio_gain_core.h`, `EfzAudioApplyGain`.

```cpp
inline int EfzAudioApplyGain(int nativeLevel, int gain) noexcept
{
    if (nativeLevel < -10000) nativeLevel = -10000;
    if (nativeLevel > 0) nativeLevel = 0;
    if (gain <= -10000) return -10000;
    if (gain > 0) gain = 0;
    const int value = nativeLevel + gain;
    return value < -10000 ? -10000 : value;
}
```

### Keeping music fades smooth

Reading a buffer's current volume can return a value that already includes the user's reduction. Applying the reduction again would make the music progressively quieter. The implementation therefore kept one BGM cache, associated with the manager pointer, concrete buffer pointer and buffer index.

| Cache state | Meaning and next action |
|---|---|
| `Unobserved` | A new output needs its first raw level. A successful `GetVolume`, including zero, can seed it. |
| `Known` | A trusted raw sample exists. Slider changes and repeated playback calculate from that saved sample. |
| `AwaitingNativeSample` | Ownership or a late write has made the current output ambiguous. Wait for a fresh native volume call. |

A changed manager, buffer pointer or index reset the cache. Loading a track also reset it while training owned music, covering a new track that reused the same address and slot.

The native volume hook observed the incoming BGM sample before applying gain. When training owned the lane, it saved the raw level and marked that gain might have been applied. That marker preceded completion of the output write. A sample observed while gain was disabled or foreign-owned moved the cache to `AwaitingNativeSample`.

If the first `GetVolume` failed, a zero-percent setting could still mute immediately. A successful mute moved the cache to `AwaitingNativeSample`, so raising the slider waited for fresh raw evidence. Getter failures, uncertain raw state and failed final BGM writes requested another apply. An unavailable manager or buffer ended that attempt before requesting another one.

On retirement, the cache restored raw volume only when training owned BGM, gain might have been applied, raw was known, and the current BGM manager, index and concrete buffer still matched. It then entered `AwaitingNativeSample`, cleared the possible-gain flag and requested another apply, including after a failed restoration.

The cache also supported a handoff to another audio owner. Waiting for a fresh sample protected against an earlier owner's volume write finishing late. Returning to a nonmute setting could consequently wait for the next native sample.

### Applying a slider change

Both menu implementations used the same lane-setting helper. It took the settings locks, read the latest BGM/SE pair, changed one percentage, published the pair and released the locks. It then attempted one immediate direct apply. Multiple queued changes also collapsed into one pending apply request.

Direct application acquired the gameplay handoff mutex and then checked online mode and active sessions. An online result queued another apply and returned. The offline path then entered the recursive audio boundary and handled BGM, common SE and both characters. Missing sound readiness queued another attempt. Suppression ended the attempt immediately.

The custom menu exposed General → Audio and a Sound Settings shortcut. Its sliders supported steps of 1 and 10 and showed percentages. ImGui offered the same two sliders and Reset-to-100 buttons. Custom-menu Reload loaded settings and applied immediately. ImGui Reload relied on the queued application path. ImGui displayed the atomic percentages. The custom menu edited Config fields before publishing, so a failed shared write could leave it showing an attempted value while audio used the last accepted pair. Save could persist that attempted value.

## Reproducing the vanilla feature

### Establish the native entry points once

Use the verified Memorial executable identity and function signatures in the reference below. Resolve addresses from the module base and their RVAs. Keep the original function pointers and calling conventions for each detour.

The old installer created the hooks and then enabled them in order. Its common-site check accepted either pristine bytes or a leading jump, and a partial failure could leave earlier hooks installed. A reconstruction should establish who owns each entry and clean up a partially installed group before reporting installation failure.

The game performs all subsequent loading, playback and fading through those entry points. Install interception during initialization and use ordinary state changes for slider updates.

### Load the settings and catch sounds that already exist

Load the two percentages with a default of 100 and clamp them to 0–100. Publish them as one coherent pair. Initialize the raw BGM cache and the audio boundary before admitting callbacks.

Wait for GameSystem and its SoundManager/DirectSound object to become usable. Then apply once to the current BGM buffer, all common SE entries and each current fighter's sound table. This covers injection or settings initialization after the game has already loaded audio. The original delayed initializer used a 1,500-ms stabilization delay, followed by separate hook-resolution and sound-readiness retries of up to 20 attempts at 50-ms intervals.

Keep readiness and hook ownership separate. The old code could attempt direct application after a deferred or failed hook install. A new implementation should explicitly choose which successfully initialized paths may act. Individual buffer operations still need current receiver validation after readiness has first succeeded.

### Use the loader hooks for new resources

For `loadSoundEffects` and `loadCharacterSounds`, call the original first and apply the SE setting to the newly populated table afterward. For `playBackgroundMusic`, reset its raw cache before the original call, then bind/apply to the resulting buffer.

The music loader's own playback call passes through `playSoundBuffer`. That pre-play hook is the place to apply gain before the first audible sample. Post-load application also handles already-running or newly resolved output. A logical music track ID and the allocated sound-buffer index must be stored separately.

### Apply gain at the native volume and playback calls

At `setSoundVolume`, resolve the current buffer, determine its lane and capture the game's raw BGM request before calculating output gain. The old vanilla detour called the concrete buffer's COM SetVolume directly. Calling the intercepted entry again would recurse.

At `playSoundBuffer`, validate Play, SetCurrentPosition and Stop, apply the buffer's setting, then call the original with the original index and loop flag. Use the raw BGM cache for music. The historical SE path used `-500`, with the special-level limitation described above.

Preserve the game's target/current/rate fields and the fade function's native arguments. Writing the slider's gain into those fields and clearing the rate ends the game's fade. Our workaround did that earlier, and the native fade harness reproduced the resulting abrupt transition.

### Apply settings changes through the same path

Both UIs and file imports should update the same complete settings pair. After publication, apply through the same BGM/common-SE/P1/P2 helper used at startup. Keep disk work outside audio callbacks and serialize output work with the raw cache.

The original worker watched file changes and the monitor consumed coalesced apply requests. This supported immediate slider changes and deferred application when audio was temporarily unavailable. Keep UI display, accepted runtime settings and saved settings consistent when a write fails.

### Follow resource lifetime and test the whole sequence

Resolve each character's current table after loading and on later setting changes. A reused slot or address can hold a different sound. Reset music cache identity on a new manager, receiver, index or track load. The old implementation depended on those identity checks and native lifecycle operations. Its test-only output registry explored explicit retained lifetimes and application receipts.

Test both fighters, including the same character on both sides, character changes and repeated match loads. Exercise every character-sound table entry that can be loaded, shared effects, ordinary and looping playback, track changes, fades and mid-fade slider changes. Include missing buffers, zero-index buffers, mute/unmute and output failures. The test and evidence section identifies which parts already have isolated coverage.

## Historical behavior to preserve or correct

### Callback completion and failures

The vanilla BGM hook recorded the requested logical track before its original call. The public `PlayBackgroundMusic` helper recorded it after a successful native call and used the original trampoline when available. Its success result described native playback even when volume application deferred or failed.

The common/character loader hooks were void. A native exception ended follow-up, while a missing original pointer still allowed follow-up. The playback hook returned the original result or the buffer index on invalid receiver, missing original or native exception. The setter returned zero only for exact COM HRESULT zero, otherwise the buffer index. Index zero therefore made those fallback results ambiguous.

The fade hook returned the original result or zero on a missing/faulting original. The playback/fade SEH wrappers produced an `originalOk` value that their callers discarded. Public buffer writes forwarded the supplied integer, with clamping performed by the gain path. Buffer COM exceptions became failure results. Bulk SE writes skipped unreadable entries and discarded failure counts.

### Admission, retries and cleanup

Every admitted callback entered a recursive audio boundary, read its settings there and released it through `__finally`. The native gate also checked admission and passthrough. This covered callbacks selected just before admission closed. Ordinary Practice cleanup treated the audio hooks as process-lifetime participants.

The old standalone-host check required a recognized parent to be stable for 2,000 ms. Unsupported launch conditions could defer installation, with a 60,000-ms unproven-host timeout for selected cases. Exact pristine bytes were required for the contested vanilla playback/setter pair. A conflict latched suppression. Installed groups returned ready on later checks, leaving later ownership changes outside revalidation.

The legacy frame monitor retried deferred host ownership once per second outside scoped-monitor mode. It consumed queued applies after online parking checks and before the disabled-feature/menu early exit. Its normal target interval was 5,208,333 ns, subject to lifecycle/idle waits.

Dynamic unload needed further work. The shared hook registry required a valid retirement proof before reclaiming callbacks. The audio-specific shutdown path supplied an empty proof for its cross-mod callbacks and retained those records. Explicit DLL unload later called `MH_Uninitialize`. Process termination skipped that cleanup branch. A standalone reconstruction needs a complete drain/retirement path before offering unload or hot replacement.

### Quiet failures and unused helpers

Logs covered installation failures, unexpected bytes, host deferral/timeout, ownership conflicts and retained cleanup state. Byte dumps were capped at 16 bytes. Some failures remained quiet, including COM exceptions, discarded SE write counts and failed volume follow-up after loading.

`TraceAudioBuffer`, `SehSetSoundVolume`, the local `ClampPercent`, `GetConfiguredBgmFadeBaseVolume` and `ResolveSetSoundVolume` had no callers in the historical source/header/test search. Public direct-DS gain getters and public readiness setter/getters also lacked outside callers in that search. ImGui used the percentage getters, and menu playback used `IsCommonSoundEffectReady`.


## Why Revival should own its volume controls

The training volume feature was removed because our volume handling conflicted with how Revival manages audio during rollback and netplay. The fade problems came from our attempts to work around that conflict. Entering netplay also handed audio back to Revival and removed the music attenuation we had applied.

### What we know

Revival intercepts EFZ's playback and volume operations and chooses how to play the sound. It handles ordinary sound buffers and decoded music through different output paths. A music restart can reuse an existing output that still carries its previous volume.

Rollback restores engine state containing sound-related fields, while audio output objects have their own lifetimes. Audio calls can execute during catch-up even when rendering is suppressed. An external volume controller therefore has to agree with Revival about which sound is current, which volume is the game's fade value, and when output changes can safely complete. These facts come from the inspected savestate copies, battle audio calls and graph lifetime handling.

Our implementation added cached raw volumes, direct writes after settings changes and a handoff between offline control and native netplay behavior. Retiring our music gain during that handoff restored the louder raw level. An earlier workaround wrote the configured gain into the game's fade state and cleared its fade rate, interrupting native fades. These were consequences of fitting an external controller around Revival's audio lifecycle. The removed handoff is captured in `SuspendForNetplay`, `RetireBgmVolume` and `SetNativePassthrough`.

### Why external changes carry a desync risk

EFZ has a native throw calculation that consumes partly uninitialized stack data. It fills four entries in a twenty-entry contact table, then reads all twenty. That table affects contact bounds and effect placement, with subsequent effects involving RNG. Audio calls can leave different data in memory later used by that calculation. The native player-contact instruction sequence establishes the partial initialization and later reads.

In a controlled experiment, changing the volume passed to a real Windows sound buffer changed that stack residue, even with an adapter that preserved its own stack depth. Replaying the captured data through the native throw calculation produced different contact bounds and effect positions. Ordinary-hit control results matched.

That establishes a concrete compatibility hazard. Full intervening frame execution and live-match RNG divergence still need measurement. The experiment used controlled stack placement and synthetic geometry, so those limits remain part of the finding. The executable evidence comes from `native_output_probe.cpp` and the native-instruction replay in `replay_output_throw_residue.py`.

For the training mod, the decision is to leave Revival's audio output under Revival's control. Adding external hooks or volume writes would bring back ownership and ordering problems, together with this desync risk. Keeping the user's volume preference consistent online needs a solution owned and validated by Revival.

### A possible approach for Revival developers

Revival could expose separate BGM and SE settings and apply them inside its existing output lifecycle. Keep the game's raw fade level separate from the player's gain, remember that raw level for each live output, and apply the selected gain before first playback and on native volume changes. Reused outputs need their saved raw level, while newly created outputs need a fresh lifetime.

Local volume preferences and output bookkeeping should live with Revival's presentation state. Rollback needs an explicit policy for restoring raw audio intent and handling playback during catch-up. Existing external gain owners would also need a coordinated handoff to Revival.

The stack-residue problem still needs a compatibility decision. Revival developers could investigate a fully initialized native throw calculation with an appropriate peer/replay compatibility policy, or a downstream gain stage that preserves simulation-side audio behavior. Either approach needs full gameplay and RNG comparisons with different local settings, rollback, spectators and repeated sessions before release.

## Technical reference

These are Memorial EFZ executable VAs at image base `0x400000`. The implementation stored the corresponding RVAs. Object and vtable offsets are hexadecimal bytes unless stated otherwise. The verified executable SHA-256 is `3a1616d6c76d2eea08a95bb198d853e96cb6b9b3266e277ca1d167426dd66481`.

### EFZ hook signatures and original bytes

The signatures below show the original function's explicit arguments. The receiver arrived in ECX. The detour added an unused EDX slot through `__fastcall`.

| Function | RVA | Receiver and explicit arguments | Return | Pristine prologue |
|---|---|---|---|---|
| BGM playback | `0x68B0` | GameSystem, `uint16_t track` | `void` | `55 8B EC 83 EC 34` |
| Common-SE load | `0x6780` | GameSystem | `void` | `55 8B EC 83 EC 34` |
| Character-SE load | `0x11AE0` | Character | `void` | `55 8B EC 83 EC 34` |
| Screen fade | `0x359C90` | ScreenEffect, `int palette`, `uint8_t direction`, `int base`, `int adjustment` | `int` | `55 8B EC 83 EC 14` |
| Buffer playback | `0xDE80` | SoundManager, `uint16_t index`, `int loop` | `int` | `55 8B EC 83 EC 08` |
| Buffer volume | `0xE5D0` | SoundManager, `uint16_t index`, `int level` | `int` | `55 8B EC 51 89 4D FC` |

Buffer playback is VA `0x40DE80`. Nearby VA `0x40DF50` releases a buffer. The BGM loader reaches wave loading at `0x40DFB0` and timing-data loading at `0x40E2B0` before playback.

### Object layout and COM calls

| Location | Stored value |
|---|---|
| EFZ base + `0x390104`, + `0x390108` | P1 and P2 character pointers. |
| EFZ base + `0x39010C` | Current GameSystem pointer. |
| GameSystem + `0x8` | SoundManager pointer. |
| GameSystem + `0xF26` | Current BGM buffer index, unsigned 16-bit. |
| GameSystem + `0xF28` | Target BGM level, signed 32-bit. |
| GameSystem + `0xF2C` | Current BGM level, signed 32-bit. |
| GameSystem + `0xF30` | Fade rate, signed 16-bit. |
| GameSystem + `0xEA4` | 65 unsigned 16-bit common-SE indices. Decimal offset 3748. |
| SoundManager + `0xC` | DirectSound object. |
| SoundManager + `0x4C0` | 150 concrete buffer pointers. Decimal offset 1216. |
| Character + `0x88` | Reference to the supplied SoundManager. Decimal offset 136. |
| Character + `0x94` | Character folder name used to locate its WAV files. |
| Character + `0x264` | 50 unsigned 16-bit SE indices. Decimal offset 612. |
| Character + `0x2C8` | Pending character-sound number, with -1 indicating no request. |
| Character + `0x2C9` | Pending common-sound number, with -1 indicating no request. |
| Character + `0x30E8` | Resources-initialized flag. |

Valid buffer indices were 0–149. Index 150 served as the cache's invalid index. The BGM cache also stored raw level, state and a possible-gain flag. The direct COM setter used `int __stdcall(void* receiver, int level)`.

| Interface | Vtable byte offset | Operation |
|---|---|---|
| DirectSound | `0x18` | SetCooperativeLevel, checked for sound readiness. |
| DirectSoundBuffer | `0x18` | GetVolume. |
| DirectSoundBuffer | `0x30` | Play. |
| DirectSoundBuffer | `0x34` | SetCurrentPosition. |
| DirectSoundBuffer | `0x3C` | SetVolume. |
| DirectSoundBuffer | `0x48` | Stop. |


The raw decompiler expresses the native setter guard as `if(index)`. Exact instructions compare against sentinel 150. Buffer zero is valid and must receive volume updates. The refactored native guard is `if (bufferIndex != 150)`. Source `setSoundVolume`, verified against its instruction bytes.

### Native fade paths

At VA `0x4079C0`, EFZ advances current BGM level toward target by the positive rate, clamps at target, clears the rate on completion and sends the sample through `0x40E5D0`.

VA `0x759C90` emits 21 blocking transition samples. In the tested cases they followed `base − adjustment × sample / 2`, with native integer rounding, for sample 0 through 20. Both visual directions produced that sequence. VA `0x765FA0` emits `current − progress × 50` while progress is below 100, then advances progress. These two paths preserve the stored target/current/rate values.

The fade harness used rates 10, 20, 100 and 1000. It executed original x86 instructions with synthetic objects and supplied state. Clock/render boundaries and the final volume call were stubbed for that harness.

### Native character and common-sound call chain

| Function VA | Role |
|---|---|
| `0x410B70` | Character resource initialization, guarded by the initialized flag. |
| `0x411AE0` | Load the character bank and set its native levels. |
| `0x406780` | Load the common bank and set its native levels. |
| `0x406860` | Map a common logical sound to its allocated buffer. |
| `0x764DB0` | Consume both fighters' common/character sound requests. |
| `0x40DE80` | Stop, rewind and play an allocated buffer. |
| `0x40E5D0` | Set an allocated buffer's level. |
| `0x40ECB0` | Character cleanup, including its loaded sound indices. |
| `0x40DF50` | Release buffer and associated sample memory. |
| `0x40DC50` | Release the DirectSound receiver and clear its manager slot. |

## Settings shared with another mod

The bridge had two jobs. It exchanged the BGM/SE percentages through an INI file, and it negotiated which module could transform each audio lane. These states were independent. A usable shared file could coexist with incompatible gain ownership.

### Local settings and publication

`efz_training_config.ini` sat beside the training DLL, with executable-directory and current-directory fallbacks. Its `[General]` keys were `bgmVolumePercent` and `seVolumePercent`, each defaulting to 100 and clamped to 0–100.

Ordinary file loading accepted hexadecimal `0x` values and decimal `std::stoi` values. Missing keys used 100. A malformed existing value became zero after a parse error. The special Config setter instead used 100 after a parse exception. It recognized the General section and keys case-insensitively, read the current pair, replaced one lane, normalized both lowercase map entries and published once.

The mirror helper only updated Config fields and its map. Full-load publication and the one-lane setter acquired the shared file transaction lock first. A failed setter lock still returned “handled,” ending that setter attempt.

Saving used the normal whole-config temporary file, checked close/write status and `MoveFileExA` with replacement and write-through flags. Failure attempted temporary-file cleanup. Shared-file imports updated the in-memory Config mirror. The normal Save action persisted that mirror to the training INI.

### The shared file and its transaction marker

Discovery checked loaded modules in this order.

`EFZConfigExtension.dll`, `efz_config_extension.dll`, `EFZ_Config_Extension.dll`, `EFZExtendedConfig.dll`, `ExtendedConfig.dll`, `InGameControlsRebind.dll`.

It searched beside the loaded companion first, then the working directory and recognized mod subdirectories, deduplicated case-insensitively. In each directory it tried `efz_extended_config.ini`, `EFZExtendedConfig.ini`, `EFZConfigExtension.ini`, `extended_config.ini` and `InGameControlsRebind.ini`. The first regular file won. The fallback was `efz_extended_config.ini` beside a loaded companion, or in the current directory.

| Shared field | Interpretation |
|---|---|
| `[Shared] version` | Status metadata, default zero. |
| `[Shared] enabled` | Sharing switch, default one. |
| `[Shared] audio` | Audio switch, falling back to `[Audio] enabled`, default zero. |
| `[Shared] controls` | Controls status, falling back to `[Controls] enabled`, default zero. |
| `[Audio] bgmVolumePercent` and `seVolumePercent` | Each available when its parsed integer is within 0–100. |
| `[Audio] revision` and `[Controls] revision` | Recorded revisions, default zero. |
| `[Audio] writeInProgress` | Guards acceptance of a partially written pair. |

Audio import required enabled sharing and at least one valid percentage. A missing or invalid lane retained its previous atomic value. Parsing used `GetPrivateProfileIntA`, followed by range validation. Version and revision were recorded as metadata. Controls fields described status, with audio being the bridge's implemented publication path.

A writer set `writeInProgress=1`, wrote BGM, wrote SE, incremented revision, then cleared the marker. Revision wrapped from `INT_MAX` to one. Only a complete successful sequence updated runtime settings and the mirror. A later failure left the marker set and retained the last accepted tuple. A later complete explicit write could repair it.

Readers checked the marker before and after reading. A missing or disabled file retained runtime percentages. Refresh cleared the file-found and audio-availability flags, while some cached controls metadata and numeric values could persist in status. An unfinished marker returned before replacing status or publishing.

Without bridge admission or a discovered companion/file, publication updated local runtime settings and the mirror, then returned false. Otherwise it attempted the shared write, including when the file's sharing switches were disabled. Those switches gated imports, while publication preserved their values. Actual shared-write failure also returned false. The UI lane helper still attempted direct application after either outcome, using the last accepted settings. A failed outer file-lock acquisition ended the lane helper before that apply.

### The directory worker

Initialization discovered the path once. `Refresh(force)` ignored its force argument and reused that path. The worker watched filename, last-write and size changes in the selected file's nearest existing directory ancestor, without subtree watching.

It waited indefinitely for stop or a notification, rearmed the notification before import, then applied volumes if the accepted percentages changed. A failed watch produced a stop-only worker. Wait/rearm failure ended the worker. Worker creation failure happened after the admitted flag had been set, so another initialization call returned through the already-admitted path. Recovery required lifecycle work beyond the implemented worker.

`StopAudioControl` could signal, join and close worker handles. Its implementation retained the provider reference/boundary and admitted flag. Production DllMain used the stop signal. The full-stop method, bridge status accessors and `RequestTrainingGainOwnership` had available APIs but no production caller in the historical search.

### One coherent runtime snapshot

An aligned, lock-free 32-bit atomic stored BGM in bits 0–6, SE in bits 7–13, their two training-ownership flags in bits 14–15 and a wrapping publication counter in bits 16–31. Initial settings were 100/100 with both lanes locally owned.

Publication clamped percentages and derived ownership from acknowledged provider state plus disabled lanes. A changed lower 16-bit tuple incremented the counter, performed a release store and requested application. Readers used acquire loads. Apply requests used a coalescing boolean consumed with an acquire-release exchange.

Closing netplay gain admission cleared ownership in the callback's returned view while retaining the stored percentages and acknowledged tuple. The admission boolean lived under the audio boundary. Boundary enter/leave preserved `GetLastError` and used either a local recursive critical section or the companion's recursive boundary.

### Sharing ownership safely

The bridge looked for export `EfzGetAudioOwnerV1`. Its API supplied enter, leave, query and retire callbacks. Training retained the provider DLL, checked API size/version, a nonzero provider identity and every function pointer, queried the record, bound the provider boundary and acknowledged it inside that boundary.

Lane bits were BGM=1, SE=2 and both=3. A record identified provider-owned lanes and lanes guaranteed to arrive raw. Those masks had to be disjoint. Unclassified lanes disabled training gain. An installed companion with an unacceptable contract disabled both training lanes and logged a compatibility error.

Acknowledgement rejected invalid size/version, zero provider identity or generation, unknown/overlapping lane bits, a changed provider identity, an older generation, or different masks at the same generation. This prevented an old or conflicting acknowledgement from granting transform ownership.

The available ownership-transfer helper first published the requested percentages, queried the current record and asked the provider to retire selected lanes. Its commit accepted the same provider and exactly the next generation. The transfer contract required draining prior transforms and committing the new owner before releasing the shared boundary.

The provider implementation included in training's tests began at generation one and froze initialization after its first query. Retirement required an exact expected record, a valid mask and commit callback. It rejected self-draining from a thread already inside the boundary. Under SEH-finally protection it removed provider-owned bits, marked the lanes raw, advanced generation and committed before unlocking. Record mutation preceded the commit callback.

### Lock order

Bridge settings operations acquired the module's recursive control mutex, then `Local\EfzAudioConfigV1-<PID>`, then the audio boundary. Config's load-publication and special setter began with the file mutex, then entered the audio boundary. The named Windows mutex serialized cooperating DLLs within the process. Callbacks operated inside the audio boundary, with file work confined to the control side.

The file mutex waited indefinitely. An abandoned mutex was acquired for release/cleanup but declined as a valid transaction. Import held its transaction through runtime publication and mirror update. Nested bridge operations depended on recursive module and Windows mutex behavior.

Native callbacks and provider retirement used explicit `__try`/`__finally` cleanup. The bridge's ordinary control paths used C++ locks. Foreign provider calls and the worker body depended on their callees completing normally because they had no surrounding native-exception shield.

### Ownership ABI layout

The following x86 layout follows the historical declarations. A fresh compiled-layout probe remains useful before reusing the ABI. All callbacks used `__cdecl`.

| Structure | Fields and byte offsets | Size |
|---|---|---|
| `EfzAudioOwnerV1` | size +0, version +4, provider incarnation +8, owned mask +16, raw-input mask +20, acknowledgement generation +24. | 32 bytes |
| `EfzAudioOwnerApiV1` | size +0, version +4, provider incarnation +8, enter +16, leave +20, query +24, retire +28. | 32 bytes |

Size/version/masks were unsigned 32-bit. Provider incarnation and acknowledgement generation were unsigned 64-bit. Retire received the exact expected record, requested lane mask and commit callback. Boundary binding was setup-only, before callbacks/workers, with the provider retained for their lifetime.

## Prototype code included in the removal

Three headers preserved alternative designs exercised by isolated tests. Their production integration status matters when reusing them. The active DLL used its BGM cache and companion-export ownership bridge.

### Output-lifetime registry

`audio_output_lifetime.h` was included only by its dedicated test. It modeled 150 DirectSound bindings and eight decoded graphs. Each binding held owner, receiver, optional graph control, slot, output kind, lane, lifetime incarnation, raw generation, applied generation, raw level, settings receipt and known/applied flags. Operations supplied retain, release, current-binding validation and a setter.

`Created` required an owner and slot below 150, retired that owner/slot's previous binding, retained the new receiver and assigned a fresh incarnation. A failure after retirement left the old binding released. `GraphReady` required context/control/audio. The same triple reused its binding. A replacement graph for the context released the previous reference and created a fresh decoded BGM binding.

`Observe` advanced raw generation on the first sample or a changed raw level/lane. `Prepare` required known raw state and classification, validated current identity, honored lane ownership and skipped an already successful write when raw generation and that lane's percent/ownership matched. Foreign ownership cleared the applied flag. A stale receiver was released.

Receipts accepted nonnegative setter results. A failure cleared success and incremented a failure counter, allowing retry. `ApplyLatest` visited both arrays. Retirement supported one slot, a manager, a graph or all entries. BGM rebase replaced its raw values and invalidated receipts. Release cleared the binding before invoking the external release callback.

The proposed thread model assigned binding operations to the native audio owner and limited a file worker to publishing settings/wake requests. Capacity exhaustion, failed retention, multithreaded lifetimes and real COM destruction remained additional test cases.

### Five x86 endpoint adapters

`audio_native_adapters.inl` was included only by its ABI test. Each naked adapter used saved flags/registers around helper callbacks, replayed displaced instructions and jumped to a supplied continuation. Installation required a qualified whole-instruction patch window owned by the caller.

| Adapter | Operation |
|---|---|
| `AudioDsPlayAdapter` | Prepare from saved frame, place EAX receiver at `[esp]`, call `[edx+0x30]`, continue. |
| `AudioGraphRunAdapter` | Prepare, read EAX's vtable, place receiver at `[esp]`, call Run through `[edx+0x1C]`, continue. |
| `AudioDsCreateAdapter` | Observe creation, replay `[ebp-0x3C]=eax` and comparison with zero, continue. |
| `AudioSplitSetAdapter` | Adjust saved ECX, write it to `[esp+4]`, call `[edx+0x3C]`, capture the result through another saved frame, continue. |
| `AudioLoopPlayAdapter` | Prepare, read ECX's vtable, push EDX receiver, call `[eax+0x30]`, continue. |

These test adapters used `pushfd/pushad` around helpers. The later controlled-stack output probe used a separate adapter design with existing outgoing argument storage. Each experiment's stack behavior needs its own evidence.

### Provider helper and leaf exports

`audio_owner_provider.h` supplied the provider state machine exercised by runtime-state tests. The production bridge obtained its API from the companion module's export.

`audio_gain_leaf.cpp` exposed three noinline cdecl wrappers for manual x86 disassembly. `EfzAudioGainLeaf` applied integer gain, `EfzAudioResolveGain` looked up percentage gain and `EfzAudioConvertGain` called the compatibility converter. It had no CMake target or executable assertions.

## Tests and evidence

### Deleted tests and their actual scope

The deleted suite registered six executables. Configured-apply and native-adapter tests had MSVC/x86 guards. File-transaction tests required two source files from the sibling ExtendedConfig checkout and linked MinHook. The gain-leaf file supported manual disassembly.

| Test | Coverage | Remaining limits |
|---|---|---|
| `audio_volume_contract.cpp` | Exhaustive gain comparison for percentages 1–100 and raw levels -10000 through zero, mute/unity, monotonicity, out-of-domain cases and floating-point flags. Validated buffer success/failure cases, including positive nonzero HRESULT and SEH. BGM startup, repeated playback, fades, slider changes, mute/unmute, reused slots, ownership changes, late writes, getter/retirement failures and preserved fade fields. | Memory and COM were modeled. Optional native-trace replay required an explicit filename. |
| `audio_configured_apply_tests.cpp` | Online/session rejection, pending apply, offline BGM/common/character writes, suppression/readiness, recheck under the handoff lock, draining in-flight apply/fade callbacks, ordered suspend/resume and SEH lock release. | Native output writers were stubs. |
| `audio_runtime_state_tests.cpp` | Raw-level continuity, native SE baseline, foreign ownership, clamping, LastError preservation, provider registration/drain/commit, stale acknowledgement, self-drain rejection, ownership freeze, 100,000 alternating coherent publications, counter wrap, coalescing, lane classification, admission and SEH cleanup. | Selected malformed-record cases covered only part of the full validator. Discovery and file operations used spies. |
| `audio_file_transactions_tests.cpp` | Real Win32 profile/file operations and actual training/companion writers. Pair coherence under interleaving, blocked Config/UI writes, ordinary INI edits, mirror normalization, interrupted writes, failure at each write position, abandoned mutex recovery, explicit repair and publication before ownership acknowledgement. | Direct volume apply was stubbed. Companion source came from the sibling checkout at build time. |
| `audio_output_lifetime_tests.cpp` | Owned references, duplicate-write suppression, fade invalidation, unity restoration, failed-set retry, reused-slot incarnation, SE separation, graph first play/reuse/rebase/retirement and stale-current release. | Fake output objects. Capacity, retain failure and concurrent destruction need additional tests. |
| `audio_native_adapter_abi_tests.cpp` | Synthetic naked COM endpoints verified arguments, receivers, private/native call counts, exact stack cleanup, EBX/ESI/EDI preservation, Play/Run results, split-set adjustment and failing HRESULT receipt, loop arguments and creation result storage. | Real patch-window installation and native code paths require separate qualification. Explicit flag assertions concerned playback arguments. |

The file-transaction harness used `audio_transaction_training.cpp` to include the real bridge and publication helpers with a small Config fixture. `audio_transaction_companion.cpp` included the companion's actual store/control sources and ownership provider, inserting deterministic read/write/activation barriers. `audio_transaction_test_support.h` declared the tuple, wrappers and synchronization functions.

The optional volume-contract trace reader accepted rows of `scenario tick raw target current rate`, reset per scenario and replayed each sample at 0/23/50/100 percent. It required complete parsing and at least one sample. CTest invoked this executable without a filename, so native-trace coverage came from a separate invocation.

Adjacent source-contract checks covered process-lifetime audio admission, suspension order, track reset before playback, BGM helper memory-write policy and resume order. Localization checks covered the audio explanation and generated Japanese strings. The removal also changed these callers/checks and removed nine Japanese entries.

After removal, the retained native-hook fixture exercises OST and menu-sound production paths against a loaded image through the real hook registry and MinHook. It checks logical track versus buffer identity, original arguments, offline observation, online native bypass, failed playback, OFF selection, fade fields and unavailable buffers. Live transitions and two-peer netplay remain separate runtime checks.

### Historical results and how to repeat the checks

The native fade harness reproduced interrupted ramps and generated the raw samples. The separate helper replay applied the removed gain/cache code to those samples. These measurements describe the investigation. A new implementation should repeat the checks against its own callbacks and outputs.

- The native fade verifier executes original fade instructions and records their samples. Its JSON summary and raw trace preserve the 1,940-sample run. To repeat it, capture native raw levels and target/current/rate before and after each fade step, then compare the adjusted output against the gain formula at 0, 23, 50 and 100 percent.
- A later helper harness fed those native samples through real stopped DirectSound buffers at four gains. It recorded 23,334 GetVolume checks with zero failures. To repeat it, apply each captured sample to a real stopped buffer, read its level back and compare it with the expected clamped result. Keep fade-state checks separate from output-level checks.

The [Revival section](#why-revival-should-own-its-volume-controls) explains the output experiment and its compatibility limits.

The fade-field workaround and its failure are described above. Reproduce the sample-based gain path when evaluating vanilla volume control.

## Removed files and their responsibilities

The inventory covers all 30 removed files. The use column identifies runtime code, tests and prototypes. Each responsibility is explained in the sections above.

| Historical file | Use in training | Responsibility |
|---|---|---|
| `audio_bgm_volume.inl` | Runtime | Three-state raw cache, binding, observation, mute recovery, pre-play application and retirement. |
| `audio_buffer_volume.inl` | Runtime | Concrete buffer resolution, validated COM get/set, HRESULT handling and SEH fallback. |
| `audio_callback_dispatch.inl` | Runtime | Recursive boundary entry with native-exception-safe final release. |
| `audio_config_publication.inl` | Runtime | Config mirror, loaded-pair publication and case-insensitive one-lane Config setter. |
| `audio_configured_apply.inl` | Runtime | Ordered gameplay/audio locks, online checks, bulk apply and netplay suspend/resume. |
| `audio_control.h` | Runtime API | Hook phases/results, playback, readiness, direct apply, suspension and gain getters. |
| `audio_file_control.h` | Runtime | Process-named file mutex, acquisition checks and abandoned-transaction handling. |
| `audio_gain_conversion.inl` | Runtime/helper tests | Compatibility converter with integer supported-domain path and floating-point fallback. |
| `audio_gain_core.h` | Runtime | 101-entry percentage gain table, integer adjustment and native-range clamp. |
| `audio_lane_adjustment.inl` | Runtime | Validate receiver, classify BGM/SE, observe raw music and calculate effective level. |
| `audio_lane_gain.inl` | Runtime | Ownership-aware lane gain, owned-input clamping and foreign-owner raw forwarding. |
| `audio_native_adapters.inl` | Test-only prototype | Five x86 endpoint adapters with explicit register/stack and continuation handling. |
| `audio_output_lifetime.h` | Test-only prototype | 150 buffer/eight graph registry, retained receivers, incarnations, raw generations and application receipts. |
| `audio_owner_abi.h` | Runtime ABI | Provider record, masks, recursive-boundary callbacks and retire/commit contract. |
| `audio_owner_provider.h` | Test-only in training | Provider registration, exact-generation retirement and commit-under-lock implementation. |
| `audio_runtime_state.h` | Runtime API | Packed snapshot layout, ownership/admission operations and callback-facing access. |
| `extended_config_bridge.h` | Runtime API | Shared status fields, import/publication, transfer and worker control declarations. |
| `audio_control.cpp` | Runtime | Six vanilla detours and two alternative cross-mod callbacks, host validation, native receiver checks, common/character SE application, track playback and shutdown. |
| `audio_runtime_state.cpp` | Runtime | Atomic publication, pending apply, admission, recursive boundary and ownership acknowledgement. |
| `extended_config_bridge.cpp` | Runtime | Discovery, shared-file import/publication, companion ABI binding, directory worker and UI lane helper. |
| `audio_configured_apply_tests.cpp` | Test | Direct apply, online admission, handoff serialization and SEH cleanup. |
| `audio_file_transactions_tests.cpp` | Test | Actual INI writer interleaving, fault injection, abandoned transactions and recovery. |
| `audio_gain_leaf.cpp` | Manual disassembly helper | Three exported noinline gain wrappers for release-x86 inspection. |
| `audio_native_adapter_abi_tests.cpp` | Test | Synthetic x86 COM endpoints, arguments, cleanup, return values and nonvolatile registers. |
| `audio_output_lifetime_tests.cpp` | Test | Reference ownership, receiver reuse, receipt deduplication/retry and graph lifetime. |
| `audio_runtime_state_tests.cpp` | Test | Publication, ownership, provider transitions, lane routing and callback boundaries. |
| `audio_transaction_companion.cpp` | Test support | Actual sibling companion store/control implementation with deterministic barriers. |
| `audio_transaction_test_support.h` | Test support | Tuple, read/write intercepts, harness operations and synchronization declarations. |
| `audio_transaction_training.cpp` | Test support | Actual training bridge/publication implementation with Config and output-apply fixtures. |
| `audio_volume_contract.cpp` | Test | Gain conversion, validated buffer operations, BGM raw cache/fades and optional trace replay. |

### Callers and adjacent contracts

These code citations identify the production entry points and cleanup behavior described above. Line ranges refer to the removed implementation.

| Source | Relevant wiring |
|---|---|
| `dllmain.cpp`, lines 148–315 | Initial passthrough, common hooks, delayed host resolution, sound readiness and first application. |
| `dllmain.cpp`, lines 372–449 | Settings/bridge startup, worker stop signal and unload sequence. |
| `frame_monitor.cpp`, lines 1081–1236 | Online parking, deferred host retry and pending application. |
| `config.cpp`, lines 79–150 | INI location and integer parsing. |
| `config.cpp`, lines 802–1072 | Loaded-pair publication, saved percentages, atomic file replacement and special setting dispatch. |
| `screens_defs.cpp`, lines 1774–1794 | Custom-menu lane callbacks and audio summary. |
| `screens_defs.cpp`, lines 1918–1959 | Custom-menu sliders and General → Audio entry. |
| `screens_defs.cpp`, lines 6820–6834 | Top-level Sound Settings shortcut. |
| `config.cpp`, lines 1237–1247 | Hexadecimal and malformed-value parsing. |
| `imgui_settings.cpp`, lines 343–366 | ImGui volume sliders and reset buttons. |
| `utilities.cpp`, lines 1100–1103 | Netplay suspension call. |
| `utilities.cpp`, lines 1250–1263 | Post-cleanup netplay resume call. |
| `practice_runtime.cpp`, lines 61–62 | Process-lifetime treatment of audio hooks. |
| `minhook_utils.cpp`, lines 156–214 | Target module retention, native gates and removal request. |
| `hook_registry.cpp`, lines 299–305 | Retirement-proof check during reclaim. |
| `verify_legacy_netplay_handoff.py`, lines 13–40 | Source-contract checks for audio handoff and track reset. |
| `CMakeLists.txt`, lines 303–390 | Runtime/file/lifetime/adapter/configured-apply test targets and guards. |
| `CMakeLists.txt`, lines 529–535 | Volume-contract test registration. |
