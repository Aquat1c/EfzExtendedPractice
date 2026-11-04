#include "game/palette_assets.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace PaletteEditor {
namespace {
constexpr size_t kMaxFileBytes = 64 * 1024 * 1024;
constexpr size_t kMaxDecodedPixels = 64 * 1024 * 1024;
constexpr size_t kMaxSprites = 8192;
constexpr int kMaxDimension = 4096;
constexpr size_t kFrameBytes = 240;

bool Fail(std::string& error, const char* message) {
    error = message;
    return false;
}

Color ReadColor(const uint8_t* bytes) {
    return {bytes[2], bytes[1], bytes[0]};
}

int ReadInt(const std::vector<uint8_t>& bytes, size_t offset) {
    const uint32_t value = static_cast<uint32_t>(bytes[offset])
        | (static_cast<uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    int32_t signedValue;
    std::memcpy(&signedValue, &value, sizeof(value));
    return signedValue;
}

bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return Fail(error, "Could not open the requested asset file.");
    const auto length = file.tellg();
    if (length <= 0 || length > static_cast<std::streamoff>(kMaxFileBytes)) {
        return Fail(error, "Asset file is empty or exceeds the 64 MiB limit.");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return Fail(error, "Could not read the complete asset file.");
    }
    out = std::move(bytes);
    return true;
}

bool DecodeSprites(const std::vector<uint8_t>& dat, CharacterAssets& assets, std::string& error) {
    if (dat.size() < 721 || dat.size() > kMaxFileBytes || dat[0] != 240) {
        return Fail(error, "Character DAT must contain six 40-color palettes.");
    }
    for (size_t slot = 0; slot < assets.stockPalettes.size(); ++slot) {
        for (size_t color = 0; color < 40; ++color) {
            assets.stockPalettes[slot][color] = ReadColor(dat.data() + 1 + slot * 120 + color * 3);
        }
    }
    size_t offset = 1 + static_cast<size_t>(dat[0]) * 3;
    size_t decodedPixels = 0;
    while (offset < dat.size()) {
        if (dat.size() - offset < 4 || assets.sprites.size() >= kMaxSprites) {
            return Fail(error, "Sprite header is truncated or the sprite limit was exceeded.");
        }
        IndexedSprite sprite;
        sprite.width = dat[offset] | (dat[offset + 1] << 8);
        sprite.height = dat[offset + 2] | (dat[offset + 3] << 8);
        offset += 4;
        if (sprite.width < 1 || sprite.height < 1
            || sprite.width > kMaxDimension || sprite.height > kMaxDimension) {
            return Fail(error, "Sprite dimensions are invalid.");
        }
        const size_t pixels = static_cast<size_t>(sprite.width) * sprite.height;
        if (pixels > kMaxDecodedPixels - decodedPixels) {
            return Fail(error, "Decoded sprites exceed the 64 MiB limit.");
        }
        sprite.indices.reserve(pixels);
        uint8_t current = 0;
        while (sprite.indices.size() < pixels) {
            if (offset == dat.size()) return Fail(error, "Sprite pixel stream is truncated.");
            const uint8_t code = dat[offset++];
            if (code <= 0x80) {
                current = code;
                sprite.indices.push_back(current);
            } else {
                const size_t run = code - 0x80;
                if (sprite.indices.empty() || run > pixels - sprite.indices.size()) {
                    return Fail(error, "Sprite repeat exceeds its frame or has no preceding color.");
                }
                sprite.indices.insert(sprite.indices.end(), run, current);
            }
        }
        if (assets.sprites.empty()) assets.transparentKey = sprite.indices.front();
        decodedPixels += pixels;
        assets.sprites.push_back(std::move(sprite));
    }
    if (assets.sprites.empty()) return Fail(error, "Character DAT contains no sprites.");
    return true;
}

bool DecodeAnimations(const std::vector<uint8_t>& pat, CharacterAssets& assets, std::string& error) {
    if (pat.size() < 4 || pat.size() > kMaxFileBytes) return Fail(error, "Invalid PAT file size.");
    const int count = ReadInt(pat, 0);
    if (count < 1 || count > 8192 || static_cast<size_t>(count) > (pat.size() - 4) / 16) {
        return Fail(error, "Invalid PAT animation count.");
    }
    size_t offset = 4;
    assets.animations.reserve(static_cast<size_t>(count));
    for (int a = 0; a < count; ++a) {
        if (pat.size() - offset < 16) return Fail(error, "PAT animation header is truncated.");
        Animation animation;
        animation.id = ReadInt(pat, offset);
        const int frameCount = ReadInt(pat, offset + 12);
        offset += 16;
        if (animation.id < 0 || animation.id > 65535) {
            return Fail(error, "PAT animation ID is invalid.");
        }
        if (frameCount < 0 || frameCount > 255
            || static_cast<size_t>(frameCount) > (pat.size() - offset) / kFrameBytes) {
            return Fail(error, "PAT frames are truncated or their count is invalid.");
        }
        animation.frames.reserve(static_cast<size_t>(frameCount));
        for (int f = 0; f < frameCount; ++f) {
            AnimationFrame frame;
            frame.durationFrames = ReadInt(pat, offset);
            frame.spriteId = ReadInt(pat, offset + 4);
            frame.width = ReadInt(pat, offset + 8);
            frame.height = ReadInt(pat, offset + 12);
            frame.originX = ReadInt(pat, offset + 16);
            frame.originY = ReadInt(pat, offset + 20);
            if (frame.spriteId < 0 || static_cast<size_t>(frame.spriteId) >= assets.sprites.size()) {
                return Fail(error, "PAT frame references a missing sprite.");
            }
            if (frame.durationFrames < 0 || frame.durationFrames > 32767
                || frame.width < 1 || frame.height < 1
                || frame.width > kMaxDimension || frame.height > kMaxDimension
                || frame.originX < -32768 || frame.originX > 32767
                || frame.originY < -32768 || frame.originY > 32767) {
                return Fail(error, "PAT frame duration or geometry is invalid.");
            }
            animation.frames.push_back(frame);
            offset += kFrameBytes;
        }
        const auto previous = std::find_if(assets.animations.begin(), assets.animations.end(),
            [&animation](const Animation& value) { return value.id == animation.id; });
        // The native PAT loader replaces table[id] in file order.
        if (previous == assets.animations.end()) assets.animations.push_back(std::move(animation));
        else *previous = std::move(animation);
    }
    if (offset != pat.size()) return Fail(error, "PAT file has unexpected trailing bytes.");
    return true;
}

bool LoadPaletteRange(const std::filesystem::path& path, size_t destination,
                      size_t count, Palette256& out, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes, error)) return false;
    if (bytes[0] < count || bytes.size() < 1 + static_cast<size_t>(bytes[0]) * 3) {
        return Fail(error, "Shared palette is truncated or contains too few colors.");
    }
    for (size_t i = 0; i < count; ++i) out[destination + i] = ReadColor(bytes.data() + 1 + i * 3);
    return true;
}
}

bool DecodeNativePalette(const std::vector<uint8_t>& bytes, Palette& out, std::string& error) {
    if (bytes.size() != 121) return Fail(error, "Native PAL files must contain a header and 40 BGR colors (121 bytes).");
    Palette palette{};
    for (size_t i = 0; i < palette.size(); ++i) palette[i] = ReadColor(bytes.data() + 1 + i * 3);
    out = palette;
    error.clear();
    return true;
}

std::array<uint8_t, 121> EncodeNativePalette(const Palette& palette) {
    std::array<uint8_t, 121> bytes{};
    bytes[0] = 40;
    for (size_t i = 0; i < palette.size(); ++i) {
        bytes[1 + i * 3] = palette[i].b;
        bytes[2 + i * 3] = palette[i].g;
        bytes[3 + i * 3] = palette[i].r;
    }
    return bytes;
}

bool DecodeCharacterAssets(const std::vector<uint8_t>& dat, const std::vector<uint8_t>& pat,
                           CharacterAssets& out, std::string& error) {
    CharacterAssets assets;
    if (!DecodeSprites(dat, assets, error) || !DecodeAnimations(pat, assets, error)) return false;
    out = std::move(assets);
    error.clear();
    return true;
}

bool HasAlternateAnimations(const std::string& resourceName) {
    return resourceName == "nanase" || resourceName == "ikumi";
}

bool LoadCharacterAssets(const std::filesystem::path& gameDirectory, const std::string& resourceName,
                         CharacterAssets& out, std::string& error, AnimationVariant variant) {
    if (resourceName.empty() || resourceName.size() > 32
        || !std::all_of(resourceName.begin(), resourceName.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_';
        })) {
        return Fail(error, "Invalid character resource name.");
    }
    const auto directory = gameDirectory / resourceName;
    if (variant == AnimationVariant::Alternate && !HasAlternateAnimations(resourceName)) {
        return Fail(error, "This character has no alternate animation table.");
    }
    const std::string patName = resourceName + (variant == AnimationVariant::Alternate ? "2.pat" : ".pat");
    std::vector<uint8_t> dat;
    std::vector<uint8_t> pat;
    if (!ReadFile(directory / (resourceName + ".dat"), dat, error)
        || !ReadFile(directory / patName, pat, error)) return false;
    return DecodeCharacterAssets(dat, pat, out, error);
}

const Animation* FindAnimation(const CharacterAssets& assets, int id) {
    const auto found = std::find_if(assets.animations.begin(), assets.animations.end(),
        [id](const Animation& animation) { return animation.id == id; });
    return found == assets.animations.end() ? nullptr : &*found;
}

Palette256 BuildPreviewColors(const Palette& palette, const Palette256& sharedRuntimeColors) {
    Palette256 colors{};
    std::copy(palette.begin(), palette.end(), colors.begin());
    for (size_t i = palette.size(); i < colors.size(); ++i) colors[i] = sharedRuntimeColors[(i + 135) & 255];
    return colors;
}

bool LoadSharedBattlePalette(const std::filesystem::path& gameDirectory, int stageId,
                            Palette256& out, std::string& error) {
    if (stageId < 0 || stageId >= 23) return Fail(error, "Invalid stage palette selection.");
    const auto directory = gameDirectory / "system" / "stage";
    const auto filename = (stageId < 10 ? "0" : "") + std::to_string(stageId) + ".dat";
    Palette256 palette{};
    if (!LoadPaletteRange(directory / "all.dat", 175, 16, palette, error)
        || !LoadPaletteRange(directory / filename, 191, 64, palette, error)) return false;
    out = palette;
    error.clear();
    return true;
}
}
