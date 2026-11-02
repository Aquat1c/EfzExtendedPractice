#include "../include/utils/version_compare.h"

#include <cctype>
#include <cstdlib>

namespace VersionCompare {
namespace {

// Must stay at the int limit, not a smaller cap, or long digit runs compare equal.
constexpr long long kMaxComponent = 2147483647LL;

void SplitSuffix(const std::string& suffix, std::string* outTag, long* outNumber) {
    std::string tag;
    std::string digits;
    for (char c : suffix) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalpha(uc)) {
            if (!digits.empty()) {
                break;
            }
            tag.push_back(static_cast<char>(std::tolower(uc)));
        } else if (std::isdigit(uc)) {
            digits.push_back(c);
        }
    }
    *outTag = tag;
    *outNumber = digits.empty() ? 0L : std::strtol(digits.c_str(), nullptr, 10);
}

}

bool Parse(const std::string& text, ParsedVersion* out) {
    if (out == nullptr) {
        return false;
    }
    *out = ParsedVersion{};

    size_t pos = 0;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    if (pos < text.size() && (text[pos] == 'v' || text[pos] == 'V')
        && pos + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[pos + 1]))) {
        ++pos;
    }
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
        return false;
    }

    while (out->partCount < 4 && pos < text.size()
           && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        long long value = 0;
        bool saturated = false;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            if (!saturated) {
                value = value * 10 + (text[pos] - '0');
                if (value > kMaxComponent) {
                    value = kMaxComponent;
                    saturated = true;
                }
            }
            ++pos;
        }
        out->parts[out->partCount++] = static_cast<int>(value);
        if (pos < text.size() && text[pos] == '.'
            && pos + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[pos + 1]))) {
            ++pos;
            continue;
        }
        break;
    }

    std::string suffix = text.substr(pos);
    while (!suffix.empty() && std::isspace(static_cast<unsigned char>(suffix.back()))) {
        suffix.pop_back();
    }
    out->suffix = suffix;
    return true;
}

int Compare(const std::string& a, const std::string& b) {
    ParsedVersion pa;
    ParsedVersion pb;
    const bool okA = Parse(a, &pa);
    const bool okB = Parse(b, &pb);
    if (!okA || !okB) {
        return (okA ? 1 : 0) - (okB ? 1 : 0);
    }
    for (int i = 0; i < 4; ++i) {
        if (pa.parts[i] != pb.parts[i]) {
            return pa.parts[i] < pb.parts[i] ? -1 : 1;
        }
    }
    const bool preA = !pa.suffix.empty();
    const bool preB = !pb.suffix.empty();
    if (preA != preB) {
        return preA ? -1 : 1;
    }
    if (!preA) {
        return 0;
    }
    std::string tagA;
    std::string tagB;
    long numA = 0;
    long numB = 0;
    SplitSuffix(pa.suffix, &tagA, &numA);
    SplitSuffix(pb.suffix, &tagB, &numB);
    if (tagA != tagB) {
        return tagA < tagB ? -1 : 1;
    }
    if (numA != numB) {
        return numA < numB ? -1 : 1;
    }
    return 0;
}

bool IsNewer(const std::string& candidate, const std::string& current) {
    ParsedVersion parsedCandidate;
    if (!Parse(candidate, &parsedCandidate)) {
        return false;
    }
    // Both sides must parse, else an unparsable current makes every candidate "newer".
    ParsedVersion parsedCurrent;
    if (!Parse(current, &parsedCurrent)) {
        return false;
    }
    return Compare(candidate, current) > 0;
}

}
