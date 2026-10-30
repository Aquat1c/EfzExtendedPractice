# How EfzExtendedPractice works

A tour of what the mod does inside the game, for curious users and modders.
Symbols are named as they appear in the source, with the file that owns them.

## The short version

The mod is a 32-bit DLL that EFZ Mod Manager loads into the game process.
At startup it identifies the exact game build (vanilla Memorial efz.exe or one
of the EfzRevival wrapper generations), then installs a small set of hooks:
one on the place where the game reads player input, one on the frame loop,
one on the final draw call, and a handful of byte patches that are tracked so
they can be undone. Everything else - the training menu, frame data, macros -
is ordinary C++ code running off those hooks. While an online
session is live, every feature that could desync the match is switched off.

## Repository map

| Path | What lives there |
| --- | --- |
| `src/dllmain.cpp` | Entry point: `DllMain`, `DelayedInitialization` |
| `src/core/` | Memory access (`memory.cpp`), logging (`logger.cpp`), global state (`globals.cpp`), key codes |
| `src/game/` | Training features: auto-actions, macros, savestates, frame data, dummy control, character quirks |
| `src/game/mission/` | Experimental mission/tutorial engine, data model, rendering, tutorial sessions; compiled out by default |
| `src/gui/` | ImGui menu, overlay rendering, frame bar, custom menu |
| `src/gui/custom_menu/` | The hand-drawn Revival-look menu (fonts, layout, screens) |
| `src/input/` | Input hooks, input buffer control, motion patterns, frame stepping |
| `src/runtime/` | Hook registry, patch ledger, practice-mode isolation runtime |
| `src/utils/` | Config, audio, netplay detection, update check, controllers, misc |
| `include/` | Headers mirroring the above; `include/compat/` holds cross-mod ABI |
| `tests/` | Plain C++ policy tests run under CTest |
| `assets/` | Shipped data: fonts, control icons, movedata tables, mission packs |
| `3rdparty/` | Bundled MinHook, Detours, Dear ImGui, nlohmann/json |
| `tools/` | `build-task.ps1`, the build wrapper used by the VS Code tasks |

## Startup

`DllMain` (`src/dllmain.cpp`) installs the crash handler and spawns
`DelayedInitialization` on its own thread. At process exit the detach path
only sets flags, because it runs under the loader lock and the other threads
are already gone; an explicit `FreeLibrary` also runs a best-effort teardown.
`DelayedInitialization` sets up logging and config, initializes MinHook,
installs the feature hooks in stages, and restores the Final Memory and
timer-freeze patch baselines.

`src/utils/crash_handler.cpp` writes the report from a helper thread with its
own stack, so a stack overflow can still be reported. The minidump comes first,
before any symbol lookup that could stall.

Hooks and patches that depend on the game's memory layout check the build
first, and not every hook is gated the same way.
`GetQualifiedMemorialImage` (`src/runtime/native_game_profile.cpp`) admits
the executable only on an exact PE-identity and hash match, and guards the
byte patches, framestep and the collision layout. The audio hooks compare
entry bytes instead. `src/utils/network.cpp` detects which EfzRevival version (1.02e through
1.02j, including the two 1.02j build profiles in
`include/utils/efz_revival_profile.h`) is hosting the process. Per-version
addresses come from the accessors in `src/game/efzrevival_addrs.cpp`.

## How the game is modified

Two mechanisms, both reversible:

- **Function hooks.** MinHook detours on native game functions, managed by
  `PracticeHooks::Registry` (`src/runtime/hook_registry.cpp`) with preimage
  verification, admission control, and retirement receipts. A hook is only
  physically removed with a qualified receipt, which nothing issues yet. Until
  then a retired hook stays attached with admission closed, so its original
  stays callable while callbacks may be in flight, and `MH_Uninitialize` drops
  it at unload.
- **Byte patches.** `PatchLedger` (`src/runtime/patch_ledger.cpp`) records
  the code bytes of the patches routed through it, with page protection
  handled and an owner on each patch. The Final Memory HP bypass
  (`src/game/final_memory_patch.cpp`) and the timer freezes
  (`src/game/timer_freeze_patch.cpp`) work this way; both are applied only
  inside practice contexts and restored afterward. A few older toggles, such
  as auto-airtech (`src/game/auto_airtech.cpp`), still write through
  `PatchMemory` and restore their own saved bytes.

## Practice-only hook bodies

Hooks stay installed for the whole process, because they can't always be removed safely, but their bodies only run in a live Practice battle. `PracticeBattleHooksOpen` (`include/game/game_state.h`) is the shared check: it is open only while the polling flag is set and no netplay state is published, and it shuts at battle exit and before netplay starts. Everywhere else a hook calls the original and does nothing else. The battle update hook checks `IsPracticeContext` directly so it can still run the exit cleanup on the battle's last frame. The audio hooks are the exception: volume settings apply in every mode, and they stay installed through netplay.

## The game-thread queue

Engine and Revival calls and code-patch writes must happen on the game thread.
`GameThread::Post` (`include/game/game_thread_queue.h`) runs a command inline
when it is already there, and otherwise queues it, coalesced by command and
argument. `HookedBattleUpdate` drains the queue at the start of each Practice
battle update. Leaving the battle calls `GameThread::Clear`, which empties the
queue and refuses new work until the next Practice battle, because queued work
belongs to that battle's fighters. The netplay branch of the hook returns
before the drain, so the queue is never reached online.

## The input pipeline

`src/input/input_hook.cpp` detours the game's character-input, poll, and
entity-update functions. On top of that raw access sit three injection
paths:

- **Immediate writes.** `ImmediateInput`
  (`src/input/immediate_input.cpp`) writes direction/button bytes straight
  into the player struct for same-frame effects (jumps, one-shot inputs),
  with strict ownership rules: a write lease is held for the whole
  transaction, delayed values are never delivered by an out-of-date
  worker, and everything is refused while netplay owns the fighters.
- **Buffer freezes.** `src/input/input_freeze.cpp` builds a motion pattern
  from numpad notation (QCF, DP, dashes, ...) and
  `StartBufferFreezeWorker` (`src/input/input_buffer.cpp`) rewrites the
  game's real 180-entry input ring each tick until the move comes out.
  This is what makes buffered specials and dash timing engine-accurate.
- **Motion transactions.** A producer/consumer pair in `input_hook.cpp`
  that queues a notation, feeds it through the buffer on consecutive
  frames, and confirms execution by move ID - the path auto-actions and
  Final Memory commands use.

Frame stepping (`src/input/framestep.cpp`) pauses the engine through the
gamespeed byte and steps it a visual frame (or a subframe) at a time.

## Auto-actions and macros

Auto-actions (`src/game/auto_action.cpp`) arm on triggers - after block,
after hitstun, on wakeup, after airtech, on RG - and fire an action through
the motion-transaction path, a macro slot, or an immediate jump. Delay
states and scoped input reservations keep two features from fighting over
the same player. The IC/FIC follow-up watcher
(`src/game/auto_action_charge.cpp`) is a small state machine that watches a
source attack land and then submits the 22C charge motion in the exact
window the engine accepts.

Macros (`src/game/macro_controller.cpp`) record both the raw button state
and the game's input buffer at 64 Hz. Playback
(`PrepareNextStreamTick` / `PublishPreparedStreamFrame`) republishes the
buffer one consumed frame at a time, so replays are frame-step aware and
facing-aware (directions flip with the character).

## Frame data

`src/game/frame_monitor.cpp` runs the analysis loop at the game's internal
tick rate (192 steps per second, three per visual frame): pointer caches,
phase transitions, RG analysis, continuous recovery, and the paginated
stats overlay. Frame advantage
(`src/game/frame_advantage.cpp`) compares when the attacker recovers
against when the defender becomes actionable and publishes the +/- numbers
and block-gap strings the overlay shows; `GetCurrentInternalFrame` returns
the engine tick those numbers are measured in. Other subsystems keep their
own clocks, such as `frameCounter` in the monitor loop.

## The frame bar and move data

The frame bar (`src/gui/framebar.cpp`) samples both player structs once per
engine tick on the game thread, three ticks to a visual frame, and turns them
into an activity strip. CELL STEP chooses whether a cell is one tick or one
visual frame, and hit stop and super flash are marked as frozen
(`MoveTiming::SideFrozen`).

The MOVE DATA line above the bar shows each fighter's last attack as startup,
active, and recovery, in the units CELL STEP uses. `MoveTiming::Tracker`
(`include/game/move_timing_policy.h`) builds it: startup counts up to and
including the first active tick, a command move ID starts a new record, and any
other ID is a phase of the same move. Contact closes the engine's hit gate for
the rest of the window, so the contact tick and later box frames still count as
active, which makes hit, block and whiff measure alike.

## Hit boxes and hurt boxes

The box display (`src/game/collision_display.cpp`) follows the rules in
`include/game/collision_box_policy.h`, which match Revival's and the engine's:

- A character's hit box has a solid outline while its frame flag or attack
  state says it is active, and a dotted one otherwise.
- Hurt boxes are not drawn while a frame flag or the hit-immunity timer is set,
  because the engine will not hit them either.
- A solid box can still fail to connect. The engine's attacker gate also needs a
  clear contact latch and a running attack timer, and Revival's outline ignores
  the latch. `CharacterHitCanConnect` is that gate.
- A projectile's hit box needs its frame flag clear, life left, and the entry
  not destroyed.

## Practice control, savestates, character tools

- `src/game/practice_patch.cpp` enables P2 in practice mode and drives the
  dummy: auto-block, adaptive guard stance, random block
  (`random_block.cpp`) and random RG (`random_rg.cpp`).
- Savestates ride on EfzRevival's own snapshot system:
  `src/game/savestate_hook.cpp` hooks its save/load entry points and
  carries the mod's own state (side, macros, enforcements) across a
  rollback. The capture and restore backend in
  `src/game/custom_savestate.cpp` is not reached by the menus or hotkeys.
- Character hotswap (`src/game/character_hotswap.cpp`) reloads the match
  with different characters by driving the game's own character-select
  frontend and bootstrapping practice fighters inside the loading screen.
- Per-character quirks (Ikumi blood, Misuzu feathers, Doppel tech,
  Sayuri's remembered move, Neyuki jams, ...) live in
  `src/game/character_settings.cpp` and a few dedicated files, re-applied
  every tick by `TickCharacterEnforcements` so the game's own resets
  cannot undo them.

## Menus and rendering

The overlay owns the final draw call: `src/gui/overlay.cpp` hooks
EndScene, validates the device every frame, and runs the registered
external renderers, the ImGui menu, and the mod's own message layer in a
fixed order over the game's 640x480 surface, remapped into whatever
letterboxed area the real render target uses. The ImGui backend
(`src/gui/imgui_impl.cpp`) owns font atlases, gamepad/keyboard navigation
with per-direction repeat gates, and the fallback window used when the
game's own device is unavailable.

There are two menu systems:

- The **ImGui menu** (`src/gui/imgui_gui.cpp`) - tabs for values,
  auto-actions, macros, characters, settings, and help.
- The **custom menu** (`src/gui/custom_menu/`) - a hand-drawn menu in the
  Revival style with its own embedded font atlas, layout engine
  (`layout.cpp`, `renderer.cpp`), and screen definitions
  (`screens.cpp`, `screens_defs.cpp`). Builds with the experimental missions
  enabled also use it for the title-screen MISSIONS/TUTORIAL browser
  (`src/game/practice_menu/`).

Messages for the overlay queue through `AddMessage` /
`AddPermanentMessage` in `overlay.cpp`, which is what auto-actions, frame
advantage, and the menu's settings actions use to talk to the player. The
update check does not: it shows a `[!]` on the Help tab instead.

## Missions and tutorials

This code is experimental and not in release builds. It only compiles with
`-DEFZ_ENABLE_EXPERIMENTAL_TRIAL_TUTORIAL=ON`, which also adds the title-screen
Practice patch.

Missions are JSON packs under `assets/missions/`. The data model and
serializer (with migrations for older pack formats) are
`src/game/mission/mission_data.cpp`; authoring/publish transactions are
`mission_authoring.cpp`; the recorder (which captures a real replay plus
an entity trace) and the strict runner are `mission_engine.cpp`
(`RecorderCapture`, `TickRun`). Tutorial sessions
(`src/game/mission/tutorial_session.cpp`) are lesson state machines that
grade tasks - sequences, contacts, goal states - against the same engine
snapshot, with their own pause menu, pages, and dummy control.

## Online safety and cross-mod cooperation

- `include/utils/efz_netplay_state.h` describes the state that InGameNetplay
  publishes through its `EFZNetplay_GetState` export. The moment an online
  session is live, injected inputs, training hotkeys, and risky patches are
  refused - most of that policy is enforced centrally in
  `src/utils/utilities.cpp` so no feature can forget it.
- Entering that suspend is immediate. Leaving it is not: a failed export read
  with the netplay mod loaded keeps the last known state, and when only Revival's
  own online state is available it takes three offline polls in a row
  (`RefreshNetplayRuntimeState` in `src/utils/network.cpp`).
- The audio control plane (`include/utils/audio_owner_abi.h`) arbitrates
  volume ownership between this mod and ExtendedConfig through
  `EfzAudioOwnerApiV1`; the bridge is `src/utils/extended_config_bridge.cpp`.
- The overlay reset contract
  ([displaymanager-reset-contract.md](displaymanager-reset-contract.md))
  lets EfzDisplayManager reset the Direct3D device without destroying this
  mod's ImGui backend; the export is `EFZ_QueryOverlayResetApiV1`
  (`include/compat/overlay_reset_api.h`), and other mods can draw through
  this overlay via `EFZ_RegisterOverlayRenderer` (`src/gui/overlay_api.cpp`).

## Update check

`src/utils/update_check.cpp` asks the GitHub releases API when the menu opens,
on a detached worker, using only a `User-Agent` of
`EfzExtendedPractice/<version>`. A success ends checking for that session; after
a failure the next menu open retries once 5 minutes have passed. Tag comparison logic lives in
`src/utils/version_compare.cpp`, and the tests
(`tests/update_check_policy_tests.cpp`, `tests/version_compare_tests.cpp`)
pin the behavior, including which tags are safe to suggest.

## Where would I change this?

| I want to... | Start here |
| --- | --- |
| Add a menu screen or row | `src/gui/custom_menu/screens_defs.cpp` (row descriptors), `screens.cpp` (behavior); for the ImGui menu, `src/gui/imgui_gui.cpp` |
| Add a setting | `include/utils/config.h` + `src/utils/config.cpp` (INI schema), then the relevant screen row |
| Support a new game/Revival build | `include/utils/efz_revival_profile.h` (PE profiles), `src/game/efzrevival_addrs.cpp` (per-version addresses) |
| Add a character quirk | `src/game/character_settings.cpp` (+ a dedicated file if it grows) |
| Add an auto-action action type | `src/game/auto_action_helpers.cpp` (action-to-motion mapping), `auto_action.cpp` (arming) |
| Add a new export for other mods | `include/compat/` for the ABI header; keep it append-only and versioned |
| Change what is disabled online | The central gates in `src/utils/utilities.cpp` and `src/game/game_state.cpp` |

## Building and testing

Win32 (x86) with CMake and Visual Studio (2022 or the 2026 presets):

```bat
cmake --preset vs18-win32
cmake --build --preset vs18-release
ctest --test-dir build -C Release --output-on-failure
```

The DLL is written to `build\bin\Release\efz_training_mode.dll`.
`tools/build-task.ps1` wraps the same presets (including the
`v141_xp` XP-compatible build via `-Target xp`) and is what the VS Code
build tasks call.

The tests are plain C++ programs with no game dependency: policy suites for
missions, input, hotswap, updates and versions, plus gate tests for the
overlay reset contract. They are Windows programs, and some of them need more
than the toolchain:

- `mission_data_tests` checks source references to reference material that is
  not in the repository, so it fails on a fresh clone.
- `mission_entity_manifest_check` needs tooling that is not in the repository
  and is only registered when that tooling is present.
- The audio transaction tests need a sibling ExtendedConfig checkout and are
  only built when it is present.
- `audio_native_adapter_abi_tests` is built only for 32-bit MSVC.
