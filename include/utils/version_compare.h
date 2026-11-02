#pragma once

#include <string>

// Tagging rule: any non-empty tail after the dotted numbers sorts below the final release, so
// "0.8.2-release" and "1.2.0+build5" are pre-releases. Use at most 4 components and alpha/beta/rc.
namespace VersionCompare {

struct ParsedVersion {
    int parts[4] = {0, 0, 0, 0};
    int partCount = 0;
    std::string suffix;
};

bool Parse(const std::string& text, ParsedVersion* out);

// <0 when a<b, 0 when equal, >0 when a>b. A final release outranks its pre-releases; missing
// components are 0. Unparsable strings rank below everything parsable and equal each other.
int Compare(const std::string& a, const std::string& b);

bool IsNewer(const std::string& candidate, const std::string& current);

}
