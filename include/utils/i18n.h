#pragma once

#include <string>

namespace I18n {

enum Language : int {
    English = 0,
    Japanese = 1,
};

void SetLanguage(Language language);
Language GetLanguage();

// Returns the translation of `en` for the active language, or `en` itself
// (same pointer) for English or a miss. The table is static and read-only, so
// any thread may call this.
const char* Tr(const char* en);

// snprintf of Tr(enFormat); translations keep the same specifiers in order.
std::string TrFormat(const char* enFormat, ...);

// Inclusive, zero-terminated ImWchar pairs covered by the embedded Japanese font.
const unsigned short* JapaneseGlyphRanges();

} // namespace I18n
