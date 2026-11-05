#include "../include/utils/i18n.h"
#include "../include/utils/i18n_ja.generated.h"
#include "../include/game/mission/move_notation_tables.h"
#include "../include/game/mission/entity_notation_tables.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

std::vector<char> ConversionSequence(const char* s) {
    std::vector<char> out;
    for (const char* p = s; *p; ++p) {
        if (*p != '%') continue;
        ++p;
        if (*p == '%') continue;
        while (*p && std::strchr("-+ #0123456789.*", *p)) ++p;
        while (*p && std::strchr("hlLzjtI64", *p)) ++p;
        if (*p) out.push_back(*p);
    }
    return out;
}

bool HasNonAscii(const char* s) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
        if (*p >= 0x80) return true;
    }
    return false;
}

unsigned DecodeUtf8(const unsigned char*& p) {
    const unsigned c = *p;
    if (c < 0x80) { ++p; return c; }
    int extra = 0;
    unsigned cp = 0;
    if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
    else { ++p; return 0; }
    ++p;
    for (int i = 0; i < extra; ++i) {
        if ((*p & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (*p & 0x3F);
        ++p;
    }
    return cp;
}

bool InGlyphRanges(unsigned cp) {
    const unsigned short* r = I18n::JapaneseGlyphRanges();
    for (; r[0] != 0; r += 2) {
        if (cp >= r[0] && cp <= r[1]) return true;
    }
    return false;
}

void TestTableIntegrity() {
    using I18n::Generated::kJaTable;
    using I18n::Generated::kJaTableCount;
    CHECK(kJaTableCount > 1000);
    for (unsigned i = 0; i < kJaTableCount; ++i) {
        const char* en = kJaTable[i].en;
        const char* ja = kJaTable[i].ja;
        CHECK(en && *en);
        CHECK(ja && *ja);
        CHECK(std::strcmp(en, ja) != 0);
        if (i > 0) CHECK(std::strcmp(kJaTable[i - 1].en, en) < 0);
        CHECK(ConversionSequence(en) == ConversionSequence(ja));
        if (std::strchr(en, '%') == nullptr) CHECK(std::strchr(ja, '%') == nullptr);
        // Replacement character and half-width katakana (U+FF61-U+FF9F) are not allowed.
        const unsigned char* p = reinterpret_cast<const unsigned char*>(ja);
        while (*p) {
            const unsigned cp = DecodeUtf8(p);
            CHECK(cp != 0xFFFD);
            CHECK(!(cp >= 0xFF61 && cp <= 0xFF9F));
            if (cp >= 0x80) CHECK(InGlyphRanges(cp));
        }
    }
}

void TestLanguageSwitch() {
    I18n::SetLanguage(I18n::English);
    CHECK(I18n::GetLanguage() == I18n::English);
    const char* en = "OPEN MENU";
    CHECK(I18n::Tr(en) == en);

    I18n::SetLanguage(I18n::Japanese);
    CHECK(I18n::GetLanguage() == I18n::Japanese);
    CHECK(std::strcmp(I18n::Tr(en), "\xE3\x83\xA1\xE3\x83\x8B\xE3\x83\xA5\xE3\x83\xBC\xE3\x82\x92\xE9\x96\x8B\xE3\x81\x8F\xE3\x82\xAD\xE3\x83\xBC") == 0);
    CHECK(std::strcmp(I18n::Tr("Mishio: "), "\xE7\xBE\x8E\xE6\xB1\x90: ") == 0);
    const char* miss = "not a table entry";
    CHECK(I18n::Tr(miss) == miss);
    CHECK(I18n::Tr(nullptr) == nullptr);

    I18n::SetLanguage(I18n::English);
    CHECK(I18n::Tr(en) == en);
}

void TestFormattedStrings() {
    I18n::SetLanguage(I18n::English);
    CHECK(I18n::TrFormat("MoveID:  P1: %d  P2: %d", 3, 4) == "MoveID:  P1: 3  P2: 4");
    CHECK(I18n::TrFormat("Status: %s", "idle") == "Status: idle");

    I18n::SetLanguage(I18n::Japanese);
    CHECK(I18n::TrFormat("MoveID:  P1: %d  P2: %d", 3, 4) == "\xE6\x8A\x80ID:  P1: 3  P2: 4");
    CHECK(I18n::TrFormat("Status: %s", "idle") == "\xE7\x8A\xB6\xE6\x85\x8B: idle");
    CHECK(I18n::TrFormat("Open the menu: %s (Controller: %s).", "Esc", "START")
          == "\xE3\x83\xA1\xE3\x83\x8B\xE3\x83\xA5\xE3\x83\xBC\xE3\x82\x92\xE9\x96\x8B\xE3\x81\x8F: Esc (\xE3\x82\xB3\xE3\x83\xB3\xE3\x83\x88\xE3\x83\xAD\xE3\x83\xBC\xE3\x83\xA9\xE3\x83\xBC: START)\xE3\x80\x82");
    I18n::SetLanguage(I18n::English);
}

void TestPaletteMoveLabelsTranslated() {
    I18n::SetLanguage(I18n::Japanese);
    const auto check = [](const auto& tables) {
        for (const auto& table : tables) {
            for (int i = 0; i < table.count; ++i) {
                const char* en = table.entries[i].note;
                if (std::strcmp(en, "FMA") == 0) continue;
                int letters = 0;
                bool prose = false;
                for (const char* p = en; *p; ++p) {
                    letters = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ? letters + 1 : 0;
                    if (letters >= 3) prose = true;
                }
                if (!prose) continue;
                const char* ja = I18n::Tr(en);
                if (ja == en) std::fprintf(stderr, "Missing move translation: %s\n", en);
                CHECK(ja != en);
                CHECK(HasNonAscii(ja));
            }
        }
    };
    check(Mission::MoveNames::kCharTables);
    check(Mission::EntityNames::kCharTables);
    I18n::SetLanguage(I18n::English);
}

void TestPaletteStringsTranslated() {
    const char* required[] = {
        "Palette", "SAVE AS", "PICKER", "SHADE", "RAMP", "UNPIN", "PIN", "UNDO %zu",
        "TOOLS", "Palette tools", "Generate palette", "Randomize selection", "RGB gradient", "HSV gradient",
        "Color copied.", "Copy a color before pasting.",
        "The transparent shade is protected.", "Palette tool applied. Undo restores the previous edit.",
        "No editable colors changed. Check pins and the selected ramp.",
        "Generate new ramp colors for the whole palette. Pins and transparency are protected; Undo restores the draft.",
        "Randomize the selected ramp or shade. Pins and transparency are protected.",
        "Blend between this ramp's endpoint colors in RGB. Changed shades become pinned overrides; existing pins are kept.",
        "Blend this ramp's endpoints along the shortest hue path. Changed shades become pinned overrides; existing pins are kept.",
        "Lighten the selected ramp or shade by 16 RGB steps. Ramp shading and pinned shades are preserved.",
        "Darken the selected ramp or shade by 16 RGB steps. Ramp shading and pinned shades are preserved.",
        "Copy the selected ramp's target color or individual shade for use elsewhere in this editor.",
        "Paste the copied color into this ramp or shade. Ramp shading and pinned shades are preserved.",
        "Generate palette colors or create RGB/HSV gradients in the selected ramp.",
        "All moves", "Normals", "Specials", "Supers", "Entities / effects", "Unclassified",
        "Idle", "Walk forward", "Walk backward",
        "Crouch", "Stand up", "Jump startup",
        "Jump", "Jump forward", "Jump backward",
        "Falling", "Double jump", "Double jump forward",
        "Double jump backward", "Landing", "Standing guard",
        "Crouching guard", "Air guard", "Standing recoil guard",
        "Crouching recoil guard", "Air recoil guard", "Sweep hitstun",
        "Air recovery forward", "Air recovery backward", "Ground recovery startup",
        "Ground recovery finish", "Dash forward", "Dash backward",
        "Air dash forward", "Air dash backward", "Dash recovery",
        "Ground instant charge", "Air instant charge", "Burning hitstun",
        "Electric hitstun", "Standing hitstun", "Crouching hitstun",
        "Air hitstun", "Ground recovery", "Frozen hitstun",
        "Thrown", "System", "system",
        "command", "special", "super",
        "projectile", "super_ent", "entity",
        "Stock %d loaded.", "Picked ramp %d from shade %d.", "Picked shade %d.",
        "Live preview idle", "Live preview pending", "Live preview applied",
        "Restoring original colors", "Original colors restored", "Palette in use",
        "Live preview unavailable or fighter changed", "Live preview unavailable",
        "Could not open the requested asset file.",
        "Native PAL files must contain a header and 40 BGR colors (121 bytes).",
        "Invalid or truncated palette file.", "Palette belongs to another character.",
        "Couldn't save palette; existing file was retained.",
        "Open the palette editor in an offline Practice match.",
        "The palette file chooser could not open.",
        "Keep editing or discard this draft?", "KEEP EDITING",
        "Use 3 or 6 hexadecimal digits.",
        "Choose a .json or .pal palette file.",
        "Palette changed on disk; open it again before saving.",
        "Open palette", "Save palette as", "Palette files (*.json;*.pal)",
        "Palette document (*.json)", "Native palette (*.pal)",
        "The palette file dialog could not open.", "Select a JSON or PAL palette file.",
        "Palette belongs to a different character.",
        "Save a palette file before replacing it.", "No file selected.",
        "RESET", "Could not read palette file.",
        "Saved colors to %s. PAL files do not store ramps or pins.",
        "Choose a ramp to recolor its shades together, or edit one shade.",
        "Switch between the visual picker, RGB sliders and HSV sliders.",
        "Enter a three or six digit hex color for the selected ramp or shade.",
        "Confirm to adjust saturation and brightness with the arrows. Back finishes editing.",
        "Confirm to adjust hue with the arrows. Back finishes editing.",
        "Adjust the blue channel or brightness of the selected ramp or shade.",
        "Pin this shade to protect it from ramp edits. Unpin it to follow the ramp color.",
        "Reset the selected ramp or shade to its original stock colors.",
        "Undo the last edit. A continuous drag or held adjustment is one edit.",
        "Choose an original stock palette. Replacing an unsaved draft requires confirmation.",
        "Choose an animation using the classification tabs. Preview never advances the match.",
        "Pause the animation and show its previous frame.",
        "Play or pause the sprite animation using its original frame durations.",
        "Pause the animation and show its next frame.",
        "Change sprite magnification. The mouse wheel also zooms over the preview.",
        "Pan with the arrows or right-drag the sprite. Confirm or Back finishes panning.",
        "Center the sprite and fit it inside the preview.",
        "Compare original and edited colors, or show both side by side.",
        "Highlight pixels using the selected shade in the sprite preview.",
        "Temporarily preview edits on the fighter. Apply keeps them when the menu closes.",
        "Keep these colors in the current Practice match. This does not save a file.",
        "Stop the temporary preview and restore the last applied colors. Keep the editable draft.",
        "Choose the fighter to edit. Switching away from an unsaved draft requires confirmation.",
        "Open a palette with Windows Open. JSON includes ramps and pins; PAL contains colors only.",
        "Save to the current file. A new draft opens Windows Save As.",
        "Use Windows Save As to choose another name or location. JSON keeps ramps and pins.",
        "Switch between the character's normal and alternate sprite animations.",
        "Click a sprite pixel to select its ramp or shade. Wheel zooms; right-drag pans.",
        "Adjust hue for the selected ramp or shade.",
        "Adjust the red channel for the selected ramp or shade.",
        "Adjust saturation for the selected ramp or shade.",
        "Adjust the green channel for the selected ramp or shade.",
        "Stock palette",
    };
    I18n::SetLanguage(I18n::Japanese);
    for (const char* en : required) {
        const char* ja = I18n::Tr(en);
        if (ja == en) std::fprintf(stderr, "Missing palette translation: %s\n", en);
        CHECK(ja != en);
        CHECK(HasNonAscii(ja) || (std::strcmp(en, "super") == 0 && std::strcmp(ja, "ES") == 0));
    }
    CHECK(I18n::TrFormat("Stock %d loaded.", 3) == "\xE6\xA8\x99\xE6\xBA\x96\xE3\x82\xAB\xE3\x83\xA9\xE3\x83\xBC\x33\xE3\x82\x92\xE8\xAA\xAD\xE3\x81\xBF\xE8\xBE\xBC\xE3\x81\xBF\xE3\x81\xBE\xE3\x81\x97\xE3\x81\x9F\xE3\x80\x82");
    CHECK(I18n::TrFormat("Picked ramp %d from shade %d.", 2, 7) == "\xE3\x82\xB0\xE3\x83\xAB\xE3\x83\xBC\xE3\x83\x97\x32\xE3\x82\x92\xE9\x81\xB8\xE6\x8A\x9E\xE3\x81\x97\xE3\x81\xBE\xE3\x81\x97\xE3\x81\x9F\xEF\xBC\x88\xE8\x89\xB2\x37\xEF\xBC\x89\xE3\x80\x82");
    I18n::SetLanguage(I18n::English);
    CHECK(I18n::TrFormat("Stock %d loaded.", 3) == "Stock 3 loaded.");
}

void TestRequiredStringsTranslated() {
    const char* required[] = {
        "LANGUAGE", "JAPANESE", "OPEN MENU", "USE ESC TO OPEN MENU", "Hotkeys",
        "TYPE VK CODE   APPLY   ESC CANCEL", "SAVE", "LOAD", "APPLY", "CANCEL",
        "Backdash", "Block", "Mishio: ", "Rumi: ", "Ikumi: ", "Mai: ", "Michiru: ",
        "MOVE %s  COMBO %d  MAX %d", "P%d %s  Aw: %d", "P%d %s  Kimchi: %s (%d)",
        "P%d Blood Value %d  Lvl %d  Genocide Timer %d", " [NoCD]", " [Anim %d:%d]",
        "%s IC LOCKED - MANAGED BY %s", "RF Freeze", "Gap: %d", "Macro: Slot %d", "Macro %s | Slot %d",
        "2 KEYS", "Sets the menu key back to Esc.",
        "Type a raw virtual-key code, then apply it or return without changing it.",
    };
    I18n::SetLanguage(I18n::Japanese);
    for (const char* s : required) {
        const char* ja = I18n::Tr(s);
        CHECK(ja != s);
        CHECK(HasNonAscii(ja) || std::strcmp(s, ja) != 0);
    }
    I18n::SetLanguage(I18n::English);
}

}

int main() {
    TestTableIntegrity();
    TestLanguageSwitch();
    TestFormattedStrings();
    TestRequiredStringsTranslated();
    TestPaletteStringsTranslated();
    TestPaletteMoveLabelsTranslated();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("i18n_tests: all checks passed\n");
    return 0;
}
