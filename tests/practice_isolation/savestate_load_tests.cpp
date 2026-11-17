#include <windows.h>
#include <atomic>
#include <string>
#include <cstdio>

namespace {
int failures, restored, notified, nativeCalls, completed, overlayResets, enforced, messages;
bool nativeResult, ownedPause, completedOwnedPause, hooksOpen = true, admitted = true;
std::atomic<bool> s_installed{true};
std::atomic<unsigned> s_loadCount{0};
bool s_loadStartedFromOwnedPause;
int displayData;
void* controller = reinterpret_cast<void*>(1);
void Check(bool ok, const char* name) { if (!ok) { ++failures; std::printf("FAIL %s\n", name); } }
void LogOut(const std::string&, bool) {}
void RestoreModState() { ++restored; }
void BeginTrackedLoad() { s_loadStartedFromOwnedPause = ownedPause; }
uintptr_t GetEFZBase() { return 1; }
bool PracticeBattleHooksOpen() { return hooksOpen; }
bool NativeLoad(void*) { ++nativeCalls; return nativeResult; }
using LoadStateFn = bool (*)(void*);
LoadStateFn oLoadState = NativeLoad;
}
namespace Framestep {
void FinishSavestateRestore(bool owned) { ++completed; completedOwnedPause = owned; }
}
namespace ComboOverlay { void ResetForStateLoad() { ++overlayResets; } }
namespace CharacterSettings { void TickCharacterEnforcements(uintptr_t, int) { ++enforced; } }
namespace Mission {
namespace Engine {
void NotifyStateLoaded() { ++notified; }
namespace Runner { bool IsActive() { return false; } }
namespace Recorder { bool IsSessionActive() { return false; } }
namespace Demo { bool IsActive() { return false; } }
}
namespace TutorialSession { bool IsActive() { return false; } }
}
namespace DirectDrawHook {
void AddMessage(const char*, const char*, unsigned long, int, int, int) { ++messages; }
}
namespace PauseIntegration {
void* GetPracticeControllerPtr() { return controller; }
void* ResolvePracticeControllerPtrNow(const char*) { return controller; }
}
namespace MinHookUtils {
struct Execution { operator bool() const { return true; } bool Admitted() const { return admitted; } };
template<auto Fn> int TicketFor() { return 0; }
Execution EnterExecution(int) { return {}; }
}
#include "game/savestate_load.inl"

int main() {
    // Revival's load leaves AL as loop scratch, so its return value must never decide success.
    for (bool native : {false, true}) {
        nativeResult = native;
        ownedPause = native;
        Check(SavestateHook::TriggerLoad(), "a native call is a completed load");
        Check(completedOwnedPause == native && !s_loadStartedFromOwnedPause, "pause ownership is released");
    }
    Check(nativeCalls == 2 && completed == 2, "each load runs once");
    Check(s_loadCount == 2 && restored == 2 && notified == 2 && overlayResets == 2 && enforced == 2 && messages == 2,
          "restore effects run whatever the native return value");
    hooksOpen = false;
    Check(SavestateHook::TriggerLoad() && nativeCalls == 3 && completed == 2 && s_loadCount == 2,
          "non-Practice load only invokes native operation");
    hooksOpen = true;
    admitted = false;
    Check(SavestateHook::TriggerLoad() && nativeCalls == 4 && completed == 2,
          "closed admission only invokes native operation");
    admitted = true;
    controller = nullptr;
    Check(!SavestateHook::TriggerLoad() && nativeCalls == 4, "missing controller fails without native call");
    controller = reinterpret_cast<void*>(1);
    s_installed = false;
    Check(!SavestateHook::TriggerLoad() && nativeCalls == 4, "uninstalled load fails without native call");
    std::printf("savestate load: %d failures\n", failures);
    return failures ? 1 : 0;
}
