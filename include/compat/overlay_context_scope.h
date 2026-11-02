#pragma once

#include "imgui.h"

// Backend functions use ImGui's current context; explicitly select this
// backend's context, never one inferred from another overlay or the caller.
class EfzScopedImGuiContext {
public:
    explicit EfzScopedImGuiContext(ImGuiContext* context)
        : previous_(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(context); }
    ~EfzScopedImGuiContext() { ImGui::SetCurrentContext(previous_); }
    EfzScopedImGuiContext(const EfzScopedImGuiContext&) = delete;
    EfzScopedImGuiContext& operator=(const EfzScopedImGuiContext&) = delete;
    void ForgetDestroyed(ImGuiContext* context) {
        if (previous_ == context) previous_ = nullptr;
    }
private:
    ImGuiContext* previous_;
};
