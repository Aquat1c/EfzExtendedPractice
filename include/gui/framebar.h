#pragma once
// Sampled once per engine tick; cells fold to visual frames when displayed.
// Drawn once per EndScene when enabled, independent of menu visibility.

#include <atomic>
#include <cstdint>

namespace FrameBar {

enum class Cat : uint8_t {
    None = 0,
    Neutral,
    WalkFwd,
    WalkBack,
    PreJump,
    JumpNeutral,
    JumpFwd,
    JumpBack,
    DoubleJumpNeutral,
    DoubleJumpFwd,
    DoubleJumpBack,
    Falling,
    Landing,
    Crouch,
    DashFwd,
    DashBack,
    AirDashNeutral,
    AirDashFwd,
    AirDashBack,
    AttackStartup,
    AttackActive,
    AttackRecovery,
    Blockstun,
    Hitstun,
    Launched,
    AirtechFwd,
    AirtechBack,
    Groundtech,
    SpecialStun,    // fire / electric / frozen
    Thrown,
    RG,
    SuperFlash,     // IC/super screen freeze
    HitstopShared,  // this side held by its own hit-hitstop
    COUNT_
};

constexpr int kBarMemory = 240;     // ring buffer length (subframe samples by default)

extern std::atomic<bool> g_enabled;

// Called from OnEngineTick on the game thread, inside a Practice battle only.
// Populates the next ring-buffer cell; never writes game memory.
void TickSample();

struct DrawCtx {
    float ox;
    float oy;
    float scale;     // game-area scale factor (1.0 = native 640x480)
};
void Render(const DrawCtx& ctx);

void Reset();

void ResetTimeline();

}
