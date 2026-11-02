#pragma once

#include <cctype>
#include <string>

#include "version_compare.h"

#include "nlohmann/json.hpp"

// Pure decision logic with no globals, I/O, clock or Win32, so tests can pin it.
namespace UpdateCheck {
namespace Policy {

// Tags are attacker-influenced text that reaches a UI buffer; anything longer
// or outside this charset is rejected rather than rendered into the menu.
constexpr size_t kMaxTagLength = 32;

inline bool IsSafeTag(const std::string& tag) {
    if (tag.empty() || tag.size() > kMaxTagLength) return false;
    for (char c : tag) {
        const auto uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '.' || c == '_' || c == '-')) return false;
    }
    return true;
}

inline std::string NormalizeTag(std::string tag) {
    if (tag.size() >= 2 && (tag[0] == 'v' || tag[0] == 'V')
        && std::isdigit(static_cast<unsigned char>(tag[1]))) {
        tag.erase(0, 1);
    }
    return tag;
}

// Returns "" for anything malformed or unsafe. A real JSON parse: the release object embeds the
// changelog in "body", so a substring search for "tag_name" could match release-note text.
inline std::string ExtractTagFromReleaseJson(const std::string& body) {
    auto parsed = nlohmann::json::parse(body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return {};
    auto it = parsed.find("tag_name");
    if (it == parsed.end() || !it->is_string()) return {};
    const std::string tag = NormalizeTag(it->get<std::string>());
    return IsSafeTag(tag) ? tag : std::string{};
}

struct Availability {
    bool hasNewerRelease = false;
    bool updateAvailable = false;
};

inline Availability Decide(const std::string& latest,
                           const std::string& current,
                           const std::string& acknowledged) {
    Availability out;
    if (latest.empty()) return out;
    out.hasNewerRelease = VersionCompare::IsNewer(latest, current);
    out.updateAvailable = out.hasNewerRelease && latest != acknowledged;
    return out;
}

}
}
