#pragma once

#include <cctype>
#include <string>
#include <utility>

namespace Mission::TutorialTextPolicy {

inline void ReplaceAll(std::string& text, const char* needle,
                       const char* replacement) {
    const std::string from = needle ? needle : "";
    if (from.empty()) return;
    const std::string to = replacement ? replacement : "";
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

inline std::string NormalizeUiText(std::string text) {
    ReplaceAll(text, "\xE2\x80\x98", "'");
    ReplaceAll(text, "\xE2\x80\x99", "'");
    ReplaceAll(text, "\xE2\x80\x9C", "\"");
    ReplaceAll(text, "\xE2\x80\x9D", "\"");
    ReplaceAll(text, "\xE2\x80\x93", "-");
    ReplaceAll(text, "\xE2\x80\x94", "-");
    ReplaceAll(text, "\xE2\x80\xA6", "...");
    ReplaceAll(text, "\xC2\xA0", " ");
    return text;
}

inline std::string Trim(std::string text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

inline std::string UpperAscii(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

inline std::string HumanizeIdentifier(std::string id) {
    const size_t scope = id.find_last_of('.');
    if (scope != std::string::npos) id.erase(0, scope + 1);
    for (char& ch : id) {
        if (ch == '_' || ch == '-') ch = ' ';
    }
    return UpperAscii(Trim(std::move(id)));
}

inline bool IsSystemTerm(const std::string& text) {
    const std::string upper = UpperAscii(Trim(text));
    return upper == "IC" || upper == "BIC" || upper == "FIC" ||
           upper == "RG" || upper == "FM" || upper == "RED IC" ||
           upper == "BLUE IC";
}

inline bool IsInputToken(const std::string& token) {
    if (token.empty() || IsSystemTerm(token)) return false;
    size_t pos = 0;
    static const char* const prefixes[] = {"dj.", "jc.", "j.", "c.", "f."};
    for (const char* prefix : prefixes) {
        const std::string value(prefix);
        if (token.compare(0, value.size(), value) == 0) {
            pos = value.size();
            break;
        }
    }
    if (pos >= token.size()) return false;
    bool haveInput = false;
    for (; pos < token.size(); ++pos) {
        const char ch = token[pos];
        const bool direction = ch >= '1' && ch <= '9';
        const bool button = ch == 'A' || ch == 'B' || ch == 'C' ||
                            ch == 'D' || ch == 'S';
        if (!direction && !button) return false;
        haveInput = true;
    }
    return haveInput;
}

inline std::string NotationPartToRich(std::string part) {
    part = Trim(NormalizeUiText(std::move(part)));
    if (part.empty()) return {};

    std::string lead;
    if (part[0] == '~') {
        lead = "~ ";
        part = Trim(part.substr(1));
    } else {
        const std::string upper = UpperAscii(part);
        constexpr const char* kDelayed = "DELAYED ";
        if (upper.compare(0, 8, kDelayed) == 0) {
            lead = "delayed ";
            part = Trim(part.substr(8));
        }
    }
    if (part.empty()) return Trim(lead);
    if (IsSystemTerm(part)) return lead + "{term:" + part + "}";

    const size_t space = part.find_first_of(" \t");
    const std::string head = part.substr(0, space);
    if (!IsInputToken(head)) return lead + part;

    return lead + "{input:" + part + "}";
}

inline std::string NotationToRich(const std::string& notation) {
    std::string out;
    size_t start = 0;
    for (;;) {
        const size_t slash = notation.find('/', start);
        const std::string part = NotationPartToRich(notation.substr(
            start, slash == std::string::npos
                ? std::string::npos : slash - start));
        if (!part.empty()) {
            if (!out.empty()) out += "  /  ";
            out += part;
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return out.empty() ? std::string("?") : out;
}

}
