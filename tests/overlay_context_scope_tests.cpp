#include <cstdio>

#if __has_include("compat/overlay_context_scope.h")
#include "compat/overlay_context_scope.h"

int main() {
    int failures = 0;
    const auto check = [&](bool value, const char* label) {
        if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
    };
    ImGuiContext* caller = ImGui::CreateContext();
    ImGuiContext* owner = ImGui::CreateContext();
    ImGui::SetCurrentContext(caller);
    {
        EfzScopedImGuiContext scope(owner);
        check(ImGui::GetCurrentContext() == owner, "backend work selects the actual owner");
    }
    check(ImGui::GetCurrentContext() == caller, "callback restores another current context");
    ImGui::SetCurrentContext(nullptr);
    {
        EfzScopedImGuiContext scope(owner);
    }
    check(ImGui::GetCurrentContext() == nullptr, "callback preserves null current context");
    ImGui::SetCurrentContext(owner);
    {
        EfzScopedImGuiContext scope(owner);
        scope.ForgetDestroyed(owner);
        ImGui::DestroyContext(owner);
    }
    check(ImGui::GetCurrentContext() == nullptr, "teardown never restores a destroyed context");
    ImGui::DestroyContext(caller);
    std::printf("overlay_context_scope: 4 checks, %d failures\n", failures);
    return failures ? 1 : 0;
}
#else
int main() {
    std::fputs("FAIL: owned ImGui context scope is not implemented\n", stderr);
    return 1;
}
#endif
