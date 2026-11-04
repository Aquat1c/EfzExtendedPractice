#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace PaletteEditor {

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b;
    }
    bool operator!=(const Color& other) const { return !(*this == other); }
};

using Palette = std::array<Color, 40>;
using Palette256 = std::array<Color, 256>;

struct IndexedSprite {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> indices;
};

struct AnimationFrame {
    int spriteId = 0;
    int durationFrames = 0; // PAT visual frames at 64 Hz; zero-duration records are valid.
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
};

struct Animation {
    int id = 0;
    std::vector<AnimationFrame> frames;
};

struct CharacterAssets {
    std::array<Palette, 6> stockPalettes{};
    std::vector<IndexedSprite> sprites;
    std::vector<Animation> animations;
    uint8_t transparentKey = 0;
};

enum class AnimationVariant { Standard, Alternate };
bool HasAlternateAnimations(const std::string& resourceName);

// Failed loads leave the destination unchanged.
bool DecodeNativePalette(const std::vector<uint8_t>& bytes, Palette& out, std::string& error);
std::array<uint8_t, 121> EncodeNativePalette(const Palette& palette);
bool DecodeCharacterAssets(const std::vector<uint8_t>& dat,
                           const std::vector<uint8_t>& pat,
                           CharacterAssets& out, std::string& error);
bool LoadCharacterAssets(const std::filesystem::path& gameDirectory,
                         const std::string& resourceName,
                         CharacterAssets& out, std::string& error,
                         AnimationVariant variant = AnimationVariant::Standard);
const Animation* FindAnimation(const CharacterAssets& assets, int id);
Palette256 BuildPreviewColors(const Palette& palette, const Palette256& sharedRuntimeColors);
// Loads shared battle entries 175..254; entries outside that bank remain zero.
bool LoadSharedBattlePalette(const std::filesystem::path& gameDirectory,
                            int stageId, Palette256& out, std::string& error);

}
