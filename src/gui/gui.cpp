#include "../include/gui/gui.h"
#include "../include/utils/utilities.h"

#include "../include/core/logger.h"
#include "../include/utils/config.h"
#include "../include/gui/imgui_impl.h"
#include "../include/gui/overlay.h"
#include "../include/game/game_state.h"
#include "../include/utils/update_check.h"
#include "../include/game/mission/mission_engine.h"
#include "../include/game/mission/mission_pause_menu.h"

void OpenMenu() {
    UpdateWindowActiveState();
    if (!g_efzWindowActive.load()) {
        LogOut("[GUI] EFZ window not active, cannot open menu", true);
        return;
    }

    if (Mission::Engine::Recorder::IsMenuInputHandoffActive() &&
        !Mission::PauseMenu::IsOpen()) {
        LogOut("[GUI] Recording menu handoff active; menu press deferred", true);
        return;
    }

    if (Mission::Engine::Recorder::IsSessionActive() ||
        Mission::Engine::Runner::IsActive()) {
        Mission::PauseMenu::Toggle();
        return;
    }

    (void)OpenPracticeMenuDirect();
}

bool OpenPracticeMenuDirect(bool allowOutOfContext) {
    UpdateWindowActiveState();
    if (!g_efzWindowActive.load()) {
        LogOut("[GUI] EFZ window not active, cannot open Practice menu", true);
        return false;
    }

    // MUST stay above the context gate so an open menu is never refused
    // (the user would be trapped after the game leaves a gameplay screen).
    if (menuOpen) {
        LogOut("[GUI] Menu already open", detailedLogging.load());
        return ImGuiImpl::IsVisible();
    }

    if (!allowOutOfContext && !IsTrainingMenuContext()) {
        static DWORD s_lastBlockLog = 0;
        const DWORD now = GetTickCount();
        if (now - s_lastBlockLog > 1000) {
            s_lastBlockLog = now;
            LogOut("[GUI] Menu open refused: not in a gameplay context", true);
        }
        return false;
    }

    LogOut("[GUI] Opening config menu", detailedLogging.load());

    UpdateCheck::Start();

    const bool d3d9Ready = DirectDrawHook::SetD3D9Active(true);
    const bool useFallbackWindow = !d3d9Ready || DirectDrawHook::ShouldUseExternalMenuFallback();
    if (!useFallbackWindow && ImGuiImpl::IsExternalFallbackHost()) {
        // The fallback host holds the only ImGui context; the game window can
        // initialize again only after that thread has shut it down.
        if (!ImGuiImpl::ReleaseExternalFallbackHost(2000)) {
            LogOut("[GUI] In-game render is back but the fallback window did not release ImGui", true);
            return false;
        }
        LogOut("[GUI] In-game render is back; fallback window released", true);
    }
    if (useFallbackWindow) {
        LogOut("[GUI] In-game render unavailable; opening fallback ImGui window", true);
        if (!ImGuiImpl::ShowFallbackWindow()) {
            LogOut("[GUI] Failed to open fallback ImGui window", true);
            menuOpen = false;
            return false;
        }
        menuOpen = true;
        return true;
    }

    LogOut("[GUI] Using in-game ImGui visibility toggle", detailedLogging.load());
    ImGuiImpl::ToggleVisibility();
    menuOpen = ImGuiImpl::IsVisible();
    return menuOpen.load();
}
