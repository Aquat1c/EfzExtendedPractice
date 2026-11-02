#include "utils/update_check_policy.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "update_check_policy_tests: " << message << '\n';
        ++g_failures;
    }
}

using UpdateCheck::Policy::Availability;
using UpdateCheck::Policy::Decide;
using UpdateCheck::Policy::ExtractTagFromReleaseJson;
using UpdateCheck::Policy::IsSafeTag;
using UpdateCheck::Policy::NormalizeTag;

std::string ReleaseJson(const std::string& tag, const std::string& body = "Release notes.") {
    return std::string("{\"url\":\"https://api.github.com/repos/a/b/releases/1\","
                       "\"id\":1,\"draft\":false,\"prerelease\":false,"
                       "\"tag_name\":\"") + tag + "\","
           "\"name\":\"Release\",\"body\":\"" + body + "\","
           "\"assets\":[{\"name\":\"efz_training_mode.dll\",\"size\":4015616}]}";
}

void TestIsSafeTag() {
    Require(IsSafeTag("1.2.2"), "plain version must be safe");
    Require(IsSafeTag("1.2.0_beta6"), "underscore pre-release must be safe");
    Require(IsSafeTag("v1.0"), "leading v must be safe");
    Require(IsSafeTag("0.8.2-release"), "hyphen must be safe");
    Require(!IsSafeTag(""), "empty must be rejected");
    Require(!IsSafeTag(std::string(33, '1')), "over-long must be rejected");
    Require(IsSafeTag(std::string(32, '1')), "exactly 32 must be accepted");
    Require(!IsSafeTag("1.2.2 <script>"), "spaces and markup must be rejected");
    Require(!IsSafeTag("1.2.2\n"), "control characters must be rejected");
    Require(!IsSafeTag("1.2.2%s%s"), "format specifiers must be rejected");
    Require(!IsSafeTag("../../etc"), "path characters must be rejected");
}

void TestNormalizeTag() {
    Require(NormalizeTag("v1.2.2") == "1.2.2", "v prefix must be stripped");
    Require(NormalizeTag("V1.2.2") == "1.2.2", "V prefix must be stripped");
    Require(NormalizeTag("1.2.2") == "1.2.2", "no prefix is unchanged");
    Require(NormalizeTag("version2") == "version2", "v not followed by a digit is left alone");
    Require(NormalizeTag("v") == "v", "bare v is left alone");
    Require(NormalizeTag("") == "", "empty is left alone");
}

void TestExtractTagHappyPath() {
    Require(ExtractTagFromReleaseJson(ReleaseJson("1.2.2")) == "1.2.2", "plain tag");
    Require(ExtractTagFromReleaseJson(ReleaseJson("v1.3")) == "1.3", "tag is normalized");
    Require(ExtractTagFromReleaseJson(ReleaseJson("1.2.0_beta6")) == "1.2.0_beta6", "pre-release tag");
}

void TestExtractTagIgnoresReleaseNotesBody() {
    const std::string tricky =
        ReleaseJson("1.2.2", "Fixed the thing where \\\"tag_name\\\": \\\"99.9.9\\\" was wrong.");
    const std::string got = ExtractTagFromReleaseJson(tricky);
    Require(got == "1.2.2",
            "tag_name inside the release body must not win, got '" + got + "'");
}

void TestExtractTagRejectsMalformed() {
    Require(ExtractTagFromReleaseJson("") == "", "empty body");
    Require(ExtractTagFromReleaseJson("not json at all") == "", "garbage body");
    Require(ExtractTagFromReleaseJson("{\"tag_name\":") == "", "truncated json");
    Require(ExtractTagFromReleaseJson("[1,2,3]") == "", "array is not an object");
    Require(ExtractTagFromReleaseJson("\"just a string\"") == "", "bare string is not an object");
    Require(ExtractTagFromReleaseJson("{\"message\":\"API rate limit exceeded\"}") == "",
            "a 403 error document has no tag_name");
    Require(ExtractTagFromReleaseJson("{\"tag_name\":null}") == "", "null tag_name");
    Require(ExtractTagFromReleaseJson("{\"tag_name\":123}") == "", "numeric tag_name");
    Require(ExtractTagFromReleaseJson("{\"tag_name\":{\"a\":1}}") == "", "object tag_name");
    Require(ExtractTagFromReleaseJson(ReleaseJson("")) == "", "empty tag_name");
    Require(ExtractTagFromReleaseJson(ReleaseJson("not a version")) == "",
            "an unsafe tag is rejected at extraction");
    Require(ExtractTagFromReleaseJson(ReleaseJson(std::string(40, '9'))) == "",
            "an over-long tag is rejected at extraction");
}

void TestDecideBadge() {
    Availability a = Decide("1.2.2", "1.2.0", "");
    Require(a.hasNewerRelease && a.updateAvailable, "newer + unacknowledged must badge");

    a = Decide("1.2.2", "1.2.0", "1.2.2");
    Require(a.hasNewerRelease && !a.updateAvailable,
            "acknowledging the current latest must hide the badge but keep hasNewerRelease");

    a = Decide("1.2.3", "1.2.0", "1.2.2");
    Require(a.hasNewerRelease && a.updateAvailable,
            "a tag newer than the acknowledged one must re-raise the badge");

    a = Decide("1.2.2", "1.2.2", "");
    Require(!a.hasNewerRelease && !a.updateAvailable, "equal versions must not badge");

    a = Decide("1.2.2", "1.3.0", "");
    Require(!a.hasNewerRelease && !a.updateAvailable, "a newer local build must not badge");

    a = Decide("", "1.2.2", "");
    Require(!a.hasNewerRelease && !a.updateAvailable, "an empty latest must not badge");

    a = Decide("1.2.2", "not-a-version", "");
    Require(!a.hasNewerRelease && !a.updateAvailable,
            "an unparsable current version must never badge");

    a = Decide("2.0.0", "1.2.2", "1.2.3");
    Require(a.updateAvailable, "a stale acknowledgement must not suppress a newer release");
}

void TestRealWorldScenarios() {
    Availability a = Decide("1.2", "1.2.0_beta6", "");
    Require(a.updateAvailable,
            "a user on the 1.2 release build (version.h 1.2.0_beta6) is told 1.2 is newer");

    a = Decide("1.2", "1.2", "");
    Require(!a.updateAvailable, "aligned version.h and tag must not nag");

    a = Decide("1.2.2", "1.2.2", "");
    Require(!a.updateAvailable, "the current shipping build must not nag");
}

}

int main() {
    TestIsSafeTag();
    TestNormalizeTag();
    TestExtractTagHappyPath();
    TestExtractTagIgnoresReleaseNotesBody();
    TestExtractTagRejectsMalformed();
    TestDecideBadge();
    TestRealWorldScenarios();

    if (g_failures != 0) {
        std::cerr << "update_check_policy_tests: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "update_check_policy_tests passed\n";
    return 0;
}
