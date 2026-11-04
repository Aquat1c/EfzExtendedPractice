# How EfzExtendedPractice works

A tour of what the mod does inside the game, for curious users and modders.
Symbols are named as they appear in the source, with the file that owns them.
Raw memory offsets stay in the code, next to the constants that use them.

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

Some of the staging exists for a reason you can't see from the code:

- Audio hooks go in over two phases. The four EFZ audio hooks Revival does not
  own go in early. `playSoundBuffer` and `setSoundVolume` stay untouched until
  the host and version are resolved, because Revival could otherwise copy
  MinHook's relative JMP into its own trampoline without relocating it.
- Startup force-restores the Final Memory bypass and the timer freezes, because
  a previous injected session may have left them patched. The runtime decides
  afterwards whether to reapply them for local Practice.
- The XP build links the static CRT, where `DisableThreadLibraryCalls` is
  disallowed, so thread notifications simply stay on.
- Explicit `FreeLibrary` is not a real shutdown API. One would have to quiesce
  and join every worker first. Revival callback hooks also hold a module
  reference, which is kept until process exit because `FreeLibrary` must never
  run under the loader lock.

`src/utils/crash_handler.cpp` writes the report from a helper thread with its
own stack, so a stack overflow can still be reported. The minidump comes first,
before any symbol lookup that could stall. A helper thread can't start while
the faulting thread holds the loader lock, so in that case the report is written
inline after a short wait. A watchdog guarantees the process exits even if the
reporter stalls, and the default after reporting is to terminate rather than
chain to Windows Error Reporting, which avoids ghost processes. The vectored
handler also sees first-chance faults that safe-memory probes catch on purpose,
so it never latches the "handling a crash" flag. `EFZ_TM_CRASH_DETAILED_DUMP=1`
opts into heavier dump flags.

Hooks and patches that depend on the game's memory layout check the build
first, and not every hook is gated the same way.
`GetQualifiedMemorialImage` (`src/runtime/native_game_profile.cpp`) admits
the executable only on an exact PE-identity and hash match, and guards the
byte patches, framestep and the collision layout. The audio hooks compare
entry bytes instead. `src/utils/network.cpp` detects which EfzRevival version (1.02e through
1.02j, including the two 1.02j build profiles in
`include/utils/efz_revival_profile.h`) is hosting the process. Per-version
addresses come from the accessors in `src/game/efzrevival_addrs.cpp`.

Version detection has its own traps. A Vanilla or Other result isn't final,
because `EfzRevival.dll` can load after this mod. The window title isn't enough
for 1.02j either: that build moved code, globals, vtable slots and Practice
fields, so its addresses are used only on an exact PE profile match. The two
1.02j packages are separate compilations that share one version and Practice
ABI. Accessors return 0 for an unsupported version so callers can skip the
feature, and they never guess: a wrong fast path can overlap unrelated code in
the split 1.02f builds.

## How the game is modified

Two mechanisms, both reversible:

- **Function hooks.** MinHook detours on native game functions, managed by
  `PracticeHooks::Registry` (`src/runtime/hook_registry.cpp`) with preimage
  verification, admission control, and retirement receipts. A hook is only
  physically removed with a qualified receipt, which nothing issues yet. Until
  then a retired hook stays attached with admission closed. Its entry jumps
  directly to the original trampoline while existing callbacks may finish,
  and `MH_Uninitialize` drops it at unload.
- **Byte patches.** `PatchLedger` (`src/runtime/patch_ledger.cpp`) records
  the code bytes of the patches routed through it, with page protection
  handled and an owner on each patch. The Final Memory HP bypass
  (`src/game/final_memory_patch.cpp`) and the timer freezes
  (`src/game/timer_freeze_patch.cpp`) work this way; both are applied only
  inside practice contexts and restored afterward.

### Native forwarding during netplay

Each MinHook entry goes through an indirect jump inside the training DLL.
Closing admission changes its destination to the original trampoline before
any C++ callback runs. The jump preserves the native stack, registers and
flags. The entry remains inside the DLL image so other mods can validate and
chain it.

This matters for throws: EFZ's native contact routine leaves part of its
contact table uninitialized, then reads it when placing effects. A C++ wrapper
can change those stack values even when it only forwards the original call.
Different effect positions can change particle bounce timing and shared RNG
consumption. The mod preserves the native call path instead of changing the
game's collision table or RNG.

Startup holds all entries on their native route until the initial netplay
state is resolved. Gameplay and audio callbacks open only for an offline
Practice battle. Replay, VS and Arcade retain the native route. The title
and loading callbacks are explicit offline exceptions so Practice can be
entered again. Leaving a Practice battle closes gameplay routing before
the next screen runs.

Netplay suspension overrides every entry, including those frontend
exceptions; reopening an individual feature cannot override it. Native
audio volume processing runs without training's per-play gain changes
while suspended.

### Why the hook registry is so cautious

- A peer MinHook, such as the netplay battle log, shares the D3D9 EndScene entry
  and can restore the original bytes underneath our JMP. `Registry::Enable`
  re-installs when it finds exactly the bytes it originally hooked over. A
  foreign image layered over our live JMP is never rewritten; only admission
  reopens.
- Outstanding-call counts can't prove a native entry is unreachable or that
  foreign hook chains have drained, which is why a retirement proof is tied to
  one registry epoch and has to be issued outside every retiring callback.
- Callback depth uses only the fixed TEB TLS slots, because expansion indices
  aren't safe to read from a callback. A host can exhaust those slots before the
  mod loads. In that case callbacks are still admitted and retirement stays
  blocked, even if slots free up later.
- Every registry-owned object is allocated before the physical hook is created,
  and callback state exists before the ledger touches a byte, so a failure can't
  leave a live hook pointing at nothing.
- Inline instruction sites, such as the title-case JMP, sit in the same registry
  but go through their bound `PatchLedger`, never MinHook, including during
  cleanup.

The ledger follows the same ordering rule: a site is recorded and its page
references reserved before any protection or byte changes, so a failed
acquisition is still restorable. It coordinates our writers but doesn't stop
native execution, so callers must patch from a point that is already safe.
Destroying a ledger neither restores bytes nor releases pins. The native ledger
is a heap-owned singleton, which avoids lazy-init races under the XP build's
`/Zc:threadSafeInit-` and lets its records survive loader-lock teardown.

`SafeWriteMemory` (`src/core/memory.cpp`) skips `VirtualProtect` when a range is
already writable, because a protect/restore pair on a live data page races other
writers' restores and can leave the page executable.

### Timer freezes

Infinite-timer features no-op one instruction of the engine's own tick instead
of writing a value every frame (`TimerFreeze`,
`src/game/timer_freeze_patch.cpp`). Every other side effect of that tick (curse
aura, freeze pulses, meter pin, HUD gauges) keeps running from unmodified code,
and restoring the bytes resumes the countdown.

- Akiko's curse timer and Mizuka's Final Memory timer turn a subtract-one into a
  subtract-zero. Each site is followed by an explicit compare that rewrites the
  unchanged value, so the substitution doesn't disturb flags.
- Akiko's time-slow ends from a hidden odometer cell, not from the three digits
  on screen. Freezing the ones digit means the higher digits never carry, so the
  wind-down never arms while the freeze pulses keep running.
- Writing Mizuka's timer directly doesn't work: writing 0 while her Final Memory
  runs sends it to -1 so it never ends, and a positive value with no Final Memory
  running never decrements.
- The sites live in character-specific handlers, so a patch is inert unless that
  character is in the match. Because the patch is code-level, the live request is
  P1 OR P2; per-side state only lets each menu clear its own toggle.
- Patch gates read the raw screen byte as well as the phase, because the debounced
  phase still reads Match just after a battle exit.

The Final Memory patch accepts only the exact expected instruction bytes from the
qualified image and changes only the immediate. It never adopts bytes that merely
look already bypassed, since those could belong to something else.

## Practice-only hook bodies

Hooks stay installed for the whole process, because they can't always be removed safely, but their bodies only run in a live Practice battle. `PracticeBattleHooksOpen` (`include/game/game_state.h`) is the shared check: it is open only while the polling flag is set and no netplay state is published, and it shuts at battle exit and before netplay starts. Everywhere else a hook calls the original and does nothing else. The battle update hook checks `IsPracticeContext` directly so it can still run the exit cleanup on the battle's last frame. The audio hooks are the exception: volume settings apply in every mode, and they stay installed through netplay.

The battle update and battle hotkey hooks treat their byte signatures as advisory.
Under EfzRevival those prologues are already JMP-patched, so live bytes never match
retail and a hard signature gate would silently disable the hooks. MinHook's create
call is the real gate, and only an unreadable target is refused.

"Is it safe to do this now?" has more than one answer, and the code keeps them apart:

- `IsTrainingMenuContext` reads the raw screen byte rather than the debounced game
  phase. The phase query lags by up to three calls and isn't side-effect free, so
  polling it from the hotkey thread would disturb the frame monitor's hysteresis.
- Fighter liveness can't be a pointer test alone. The P1 object slot stays allocated
  from character-select confirm until battle cleanup zeroes it, so
  `ShouldFeaturesBeActive` reads true well outside a fight.
- World-scoped restores (CPU and AI flags, fighter fields, the Practice controller)
  may write only under `IsPracticeWorldLive`. Once the fighters are gone, the next
  owner of those objects re-initialises them itself.
- `GetCompletedBattleUpdateBatch` brackets the exact game-thread update, so a monitor
  tick can't observe half of one update.

On a battle's last frame the hook unwinds the menu and framestep pause while the
Practice controller still exists, and restores the Final Memory and timer patches
at once, because the frame monitor would only get to them after its phase debounce.

Hotkey routing keeps EFZ's own dispatcher running everywhere outside a Practice
battle. Inside one, it passes through except while the mod's UI is up, for the Esc
that closed it, and while Esc is the menu key or a Tutorial or Runner owns it.

## The game-thread queue

Engine and Revival calls and code-patch writes must happen on the game thread.
`GameThread::Post` (`include/game/game_thread_queue.h`) runs a command inline
when it is already there, and otherwise queues it, coalesced by command and
argument. `HookedBattleUpdate` drains the queue at the start of each Practice
battle update. Leaving the battle calls `GameThread::Clear`, which empties the
queue and refuses new work until the next Practice battle, because queued work
belongs to that battle's fighters. The netplay branch of the hook returns
before the drain, so the queue is never reached online.

## Lifecycle ownership

Each subsystem owns its phases and its cleanup. A queued action, an accepted
input and an action consumed by EFZ are distinct events. Timers measure
progress within a phase; generation and lease tokens identify the work that
may complete or release it.

`TriggerDelayState` (`include/game/auto_action.h`) separates waiting to
dispatch from waiting for a motion, normal input or staged recipe. It keeps
one consumer generation at a time. A failed consumer releases that generation
before retrying the same selected option. Choosing an option and consuming
game RNG remain in their original call paths.

Macro playback has separate priming and finish phases. Priming waits for the
first poll before draining the remaining input slices. Finishing waits for
the final poll and clear tick, neutralizes input, then guards the handoff.
Airtech attempts likewise distinguish neutral input, publication, held input
and release; their counters advance on engine ticks rather than monitor calls.

Mission recording separates command acknowledgement, neutral polls, input
leases and stopping at a macro boundary. Demonstrations distinguish input
playback, settling final contacts and holding the terminal frame before
restoring the baseline. A tutorial restore result remains pending until the
tutorial acknowledges it; closing or resetting the session retires the
handoff. These phases live in `include/game/mission/mission_engine_state.h`.

UI and render state are owned by `GuiLifecycle`
(`include/gui/lifecycle_state.h`). Binding capture waits for the opening key
to be released. Menu confirmation is a page of an open menu. The fallback
worker claims startup before launching its thread, and a late readiness
notification cannot undo a stop request. Missing game frames and an unavailable
render backend are separate fallback reasons. The shared reset gate preserves
pending reset work across backend unbinding.

Hotkey and controller polling share a worker lifecycle: stopped, starting,
running and stopping. A stop closes input admission while retaining the worker
claim until cleanup finishes. Allocation or thread-creation failure releases
the claim for retry. Controller polling can restart after the old worker has
released its device resources.

Practice worker faults remain terminal when a later stop request arrives.
Frame-step completion, savestate side reconciliation and tutorial preview
refresh each retain their own phases. Input leases, borrowed pointers, patch
ownership and queued cleanup requests can overlap; they remain independent
of these phases so partial cleanup cannot discard a remaining obligation.
Native game-state values, exported layouts and saved configuration formats
keep their existing representation.

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

### Who owns an input

Every injected input has one owner at a time, and most of the input code exists
to keep it that way:

- Admission to a lane (immediate registers, motion queue, buffer freeze, poll
  override) fails rather than caching input behind another producer. A cached
  value would fire later, out of context, after the other owner let go.
- A failed admission leaves the competing owner alone. Macro playback and normal
  pulses validate and acquire before clearing any lane, so the check that finds
  another owner can't destroy that owner's input.
- Requests carry generation numbers. A cancel or cleanup only acts if its
  generation is still the active one, so delayed cleanup from an old request can't
  retire a successor.
- Two failures are kept apart. Busy means another owner holds the lane right now,
  and a delayed trigger retries without rerolling its row. Invalid means the
  request can never work, and the trigger retires.
- Lock order is controller mutex, then motion queue mutex, and netplay publication
  takes the same controller mutex. The motion-transaction mutex is recursive
  because `HookedPoll` re-enters it from inside the original
  `processCharacterInput` on the same thread.
- A control-role change on either side cancels authored commands before the flag
  becomes visible, because P1 is the dummy after Switch Players.
- A lost AI-flag restore can strand a fighter in human control, so a pending
  restore is stored and retried. It is discarded outside Practice rather than
  replayed, since netplay may own controller setup by then.

Netplay is a hard boundary for all of this. Before online ownership is published,
a synchronous drain (`DrainAutoActionNormalPulsesForOwnershipBoundary`) scrubs both
fighters' lanes; after it, bookkeeping is retired without touching memory. Detached
writers re-check after waking, because a worker can be asleep when netplay publishes
and even a neutral write from it would be unauthorized input.

### How EFZ's own input path is used

- EFZ splits input into a producer and a consumer. `processCharacterInput` polls and
  recognizes commands, and the command is spent later inside `updateEntityState`
  through the character's vtable slot. A press handed to the poll isn't yet a move,
  so outcomes are published as Accepted only after the consumer starts it.
- `updateEntityState` returns before the consumer while a fighter's block or hit stop
  or either side's super flash runs, so a visit during a freeze isn't a consumer
  attempt, and a press has to wait for a real consumer tick.
- Trigger normals don't use the detached `ImmediateInput` timer. AI fighters keep
  their AI flag (which keeps their block direction) and get a raw pulse after AI
  generation; human and tutorial fighters use the native poll. Both hold the press
  through the matching consumer, then hold release-neutral through the next pass.
- EFZ's input history records directions plus button rising edges, not the held mask,
  so a held button disappears after its first poll. Synthesized history therefore
  derives from the captured held latch.
- Writing the buttons alone doesn't neutralize a normal. EFZ also keeps a held latch
  per button, specials consume the motion token while button edges remain, and leaving
  those edges set turns a rejected special into 5C. Cleanup clears the whole handoff.
  The motion token is 16 bits, so a one-byte write leaves a live high byte behind.
- A normal is credited only when the exact expected tier starts, because a surviving AI
  command token can start a special at the same consumer. A dash also needs the
  detector's double-tap token, or a restored AI's own dash would be credited to a delayed
  wake or RG transaction. A 22C is accepted only by the real IC actions.
- Command detectors look back different distances, some through 135 of the 180 history
  samples. Motion transactions therefore claim the whole ring, so a detected token can be
  attributed to this request and old commands can't fire again after cleanup.
- Normals out of Recoil Guard go by the live PAT row, not a move ID. RG is two rows under
  one move ID: the defender freeze, then the advantage window with the cancel bit set.
  Across the retail `.pat` files move 168 is 20 + 20 frames (42 total on Sayuri) and
  169/170 are 22 + 20, with cancel tier 10.
- With side switching, EFZ can poll P1's physical binding for the P2 character. The
  fighter being processed decides who a poll belongs to, and the poll's own argument is
  only a fallback. Character select polls with no fighter context at all.
- The poll route uses explicit Win32 TLS, because implicit thread-local storage isn't
  allocated for `LoadLibrary`'d DLLs on XP.

### Immediate writes and buffer freezes

- Only motion queues and buffer freezes write the circular buffer directly. Writing
  normals or jumps there can misalign it and drop presses.
- The hook applies the desired mask before and after the original processor, because the
  original can overwrite the registers mid-frame and a short press would be lost.
- `ImmediateInput` inserts a neutral frame when changing or re-asserting a mask, so the
  engine sees a clean edge.
- The frozen index points one before the end of the pattern, so the terminating button
  stays resident for the game's recognizer.
- Starting a freeze waits for the worker's acknowledgement, because callers check the active
  flag immediately. A refused worker rolls back the pattern and the index lock.
- Ending a freeze neutralizes only the written region and a small tail, and only the owner's
  buffer. A full clear causes a frame hitch, and clearing both players erases the other
  side's inputs.
- The buffer is 180 bytes, not 0x180. A 0x180 length overruns adjacent fields, including the
  index.
- Dash patterns are written to the buffer in full at once, because frame-by-frame injection
  gets contaminated by neutral writes.
- Motion patterns are built in canonical forward/back and flipped once for facing in one
  place. Supplying already-flipped directions would flip them twice.

### Frame stepping

On the vanilla and Revival-engine backends, a pause is gamespeed 0 plus the shared visual
NOPs. Writing gamespeed decides the toggle, so a refused write leaves the state untouched.
Steps run only inside the battle update, so without that hook a request is dropped rather
than left waiting. 1.02j toggles inline in its dispatcher, past the `TogglePause` hook, so
suppressing it takes more than hooking the toggle. Restoring game memory can bring back stale
pause metadata, so restore-side cleanup captures its input before the restore write.

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

### Submitting an action

Submitting an auto-action doesn't mean it happened. Queueing only reserves a generation,
and EFZ's command consumer decides later, so a trigger holds its exact chosen option until
that generation reports accepted, failed or cancelled.

- A pending trigger is released early if the fighter is interrupted. Its own attack or dash
  state is expected, but hitstun, a throw, a launch, a freeze or fresh blockstun are not, and
  holding through them would fire a stale action on the next actionable frame. The check
  covers only clear "was hit" states, because After Airtech and On Wakeup legitimately span
  tech recovery.
- Existing delays are validated before the trigger monitor runs, and the monitor runs last. A
  delay ending on the tick a hit starts would otherwise queue a normal that fires much later.
- A submitted normal the engine never consumes can't hold a trigger forever. The wait is capped
  at 60 visual frames, longer than the slowest legitimate wait (the 42-frame RG state, or
  falling after an airtech), and it counts matching fighter passes rather than time.
- Auto-actions run once per internal tick from the input hook, and the frame monitor skips its
  copy. Because this is inside EFZ's input hook it never waits on a lock: if the state mutex is
  busy, that tick is skipped.
- Motion transactions make an AI-flagged fighter human for exactly one producer pass. That's why
  specials, supers, Final Memory and dashes use them on both sides: EFZ's AI branch never scans
  the input ring, so a legacy queued dash never comes out on a swapped P1 dummy.
- Cancelling has an order: retire the scoped motion, cancel the normal transaction, then restore
  controller flags. The restore helper defers while a normal is active, so the opposite order can
  strand P2 in human control.
- Dropping a delay's bookkeeping isn't enough. Without retiring its queued pulse, the pulse stays
  live and untracked and still presses later.
- Wake and macro state is indexed by player, because the dummy can be P1 after a swap. The release
  sits at function level, since the P1 trigger gate stops running after a swap.
- Random Pool picks at arm time. Bits and delays for a move the current character lacks stay
  stored, but that move is never rolled. New recipes append to the pool so saved bits never shift;
  indices 0 to 82 are a shipped positional format.

### Instant Charge and Flicker Instant Charge

IC and FIC both submit the engine's own 22C. The only difference is the evidence required: IC waits
for a connection, and FIC requires no connection unless the source is a catalogued fixed-window move
(`AutoActionChargePolicy`, `include/game/auto_action_charge_policy.h`).

- The watcher is passive when armed. It reserves a lane beside the primary input and can't submit
  22C until that exact primary generation is accepted and the source's gate is proven.
- The mod mirrors the engine's IC gate, including a detail that's easy to break: red RF accepts every
  action phase above 1, while only the light-blue branch excludes the counter phase. Applying that
  exclusion to both would reject a red IC the game accepts.
- The gate reads the action phase rather than a contact flag, because authored FIC scripts can set it
  without any collision.
- A missing contact hook fails closed. Projectiles have no parent-action data, so any same-owner entity
  contact can disqualify FIC, and uncatalogued sources stay conservative so FIC can't silently turn into
  a hit-confirm IC.
- Once the exact 22C is accepted, a later contact from a lingering projectile can't cancel it: EFZ
  consumes 22C in the character update, before that battle update's collision resolvers run.
- A follow-up belongs to one action instance. A rewind or a move change means a reset or transition
  happened, and a buffered 22C must not leak into a later action. Kano's random-magic supers are the
  catalogued exception, because they loop animation records six times before the FIC flag appears.
- A failing follow-up never demotes a frame-one wake reversal. The primary stays authoritative.

### On Wakeup

Wake timing is per character. Each rising state lasts a fixed number of visual frames, and specials
buffer on the last rising ticks, so the reversal comes out on the first possible frame.

- The wake option is picked as soon as groundtech starts, independent of pre-arm and the Pre-buffer
  Wakeup toggle. Both wake paths use the same pick, so the buffered action can't diverge from the armed
  one.
- Wake macros time themselves backward from the first attack button in the recording, so the press lands
  in the buffer window before actionable. Ticks count only in the final rising state, because that's what
  the character table measures.
- A populated 0-frame macro row owns the whole wakeup. With Pre-buffer Wakeup on, it starts during the
  rising state; with it off, the normal On Wakeup trigger plays it on the first actionable frame. Its row's
  ordinary move never becomes a second producer. A delayed row can't pre-buffer at all.
- While a wake macro waits to start, the dummy is held neutral so AI inputs such as Down can't cause
  accidental transitions like crouching.
- Restoring AI control after a wake macro keeps the buffer and motion token, so a pre-buffered DP can still
  be recognized. The mod then watches for the buffered move and clears the leftover token, and it never does
  an unattributed whole-buffer clear.
- Pre-arm expiry uses engine ticks, so a pause, framestep or hitch during rising can't cancel a reversal.

### On RG and dashes

On RG schedules the action for the end of that stance's stun. With a 0-frame delay, a special or Final Memory
is pre-armed at RG entry so its motion exists on the first actionable frame. Control isn't restored on RG exit,
because the buffered input must stay until the special begins; the Counter RG fast restore returns only the
control flags, so the buffered special survives.

Dash follow-ups fire on the first frame the dash move ID appears, judged from the move ID rather than the input
queue. There's no grounded 66X posture in the air, so air dashes deliver the matching j.X with a neutral direction.
Control is restored when the fighter leaves the whole dash state, not when the dash starts, so the follow-up isn't
cut off. A forward dash's follow-up setting belongs to Forward Dash only; reusing it for Backdash would inject a
normal nobody chose. Kaori's forward dash start often lasts one frame, so she is sampled every frame.

The P2 controller snapshot is tied to the battle objects it came from, so a failed restore stays pending until it
is verified or the world is proven replaced. Restoring a value into a replacement battle object is worse than
discarding a stale one.

### Macro format and playback

The text format is `EFZMACRO 1` followed by space-separated tokens, one per 64 Hz tick: a numpad digit plus buttons
(`5`, `6A`, `2AB`, with `N` for neutral), an optional `xN` repeat (`5Ax50`), and an optional `{k: v1 v2 ...}` group
listing the k raw buffer writes of that tick. Without a group, playback writes the tick mask once. Directions are
stored P1-facing. A repeat count is capped on import, so pasted text can't exhaust memory.

- One 64 Hz tick spans three 192 Hz subframes, and each can show one distinct raw state. A tick with more than three
  raw writes can't be reproduced without losing motion samples, so it's rejected rather than played lossily.
- The serialized mask owns its whole tick. Monitor passes don't reliably interleave with EFZ's three polls, so letting
  a raw sample replace it would make something like {A, B, neutral} depend on scheduling.
- A pause is detected by phase, the Practice pause flag, or gamespeed 0, any one of them, because patch-based pauses and
  emergency freezes set only one. While frozen, recording and playback advance only on buffer movement or a step.
- The recorder takes directions from the buffer and buttons from both the buffer and the immediate registers, and
  synthesizes an entry on ticks where the engine wrote nothing, so playback reproduces per-tick cadence.
- Replay ends in a fixed order: the final byte stays until a poll consumes it, a neutral tick reaches the input history,
  a short guard window lets a detected command commit to a move ID, the motion token is neutralized, and only then does
  control return to the AI. KO and round transitions can stall the ring, so these waits are bounded.
- Side swaps are refused during record and playback, because the P2 poll override would drive the human's new side and
  stale CPU flags would be restored at the end.
- Recording behind a pause surface suspends the timeline and buffer cursor, then rebases the cursor on release, so menu
  input can't become clip input.

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

### How frame advantage is measured

- Timing uses an engine-tick clock advanced from the battle update, not wall time. It stops with the engine during a
  pause, framestep or hitch, so those never inflate a result.
- The result is defender-free tick minus attacker-actionable tick, so positive favours the attacker. It shows `.33` and
  `.66` fractions because three ticks make one visual frame.
- Superflash ticks are subtracted from gap and advantage, because a flash stops the other side's scripts. Fire, electric
  and frozen stun stop neither side, so they aren't subtracted, and a real game-speed freeze runs no ticks.
- A hit counts as connecting when the defender enters blockstun, hitstun, launch or a thrown state, with a fallback for
  transitions that fit no category: the defender going non-actionable just after the attack edge.
- IC superflash can look like a connect, so detection is off during IC moves. It checks move IDs rather than the game-speed
  freeze, because that read can return a stale zero on some Revival versions.
- If the attacker recommits before the defender is free, the earlier recovery is thrown away, so a string reports the
  advantage of its last move.
- A landing reads as actionable, but the defender isn't free until it ends. Air-blocked strings resolve when the landing finishes,
  into anything but a new stun (`FrameAdvantagePolicy::DefenderFreeEdge`).
- A result is held back 30 visual frames when the defender recovered through a tech, a fall or a landing, and shown at once
  otherwise. A new contact overrides any earlier display.
- Teleports and position resets cancel the exchange in progress, since the artificial state change would produce a
  meaningless number.

### Recoil Guard analysis

The RG overlay shows `[FA1]/FA2`. FA1 is the difference between the two freezes, looked up from the values on the EFZ wiki's
strategy and mechanics pages. FA2 is the measured gap between both sides becoming actionable. RG stun is 20 visual frames for
everyone except a Sayuri defender, who takes about 21.66; that's an engine quirk the mod reproduces rather than normalizes.

The Counter RG assist turns on the dummy's autoblock and arms its RG while the human's window is open. The arming field is
per-fighter, so it targets the dummy by side: after a swap the dummy is P1. Leaving Match restores autoblock separately, because
the normal stop test no longer runs there.

### The monitor loop

The loop paces on an accumulating schedule to avoid drift: it sleeps most of the interval and spins only during Match, and if it
falls several frames behind it realigns instead of running a burst of catch-up frames. The high-resolution timer and raised priority
are held only during active intervals. With no owned Practice world, it reads no game pointers and advances no counters.

Other threads read owned copies of the monitor's data under a short mutex.
The same publication boundary protects complete trigger-option lists, so a
reader cannot pair a new count with old rows. Features follow game mode,
not window focus; focus only gates key monitoring.

On character select the engine toggles the active-player byte as players navigate, while the CPU flags stay authoritative, so the mod
never forces them mid-screen. It re-applies CPU flags on exit. Trigger clearing there waits for stable frames, because the phase can
read as character select falsely mid-match.

Leaving a match closes the menu on the phase edge, before session teardown, because pause ownership has to unwind against the battle
context it froze. Doppel tech and Sayuri's counter poll run outside the moved-ID gate, because they need per-frame windows inside
follow-up states.

Continuous Recovery stands down when the engine's own F4/F5 regen is active. EFZ's regen state is visible as two parameter words kept in
sync across both players: F5 cycles A between 1000 and 2000 and sets B to 3332, and F4 forces B to 9999 and steps A through 0..2000 by 5.
A also encodes the RF gauge: below 1000 is red with RF equal to A, 1000 is full blue, and 1001..2000 is blue with RF = 2000 - A. Detection
needs A actually stepping, because a static B of 9999 is common at max HP.

### Combo overlay

- After a reset the overlay ignores counters that were already running. A loaded state can hold them up for as long as it likes, so a new
  hit on top ends the wait as well as both sides going idle (`ComboBaseline::Tracker`).
- Joining a combo already in progress rebuilds the starting HP from the current HP plus the combo's damage, because that damage has already
  been taken.
- The session Max survives a state load, because reloading to drill a combo is one session.
- Between hits the resource columns only follow spends. Gains appear on the next hit, since meter and RF gains are hit-locked.

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

In Full and Compact detail modes, numbers inside the bars count consecutive
displayed cells with the same state and move ID. A `+` marks a run clipped by
the visible history; short runs omit the label if it will not fit. These are
state durations in the selected CELL STEP units, not a second block-gap
measurement.

With the menu closed, hovering over either row highlights that column for both
players and shows their recorded state, move/frame IDs, boxes, timers and guard
availability. In visual-frame mode the inspector uses the newest raw tick in
the cell, rather than presenting the merged cell's maxima and flags as a single
sample. Inspection also works in Bars Only mode and does not pause the game.

A few more decisions shape what you see:

- `TickSample` runs at the start of an engine tick, so what it reads is the previous tick's post-collision state, and the contact journal
  holds exactly that tick's contacts.
- The bar uses MBAACC-style activity gating: it advances while something happens, keeps going for a tail of about a second so post-action
  frames stay visible, then holds. The next activity wipes it.
- Cells are classified from frame data and engine timers before move IDs, because EFZ writes block and hit timers independently of the
  animation ID. The combo timer isn't used as stun proof, since it can linger on an attacker who is moving.
- A frame shows as active only when the engine's collision gate would let the boxes connect. Boxes that fail the gate get their own marker,
  so a dead hit box looks dead. Active projectiles also count as active frames.
- Superflash and hit stop are kept visually separate because they're different things in the engine. A frame that is active while frozen
  stays red.
- The contact-latch value is shown raw as a diagnostic. Its colors must not be read as hit, block or throw results.
- In visual-frame mode each group of three ticks folds into one cell with a per-category priority, so a one-tick event still shows.
- The frozen test uses the previous sample's hit stop and flash, because both counters decrement after collision, so the previous values
  are the ones that gated this tick.
- A move with no character hit box, such as a projectile throw, reports only its total length.

The status text uses these short labels:

| Label | Meaning |
| --- | --- |
| ST | State timer: hit stop on an attacker, block or hit freeze on a defender. 0 means the engine is ticking |
| UT | Untech set by the latest hit |
| SF | Superflash freeze on this player |
| AT | Air time; RG locks at 30 ticks |
| AM | Air-mobility counters, reset on landing. Their meaning varies by character, so they're shown raw |
| FA | First active frame of the sequence |
| ACT | Consecutive active or projectile frames |
| BX | Collision-active and raw character boxes |
| PB | Collision-active projectile boxes |
| P | Alive projectile slots |
| ATK | Post-resolution attacker countdown |
| FL/CL | Frame and collision lockouts |
| TOT | Engine-busy frames in the sequence |
| GG | Guard Gauge, 0 to 360 |
| B/R | Can block / can RG the current attack |
| HitS | Raw contact latch |
| CH | Counter-hit flag this frame |
| HST | This side's hit stop |

## Hit boxes and hurt boxes

SHOW COLLISION OVERLAY controls the whole display while preserving the selected
layers and player filters. Its `collisionDisplayEnabled` setting defaults to on
for existing configurations. When hidden, the renderer skips rebuilding the box
list and reading the game state for it.

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

Contact events are published from inside the game's collision resolvers, by comparing state before and after the original call, rather than
guessed from a later sample. That makes contact exact to the tick. For projectile contact, the state changes prove that some contact branch
committed but not which, because Guard Point, armor and guard break overlap.

The display replaces Revival's, and its hotkeys are consumed before Revival sees them. If the overlay can't render, Revival's display keeps its
keys, so the player is never left with neither. The Revival hit-box, hurt-box and frame-display toggle hooks themselves are disabled, because they
crash on every Revival version.

Each player's projectile ring has 64 slots and is read once per sample with no writes. A slot that can't be read keeps its prior state rather than
counting as a despawn, and even the ring's cursors can't rule out a spawn and despawn between two samples, so all alive flags are always scanned.

Nagamori's note-trigger rectangles aren't in frame data. The game calls a range-marking routine from per-move and per-projectile code, so the overlay
reproduces those ranges from a table. Her bow's trigger is continuous and sits over the projectile body, so it's drawn faintly with the real boxes
drawn again on top.

## Engine facts worth knowing

These apply across the mod, and they're the things most likely to trip up new code.

- **The hit-state latch isn't a result enum.** Direct collision usually writes it, but scripts and entity resolvers write it too, and some FIC scripts
  force the "hit" value on a complete whiff. Grade contact from the resolver hooks, never from a raw value alone.
- **One timer does two jobs.** The same field is remaining freeze on a defender and remaining hit stop on an attacker. While both players have it above
  zero, gameplay timers don't advance. Superflash freeze is a separate counter, set only on the IC or super activator.
- **Frame data blocks are 200 bytes:** 5 hurt boxes, then 4 attack boxes, then attack properties such as damage, block rules, hit-reaction overrides,
  knockback and each side's hit stop.
- **Guard Gauge** depletes per subframe at neutral 0.075, grounded hitstun 0.2 and air hitstun 1.0, and blocked chip damage refills it by base damage / 30.
- **Dash move IDs:** 163 is the forward dash, 164 the backdash, and 165/166 the forward and backward airdash. Kaori's forward dash starts on 250.
- **RG is refused when airborne too long.** The engine counts frames since last touching the ground and treats 30 or more as too long.
- **Thrown states differ by character.** There's a primary window plus outliers, so "is being thrown" checks have to cover all of them.
- **Writing 99 to the motion token** cancels in-flight recognition. The command flags before the input ring toggle when a motion is recognized, and injected
  input has to clear them or stale move ID transitions fire afterwards.
- **Character files don't match character names.** "nagamori" is Mizuka, "mizuka" is the boss UNKNOWN, "mizukaB" is playable UNKNOWN, "nanase" is Rumi,
  "exnanase" is Doppel, "nayuki" is the sleepy variant and "nayukib" the awake one.
- **Per-character value slots are shared.** One engine slot means Ikumi's genocide timer, Akiko's bullet cycle, Mio's stance, Kano's magic, Sayuri's remembered
  move and others, depending on the character, so every access sits behind a character gate.

## Practice control, savestates, character tools

- `src/game/practice_patch.cpp` enables P2 in practice mode and drives the
  dummy: auto-block, adaptive guard stance, random block
  (`random_block.cpp`) and random RG (`random_rg.cpp`).
- Savestates ride on EfzRevival's own snapshot system:
  `src/game/savestate_hook.cpp` hooks its save/load entry points and
  carries the mod's own state (side, macros, enforcements) across a
  rollback. A failed native load returns failure to the hotkey and skips
  mod-state restoration, counters and success notifications; pause ownership
  is settled on either result. The capture and restore backend in
  `src/game/custom_savestate.cpp` is not reached by the menus or hotkeys.
- Character hotswap (`src/game/character_hotswap.cpp`) reloads the match
  with different characters by driving the game's own character-select
  frontend and bootstrapping practice fighters inside the loading screen.
- Per-character quirks (Ikumi blood, Misuzu feathers, Doppel tech,
  Sayuri's remembered move, Neyuki jams, ...) live in
  `src/game/character_settings.cpp` and a few dedicated files, re-applied
  every tick by `TickCharacterEnforcements` so the game's own resets
  cannot undo them.
- Auto-airtech (`src/game/auto_airtech.cpp`) and auto-jump
  (`src/game/auto_jump.cpp`) work by input injection; no code bytes are patched.

### Dummy control

- Random RG and Always RG arm the dummy side, resolved through `SwitchPlayers`, so they follow a control swap. Revival's own Random RG flips a coin the same way.
- Random Block owns the auto-block flag for its whole enabled lifetime, or the native F7 watcher would mistake a coin-flip OFF for the user pressing F7. On disable
  it restores the authored window, never the last coin flip, and it never turns OFF mid-guard.
- Only the two custom auto-block modes override the flag; None and All leave native F7 authoritative.
- Adaptive stance chooses crouch against a grounded attacker and stand against an airborne one, with `GuardOverrides` for grounded overheads and low-hitting air moves.
  The engine re-derives stance from the dummy's neutral input each frame, so writes can repeat, and blockstun and the blocking move ID are preserved so a stance change
  can't cut guard.
- Writing P2's CPU or AI flag on one peer of an online match desyncs it, so control-flag code runs only in offline Practice. After a Revival side switch P2 can be the
  local human side, so nothing may force it back to AI.
- Auto-jump presents one visual frame of neutral after landing, so the engine sees a new UP press.
- Auto-airtech excludes air guard, because it fired spuriously while air-blocking.

### Savestates and Revival integration

- Loading a Revival state runs Revival's own unfreeze toggle, so a load has to clear the mod's framestep pause.
- 1.02j inlines save and load inside its hotkey dispatcher, so tracking brackets that call rather than adding a second hook at the same address.
- The hotkey gate never gates the Practice object netplay is about to delete. It blocks manual save and load during a mission run, because the runner owns Revival's single
  Practice checkpoint.
- The evaluator detour keeps EDX and returns the full EAX: 1.02j's MinGW body reads EDX and consumes a pointer-sized result, while older builds use `__thiscall` and read AL.

### Switching players

A side switch (`src/utils/switch_players.cpp`) is the same on every build: swap EFZ's two live control maps, write the game-state flags, and mirror the side into Practice's display field.
Revival's own hotkey does the equivalent, flipping that field and swapping the same pair of maps. The swap is verified and restored from a saved baseline rather than toggled, so a repeated
request can't flip the human's controls.

That display field is a dword on every build. Revival's rollback session has its own local and remote sides and input buffers at higher offsets, but those aren't part of the Practice object,
which is 0x2B0 bytes (0x2B8 on 1.02i, 0x2F0 on 1.02j). On character select the mod leaves the field alone, because the active-player byte toggles there as players navigate and Practice may
be between lives.

### Pausing for the menu

All menu surfaces share one physical Practice pause: the first visible surface acquires it, the last releases it, and closing only unpauses if the mod froze the game itself, so a pause
the player set beforehand is left alone. Practice objects captured during netplay suspend are treated as dangling, because the netplay mod deletes the Practice controller once its session
initialises.

Without Revival, the mod mirrors Revival's pause: gamespeed 0 plus NOPs on 10 animation and effect update sites in efz.exe, freezing only from the default speed so a speed the player chose
isn't overwritten. Those sites are shared with Revival's patch toggler, which saves live bytes as its originals. It must only ever see real bytes, or NOPs become its "originals" and it restores
them on every close. The owner refuses bytes it doesn't recognize, and at the Practice boundary it drops ownership regardless of order (`include/utils/visual_freeze_policy.h`).

### Character quirks

- **Doppel's throw follow-ups** are EFZ's only techable throw. The engine escapes through a latch on Doppel's own struct, filled from the opponent's rising-edge buttons while it's zero, so the
  first press wins. C refuses the A-branch follow-ups, B refuses the B-branch, and A locks out both. FOLLOW-UP TECH defers its write to the frame where the engine samples the opponent, because
  the engine zeroes the latch a couple of frames into each state. ALWAYS stays armed, because her recognizer rewrites the committed follow-up every frame. Outside those four states the field is
  generic scratch, so both a character gate and a fresh move ID are mandatory.
- **Sayuri's 214 stance** has no attack box. When an attack lands on a counter frame, the engine copies the opponent's move ID into her memory and throws them. On later grounded blockstun entry it
  compares the opponent's live move ID against that value, and a match arms the cut-out and lets A/B/C cancel into Magical Cutter. 999 means empty, because 0 is a real move ID. The picker stores a
  resolved move ID rather than a menu index, because "236A" is a different attack on every character. AUTO CUTTER goes through the normal pulse, so it lands after EFZ's producer and before her
  consumer, and it's the only way a CPU Sayuri ever cuts out.
- **Kaori's recoil duck** is a staged native 44 then 66. The engine consumes a forward dash token only on two frames of backdash, so the second command is submitted in exactly that window. It can't
  be one input-history pattern.
- **Rumi's mode** goes through the engine's own toggle routine rather than poking bytes, and is deferred until she is actionable.
- **Mai's forced summon** writes her native summon move, so the engine performs every side effect. One int timer serves all her statuses, so the three timer names are aliases.
- **Final Memory commands** sometimes need input shaping: Akiko needs neutral release windows, Kaori's 666S needs sustained forward holds, and the detector expects the terminating button a few history
  entries old.
- **Enforcement** can run on the monitor thread and on the game thread after a load, and its statics aren't atomic, so it uses `try_lock`. Skipping is safe because the other thread is enforcing.

The auto-action catalog keys actions by character and action type rather than move ID, because IDs change with stance, air state and follow-up phase.

### HUD disable

Whole-HUD hide wraps the battle render and forces the engine's own intro gate positive around it, the same path the game uses every round intro, so everything suppresses itself. Per-element hide is a blit
filter: the two HUD render functions set a thread-local seam around their draws, and the blitters skip a draw only inside a seam when its destination matches a disabled element. Gameplay sprites have no
seam, so they always pass through. All six hooks are refused as a unit if any entry differs, because a generic x86 prologue proves nothing about the body.

## Palette editing

Both menu styles expose a top-level Palette tab. Compact colour controls sit
beside an animated preview. Load, Save, Save As, Apply and Restore form the
bottom action row; the shared menu header is unchanged. Palette uses the
same pane transition as other tabs, including its clip rectangles, and
waits for the transition to finish before accepting input. Each control
has a description for mouse hover and keyboard/gamepad focus.
Shade groups are selected initially, and opaque control backgrounds keep
the underlying match from obscuring the editor.
Back closes the editor without discarding the draft. It first cancels an
open dialog or finishes colour adjustment or panning when those are active.
The visual picker edits saturation/value and hue in the existing colour
control area. RGB/HSV sliders and three/six-digit hex entry are also available.
The sprite eyedropper selects the clicked index in Shade mode and its group
in Ramp mode; transparent and shared effect colours remain uneditable.
Numbered shade groups preserve the stock palette's lightness relationships.
Pins protect individual edits from group changes. Undo keeps sixteen edits,
with a slider drag or held adjustment recorded as one edit. Hue remains editable on black and
gray even when changing it does not change the visible RGB value.

`palette_assets.cpp` reads the character's DAT and PAT files into bounded,
mod-owned assets. Six stock palettes supply forty colours each. Transparency
comes from the sprite data, and shared effect colours come from the stage
palette rather than the editable fighter bank. Animation choices use PAT
IDs, classification subtabs and the move/entity notation catalogues,
including sparse IDs and the native last-definition rule for duplicates.
Nanase and Ikumi also expose their alternate PATs. Preview playback uses visual frame durations at 64 Hz and
never executes a move or advances the match. Dynamic default-pool D3D9 textures
support both classic and Ex devices. They belong to the overlay's device and
are released through its reset/shutdown gate.

Move classifications are embedded as Windows resources from `assets/movedata`.
The shared move-data cache reads them directly from the loaded DLL, including
Nanase and Ikumi's alternate tables. External move-data files are not required
or read; resource filenames remain internal keys while menus use character
display names.

`palette_editor.cpp` owns the document and file operations. Windows Open and
Save As choose JSON or native PAL files at a user-selected path. The initial
directory is `palettes` beside the configuration file; older library JSON
files remain readable. JSON stores the immutable stock baseline, edited
colours, pins and group targets. Native PAL stores forty BGR colour triples
and their count. Invalid or wrong-character imports leave the draft intact.
Save checks the loaded file's fingerprint before replacement; Save As uses
the native overwrite confirmation. Writes use a flushed temporary file and
atomic rename. Cancelling either dialog preserves the draft and source path.
Japanese labels, descriptions and move names use the shared translation
table and embedded font. A language change rebuilds cached move labels
without discarding the palette draft.

`palette_live.cpp` owns temporary fighter recolouring. The panel requests a
snapshot; the existing admitted battle update applies it on the game thread,
including while Practice is paused. Native entry bytes are verified before
use. Only the selected fighter's palette row changes, preserving each
entry's reserved byte. DirectDraw's palette upload result is checked before
the native blend tables are regenerated. Changed previews coalesce over
100 ms; unchanged colours do no refresh work. No simulation, RNG, reload or
additional game hook is involved.

The live snapshot records the fighter, scene generation, character and slot.
Restoration requires that identity and the last-written bytes to still match.
Leaving the editor restores uncommitted preview colours. **Apply** retains
the edited colours for the current match without writing a file; later preview
edits restore to those applied colours. **Restore** cancels temporary preview.
Saving a PAL to a native character slot makes it available through the
existing custom-palette selection. Savestate writes
suspend the panel, invalidate its old ownership, and refresh the complete
restored palette after simulation state has been restored. An unavailable
graphics device defers presentation refresh without failing the savestate.
Scene retirement drops stale snapshots, and netplay admission prevents every native palette read or
write through this adapter. Suspending the menu preserves the editable draft.

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

### Sharing EndScene and the window

Other mods should register through `EFZ_RegisterOverlayRenderer` rather than hook EndScene, so one mod owns the device hook. Registered renderers run before ImGui initializes, so they appear on the first
640x480 frame, and each call is SEH-guarded so a misbehaving renderer can't take down the frame. Outside a Practice battle they still get their call, but the ImGui work stops, because the Win32 backend polls input
during NewFrame.

- A foreign image at the EndScene entry is either a peer chained over our live JMP, which still reaches us, or the peer's restored bytes, which don't. Only a recent detour entry tells them apart. The hook is
  re-enabled only over its exact preimage.
- If no 640x480 frame arrives within 6 seconds of enabling the hook, a watchdog publishes the external menu fallback. Any later 640x480 frame lifts it.
- The device is checked every frame, because a replaced device must never render through a backend retained for an older one.
- The font atlas work runs before `ImGui::NewFrame`, since it can recreate the shared font texture. It also runs while the title screens or pause menu are up, and in Japanese, because those glyphs live only in that atlas.
- The game-window subclass is detached at the netplay boundary and re-attached after, because peer mods re-subclass the same window. A peer on top either chains to us or replaced us, and re-attaching over a live chain would
  close a ring, so only a probe message that misses our procedure counts as a wipe. If a ring has already closed, the re-entering message is handed once to the window class procedure, which stops the recursion instead of
  overflowing the stack.
- The forward target is published before the window slot is written, because attach and detach run off the window thread and a message can arrive right after the write.

### Device reset and the fallback window

A display reset invalidates only vertex, index and font objects; the ImGui context and window-procedure chain survive it. The fallback window has its own Direct3D 9 device, which is released after a short idle, because a second live
device blocks the game's exclusive Reset. The fallback host holds the only ImGui context, so the game window can initialize again only after that thread has shut it down.

Closing the menu isn't netplay-gated, because callers close it when the world is changing underneath it. The opener has the opposite rule: an open menu is never refused by the context gate, or the player would be trapped when the game
leaves a gameplay screen.

### Menu input

Mouse positions enter ImGui in the overlay's 640×480 coordinates before button
events. The Win32 backend still owns button, wheel, capture and leave handling;
its raw position events are replaced before ImGui consumes the queue. Frame
polling uses the same mapping, so a click cannot overtake its scaled position.

For supported Revival wrappers, training checks that DisplayManager owns the
known final-blit call before using its configured Fit, Integer or Custom image
rectangle. The rectangle is projected from the current backbuffer into the
window client area; clicks in its borders are outside the overlay. Native
presentation, unknown wrappers and the fallback host keep stretch mapping.
DisplayManager's startup configuration is read from beside its loaded module;
this integration does not patch or change DisplayManager.

- Custom menu navigation polls physical inputs rather than ImGui keys, because ImGui's mirror of the game input carries what auto-actions and macros write and would produce false presses. Polling is gated on EFZ being the foreground
  window, so keys typed elsewhere aren't consumed.
- Input held when the menu opens stays ignored until released, including phantom DirectInput directions and the menu key itself.
- Mouse hover takes focus only when the mouse actually moved or clicked, so a resting cursor can't steal keyboard or pad focus.
- ImGui navigation accepts gameplay inputs as well as XInput, so devices that work in-game can still navigate. All sources go through one edge and repeat gate, because ImGui's own NavUpdate would trigger a second time.
- Controller names come from a copy published by the watcher thread, because Windows controller enumeration must not run on the render thread.

### Custom menu internals

- Secondary screens are lists of `Row` descriptors built with small helpers, so a screen definition reads like a short table. To add a row, pick a `RowKind` and a builder in `include/gui/custom_menu/screens.h`.
- Row builders run twice per frame and must stay read-only. Changes that invalidate choice pointers are queued for the next menu tick, because the current frame can still hold `Row` pointers into those vectors.
- An open dropdown reads input before the list does, through an input-only pass, and is drawn on top afterwards. Otherwise the list would drain the edges first.
- Info rows take focus so keyboard and pad players can scroll long paragraphs; headers and spacers don't.
- Top tabs, subtabs and categorized popup tabs share the drawing geometry for hit testing, including font measurements, pixel rounding and label padding.
- Categorized popups hide empty categories and keep a fixed size, so the tab strip doesn't move between categories.
- Translations and default help are both looked up by whole string. Popup choices are translated before a marker prefix is added, and rows with translated labels set their help explicitly.
- ITC Bolt lacks several ASCII symbols and most accents. Noto fills those gaps by merging, which never overwrites existing glyphs. The atlas is shared with the ImGui context, so the default font is rebuilt too.
- Fades and slides use a high-resolution clock, because `GetTickCount` moves in steps of about one frame, which makes slides judder.
- The per-row footer shows one line and cuts off near 100 characters, so longer explanations belong on the Help pages.
- Continuous recovery values live in the menu's local data, which the apply step copies into the runtime, so a value written anywhere else is reverted by the next apply.

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

### Grading

- Contact grading trusts the collision resolvers, not sampled state. A latch or a life drop alone stays unclassified, and an unclassified contact never clears a strict requirement.
- State-sensitive contacts such as OTG, or an armor-breaking hit, use the defender state sampled inside the resolver. By the next snapshot the victim is already in hitstun.
- EFZ can reset an expired combo and apply a meaty hit in one resolver call, so the counter goes straight from N to a smaller positive number. A restart is accepted only when damage, hit reaction and hit branch agree.
- A same-ID rewind counts as a new action only at the animation head with a consumed button, because looping animations such as air IC float rewind too.
- Recorded multi-hit steps are route requirements: one hit is enough once the route continues, because the contacts a move lands can vary. Damage is compared per step because hit variants share a move ID and differ by thousands, while proration stays within a few hundred.
- While a combo is alive, EFZ's own continuation window governs; `maxGap` applies only after a combo end, and `maxDelay` only to armed delayed hits.

### Projectiles

Projectile hits run in a schedule beside the linear step list, because a projectile can keep hitting while later normals are performed. Ring slots are reused and the contact journal has no generation field, so identity is slot plus a generation counted from observed
dead-to-live transitions. A contact grades only against the nearest preceding lifecycle of that entity.

Timing is never treated as causality: two actions active at spawn are ambiguous even with the same move ID. Flexible fanout, for interchangeable children that may connect in any subset, is curated per move rather than inferred. Rumi's 214C is deliberately excluded,
because its four random volleys can't be told apart by slot and generation. PAT's attack flag doesn't mean an entity phase will hit, so the catalog's lifecycle disposition decides that.

### Recording and data

- One recording is one authored take. A dropped combo becomes an explicit setup boundary rather than a retry timeout, committed only once a later step proves it was intentional.
- The recorder's clock excludes freezes, so measured gaps use the same clock the runner grades with.
- Publishing is transactional. Files are staged beside their destinations and the mission file is the commit marker, so an exit partway can't expose a partial mission.
- Migration reads older files in memory and never rewrites them. Field absence is a distinct state, so an old file isn't confused with a recording that proved no single setter.
- An embedded start state is a Revival savestate image, but heap pointers, sound IDs and other runtime-owned ranges keep the current session's bytes, because another session's would crash or play the wrong sounds. A mission with an embedded state never falls back to value-level setup, which would rebuild a different starting position.
- The on-screen recipe is presentation only: it can fold rows but never changes what is graded. Counts show the authored minimum, because showing the maximum would suggest every possible projectile must connect.

### Input ownership in missions

From choosing a mission until the player takes over, one zero-input owner holds P1 with no release frame in between, because a stray poll would land in the baseline. Closing any frozen surface goes through a neutral gate, or the button that closed it would become a jump or attack on the first live poll.
Releases are ordered so the mistake is always over-suppression. The snapshot is thrown away before any restore, because a restore can change the world and still report failure.

### Tutorial dummies

- Idle, block and RG episodes leave P2 under native CPU control, because forcing human control injects neutral every sub-tick and bypasses the native block processor. Only attack scripts drive P2 by injection.
- Wakeup attacks are a config lease over the native auto-action wake path, because injecting on the wake edge is always frames late.
- Dummy specials are frozen into the buffer with their full motion, or only the button lands (236A becomes 5A), and released once recognized so they fire once.
- Settings an episode changes are restored only if they still hold the episode's value, so a change the player makes during a lesson wins.
- Hit-confirm variants use a shuffled assignment that stays the same across retries, so a failed attempt can't change the answer.

### Title-screen submenu

The submenu coexists with InGameNetplay by leaving the title menu count, wrap bounds, geometry, jump table and vtable unpatched; it enters through a jump patch on the Practice case and a MinHook on the title update, so the detours compose in either install order. Its artwork loads into a private surface, so
it never competes over `title_ob.dat`. Starting Practice copies the vanilla case including its blocking fade, because skipping the fade starts the match BGM about a second late.

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

### Netplay suspend

Suspend drains the current Practice writers before publishing the online
boundary. The monitor, battle callback and direct volume apply share the
P2 control mutex; retirement also drains the input buffer worker and
restores owned state. The monitor releases that mutex while parked or
sleeping. On exit, resets run while online mode is still published, then
only offline frontend callbacks reopen. A new Practice battle opens its
own gameplay callbacks.

InGameNetplay calls `EFZ_TM_PrepareNetplayV1` before native online session
initialization or adoption. Starting a background host listener alone does
not retire the current Practice session. The call retires Practice synchronously and holds a pending
handoff until the producer publishes the resulting state and calls
`EFZ_TM_FinishNetplayPrepareV1`. A failed producer drain rejects the handoff
and releases its pending hold so the previous offline state can recover.
Both companion DLLs must support this handshake for synchronous handoff;
polling an older netplay producer cannot provide that guarantee.
The mod's WndProc detaches before netplay re-takes the window procedure,
because a foreign procedure on top would turn the re-take into a ring.
Hosting in the background is not a live match.

Training reads `EFZNetplay_CopyState`, which copies a complete publication
under the producer's mutex. A failed copy cannot reopen Practice. Older
producers fall back to native Revival state; only confirmed offline reads
count toward resuming, and uncertain reads retain suspension. Training
never opens the shared mapping by name, which could select another process.

### Reading the netplay state from another mod

Consumer mods should call the `__cdecl` export
`EFZNetplay_CopyState(output, outputSize, minimumVersion)` on
`efz_netplay_mod`. It returns nonzero after copying a compatible snapshot
into caller-owned storage. Check the magic, version and `structSize` before
using fields. `EFZNetplay_GetState` and the named mapping remain for ABI
compatibility, but their raw pointer views do not synchronize with writes.

Capability bits say which field groups are live on this tick, and a bit can be zero simply because its data source is unavailable, so check the bit rather than trusting the struct version. Async hosting keeps `sessionMode` at hosting while the local player uses EFZ normally, so use `inNetplayMatch` to detect a live match. Scores are absolute, not adjusted for the local player's side.

### Audio and EfzRevival

- The Revival launcher creates `efz.exe` suspended and injects later, so the mod takes the vanilla audio entries only after a stable, positively standalone launch signature. If that's never proven within a time limit, the entries stay unowned, because claiming an unproven entry could break a late Revival.
- Revival rebuilds its EFZ wrappers from its frame hook, so a torn read is normal and only defers the decision. Snapshots use `ReadProcessMemory`, which fails cleanly if a wrapper is freed mid-read.
- Revival's audio callback receives its saved machine state rather than conventional arguments. The hook cross-checks the saved ESP, so an ordinary cdecl caller can't make it write through an unrelated stack slot, and it also writes the live stack argument, because the copied EFZ prologue reads it after the callback returns.
- The game system's BGM field holds the allocated buffer index, not the logical track, so the track number is recorded in the play hook.

### Sharing audio settings with ExtendedConfig

Every callback and transform runs under one recursive boundary that no IO may run under. It uses `__finally`, because native exceptions must release it and a C++ lock alone isn't enough under `/EHsc`. Registration freezes initial ownership, so a late `Initialize` can't activate a transform behind a lane already acknowledged as raw.

This mod and ExtendedConfig name the same process-scoped Win32 mutex, because their separate C++ locks couldn't otherwise stop overlapping file operations. Lock order is module control mutex, then this mutex, then the audio boundary; it's never taken from a native callback, and recursive acquisition by the same control thread lets helpers compose. Writers mark `writeInProgress`, write the payload and revision, then clear the marker, and readers keep the last accepted tuple until a complete pair is written. Moving a slider requests an event-driven apply, including outside Practice. Direct applies acquire the P2 control mutex before the audio boundary and recheck netplay ownership; online changes remain pending until offline ownership returns.

## Controllers and hotkeys

All XInput and DirectInput hardware work happens on a background watcher thread (`src/utils/xinput_shim.cpp`), which publishes under a short copy-only mutex. Native polls can block for seconds on a disconnected slot and enumeration can stall on some drivers, neither of which the render or game thread can tolerate.

- Generic pads need a real neutral sample before their axes are trusted. A stick held during enumeration, an unused axis idling at its endpoint on some leverless and DualSense modes, and slow drift during a held direction must never teach a new neutral, or release would emit the opposite direction.
- POV neutral comes in several forms across HID drivers, and digital fallbacks stay untrusted until several fully neutral samples, so stuck bits can't become menu navigation.
- Sony DS4 and DualSense pads over DirectInput use Z/Rz for the right stick and Rx/Ry for analog L2/R2. Some fight sticks report the dpad as buttons 12 to 15 instead of a POV hat.
- Guide comes from the undocumented `XInputGetStateEx` (ordinal 100), which is why `xinput9_1_0`, which lacks it, is tried last. TOUCHPAD comes only from the DirectInput PlayStation mapping.
- Bindings above the XInput `wButtons` range are read from triggers and stick axes, so those can be bound like buttons (`ConfigPolicy::PadBindingDown`, `include/utils/config_policy.h`). Rebinding a navigation action onto a button another action uses hands that action the old button.
- First-run controller hotkeys treat any used face button as reserving the whole cluster, because DirectInput numbers them differently by device and `key.ini` stores only the number.

Hotkey detection samples every tracked key on every poll, live or not, so a press only counts if the key went down while live. A key's first sample never counts, because bit 0 of `GetAsyncKeyState` may be stale. User configs are never silently rewritten; collisions are only reported in the log. Hotkey actions that call into EFZ or Revival run as game-thread commands.

## Logging

Log lines wait in a fixed-capacity queue drained only when a console appears, so overflow is reported straight to the file log. High-frequency categories are dropped before any lock or timestamp unless detailed logging is on. QuickEdit is disabled on a console the mod owns, because a selection blocks console writes while `LogOut` holds its mutex, which would stall the game thread.

## Update check

`src/utils/update_check.cpp` asks the GitHub releases API when the menu opens,
on a detached worker, using only a `User-Agent` of
`EfzExtendedPractice/<version>`. A success ends checking for that session; after
a failure the next menu open retries once 5 minutes have passed. Tag comparison logic lives in
`src/utils/version_compare.cpp`, and the tests
(`tests/update_check_policy_tests.cpp`, `tests/version_compare_tests.cpp`)
pin the behavior, including which tags are safe to suggest.

- The worker never touches game memory and swallows every exception, because an escaped one would reach the crash handler's terminate path and kill the game. It's detached because `DLL_PROCESS_DETACH` must not wait on threads under the loader lock.
- WinHTTP is resolved at runtime, because a static import could stop the DLL from loading at all. On XP the check always fails silently, because GitHub needs TLS 1.2.
- Acknowledgement lives in a sidecar file, because `SaveSettings()` truncates the main INI and re-emits only known keys.
- Release tags are attacker-influenced text that reaches a UI buffer, so anything long or outside a safe charset is rejected. `tag_name` comes from a real JSON parse, because the changelog in `body` could contain matching text.
- Tagging rule: any text after the dotted numbers sorts below the final release, so `0.8.2-release` is a pre-release. Use at most four components and alpha, beta or rc.

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
