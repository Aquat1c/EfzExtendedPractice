#include "../../../include/game/practice_menu/practice_menu.h"
#include "../../../include/game/practice_menu/efz_title.h"
#include "../../../include/game/practice_menu/title_render.h"

#include "../../../include/core/logger.h"
#include "../../../include/core/memory.h"
#include "../../../include/utils/minhook_utils.h"
#include "../../../include/utils/utilities.h"
#include "../../../include/game/mission/mission_engine.h"
#include "../../../include/game/mission/mission_data.h"
#include "../../../include/game/mission/mission_authoring.h"
#include "../../../include/game/mission/tutorial_support.h"
#include "../../../include/game/mission/tutorial_session.h"
#include "../../../include/game/character_hotswap.h"
#include "../../../include/game/practice_menu/mission_title_screen.h"
#include "../../../include/gui/overlay.h"
#include "runtime/practice_frontend_case.h"

#include <algorithm>
#include <string>
#include <vector>

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Hooks: a 7-byte whole-instruction jmp on the Practice case (0x776352) and a MinHook trampoline
// on updateTitleScreenLogic (0x775FB0). InGameNetplay's title patches stay untouched.

using namespace PracticeMenu::EfzTitle;

namespace {

constexpr uintptr_t kVaProcessPlayerInput = 0x00406590; // processPlayerInput(inputManager)
typedef void (__thiscall* ProcessInputFn)(void* inputManager);

enum Phase { PHASE_INACTIVE = 0, PHASE_ENTERING, PHASE_ACTIVE, PHASE_LEAVING };

std::atomic<bool> g_installed{false};
std::atomic<int>  g_phase{PHASE_INACTIVE};
std::atomic<bool> g_needPaletteApply{false};
float g_animT = 0.0f;   // 0 = fully off-screen (right), 1 = fully in place
int  g_selection = PracticeMenu::Render::ROW_PRACTICE;
uint32_t g_lastScreenContext = 0;

constexpr float kAnimStep     = 0.16f;
constexpr int   kSlideDistPx  = 200;

uintptr_t g_updateHookTarget = 0;
TitleUpdateFn g_origUpdate = nullptr;

struct InputState { int horiz = 0; int vert = 0; bool confirm = false; bool cancel = false; };
InputState g_prevInput;

// 0 = none, otherwise a TitleScreen::Screen value; consumed by the title update detour.
std::atomic<int> g_pendingReopen{0};

inline float Smoothstep(float t) {
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}
inline int SlideOffsetPx() {
    return static_cast<int>((1.0f - Smoothstep(g_animT)) * static_cast<float>(kSlideDistPx));
}

void Log(const char* msg) { LogOut(std::string("[PRACTICE_MENU] ") + msg, true); }

bool SupportIssueContains(const std::vector<std::string>& issues, const char* needle) {
    for (const std::string& issue : issues) {
        if (issue.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::string PlayerFacingUnavailableReason(const std::vector<std::string>& issues) {
    const bool setup = SupportIssueContains(issues, "exact start state");
    const bool opponent = SupportIssueContains(issues, "dummy episode") ||
                          SupportIssueContains(issues, "dummy_script") ||
                          SupportIssueContains(issues, "injectable action") ||
                          SupportIssueContains(issues, " trigger '") ||
                          SupportIssueContains(issues, " start '");
    const bool exercise = SupportIssueContains(issues, "placeholder") ||
                          SupportIssueContains(issues, "not authored") ||
                          SupportIssueContains(issues, "not declared") ||
                          SupportIssueContains(issues, "unknown dummy episode");
    const bool demonstration = SupportIssueContains(issues, "demonstration");
    const bool retry = SupportIssueContains(issues, "failure reset") ||
                       SupportIssueContains(issues, "checkpoint");

    if (setup) {
        return "The exact starting position for this lesson is still being prepared.";
    }
    if (opponent && exercise) {
        return "This lesson's exercise and scripted opponent are still being prepared.";
    }
    if (opponent) {
        return "The scripted opponent for this lesson is not ready yet.";
    }
    if (exercise) {
        return "The exercise for this lesson is still being authored.";
    }
    if (demonstration) {
        return "This lesson's demonstration is not ready yet.";
    }
    if (retry) {
        return "This lesson's retry behavior is not ready yet.";
    }
    return "The game-event tracking needed to score this lesson accurately is still being completed.";
}

std::string PlayerFacingRuntimeUnavailableReason(const std::string& error,
                                                 bool recorded,
                                                 const std::string& sourcePath) {
    const std::string subject = recorded ? "recording" : "mission";
    if (error.find("mission needs author review:") != std::string::npos) {
        const std::size_t separator = error.find(':');
        const std::string reasons = separator == std::string::npos
            ? std::string() : error.substr(separator + 1);
        return "This " + subject + " needs review before it can be played" +
            (reasons.empty() ? std::string(".") : std::string(":") + reasons);
    }
    if (error.find("uncompiled entity contacts") != std::string::npos ||
        error.find("entity-contact schedule") != std::string::npos) {
        return "This " + subject +
            " has incomplete projectile-hit data. Retake or review it before playing.";
    }
    if (error.find("contact tracking") != std::string::npos ||
        error.find("collision hook") != std::string::npos) {
        return "The contact tracking needed to score this " + subject +
            " is unavailable.";
    }
    if (error.find("has no steps") != std::string::npos) {
        return "This " + subject + " has no playable steps yet.";
    }

    std::string detail = error;
    const std::size_t pathAt = sourcePath.empty()
        ? std::string::npos : detail.find(sourcePath);
    if (pathAt != std::string::npos) {
        detail.erase(pathAt, sourcePath.size());
        while (!detail.empty() &&
               (detail.back() == ' ' || detail.back() == ':')) {
            detail.pop_back();
        }
    }
    if (detail.empty()) detail = "its runtime requirements are not available";
    return "This " + subject + " cannot be played: " + detail;
}

// Own frame: C++ objects are safe here; UpdateDetour's __try only calls it.
void PopulateMissionEntries() {
    Mission::TutorialSession::BeginLessonRegistryRefresh();
    std::vector<PracticeMenu::TitleScreen::MissionInfo> out;
    const std::string root = Mission::ResolveMissionsRoot();
    int libraryErrorCount = 0;
    std::string firstLibraryError;
    auto recordLibraryError = [&](const std::string& path,
                                  const std::string& error) {
        ++libraryErrorCount;
        const std::string detail = path + ": " +
            (error.empty() ? std::string("unknown parse error") : error);
        if (firstLibraryError.empty()) firstLibraryError = detail;
        LogOut("[PRACTICE_MENU] library entry skipped: " + detail, true);
    };
    auto parseOne = [&](const std::string& path,
                        const std::string& source,
                        const std::string& author,
                        const std::string& scenarioDescription,
                        bool locked,
                        bool recorded,
                        const std::string& packId,
                        const std::string& packVersion,
                        const Mission::Scenario* scenario,
                        const Mission::Pack* pack) {
        Mission::Mission mi;
        std::string err;
        if (!Mission::LoadMission(path, mi, err)) {
            recordLibraryError(path, err);
            return;
        }
        PracticeMenu::TitleScreen::MissionInfo e;
        const size_t slash = path.find_last_of("\\/");
        e.name = scenario && !scenario->name.empty() ? scenario->name
               : !mi.name.empty() ? mi.name
               : (slash != std::string::npos ? path.substr(slash + 1) : path);
        e.path = path;
        e.description = !scenarioDescription.empty()
            ? scenarioDescription : mi.description;
        e.type = mi.type;
        e.character = mi.player.character;
        e.dummy = mi.dummy.character;
        e.source = source;
        e.packId = packId;
        if (pack) e.packFolder = pack->folderPath;
        e.author = author;
        e.packVersion = packVersion;
        if (pack) e.packDescription = pack->description;
        e.category = scenario && !scenario->category.empty()
            ? scenario->category : mi.category;
        if (pack && !e.category.empty()) {
            for (const Mission::PackCategory& category : pack->categories) {
                if (category.id != e.category) continue;
                e.categoryLabel = category.label;
                e.categoryDescription = category.description;
                e.categoryOrder = category.order;
                break;
            }
        }
        e.difficulty = scenario && scenario->difficulty > 0
            ? scenario->difficulty : mi.difficulty;
        e.order = scenario && scenario->order > 0 ? scenario->order : mi.order;
        e.lessonId = scenario && !scenario->id.empty() ? scenario->id : mi.lessonId;
        e.recommendedAfter = scenario && !scenario->recommendedAfter.empty()
            ? scenario->recommendedAfter : mi.recommendedAfter;
        if (!mi.summary.empty()) e.description = mi.summary;
        std::string runtimeReadinessError;
        const bool runtimeReady =
            Mission::Engine::ValidateMissionRuntimeReadiness(
                mi, path, runtimeReadinessError);
        if (mi.tutorialSchema > 0) {
            const auto issues = Mission::Tutorial::RuntimeSupportIssues(mi);
            if (!issues.empty()) {
                e.unavailableReason = PlayerFacingUnavailableReason(issues);
            }
        }
        if (!runtimeReady && e.unavailableReason.empty()) {
            e.unavailableReason = PlayerFacingRuntimeUnavailableReason(
                runtimeReadinessError, recorded, path);
        }
        if (mi.tutorialSchema > 0) {
            if (!packId.empty() && !mi.lessonId.empty()) {
                const Mission::Tutorial::LessonProgress progress =
                    Mission::Tutorial::ProgressGet(packId, mi.lessonId);
                e.cleared = progress.cleared;
                e.updated = progress.cleared &&
                    progress.revisionCleared < mi.revision;
            }
            if (!mi.lessonId.empty()) {
                Mission::TutorialSession::RegisterLessonPath(
                    packId, mi.lessonId, path, mi.name, mi.nextLessonId,
                    e.unavailableReason.empty());
            }
        }
        e.steps = static_cast<int>(mi.hasLesson
                                       ? mi.lesson.tasks.size()
                                       : mi.steps.size());
        e.stage = mi.stage;
        e.bgm = mi.bgm;
        e.recorded = recorded;
        e.locked = locked;
        e.hasDemo = !mi.demo.empty();
        e.hasSavestate = !mi.savestate.empty();
        if (!mi.hints.empty()) e.hint = mi.hints.front();
        // Must match SplitNotation: drop a lowercase position prefix (j./c./f.), then a leading
        // neutral '5' ("5A"->"A", "c.5B"->"c.B").
        auto stripNeutral = [](const std::string& n) -> std::string {
            size_t i = 0;
            while (i < n.size() && ((n[i] >= 'a' && n[i] <= 'z') || n[i] == '.')) ++i;
            if (i < n.size() && n[i] == '5' && i + 1 < n.size() && n[i + 1] != ' ')
                return n.substr(0, i) + n.substr(i + 1);
            return n;
        };
        for (const Mission::Step& step : mi.steps) {
            if (!e.recipe.empty()) e.recipe += "  >  ";
            if (!step.notation.empty()) e.recipe += stripNeutral(step.notation);
            else if (!step.moveIds.empty()) e.recipe += "#" + std::to_string(step.moveIds.front());
            else e.recipe += "?";
        }
        out.push_back(std::move(e));
    };
    std::vector<std::pair<std::string, std::string>> tutorialCats;
    if (!root.empty()) {
        for (const std::string& pj : Mission::DiscoverPackJsonPaths(root)) {
            Mission::Pack pack; std::string err;
            if (!Mission::LoadPack(pj, pack, err)) {
                recordLibraryError(pj, err);
                continue;
            }
            const std::string src = pack.name.empty() ? std::string("PACK") : pack.name;
            const std::size_t packEntryStart = out.size();
            for (const auto& sc : pack.scenarios) {
                std::string missionPath;
                std::string componentError;
                if (!Mission::Authoring::ResolveScenarioMissionPath(
                        pack.folderPath, sc.file, missionPath, componentError)) {
                    recordLibraryError(pj,
                        "unsafe scenario file '" + sc.file + "': " +
                        (componentError.empty() ? std::string("path could not be resolved")
                                                : componentError));
                    continue;
                }
                parseOne(missionPath,
                         src,
                         pack.author,
                         sc.description,
                         sc.locked,
                         false,
                         pack.id,
                         pack.version,
                         &sc,
                         &pack);
            }
            bool containsTutorial = pack.curriculumRevision > 0;
            for (std::size_t i = packEntryStart; i < out.size(); ++i) {
                if (out[i].type == "tutorial") {
                    containsTutorial = true;
                    break;
                }
            }
            if (containsTutorial && !pack.categories.empty()) {
                std::vector<const Mission::PackCategory*> ordered;
                for (const auto& c : pack.categories) ordered.push_back(&c);
                std::stable_sort(ordered.begin(), ordered.end(),
                                 [](const Mission::PackCategory* a, const Mission::PackCategory* b) {
                                     return a->order < b->order;
                                 });
                for (const auto* c : ordered) tutorialCats.emplace_back(c->id, c->label);
            }
        }
        WIN32_FIND_DATAA fd{};
        HANDLE h = FindFirstFileA((root + "\\_recorded\\*.json").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                parseOne(root + "\\_recorded\\" + fd.cFileName,
                         "RECORDED", "LOCAL CAPTURE", std::string(), false, true,
                         std::string(), std::string(), nullptr, nullptr);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    Mission::TutorialSession::FinishLessonRegistryRefresh();
    PracticeMenu::TitleScreen::SetTutorialCategories(std::move(tutorialCats));
    PracticeMenu::TitleScreen::SetMissions(std::move(out));
    if (libraryErrorCount > 0) {
        std::string message = "Session library skipped " +
            std::to_string(libraryErrorCount) +
            (libraryErrorCount == 1 ? " unreadable file" : " unreadable files");
        if (!firstLibraryError.empty()) message += "; see the log for details";
        DirectDrawHook::AddMessage(message.c_str(), "SESSION LIBRARY",
                                   RGB(255, 175, 120), 4200, 20, 96);
    }
}

void SurfaceTutorialProgressWarning() {
    std::string warning;
    if (Mission::Tutorial::ProgressLoad(warning) || warning.empty()) return;
    DirectDrawHook::AddMessage(
        "Tutorial progress was unreadable. It was reset, and the old file was kept as .bad.",
        "TUTORIAL", RGB(255, 175, 120), 5200, 20, 96);
    LogOut("[PRACTICE_MENU] " + warning, true);
}

bool QueueSelectedMission(bool& directLoading) {
    directLoading = false;
    const std::string path = PracticeMenu::TitleScreen::SelectedMissionPath();
    if (path.empty()) return false;
    std::string error;
    if (!Mission::Engine::SetPendingMissionLoad(path, error)) {
        if (error.empty()) error = "the selected session could not be prepared";
        const bool tutorial = PracticeMenu::TitleScreen::Current() ==
                              PracticeMenu::TitleScreen::Screen::Tutorial;
        const std::string message = std::string(tutorial ? "Lesson" : "Mission") +
                                    " could not start: " + error;
        DirectDrawHook::AddMessage(message.c_str(),
                                   tutorial ? "TUTORIAL" : "MISSION",
                                   RGB(255, 150, 120), 3000, 20, 96);
        LogOut("[PRACTICE_MENU] selected launch rejected before leaving browser: " +
               error, true);
        return false;
    }
    directLoading = CharacterHotswap::IsDirectPracticeLoadPending();
    return true;
}

}

// Runs inside updateTitleScreenLogic; keep it minimal and guarded.
extern "C" void __cdecl PracticeMenu_EnterFromCase(uint32_t screenContext) {
    // A function using __try cannot hold C++ objects that need unwinding (C2712);
    // string building stays inside the Log() helper's own frame.
    __try {
        g_lastScreenContext = screenContext;
        g_selection = PracticeMenu::Render::ROW_PRACTICE;
        PracticeMenu::TitleScreen::Close();
        g_animT = 0.0f;
        g_needPaletteApply.store(true);
        g_prevInput = InputState{0, 0, true, false};
        g_phase.store(PHASE_ENTERING);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_phase.store(PHASE_INACTIVE);
    }
    if (g_phase.load() != PHASE_INACTIVE) Log("Entered submenu from title Practice");
}

namespace {

InputState ReadInput(uint32_t gc) {
    InputState s;
    if (!gc) return s;
    const int8_t h1 = *reinterpret_cast<int8_t*>(gc + kGcHorizBase);
    const int8_t h2 = *reinterpret_cast<int8_t*>(gc + kGcHorizBase + 1);
    const int h = (h1 != 0) ? h1 : h2;
    s.horiz = (h < 0) ? -1 : (h > 0 ? 1 : 0);
    const int8_t v1 = *reinterpret_cast<int8_t*>(gc + kGcVertBase);
    const int8_t v2 = *reinterpret_cast<int8_t*>(gc + kGcVertBase + 1);
    const int v = (v1 != 0) ? v1 : v2;
    s.vert = (v < 0) ? -1 : (v > 0 ? 1 : 0);
    s.confirm = (*reinterpret_cast<uint8_t*>(gc + kGcConfirmBase) != 0) ||
                (*reinterpret_cast<uint8_t*>(gc + kGcConfirmBase + 1) != 0);
    s.cancel  = (*reinterpret_cast<uint8_t*>(gc + kGcCancelBase) != 0) ||
                (*reinterpret_cast<uint8_t*>(gc + kGcCancelBase + 1) != 0);
    return s;
}

void BeginLeave() {
    g_phase.store(PHASE_LEAVING);
}
void FinishLeave(uint32_t sc) {
    PracticeMenu::TitleScreen::Close();
    PracticeMenu::Render::Release(sc);
    g_phase.store(PHASE_INACTIVE);
    Log("Submenu closed -> title menu");
}

// Replicates the vanilla Practice case byte-for-byte, including the blocking fade;
// skipping the fade starts the practice-match BGM ~1s late.
void LaunchPractice(uint32_t sc, bool directLoading = false) {
    // Restore the vanilla palette BEFORE the fade: it re-renders from the +1080
    // surface each step, so an applied 193+ overlay palette fades out broken.
    PracticeMenu::Render::Release(sc);
    __try {
        const uint32_t gc = GameContext(sc);
        if (gc) {
            reinterpret_cast<PlaySfxFn>(Resolve(kVaPlaySfx))(
                reinterpret_cast<void*>(gc), kSfxConfirm);
            *reinterpret_cast<uint8_t*>(gc + kGcP1Type) = 0;      // P1 human
            *reinterpret_cast<uint8_t*>(gc + kGcP2Type) = 1;      // P2 CPU/dummy
            *reinterpret_cast<uint8_t*>(gc + kGcActivePlayer) = 0;
            *reinterpret_cast<uint8_t*>(gc + kGcGameMode) = 1;    // Practice
            *reinterpret_cast<uint8_t*>(gc + kGcRoundCount) = 2;
        }
        reinterpret_cast<FadeFn>(Resolve(kVaFade))(
            reinterpret_cast<void*>(sc), static_cast<int>(sc + kOffPalette), 1, 0, 0);
        *reinterpret_cast<uint8_t*>(sc + kOffLifecycle) = 2;      // title re-entry fade state
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    Log(directLoading
        ? "Launching mission/tutorial (mode=1 -> direct Loading)"
        : "Launching Practice (mode=1 -> Character Select)");
}

int Activate(uint32_t sc) {
    switch (g_selection) {
        case PracticeMenu::Render::ROW_PRACTICE:
            LaunchPractice(sc);
            g_phase.store(PHASE_INACTIVE);
            return 1;
        case PracticeMenu::Render::ROW_TUTORIAL:
            SurfaceTutorialProgressWarning();
            PopulateMissionEntries();
            PracticeMenu::TitleScreen::Open(PracticeMenu::TitleScreen::Screen::Tutorial);
            Log("Tutorial screen opened");
            return -1;
        case PracticeMenu::Render::ROW_MISSION:
            PopulateMissionEntries();
            PracticeMenu::TitleScreen::Open(PracticeMenu::TitleScreen::Screen::Missions);
            Log("Missions screen opened");
            return -1;

        default:
            return -1;
    }
}

// The vanilla title render draws AND presents; we must present ourselves,
// blitting only the +1076 background (no menu) or the last vanilla frame freezes.
BOOL CallPresentGuarded(uint32_t gfxCtx) {
    const uintptr_t presentVa = Resolve(kVaPresent);
    if (!presentVa) return FALSE;
    BOOL r = FALSE;
    __try {
        r = reinterpret_cast<PresentFn>(presentVa)(static_cast<int>(gfxCtx));
    } __except (EXCEPTION_EXECUTE_HANDLER) { r = FALSE; }
    return r;
}

void RenderAndPresent(uint32_t sc) {
    PracticeMenu::Render::DrawBackground(sc);
    // MUST come after DrawBackground: setting the palette before this frame's
    // first blit disrupts the blit target.
    if (g_needPaletteApply.exchange(false)) {
        PracticeMenu::Render::ApplyPalette(sc);
    }
    if (!PracticeMenu::TitleScreen::Active()) {
        PracticeMenu::Render::DrawRows(sc, g_selection, SlideOffsetPx());
    }
    const uint32_t gfxCtx = GraphicsSystem(sc);
    const BOOL presentRet = CallPresentGuarded(gfxCtx);

    static DWORD s_lastLog = 0;
    const DWORD now = GetTickCount();
    if (detailedLogging.load() && now - s_lastLog > 1000) {
        s_lastLog = now;
        char buf[256];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
            "render phase=%d animT=%.2f off=%d gfx=0x%08X bg=0x%08X obj=0x%08X loaded=%d present=%d",
            g_phase.load(), g_animT, SlideOffsetPx(), gfxCtx,
            sc ? *reinterpret_cast<uint32_t*>(sc + kOffBgSurface) : 0u,
            sc ? *reinterpret_cast<uint32_t*>(sc + kOffObjSurface) : 0u,
            PracticeMenu::Render::IsLoaded() ? 1 : 0, static_cast<int>(presentRet));
        Log(buf);
    }
}

void ReopenGuardedStateEnter(uint32_t sc) {
    __try {
        g_lastScreenContext = sc;
        g_animT = 1.0f;
        g_needPaletteApply.store(true);
        g_prevInput = InputState{0, 0, true, true};
        g_phase.store(PHASE_ACTIVE);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_phase.store(PHASE_INACTIVE);
    }
}

void ConsumeReopenRequest(uint32_t sc) {
    const int reopen = g_pendingReopen.exchange(0);
    if (reopen == 0) return;
    const auto screen = static_cast<PracticeMenu::TitleScreen::Screen>(reopen);
    g_selection = screen == PracticeMenu::TitleScreen::Screen::Tutorial
                ? PracticeMenu::Render::ROW_TUTORIAL
                : PracticeMenu::Render::ROW_MISSION;
    ReopenGuardedStateEnter(sc);
    if (g_phase.load() != PHASE_INACTIVE) {
        if (screen == PracticeMenu::TitleScreen::Screen::Tutorial) {
            SurfaceTutorialProgressWarning();
        }
        PopulateMissionEntries();
        PracticeMenu::TitleScreen::Open(
            screen, screen == PracticeMenu::TitleScreen::Screen::Tutorial);
        Log("Browser reopened after session exit");
    }
}

char UpdateDetourBody(uint32_t sc) {
    int phase = g_phase.load();
    if (phase == PHASE_INACTIVE) {
        ConsumeReopenRequest(sc);
        phase = g_phase.load();
        if (phase == PHASE_INACTIVE) {
            return g_origUpdate ? g_origUpdate(sc) : 0;
        }
    }

    __try {
        g_lastScreenContext = sc;

        if (phase == PHASE_ENTERING) {
            g_animT += kAnimStep;
            if (g_animT >= 1.0f) { g_animT = 1.0f; g_phase.store(PHASE_ACTIVE); }
        } else if (phase == PHASE_LEAVING) {
            g_animT -= kAnimStep;
            if (g_animT <= 0.0f) {
                g_animT = 0.0f;
                FinishLeave(sc);
                return g_origUpdate ? g_origUpdate(sc) : 0;
            }
        } else {
            const uint32_t gc = GameContext(sc);
            if (gc) {
                reinterpret_cast<ProcessInputFn>(Resolve(kVaProcessPlayerInput))(reinterpret_cast<void*>(gc));
                const InputState cur = ReadInput(gc);

                static int s_vertHold = 0;
                if (cur.vert != 0 && cur.vert == g_prevInput.vert) ++s_vertHold;
                else s_vertHold = 0;
                int vertEdge = (g_prevInput.vert == 0 && cur.vert != 0) ? cur.vert : 0;
                if (vertEdge == 0 && cur.vert != 0 &&
                    s_vertHold >= 18 && ((s_vertHold - 18) % 6) == 0) {
                    vertEdge = cur.vert;
                }
                const bool confirmEdge = (!g_prevInput.confirm && cur.confirm);
                const bool cancelEdge  = (!g_prevInput.cancel && cur.cancel);
                const int horizEdge = (g_prevInput.horiz == 0 && cur.horiz != 0) ? cur.horiz : 0;
                g_prevInput = cur;

                static bool s_prevEsc = false;
                const bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
                const bool escEdge = escDown && !s_prevEsc;
                s_prevEsc = escDown;

                if (PracticeMenu::TitleScreen::Active()) {
                    if (confirmEdge) {
                        const auto act = PracticeMenu::TitleScreen::Confirm();
                        bool directLoading = false;
                        if (act == PracticeMenu::TitleScreen::ConfirmAction::Launch &&
                            QueueSelectedMission(directLoading)) {
                            PracticeMenu::TitleScreen::Close();
                            LaunchPractice(sc, directLoading);
                            g_phase.store(PHASE_INACTIVE);
                            // Returning 2 is the native route into Loading; our
                            // one-shot Loading hook supplies the fighter objects.
                            return directLoading ? 2 : 1;
                        }
                        if (act == PracticeMenu::TitleScreen::ConfirmAction::Record) {
                            Mission::Engine::SetPendingRecordMode(true);
                            PracticeMenu::TitleScreen::Close();
                            LaunchPractice(sc);
                            g_phase.store(PHASE_INACTIVE);
                            return 1;
                        }
                        if (act == PracticeMenu::TitleScreen::ConfirmAction::Author) {
                            Mission::Engine::SetPendingMissionMode(true);
                            PracticeMenu::TitleScreen::Close();
                            LaunchPractice(sc);
                            g_phase.store(PHASE_INACTIVE);
                            return 1;
                        }
                    } else if (cancelEdge || escEdge) {
                        if (!PracticeMenu::TitleScreen::Back()) {
                            PracticeMenu::TitleScreen::Close();
                        }
                    } else if (horizEdge != 0) {
                        reinterpret_cast<PlaySfxFn>(Resolve(kVaPlaySfx))(
                            reinterpret_cast<void*>(gc), kSfxCursor);
                        PracticeMenu::TitleScreen::MoveTab(horizEdge);
                    } else if (vertEdge != 0) {
                        reinterpret_cast<PlaySfxFn>(Resolve(kVaPlaySfx))(
                            reinterpret_cast<void*>(gc), kSfxCursor);
                        PracticeMenu::TitleScreen::MoveSelection(vertEdge);
                    }
                } else if (confirmEdge) {
                    const int r = Activate(sc);
                    if (r >= 0) return static_cast<char>(r);
                } else if (cancelEdge || escEdge) {
                    BeginLeave();
                } else if (vertEdge != 0) {
                    reinterpret_cast<PlaySfxFn>(Resolve(kVaPlaySfx))(
                        reinterpret_cast<void*>(gc), kSfxCursor);
                    g_selection += vertEdge;
                    if (g_selection < 0) g_selection = PracticeMenu::Render::ROW_COUNT - 1;
                    if (g_selection >= PracticeMenu::Render::ROW_COUNT) g_selection = 0;
                }
            }
        }

        RenderAndPresent(sc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Mission::Engine::CancelPendingMissionLoad("title update exception");
        CharacterHotswap::CancelDirectPracticeLoad("title update exception");
        g_phase.store(PHASE_INACTIVE);
    }
    return 0;
}

char __fastcall UpdateDetour(uint32_t sc, void*) {
    auto execution=MinHookUtils::EnterExecution(MinHookUtils::TicketFor<&UpdateDetour>());
    if (!execution.Admitted()) return g_origUpdate ? g_origUpdate(sc) : 0;
    return UpdateDetourBody(sc);
}

bool VerifyVanillaTitle() {
    const uintptr_t updateVa = Resolve(kVaUpdateTitle);
    if (!updateVa) return false;
    static const uint8_t kExpected[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C}; // push ebp; mov ebp,esp; sub esp,0Ch
    uint8_t got[6] = {0};
    if (!SafeReadMemory(updateVa, got, sizeof(got))) return false;
    return std::memcmp(got, kExpected, sizeof(kExpected)) == 0;
}

bool PatchPracticeCase() {
    return Practice::InstallTitleCaseAdapter(&PracticeMenu_EnterFromCase);
}

void UnpatchPracticeCase() {
    // Physical restoration belongs to the native owner continuation. Retain code, the accepted
    // bypass and resources until the registry drains it.
    Practice::CloseTitleCaseAdmission();
}

}

namespace PracticeMenu {

bool Install() {
    if (g_installed.load()) return true;

    if (!MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(g_updateHookTarget)) && !VerifyVanillaTitle()) {
        Log("Vanilla title code not recognized; skipping (Practice stays vanilla)");
        return false;
    }

    const uintptr_t updateVa = Resolve(kVaUpdateTitle);
    if (g_updateHookTarget!=updateVa && MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(g_updateHookTarget))) return false;
    g_updateHookTarget=updateVa;
    if (!MinHookUtils::CreateAndEnableHook(reinterpret_cast<void*>(updateVa),
                                           reinterpret_cast<void*>(&UpdateDetour),
                                           reinterpret_cast<void**>(&g_origUpdate),
                                           "PracticeMenu", "updateTitleScreenLogic", nullptr, nullptr, &MinHookUtils::TicketFor<&UpdateDetour>())) {
        Log("Failed to hook updateTitleScreenLogic");
        return false;
    }

    if (!PatchPracticeCase()) {
        Log("Failed to patch Practice case; rolling back update hook");
        MinHookUtils::RemoveHook(reinterpret_cast<void*>(updateVa), "PracticeMenu", "updateTitleScreenLogic");
        (void)PracticeHooks::ReleaseRetiredOriginal(MinHookUtils::OwnedHooks(), g_updateHookTarget, g_origUpdate);
        return false;
    }

    if (!CharacterHotswap::InstallDirectPracticeBootstrap()) {
        Log("Direct Loading bootstrap unavailable; missions retain Character Select fallback");
    }

    g_installed.store(true);
    Log("Installed (Practice -> submenu; coexists with InGameNetplay)");
    return true;
}

void Uninstall() {
    if (!g_installed.load() && !MinHookUtils::HasOwnedTarget(reinterpret_cast<void*>(g_updateHookTarget)) &&
        !Practice::TitleCaseRetained()) return;
    g_phase.store(PHASE_INACTIVE);
    Mission::Engine::CancelPendingMissionLoad("Practice menu uninstall");
    CharacterHotswap::CancelDirectPracticeLoad("Practice menu uninstall");
    CharacterHotswap::UninstallDirectPracticeBootstrap();
    UnpatchPracticeCase();
    const uintptr_t updateVa = g_updateHookTarget;
    if (updateVa) MinHookUtils::RemoveHook(reinterpret_cast<void*>(updateVa), "PracticeMenu", "updateTitleScreenLogic");
    g_installed.store(false);
    g_pendingReopen.store(0);
    if (Practice::TitleCaseRetained() ||
        !PracticeHooks::ReleaseRetiredOriginal(MinHookUtils::OwnedHooks(), g_updateHookTarget, g_origUpdate)) {
        Log("Admission closed; update hook and render resources retained until retirement");
        return;
    }
    g_updateHookTarget=0;
    if (g_lastScreenContext) Render::Release(g_lastScreenContext);
    Log("Uninstalled");
}

bool IsSubmenuActive() { return g_phase.load() != PHASE_INACTIVE; }

void RequestTitleReopen(TitleScreen::Screen screen) {
    g_pendingReopen.store(static_cast<int>(screen));
}

}
