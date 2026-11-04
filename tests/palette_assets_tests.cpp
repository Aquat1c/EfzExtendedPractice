#include "game/palette_assets.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
using namespace PaletteEditor;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

void AppendInt(std::vector<uint8_t>& bytes, int value) {
    const auto raw = static_cast<uint32_t>(value);
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<uint8_t>(raw >> shift));
}

std::vector<uint8_t> Dat() {
    std::vector<uint8_t> bytes(721);
    bytes[0] = 240;
    bytes[1] = 255; bytes[2] = 128; bytes[3] = 17;
    bytes[601] = 7; bytes[602] = 8; bytes[603] = 9;
    const uint8_t sprites[] = {3, 0, 1, 0, 15, 0x82, 2, 0, 1, 0, 119, 128};
    bytes.insert(bytes.end(), std::begin(sprites), std::end(sprites));
    return bytes;
}

std::vector<uint8_t> Pat() {
    std::vector<uint8_t> bytes;
    AppendInt(bytes, 2);
    for (int id : {0, 45}) {
        for (int value : {id, 0, 0, 1}) AppendInt(bytes, value);
        const int properties[] = {id == 0 ? 0 : 6, id == 0 ? 0 : 1, 2, 1, -2, 1};
        for (int value : properties) AppendInt(bytes, value);
        for (int p = 6; p < 60; ++p) AppendInt(bytes, 0);
    }
    return bytes;
}

void Write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Check(file.good(), "fixture file writes completely");
}
}

int main(int argc, char** argv) {
    using namespace PaletteEditor;
    std::string error;
    Palette palette{};
    std::vector<uint8_t> pal(121);
    pal[0] = 40; pal[1] = 255; pal[2] = 128; pal[3] = 17;
    Check(DecodeNativePalette(pal, palette, error), "native palette accepts the game color-count header");
    Check(palette[0] == Color{17, 128, 255}, "native BGR preserves all eight channel bits");
    pal[0] = 1;
    Check(DecodeNativePalette(pal, palette, error), "legacy editor header is accepted");
    const auto encoded = EncodeNativePalette(palette);
    Check(encoded[0] == 40 && encoded[1] == 255 && encoded[2] == 128 && encoded[3] == 17,
          "native export writes color-count header and BGR bytes");
    pal.pop_back();
    Check(!DecodeNativePalette(pal, palette, error) && palette[0] == Color{17, 128, 255},
          "truncated palette is rejected without changing output");

    CharacterAssets assets;
    Check(DecodeCharacterAssets(Dat(), Pat(), assets, error), "character DAT and PAT decode");
    Check(assets.sprites.size() == 2 && assets.sprites[0].indices == std::vector<uint8_t>({15, 15, 15}),
          "repeat-current RLE consumes exactly one chained sprite");
    Check(assets.sprites[1].indices == std::vector<uint8_t>({119, 128}), "0x80 remains a literal");
    Check(assets.transparentKey == 15, "color key comes from the first decoded pixel");
    Check(assets.stockPalettes[0][0] == Color{17, 128, 255} && assets.stockPalettes[5][0] == Color{9, 8, 7},
          "six stock banks use the palette header rather than sprite bytes");
    const Animation* animation = FindAnimation(assets, 45);
    Check(animation && animation->frames[0].spriteId == 1 && animation->frames[0].durationFrames == 6
              && animation->frames[0].originX == -2 && !FindAnimation(assets, 1),
          "sparse move IDs select their own sprite and timing");

    auto dat = Dat(); dat[726] = 0x83;
    Check(!DecodeCharacterAssets(dat, Pat(), assets, error) && assets.sprites.size() == 2,
          "an RLE overrun is rejected transactionally");
    dat = Dat(); dat.pop_back();
    Check(!DecodeCharacterAssets(dat, Pat(), assets, error), "truncated sprite is rejected");
    dat = Dat(); dat[0] = 40;
    Check(!DecodeCharacterAssets(dat, Pat(), assets, error), "character bank count must describe six stock palettes");
    dat = Dat(); dat[721] = 255; dat[722] = 255;
    Check(!DecodeCharacterAssets(dat, Pat(), assets, error), "oversized dimensions fail before pixel allocation");
    dat = Dat(); dat[725] = 0x81;
    Check(!DecodeCharacterAssets(dat, Pat(), assets, error), "a repeat requires a preceding literal color");
    auto pat = Pat(); pat.pop_back();
    Check(!DecodeCharacterAssets(Dat(), pat, assets, error), "truncated PAT box payload is rejected");
    pat = Pat(); pat[24] = 2;
    Check(!DecodeCharacterAssets(Dat(), pat, assets, error), "PAT cannot reference a missing sprite");
    pat = Pat(); pat[20] = 255; pat[21] = 255; pat[22] = 255; pat[23] = 255;
    Check(!DecodeCharacterAssets(Dat(), pat, assets, error), "negative frame duration is rejected");
    pat = Pat(); pat.push_back(0);
    Check(!DecodeCharacterAssets(Dat(), pat, assets, error), "unexpected trailing PAT bytes are rejected");
    pat = Pat(); pat[0] = 255; pat[1] = 255; pat[2] = 255; pat[3] = 127;
    Check(!DecodeCharacterAssets(Dat(), pat, assets, error), "untrusted animation counts cannot drive large allocations");
    pat = Pat(); pat[260] = 0;
    Check(DecodeCharacterAssets(Dat(), pat, assets, error) && assets.animations.size() == 1
              && FindAnimation(assets, 0)->frames[0].spriteId == 1,
          "later duplicate PAT animation replaces the earlier native table entry");

    Palette256 shared{};
    shared[175] = {1, 2, 3}; shared[254] = {4, 5, 6}; shared[7] = {7, 8, 9};
    const auto preview = BuildPreviewColors(palette, shared);
    Check(preview[0] == palette[0] && preview[40] == Color{1, 2, 3}
              && preview[119] == Color{4, 5, 6} && preview[128] == Color{7, 8, 9},
          "preview remaps shared colors using native wraparound");

    const auto fixture = std::filesystem::temp_directory_path()
        / ("efz_palette_assets_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Write(fixture / "ayu" / "ayu.dat", Dat());
    Write(fixture / "ayu" / "ayu.pat", Pat());
    Check(LoadCharacterAssets(fixture, "ayu", assets, error), "character files resolve under their resource directory");
    Check(!LoadCharacterAssets(fixture, "../ayu", assets, error), "resource names cannot escape the game directory");
    Check(HasAlternateAnimations("nanase") && HasAlternateAnimations("ikumi") && !HasAlternateAnimations("ayu"),
          "only fighters with native alternate PAT tables offer that variant");
    Write(fixture / "nanase" / "nanase.dat", Dat());
    Write(fixture / "nanase" / "nanase.pat", Pat());
    auto alternate = Pat(); alternate[20] = 9;
    Write(fixture / "nanase" / "nanase2.pat", alternate);
    Check(LoadCharacterAssets(fixture, "nanase", assets, error, AnimationVariant::Alternate)
              && FindAnimation(assets, 0)->frames[0].durationFrames == 9,
          "alternate PAT selects its own animations while sharing the character sprite pool");
    Check(!LoadCharacterAssets(fixture, "ayu", assets, error, AnimationVariant::Alternate),
          "unsupported alternate PAT cannot silently load the primary animations");
    std::vector<uint8_t> all(1 + 16 * 3); all[0] = 16; all[1] = 3; all[2] = 2; all[3] = 1;
    std::vector<uint8_t> stage(1 + 64 * 3); stage[0] = 64; stage[190] = 6; stage[191] = 5; stage[192] = 4;
    Write(fixture / "system" / "stage" / "all.dat", all);
    Write(fixture / "system" / "stage" / "00.dat", stage);
    Check(LoadSharedBattlePalette(fixture, 0, shared, error)
              && shared[175] == Color{1, 2, 3} && shared[254] == Color{4, 5, 6},
          "shared colors combine stage all and selected stage banks");
    Check(!LoadSharedBattlePalette(fixture, 23, shared, error) && shared[175] == Color{1, 2, 3},
          "invalid stage leaves shared colors unchanged");
    std::filesystem::remove_all(fixture);

    if (argc == 2) {
        const std::filesystem::path root(argv[1]);
        int checked = 0;
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (!entry.is_directory()) continue;
            const auto resource = entry.path().filename().string();
            if (!std::filesystem::exists(entry.path() / (resource + ".pat"))) continue;
            if (!LoadCharacterAssets(root, resource, assets, error)) {
                std::cerr << resource << ": " << error << '\n';
                return 1;
            }
            ++checked;
            if (HasAlternateAnimations(resource)) {
                Check(LoadCharacterAssets(root, resource, assets, error, AnimationVariant::Alternate),
                      "local alternate PAT references the shared sprite pool correctly");
            }
        }
        Check(checked > 0, "optional local asset check finds character packs");
        for (int stageId = 0; stageId < 23; ++stageId) {
            Check(LoadSharedBattlePalette(root, stageId, shared, error), "local stage palette loads completely");
        }
        std::cout << "Parsed " << checked << " local character packs\n";
    }
    std::cout << "palette_assets_tests passed\n";
}
