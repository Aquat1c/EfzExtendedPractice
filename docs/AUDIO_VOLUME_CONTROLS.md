# Adding volume controls to EFZ

Vanilla Memorial EFZ plays music and sound effects through DirectSound. You can add separate BGM and SE controls by adjusting the volume sent to its sound buffers. The game already handles loading, playback and fades, so the main work is applying the user's setting at the right time and remembering the game's original volume.

This reference covers the native functions, character sound banks and buffer lifetime. The examples use readable names for fields recovered from the executable. Helper names such as `CharacterWaveName` abbreviate filename construction. The address tables give the layouts needed to connect those examples to the game.

## Where to apply the setting

A sound can load after your mod starts, and EFZ can change its volume later. Cover loading, playback and native volume changes so the setting follows the sound through all three.

| Native function | How to use it |
|---|---|
| `loadSoundEffects` | Call the original, then apply SE volume to the common sound bank. |
| `loadCharacterSounds` | Call the original, then apply SE volume to that character's bank. One shared function covers the roster. |
| `playBackgroundMusic` | Reset the previous music's cached volume, call the original and bind the resulting BGM buffer. |
| `playSoundBuffer` | Apply the setting immediately before calling native playback. This covers the first audible sample. |
| `setSoundVolume` | Save the game's requested level, apply the user's gain and send the result to the concrete buffer. |

`fadeWithSoundAdjustment` is also listed in the reference because it drives transition fades. The former implementation hooked it and forwarded its arguments. Its individual volume writes reach `setSoundVolume`, which is where the gain calculation belongs.

The native methods use `__thiscall`. An x86 C++ detour can use `__fastcall` with the original object in ECX and an unused EDX argument. Preserve the explicit arguments and return type shown in the table. When writing the adjusted level, call the concrete buffer's SetVolume method or a suitable original trampoline. Calling your intercepted entry again would recurse.

## Character sounds

Each fighter has a table of 50 possible sounds. Its SoundManager reference is at `Character + 0x88`, and its unsigned 16-bit buffer indices start at `+0x264`. Native character construction supplies the screen's shared manager. Each fighter has its own index table within that manager.

A logical sound number selects an entry in the character's table. The value stored there selects the actual DirectSound buffer. For example, character sound 7 might be allocated as buffer 92. Use the stored index when applying volume.

The shared character loader attempts WAV files numbered `00.wav` through `49.wav` using the character's folder name at `+0x94`. It stores each returned buffer index and gives it the native level `-500`. A missing file can return index 150. The following is a refactored view of `loadCharacterSounds`.

```cpp
void LoadCharacterSoundBank(Character& character)
{
    for (int sound = 0; sound < 50; ++sound) {
        const auto filename = CharacterWaveName(character.folder, sound);
        const uint16_t buffer = LoadWaveFile(character.soundManager, filename);
        character.soundBuffers[sound] = buffer;
        SetSoundVolume(character.soundManager, buffer, -500);
    }
}
```

Call this original loader first, then walk the resulting table to apply the SE setting. At that point the new buffers exist and their indices are available. Resolve a buffer through `manager + 0x4C0 + bufferIndex * 4`, accepting indices 0–149. Buffer zero is valid.

When the user changes SE volume, update the common bank and both current fighters' banks. Read the current P1 and P2 pointers again because a character change can replace those objects. Update their existing buffers. Calling the sound loader again would allocate another bank and overwrite the old indices before releasing them.

Native resource initialization loads graphics, animations and sounds while `Character + 0x30E8` is zero, then marks the resources initialized. Character cleanup releases its sound indices. Buffer release clears the manager's slot, allowing a later sound to reuse it. Sources `initializeCharacterResources`, `loadCharacterSounds`, `cleanupCharacterObject` and `releaseSoundBufferAndMemory`.

## How a character requests a sound

During battle, `playPlayerSoundEffects` checks both fighters. The byte at `Character + 0x2C8` requests a character sound numbered 0–49. The byte at `+0x2C9` requests a common sound numbered 0–64. EFZ resolves the buffer index, plays it with loop flag zero and clears the request to -1.

Here is the normal playback branch with the decompiled fields renamed. The separate screen-state-two branch clears both requests for both fighters.

```cpp
for (Character* fighter : battle.fighters) {
    const int common = fighter->pendingCommonSound;
    if (common >= 0 && common < 65) {
        PlaySoundBuffer(battle.soundManager,
            battle.gameSystem->commonSoundBuffers[common], 0);
        fighter->pendingCommonSound = -1;
    }

    const int ownSound = fighter->pendingCharacterSound;
    if (ownSound >= 0 && ownSound < 50) {
        PlaySoundBuffer(battle.soundManager,
            fighter->soundBuffers[ownSound], 0);
        fighter->pendingCharacterSound = -1;
    }
}
```

The volume hooks act at the shared loader, playback and setter functions. EFZ's sound requests continue choosing which sound plays. Native `playSoundBuffer` stops the selected buffer, resets its cursor and starts playback.

The common bank works similarly. `loadSoundEffects` loads 65 possible SE files, stores their indices at `GameSystem + 0xEA4` and sets their initial level to `-500`. Its manager comes from `GameSystem + 0x8`. Common and environmental sounds use this bank, so it needs the same SE adjustment as the character banks. Sources `loadSoundEffects`, `playSoundEffect` and `playEnvironmentalSoundEffects`.

## Volume and fades

DirectSound volume uses hundredths of a decibel. Zero is the original level, and `-10000` is the minimum. To turn a percentage into gain, use `round(2000 * log10(percent / 100))`, handling zero percent as mute. This gives `-602` at 50 percent and `-1277` at 23 percent. You can precompute the 101 values for a 0–100 slider.

Keep two values separate for each output. The **raw level** is what EFZ requested. The **output level** includes the user's gain. If EFZ requests `-700` during a fade and the setting is 23 percent, send `-1977` to the buffer.

This integer helper applies the gain after conversion. Source `EfzAudioApplyGain` in the former volume implementation.

```cpp
int ApplyGain(int rawLevel, int gain)
{
    rawLevel = std::clamp(rawLevel, -10000, 0);
    gain = std::clamp(gain, -10000, 0);
    return std::max(-10000, rawLevel + gain);
}
```

Save the raw value before adjusting it. A later slider change should calculate from that saved value. Reading the already adjusted output and reducing it again would make repeated playback or slider updates progressively quieter.

EFZ stores music's target level, current level and fade rate in GameSystem. Let the game advance those fields and apply gain to each resulting volume request. Writing the user's gain into target/current and clearing the rate ends the fade abruptly. Both blocking transitions and ordinary frame-driven fades need this treatment.

The native SE loaders use `-500`, but later native calls can supply another level. Preserve those requests too. The old implementation reapplied `-500` during bulk updates and before SE playback, which could overwrite a special sound's level. Keeping raw levels per buffer avoids that limitation.

## Startup, track changes and buffer reuse

Load the percentages before applying them, using 100 as the default and clamping input to 0–100. Once the game has a usable sound manager, update any existing BGM, common sounds and both fighters. Loader hooks cover resources created afterward.

The BGM loader releases the previous music, loads the new wave and timing data, then plays it. Its inspected body supplies no initial SetVolume call. Applying gain at `playSoundBuffer` handles the setting before that first playback. Applying only after the BGM loader returns can let the start of the track play at the previous level.

A logical music track ID and an allocated buffer index are separate identifiers. Identify the current BGM by matching both the GameSystem manager and its BGM buffer index. Other valid buffers take the SE setting.

Associate saved raw levels with the manager, concrete buffer and its lifetime. A slot or address can be reused. Clear the music cache on a new track even if its slot and pointer appear identical. Clear other cached entries when their buffers are released or replaced.

A fresh output can seed its raw level from a successful GetVolume when its origin is known. After an ownership change or uncertain late write, wait for a trusted native sample. Mute can be applied immediately, but raising the volume again needs a valid raw level.

Check setter results before recording a successful application. Keep failed updates retryable. Serialize slider changes with playback and cache updates, and keep file I/O outside those callbacks. Apply settings through one shared routine so startup, UI changes and reloads use the same calculation.

## Revival

Revival controls playback around rollback and intercepts EFZ's playback and volume operations. It also has separate paths for ordinary buffers and decoded music. Output objects can survive a music restart, while rollback restores engine sound-related state on a different lifetime.

Our volume handling conflicted with that behavior. The workarounds introduced fade problems, and handing audio back to Revival removed the attenuation applied by the mod. This is why the training mod's volume controls were removed.

There is also a gameplay concern. A native throw calculation fills four entries in a twenty-entry contact table, then reads all twenty. Sound calls can change stack data later consumed by that calculation. A controlled experiment changed the volume passed to a real Windows sound buffer and obtained different contact bounds when the resulting stack data was replayed through the native throw code. Full intervening frame execution and live-match RNG divergence still require measurement.

External audio hooks or volume writes would bring back these ownership and desync risks. We leave Revival's output under Revival's control. A native Revival feature could keep the user's gain alongside its existing sound lifetimes, preserve raw fade values and apply gain before playback. It would still need gameplay and RNG comparisons across different settings, rollback, spectators and repeated sessions. Resolving the partially initialized throw data or applying gain downstream both require their own compatibility work.

## Checking your implementation

Start with startup volume, character changes, both fighters using the same character, common effects and BGM track changes. Include buffer zero, missing sounds and reused slots. Check that each newly loaded sound starts at the selected level.

Then test fade-in, fade-out, mute/unmute and slider changes halfway through a fade. Compare each output against the saved raw level plus gain, and confirm EFZ's target/current/rate continue following the native sequence. Repeated playback should produce the same level each time.

Finally, exercise failed GetVolume/SetVolume calls and resource cleanup while settings change. The examples and native addresses here were checked against source and executable evidence. Playback quality and compatibility still need testing in the environment where your mod will run.

## Native reference


These are Memorial EFZ executable VAs at image base `0x400000`. Use module base plus RVA to resolve an entry point. Object and vtable offsets are hexadecimal bytes unless stated otherwise. The verified executable SHA-256 is `3a1616d6c76d2eea08a95bb198d853e96cb6b9b3266e277ca1d167426dd66481`.

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

Valid buffer indices are 0–149. Index 150 means an unused or failed allocation. COM methods use `__stdcall`, with the receiver as their first argument.

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
