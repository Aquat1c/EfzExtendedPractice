#pragma once
#include "game/palette_assets.h"
#include <bitset>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace PaletteEditor {
struct Hsv { float h = 0, s = 0, v = 0; };
struct Ramp { unsigned first = 0, last = 0, key = 0; };
Hsv RgbToHsv(Color color);
Color HsvToRgb(Hsv color);
bool ParseHex(const std::string& text, Color& color);
std::string FormatHex(Color color);
std::vector<Ramp> DetectRamps(const Palette& original);

struct SavedPalette {
    std::string name, character;
    unsigned stockSlot = 0;
    Palette baseline{}, colors{};
    std::bitset<40> pins;
    std::array<std::optional<Color>, 40> targets{};
    std::vector<Ramp> ramps;
};
class Document {
public:
    void LoadStock(const std::string& character, unsigned slot, const Palette& stock);
    bool LoadSaved(const SavedPalette& saved, const Palette& stock, std::string& error);
    void Import(const Palette& colors);
    const Palette& Original() const { return state_.baseline; }
    const Palette& Colors() const { return state_.colors; }
    const SavedPalette& State() const { return state_; }
    const std::vector<Ramp>& Ramps() const { return state_.ramps; }
    bool Dirty() const;
    bool Pinned(unsigned entry) const { return entry < 40 && state_.pins[entry]; }
    Hsv ColorHsv(unsigned entry) const;
    Hsv RampHsv(unsigned ramp) const;
    Color RampColor(unsigned ramp) const;
    void SetName(const std::string& name) { state_.name = name; }
    bool SetColor(unsigned entry, Color color);
    bool SetHsv(unsigned entry, Hsv color);
    bool SetHex(unsigned entry, const std::string& text);
    bool SetRampColor(unsigned ramp, Color color);
    bool SetRampHsv(unsigned ramp, Hsv color);
    bool SetRampHex(unsigned ramp, const std::string& text);
    bool SetPinned(unsigned entry, bool pinned);
    bool ResetEntry(unsigned entry);
    bool ResetRamp(unsigned ramp);
    bool ResetAll();
    void BeginGesture();
    void EndGesture();
    bool Undo();
    std::size_t UndoCount() const { return history_.size(); }
    void MarkSaved();
    void Revert();
private:
    void Checkpoint();
    void ComposeRamp(unsigned ramp);
    void ResetHsv();
    SavedPalette state_, loaded_;
    struct Snapshot { SavedPalette palette; std::array<Hsv, 40> hsv, rampHsv; };
    std::vector<Snapshot> history_;
    std::array<Hsv, 40> hsv_{}, rampHsv_{};
    bool gesture_ = false, gestureSaved_ = false;
};

struct LibraryEntry {
    std::filesystem::path path;
    std::string name, character, error;
    unsigned stockSlot = 0;
    bool deleted = false;
    std::uint64_t fingerprint = 0;
};
class Library {
public:
    explicit Library(std::filesystem::path directory) : directory_(std::move(directory)) {}
    const std::filesystem::path& Directory() const { return directory_; }
    std::vector<LibraryEntry> Scan(const std::string& character, bool deleted, std::string& error) const;
    bool Read(const LibraryEntry& entry, SavedPalette& out, std::string& error) const;
    bool SaveNew(const SavedPalette& palette, LibraryEntry& out, std::string& error) const;
    bool Replace(const LibraryEntry& entry, const SavedPalette& palette, LibraryEntry& out, std::string& error) const;
    bool Delete(const LibraryEntry& entry, LibraryEntry& deleted, std::string& error) const;
    bool Restore(const LibraryEntry& deleted, LibraryEntry& restored, std::string& error) const;
private:
    bool CheckPath(const LibraryEntry& entry, std::string& error) const;
    std::filesystem::path directory_;
};
struct PaletteFile {
    std::filesystem::path path;
    SavedPalette palette;
    bool native = false;
    std::uint64_t fingerprint = 0;
};
bool ReadPaletteFileIdentity(const std::filesystem::path& path, std::uint64_t& fingerprint, std::string& error);
bool ReadPaletteFile(const std::filesystem::path& path, PaletteFile& out, std::string& error);
// A missing expected fingerprint permits creation only.
bool SavePaletteFile(const std::filesystem::path& path, const SavedPalette& palette,
    std::optional<std::uint64_t> expectedFingerprint, PaletteFile& out, std::string& error);

bool ReadNativePalette(const std::filesystem::path& path, Palette& out, std::string& error);
bool ExportNativePalette(const std::filesystem::path& path, const Palette& colors, bool replace, std::string& error);
}
