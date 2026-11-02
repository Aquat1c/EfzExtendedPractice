# Overlay reset contract (DisplayManager cooperation), version 1

When the game's Direct3D 9 device is reset — a window resize, a fullscreen
toggle, recovery from a lost device — every mod holding device resources must
release them before the reset and recreate them after. This document specifies
the ABI through which EfzDisplayManager coordinates this mod's ImGui overlay
backend across such resets. The ABI governs this mod's own backend only: it
does not authorize another DLL to release this mod's COM references and does
not certify resources retained by third-party renderers.

## ABI and discovery

The DLL exports the undecorated C symbol `EFZ_QueryOverlayResetApiV1`. It uses
`__cdecl` and returns a pointer to a static table described in
`include/compat/overlay_reset_api.h`. Querying it does not initialize ImGui,
install a hook, or create a device. The table is 16 bytes in the supported x86
build:

| Field | Meaning |
| --- | --- |
| `size` | `sizeof(EfzOverlayResetApiV1)` |
| `version` | `1` |
| `beforeReset(void* device, uint32_t epoch)` | Quiesce the target and invalidate only matching owner resources |
| `afterReset(void* device, uint32_t epoch, uint32_t ready)` | Retain the gate, or recreate owner objects and reopen admission |

The caller must hold a module reference through all outstanding callbacks, use
a nonzero epoch, and call synchronously at the real game renderer's safe
boundary. The participant records the thread that initialized its
context/backend; this is an observed overlay owner thread, **not proof of the
D3D device creation thread**. D3D9 still requires Reset on its actual creation
thread.

## Transaction semantics

1. `beforeReset` returns `S_OK` only after the matching backend has released
   its own vertex buffer, index buffer, and font texture through
   `ImGui_ImplDX9_InvalidateDeviceObjects`. The context, CPU font atlas, Win32
   backend, WndProc chain, visibility, and online/practice policy remain
   intact. If this participant has no backend for the device, it acknowledges
   without touching another device, but still gates late initialization for
   the target.
2. Repeating `beforeReset` while already quiesced does not release the same
   objects again. A newer epoch can continue pending recovery for the same
   device/thread. Older epochs and an incompatible second pending device are
   refused. A participant reopened during partial multi-overlay recovery can
   be quiesced again with the same epoch.
3. `afterReset(..., 0)` leaves rendering, input handling, and lazy backend
   initialization gated. It performs no normal device work. Keep this state
   after a failed Reset or incomplete required wrapper-resource
   reconstruction.
4. Call `afterReset(..., 1)` only after an actual successful Reset and
   required wrapper reconstruction, or to cancel a transition before **any**
   real Reset. For a matching backend it verifies cooperative status, selects
   the owned ImGui context, and recreates the backend's device objects.
   `S_OK` opens admission; creation failure releases any partially created
   owner objects and leaves the gate closed. Retry the same notification after
   the device/renderer is ready. ImGui's vertex/index buffers grow lazily
   during normal rendering, as before.
5. A backend suspended for online simulation is not reinitialized by a ready
   notification. Its existing menu/online gates decide whether a later frame
   should initialize it. A separate host/device never has its resources
   invalidated merely because the game resets.

`E_INVALIDARG` reports an invalid identity, epoch, or readiness value.
`RPC_E_WRONG_THREAD` rejects work on the wrong observed owner/transaction
thread. `HRESULT_FROM_WIN32(ERROR_BUSY)` refuses concurrent or reentrant work
without waiting. Resource recreation can return the actual cooperative-level
failure or `E_FAIL`. A refusal is not permission to release another owner's
resources or to continue a voluntary reset.

## Rendering and context ownership

A nonblocking gate covers backend initialization, rendering and Win32 ImGui
input. Reset-induced window-message reentrancy passes through without
touching ImGui. Callback context selection is scoped and restores the
caller's prior context, including a null context. Backend rebinds use device
identity and the existing host; they do not silently accept an unrelated
device.

All normal device work must also honor cooperative-level loss and
DisplayManager's required-renderer readiness. Size changes are not used to
infer reset: an unchanged-size Reset is an epoch transition too.

## Scope and integration boundary

Training's external `EFZ_RegisterOverlayRenderer` ABI has no reset
notification. Gating its dispatch stops draw calls during recovery but cannot
release a registered renderer's retained resources. Such renderers need their
own cooperative contract or must be excluded from voluntary reset
configurations. Managed GIF, mission-atlas and battle-log icon textures retain
their normal owner lifetimes.

A DisplayManager implementation that coordinates both owner callbacks around
every supported device Reset must distinguish real Reset success, required
wrapper readiness, and each participant's reconstruction result, and must
retain callback/module lifetimes until recovery or safe cancellation
completes. A release-to-zero compatibility sweep is not compatible with this
contract.

## Testing

The reusable gate tests (`overlay_reset_gate_tests`) cover failed-reset
retention, repeated/same-size/same-epoch recovery, stale callback rejection,
wrong device/thread rejection, late initialization, fallback isolation,
pending recovery across shutdown, and nonblocking reentrant refusal. The
context tests (`overlay_context_scope_tests`) execute against the real
portable ImGui sources and cover owner selection, caller restoration,
null-current preservation, and destroyed-context handling. Both targets are
registered with CTest; run them with `ctest --output-on-failure`. A direct
host build of the gate tests also works:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -Iinclude tests/overlay_reset_gate_tests.cpp -o overlay_reset_gate_tests
./overlay_reset_gate_tests
```

Host tests and x86 compilation do not replace a Windows D3D9 runtime trace:
failed Reset, cancellation before Reset, partial reconstruction failure,
same-size Reset, late backend initialization, font rebuild/buffer growth,
device replacement, and unchanged online/practice behavior all need
verification on a real D3D9 runtime after any change to this contract.
