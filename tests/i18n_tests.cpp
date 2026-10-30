#include "../include/utils/i18n.h"
#include "../include/utils/i18n_ja.generated.h"

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

// Conversion specifiers in order, ignoring flags/width/precision, "%%" excluded.
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

// Decodes one UTF-8 code point at *p and advances it. Returns 0 on malformed input.
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
        // Plain text containing '%' that is never used as a format (see NOT_A_FORMAT in tools/gen_i18n_ja.py).
        if (std::strcmp(en, "100% leaves the game's own volume untouched.") != 0) {
            CHECK(ConversionSequence(en) == ConversionSequence(ja));
        }
        // Plain-text labels are drawn unformatted; a '%' in the translation would look like a specifier to any later printf.
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

void TestRequiredStringsTranslated() {
    // Labels a player sees on the default screens; each must have a Japanese entry.
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

} // namespace

int main() {
    TestTableIntegrity();
    TestLanguageSwitch();
    TestFormattedStrings();
    TestRequiredStringsTranslated();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("i18n_tests: all checks passed\n");
    return 0;
}
