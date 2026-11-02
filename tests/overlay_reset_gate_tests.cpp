#include <cstdint>
#include <cstdio>

#if __has_include("compat/overlay_reset_gate.h")
#include "compat/overlay_reset_gate.h"

namespace {
int failures = 0;
int checks = 0;
void Check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label);
    }
}
using Gate = EfzOverlayResetGate;
using Result = Gate::Result;

void FailedResetKeepsResourcesInvalidated() {
    Gate gate;
    Gate::Access access(gate);
    int device;
    Check(gate.BindBackend(&device, 7), "bind game backend");
    bool invalidate = false;
    Check(gate.BeforeReset(&device, 1, 7, invalidate) == Result::Ok && invalidate,
          "first reset requires owner invalidation");
    gate.DidInvalidateBackend();
    Check(gate.AfterReset(&device, 1, 7, 0) == Result::Ok,
          "failed reset notification accepted");
    Check(!gate.CanRender(&device, 7), "failed reset prevents device work");
    Check(gate.BeforeReset(&device, 1, 7, invalidate) == Result::Ok && !invalidate,
          "same-epoch retry does not invalidate twice");
    Check(gate.BeforeReset(&device, 2, 7, invalidate) == Result::Ok && !invalidate,
          "new recovery attempt keeps already-invalidated backend");
    Check(gate.AfterReset(&device, 1, 7, 1) == Result::InvalidArgument,
          "stale completion cannot resume a newer epoch");
    Check(!gate.CanRender(&device, 7), "stale completion leaves gate closed");
    Check(gate.AfterReset(&device, 2, 7, 1) == Result::Ok,
          "ready matching epoch resumes");
    Check(gate.CanRender(&device, 7), "ready backend can render again");
    Check(gate.BeforeReset(&device, 3, 7, invalidate) == Result::Ok && invalidate,
          "same-size next reset invalidates the new backend objects");
    gate.DidInvalidateBackend();
    Check(gate.AfterReset(&device, 3, 7, 1) == Result::Ok,
          "completed overlay resumes while other participants may remain pending");
    Check(gate.BeforeReset(&device, 3, 7, invalidate) == Result::Ok && invalidate,
          "reopened overlay can requiesce in the same transaction epoch");
}

void DeviceAndThreadIdentityAreChecked() {
    Gate gate;
    Gate::Access access(gate);
    int game, other;
    Check(gate.BindBackend(&game, 4), "bind thread-owned backend");
    bool invalidate = false;
    Check(gate.BeforeReset(&game, 1, 8, invalidate) == Result::WrongThread,
          "wrong thread cannot invalidate owned backend");
    Check(gate.CanRender(&game, 4), "rejected callback did not close owner gate");
    Check(!gate.CanRender(&game, 8), "foreign thread cannot use ImGui backend");
    Check(gate.BeforeReset(&game, 1, 4, invalidate) == Result::Ok,
          "owner thread may start reset");
    gate.DidInvalidateBackend();
    Check(gate.AfterReset(&other, 1, 4, 1) == Result::InvalidArgument,
          "other device cannot reopen gate");
    Check(gate.AfterReset(&game, 1, 8, 1) == Result::WrongThread,
          "other thread cannot complete reset");
    Check(gate.BeforeReset(&other, 2, 4, invalidate) == Result::Busy,
          "second pending device is refused");
    Check(gate.BeforeReset(&game, 0, 4, invalidate) == Result::InvalidArgument,
          "zero epoch rejected");
    Check(gate.AfterReset(&game, 1, 4, 2) == Result::InvalidArgument,
          "invalid readiness value rejected");
    Check(gate.AfterReset(&game, 1, 4, 1) == Result::Ok,
          "correct completion survives rejected notifications");
}

void LateInitializationAndFallbackStaySeparated() {
    Gate gate;
    Gate::Access access(gate);
    int game, fallback;
    bool invalidate = true;
    Check(gate.BeforeReset(&game, 10, 1, invalidate) == Result::Ok && !invalidate,
          "uninitialized participant acknowledges without backend work");
    Check(!gate.BindBackend(&game, 1), "late game initialization stays blocked");
    Check(gate.BindBackend(&fallback, 9), "other host may initialize independently");
    Check(gate.CanRender(&fallback, 9), "fallback host continues rendering");
    Check(gate.BeforeReset(&game, 10, 1, invalidate) == Result::Ok && !invalidate,
          "game reset never invalidates fallback-device objects");
    Check(gate.AfterReset(&game, 10, 1, 1) == Result::Ok,
          "game cancellation does not need fallback owner thread");
    Check(gate.CanRender(&fallback, 9), "game completion preserves fallback binding");
    gate.UnbindBackend();
    Check(gate.BindBackend(&game, 1), "game may bind after fallback teardown");
}

void ShutdownDoesNotErasePendingRecovery() {
    Gate gate;
    Gate::Access access(gate);
    int game;
    bool invalidate = false;
    Check(gate.BindBackend(&game, 2), "bind before online suspension");
    Check(gate.BeforeReset(&game, 5, 2, invalidate) == Result::Ok,
          "quiesce before online suspension");
    gate.DidInvalidateBackend();
    gate.UnbindBackend();
    Check(!gate.CanRender(&game, 2), "teardown leaves pending device gated");
    Check(!gate.BindBackend(&game, 2), "teardown cannot permit premature reinit");
    Check(gate.AfterReset(&game, 5, 2, 1) == Result::Ok,
          "completion after shutdown merely opens admission");
    Check(gate.BoundDevice() == nullptr, "completion never recreates suspended backend");
}

void ReentrantWorkIsRefusedWithoutWaiting() {
    Gate gate;
    Gate::Access render(gate);
    Check(render.Acquired(), "render owns gate");
    Gate::Access reset(gate);
    Check(!reset.Acquired(), "reentrant reset is busy");
    render.Release();
    Gate::Access next(gate);
    Check(next.Acquired(), "next callback admitted after render returns");
}

void IncompleteInvalidationCannotBeAcknowledgedByRetry() {
    Gate gate;
    Gate::Access access(gate);
    int game;
    bool invalidate = false;
    gate.BindBackend(&game, 1);
    gate.BeforeReset(&game, 7, 1, invalidate);
    Check(invalidate, "initial callback needs invalidation");
    Check(gate.BeforeReset(&game, 7, 1, invalidate) == Result::Ok && invalidate,
          "failed owner callback must retry invalidation");
    Check(gate.AfterReset(&game, 7, 1, 1) == Result::Busy,
          "missing invalidation acknowledgement cannot reopen a bound backend");
    Check(!gate.CanRender(&game, 1), "incomplete owner invalidation stays gated");
    gate.DidInvalidateBackend();
    Check(gate.BeforeReset(&game, 6, 1, invalidate) == Result::InvalidArgument,
          "older BeforeReset cannot replace newer pending epoch");
}
}

int main() {
    FailedResetKeepsResourcesInvalidated();
    DeviceAndThreadIdentityAreChecked();
    LateInitializationAndFallbackStaySeparated();
    ShutdownDoesNotErasePendingRecovery();
    ReentrantWorkIsRefusedWithoutWaiting();
    IncompleteInvalidationCannotBeAcknowledgedByRetry();
    std::printf("overlay_reset_gate: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
#else
int main() {
    std::fputs("FAIL: cooperative overlay reset gate is not implemented\n", stderr);
    return 1;
}
#endif
