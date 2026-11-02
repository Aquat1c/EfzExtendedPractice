#include "utils/version_compare.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "version_compare_tests: " << message << '\n';
        ++g_failures;
    }
}

void ExpectOrder(const char* lower, const char* higher) {
    Require(VersionCompare::Compare(lower, higher) < 0,
            std::string("expected ") + lower + " < " + higher);
    Require(VersionCompare::Compare(higher, lower) > 0,
            std::string("expected ") + higher + " > " + lower);
    Require(VersionCompare::IsNewer(higher, lower),
            std::string("expected IsNewer(") + higher + ", " + lower + ")");
    Require(!VersionCompare::IsNewer(lower, higher),
            std::string("expected !IsNewer(") + lower + ", " + higher + ")");
}

void ExpectEqual(const char* a, const char* b) {
    Require(VersionCompare::Compare(a, b) == 0,
            std::string("expected ") + a + " == " + b);
    Require(!VersionCompare::IsNewer(a, b) && !VersionCompare::IsNewer(b, a),
            std::string("equal versions must not be newer: ") + a + " / " + b);
}

void TestNumericOrdering() {
    ExpectOrder("1.2.9", "1.2.10");
    ExpectOrder("1.9.0", "1.10.0");
    ExpectOrder("0.8.2", "0.8.3");
    ExpectOrder("1.2.2", "2.0.0");
    ExpectEqual("1.2", "1.2.0");
    ExpectEqual("1.2.0", "1.2.0.0");
    ExpectEqual("v1.2.2", "1.2.2");
    ExpectEqual("V1.2.2", "1.2.2");
}

void TestRealProjectVersions() {
    ExpectOrder("1.2.0_beta6", "1.2.0");
    ExpectOrder("1.2.0_beta6", "1.2");
    ExpectOrder("1.2.0_beta6", "1.2.2");
    ExpectOrder("1.0.8", "1.1");
    ExpectOrder("1.0.0", "1.0.8");
    ExpectEqual("1.2.2", "1.2.2");
    Require(!VersionCompare::IsNewer("1.2", "1.2.2"),
            "a coarser older tag must not look newer than a finer current build");
}

void TestPreReleaseOrdering() {
    ExpectOrder("1.2.0_beta6", "1.2.0_beta10");
    ExpectOrder("1.2.0_alpha1", "1.2.0_beta1");
    ExpectOrder("1.2.0_beta2", "1.2.0_rc1");
    ExpectEqual("1.2.0_beta1", "1.2.0-beta1");
    ExpectEqual("1.2.0_beta", "1.2.0_beta0");
}

void TestUnparsableCurrentNeverAdvertisesAnUpdate() {
    Require(!VersionCompare::IsNewer("0.0.1", "beta7"),
            "a downgrade must not be newer than an unparsable current version");
    Require(!VersionCompare::IsNewer("1.0", "EFZ 1.2.3"),
            "an unparsable current version must never advertise an update");
    Require(!VersionCompare::IsNewer("1.2.3", ""),
            "an empty current version must never advertise an update");
    Require(!VersionCompare::IsNewer("", "1.2.3"),
            "an empty candidate is never newer");
    Require(!VersionCompare::IsNewer("not-a-version", "1.2.3"),
            "an unparsable candidate is never newer");
}

void TestLargeComponentsStillOrder() {
    ExpectOrder("9999999.0", "10000000.0");
    ExpectOrder("1500000.0", "2000000.0");
    ExpectOrder("20260101", "20260907");
}

void TestDocumentedConstraints() {
    // "0.8.2-release" is a real repo tag: any trailing text sorts below the plain version. Do not tag like this.
    Require(VersionCompare::Compare("0.8.2-release", "0.8.2") < 0,
            "trailing words are treated as a pre-release (documented constraint)");
    Require(VersionCompare::Compare("1.2.0+build5", "1.2.0") < 0,
            "build metadata is treated as a pre-release (documented constraint)");
    Require(VersionCompare::Compare("garbage", "1.0") < 0, "garbage ranks below parsable");
    Require(VersionCompare::Compare("garbage", "nonsense") == 0, "two unparsable strings are equal");
}

void TestParse() {
    VersionCompare::ParsedVersion v;
    Require(VersionCompare::Parse("1.2.0_beta6", &v), "should parse");
    Require(v.parts[0] == 1 && v.parts[1] == 2 && v.parts[2] == 0, "components wrong");
    Require(v.partCount == 3, "partCount wrong");
    Require(v.suffix == "_beta6", "suffix wrong");
    Require(!VersionCompare::Parse("", &v), "empty must not parse");
    Require(!VersionCompare::Parse("beta", &v), "leading letters must not parse");
    Require(!VersionCompare::Parse(nullptr ? "" : "v", &v), "bare v must not parse");
}

}

int main() {
    TestParse();
    TestNumericOrdering();
    TestRealProjectVersions();
    TestPreReleaseOrdering();
    TestUnparsableCurrentNeverAdvertisesAnUpdate();
    TestLargeComponentsStillOrder();
    TestDocumentedConstraints();

    if (g_failures != 0) {
        std::cerr << "version_compare_tests: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "version_compare_tests passed\n";
    return 0;
}
