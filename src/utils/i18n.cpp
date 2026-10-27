#include "../../include/utils/i18n.h"
#include "../../include/utils/i18n_ja.generated.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace I18n {

namespace {
std::atomic<int> g_language{English};
}

void SetLanguage(Language language) {
    g_language.store(language == Japanese ? Japanese : English, std::memory_order_relaxed);
}

Language GetLanguage() {
    return static_cast<Language>(g_language.load(std::memory_order_relaxed));
}

const char* Tr(const char* en) {
    if (!en || !*en || GetLanguage() != Japanese) return en;
    unsigned lo = 0;
    unsigned hi = Generated::kJaTableCount;
    while (lo < hi) {
        const unsigned mid = lo + (hi - lo) / 2;
        const int cmp = std::strcmp(Generated::kJaTable[mid].en, en);
        if (cmp == 0) return Generated::kJaTable[mid].ja;
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    return en;
}

std::string TrFormat(const char* enFormat, ...) {
    const char* fmt = Tr(enFormat);
    if (!fmt) return std::string();
    va_list args;
    va_start(args, enFormat);
    va_list sizing;
    va_copy(sizing, args);
    const int len = std::vsnprintf(nullptr, 0, fmt, sizing);
    va_end(sizing);
    std::string out;
    if (len > 0) {
        out.resize(static_cast<size_t>(len) + 1);
        std::vsnprintf(&out[0], out.size(), fmt, args);
        out.resize(static_cast<size_t>(len));
    }
    va_end(args);
    return out;
}

const unsigned short* JapaneseGlyphRanges() {
    return Generated::kJaGlyphRanges;
}

} // namespace I18n
