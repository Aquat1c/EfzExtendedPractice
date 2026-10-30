#pragma once

#include <cstddef>
#include <cstdint>

namespace CollisionDisplay {

struct Status {
    bool hitEnabled = false;
    bool hurtEnabled = false;
    bool collisionEnabled = false;
    bool projectileInteractionsEnabled = false;
    uint32_t toggleCount = 0;
};

enum OverlayShape : uint8_t {
    ShapeRectangle = 0,
    ShapeDot = 1,
};

// Virtual 640x480 overlay rectangle. Colors are stored as AARRGGBB.
struct OverlayBox {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    uint32_t fillArgb = 0;
    uint32_t outlineArgb = 0;
    uint8_t shape = ShapeRectangle;
    bool dottedOutline = false;
};

void Initialize();
void Shutdown();

bool IsAnyLayerEnabled();
Status GetStatus();
void SetLayerEnabled(int layerIndex, bool enabled); // 0=hit, 1=hurt, 2=collision/push, 3=projectile interactions

void RebuildFrame();
std::size_t GetOverlayBoxCount();
bool GetOverlayBox(std::size_t index, OverlayBox* outBox);

// Called from the Revival Practice hotkey dispatcher gate; true when the key
// was consumed by our replacement display and must not reach Revival.
bool ShouldSuppressRevivalHotkey(void* practiceController, int key);
// efz.exe's battle hotkeys or a key in EfzRevival.ini's [Practice] section. False for this
// mod's stock keys, some of which sit on Revival's on purpose. Reads the ini once.
bool IsGameHotkeyVk(int vk);

// Final render camera from EFZ's active battle-screen context. Fighter world
// coordinates remain IEEE-754 doubles; these two values are the game's native
// signed 32-bit projection offsets. False means no coherent on-screen battle
// context and callers must not derive or write world positions.
bool ProbeBattleCameraOffsets(int* outCameraX, int* outCameraY);

// Fixed-layout, read-only view of EFZ's 64-slot per-player entity ring; a
// successful probe writes exactly kProjectileRingSlotCapacity entries.
// readable == false: an active entry could not be sampled; retain prior state
// rather than infer a despawn.
constexpr std::size_t kProjectileRingSlotCapacity = 64;
struct ProjectileRingSlotProbe {
    int slot = -1;
    bool readable = false;
    bool alive = false;
    uint16_t pattern = 0;
    uint16_t frame = 0;
    uint16_t frameTick = 0;
    double x = 0.0;
    double y = 0.0;
    int16_t life = 0;
    uint32_t destroyed = 0;
};

// Allocation/traversal cursors sampled beside the slot endpoints: the common
// allocator writes the slot at `allocationCursor` (+0x2CA) then advances it;
// `scanHead` (+0x2CC) is maintained by the update/collision traversal. Integrity
// evidence only, never a replacement for scanning all 64 alive flags.
// readable == false: cursor movement cannot rule out a spawn+despawn between
// two endpoint samples.
struct ProjectileRingCursorProbe {
    bool readable = false;
    uint16_t allocationCursor = 0;
    uint16_t scanHead = 0;
};

// Reads the alive table once and each active entry at most once; no transforms,
// allocation, pattern filtering, or gameplay mutation. outCapacity must be at
// least kProjectileRingSlotCapacity. A successful sample is an endpoint
// observation, not an atomic cross-slot event ordering guarantee.
bool ProbeProjectileRing(int playerIndex, ProjectileRingSlotProbe* outSlots,
                         std::size_t outCapacity,
                         ProjectileRingCursorProbe* outCursors = nullptr);

// Max `life` among that player's alive projectiles matching `pattern`, or -1 if
// a complete snapshot proves none alive; kProjectileLifeProbeUnavailable means
// the sample was incomplete and must not be read as a despawn. playerIndex is
// 1 or 2. Same bounded sampler as ProbeProjectileRing; no transforms or
// gameplay mutation.
constexpr int kProjectileLifeProbeUnavailable = -2;
int ProbeProjectileLifeForPattern(int playerIndex, uint16_t pattern);

} // namespace CollisionDisplay
