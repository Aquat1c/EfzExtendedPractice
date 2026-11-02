#include "utils/efz_revival_profile.h"
#include <iostream>

namespace {

int g_failures = 0;

void Check(bool condition, const char* label) {
    if (condition) return;
    std::cerr << "FAILED: " << label << '\n';
    ++g_failures;
}

}

int main() {
    const auto* original = FindEfzRevival102jProfileByPeIdentity(
        0x6A36A6AEu, 0x001D9000u);
    const auto* updated = FindEfzRevival102jProfileByPeIdentity(
        0x6A6F0F7Bu, 0x001D8000u);

    Check(original != nullptr, "original J identity resolves");
    Check(updated != nullptr, "August J identity resolves");
    Check(original != updated, "J compile identities remain distinct");
    Check(original && original->build == EfzRevival102jBuild::Original20260620,
          "original J build id");
    Check(updated
              && updated->build == EfzRevival102jBuild::Updated20260802,
          "August J build id");
    Check(FindEfzRevival102jProfileByBuild(
              EfzRevival102jBuild::Original20260620) == original,
          "original build id resolves to its profile");
    Check(FindEfzRevival102jProfileByBuild(
              EfzRevival102jBuild::Updated20260802) == updated,
          "August build id resolves to its profile");

    Check(FindEfzRevival102jProfileByPeIdentity(
              0x6A6F0F7Bu, 0x001D9001u) == nullptr,
          "wrong image size fails closed");
    Check(FindEfzRevival102jProfileByPeIdentity(
              0x6A6F0F7Au, 0x001D8000u) == nullptr,
          "wrong timestamp fails closed");
    Check(FindEfzRevival102jProfileByBuild(EfzRevival102jBuild::Unknown) == nullptr,
          "unknown build has no profile");

    if (original && updated) {
        Check(original->roleRva == updated->roleRva
                  && original->roleRva == 0x0014EC40u,
              "J role ABI is shared");
        Check(original->sessionPtrRva == updated->sessionPtrRva
                  && original->sessionPtrRva == 0x0014E980u,
              "J session pointer ABI is shared");

        Check(original->patchTogglerRva == 0x00077F40u
                  && updated->patchTogglerRva == 0x00078D00u,
              "patch toggler compile matrix");
        Check(original->patchContextRva == 0x0014E8C0u
                  && updated->patchContextRva == 0x0014E8C0u,
              "patch context stays ABI-stable");
        Check(original->togglePauseRva == 0x0007DB60u
                  && updated->togglePauseRva == 0x0007EDA0u,
              "pause toggle compile matrix");
        Check(original->practiceStepRenderRva == 0x0007D6B0u
                  && updated->practiceStepRenderRva == 0x0007E8F0u,
              "Practice step/render compile matrix");
        Check(original->practiceDispatcherRva == 0x0007CF60u
                  && updated->practiceDispatcherRva == 0x0007E1A0u,
              "Practice dispatcher compile matrix");
        Check(original->loadStateRva == 0x0007E040u
                  && updated->loadStateRva == 0x0007F280u,
              "Practice load compile matrix");
        Check(original->saveStateRva == 0x0007E0F0u
                  && updated->saveStateRva == 0x0007F330u,
              "Practice save compile matrix");
        Check(original->practiceMainTickRva == 0x0007E140u
                  && updated->practiceMainTickRva == 0x0007F380u,
              "Practice main-tick compile matrix");
        Check(original->renderContextGlobalRva == 0x0014E8D8u
                  && updated->renderContextGlobalRva == 0u,
              "removed Revival render global stays unavailable");

        const std::array<uint32_t, 4> expectedOriginalRoleVtables = {{
            0x0016FEF0u, 0x0016FF20u, 0x0016FF80u, 0x0016FEB0u,
        }};
        const std::array<uint32_t, 4> expectedUpdatedRoleVtables = {{
            0x0016FBD0u, 0x0016FC00u, 0x0016FC60u, 0x0016FB90u,
        }};
        Check(original->roleVtableRvas == expectedOriginalRoleVtables,
              "original role-vtable matrix");
        Check(updated->roleVtableRvas == expectedUpdatedRoleVtables,
              "August role-vtable matrix");

        const std::array<uint32_t, 10> expectedOriginalPracticeVtable = {{
            0x00080650u, 0x00080630u, 0x0007DE40u, 0x0007E140u,
            0x0007CF60u, 0x0007DBC0u, 0x00064680u, 0x0007CCC0u,
            0x0007CDA0u, 0x0007D780u,
        }};
        const std::array<uint32_t, 10> expectedUpdatedPracticeVtable = {{
            0x00081890u, 0x00081870u, 0x0007F080u, 0x0007F380u,
            0x0007E1A0u, 0x0007EE00u, 0x00065580u, 0x0007DCD0u,
            0x0007DEB0u, 0x0007E9C0u,
        }};
        Check(original->practiceVtableSlots == expectedOriginalPracticeVtable,
              "original Practice vtable matrix");
        Check(updated->practiceVtableSlots == expectedUpdatedPracticeVtable,
              "August Practice vtable matrix");

        Check(original->roleVtableRvas[2] == original->practiceVtableRva,
              "original Practice role/vtable link");
        Check(updated->roleVtableRvas[2] == updated->practiceVtableRva,
              "August Practice role/vtable link");
        Check(original->practiceVtableSlots[3] == original->practiceMainTickRva
                  && original->practiceVtableSlots[4]
                      == original->practiceDispatcherRva,
              "original Practice vtable links");
        Check(updated->practiceVtableSlots[3]
                      == updated->practiceMainTickRva
                  && updated->practiceVtableSlots[4]
                      == updated->practiceDispatcherRva,
              "August Practice vtable links");

    }

    if (g_failures != 0) {
        std::cerr << g_failures << " Revival profile check(s) failed\n";
        return 1;
    }
    std::cout << "Revival compile profiles: PASS\n";
    return 0;
}
