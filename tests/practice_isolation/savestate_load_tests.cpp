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
    for (bool owned : {false, true}) {
        ownedPause = owned;
        nativeResult = false;
        Check(!SavestateHook::TriggerLoad(), "native failure propagates");
        Check(nativeCalls == completed && completedOwnedPause == owned, "failure releases original pause ownership");
        Check(!s_loadStartedFromOwnedPause, "failure clears tracked pause ownership");
        Check(!s_loadCount && !restored && !notified && !overlayResets && !enforced && !messages,
              "failure has no successful restore effects");
    }
    nativeResult = true;
    ownedPause = true;
    Check(SavestateHook::TriggerLoad(), "native success propagates");
    Check(nativeCalls == 3 && completed == 3 && completedOwnedPause, "successful pause cleanup runs once");
    Check(s_loadCount == 1 && restored == 1 && notified == 1 && overlayResets == 1 && enforced == 1 && messages == 1,
          "successful restore effects run once");
    hooksOpen = false;
    Check(SavestateHook::TriggerLoad() && nativeCalls == 4 && completed == 3 && s_loadCount == 1,
          "non-Practice load only invokes native operation");
    hooksOpen = true;
    admitted = false;
    nativeResult = false;
    Check(!SavestateHook::TriggerLoad() && nativeCalls == 5 && completed == 3,
          "closed admission preserves native failure without mod effects");
    admitted = true;
    controller = nullptr;
    Check(!SavestateHook::TriggerLoad() && nativeCalls == 5, "missing controller fails without native call");
    controller = reinterpret_cast<void*>(1);
    s_installed = false;
    Check(!SavestateHook::TriggerLoad() && nativeCalls == 5, "uninstalled load fails without native call");
    std::printf("savestate load: %d failures\n", failures);
    return failures ? 1 : 0;
}
