#include "../../include/game/collision_display.h"
#include "../../include/game/collision_box_policy.h"

#include "../../include/core/constants.h"
#include "../../include/core/di_keycodes.h"
#include "../../include/core/logger.h"
#include "../../include/core/memory.h"
#include "../../include/game/game_state.h"
#include "../../include/utils/config.h"
#include "../../include/utils/network.h"
#include "../../include/utils/utilities.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace CollisionDisplay {
namespace {

constexpr uint8_t kGameplayScreenIndex = 3;
constexpr uintptr_t kRvaGameModeArray = 0x00390110;
constexpr uintptr_t kRvaTransformEntityHitbox = 0x00367600;    // efz.exe VA 0x00767600
constexpr uintptr_t kRvaTransformCharacterHitbox = 0x003677A0; // efz.exe VA 0x007677A0

constexpr uint8_t kLayerHit = 1u << 0;
constexpr uint8_t kLayerHurt = 1u << 1;
constexpr uint8_t kLayerCollision = 1u << 2;
constexpr uint8_t kLayerProjectileInteractions = 1u << 3;
constexpr int kLayerCount = 4;

constexpr std::size_t kFrameBlobBytes = 200u;
constexpr std::size_t kCharacterSnapshotBytes = 236u;

constexpr std::size_t kOffsetAnimFrame = MOVE_ID_OFFSET; // pattern/state id
constexpr std::size_t kOffsetSubframe = CURRENT_FRAME_INDEX_OFFSET;
constexpr std::size_t kOffsetAnimMetaTable = 0x10u;
constexpr std::size_t kOffsetCameraX = 1116u;
constexpr std::size_t kOffsetCameraY = 1120u;

constexpr std::size_t kHurtRectBase = 0u;
constexpr std::size_t kHitRectBase = 80u;
constexpr std::size_t kCollisionRectBase = 144u;
constexpr std::size_t kBlobAttackFlags = 170u;
constexpr std::size_t kBlobSuppressHurtFlag = 176u;
constexpr std::size_t kOffsetAttackState = 364u;
constexpr std::size_t kOffsetHitImmunity = 316u;

constexpr int kHurtRectCount = 5;
constexpr int kHitRectCount = 4;
constexpr int kCollisionRectCount = 1;

// Stock EFZ projectile ring; matches FrameBar/processProjectileCollision.
constexpr std::size_t kProjectileHeadOffset = 0x2CCu;
constexpr std::size_t kProjectileTailOffset = 0x2CAu;
constexpr std::size_t kProjectileAliveFlagsOffset = 0x3D0u;
constexpr std::size_t kProjectileEntryBaseOffset = 0x4D0u;
constexpr std::size_t kProjectileEntryStride = 0x98u;
constexpr std::size_t kProjectilePatternOffset = 0x00u;
constexpr std::size_t kProjectileFrameOffset = 0x02u;
constexpr std::size_t kProjectileFrameTickOffset = 0x04u;
constexpr std::size_t kProjectileXOffset = 0x18u;
constexpr std::size_t kProjectileYOffset = 0x20u;
constexpr std::size_t kProjectileDestroyedOffset = 0x80u;
constexpr std::size_t kProjectileLifeOffset = 0x84u;
constexpr int kProjectileSlotCount = 64;

constexpr std::size_t kOffsetActionFrameTick = 0x0Cu;
constexpr std::size_t kNagamoriActivationFlagBaseOffset = 12652u;

constexpr uint32_t kColorHitOutline = 0xFFFF2828u;       // red
constexpr uint32_t kColorHurtOutline = 0xFF00DC00u;      // green
constexpr uint32_t kColorCollisionOutline = 0xFFFFDC00u; // yellow
constexpr uint32_t kColorProjectileOutline = 0xFF28C8FFu; // cyan
constexpr uint32_t kColorProjectileInactiveOutline = 0xAA28C8FFu;
constexpr float kInactiveHitFillScale = 0.2f;
constexpr uint32_t kColorProjectileDot = 0xFFFFFFFFu;
constexpr uint32_t kColorProjectileIntersect = 0xFFFF40FFu; // magenta
constexpr uint32_t kColorNagamoriRange = 0xFFFFA000u;       // orange
constexpr uint32_t kColorNagamoriAffected = 0xFFFFE070u;

using TransformEntityHitboxFn = int* (__stdcall*)(int* outRect, unsigned short* entityHeader, int* localRect);
using TransformCharacterHitboxFn = int* (__stdcall*)(int* outRect, int characterSnapshot, int* localRect);

struct RectI {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct DrawRect {
    RectI rect;
    uint32_t color = 0;
    bool inactive = false;
};

struct ProjectileInfo {
    int playerIndex = 0;
    uintptr_t owner = 0;
    uintptr_t entryBase = 0;
    int slot = -1;
    uint16_t pattern = 0;
    uint16_t frame = 0;
    uint16_t frameTick = 0;
    int16_t priority = 0;
    int16_t life = 0;
    uint32_t destroyed = 0;
    double x = 0.0;
    double y = 0.0;
    float screenX = 0.0f;
    float screenY = 0.0f;
    std::vector<RectI> hitRects;
    bool hitActive = true;
    bool hasCollisionRect = false;
    bool nagamoriOwner = false;
    bool nagamoriFlagged = false;
    RectI gameRect;
    RectI screenRect;
};

struct NagamoriActivationRange {
    uintptr_t owner = 0;
    int sourceSlot = -1;
    uint16_t sourcePattern = 0;
    uint16_t sourceMove = 0;
    double centerX = 0.0;
    double centerY = 0.0;
    int rangeX = 0;
    int rangeY = 0;
    bool triggerFrame = false;
    bool previewWhenInactive = true;
    bool sourceIsCharacter = false;
    RectI screenRect;
};

std::mutex g_mutex;
std::atomic<bool> g_initialized{false};

uint32_t g_toggleCount = 0;
std::vector<DrawRect> g_frameRects;
std::vector<OverlayBox> g_overlayBoxes;

TransformEntityHitboxFn g_transformEntity = nullptr;
TransformCharacterHitboxFn g_transformCharacter = nullptr;

struct RevivalDisplayHotkeys {
    int hit = DIK_PRIOR;
    int hurt = DIK_NEXT;
    int collision = DIK_HOME;
    bool loaded = false;
    char iniPath[MAX_PATH] = {};
};

RevivalDisplayHotkeys g_revivalHotkeys;

template <typename T>
bool ReadValue(uintptr_t address, T& out) {
    return SafeReadMemory(address, &out, sizeof(T));
}

bool ReadBytes(uintptr_t address, void* out, std::size_t size) {
    return SafeReadMemory(address, out, size);
}

uint32_t ColorWithAlphaByte(uint32_t outlineColor, uint8_t alpha) {
    return (outlineColor & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha) << 24);
}

int ClampAlphaPercent(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

uint8_t FillAlphaByte(float multiplier = 1.0f) {
    const int percent = ClampAlphaPercent(Config::GetSettings().collisionDisplayFillAlphaPercent);
    int alpha = static_cast<int>((static_cast<float>(percent) * 255.0f * multiplier / 100.0f) + 0.5f);
    if (alpha < 0) {
        alpha = 0;
    }
    if (alpha > 255) {
        alpha = 255;
    }
    return static_cast<uint8_t>(alpha);
}

bool LayerEnabledFromSettings(int layerIndex) {
    const Config::Settings& s = Config::GetSettings();
    switch (layerIndex) {
    case 0:
        return s.collisionDisplayHitboxes;
    case 1:
        return s.collisionDisplayHurtboxes;
    case 2:
        return s.collisionDisplayCollisionBoxes;
    case 3:
        return s.collisionDisplayProjectileInteractions;
    default:
        return false;
    }
}

bool PlayerLayerFilterEnabledFromSettings(int layerIndex, int playerIndex) {
    if (playerIndex != 1 && playerIndex != 2) {
        return false;
    }

    const Config::Settings& s = Config::GetSettings();
    const bool p1 = playerIndex == 1;
    switch (layerIndex) {
    case 0:
        return p1 ? s.collisionDisplayP1Hitboxes
                  : s.collisionDisplayP2Hitboxes;
    case 1:
        return p1 ? s.collisionDisplayP1Hurtboxes
                  : s.collisionDisplayP2Hurtboxes;
    case 2:
        return p1 ? s.collisionDisplayP1CollisionBoxes
                  : s.collisionDisplayP2CollisionBoxes;
    case 3:
        return true;
    default:
        return false;
    }
}

bool PlayerLayerEnabledFromSettings(int layerIndex, int playerIndex) {
    return LayerEnabledFromSettings(layerIndex) &&
           PlayerLayerFilterEnabledFromSettings(layerIndex, playerIndex);
}

bool AnyPlayerEnabledForLayer(int layerIndex) {
    return PlayerLayerEnabledFromSettings(layerIndex, 1) ||
           PlayerLayerEnabledFromSettings(layerIndex, 2);
}

const char* LayerConfigKey(int layerIndex) {
    switch (layerIndex) {
    case 0:
        return "collisionDisplayHitboxes";
    case 1:
        return "collisionDisplayHurtboxes";
    case 2:
        return "collisionDisplayCollisionBoxes";
    case 3:
        return "collisionDisplayProjectileInteractions";
    default:
        return nullptr;
    }
}

uint8_t CurrentLayerMaskFromSettings() {
    uint8_t mask = 0;
    for (int i = 0; i < kLayerCount; ++i) {
        if (AnyPlayerEnabledForLayer(i)) {
            mask = static_cast<uint8_t>(mask | (1u << i));
        }
    }
    return mask;
}

bool AnyProjectileInteractionSubLayerEnabled() {
    const Config::Settings& s = Config::GetSettings();
    return s.collisionDisplayProjectileBoxes
        || s.collisionDisplayProjectileOrigins
        || s.collisionDisplayProjectileIntersections
        || s.collisionDisplayNagamoriRanges
        || s.collisionDisplayNagamoriAffected
        ;
}

bool HasAnyEnabledLayer() {
    const uint8_t mask = CurrentLayerMaskFromSettings();
    if ((mask & (kLayerHit | kLayerHurt | kLayerCollision)) != 0) {
        return true;
    }
    return (mask & kLayerProjectileInteractions) != 0
        && AnyProjectileInteractionSubLayerEnabled();
}

bool SetLayerEnabledNoLock(int layerIndex, bool enabled, const char* originLabel) {
    const char* key = LayerConfigKey(layerIndex);
    if (!key) {
        return false;
    }

    const bool current = LayerEnabledFromSettings(layerIndex);
    if (current == enabled) {
        return false;
    }

    Config::SetSetting("General", key, enabled ? "1" : "0");
    ++g_toggleCount;

    static const char* const kNames[kLayerCount] = {
        "Hitboxes",
        "Hurtboxes",
        "Collision boxes",
        "Projectile interactions",
    };
    std::ostringstream oss;
    oss << "[COLLISION_DISPLAY] " << kNames[layerIndex] << ' ' << (enabled ? "ON" : "OFF");
    if (originLabel && originLabel[0] != '\0') {
        oss << " (" << originLabel << ')';
    }
    LogOut(oss.str(), true);
    return true;
}

void StripFilename(char* path) {
    if (!path) {
        return;
    }
    char* slash = path + std::strlen(path);
    while (slash > path && slash[-1] != '\\' && slash[-1] != '/') {
        --slash;
    }
    *slash = '\0';
}

bool AppendPath(char* path, std::size_t pathSize, const char* leaf) {
    if (!path || !leaf || pathSize == 0) {
        return false;
    }

    const std::size_t len = std::strlen(path);
    if (len + std::strlen(leaf) + 1 >= pathSize) {
        return false;
    }
    std::strcat(path, leaf);
    return true;
}

bool FileExistsA(const char* path) {
    if (!path || !*path) {
        return false;
    }
    const DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ResolveRevivalIniPath(char* outPath, std::size_t outPathSize) {
    if (!outPath || outPathSize == 0) {
        return false;
    }
    outPath[0] = '\0';

    auto tryFromModule = [&](HMODULE module) -> bool {
        char path[MAX_PATH] = {};
        const DWORD len = GetModuleFileNameA(module, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            return false;
        }
        StripFilename(path);
        if (!AppendPath(path, sizeof(path), "EfzRevival.ini") || !FileExistsA(path)) {
            return false;
        }
        std::strncpy(outPath, path, outPathSize - 1);
        outPath[outPathSize - 1] = '\0';
        return true;
    };

    if (HMODULE revival = GetModuleHandleA("EfzRevival.dll")) {
        if (tryFromModule(revival)) {
            return true;
        }
    }
    if (tryFromModule(nullptr)) {
        return true;
    }

    char cwd[MAX_PATH] = {};
    const DWORD cwdLen = GetCurrentDirectoryA(MAX_PATH, cwd);
    if (cwdLen == 0 || cwdLen >= MAX_PATH) {
        return false;
    }
    const std::size_t len = std::strlen(cwd);
    if (len > 0 && cwd[len - 1] != '\\' && cwd[len - 1] != '/') {
        if (!AppendPath(cwd, sizeof(cwd), "\\")) {
            return false;
        }
    }
    if (!AppendPath(cwd, sizeof(cwd), "EfzRevival.ini") || !FileExistsA(cwd)) {
        return false;
    }
    std::strncpy(outPath, cwd, outPathSize - 1);
    outPath[outPathSize - 1] = '\0';
    return true;
}

std::string NormalizeDikValue(const char* rawValue) {
    std::string value = rawValue ? rawValue : "";
    const std::size_t commentPos = value.find_first_of(";#");
    if (commentPos != std::string::npos) {
        value.erase(commentPos);
    }

    const auto isNotSpace = [](unsigned char ch) {
        return !std::isspace(ch);
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());

    const std::size_t splitPos = value.find_first_of(" \t\r\n");
    if (splitPos != std::string::npos) {
        value.erase(splitPos);
    }

    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

int ParseDikValue(const char* rawValue, int fallback) {
    const std::string value = NormalizeDikValue(rawValue);
    if (value.empty()) {
        return fallback;
    }

    char* end = nullptr;
    const int base = (value.size() > 2 && value[0] == '0' && value[1] == 'X') ? 16 : 10;
    const long numeric = std::strtol(value.c_str(), &end, base);
    if (end && *end == '\0' && numeric > 0 && numeric <= 0xFF) {
        return static_cast<int>(numeric);
    }

    struct NameCode {
        const char* name;
        int code;
    };
#define EFZ_DIK_NAME(name) { #name, name }
    static const NameCode kNames[] = {
        EFZ_DIK_NAME(DIK_ESCAPE), EFZ_DIK_NAME(DIK_1), EFZ_DIK_NAME(DIK_2), EFZ_DIK_NAME(DIK_3),
        EFZ_DIK_NAME(DIK_4), EFZ_DIK_NAME(DIK_5), EFZ_DIK_NAME(DIK_6), EFZ_DIK_NAME(DIK_7),
        EFZ_DIK_NAME(DIK_8), EFZ_DIK_NAME(DIK_9), EFZ_DIK_NAME(DIK_0), EFZ_DIK_NAME(DIK_MINUS),
        EFZ_DIK_NAME(DIK_EQUALS), EFZ_DIK_NAME(DIK_BACK), EFZ_DIK_NAME(DIK_BACKSPACE),
        EFZ_DIK_NAME(DIK_TAB), EFZ_DIK_NAME(DIK_Q), EFZ_DIK_NAME(DIK_W), EFZ_DIK_NAME(DIK_E),
        EFZ_DIK_NAME(DIK_R), EFZ_DIK_NAME(DIK_T), EFZ_DIK_NAME(DIK_Y), EFZ_DIK_NAME(DIK_U),
        EFZ_DIK_NAME(DIK_I), EFZ_DIK_NAME(DIK_O), EFZ_DIK_NAME(DIK_P), EFZ_DIK_NAME(DIK_LBRACKET),
        EFZ_DIK_NAME(DIK_RBRACKET), EFZ_DIK_NAME(DIK_RETURN), EFZ_DIK_NAME(DIK_LCONTROL),
        EFZ_DIK_NAME(DIK_A), EFZ_DIK_NAME(DIK_S), EFZ_DIK_NAME(DIK_D), EFZ_DIK_NAME(DIK_F),
        EFZ_DIK_NAME(DIK_G), EFZ_DIK_NAME(DIK_H), EFZ_DIK_NAME(DIK_J), EFZ_DIK_NAME(DIK_K),
        EFZ_DIK_NAME(DIK_L), EFZ_DIK_NAME(DIK_SEMICOLON), EFZ_DIK_NAME(DIK_APOSTROPHE),
        EFZ_DIK_NAME(DIK_GRAVE), EFZ_DIK_NAME(DIK_LSHIFT), EFZ_DIK_NAME(DIK_BACKSLASH),
        EFZ_DIK_NAME(DIK_Z), EFZ_DIK_NAME(DIK_X), EFZ_DIK_NAME(DIK_C), EFZ_DIK_NAME(DIK_V),
        EFZ_DIK_NAME(DIK_B), EFZ_DIK_NAME(DIK_N), EFZ_DIK_NAME(DIK_M), EFZ_DIK_NAME(DIK_COMMA),
        EFZ_DIK_NAME(DIK_PERIOD), EFZ_DIK_NAME(DIK_SLASH), EFZ_DIK_NAME(DIK_RSHIFT),
        EFZ_DIK_NAME(DIK_MULTIPLY), EFZ_DIK_NAME(DIK_NUMPADSTAR), EFZ_DIK_NAME(DIK_LMENU),
        EFZ_DIK_NAME(DIK_LALT), EFZ_DIK_NAME(DIK_SPACE), EFZ_DIK_NAME(DIK_CAPITAL),
        EFZ_DIK_NAME(DIK_CAPSLOCK), EFZ_DIK_NAME(DIK_F1), EFZ_DIK_NAME(DIK_F2), EFZ_DIK_NAME(DIK_F3),
        EFZ_DIK_NAME(DIK_F4), EFZ_DIK_NAME(DIK_F5), EFZ_DIK_NAME(DIK_F6), EFZ_DIK_NAME(DIK_F7),
        EFZ_DIK_NAME(DIK_F8), EFZ_DIK_NAME(DIK_F9), EFZ_DIK_NAME(DIK_F10), EFZ_DIK_NAME(DIK_NUMLOCK),
        EFZ_DIK_NAME(DIK_SCROLL), EFZ_DIK_NAME(DIK_NUMPAD7), EFZ_DIK_NAME(DIK_NUMPAD8),
        EFZ_DIK_NAME(DIK_NUMPAD9), EFZ_DIK_NAME(DIK_SUBTRACT), EFZ_DIK_NAME(DIK_NUMPADMINUS),
        EFZ_DIK_NAME(DIK_NUMPAD4), EFZ_DIK_NAME(DIK_NUMPAD5), EFZ_DIK_NAME(DIK_NUMPAD6),
        EFZ_DIK_NAME(DIK_ADD), EFZ_DIK_NAME(DIK_NUMPADPLUS), EFZ_DIK_NAME(DIK_NUMPAD1),
        EFZ_DIK_NAME(DIK_NUMPAD2), EFZ_DIK_NAME(DIK_NUMPAD3), EFZ_DIK_NAME(DIK_NUMPAD0),
        EFZ_DIK_NAME(DIK_DECIMAL), EFZ_DIK_NAME(DIK_NUMPADPERIOD), EFZ_DIK_NAME(DIK_OEM_102),
        EFZ_DIK_NAME(DIK_F11), EFZ_DIK_NAME(DIK_F12), EFZ_DIK_NAME(DIK_F13), EFZ_DIK_NAME(DIK_F14),
        EFZ_DIK_NAME(DIK_F15), EFZ_DIK_NAME(DIK_KANA), EFZ_DIK_NAME(DIK_ABNT_C1),
        EFZ_DIK_NAME(DIK_CONVERT), EFZ_DIK_NAME(DIK_NOCONVERT), EFZ_DIK_NAME(DIK_YEN),
        EFZ_DIK_NAME(DIK_ABNT_C2), EFZ_DIK_NAME(DIK_NUMPADEQUALS), EFZ_DIK_NAME(DIK_PREVTRACK),
        EFZ_DIK_NAME(DIK_AT), EFZ_DIK_NAME(DIK_COLON), EFZ_DIK_NAME(DIK_UNDERLINE),
        EFZ_DIK_NAME(DIK_KANJI), EFZ_DIK_NAME(DIK_STOP), EFZ_DIK_NAME(DIK_AX),
        EFZ_DIK_NAME(DIK_UNLABELED), EFZ_DIK_NAME(DIK_NEXTTRACK), EFZ_DIK_NAME(DIK_NUMPADENTER),
        EFZ_DIK_NAME(DIK_RCONTROL), EFZ_DIK_NAME(DIK_MUTE), EFZ_DIK_NAME(DIK_CALCULATOR),
        EFZ_DIK_NAME(DIK_PLAYPAUSE), EFZ_DIK_NAME(DIK_MEDIASTOP), EFZ_DIK_NAME(DIK_VOLUMEDOWN),
        EFZ_DIK_NAME(DIK_VOLUMEUP), EFZ_DIK_NAME(DIK_WEBHOME), EFZ_DIK_NAME(DIK_NUMPADCOMMA),
        EFZ_DIK_NAME(DIK_DIVIDE), EFZ_DIK_NAME(DIK_NUMPADSLASH), EFZ_DIK_NAME(DIK_SYSRQ),
        EFZ_DIK_NAME(DIK_RMENU), EFZ_DIK_NAME(DIK_RALT), EFZ_DIK_NAME(DIK_PAUSE), EFZ_DIK_NAME(DIK_HOME),
        EFZ_DIK_NAME(DIK_UP), EFZ_DIK_NAME(DIK_UPARROW), EFZ_DIK_NAME(DIK_PRIOR), EFZ_DIK_NAME(DIK_PGUP),
        EFZ_DIK_NAME(DIK_LEFT), EFZ_DIK_NAME(DIK_LEFTARROW), EFZ_DIK_NAME(DIK_RIGHT),
        EFZ_DIK_NAME(DIK_RIGHTARROW), EFZ_DIK_NAME(DIK_END), EFZ_DIK_NAME(DIK_DOWN),
        EFZ_DIK_NAME(DIK_DOWNARROW), EFZ_DIK_NAME(DIK_NEXT), EFZ_DIK_NAME(DIK_PGDN),
        EFZ_DIK_NAME(DIK_INSERT), EFZ_DIK_NAME(DIK_DELETE), EFZ_DIK_NAME(DIK_LWIN),
        EFZ_DIK_NAME(DIK_RWIN), EFZ_DIK_NAME(DIK_APPS), EFZ_DIK_NAME(DIK_POWER),
        EFZ_DIK_NAME(DIK_SLEEP), EFZ_DIK_NAME(DIK_WAKE), EFZ_DIK_NAME(DIK_WEBSEARCH),
        EFZ_DIK_NAME(DIK_WEBFAVORITES), EFZ_DIK_NAME(DIK_WEBREFRESH), EFZ_DIK_NAME(DIK_WEBSTOP),
        EFZ_DIK_NAME(DIK_WEBFORWARD), EFZ_DIK_NAME(DIK_WEBBACK), EFZ_DIK_NAME(DIK_MYCOMPUTER),
        EFZ_DIK_NAME(DIK_MAIL), EFZ_DIK_NAME(DIK_MEDIASELECT),
    };
#undef EFZ_DIK_NAME

    for (const NameCode& entry : kNames) {
        if (value == entry.name) {
            return entry.code;
        }
    }
    return fallback;
}

int ReadRevivalHotkeyFromIni(const char* path, const char* keyName, int fallback) {
    if (!path || !*path || !keyName || !*keyName) {
        return fallback;
    }

    char value[96] = {};
    const DWORD chars = GetPrivateProfileStringA("Practice", keyName, "", value, sizeof(value), path);
    if (chars == 0) {
        return fallback;
    }
    return ParseDikValue(value, fallback);
}

void RefreshRevivalHotkeysLocked() {
    if (g_revivalHotkeys.loaded) return;

    RevivalDisplayHotkeys next{};
    next.loaded = true;
    if (ResolveRevivalIniPath(next.iniPath, sizeof(next.iniPath))) {
        next.hit = ReadRevivalHotkeyFromIni(next.iniPath, "ToggleHitBoxes", next.hit);
        next.hurt = ReadRevivalHotkeyFromIni(next.iniPath, "ToggleHurtBoxes", next.hurt);
        next.collision = ReadRevivalHotkeyFromIni(next.iniPath, "ToggleCollisionBoxes", next.collision);
    }

    g_revivalHotkeys = next;
}

bool ResolveTransformFunctions() {
    if (g_transformEntity && g_transformCharacter) {
        return true;
    }

    const uintptr_t base = GetEFZBase();
    if (!base) {
        return false;
    }

    const uintptr_t entityAddr = base + kRvaTransformEntityHitbox;
    const uintptr_t characterAddr = base + kRvaTransformCharacterHitbox;
    // Retail 1.02e prologues, shared up to the first argument read:
    //   push ebp; mov ebp,esp; sub esp,0x3C; mov [ebp-0x1C],ecx; mov eax,[ebp+0xC]
    static const uint8_t kExpected[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C, 0x89, 0x4D, 0xE4, 0x8B, 0x45, 0x0C};
    uint8_t entityBytes[sizeof(kExpected)] = {};
    uint8_t characterBytes[sizeof(kExpected)] = {};
    if (!SafeReadMemory(entityAddr, entityBytes, sizeof(entityBytes)) ||
        !SafeReadMemory(characterAddr, characterBytes, sizeof(characterBytes)) ||
        std::memcmp(entityBytes, kExpected, sizeof(kExpected)) != 0 ||
        std::memcmp(characterBytes, kExpected, sizeof(kExpected)) != 0) {
        static std::atomic<bool> s_logged{false};
        if (!s_logged.exchange(true)) {
            LogOut("[COLLISION_DISPLAY] Refusing native hitbox transforms: signature mismatch", true);
        }
        return false;
    }

    g_transformEntity = reinterpret_cast<TransformEntityHitboxFn>(entityAddr);
    g_transformCharacter = reinterpret_cast<TransformCharacterHitboxFn>(characterAddr);
    return true;
}

uint8_t ReadCurrentScreenIndex() {
    const uintptr_t base = GetEFZBase();
    if (!base) {
        return 0xFFu;
    }
    uint8_t screen = 0xFFu;
    ReadValue(base + EFZ_BASE_OFFSET_SCREEN_STATE, screen);
    return screen;
}

uintptr_t ReadActiveGameplayScreen() {
    const uintptr_t base = GetEFZBase();
    if (!base || ReadCurrentScreenIndex() != kGameplayScreenIndex) {
        return 0;
    }

    uintptr_t context = 0;
    const uintptr_t slot = base + kRvaGameModeArray + sizeof(uintptr_t) * static_cast<uintptr_t>(kGameplayScreenIndex);
    if (!ReadValue(slot, context)) {
        return 0;
    }
    return context;
}

bool IsPracticeGameplayActive() {
    if (GetCurrentGameMode() != GameMode::Practice) {
        return false;
    }
    if (ReadCurrentScreenIndex() != kGameplayScreenIndex) {
        return false;
    }
    if (IsNetplaySuspendActive() || IsNetplaySessionActive()) {
        return false;
    }
    return true;
}

bool ReadCameraOffsets(uintptr_t gameplayScreen, int* outCamX, int* outCamY) {
    if (!gameplayScreen || !outCamX || !outCamY) {
        return false;
    }
    return ReadValue(gameplayScreen + kOffsetCameraX, *outCamX)
        && ReadValue(gameplayScreen + kOffsetCameraY, *outCamY);
}

uintptr_t GetPlayerObject(int playerIndex) {
    uintptr_t player = GetPlayerBase(playerIndex);
    if (player) {
        return player;
    }

    const uintptr_t base = GetEFZBase();
    if (!base) {
        return 0;
    }
    const uintptr_t ptrOffset = (playerIndex == 1) ? EFZ_BASE_OFFSET_P1 : EFZ_BASE_OFFSET_P2;
    ReadValue(base + ptrOffset, player);
    return player;
}

bool RectHasArea(const int* rect) {
    return rect && rect[2] != rect[0] && rect[3] != rect[1];
}

void MapEntityHitToScreen(const int* gameRect, int camX, int camY, RectI* out) {
    if (!RectHasArea(gameRect) || !out) {
        return;
    }

    const int width = gameRect[2] - gameRect[0];
    const int height = gameRect[3] - gameRect[1];
    out->left = 2 * (camX + gameRect[0]);
    out->right = out->left + 2 * width;
    const int top = 2 * (camY + gameRect[1]) + 420;
    out->top = top;
    out->bottom = top + 2 * height;
}

void MapCharacterHitToScreen(
    const int* gameRect,
    const int* localRect,
    const uint8_t* snapshot,
    int camX,
    int camY,
    RectI* out) {
    if (!RectHasArea(gameRect) || !RectHasArea(localRect) || !snapshot || !out) {
        return;
    }

    const int width = gameRect[2] - gameRect[0];
    const int localHeight = localRect[3] - localRect[1];
    out->left = 2 * (camX + gameRect[0]);
    out->right = out->left + 2 * width;

    const double posY = *reinterpret_cast<const double*>(snapshot + 224u);
    const int16_t adjust = *reinterpret_cast<const int16_t*>(snapshot + 206u);
    double topValue = static_cast<double>(localRect[1] + camY) + posY + 210.0;
    if (posY < 0.0) {
        topValue -= 1.0;
    }
    out->top = static_cast<int>((topValue - static_cast<double>(adjust)) * 2.0);
    out->bottom = out->top + 2 * localHeight;
}

void QueueRect(const RectI& rect, uint32_t color, std::vector<DrawRect>& out, bool inactive = false) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }
    out.push_back(DrawRect{rect, color, inactive});
}

void QueueVerticalLine(RectI rect, uint32_t color, std::vector<DrawRect>& out) {
    const int centerX = (rect.left + rect.right) / 2;
    rect.left = centerX;
    rect.right = centerX + 1;
    QueueRect(rect, color, out);
}

bool RectHasArea(const RectI& rect) {
    return rect.right > rect.left && rect.bottom > rect.top;
}

bool IntersectRects(const RectI& a, const RectI& b, RectI* out) {
    if (!out) {
        return false;
    }
    out->left = (a.left > b.left) ? a.left : b.left;
    out->top = (a.top > b.top) ? a.top : b.top;
    out->right = (a.right < b.right) ? a.right : b.right;
    out->bottom = (a.bottom < b.bottom) ? a.bottom : b.bottom;
    return RectHasArea(*out);
}

void MapWorldPointToScreen(double x, double y, int camX, int camY, float* outX, float* outY) {
    if (!outX || !outY) {
        return;
    }
    *outX = static_cast<float>(2.0 * (static_cast<double>(camX) + x));
    *outY = static_cast<float>(2.0 * (static_cast<double>(camY) + y) + 420.0);
}

RectI MapWorldRangeToScreen(double centerX, double centerY, int rangeX, int rangeY, int camX, int camY) {
    RectI out{};
    out.left = static_cast<int>(2.0 * (static_cast<double>(camX) + centerX - static_cast<double>(rangeX)));
    out.right = static_cast<int>(2.0 * (static_cast<double>(camX) + centerX + static_cast<double>(rangeX)));
    out.top = static_cast<int>(2.0 * (static_cast<double>(camY) + centerY - static_cast<double>(rangeY)) + 420.0);
    out.bottom = static_cast<int>(2.0 * (static_cast<double>(camY) + centerY + static_cast<double>(rangeY)) + 420.0);
    return out;
}

void AppendOverlayRect(const RectI& rect, uint32_t outlineColor, uint8_t fillAlpha, bool dottedOutline = false) {
    if (!RectHasArea(rect)) {
        return;
    }

    OverlayBox box{};
    box.x = static_cast<float>(rect.left);
    box.y = static_cast<float>(rect.top);
    box.w = static_cast<float>(rect.right - rect.left);
    box.h = static_cast<float>(rect.bottom - rect.top);
    box.fillArgb = ColorWithAlphaByte(outlineColor, fillAlpha);
    box.outlineArgb = outlineColor;
    box.shape = ShapeRectangle;
    box.dottedOutline = dottedOutline;
    g_overlayBoxes.push_back(box);
}

void AppendOverlayDot(float x, float y, float radius, uint32_t fillColor, uint32_t outlineColor = 0xFF000000u) {
    OverlayBox box{};
    box.x = x;
    box.y = y;
    box.w = radius;
    box.h = 0.0f;
    box.fillArgb = fillColor;
    box.outlineArgb = outlineColor;
    box.shape = ShapeDot;
    g_overlayBoxes.push_back(box);
}

void AppendProjectilePhysicalBoxes(const ProjectileInfo& projectile,
                                   bool priorityCollisionEligible,
                                   bool showHitRects,
                                   bool showCollisionRect,
                                   float hitFillMultiplier = 0.30f,
                                   float activeCollisionFillMultiplier = 0.38f,
                                   float inactiveCollisionFillMultiplier = 0.16f) {
    if (showHitRects) {
        for (const RectI& hitRect : projectile.hitRects) {
            AppendOverlayRect(hitRect, kColorHitOutline,
                              FillAlphaByte(projectile.hitActive
                                                ? hitFillMultiplier
                                                : hitFillMultiplier * kInactiveHitFillScale),
                              !projectile.hitActive);
        }
    }

    if (!showCollisionRect || !projectile.hasCollisionRect) {
        return;
    }

    const uint32_t color = priorityCollisionEligible
        ? kColorProjectileOutline
        : kColorProjectileInactiveOutline;
    AppendOverlayRect(projectile.screenRect,
                      color,
                      priorityCollisionEligible
                          ? FillAlphaByte(activeCollisionFillMultiplier)
                          : FillAlphaByte(inactiveCollisionFillMultiplier));
}

bool IsNagamoriOwner(uintptr_t owner) {
    char name[16] = {};
    if (!ReadBytes(owner + CHARACTER_NAME_OFFSET, name, sizeof(name) - 1)) {
        return false;
    }
    return std::strncmp(name, "nagamori", 8) == 0;
}

int16_t ReadProjectilePriority(uintptr_t owner, uint16_t pattern) {
    uintptr_t animTable = 0;
    if (!ReadValue(owner + ANIM_TABLE_OFFSET, animTable) || !animTable) {
        return 0;
    }

    int16_t priority = 0;
    ReadValue(animTable + ANIM_ENTRY_STRIDE * static_cast<uintptr_t>(pattern), priority);
    return priority;
}

bool CopyCharacterSnapshot(uintptr_t entity, uint8_t* outSnapshot) {
    if (!entity || !outSnapshot) {
        return false;
    }

    uint16_t animFrame = 0;
    uint16_t subframe = 0;
    uintptr_t animTable = 0;
    uintptr_t metaTable = 0;
    if (!ReadValue(entity + kOffsetAnimFrame, animFrame)
        || !ReadValue(entity + kOffsetSubframe, subframe)
        || !ReadValue(entity + ANIM_TABLE_OFFSET, animTable)
        || !ReadValue(entity + kOffsetAnimMetaTable, metaTable)
        || !animTable
        || !metaTable) {
        return false;
    }

    uintptr_t frameTableEntry = 0;
    if (!ReadValue(animTable + ANIM_ENTRY_STRIDE * static_cast<uintptr_t>(animFrame) + ANIM_ENTRY_FRAMES_PTR_OFFSET,
                   frameTableEntry)
        || !frameTableEntry) {
        return false;
    }

    const uintptr_t frameBlob = frameTableEntry + FRAME_BLOCK_STRIDE * static_cast<uintptr_t>(subframe);
    if (!ReadBytes(frameBlob, outSnapshot, kFrameBlobBytes)) {
        return false;
    }

    uintptr_t metaFrameTable = 0;
    if (!ReadValue(metaTable + ANIM_ENTRY_STRIDE * static_cast<uintptr_t>(animFrame) + ANIM_ENTRY_FRAMES_PTR_OFFSET,
                   metaFrameTable)
        || !metaFrameTable) {
        return false;
    }

    const uintptr_t metaRow = metaFrameTable + sizeof(uint32_t) * static_cast<uintptr_t>(3u * subframe);
    if (!ReadBytes(metaRow + sizeof(uint32_t), outSnapshot + 204u, 8u)
        || !ReadBytes(entity + XPOS_OFFSET, outSnapshot + 216u, 8u)
        || !ReadBytes(entity + YPOS_OFFSET, outSnapshot + 224u, 8u)
        || !ReadBytes(entity + FACING_DIRECTION_OFFSET, outSnapshot + 232u, 1u)) {
        return false;
    }

    return true;
}

bool CopyProjectileFrameBlob(uintptr_t owner, uint16_t animIndex, uint16_t subframe, uint8_t* outBlob) {
    if (!owner || !outBlob) {
        return false;
    }

    uintptr_t animTable = 0;
    if (!ReadValue(owner + ANIM_TABLE_OFFSET, animTable) || !animTable) {
        return false;
    }

    uintptr_t frameTableEntry = 0;
    if (!ReadValue(animTable + ANIM_ENTRY_STRIDE * static_cast<uintptr_t>(animIndex) + ANIM_ENTRY_FRAMES_PTR_OFFSET,
                   frameTableEntry)
        || !frameTableEntry) {
        return false;
    }

    return ReadBytes(frameTableEntry + FRAME_BLOCK_STRIDE * static_cast<uintptr_t>(subframe),
                     outBlob,
                     kFrameBlobBytes);
}

bool ReadProjectileInfo(int playerIndex,
                        uintptr_t owner,
                        int slotIndex,
                        int camX,
                        int camY,
                        ProjectileInfo& out) {
    if (!owner || !g_transformEntity || slotIndex < 0 || slotIndex >= kProjectileSlotCount) {
        return false;
    }

    uint32_t alive = 0;
    if (!ReadValue(owner + kProjectileAliveFlagsOffset + sizeof(uint32_t) * static_cast<uintptr_t>(slotIndex), alive)
        || alive == 0) {
        return false;
    }

    const uintptr_t entryBase = owner + kProjectileEntryBaseOffset
        + kProjectileEntryStride * static_cast<uintptr_t>(slotIndex);

    uint16_t pattern = 0;
    uint16_t frame = 0;
    uint16_t frameTick = 0;
    uint32_t destroyed = 0;
    int16_t life = 0;
    double x = 0.0;
    double y = 0.0;
    if (!ReadValue(entryBase + kProjectilePatternOffset, pattern)
        || !ReadValue(entryBase + kProjectileFrameOffset, frame)
        || !ReadValue(entryBase + kProjectileFrameTickOffset, frameTick)
        || !ReadValue(entryBase + kProjectileDestroyedOffset, destroyed)
        || !ReadValue(entryBase + kProjectileLifeOffset, life)
        || !ReadValue(entryBase + kProjectileXOffset, x)
        || !ReadValue(entryBase + kProjectileYOffset, y)) {
        return false;
    }

    out = ProjectileInfo{};
    out.playerIndex = playerIndex;
    out.owner = owner;
    out.entryBase = entryBase;
    out.slot = slotIndex;
    out.pattern = pattern;
    out.frame = frame;
    out.frameTick = frameTick;
    out.destroyed = destroyed;
    out.life = life;
    out.priority = ReadProjectilePriority(owner, pattern);
    out.x = x;
    out.y = y;
    out.nagamoriOwner = IsNagamoriOwner(owner);
    if (out.nagamoriOwner) {
        uint8_t flag = 0;
        out.nagamoriFlagged =
            ReadValue(owner + kNagamoriActivationFlagBaseOffset + static_cast<uintptr_t>(slotIndex), flag)
            && flag != 0;
    }
    MapWorldPointToScreen(x, y, camX, camY, &out.screenX, &out.screenY);

    uint8_t frameBlob[kFrameBlobBytes] = {};
    if (!CopyProjectileFrameBlob(owner, pattern, frame, frameBlob)) {
        return true;
    }

    auto* entityHeader = reinterpret_cast<unsigned short*>(entryBase);

    out.hitActive = CollisionBoxPolicy::ProjectileHitBoxActive(
        frameBlob[kBlobSuppressHurtFlag], life, destroyed);
    out.hitRects.reserve(kHitRectCount);
    int gameRect[4] = {};
    RectI screenRect{};
    const int* hitRectBase = reinterpret_cast<const int*>(frameBlob + kHitRectBase);
    for (int i = 0; i < kHitRectCount; ++i) {
        const int* localRect = hitRectBase + (4 * i);
        if (!RectHasArea(localRect)) {
            continue;
        }

        int* transformed = g_transformEntity(gameRect, entityHeader, const_cast<int*>(localRect));
        if (!RectHasArea(transformed)) {
            continue;
        }

        MapEntityHitToScreen(transformed, camX, camY, &screenRect);
        if (RectHasArea(screenRect)) {
            out.hitRects.push_back(screenRect);
        }
    }

    const int* localRect = reinterpret_cast<const int*>(frameBlob + kCollisionRectBase);
    if (RectHasArea(localRect)) {
        int* transformed = g_transformEntity(gameRect, entityHeader, const_cast<int*>(localRect));
        if (RectHasArea(transformed)) {
            out.gameRect.left = transformed[0];
            out.gameRect.top = transformed[1];
            out.gameRect.right = transformed[2];
            out.gameRect.bottom = transformed[3];
            MapEntityHitToScreen(transformed, camX, camY, &out.screenRect);
            out.hasCollisionRect = RectHasArea(out.screenRect);
        }
    }
    return true;
}

void CollectPlayerProjectiles(int playerIndex,
                              uintptr_t entity,
                              int camX,
                              int camY,
                              std::vector<ProjectileInfo>& out) {
    if (!entity) {
        return;
    }

    int16_t head = 0;
    int16_t tail = 0;
    if (!ReadValue(entity + kProjectileHeadOffset, head)
        || !ReadValue(entity + kProjectileTailOffset, tail)
        || head < 0
        || tail < 0
        || head >= kProjectileSlotCount
        || tail >= kProjectileSlotCount) {
        return;
    }

    int slot = head;
    int safety = kProjectileSlotCount;
    while (slot != tail && safety-- > 0) {
        ProjectileInfo info{};
        if (ReadProjectileInfo(playerIndex, entity, slot, camX, camY, info)) {
            out.push_back(info);
        }
        slot = (slot + 1) & (kProjectileSlotCount - 1);
    }
}

void CollectProjectiles(int camX, int camY, std::vector<ProjectileInfo>& out) {
    CollectPlayerProjectiles(1, GetPlayerObject(1), camX, camY, out);
    CollectPlayerProjectiles(2, GetPlayerObject(2), camX, camY, out);
}

bool IsDiscreteNagamoriTriggerFrame(const ProjectileInfo& projectile, uint16_t triggerFrame) {
    // Nagamori's update calls markEntitiesInRange() before incrementing entry+0x04; the overlay samples after update, so accept tick 1.
    return projectile.frame == triggerFrame && projectile.frameTick <= 1;
}

bool IsNagamoriExplodableNote(const ProjectileInfo& projectile) {
    if (!projectile.nagamoriOwner) {
        return false;
    }

    switch (projectile.pattern) {
    case 0x190:
    case 0x192:
    case 0x193:
    case 0x196:
    case 0x197:
    case 0x198:
    case 0x199:
    case 0x19F:
    case 0x1C1:
        return true;
    default:
        return false;
    }
}

bool IsNagamoriBowActivatorPattern(uint16_t pattern) {
    return pattern == 0x19B || pattern == 0x19C;
}

bool IsNagamori2CActivationMove(uint16_t moveId) {
    // Vanilla efz.exe Mizuka jump table entries that enter block 0x004A5344.
    switch (moveId) {
    case 206:
    case 343:
    case 355:
    case 391:
        return true;
    default:
        return false;
    }
}

bool IsNagamoriDp252ActivationMove(uint16_t moveId) {
    // Vanilla efz.exe Mizuka jump table entries that enter block 0x004A7BDB.
    return moveId == 252 || moveId == 403;
}

bool IsNagamoriSuperWideActivationMove(uint16_t moveId) {
    // Vanilla efz.exe Mizuka jump table entries that enter block 0x004AD354,
    // plus the adjacent 314/315 blocks. These call markEntitiesInRange(x,y,1000,1000)
    // on action frame 6 tick 0.
    switch (moveId) {
    case 313:
    case 314:
    case 315:
    case 328:
    case 340:
    case 344:
    case 348:
    case 352:
    case 356:
    case 360:
    case 368:
    case 382:
    case 400:
    case 409:
    case 422:
    case 435:
    case 448:
    case 461:
    case 474:
    case 487:
    case 500:
    case 513:
        return true;
    default:
        return false;
    }
}

bool IsFreshNagamoriCharacterTriggerTick(uint16_t frameTick) {
    // Handlers call markEntitiesInRange() before incrementing this+0x0C; tick 1 = just fired, tick 0 = pre-update/paused.
    return frameTick <= 1;
}

float NagamoriRangeFillMultiplier(const NagamoriActivationRange& range);

void AddNagamoriCharacterActivationRange(std::vector<NagamoriActivationRange>& out,
                                         uintptr_t owner,
                                         uint16_t moveId,
                                         double centerX,
                                         double centerY,
                                         int rangeX,
                                         int rangeY,
                                         int camX,
                                         int camY,
                                         bool showNagamoriRanges,
                                         bool showProjectileOrigins) {
    NagamoriActivationRange range{};
    range.owner = owner;
    range.sourceSlot = -1;
    range.sourcePattern = 0;
    range.sourceMove = moveId;
    range.centerX = centerX;
    range.centerY = centerY;
    range.rangeX = rangeX;
    range.rangeY = rangeY;
    range.triggerFrame = true;
    range.previewWhenInactive = false;
    range.sourceIsCharacter = true;
    range.screenRect = MapWorldRangeToScreen(centerX, centerY, rangeX, rangeY, camX, camY);
    out.push_back(range);

    if (!showNagamoriRanges) {
        return;
    }

    AppendOverlayRect(range.screenRect,
                      kColorNagamoriRange,
                      FillAlphaByte(NagamoriRangeFillMultiplier(range)));
    if (showProjectileOrigins) {
        float sourceScreenX = 0.0f;
        float sourceScreenY = 0.0f;
        MapWorldPointToScreen(centerX, centerY, camX, camY, &sourceScreenX, &sourceScreenY);
        AppendOverlayDot(sourceScreenX, sourceScreenY, 4.0f, kColorNagamoriRange);
    }
}

void CollectNagamoriCharacterActivationRanges(uintptr_t owner,
                                              int camX,
                                              int camY,
                                              bool showNagamoriRanges,
                                              bool showProjectileOrigins,
                                              std::vector<NagamoriActivationRange>& out) {
    if (!owner || !IsNagamoriOwner(owner)) {
        return;
    }

    uint16_t moveId = 0;
    uint16_t actionFrame = 0;
    uint16_t frameTick = 0;
    double x = 0.0;
    double y = 0.0;
    int8_t facingRaw = 1;
    if (!ReadValue(owner + MOVE_ID_OFFSET, moveId)
        || !ReadValue(owner + CURRENT_FRAME_INDEX_OFFSET, actionFrame)
        || !ReadValue(owner + kOffsetActionFrameTick, frameTick)
        || !ReadValue(owner + XPOS_OFFSET, x)
        || !ReadValue(owner + YPOS_OFFSET, y)
        || !ReadValue(owner + FACING_DIRECTION_OFFSET, facingRaw)) {
        return;
    }

    const int facing = facingRaw < 0 ? -1 : 1;
    const bool freshTick = IsFreshNagamoriCharacterTriggerTick(frameTick);

    if (IsNagamori2CActivationMove(moveId)) {
        // 0x004A547A: frame 1 tick 0, markEntitiesInRange(x, -10, 90, 50).
        if (actionFrame == 1 && freshTick) {
            AddNagamoriCharacterActivationRange(out, owner, moveId, x, -10.0, 90, 50,
                                                camX, camY, showNagamoriRanges, showProjectileOrigins);
        }
        return;
    }

    if (moveId == 250) {
        // 0x004A78A8: frame 2 tick 0, markEntitiesInRange(x + dir*51, y - 63, 36, 80).
        if (actionFrame == 2 && freshTick) {
            AddNagamoriCharacterActivationRange(out, owner, moveId,
                                                x + static_cast<double>(facing * 51),
                                                y - 63.0,
                                                36, 80,
                                                camX, camY, showNagamoriRanges, showProjectileOrigins);
        }
        return;
    }

    if (moveId == 251) {
        // 0x004A7A67 and 0x004A7AEC: frames 2 and 5 tick 0, same range as DP A.
        if ((actionFrame == 2 || actionFrame == 5) && freshTick) {
            AddNagamoriCharacterActivationRange(out, owner, moveId,
                                                x + static_cast<double>(facing * 51),
                                                y - 63.0,
                                                36, 80,
                                                camX, camY, showNagamoriRanges, showProjectileOrigins);
        }
        return;
    }

    if (IsNagamoriDp252ActivationMove(moveId)) {
        // 0x004A7D57 and 0x004A7DDC: frames 2 and 5 tick 0, same range as DP A.
        if ((actionFrame == 2 || actionFrame == 5) && freshTick) {
            AddNagamoriCharacterActivationRange(out, owner, moveId,
                                                x + static_cast<double>(facing * 51),
                                                y - 63.0,
                                                36, 80,
                                                camX, camY, showNagamoriRanges, showProjectileOrigins);
        }
        return;
    }

    if (moveId == 270 || moveId == 271 || moveId == 272) {
        // 0x004A956D / 0x004A97EA / 0x004A9B90:
        // frames 4-5 while airborne (current y < 0), markEntitiesInRange(x - dir*34, y - 66, 39, 42).
        if ((actionFrame == 4 || actionFrame == 5) && y < 0.0) {
            AddNagamoriCharacterActivationRange(out, owner, moveId,
                                                x - static_cast<double>(facing * 34),
                                                y - 66.0,
                                                39, 42,
                                                camX, camY, showNagamoriRanges, showProjectileOrigins);
        }

        // 0x004A95E9 exists only in move block 270 after landing:
        // markEntitiesInRange(x + dir*39, y - 35, 70, 75).
        if (moveId == 270 && actionFrame == 6 && freshTick) {
            AddNagamoriCharacterActivationRange(out, owner, moveId,
                                                x + static_cast<double>(facing * 39),
                                                y - 35.0,
                                                70, 75,
                                                camX, camY, showNagamoriRanges, showProjectileOrigins);
        }
        return;
    }

    if (IsNagamoriSuperWideActivationMove(moveId)) {
        // 0x004AD50B / 0x004AD74A / 0x004AD988:
        // frame 6 tick 0, markEntitiesInRange(x, y, 1000, 1000).
        if (actionFrame == 6 && freshTick) {
            AddNagamoriCharacterActivationRange(out, owner, moveId, x, y, 1000, 1000,
                                                camX, camY, showNagamoriRanges, showProjectileOrigins);
        }
    }
}

float NagamoriRangeFillMultiplier(const NagamoriActivationRange& range) {
    if (IsNagamoriBowActivatorPattern(range.sourcePattern)) {
        // The bow's trigger is continuous and overlays the projectile body; keep it visible, but let the hit/collision pass read above it.
        return range.triggerFrame ? 0.20f : 0.12f;
    }
    return range.triggerFrame ? 0.53f : 0.28f;
}

bool NagamoriActivationForProjectile(const ProjectileInfo& projectile, NagamoriActivationRange& outRange) {
    if (!projectile.nagamoriOwner) {
        return false;
    }

    int range = 0;
    bool triggerFrame = false;
    bool previewWhenInactive = true;
    bool sourceIsCharacter = false;
    double centerX = projectile.x;
    double centerY = projectile.y;
    switch (projectile.pattern) {
        case 0x195: // Nagamori range marker: markEntitiesInRange(projectile x/y, 90, 90) on frame 14 tick 0
            range = 90;
            triggerFrame = IsDiscreteNagamoriTriggerFrame(projectile, 14);
            break;
        case 0x1C6: // Larger range marker: markEntitiesInRange(projectile x/y, 120, 120) on frame 13 tick 0
            range = 120;
            triggerFrame = IsDiscreteNagamoriTriggerFrame(projectile, 13);
            break;
        case 0x19B:
        case 0x19C: // Continuous note activators: range 30 every update
            range = 30;
            triggerFrame = true;
            break;
        case 0x1BF: { // Character-centered note activator: markEntitiesInRange(owner x/y, 320, 320) on frame 25
            double ownerX = 0.0;
            double ownerY = 0.0;
            if (!ReadValue(projectile.owner + XPOS_OFFSET, ownerX)
                || !ReadValue(projectile.owner + YPOS_OFFSET, ownerY)) {
                return false;
            }
            centerX = ownerX;
            centerY = ownerY;
            range = 320;
            sourceIsCharacter = true;
            triggerFrame = IsDiscreteNagamoriTriggerFrame(projectile, 25);
            previewWhenInactive = false;
            break;
        }
        default:
            return false;
    }

    outRange = NagamoriActivationRange{};
    outRange.owner = projectile.owner;
    outRange.sourceSlot = projectile.slot;
    outRange.sourcePattern = projectile.pattern;
    outRange.centerX = centerX;
    outRange.centerY = centerY;
    outRange.rangeX = range;
    outRange.rangeY = range;
    outRange.triggerFrame = triggerFrame;
    outRange.previewWhenInactive = previewWhenInactive;
    outRange.sourceIsCharacter = sourceIsCharacter;
    return true;
}

bool PointInActivationRange(const ProjectileInfo& projectile, const NagamoriActivationRange& range) {
    return projectile.owner == range.owner
        && projectile.slot != range.sourceSlot
        && IsNagamoriExplodableNote(projectile)
        && std::fabs(projectile.x - range.centerX) < static_cast<double>(range.rangeX)
        && std::fabs(projectile.y - range.centerY) < static_cast<double>(range.rangeY);
}

bool ProjectilePriorityCollisionEligible(const ProjectileInfo& projectile) {
    // EFZ's projectile-vs-projectile priority collision path requires
    // entry+0x80 == 0, entry+0x84 > 0, and a positive animation-table priority.
    return projectile.destroyed == 0
        && projectile.life > 0
        && projectile.priority > 0;
}

void AppendCharacterLayer(int boxType,
                          uintptr_t entity,
                          const uint8_t* snapshot,
                          int camX,
                          int camY,
                          std::vector<DrawRect>& out) {
    if (!entity || !snapshot || !g_transformCharacter) {
        return;
    }

    const int* rectBase = nullptr;
    int rectCount = 0;
    uint32_t color = 0;
    bool inactive = false;

    if (boxType == 0) {
        rectBase = reinterpret_cast<const int*>(snapshot + kHitRectBase);
        rectCount = kHitRectCount;
        color = kColorHitOutline;
        uint8_t attackState = 0;
        ReadValue(entity + kOffsetAttackState, attackState);
        inactive = !CollisionBoxPolicy::CharacterHitBoxActive(snapshot[kBlobAttackFlags], attackState);
    } else if (boxType == 1) {
        int16_t hitImmunity = 0;
        ReadValue(entity + kOffsetHitImmunity, hitImmunity);
        if (!CollisionBoxPolicy::CharacterHurtBoxesShown(snapshot[kBlobSuppressHurtFlag], hitImmunity)) {
            return;
        }
        rectBase = reinterpret_cast<const int*>(snapshot + kHurtRectBase);
        rectCount = kHurtRectCount;
        color = kColorHurtOutline;
    } else {
        rectBase = reinterpret_cast<const int*>(snapshot + kCollisionRectBase);
        rectCount = kCollisionRectCount;
        color = kColorCollisionOutline;
    }

    int gameRect[4] = {};
    RectI screenRect = {};
    for (int i = 0; i < rectCount; ++i) {
        const int* localRect = rectBase + (4 * i);
        if (!RectHasArea(localRect)) {
            continue;
        }

        int* transformed = g_transformCharacter(
            gameRect,
            static_cast<int>(reinterpret_cast<uintptr_t>(snapshot)),
            const_cast<int*>(localRect));
        if (!RectHasArea(transformed)) {
            continue;
        }

        MapCharacterHitToScreen(transformed, localRect, snapshot, camX, camY, &screenRect);
        QueueRect(screenRect, color, out, inactive);
        if (boxType == 2) {
            QueueVerticalLine(screenRect, color, out);
        }
    }
}

void AppendProjectileLayer(int boxType,
                           uintptr_t owner,
                           int slotIndex,
                           int camX,
                           int camY,
                           std::vector<DrawRect>& out) {
    if (!owner || !g_transformEntity || slotIndex < 0 || slotIndex >= kProjectileSlotCount) {
        return;
    }

    uint32_t alive = 0;
    if (!ReadValue(owner + kProjectileAliveFlagsOffset + sizeof(uint32_t) * static_cast<uintptr_t>(slotIndex), alive)
        || alive == 0) {
        return;
    }

    const uintptr_t entryBase = owner + kProjectileEntryBaseOffset
        + kProjectileEntryStride * static_cast<uintptr_t>(slotIndex);

    uint16_t animIndex = 0;
    uint16_t subframe = 0;
    if (!ReadValue(entryBase + kProjectilePatternOffset, animIndex)
        || !ReadValue(entryBase + kProjectileFrameOffset, subframe)) {
        return;
    }

    uint8_t frameBlob[kFrameBlobBytes] = {};
    if (!CopyProjectileFrameBlob(owner, animIndex, subframe, frameBlob)) {
        return;
    }

    if ((frameBlob[kBlobSuppressHurtFlag] & 0x20) != 0 && boxType != 0) {
        return;
    }

    const int* rectBase = nullptr;
    int rectCount = 0;
    uint32_t color = 0;
    bool inactive = false;
    if (boxType == 0) {
        rectBase = reinterpret_cast<const int*>(frameBlob + kHitRectBase);
        rectCount = kHitRectCount;
        color = kColorHitOutline;
        int16_t life = 0;
        uint32_t destroyed = 0;
        ReadValue(entryBase + kProjectileLifeOffset, life);
        ReadValue(entryBase + kProjectileDestroyedOffset, destroyed);
        inactive = !CollisionBoxPolicy::ProjectileHitBoxActive(
            frameBlob[kBlobSuppressHurtFlag], life, destroyed);
    } else if (boxType == 1) {
        rectBase = reinterpret_cast<const int*>(frameBlob + kHurtRectBase);
        rectCount = kHurtRectCount;
        color = kColorHurtOutline;
    } else {
        return;
    }

    auto* entityHeader = reinterpret_cast<unsigned short*>(entryBase);
    int gameRect[4] = {};
    RectI screenRect = {};
    for (int i = 0; i < rectCount; ++i) {
        const int* localRect = rectBase + (4 * i);
        if (!RectHasArea(localRect)) {
            continue;
        }

        int* transformed = g_transformEntity(gameRect, entityHeader, const_cast<int*>(localRect));
        if (!RectHasArea(transformed)) {
            continue;
        }

        MapEntityHitToScreen(transformed, camX, camY, &screenRect);
        QueueRect(screenRect, color, out, inactive);
    }
}

void CollectPlayerBoxes(int playerIndex, int boxType, int camX, int camY, std::vector<DrawRect>& out) {
    const uintptr_t entity = GetPlayerObject(playerIndex);
    if (!entity) {
        return;
    }

    uint8_t snapshot[kCharacterSnapshotBytes] = {};
    if (!CopyCharacterSnapshot(entity, snapshot)) {
        return;
    }

    AppendCharacterLayer(boxType, entity, snapshot, camX, camY, out);

    int16_t head = 0;
    int16_t tail = 0;
    if (!ReadValue(entity + kProjectileHeadOffset, head)
        || !ReadValue(entity + kProjectileTailOffset, tail)
        || head < 0
        || tail < 0
        || head >= kProjectileSlotCount
        || tail >= kProjectileSlotCount) {
        return;
    }

    int slot = head;
    int safety = kProjectileSlotCount;
    while (slot != tail && safety-- > 0) {
        AppendProjectileLayer(boxType, entity, slot, camX, camY, out);
        slot = (slot + 1) & (kProjectileSlotCount - 1);
    }
}

void CollectLayer(int boxType, int camX, int camY, std::vector<DrawRect>& out) {
    // A player's filter owns the fighter and every projectile/entity in that fighter's ring.
    if (PlayerLayerEnabledFromSettings(boxType, 1)) {
        CollectPlayerBoxes(1, boxType, camX, camY, out);
    }
    if (PlayerLayerEnabledFromSettings(boxType, 2)) {
        CollectPlayerBoxes(2, boxType, camX, camY, out);
    }
}

void AppendProjectileInteractions(int camX, int camY) {
    const Config::Settings& settings = Config::GetSettings();
    const bool showProjectileBoxes = settings.collisionDisplayProjectileBoxes;
    const bool showProjectileOrigins = settings.collisionDisplayProjectileOrigins;
    const bool showProjectileIntersections = settings.collisionDisplayProjectileIntersections;
    const bool showNagamoriRanges = settings.collisionDisplayNagamoriRanges;
    const bool showNagamoriAffected = settings.collisionDisplayNagamoriAffected;
    const bool needNagamoriRanges = showNagamoriRanges || showNagamoriAffected;

    std::vector<ProjectileInfo> projectiles;
    projectiles.reserve(32);
    CollectProjectiles(camX, camY, projectiles);

    std::vector<NagamoriActivationRange> activationRanges;
    activationRanges.reserve(projectiles.size());
    if (needNagamoriRanges) {
        CollectNagamoriCharacterActivationRanges(GetPlayerObject(1),
                                                 camX,
                                                 camY,
                                                 showNagamoriRanges,
                                                 showProjectileOrigins,
                                                 activationRanges);
        CollectNagamoriCharacterActivationRanges(GetPlayerObject(2),
                                                 camX,
                                                 camY,
                                                 showNagamoriRanges,
                                                 showProjectileOrigins,
                                                 activationRanges);
    }

    for (const ProjectileInfo& projectile : projectiles) {
        const bool priorityCollisionEligible = ProjectilePriorityCollisionEligible(projectile);
        const bool showOwnerHitRects =
            PlayerLayerFilterEnabledFromSettings(0, projectile.playerIndex);
        const bool showOwnerCollisionRect =
            PlayerLayerFilterEnabledFromSettings(2, projectile.playerIndex);
        if (showProjectileBoxes &&
            (showOwnerHitRects || showOwnerCollisionRect)) {
            AppendProjectilePhysicalBoxes(projectile,
                                          priorityCollisionEligible,
                                          showOwnerHitRects,
                                          showOwnerCollisionRect);
        }

        if (showProjectileOrigins) {
            AppendOverlayDot(projectile.screenX, projectile.screenY, projectile.nagamoriFlagged ? 3.5f : 2.5f,
                             projectile.nagamoriFlagged ? kColorNagamoriAffected : kColorProjectileDot);
        }

        if (showNagamoriAffected && projectile.nagamoriFlagged && projectile.hasCollisionRect) {
            AppendOverlayRect(projectile.screenRect, kColorNagamoriAffected, FillAlphaByte(0.56f));
        }

        NagamoriActivationRange range{};
        if (needNagamoriRanges && NagamoriActivationForProjectile(projectile, range)) {
            range.screenRect = MapWorldRangeToScreen(range.centerX, range.centerY,
                                                     range.rangeX, range.rangeY,
                                                     camX, camY);
            activationRanges.push_back(range);
            if (showNagamoriRanges && (range.triggerFrame || range.previewWhenInactive)) {
                AppendOverlayRect(range.screenRect,
                                  kColorNagamoriRange,
                                  FillAlphaByte(NagamoriRangeFillMultiplier(range)));
                if (showProjectileOrigins) {
                    float sourceScreenX = projectile.screenX;
                    float sourceScreenY = projectile.screenY;
                    MapWorldPointToScreen(range.centerX, range.centerY, camX, camY,
                                          &sourceScreenX, &sourceScreenY);
                    AppendOverlayDot(sourceScreenX, sourceScreenY,
                                     range.triggerFrame ? 4.0f : 3.0f,
                                     kColorNagamoriRange);
                }
            }

            if (showProjectileBoxes && IsNagamoriBowActivatorPattern(projectile.pattern)) {
                // Re-append the physical boxes after the note-trigger rectangles so a real hitbox is not mistaken for a trigger helper.
                AppendProjectilePhysicalBoxes(projectile,
                                              priorityCollisionEligible,
                                              showOwnerHitRects,
                                              showOwnerCollisionRect,
                                              0.54f,
                                              0.58f,
                                              0.24f);
            }
        }
    }

    if (showProjectileIntersections) {
        for (std::size_t i = 0; i < projectiles.size(); ++i) {
            const ProjectileInfo& a = projectiles[i];
            if (!a.hasCollisionRect ||
                !PlayerLayerFilterEnabledFromSettings(2, a.playerIndex)) {
                continue;
            }
            for (std::size_t j = i + 1; j < projectiles.size(); ++j) {
                const ProjectileInfo& b = projectiles[j];
                if (!b.hasCollisionRect ||
                    !PlayerLayerFilterEnabledFromSettings(2, b.playerIndex) ||
                    (a.owner == b.owner && a.slot == b.slot)) {
                    continue;
                }

                const bool priorityPair =
                    ProjectilePriorityCollisionEligible(a) && ProjectilePriorityCollisionEligible(b);
                const bool nagamoriSetplayPair =
                    a.nagamoriOwner && b.nagamoriOwner && a.owner == b.owner;
                if (!priorityPair && !nagamoriSetplayPair) {
                    continue;
                }

                RectI intersection{};
                if (IntersectRects(a.screenRect, b.screenRect, &intersection)) {
                    AppendOverlayRect(intersection, kColorProjectileIntersect, FillAlphaByte(1.25f));
                }
            }
        }
    }

    if (showNagamoriAffected) {
        for (const NagamoriActivationRange& range : activationRanges) {
            for (const ProjectileInfo& projectile : projectiles) {
                if (!PointInActivationRange(projectile, range)) {
                    continue;
                }

                if (showNagamoriRanges && (range.triggerFrame || range.previewWhenInactive)) {
                    const RectI noteReachRect = MapWorldRangeToScreen(projectile.x, projectile.y,
                                                                      range.rangeX, range.rangeY,
                                                                      camX, camY);
                    AppendOverlayRect(noteReachRect, kColorNagamoriRange, FillAlphaByte(0.18f));
                }
                if (showProjectileOrigins) {
                    AppendOverlayDot(projectile.screenX, projectile.screenY, 4.5f, kColorNagamoriAffected);
                }
                if (projectile.hasCollisionRect) {
                    AppendOverlayRect(projectile.screenRect, kColorNagamoriAffected, FillAlphaByte(0.69f));
                }
            }
        }
    }
}

void RebuildOverlayBoxesLocked() {
    g_overlayBoxes.clear();
    g_overlayBoxes.reserve(g_frameRects.size());

    const uint8_t fillAlpha = FillAlphaByte();
    const uint8_t inactiveFillAlpha = FillAlphaByte(kInactiveHitFillScale);
    for (const DrawRect& drawRect : g_frameRects) {
        const float x = static_cast<float>(drawRect.rect.left);
        const float y = static_cast<float>(drawRect.rect.top);
        const float w = static_cast<float>(drawRect.rect.right - drawRect.rect.left);
        const float h = static_cast<float>(drawRect.rect.bottom - drawRect.rect.top);
        if (w <= 0.0f || h <= 0.0f) {
            continue;
        }

        OverlayBox box{};
        box.x = x;
        box.y = y;
        box.w = w;
        box.h = h;
        box.fillArgb = ColorWithAlphaByte(drawRect.color, drawRect.inactive ? inactiveFillAlpha : fillAlpha);
        box.outlineArgb = ColorWithAlphaByte(drawRect.color, 255u);
        box.dottedOutline = drawRect.inactive;
        g_overlayBoxes.push_back(box);
    }
}

void ToggleLayerFromHotkey(int layerIndex, const char* label) {
    (void)label;
    const bool enabled = !LayerEnabledFromSettings(layerIndex);
    (void)SetLayerEnabledNoLock(layerIndex, enabled, "Revival hotkey replacement");
}

bool ConsumeDisplayHotkeyLocked(int key) {
    RefreshRevivalHotkeysLocked();
    if (key == g_revivalHotkeys.hit) {
        ToggleLayerFromHotkey(0, "Hitboxes");
        return true;
    }
    if (key == g_revivalHotkeys.hurt) {
        ToggleLayerFromHotkey(1, "Hurtboxes");
        return true;
    }
    if (key == g_revivalHotkeys.collision) {
        ToggleLayerFromHotkey(2, "Collision boxes");
        return true;
    }
    return false;
}

} // namespace

bool ProbeBattleCameraOffsets(int* outCameraX, int* outCameraY) {
    if (!outCameraX || !outCameraY) {
        return false;
    }

    const uintptr_t gameplayScreen = ReadActiveGameplayScreen();
    int cameraX = 0;
    int cameraY = 0;
    if (!gameplayScreen ||
        !ReadCameraOffsets(gameplayScreen, &cameraX, &cameraY)) {
        return false;
    }

    // Ordinary battle projection stays within a few hundred pixels; this guard rejects stale/wrong-object reads without clipping camera motion.
    constexpr int kCameraSanityLimit = 4096;
    if (cameraX < -kCameraSanityLimit || cameraX > kCameraSanityLimit ||
        cameraY < -kCameraSanityLimit || cameraY > kCameraSanityLimit) {
        return false;
    }

    *outCameraX = cameraX;
    *outCameraY = cameraY;
    return true;
}

bool ProbeProjectileRing(int playerIndex, ProjectileRingSlotProbe* outSlots,
                         std::size_t outCapacity,
                         ProjectileRingCursorProbe* outCursors) {
    if ((playerIndex != 1 && playerIndex != 2) || !outSlots ||
        outCapacity < kProjectileRingSlotCapacity) {
        return false;
    }

    if (outCursors) *outCursors = ProjectileRingCursorProbe{};

    const uintptr_t owner = GetPlayerObject(playerIndex);
    if (!owner) return false;

    uint32_t aliveFlags[kProjectileRingSlotCapacity] = {};
    if (!ReadBytes(owner + kProjectileAliveFlagsOffset,
                   aliveFlags, sizeof(aliveFlags))) {
        return false;
    }

    constexpr std::size_t kProbeEntryBytes =
        kProjectileLifeOffset + sizeof(int16_t);
    for (std::size_t slotIndex = 0;
         slotIndex < kProjectileRingSlotCapacity; ++slotIndex) {
        ProjectileRingSlotProbe& out = outSlots[slotIndex];
        out = ProjectileRingSlotProbe{};
        out.slot = static_cast<int>(slotIndex);
        out.readable = true;
        out.alive = aliveFlags[slotIndex] != 0;
        if (!out.alive) continue;

        const uintptr_t entryBase = owner + kProjectileEntryBaseOffset +
            kProjectileEntryStride * static_cast<uintptr_t>(slotIndex);
        uint8_t entry[kProbeEntryBytes] = {};
        if (!ReadBytes(entryBase, entry, sizeof(entry))) {
            out.readable = false;
            continue;
        }
        std::memcpy(&out.pattern, entry + kProjectilePatternOffset,
                    sizeof(out.pattern));
        std::memcpy(&out.frame, entry + kProjectileFrameOffset,
                    sizeof(out.frame));
        std::memcpy(&out.frameTick, entry + kProjectileFrameTickOffset,
                    sizeof(out.frameTick));
        std::memcpy(&out.x, entry + kProjectileXOffset, sizeof(out.x));
        std::memcpy(&out.y, entry + kProjectileYOffset, sizeof(out.y));
        std::memcpy(&out.destroyed, entry + kProjectileDestroyedOffset,
                    sizeof(out.destroyed));
        std::memcpy(&out.life, entry + kProjectileLifeOffset,
                    sizeof(out.life));
    }

    if (outCursors) {
        uint16_t cursors[2] = {};
        if (ReadBytes(owner + kProjectileTailOffset,
                      cursors, sizeof(cursors)) &&
            cursors[0] < kProjectileRingSlotCapacity &&
            cursors[1] < kProjectileRingSlotCapacity) {
            outCursors->readable = true;
            outCursors->allocationCursor = cursors[0];
            outCursors->scanHead = cursors[1];
        }
    }
    return true;
}

int ProbeProjectileLifeForPattern(int playerIndex, uint16_t pattern) {
    std::array<ProjectileRingSlotProbe, kProjectileRingSlotCapacity> slots{};
    if (!ProbeProjectileRing(playerIndex, slots.data(), slots.size())) {
        return kProjectileLifeProbeUnavailable;
    }
    int maxLife = -1;
    for (const ProjectileRingSlotProbe& slot : slots) {
        if (slot.alive && !slot.readable) {
            return kProjectileLifeProbeUnavailable;
        }
        if (!slot.alive || slot.pattern != pattern) continue;
        if (static_cast<int>(slot.life) > maxLife) {
            maxLife = static_cast<int>(slot.life);
        }
    }
    return maxLife;
}

void Initialize() {
    if (g_initialized.exchange(true)) {
        return;
    }

    if (!ResolveTransformFunctions()) {
        LogOut("[COLLISION_DISPLAY] Initialized; transform helpers will be resolved lazily", true);
    } else {
        LogOut("[COLLISION_DISPLAY] Initialized using efz.exe collision transforms", true);
    }
}

void Shutdown() {
    if (!g_initialized.exchange(false)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    g_frameRects.clear();
    g_overlayBoxes.clear();
}

bool IsAnyLayerEnabled() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return HasAnyEnabledLayer();
}

Status GetStatus() {
    std::lock_guard<std::mutex> lock(g_mutex);
    Status status{};
    status.hitEnabled = LayerEnabledFromSettings(0);
    status.hurtEnabled = LayerEnabledFromSettings(1);
    status.collisionEnabled = LayerEnabledFromSettings(2);
    status.projectileInteractionsEnabled = LayerEnabledFromSettings(3);
    status.toggleCount = g_toggleCount;
    return status;
}

void SetLayerEnabled(int layerIndex, bool enabled) {
    if (layerIndex < 0 || layerIndex >= kLayerCount) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    (void)SetLayerEnabledNoLock(layerIndex, enabled, "menu");
}

void RebuildFrame() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_frameRects.clear();
    g_overlayBoxes.clear();

    const uint8_t layerMask = CurrentLayerMaskFromSettings();
    if (!HasAnyEnabledLayer() || !ResolveTransformFunctions() || !IsPracticeGameplayActive()) {
        return;
    }

    const uintptr_t gameplayScreen = ReadActiveGameplayScreen();
    if (!gameplayScreen) {
        return;
    }

    int camX = 0;
    int camY = 0;
    if (!ReadCameraOffsets(gameplayScreen, &camX, &camY)) {
        return;
    }

    if ((layerMask & kLayerHit) != 0) {
        CollectLayer(0, camX, camY, g_frameRects);
    }
    if ((layerMask & kLayerHurt) != 0) {
        CollectLayer(1, camX, camY, g_frameRects);
    }
    if ((layerMask & kLayerCollision) != 0) {
        CollectLayer(2, camX, camY, g_frameRects);
    }

    RebuildOverlayBoxesLocked();
    if ((layerMask & kLayerProjectileInteractions) != 0 && AnyProjectileInteractionSubLayerEnabled()) {
        AppendProjectileInteractions(camX, camY);
    }
}

std::size_t GetOverlayBoxCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_overlayBoxes.size();
}

bool GetOverlayBox(std::size_t index, OverlayBox* outBox) {
    if (!outBox) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (index >= g_overlayBoxes.size()) {
        return false;
    }
    *outBox = g_overlayBoxes[index];
    return true;
}

bool IsGameHotkeyVk(int vk) {
    if (vk >= VK_F4 && vk <= VK_F8) return true;
    switch (vk) {
    case '1': case '2': case 'U': case 'J': case 'L': case 'I': case 'O': case 'K': case 'P': case VK_SPACE:
        return false;
    default:
        break;
    }
    if (vk <= 0 || vk >= 256) return false;

    static bool s_loaded = false;
    static bool s_reserved[256] = {};
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!s_loaded) {
        s_loaded = true;
        RefreshRevivalHotkeysLocked();
        const auto reserve = [](int dik) {
            const int mapped = dik > 0 ? MapDIKToVK(dik) : 0;
            if (mapped > 0 && mapped < 256) s_reserved[mapped] = true;
        };
        reserve(g_revivalHotkeys.hit);
        reserve(g_revivalHotkeys.hurt);
        reserve(g_revivalHotkeys.collision);
        char path[MAX_PATH] = {};
        if (ResolveRevivalIniPath(path, sizeof(path))) {
            static const char* const kKeys[] = {
                "Pause", "StepFrame", "Save", "Load", "ToggleReplay", "ToggleRecord",
                "SwitchStartCondition", "ToggleReplayRandom", "ToggleDisplay", "SwitchPlayers",
                "MirrorPlayers", "IncreaseFPS", "DecreaseFPS", "ResetFPS",
                "SetBuffer1", "SetBuffer2", "SetBuffer3", "SetBuffer4", "SetBuffer5", "KillBG",
            };
            for (const char* key : kKeys) reserve(ReadRevivalHotkeyFromIni(path, key, 0));
        }
    }
    return s_reserved[vk];
}

bool ShouldSuppressRevivalHotkey(void* /*practiceController*/, int key) {
    if (key == 0 || !GetModuleHandleA("EfzRevival.dll")) {
        return false;
    }
    if (GetCurrentGameMode() != GameMode::Practice || IsNetplaySuspendActive()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    // If the overlay can't render, Revival's own display must keep its hotkeys.
    if (!ResolveTransformFunctions()) {
        return false;
    }
    return ConsumeDisplayHotkeyLocked(key);
}

} // namespace CollisionDisplay
