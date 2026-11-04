namespace {
    void FinishTrackedLoad(bool result) {
        if (result) {
            s_loadCount.fetch_add(1, std::memory_order_relaxed);
            RestoreModState();
        }
        Framestep::FinishSavestateRestore(s_loadStartedFromOwnedPause);
        s_loadStartedFromOwnedPause = false;
        LogOut("[SAVESTATE][REVIVAL] === LOAD STATE END (result=" + std::string(result ? "true" : "false") + ") ===", true);
        if (!result) return;

        ComboOverlay::ResetForStateLoad();
        Mission::Engine::NotifyStateLoaded();
        if (uintptr_t base = GetEFZBase()) {
            CharacterSettings::TickCharacterEnforcements(base, displayData);
        }

        if (!::Mission::Engine::Runner::IsActive() &&
            !::Mission::TutorialSession::IsActive() &&
            !::Mission::Engine::Recorder::IsSessionActive() &&
            !::Mission::Engine::Demo::IsActive())
            DirectDrawHook::AddMessage("Revival State Loaded", "savestate", RGB(100, 255, 100), 1500, 0, 100);
    }

    bool __fastcall HookedLoadState(void* self, void*) {
        auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&HookedLoadState>());
        if (execution && !execution.Admitted()) return oLoadState ? oLoadState(self) : false;
        if (!PracticeBattleHooksOpen()) {
            return oLoadState ? oLoadState(self) : false;
        }

        BeginTrackedLoad();
        bool result = oLoadState ? oLoadState(self) : false;
        FinishTrackedLoad(result);
        return result;
    }
}

namespace SavestateHook {
    bool TriggerLoad() {
        if (!s_installed.load() || !oLoadState) return false;
        void* ptr = PauseIntegration::GetPracticeControllerPtr();
        if (!ptr) ptr = PauseIntegration::ResolvePracticeControllerPtrNow("savestate hotkey load");
        if (!ptr) {
            LogOut("[SAVESTATE][REVIVAL] TriggerLoad: Practice controller unavailable", true);
            return false;
        }
        return HookedLoadState(ptr, nullptr);
    }
}
