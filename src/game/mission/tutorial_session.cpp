#include "../../../include/game/mission/tutorial_session.h"
#include "../../../include/game/mission/tutorial_episode_policy.h"
#include "../../../include/game/mission/tutorial_layout_policy.h"
#include "../../../include/game/mission/tutorial_text_policy.h"

#include "../../../include/game/mission/mission_data.h"
#include "../../../include/game/mission/mission_engine.h"
#include "../../../include/game/mission/mission_moves.h"
#include "../../../include/game/mission/mission_sequence_policy.h"
#include "../../../include/game/mission/tutorial_support.h"
#include "../../../include/game/mission/tutorial_state_policy.h"
#include "../../../include/game/character_settings.h"
#include "../../../include/game/mission/mission_render.h"
#include "../../../include/game/macro_controller.h"
#include "../../../include/game/auto_action.h"
#include "../../../include/game/always_rg.h"
#include "../../../include/game/hud_disable.h"
#include "../../../include/game/random_block.h"
#include "../../../include/game/practice_patch.h"
#include "../../../include/game/collision_display.h"
#include "../../../include/game/frame_analysis.h"
#include "../../../include/input/motion_system.h"
#include "../../../include/input/motion_constants.h"
#include "../../../include/input/immediate_input.h"
#include "../../../include/input/input_freeze.h"
#include "../../../include/input/input_core.h"
#include "../../../include/game/game_state.h"
#include "../../../include/game/practice_menu/practice_menu.h"
#include "../../../include/game/mission/mission_pause_menu.h"
#include "../../../include/gui/custom_menu/layout.h"
#include "../../../include/gui/custom_menu/fonts.h"
#include "../../../include/gui/custom_menu/theme.h"
#include "../../../include/gui/custom_menu/scale.h"
#include "../../../include/gui/imgui_impl.h"
#include "../../../include/gui/overlay.h"
#include "../../../include/core/constants.h"
#include "../../../include/core/logger.h"
#include "../../../include/core/memory.h"
#include "../../../include/utils/config.h"
#include "../../../include/utils/utilities.h"
#include "../../../include/utils/pause_integration.h"
#include "../../../include/utils/xinput_shim.h"
#include "../../../include/input/input_hook.h"
#include "../../../include/input/injection_control.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>

namespace Mission::TutorialSession {

namespace {

namespace L = CustomMenu::Layout;
namespace T = CustomMenu::Theme;
namespace Tut = ::Mission::Tutorial;
namespace HudPolicy = ::Mission::TutorialLayoutPolicy;
namespace TextPolicy = ::Mission::TutorialTextPolicy;

constexpr const char* kFallbackPackId = "efz.core_tutorial";

struct TaskState {
    bool done = false;
    bool failed = false;
};

struct TaskMetrics {
    bool   valid = false;
    int    ticks = 0;          // non-frozen session ticks, arm -> completion
    int    comboHits = 0;
    int    comboDamage = 0;
    int    maxUntech = 0;
    int    untechTicks = 0;
    double p1RfStart = 0.0, p1RfEnd = 0.0;
    double p2HpStart = 0.0, p2HpEnd = 0.0;
    double p2GuardStart = 0.0, p2GuardEnd = 0.0;
};

struct UiTaskModel {
    std::string label;
    std::string neutralLabel;
    std::string prompt;
    std::string cueText;
    std::vector<std::string> actions;
    std::vector<std::string> options;
};

struct UiPageModel {
    std::string title;
    std::string text;
    std::array<std::string, 3> introActions;
    std::array<std::string, 3> reviewActions;
    HudPolicy::HudFocus hudFocus = HudPolicy::HudFocus::None;
};

struct UiModel {
    std::string packId;
    std::string lessonId;
    std::string lessonName;
    std::string summary;
    std::string nextLessonId;
    std::string difficultyLabel;
    bool requirementsBelowStats = false;
    std::vector<UiPageModel> pages;
    std::vector<UiTaskModel> tasks;
    std::vector<std::string> completeOptions;
    std::vector<std::string> completeHints;
    std::string choiceHint;
    std::array<std::string, 3> choiceActions;
    std::array<std::string, 3> completeActions;
    std::array<std::string, 3> confirmActions;
    std::array<std::string, 3> errorActions;
};

struct JugglePreviewFields {
    short move = 0;
    short frame = 0;
    short animTimer = 0;
    double x = 0.0;
    double y = 0.0;
    double xVelocity = 0.0;
    double yVelocity = 0.0;
    short untech = 0;
    std::uint32_t wallbounce = 0;
};

struct JugglePreviewLease {
    bool held = false;
    uintptr_t playerBase = 0;
    std::uint32_t lifecycleGeneration = 0;
    HudPolicy::HudFocus focus = HudPolicy::HudFocus::None;
    JugglePreviewFields saved;
    JugglePreviewFields applied;
    bool outlineValid = false;
    HudPolicy::Rect outline;
    bool refreshActive = false;
    std::uint64_t refreshToken = 0;
    std::uint32_t refreshStartBatch = 0;
    int refreshWatchdogTicks = 0;
};

struct State {
    bool active = false;
    Phase phase = Phase::Idle;
    ::Mission::Mission lesson;
    std::shared_ptr<const UiModel> uiModel;
    std::string packId = kFallbackPackId;
    int page = 0;
    int task = 0;
    std::vector<TaskState> tasks;
    int choiceSel = 0;
    int completeSel = 0;          // 0 next / 1 again / 2 return (adjusted when no next)
    int confirmSel = 1;           // ConfirmExit: 0 = leave, 1 = stay (default stay)
    std::string feedbackText;
    bool feedbackSuccess = true;
    int feedbackTicks = 0;
    int neutralTicks = 0;
    uint32_t neutralObservedSerial = 0;
    bool cleared = false;
    bool clearRecorded = false;
    int attemptsThisRun = 0;
    Phase reviewReturn = Phase::TaskActive;
    bool reviewResumePending = false;
    bool reviewStateInvalidated = false;
    int reviewResumeTask = -1;
    Phase demoReturn = Phase::TaskActive;
    int demoReturnTask = -1;
    bool uiLatchNeedsSync = true;
    short prevMove = 0;
    short prevP2Move = 0;
    short prevFrameIdx = 0;
    short prevP2FrameIdx = 0;
    int   prevCombo = 0;
    int   comboBaseline = 0;
    int   sequenceIndex = 0;
    int   sequenceGapTicks = 0;
    std::vector<TaskMetrics> taskMetrics;
    TaskMetrics liveMetrics;
    bool  metricsLive = false;
    bool  waitingComboEnd = false;
    int   branchPhase = 0;        // 0 = starter, 1 = hit route, 2 = block hold
    int   branchSeqIndex = 0;
    int   branchTicks = 0;
    int   branchComboBase = 0;
    bool  branchActionCommitted = false;
    bool  goalBlockSeen = false;
    int   sinceP2Wake = -1;       // non-frozen ticks since P2's wake edge (-1 = none this task)
    int   sinceP2Whiff = -1;      // non-frozen ticks since P2's attack finished untouched (-1 = none)
    bool  p1TouchedByP2Attack = false;
    int   absencePhase = 0;       // action_absence: 0 = waiting for window, 1 = window open
    int   absenceElapsed = 0;
    bool  absenceDummyAttackSeen = false;
    int   absenceBlockCount = 0;
    int   absenceEpisodeCycleBase = 0;
    int   interceptionPrevLife = -1;
    bool  flickerContactSeen = false;
    bool  flickerSourceDistanceSafe = false;
    int   flickerDamageBaseline = 0;
    int   flickerDefenderHpBaseline = 0;
    bool  armedCommitted = false;
    int   prevHitState = 0;
    bool  comboWasAlive = false;
    bool  frozenLeaseHeld = false;
    bool  freezeConfirmed = false;
    bool  neutralInputLeaseHeld = false;
    JugglePreviewLease jugglePreview;
    int   jugglePreviewSuppressedPage = -1;
    int   jugglePreviewObservedPage = -1;
    bool  introWorldAdvanced = false;
    int   freezeReassertCooldown = 0;
    int   holdTicks = 0;
    int   inputCommitGrace = 0;
    uint8_t inputCommitMask = 0;
    short armedMove = 0;
    int   contactMatches = 0;
    bool  pendingSuccessRestore = false;
    bool  traceSampleValid = false;
    int   traceExpectedIndex = -1;
    short traceP1Move = 0;
    short traceP2Move = 0;
    short traceP1Frame = 0;
    short traceP2Frame = 0;
    int   traceCombo = 0;
    int   traceComboDamage = 0;
    int   traceHitState = 0;
    uint8_t traceInputs = 0;
    uint8_t traceAttackEdges = 0;
    uint32_t traceInputPollSerial = 0;
    bool  traceFreeze = false;
    bool  traceP2InStun = false;
    uint8_t traceContactCount = 0;
    uint32_t traceContactEpoch = 0;
    bool  traceContactOverflow = false;
    bool  traceDirectHookReady = false;
    bool  traceEntityHookReady = false;
    bool  episodePlaying = false;
    bool  episodeFinished = false;
    int   episodeCycleSerial = 0;
    int   episodePhase = 0;
    int   episodeTimer = 0;
    short episodeAnswerRgMove = -1;
    int   episodeCueTicks = 0;
    int   episodeCueDisplayTicks = 0;
    bool  episodeCueBlockedThisTick = false;
    int   episodeChainIndex = 0;
    bool  episodeContactFreezeSeen = false;
    short epPrevP2Move = -1;
    short epPrevP2FrameIdx = -1;
    bool  episodeFreeze = false;
    // episode_telegraph: 0 = press hop, 1 = await airborne,
    // 2 = await landed+settled, 3 = done.
    int   episodeTelegraphPhase = 0;
    int   episodeTelegraphTimer = 0;
    uint64_t wakeAutoToken = 0;
    bool rfLockApplied[2] = {false, false};

    bool  episodeDiagSnapshotValid = false;
    short episodeDiagP1Move = -1;
    short episodeDiagP2Move = -1;
    short episodeDiagP2Frame = -1;
    short episodeDiagLoggedP2Move = -32768;
    int   episodeDiagLoggedPhase = -1;
    int   episodeDiagLoggedScriptStep = -2;
    int   episodeDiagLoggedStepPhase = -1;
    bool  episodeDiagLoggedFreeze = false;
    bool  episodeDiagLoggedCueBlocked = false;
    std::string episodeDiagMovePath;

    uint64_t p2ControlToken = 0;
    uint64_t motionQueueToken = 0;
    uint64_t immediateInputToken = 0;
    uint64_t bufferFreezeToken = 0;
    uint64_t autoBlockControllerToken = 0;
    bool autoBlockTouched = false;
    bool autoBlockSaved = false;
    bool autoBlockAppliedValue = false;
    uint64_t autoBlockAppliedGeneration = 0;
    bool adaptiveStanceTouched = false;
    bool adaptiveStanceSaved = false;
    bool adaptiveStanceApplied = false;
    bool practiceBlockModeTouched = false;
    int practiceBlockModeSaved = 0;
    int practiceBlockModeApplied = 0;
    bool alwaysRGTouched = false;
    bool alwaysRGSaved = false;
    uint64_t alwaysRGAppliedGeneration = 0;
    bool randomBlockTouched = false;
    uint32_t variantSeed = 0;
    std::map<std::string, std::string> episodeVariantAssignments;
    int  episodeScriptStep = -1;
    int  scriptStepPhase = 0;     // 0 = pre-inject, 1 = awaiting move, 2 = move active
    bool scriptGapOpen = false;
    bool airtechTouched = false;
    bool airtechSavedEnabled = false;
    int  airtechSavedDirection = 0;
    int  airtechSavedDelay = 0;
    int  airtechAppliedDirection = 0;
    int  airtechAppliedDelay = 0;
    bool randomBlockSaved = false;
    uint64_t randomBlockAppliedGeneration = 0;
    bool injectImmediateOnlyTouched = false;
    bool injectImmediateOnlySaved = false;

    bool neutralPollSnapshotValid = false;
    bool neutralPollSavedActive = false;
    uint8_t neutralPollSavedMask = 0;
    bool neutralPollSavedObservation = false;
};

constexpr int kCompleteHold = 192;

struct UiSnapshot {
    bool active = false;
    uint64_t generation = 0;
    Phase phase = Phase::Idle;
    std::shared_ptr<const UiModel> model;
    int page = 0;
    int task = 0;
    bool reviewResumePending = false;
    // One byte per task: bit 0 done, bit 1 failed.
    std::vector<uint8_t> taskStates;
    int choiceSel = 0;
    int completeSel = 0;
    int confirmSel = 1;
    std::shared_ptr<const std::string> feedbackText;
    bool feedbackSuccess = true;
    int feedbackTicks = 0;
    bool episodeCueActive = false;
    int sequenceIndex = 0;
    bool sequenceArmed = false;
    bool freezeConfirmed = false;
    bool juggleOutlineValid = false;
    HudPolicy::Rect juggleOutline;
    std::string feedbackSource;
};

// EFZ targets Windows XP: no PE TLS directory - use an explicit Win32 TLS
// slot, not C++ `thread_local`.
struct UiSnapshotSlot {
    UiSnapshot snapshot;
    UiSnapshotSlot* next = nullptr;
};

std::atomic<DWORD> g_uiSnapshotTls{TLS_OUT_OF_INDEXES};
std::mutex g_uiSnapshotTlsMx;
UiSnapshotSlot* g_uiSnapshotSlots = nullptr;
bool g_uiSnapshotTlsFailureLogged = false;

UiSnapshot* AcquireUiSnapshot() {
    DWORD tlsIndex = g_uiSnapshotTls.load(std::memory_order_acquire);
    if (tlsIndex == TLS_OUT_OF_INDEXES) {
        std::lock_guard<std::mutex> lk(g_uiSnapshotTlsMx);
        tlsIndex = g_uiSnapshotTls.load(std::memory_order_relaxed);
        if (tlsIndex == TLS_OUT_OF_INDEXES) {
            tlsIndex = TlsAlloc();
            if (tlsIndex == TLS_OUT_OF_INDEXES) {
                if (!g_uiSnapshotTlsFailureLogged) {
                    g_uiSnapshotTlsFailureLogged = true;
                    LogOut("[TUTORIAL][UI] Failed to allocate explicit render TLS", true);
                }
                return nullptr;
            }
            g_uiSnapshotTls.store(tlsIndex, std::memory_order_release);
        }
    }

    UiSnapshotSlot* slot =
        static_cast<UiSnapshotSlot*>(TlsGetValue(tlsIndex));
    if (slot) return &slot->snapshot;

    slot = new (std::nothrow) UiSnapshotSlot{};
    if (!slot) return nullptr;

    std::lock_guard<std::mutex> lk(g_uiSnapshotTlsMx);
    if (g_uiSnapshotTls.load(std::memory_order_acquire) != tlsIndex ||
        !TlsSetValue(tlsIndex, slot)) {
        delete slot;
        return nullptr;
    }
    slot->next = g_uiSnapshotSlots;
    g_uiSnapshotSlots = slot;
    return &slot->snapshot;
}

std::mutex g_mx;
State g_s;
std::atomic<bool> g_active{false};
std::atomic<uint64_t> g_uiGeneration{1};

struct RegisteredLesson {
    std::string packId;
    std::string lessonId;
    std::string path;
    std::string displayName;
    std::string nextLessonId;
    bool available = true;
};

std::mutex g_regMx;
std::map<std::string, RegisteredLesson> g_registry;
std::map<std::string, RegisteredLesson> g_registryRefresh;
bool g_registryRefreshActive = false;

std::string RegistryKey(const std::string& packId, const std::string& lessonId) {
    return packId + "\x1f" + lessonId;
}

bool ResolveAvailableDescendantLocked(const std::string& packId,
                                      const std::string& firstLessonId,
                                      RegisteredLesson& resolved,
                                      int& skipped,
                                      std::string* error = nullptr) {
    skipped = 0;
    std::string current = firstLessonId;
    std::vector<std::string> visited;
    while (!current.empty()) {
        if (std::find(visited.begin(), visited.end(), current) != visited.end()) {
            if (error) *error = "the tutorial course contains a next-lesson cycle";
            return false;
        }
        visited.push_back(current);
        const auto it = g_registry.find(RegistryKey(packId, current));
        if (it == g_registry.end()) {
            if (error) *error = "the next tutorial lesson is not installed";
            return false;
        }
        if (it->second.available) {
            resolved = it->second;
            return true;
        }
        ++skipped;
        current = it->second.nextLessonId;
    }
    if (error) *error = "there is no later available lesson in this course";
    return false;
}

// Runner::Load/Unload reenter Begin/End which take g_mx; run only after
// releasing the lock.
enum class DeferredKind {
    None,
    RestoreCheckpoint,
    LoadNext,
    ReturnToBrowser,
};

struct DeferredRequest {
    DeferredKind kind = DeferredKind::None;
    std::string nextPath;
    std::string sourceLessonId;
};

bool FrozenPhase(Phase p) {
    return p == Phase::Intro || p == Phase::ConfirmExit || p == Phase::Choice ||
           p == Phase::Review || p == Phase::Complete || p == Phase::Error;
}

bool PhaseNeedsNeutralInput(Phase p) {
    return FrozenPhase(p) || p == Phase::NeutralGate || p == Phase::Feedback;
}

const char* PhaseName(Phase p) {
    switch (p) {
        case Phase::Idle:          return "idle";
        case Phase::Preparing:     return "preparing";
        case Phase::Intro:         return "intro";
        case Phase::ConfirmExit:   return "confirm_exit";
        case Phase::NeutralGate:   return "neutral_gate";
        case Phase::TaskActive:    return "task_active";
        case Phase::Choice:        return "choice";
        case Phase::Feedback:      return "feedback";
        case Phase::Review:        return "review";
        case Phase::DemoSuspended: return "demo";
        case Phase::Complete:      return "complete";
        case Phase::Error:         return "error";
    }
    return "unknown";
}

std::string LogExcerpt(std::string text) {
    for (char& ch : text) {
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
    }
    constexpr size_t kMax = 180;
    if (text.size() > kMax) text.replace(kMax - 3, std::string::npos, "...");
    return text;
}

std::string Upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

const char* DifficultyLabel(int difficulty) {
    switch (difficulty) {
        case 1: return "NOVICE";
        case 2: return "BEGINNER";
        case 3: return "INTERMEDIATE";
        case 4: return "ADVANCED";
        case 5: return "EXPERT";
        default: return "LESSON";
    }
}

std::shared_ptr<const UiModel> BuildUiModel(const ::Mission::Mission& lesson,
                                            const std::string& packId) {
    std::shared_ptr<UiModel> model = std::make_shared<UiModel>();
    model->packId = packId;
    model->lessonId = lesson.lessonId;
    model->lessonName = TextPolicy::NormalizeUiText(lesson.name);
    model->summary = TextPolicy::NormalizeUiText(lesson.summary);
    model->nextLessonId = lesson.nextLessonId;
    model->difficultyLabel = DifficultyLabel(lesson.difficulty);
    model->requirementsBelowStats =
        lesson.lesson.requirementPlacement == "belowStats";

    model->pages.reserve(lesson.lesson.pages.size());
    for (size_t i = 0; i < lesson.lesson.pages.size(); ++i) {
        const ::Mission::LessonPage& page = lesson.lesson.pages[i];
        UiPageModel uiPage;
        uiPage.title = TextPolicy::NormalizeUiText(page.title);
        uiPage.text = TextPolicy::NormalizeUiText(page.text);
        uiPage.hudFocus = HudPolicy::ParseHudFocus(page.showHud.c_str());
        const bool haveNextPage = i + 1 < lesson.lesson.pages.size();
        const std::string nextIntro = haveNextPage
            ? "{btn:A} NEXT PAGE"
            : lesson.lesson.completion == "pages"
                ? "{btn:A} FINISH LESSON"
                : "{btn:A} BEGIN LESSON";
        const std::string nextReview = haveNextPage
            ? "{btn:A} NEXT PAGE"
            : "{btn:A} RETURN TO TASK";
        const std::string backIntro = i > 0
            ? "{btn:B} PREVIOUS PAGE"
            : "{btn:B} RETURN TO LESSONS";
        const std::string backReview = i > 0
            ? "{btn:B} PREVIOUS PAGE"
            : "{btn:B} RETURN TO TASK";
        uiPage.introActions = {
            nextIntro, backIntro, "{ui:ESC} LESSON MENU"};
        uiPage.reviewActions = {
            nextReview, backReview, "{ui:ESC} LESSON MENU"};
        model->pages.push_back(std::move(uiPage));
    }
    model->tasks.reserve(lesson.lesson.tasks.size());
    for (const ::Mission::LessonTask& task : lesson.lesson.tasks) {
        UiTaskModel uiTask;
        uiTask.label = TextPolicy::NormalizeUiText(
            !task.label.empty() ? task.label
                                : !task.prompt.empty()
                                    ? task.prompt
                                    : TextPolicy::HumanizeIdentifier(task.id));
        uiTask.neutralLabel = "Release attack buttons, then: " + uiTask.label;
        uiTask.prompt = TextPolicy::NormalizeUiText(task.prompt);
        uiTask.actions.reserve(task.sequence.size());
        for (const ::Mission::LessonAction& action : task.sequence) {
            uiTask.actions.push_back(!action.notation.empty()
                ? TextPolicy::NotationToRich(action.notation)
                : TextPolicy::HumanizeIdentifier(action.id));
        }
        if (!task.script.empty()) {
            for (const ::Mission::DummyEpisode& episode : lesson.lesson.episodes) {
                if (episode.id == task.script) {
                    uiTask.cueText = TextPolicy::NormalizeUiText(episode.cueText);
                    break;
                }
            }
        }
        uiTask.options.reserve(task.options.size());
        for (const ::Mission::ChoiceOption& option : task.options) {
            uiTask.options.push_back(TextPolicy::NormalizeUiText(option.label));
        }
        model->tasks.push_back(std::move(uiTask));
    }

    std::string nextName;
    int skippedUnavailable = 0;
    if (!lesson.nextLessonId.empty()) {
        std::lock_guard<std::mutex> rl(g_regMx);
        RegisteredLesson next;
        if (ResolveAvailableDescendantLocked(
                packId, lesson.nextLessonId, next, skippedUnavailable)) {
            nextName = TextPolicy::NormalizeUiText(next.displayName);
        }
    }
    if (!nextName.empty()) {
        model->completeOptions.push_back(
            std::string(skippedUnavailable > 0
                            ? "NEXT AVAILABLE LESSON - " : "NEXT LESSON - ") +
            Upper(nextName));
        model->completeHints.push_back(
            skippedUnavailable > 0
                ? "Continue to the next available lesson."
                : "Continue to the next lesson.");
    }
    model->completeOptions.push_back("TRY AGAIN");
    model->completeHints.push_back(
        "Restart this lesson from its prepared starting state.");
    model->completeOptions.push_back("RETURN TO LESSONS");
    model->completeHints.push_back(
        "Return to the Tutorial browser and choose another lesson.");
    model->choiceHint = "Choose the answer that best matches the lesson.";
    model->choiceActions = {
        "UP / DOWN: CHOOSE", "{btn:A} SELECT", "{ui:ESC} LESSON MENU"};
    model->completeActions = model->choiceActions;
    model->confirmActions = {
        "UP / DOWN: CHOOSE", "{btn:A} SELECT", "{btn:B} KEEP READING"};
    model->errorActions = {
        std::string(), "{ui:ESC} LESSON MENU", std::string()};
    return model;
}

bool ReadJugglePreviewFields(uintptr_t player,
                             JugglePreviewFields& fields) {
    return player &&
        SafeReadMemory(player + MOVE_ID_OFFSET,
                       &fields.move, sizeof(fields.move)) &&
        SafeReadMemory(player + CURRENT_FRAME_INDEX_OFFSET,
                       &fields.frame, sizeof(fields.frame)) &&
        SafeReadMemory(player + STATE_SUBFRAME_COUNTER_OFFSET,
                       &fields.animTimer, sizeof(fields.animTimer)) &&
        SafeReadMemory(player + XPOS_OFFSET, &fields.x, sizeof(fields.x)) &&
        SafeReadMemory(player + YPOS_OFFSET, &fields.y, sizeof(fields.y)) &&
        SafeReadMemory(player + XVEL_OFFSET,
                       &fields.xVelocity, sizeof(fields.xVelocity)) &&
        SafeReadMemory(player + YVEL_OFFSET,
                       &fields.yVelocity, sizeof(fields.yVelocity)) &&
        SafeReadMemory(player + UNTECH_OFFSET,
                       &fields.untech, sizeof(fields.untech)) &&
        SafeReadMemory(player + PLAYER_WALLBOUNCE_FLAG_OFFSET,
                       &fields.wallbounce, sizeof(fields.wallbounce));
}

bool JugglePreviewFieldsEqual(const JugglePreviewFields& lhs,
                              const JugglePreviewFields& rhs) {
    return lhs.move == rhs.move && lhs.frame == rhs.frame &&
           lhs.animTimer == rhs.animTimer &&
           lhs.x == rhs.x && lhs.y == rhs.y &&
           lhs.xVelocity == rhs.xVelocity &&
           lhs.yVelocity == rhs.yVelocity &&
           lhs.untech == rhs.untech && lhs.wallbounce == rhs.wallbounce;
}

template <typename T>
bool WritePreviewValue(uintptr_t address, const T& value) {
    T observed{};
    return address && SafeWriteMemory(address, &value, sizeof(value)) &&
           SafeReadMemory(address, &observed, sizeof(observed)) &&
           std::memcmp(&observed, &value, sizeof(value)) == 0;
}

bool WriteJugglePreviewFields(uintptr_t player,
                              const JugglePreviewFields& fields,
                              bool staging) {
    if (!player) return false;
    bool ok = true;
    if (!staging) {
        ok = WritePreviewValue(player + MOVE_ID_OFFSET, fields.move) && ok;
        ok = WritePreviewValue(player + CURRENT_FRAME_INDEX_OFFSET,
                               fields.frame) && ok;
        ok = WritePreviewValue(player + STATE_SUBFRAME_COUNTER_OFFSET,
                               fields.animTimer) && ok;
    }
    ok = WritePreviewValue(player + XPOS_OFFSET, fields.x) && ok;
    ok = WritePreviewValue(player + YPOS_OFFSET, fields.y) && ok;
    ok = WritePreviewValue(player + XVEL_OFFSET, fields.xVelocity) && ok;
    ok = WritePreviewValue(player + YVEL_OFFSET, fields.yVelocity) && ok;
    ok = WritePreviewValue(player + UNTECH_OFFSET, fields.untech) && ok;
    ok = WritePreviewValue(player + PLAYER_WALLBOUNCE_FLAG_OFFSET,
                           fields.wallbounce) && ok;
    if (staging) {
        ok = WritePreviewValue(player + STATE_SUBFRAME_COUNTER_OFFSET,
                               fields.animTimer) && ok;
        ok = WritePreviewValue(player + CURRENT_FRAME_INDEX_OFFSET,
                               fields.frame) && ok;
        ok = WritePreviewValue(player + MOVE_ID_OFFSET, fields.move) && ok;
    }
    return ok;
}

void AbandonJugglePreviewLocked() {
    if (g_s.jugglePreview.refreshActive) {
        const std::uint64_t token = g_s.jugglePreview.refreshToken;
        g_s.jugglePreview.refreshActive = false;
        g_s.jugglePreview.refreshToken = 0;
        (void)PauseIntegration::EndMenuSurfaceRefresh(
            PauseIntegration::MenuSurface::TutorialPage, token);
        if (g_s.frozenLeaseHeld &&
            !PauseIntegration::IsPausedOrFrozen()) {
            PauseIntegration::ReassertMenuSurfacePause(
                PauseIntegration::MenuSurface::TutorialPage);
        }
        g_s.freezeConfirmed = g_s.frozenLeaseHeld &&
            PauseIntegration::IsPausedOrFrozen();
    }
    g_s.jugglePreview = JugglePreviewLease{};
}

bool WaitForBattleBatchCompletionLocked(std::uint32_t targetBatch) {
    constexpr DWORD kTimeoutMs = 50;
    const DWORD startTick = ::GetTickCount();
    do {
        if (HudPolicy::BattleBatchReached(
                targetBatch, GetCompletedBattleUpdateBatch())) {
            return true;
        }
        ::Sleep(1);
    } while (static_cast<DWORD>(::GetTickCount() - startTick) < kTimeoutMs);
    return HudPolicy::BattleBatchReached(
        targetBatch, GetCompletedBattleUpdateBatch());
}

bool FinishJugglePreviewRefreshLocked(bool normalizePresentation);

void RestoreJugglePreviewLocked() {
    if (g_s.jugglePreview.refreshActive) {
        (void)FinishJugglePreviewRefreshLocked(false);
    }
    if (!g_s.jugglePreview.held) return;
    const JugglePreviewLease lease = g_s.jugglePreview;
    AbandonJugglePreviewLocked();

    JugglePreviewFields current;
    const uintptr_t currentP2 = GetPlayerBase(2);
    if (GetRuntimeLifecycleGeneration() != lease.lifecycleGeneration ||
        currentP2 != lease.playerBase ||
        !ReadJugglePreviewFields(currentP2, current) ||
        !JugglePreviewFieldsEqual(current, lease.applied)) {
        LogOut("[TUTORIAL][JUGGLE_PREVIEW] restore skipped; player state "
               "was replaced by a newer owner", true);
        return;
    }
    const bool restored =
        WriteJugglePreviewFields(currentP2, lease.saved, false);
    LogOut(std::string("[TUTORIAL][JUGGLE_PREVIEW] restore ") +
           (restored ? "complete" : "failed"), true);
}

bool BuildJugglePreviewOutline(uintptr_t player,
                               const JugglePreviewFields& fields,
                               HudPolicy::Rect& outline) {
    int cameraX = 0;
    int cameraY = 0;
    std::uint8_t facing = 0;
    if (!player ||
        !std::isfinite(fields.x) || !std::isfinite(fields.y) ||
        !CollisionDisplay::ProbeBattleCameraOffsets(&cameraX, &cameraY) ||
        !SafeReadMemory(player + FACING_DIRECTION_OFFSET,
                        &facing, sizeof(facing)) ||
        !HudPolicy::IsKnownFacingByte(facing)) {
        return false;
    }
    const int width = HudPolicy::JuggleWidth(fields.untech);
    if (width <= 0) return false;
    // Mirrors EFZ's HUD routine at 0x00761B91: player doubles become integer native pixels before
    // camera offsets are added, and the one-pixel rows widen a left-facing gauge by two pixels.
    const int originX = cameraX + static_cast<int>(fields.x);
    const int left = HudPolicy::FacesRight(facing)
        ? originX : originX - width - 2;
    const int top = cameraY + static_cast<int>(fields.y) + 209;
    constexpr float margin = 3.0f;
    outline = {
        static_cast<float>(left * 2) - margin,
        static_cast<float>(top * 2) - margin,
        static_cast<float>((width + 2) * 2) + margin * 2.0f,
        10.0f + margin * 2.0f,
    };
    const HudPolicy::Rect rail = HudPolicy::JugglePreviewRail();
    return std::isfinite(outline.x) && std::isfinite(outline.y) &&
           std::isfinite(outline.w) && std::isfinite(outline.h) &&
           outline.x >= rail.x && outline.y >= rail.y &&
           outline.x + outline.w <= rail.x + rail.w &&
           outline.y + outline.h <= rail.y + rail.h;
}

bool IsJugglePreviewRefreshEnvelope(const JugglePreviewLease& lease,
                                    const JugglePreviewFields& current) {
    const double dx = current.x - lease.applied.x;
    const double dy = current.y - lease.applied.y;
    const double absDx = dx < 0.0 ? -dx : dx;
    const double absDy = dy < 0.0 ? -dy : dy;
    return current.move >= LAUNCHED_HITSTUN_START &&
           current.move <= LAUNCHED_HITSTUN_END &&
           current.y < 0.0 && absDx <= 32.0 && absDy <= 64.0 &&
           current.untech > 0 && current.untech <= lease.applied.untech &&
           current.wallbounce == 0;
}

// Restore only while the same world/player still owns the lease and the
// battle thread is paused.
bool SuppressAndReleaseJugglePreviewLocked(const char* reason,
                                           bool restoreOwnedState) {
    const JugglePreviewLease lease = g_s.jugglePreview;
    bool restored = false;
    if (restoreOwnedState && lease.held &&
        GetRuntimeLifecycleGeneration() == lease.lifecycleGeneration &&
        GetPlayerBase(2) == lease.playerBase &&
        PauseIntegration::IsPausedOrFrozen()) {
        restored = WriteJugglePreviewFields(
            lease.playerBase, lease.saved, false);
    }
    AbandonJugglePreviewLocked();
    g_s.jugglePreviewSuppressedPage = g_s.page;
    LogOut(std::string("[TUTORIAL][JUGGLE_PREVIEW] ") + reason +
           "; preview disabled for this page, rollback=" +
           (restored ? "complete" :
            (restoreOwnedState ? "not-owned" : "unsafe")), true);
    return restored;
}

bool FinishJugglePreviewRefreshLocked(bool normalizePresentation) {
    if (!g_s.jugglePreview.refreshActive) return true;

    const std::uint64_t token = g_s.jugglePreview.refreshToken;
    g_s.jugglePreview.refreshActive = false;
    g_s.jugglePreview.refreshToken = 0;
    bool paused = PauseIntegration::EndMenuSurfaceRefresh(
        PauseIntegration::MenuSurface::TutorialPage, token);
    if (!paused && g_s.frozenLeaseHeld) {
        paused = PauseIntegration::IsPausedOrFrozen();
        if (!paused) {
            PauseIntegration::ReassertMenuSurfacePause(
                PauseIntegration::MenuSurface::TutorialPage);
            paused = PauseIntegration::IsPausedOrFrozen();
        }
    }
    if (!paused) {
        (void)SuppressAndReleaseJugglePreviewLocked(
            "physical pause could not be reacquired", false);
        return false;
    }

    // Quiesce the in-flight battle call before touching player fields.
    g_s.freezeConfirmed = false;
    const std::uint32_t settleTarget = GetCurrentBattleUpdateBatch();
    if (!WaitForBattleBatchCompletionLocked(settleTarget)) {
        (void)SuppressAndReleaseJugglePreviewLocked(
            "refresh settle timed out", false);
        return false;
    }

    if (GetRuntimeLifecycleGeneration() !=
            g_s.jugglePreview.lifecycleGeneration ||
        GetPlayerBase(2) != g_s.jugglePreview.playerBase) {
        (void)SuppressAndReleaseJugglePreviewLocked(
            "world ownership changed", false);
        return false;
    }
    g_s.freezeConfirmed = g_s.frozenLeaseHeld &&
        PauseIntegration::IsPausedOrFrozen();
    if (!g_s.freezeConfirmed) {
        (void)SuppressAndReleaseJugglePreviewLocked(
            "physical pause was lost at the refresh settle barrier", false);
        return false;
    }

    JugglePreviewFields observed;
    if (!ReadJugglePreviewFields(g_s.jugglePreview.playerBase, observed) ||
        !IsJugglePreviewRefreshEnvelope(g_s.jugglePreview, observed)) {
        (void)SuppressAndReleaseJugglePreviewLocked(
            "refresh left the staged hit-reaction envelope", true);
        return false;
    }

    if (!normalizePresentation) {
        g_s.jugglePreview.applied = observed;
        return true;
    }

    g_s.jugglePreview.applied = observed;
    g_s.jugglePreview.outlineValid = BuildJugglePreviewOutline(
        g_s.jugglePreview.playerBase, observed,
        g_s.jugglePreview.outline);
    if (!g_s.jugglePreview.outlineValid) {
        (void)SuppressAndReleaseJugglePreviewLocked(
            "refreshed gauge projected outside its preview rail", true);
        return false;
    }
    LogOut("[TUTORIAL][JUGGLE_PREVIEW] refresh complete batches=" +
           std::to_string(static_cast<std::uint32_t>(
               GetCompletedBattleUpdateBatch() -
               g_s.jugglePreview.refreshStartBatch)) +
           " move=" + std::to_string(observed.move) +
           " frame=" + std::to_string(observed.frame) +
           " timer=" + std::to_string(observed.animTimer) +
           " untech=" + std::to_string(observed.untech) +
           " x=" + std::to_string(observed.x) +
           " y=" + std::to_string(observed.y) +
           " outline=" + std::to_string(g_s.jugglePreview.outline.x) +
           "," + std::to_string(g_s.jugglePreview.outline.y) +
           "," + std::to_string(g_s.jugglePreview.outline.w) +
           "," + std::to_string(g_s.jugglePreview.outline.h), true);
    return true;
}

bool StageJugglePreviewLocked(HudPolicy::HudFocus focus) {
    if (!HudPolicy::IsJuggleFocus(focus)) {
        RestoreJugglePreviewLocked();
        return false;
    }
    if (g_s.jugglePreviewSuppressedPage == g_s.page) {
        return false;
    }
    if (g_s.jugglePreview.held && g_s.jugglePreview.focus == focus) return true;
    if (!g_s.freezeConfirmed || !PauseIntegration::IsPausedOrFrozen()) {
        RestoreJugglePreviewLocked();
        return false;
    }
    RestoreJugglePreviewLocked();

    // Never stage synthetic fields until the pause-crossing battle batch
    // completes.
    const std::uint32_t pauseSettleTarget = GetCurrentBattleUpdateBatch();
    if (!WaitForBattleBatchCompletionLocked(pauseSettleTarget) ||
        !PauseIntegration::IsPausedOrFrozen()) {
        LogOut("[TUTORIAL][JUGGLE_PREVIEW] staging deferred; battle update "
               "did not settle behind the presentation pause", true);
        return false;
    }

    const uintptr_t player = GetPlayerBase(2);
    JugglePreviewFields saved;
    if (!player || !ReadJugglePreviewFields(player, saved)) return false;

    int cameraX = 0;
    int cameraY = 0;
    std::uint8_t facing = 0;
    if (!CollisionDisplay::ProbeBattleCameraOffsets(&cameraX, &cameraY) ||
        !SafeReadMemory(player + FACING_DIRECTION_OFFSET,
                        &facing, sizeof(facing))) {
        return false;
    }
    if (!HudPolicy::IsKnownFacingByte(facing)) {
        g_s.jugglePreviewSuppressedPage = g_s.page;
        LogOut("[TUTORIAL][JUGGLE_PREVIEW] staging rejected; unknown EFZ "
               "facing byte=" + std::to_string(facing), true);
        return false;
    }

    JugglePreviewFields applied = saved;
    applied.move = LAUNCHED_HITSTUN_START;
    applied.frame = 0;
    applied.animTimer = 0;
    applied.untech = HudPolicy::JugglePreviewUntech(focus);
    applied.wallbounce = 0;
    applied.xVelocity = 0.0;
    applied.yVelocity = 0.0;
    const int width = HudPolicy::JuggleWidth(applied.untech);
    constexpr int desiredGaugeCenterNative = 254;
    const double desiredOrigin = HudPolicy::FacesRight(facing)
        ? static_cast<double>(desiredGaugeCenterNative - width / 2)
        : static_cast<double>(desiredGaugeCenterNative + width / 2);
    applied.x = desiredOrigin - static_cast<double>(cameraX);
    applied.y = 145.0 - 209.0 - static_cast<double>(cameraY);

    // Battle X clamps to 20..619 and Y<0 is airborne; reject impossible
    // projections before touching P2.
    HudPolicy::Rect plannedOutline{};
    const bool savedFinite =
        std::isfinite(saved.x) && std::isfinite(saved.y) &&
        std::isfinite(saved.xVelocity) && std::isfinite(saved.yVelocity);
    const bool appliedFinite =
        std::isfinite(applied.x) && std::isfinite(applied.y) &&
        std::isfinite(applied.xVelocity) && std::isfinite(applied.yVelocity);
    if (!savedFinite || !appliedFinite ||
        applied.x < 20.0 || applied.x > 619.0 ||
        applied.y >= 0.0 || applied.y < -1024.0 ||
        !BuildJugglePreviewOutline(player, applied, plannedOutline)) {
        g_s.jugglePreviewSuppressedPage = g_s.page;
        LogOut("[TUTORIAL][JUGGLE_PREVIEW] staging rejected before write; "
               "double position or on-screen projection was invalid", true);
        return false;
    }

    if (!WriteJugglePreviewFields(player, applied, true)) {
        (void)WriteJugglePreviewFields(player, saved, false);
        g_s.jugglePreviewSuppressedPage = g_s.page;
        LogOut("[TUTORIAL][JUGGLE_PREVIEW] staging failed; original fields "
               "restored", true);
        return false;
    }

    g_s.jugglePreview.held = true;
    g_s.jugglePreview.playerBase = player;
    g_s.jugglePreview.lifecycleGeneration =
        GetRuntimeLifecycleGeneration();
    g_s.jugglePreview.focus = focus;
    g_s.jugglePreview.saved = saved;
    g_s.jugglePreview.applied = applied;
    g_s.jugglePreview.outlineValid = true;
    g_s.jugglePreview.outline = plannedOutline;
    bool refreshStarted = false;
    if (g_s.phase == Phase::Intro &&
        g_efzWindowActive.load(std::memory_order_relaxed) &&
        !::Mission::PauseMenu::IsOpen()) {
        std::uint64_t refreshToken = 0;
        if (PauseIntegration::BeginMenuSurfaceRefresh(
                PauseIntegration::MenuSurface::TutorialPage,
                refreshToken)) {
            g_s.jugglePreview.refreshStartBatch =
                GetCurrentBattleUpdateBatch();
            g_s.jugglePreview.refreshActive = true;
            g_s.jugglePreview.refreshToken = refreshToken;
            g_s.jugglePreview.refreshWatchdogTicks = 0;
            g_s.freezeConfirmed = false;
            g_s.introWorldAdvanced = true;
            refreshStarted = true;
        }
    }
    LogOut("[TUTORIAL][JUGGLE_PREVIEW] staged P2 untech=" +
           std::to_string(applied.untech) +
           " move=" + std::to_string(applied.move) +
           " outline=" +
           std::to_string(g_s.jugglePreview.outlineValid ? 1 : 0) +
           " refresh=" + std::to_string(refreshStarted ? 1 : 0), true);
    return true;
}

void SetNeutralInputLease(bool on);

void SetFreeze(bool on) {
    if (on) SetNeutralInputLease(true);
    if (on && g_s.jugglePreview.refreshActive) {
        (void)FinishJugglePreviewRefreshLocked(true);
    }
    if (!on) RestoreJugglePreviewLocked();
    if (on == g_s.frozenLeaseHeld) {
        g_s.freezeConfirmed = on && PauseIntegration::IsPausedOrFrozen();
        return;
    }
    g_s.frozenLeaseHeld = on;
    g_s.freezeConfirmed = false;
    if (!on) g_s.freezeReassertCooldown = 0;
    PauseIntegration::OnMenuSurfaceVisibilityChanged(
        PauseIntegration::MenuSurface::TutorialPage, on);
    if (on) {
        g_s.freezeConfirmed = PauseIntegration::IsPausedOrFrozen();
    }
}

void SetNeutralInputLease(bool on) {
    if (on == g_s.neutralInputLeaseHeld) {
        if (on) {
            g_pollOverrideMask[1].store(0, std::memory_order_relaxed);
            SetPollOverridePhysicalObservation(1, true);
            g_pollOverrideActive[1].store(true, std::memory_order_release);
        }
        return;
    }
    if (on) {
        g_s.neutralPollSnapshotValid = true;
        g_s.neutralPollSavedActive =
            g_pollOverrideActive[1].load(std::memory_order_acquire);
        g_s.neutralPollSavedMask =
            g_pollOverrideMask[1].load(std::memory_order_acquire);
        g_s.neutralPollSavedObservation =
            IsPollOverridePhysicalObservationEnabled(1);
        g_s.neutralInputLeaseHeld = true;
        g_pollOverrideMask[1].store(0, std::memory_order_relaxed);
        SetPollOverridePhysicalObservation(1, true);
        g_pollOverrideActive[1].store(true, std::memory_order_release);
        return;
    }

    g_s.neutralInputLeaseHeld = false;
    if (!g_s.neutralPollSnapshotValid) return;
    const bool demoOwnsPoll = ::Mission::Engine::Demo::IsActive();
    const bool curObserve = IsPollOverridePhysicalObservationEnabled(1);
    const uint8_t curMask = g_pollOverrideMask[1].load(std::memory_order_acquire);
    const bool curActive = g_pollOverrideActive[1].load(std::memory_order_acquire);
    if (curObserve) {
        SetPollOverridePhysicalObservation(1, g_s.neutralPollSavedObservation);
    }
    if (!demoOwnsPoll && curMask == 0) {
        g_pollOverrideMask[1].store(g_s.neutralPollSavedMask,
                                    std::memory_order_relaxed);
    }
    if (!demoOwnsPoll && curActive) {
        g_pollOverrideActive[1].store(g_s.neutralPollSavedActive,
                                      std::memory_order_release);
    }
    g_s.neutralPollSnapshotValid = false;
}

void EnsureFreezeLocked() {
    if (g_s.jugglePreview.refreshActive) {
        g_s.freezeConfirmed = false;
        return;
    }
    if (!g_s.frozenLeaseHeld) {
        SetFreeze(true);
        return;
    }
    SetNeutralInputLease(true);
    g_s.freezeConfirmed = PauseIntegration::IsPausedOrFrozen();
    if (g_s.freezeConfirmed) {
        g_s.freezeReassertCooldown = 0;
        return;
    }
    RestoreJugglePreviewLocked();
    if (g_s.freezeReassertCooldown > 0) {
        --g_s.freezeReassertCooldown;
        return;
    }

    LogOut("[TUTORIAL] presentation pause was lost; reacquiring", true);
    PauseIntegration::ReassertMenuSurfacePause(
        PauseIntegration::MenuSurface::TutorialPage);
    g_s.freezeConfirmed = PauseIntegration::IsPausedOrFrozen();
    g_s.freezeReassertCooldown = 30;
}

bool TickJugglePreviewRefreshLocked() {
    if (!g_s.jugglePreview.refreshActive) return false;
    ++g_s.jugglePreview.refreshWatchdogTicks;
    g_s.freezeConfirmed = false;

    HudPolicy::HudFocus pageFocus = HudPolicy::HudFocus::None;
    if (g_s.uiModel && g_s.page >= 0 &&
        g_s.page < static_cast<int>(g_s.uiModel->pages.size())) {
        pageFocus = g_s.uiModel->pages[
            static_cast<size_t>(g_s.page)].hudFocus;
    }
    const bool worldReplaced =
        GetRuntimeLifecycleGeneration() !=
            g_s.jugglePreview.lifecycleGeneration;
    const bool ownerChanged = GetPlayerBase(2) !=
        g_s.jugglePreview.playerBase;
    const bool surfaceChanged = g_s.phase != Phase::Intro ||
        pageFocus != g_s.jugglePreview.focus;
    const bool interrupted = ::Mission::PauseMenu::IsOpen() ||
        !g_efzWindowActive.load(std::memory_order_relaxed);

    if (worldReplaced) {
        LogOut("[TUTORIAL][JUGGLE_PREVIEW] refresh abandoned after "
               "runtime lifecycle replacement", true);
        AbandonJugglePreviewLocked();
        return true;
    }
    if (ownerChanged || surfaceChanged || interrupted) {
        const bool signatureOwned =
            FinishJugglePreviewRefreshLocked(false);
        if (signatureOwned) RestoreJugglePreviewLocked();
        LogOut("[TUTORIAL][JUGGLE_PREVIEW] refresh cancelled before "
               "completion", true);
        return true;
    }

    const std::uint32_t completed = GetCompletedBattleUpdateBatch();
    const bool updatesComplete = HudPolicy::JugglePreviewRefreshComplete(
        g_s.jugglePreview.refreshStartBatch, completed);
    const bool timedOut = g_s.jugglePreview.refreshWatchdogTicks >=
        HudPolicy::JugglePreviewRefreshWatchdogTicks();
    if (!updatesComplete && !timedOut) return true;

    if (timedOut && !updatesComplete) {
        LogOut("[TUTORIAL][JUGGLE_PREVIEW] refresh watchdog expired; "
               "reacquiring without further world advance", true);
    }
    (void)FinishJugglePreviewRefreshLocked(true);
    return true;
}

const ::Mission::LessonTask* CurrentTaskLocked() {
    if (g_s.task < 0 || g_s.task >= static_cast<int>(g_s.lesson.lesson.tasks.size())) return nullptr;
    return &g_s.lesson.lesson.tasks[g_s.task];
}

constexpr int kEpisodeDashMinTicks = 30;
constexpr int kEpisodeSettleTicks  = 30;
constexpr int kEpisodeAttackTicks  = 45;
constexpr int kEpisodeFreezeHold   = 12;
constexpr int kJumpRiseTicks = 66;

const ::Mission::DummyEpisode* CurrentEpisodeLocked() {
    const ::Mission::LessonTask* t = CurrentTaskLocked();
    if (!t || t->script.empty()) return nullptr;
    for (const auto& e : g_s.lesson.lesson.episodes) {
        if (e.id == t->script) return &e;
    }
    return nullptr;
}

bool ApplyTaskPositionLocked(const ::Mission::LessonTask* t, std::string& errorOut) {
    if (!t || (!t->hasPos && !t->hasDummyPos)) return true;
    const uintptr_t base = GetEFZBase();
    if (!base) {
        errorOut = "The lesson could not reach the game state needed to place the fighters.";
        return false;
    }
    if (t->hasPos) {
        if (!TrySetPlayerPosition(base, EFZ_BASE_OFFSET_P1,
                                  t->posX, t->posY, true)) {
            errorOut = "The lesson could not place your character for this task.";
            return false;
        }
    }
    if (t->hasDummyPos) {
        if (!TrySetPlayerPosition(base, EFZ_BASE_OFFSET_P2,
                                  t->dummyPosX, t->dummyPosY, true)) {
            errorOut = "The lesson could not place the dummy for this task.";
            return false;
        }
    }
    return true;
}

// task_state_seeds: applied when the task becomes live. RF is the +0x118
// double, IC color via SetICColorPlayer (+0x120 int), SP u16, HP i32.
bool ApplyTaskSeedsLocked(const ::Mission::LessonTask* t, std::string& errorOut) {
    if (!t) return true;
    auto apply = [&](int player, const ::Mission::TaskStateSeed& s) {
        if (!s.has) return true;
        const uintptr_t base = GetPlayerBase(player);
        const char* side = player == 1 ? "your character" : "the dummy";
        if (!base) {
            errorOut = std::string("The lesson could not reach ") + side +
                " to prepare this task.";
            return false;
        }
        auto writeExact = [&](uintptr_t address, const void* value, size_t size,
                              const char* field) {
            unsigned char observed[sizeof(double)] = {};
            const bool ok = address && size <= sizeof(observed) &&
                SafeWriteMemory(address, value, size) &&
                SafeReadMemory(address, observed, size) &&
                std::memcmp(observed, value, size) == 0;
            if (!ok && errorOut.empty()) {
                errorOut = std::string("The lesson could not set ") + side +
                    "'s " + field + " for this task.";
            }
            return ok;
        };
        bool ok = true;
        if (s.rf >= 0) {
            const double v = static_cast<double>(s.rf);
            ok = writeExact(base + RF_OFFSET, &v, sizeof(v), "RF Gauge") && ok;
        }
        if (s.blueIC >= 0) {
            const bool wrote = SetICColorPlayer(player, s.blueIC != 0);
            if (!wrote && errorOut.empty()) {
                errorOut = std::string("The lesson could not set ") + side +
                    "'s Instant Charge color for this task.";
            }
            ok = wrote && ok;
        }
        if (s.meter >= 0) {
            const uint16_t v = static_cast<uint16_t>(s.meter);
            ok = writeExact(base + METER_OFFSET, &v, sizeof(v), "SP meter") && ok;
        }
        if (s.hp > 0) {
            const int v = s.hp;
            ok = writeExact(base + HP_OFFSET, &v, sizeof(v), "health") && ok;
        }
        if (s.guard >= 0) {
            // Guard gauge is the +0x134 FLOAT (0..360); see mission_setup.
            const float v = static_cast<float>(s.guard);
            ok = writeExact(base + PLAYER_GUARD_GAUGE_OFFSET, &v, sizeof(v),
                            "Guard Gauge") && ok;
        }
        LogOut("[TUTORIAL][TASK_SEED] player=" + std::to_string(player) +
               " rf=" + std::to_string(s.rf) +
               " blueIC=" + std::to_string(s.blueIC) +
               " meter=" + std::to_string(s.meter) +
               " hp=" + std::to_string(s.hp) +
               " guard=" + std::to_string(s.guard) +
               " ok=" + std::to_string(ok ? 1 : 0), true);
        return ok;
    };
    const bool p1Ok = apply(1, t->playerSeed);
    const bool p2Ok = apply(2, t->dummySeed);
    return p1Ok && p2Ok;
}

int ActionToMotion(const std::string& a) {
    struct Row { const char* name; int motion; };
    static const Row kRows[] = {
        {"5A", MOTION_5A}, {"5B", MOTION_5B}, {"5C", MOTION_5C},
        {"2A", MOTION_2A}, {"2B", MOTION_2B}, {"2C", MOTION_2C},
        {"6A", MOTION_6A}, {"6B", MOTION_6B}, {"6C", MOTION_6C},
        {"j.A", MOTION_JA}, {"j.B", MOTION_JB}, {"j.C", MOTION_JC},
        {"236A", MOTION_236A}, {"236B", MOTION_236B}, {"236C", MOTION_236C},
        {"623A", MOTION_623A}, {"623B", MOTION_623B}, {"623C", MOTION_623C},
        {"214A", MOTION_214A}, {"214B", MOTION_214B}, {"214C", MOTION_214C},
        {"22A", MOTION_22A}, {"22B", MOTION_22B}, {"22C", MOTION_22C},
        {"41236A", MOTION_41236A}, {"41236B", MOTION_41236B}, {"41236C", MOTION_41236C},
    };
    for (const auto& r : kRows) if (a == r.name) return r.motion;
    return -1;
}

bool IsAirAction(const std::string& a) {
    return a.size() >= 3 && a[0] == 'j' && a[1] == '.';
}

int PredicateMoveId(const std::string& pred) {
    int moveId = -1;
    return ::Mission::TutorialEpisodePolicy::ParseP1MovePredicate(pred, moveId)
        ? moveId : -1;
}

bool EpisodeBehaviorReady(const ::Mission::DummyEpisode* e) {
    if (!e) return false;
    if (!e->variants.empty()) {
        bool kindInPool = false;
        for (const auto& v : e->variants) {
            if (v != "idle" && v != "block" && v != "rg") return false;
            if (v == e->kind) kindInPool = true;
        }
        return kindInPool;
    }
    if (e->kind == "idle" || e->kind == "block" || e->kind == "rg") {
        return true;
    }
    if (e->kind == "guard") {
        return e->clip == "stand" || e->clip == "crouch";
    }
    if (e->kind == "block_answer" || e->kind == "rg_answer") {
        return ::Mission::TutorialEpisodePolicy::IsInjectableAction(e->action) &&
               e->action != "jump" &&
               !::Mission::TutorialEpisodePolicy::IsDashNormalAction(e->action) &&
               !::Mission::TutorialEpisodePolicy::IsContactChainAction(e->action) &&
               ActionToMotion(e->action) >= 0;
    }
    if (!e->script.empty()) {
        if (e->kind != "macro" || e->start != "taskArmed" ||
            !::Mission::TutorialEpisodePolicy::IsSupportedApproach(e->approach) ||
            !::Mission::TutorialEpisodePolicy::IsSupportedTelegraph(e->telegraph) ||
            !::Mission::TutorialEpisodePolicy::IsSupportedRepeatMode(e->repeatMode) ||
            e->pausePolicy != "missionFreeze" || e->completeOn != "actionEnd") {
            return false;
        }
        for (size_t si = 0; si < e->script.size(); ++si) {
            const auto& st = e->script[si];
            if (st.action.empty()) continue;
            if (!::Mission::TutorialEpisodePolicy::IsSupportedScriptAction(
                    st.action, si)) return false;
            if (!::Mission::TutorialEpisodePolicy::IsDashNormalAction(st.action) &&
                ActionToMotion(st.action) < 0) return false;
        }
        return true;
    }
    if (e->kind == "airtech") {
        return e->clip == "forward" || e->clip == "backward";
    }
    if (e->kind != "macro") return false;
    if (!::Mission::TutorialEpisodePolicy::IsInjectableAction(e->action) ||
        !::Mission::TutorialEpisodePolicy::IsSupportedApproach(e->approach) ||
        !::Mission::TutorialEpisodePolicy::IsSupportedTelegraph(e->telegraph) ||
        !::Mission::TutorialEpisodePolicy::IsSupportedRepeatMode(e->repeatMode)) return false;
    const bool scriptedDashNormal =
        ::Mission::TutorialEpisodePolicy::IsDashNormalAction(e->action);
    const bool scriptedContactChain =
        ::Mission::TutorialEpisodePolicy::IsContactChainAction(e->action);
    if (scriptedDashNormal || scriptedContactChain) {
        return e->start == "taskArmed" && e->approach == "none";
    }
    const bool jumpOnly = e->action == "jump";
    if (!jumpOnly && ActionToMotion(e->action) < 0) return false;
    if (jumpOnly && e->start != "taskArmed") return false;
    if (e->start == "taskArmed")   return true;
    if (e->start == "playerState") return PredicateMoveId(e->predicate) >= 0;
    if (e->start == "trigger") {
        if (!::Mission::TutorialEpisodePolicy::IsSupportedTrigger(e->trigger)) {
            return false;
        }
        if (e->trigger == "onWakeup") {
            int mappedAction = 0, mappedStrength = 0;
            return ::Mission::TutorialEpisodePolicy::WakeAutoActionForAction(
                e->action, mappedAction, mappedStrength);
        }
        return true;
    }
    return false;
}

bool EpisodeExpectedAttack(const ::Mission::DummyEpisode* episode,
                           short moveId) {
    return episode &&
        ::Mission::TutorialEpisodePolicy::ExpectedAttackMatches(
            episode->expectedMoveIds, moveId);
}

bool DummyTriggerEdge(const std::string& trig, short prev, short cur) {
    if (prev < 0) return false;
    if (trig == "afterBlock")   return IsBlockstunState(prev) && !IsBlockstunState(cur) && cur <= 3;
    if (trig == "afterHitstun") return IsHitstun(prev) && !IsHitstun(cur) && cur >= 0 && cur <= 3;
    if (trig == "onRG")         return IsRecoilGuard(cur) && !IsRecoilGuard(prev);
    if (trig == "afterAirtech") return IsAirtech(prev) && !IsAirtech(cur);
    if (trig == "onWakeup")     return ::Mission::TutorialStatePolicy::IsWakeExit(
                                      IsGroundtech(prev), IsGroundtech(cur));
    return false;
}

short ReadP2MotionTokenForEpisodeDiag() {
    const uintptr_t p2 = GetPlayerBase(2);
    short token = 99;
    if (p2) {
        (void)SafeReadMemory(p2 + MOTION_TOKEN_OFFSET, &token, sizeof(token));
    }
    return token;
}

void ResetEpisodeDiagnosticsLocked() {
    g_s.episodeDiagSnapshotValid = false;
    g_s.episodeDiagP1Move = -1;
    g_s.episodeDiagP2Move = -1;
    g_s.episodeDiagP2Frame = -1;
    g_s.episodeDiagLoggedP2Move = -32768;
    g_s.episodeDiagLoggedPhase = -1;
    g_s.episodeDiagLoggedScriptStep = -2;
    g_s.episodeDiagLoggedStepPhase = -1;
    g_s.episodeDiagLoggedFreeze = false;
    g_s.episodeDiagLoggedCueBlocked = false;
    g_s.episodeDiagMovePath.clear();
    ResetInputPollOverrideHitCount(2);
}

std::string EpisodeDriveContextLocked(const ::Mission::DummyEpisode* episode) {
    const ::Mission::LessonTask* task = CurrentTaskLocked();
    std::ostringstream out;
    const MotionQueueSnapshot queue = GetMotionQueueSnapshot(2);
    const int desiredMask = queue.hasCurrentMask
        ? static_cast<int>(queue.currentMask)
        : static_cast<int>(ImmediateInput::GetCurrentDesired(2));
    out << "lesson=" << g_s.lesson.lessonId
        << " task=" << (task ? task->id : std::string("?"))
        << " episode=" << (episode ? episode->id : std::string("?"))
        << " cycle=" << g_s.episodeCycleSerial
        << " phase=" << g_s.episodePhase
        << " timer=" << g_s.episodeTimer
        << " scriptStep=" << g_s.episodeScriptStep
        << " stepPhase=" << g_s.scriptStepPhase
        << " gap=" << (g_s.scriptGapOpen ? 1 : 0)
        << " p1Move=" << g_s.episodeDiagP1Move
        << " p2Move=" << g_s.episodeDiagP2Move
        << " p2Frame=" << g_s.episodeDiagP2Frame
        << " p2Token=" << ReadP2MotionTokenForEpisodeDiag()
        << " worldFreeze=" << (g_s.episodeDiagLoggedFreeze ? 1 : 0)
        << " inputFreeze=" << (g_s.episodeFreeze ? 1 : 0)
        << " cueBlocked=" << (g_s.episodeCueBlockedThisTick ? 1 : 0)
        << " queue=" << (queue.active ? 1 : 0)
        << ':' << queue.index << '/' << queue.size
        << " queueMotion=" << queue.motionType
        << " desiredMask=" << desiredMask
        << " leases={p2:" << (g_s.p2ControlToken != 0 ? 1 : 0)
        << ",motion:" << (g_s.motionQueueToken != 0 ? 1 : 0)
        << ",immediate:" << (g_s.immediateInputToken != 0 ? 1 : 0)
        << ",freeze:" << (g_s.bufferFreezeToken != 0 ? 1 : 0) << '}'
        << " pollHits=" << GetInputPollOverrideHitCount(2)
        << " movePath=" << (g_s.episodeDiagMovePath.empty()
               ? std::string("-") : g_s.episodeDiagMovePath);
    return out.str();
}

std::string EpisodeRequestedActionLocked(const ::Mission::DummyEpisode* episode) {
    if (!episode) return "?";
    if (!episode->script.empty()) {
        if (g_s.episodeScriptStep < 0) {
            return episode->approach == "dash" ? "approach:dash" : "script:pending";
        }
        if (g_s.episodeScriptStep < static_cast<int>(episode->script.size())) {
            const auto& step = episode->script[g_s.episodeScriptStep];
            return step.action.empty() ? "gap" : step.action;
        }
        return "script:cooldown";
    }
    return episode->action.empty() ? "-" : episode->action;
}

void ObserveEpisodeDriveLocked(const ::Mission::DummyEpisode* episode,
                               const ::Mission::Engine::Snapshot& snapshot) {
    g_s.episodeDiagSnapshotValid = true;
    g_s.episodeDiagP1Move = snapshot.p1Move;
    g_s.episodeDiagP2Move = snapshot.p2Move;
    g_s.episodeDiagP2Frame = snapshot.p2FrameIdx;

    const bool moveChanged =
        snapshot.p2Move != g_s.episodeDiagLoggedP2Move;
    if (moveChanged && g_s.episodeDiagMovePath.size() < 240) {
        if (!g_s.episodeDiagMovePath.empty()) g_s.episodeDiagMovePath += '>';
        g_s.episodeDiagMovePath += std::to_string(snapshot.p2Move);
        g_s.episodeDiagMovePath += '@';
        g_s.episodeDiagMovePath += std::to_string(snapshot.p2FrameIdx);
    }

    const bool stateChanged = moveChanged ||
        g_s.episodePhase != g_s.episodeDiagLoggedPhase ||
        g_s.episodeScriptStep != g_s.episodeDiagLoggedScriptStep ||
        g_s.scriptStepPhase != g_s.episodeDiagLoggedStepPhase ||
        snapshot.freezeActive != g_s.episodeDiagLoggedFreeze ||
        g_s.episodeCueBlockedThisTick != g_s.episodeDiagLoggedCueBlocked;

    g_s.episodeDiagLoggedP2Move = snapshot.p2Move;
    g_s.episodeDiagLoggedPhase = g_s.episodePhase;
    g_s.episodeDiagLoggedScriptStep = g_s.episodeScriptStep;
    g_s.episodeDiagLoggedStepPhase = g_s.scriptStepPhase;
    g_s.episodeDiagLoggedFreeze = snapshot.freezeActive;
    g_s.episodeDiagLoggedCueBlocked = g_s.episodeCueBlockedThisTick;

    if (stateChanged && detailedLogging.load()) {
        LogOut("[TUTORIAL][EPISODE_DRIVE] event=state " +
               EpisodeDriveContextLocked(episode), true);
    }
}

void LogEpisodeInjectionLocked(const char* event, int motion, int button,
                               bool accepted) {
    const ::Mission::DummyEpisode* episode = CurrentEpisodeLocked();
    std::ostringstream out;
    out << "[TUTORIAL][EPISODE_DRIVE] event=" << event
        << " authored=" << EpisodeRequestedActionLocked(episode)
        << " requested=" << GetMotionTypeName(motion)
        << " motion=" << motion
        << " button=" << button
        << " accepted=" << (accepted ? 1 : 0) << ' '
        << EpisodeDriveContextLocked(episode);
    LogOut(out.str(), true);
}

void ReleaseDummyFreezeLocked() {
    if (!g_s.episodeFreeze) return;
    if (g_s.bufferFreezeToken != 0)
        StopTutorialBufferFreeze(g_s.bufferFreezeToken);
    g_s.episodeFreeze = false;
}

bool QueueDummyMotionLocked(int motion, int buttonMask = 0) {
    const bool accepted = g_s.motionQueueToken != 0 &&
        QueueTutorialMotionInput(2, g_s.motionQueueToken, motion, buttonMask);
    const int loggedButton = motion >= MOTION_5A && motion <= MOTION_4D &&
        buttonMask == 0 ? static_cast<int>(DetermineButtonFromMotionType(motion))
                        : buttonMask;
    LogEpisodeInjectionLocked("motion_queue", motion, loggedButton, accepted);
    return accepted;
}

bool PressDummyImmediateLocked(uint8_t mask, int ticks) {
    const bool accepted = g_s.immediateInputToken != 0 &&
        ImmediateInput::PressForTutorial(2, g_s.immediateInputToken, mask, ticks);
    std::ostringstream out;
    out << "[TUTORIAL][EPISODE_DRIVE] event=immediate_press"
        << " mask=" << static_cast<unsigned int>(mask)
        << " ticks=" << ticks
        << " accepted=" << (accepted ? 1 : 0) << ' '
        << EpisodeDriveContextLocked(CurrentEpisodeLocked());
    LogOut(out.str(), true);
    return accepted;
}

void InjectDummyMoveLocked(int motion) {
    if (motion >= MOTION_236A) {
        ReleaseDummyFreezeLocked();
        uint8_t btn = DetermineButtonFromMotionType(motion);
        if (btn == 0) btn = GAME_INPUT_A;
        const bool accepted = g_s.bufferFreezeToken != 0 &&
            FreezeBufferForTutorial(g_s.bufferFreezeToken, 2, motion,
                                    static_cast<int>(btn));
        g_s.episodeFreeze = accepted;
        LogEpisodeInjectionLocked("buffered_special", motion,
                                  static_cast<int>(btn), accepted);
    } else {
        (void)QueueDummyMotionLocked(motion, 0);
    }
}

void ReleaseEpisodeOwnersLocked() {
    if (g_s.wakeAutoToken != 0) {
        ReleaseTutorialP2WakeProducer(g_s.wakeAutoToken);
        g_s.wakeAutoToken = 0;
    }
    ReleaseDummyFreezeLocked();
    if (g_s.bufferFreezeToken != 0) {
        ReleaseTutorialBufferFreeze(g_s.bufferFreezeToken);
        g_s.bufferFreezeToken = 0;
    }
    if (g_s.motionQueueToken != 0) {
        ReleaseTutorialMotionQueue(2, g_s.motionQueueToken);
        g_s.motionQueueToken = 0;
    }
    if (g_s.immediateInputToken != 0) {
        ImmediateInput::ReleaseTutorialLease(2, g_s.immediateInputToken);
        g_s.immediateInputToken = 0;
    }
    if (g_s.injectImmediateOnlyTouched &&
        !g_injectImmediateOnly[2].load(std::memory_order_acquire)) {
        g_injectImmediateOnly[2].store(g_s.injectImmediateOnlySaved,
                                       std::memory_order_release);
    }
    g_s.injectImmediateOnlyTouched = false;
    if (g_s.p2ControlToken != 0) {
        if (ReleaseTutorialP2Control(g_s.p2ControlToken)) {
            g_s.p2ControlToken = 0;
        }
    }

    if (g_s.autoBlockTouched) {
        (void)RestorePracticeAutoBlockIfUnchanged(
            g_s.autoBlockSaved, g_s.autoBlockAppliedGeneration,
            g_s.autoBlockAppliedValue,
            "tutorial episode release");
    }
    g_s.autoBlockTouched = false;
    g_s.autoBlockAppliedValue = false;
    if (g_s.adaptiveStanceTouched &&
        GetAdaptiveStanceEnabled() == g_s.adaptiveStanceApplied) {
        SetAdaptiveStanceEnabled(g_s.adaptiveStanceSaved);
    }
    g_s.adaptiveStanceTouched = false;
    g_s.adaptiveStanceSaved = false;
    g_s.adaptiveStanceApplied = false;
    if (g_s.practiceBlockModeTouched) {
        int currentMode = 0;
        if (GetPracticeBlockMode(currentMode) &&
            currentMode == g_s.practiceBlockModeApplied) {
            (void)SetPracticeBlockMode(g_s.practiceBlockModeSaved);
        }
    }
    g_s.practiceBlockModeTouched = false;
    g_s.practiceBlockModeSaved = 0;
    g_s.practiceBlockModeApplied = 0;
    if (g_s.alwaysRGTouched) {
        (void)AlwaysRG::SetEnabledIfGeneration(
            g_s.alwaysRGSaved, g_s.alwaysRGAppliedGeneration);
    }
    g_s.alwaysRGTouched = false;
    if (g_s.randomBlockTouched) {
        (void)RandomBlock::SetEnabledIfGeneration(
            g_s.randomBlockSaved, g_s.randomBlockAppliedGeneration);
    }
    g_s.randomBlockTouched = false;
    if (g_s.airtechTouched) {
        if (autoAirtechEnabled.load() &&
            autoAirtechDirection.load() == g_s.airtechAppliedDirection &&
            autoAirtechDelay.load() == g_s.airtechAppliedDelay) {
            autoAirtechDirection.store(g_s.airtechSavedDirection);
            autoAirtechDelay.store(g_s.airtechSavedDelay);
            autoAirtechEnabled.store(g_s.airtechSavedEnabled);
        }
    }
    g_s.airtechTouched = false;
    if (g_s.autoBlockControllerToken != 0) {
        ReleaseTutorialAutoBlockController(g_s.autoBlockControllerToken);
        g_s.autoBlockControllerToken = 0;
    }
}

void BeginEpisodeCueLocked(const ::Mission::DummyEpisode* episode) {
    constexpr int kImmediateCueDisplayTicks = 192;
    const bool haveText = episode && !episode->cueText.empty();
    g_s.episodeCueTicks = haveText &&
        ::Mission::TutorialEpisodePolicy::HasTimedCue(
            episode->cueText, episode->cueLead)
        ? episode->cueLead : 0;
    g_s.episodeCueDisplayTicks = haveText
        ? (episode->cueLead > 0 ? episode->cueLead
                                : kImmediateCueDisplayTicks)
        : 0;
}

bool TickEpisodeCueLocked(const ::Mission::DummyEpisode* episode,
                          const ::Mission::Engine::Snapshot& snapshot) {
    if (!episode) return false;
    const bool blocksEpisode = g_s.episodeCueTicks > 0;
    g_s.episodeCueBlockedThisTick = blocksEpisode;
    if (!snapshot.freezeActive) {
        if (g_s.episodeCueTicks > 0 && --g_s.episodeCueTicks <= 0) {
            g_s.episodeCueTicks = 0;
        }
        if (g_s.episodeCueDisplayTicks > 0 &&
            --g_s.episodeCueDisplayTicks <= 0) {
            g_s.episodeCueDisplayTicks = 0;
        }
    }
    return blocksEpisode;
}

void StopTaskEpisodeLocked() {
    g_s.episodePlaying = false;
    g_s.episodeFinished = false;
    g_s.episodeCycleSerial = 0;
    g_s.episodePhase = 0;
    g_s.episodeTimer = 0;
    g_s.episodeAnswerRgMove = -1;
    g_s.episodeCueTicks = 0;
    g_s.episodeCueDisplayTicks = 0;
    g_s.episodeCueBlockedThisTick = false;
    g_s.episodeChainIndex = 0;
    g_s.episodeContactFreezeSeen = false;
    g_s.epPrevP2Move = -1;
    g_s.epPrevP2FrameIdx = -1;
    g_s.episodeScriptStep = -1;
    g_s.scriptStepPhase = 0;
    g_s.scriptGapOpen = false;
    ReleaseEpisodeOwnersLocked();
    ResetEpisodeDiagnosticsLocked();
}

void FailEpisodeLeaseLocked(const std::string& reason) {
    LogOut("[TUTORIAL][EPISODE_DRIVE] event=failure reason=\"" + reason +
           "\" " + EpisodeDriveContextLocked(CurrentEpisodeLocked()), true);
    StopTaskEpisodeLocked();
    g_s.phase = Phase::Error;
    g_s.feedbackSuccess = false;
    g_s.feedbackText = reason;
    SetNeutralInputLease(false);
    SetFreeze(true);
    LogOut("[TUTORIAL][INPUT_LEASE] " + reason, true);
}

void FailTaskSetupLocked(const std::string& reason) {
    StopTaskEpisodeLocked();
    g_s.phase = Phase::Error;
    g_s.feedbackSuccess = false;
    g_s.feedbackText = reason.empty()
        ? std::string("The lesson could not prepare this task.") : reason;
    SetNeutralInputLease(false);
    SetFreeze(true);
    LogOut("[TUTORIAL][TASK_SETUP] " + g_s.feedbackText, true);
}

std::string SelectEpisodeVariantLocked(const ::Mission::DummyEpisode& episode) {
    if (episode.variants.empty()) return episode.kind;
    if (g_s.variantSeed == 0) {
        g_s.variantSeed = static_cast<uint32_t>(GetTickCount()) | 1u;
    }

    if (episode.variantGroup.empty()) {
        g_s.variantSeed = g_s.variantSeed * 1664525u + 1013904223u;
        return episode.variants[
            (g_s.variantSeed >> 8) % episode.variants.size()];
    }

    const auto assigned = g_s.episodeVariantAssignments.find(episode.id);
    if (assigned != g_s.episodeVariantAssignments.end()) {
        return assigned->second;
    }

    std::vector<const ::Mission::DummyEpisode*> members;
    for (const auto& candidate : g_s.lesson.lesson.episodes) {
        if (candidate.variantGroup == episode.variantGroup) {
            members.push_back(&candidate);
        }
    }
    const auto indices =
        ::Mission::TutorialEpisodePolicy::BuildBalancedVariantAssignment(
            g_s.variantSeed, members.size(), episode.variants.size());
    for (std::size_t i = 0; i < members.size() && i < indices.size(); ++i) {
        g_s.episodeVariantAssignments[members[i]->id] =
            members[i]->variants[indices[i]];
    }
    const auto result = g_s.episodeVariantAssignments.find(episode.id);
    return result != g_s.episodeVariantAssignments.end()
        ? result->second : episode.kind;
}

void StartTaskEpisodeLocked() {
    StopTaskEpisodeLocked();
    const ::Mission::DummyEpisode* e = CurrentEpisodeLocked();
    if (!e) return;
    if (!EpisodeBehaviorReady(e)) {
        FailEpisodeLeaseLocked(
            "This lesson's dummy episode ('" + e->id +
            "') cannot be driven by the current tutorial runtime.");
        return;
    }
    // No-puppet episodes leave P2 native: forcing human control would inject neutral every
    // sub-tick and bypass the native block processor.
    std::string effKindStorage = SelectEpisodeVariantLocked(*e);
    const std::string& effKind = effKindStorage;

    const bool guardLease = effKind == "block" || effKind == "rg" ||
                            effKind == "guard" ||
                            effKind == "block_answer" || effKind == "rg_answer";
    const bool settingsLease = guardLease || effKind == "idle";
    const bool wakeAutoLease =
        ::Mission::TutorialEpisodePolicy::UsesNativeWakeProducer(
            effKind, e->start, e->trigger);
    const bool tutorialWriterLeases =
        ::Mission::TutorialEpisodePolicy::NeedsTutorialWriterLeases(
            effKind, e->start, e->trigger);
    const bool noPuppet = settingsLease || effKind == "airtech" || wakeAutoLease;
    if (settingsLease) {
        if (!AcquireTutorialAutoBlockController(g_s.autoBlockControllerToken)) {
            FailEpisodeLeaseLocked("The dummy block controller is busy. Retry the lesson.");
            return;
        }
    }

    if ((!noPuppet && !AcquireTutorialP2Control(g_s.p2ControlToken)) ||
        (tutorialWriterLeases &&
         (!AcquireTutorialMotionQueue(2, g_s.motionQueueToken) ||
          !ImmediateInput::AcquireTutorialLease(2, g_s.immediateInputToken) ||
          !AcquireTutorialBufferFreeze(g_s.bufferFreezeToken)))) {
        FailEpisodeLeaseLocked("Another input action owns the dummy. Retry after it finishes.");
        return;
    }

    if (noPuppet && !wakeAutoLease) FullCleanupAfterToggle(2);

    if (g_s.p2ControlToken != 0) {
        g_s.injectImmediateOnlySaved =
            g_injectImmediateOnly[2].load(std::memory_order_acquire);
        if (g_s.injectImmediateOnlySaved) {
            g_injectImmediateOnly[2].store(false, std::memory_order_release);
            g_s.injectImmediateOnlyTouched = true;
        }
    }

    if (wakeAutoLease) {
        int mappedAction = 0, mappedStrength = 0;
        if (!::Mission::TutorialEpisodePolicy::WakeAutoActionForAction(
                e->action, mappedAction, mappedStrength)) {
            FailEpisodeLeaseLocked(
                "The dummy's wakeup reversal action is not supported.");
            return;
        }
        if (!AcquireTutorialP2WakeProducer(
                mappedAction, mappedStrength, g_s.wakeAutoToken)) {
            FailEpisodeLeaseLocked(
                "The dummy's native wakeup producer is busy.");
            return;
        }
        LogOut("[TUTORIAL][EPISODE] wake auto-action lease action=" +
               e->action + " mapped=" + std::to_string(mappedAction) + "/" +
               std::to_string(mappedStrength), true);
    }

    if (effKind == "airtech") {
        g_s.airtechSavedEnabled = autoAirtechEnabled.load();
        g_s.airtechSavedDirection = autoAirtechDirection.load();
        g_s.airtechSavedDelay = autoAirtechDelay.load();
        g_s.airtechAppliedDirection = e->clip == "backward" ? 1 : 0;
        g_s.airtechAppliedDelay = e->reactDelay > 0 ? e->reactDelay : 0;
        autoAirtechDirection.store(g_s.airtechAppliedDirection);
        autoAirtechDelay.store(g_s.airtechAppliedDelay);
        autoAirtechEnabled.store(true);
        g_s.airtechTouched = true;
    }

    if (settingsLease) {
        g_s.randomBlockSaved = RandomBlock::IsEnabled();
        if (g_s.randomBlockSaved) {
            RandomBlock::SetEnabled(false);
            g_s.randomBlockTouched = true;
            g_s.randomBlockAppliedGeneration = RandomBlock::GetMutationGeneration();
        }
        g_s.alwaysRGSaved = AlwaysRG::IsEnabled();
        const bool wantRG = effKind == "rg" || effKind == "rg_answer";
        if (g_s.alwaysRGSaved != wantRG) {
            AlwaysRG::SetEnabled(wantRG);
            g_s.alwaysRGTouched = true;
            g_s.alwaysRGAppliedGeneration = AlwaysRG::GetMutationGeneration();
        }
        if (!GetPracticeAutoBlockEnabled(g_s.autoBlockSaved)) {
            FailEpisodeLeaseLocked("The Practice auto-block setting could not be read.");
            return;
        }
        const bool wantAutoBlock = guardLease;
        if (g_s.autoBlockSaved != wantAutoBlock) {
            if (!SetPracticeAutoBlockEnabled(wantAutoBlock,
                    wantAutoBlock ? "tutorial dummy guard lease"
                                  : "tutorial neutral dummy lease")) {
                FailEpisodeLeaseLocked("The Practice auto-block setting could not be applied.");
                return;
            }
            g_s.autoBlockTouched = true;
            g_s.autoBlockAppliedValue = wantAutoBlock;
            g_s.autoBlockAppliedGeneration = GetPracticeAutoBlockWriteGeneration();
        }
        if (effKind == "block" || effKind == "block_answer" ||
            effKind == "guard") {
            g_s.adaptiveStanceSaved = GetAdaptiveStanceEnabled();
            g_s.adaptiveStanceApplied =
                effKind == "block" || effKind == "block_answer";
            if (g_s.adaptiveStanceSaved != g_s.adaptiveStanceApplied) {
                SetAdaptiveStanceEnabled(g_s.adaptiveStanceApplied);
                g_s.adaptiveStanceTouched = true;
            }
        }
        if (effKind == "guard") {
            if (!GetPracticeBlockMode(g_s.practiceBlockModeSaved)) {
                FailEpisodeLeaseLocked(
                    "The Practice dummy guard stance could not be read.");
                return;
            }
            g_s.practiceBlockModeApplied =
                ::Mission::TutorialEpisodePolicy::FixedGuardPracticeMode(e->clip);
            if (g_s.practiceBlockModeSaved != g_s.practiceBlockModeApplied) {
                if (!SetPracticeBlockMode(g_s.practiceBlockModeApplied)) {
                    FailEpisodeLeaseLocked(
                        "The Practice dummy guard stance could not be applied.");
                    return;
                }
                g_s.practiceBlockModeTouched = true;
            }
            LogOut("[TUTORIAL][DUMMY_GUARD] mode=fixed clip=" + e->clip +
                   " practiceMode=" +
                   std::to_string(g_s.practiceBlockModeApplied) +
                   " inputInjection=0", true);
        }
    }
    g_s.episodePlaying = true;
    g_s.episodeFinished = false;
    g_s.episodeCycleSerial = 0;
    g_s.episodePhase = 0;
    g_s.episodeTimer = 0;
    g_s.episodeAnswerRgMove = -1;
    g_s.episodeChainIndex = 0;
    g_s.episodeContactFreezeSeen = false;
    g_s.epPrevP2Move = -1;
    g_s.epPrevP2FrameIdx = -1;
    g_s.episodeScriptStep = -1;
    g_s.scriptStepPhase = 0;
    g_s.scriptGapOpen = false;
    g_s.episodeTelegraphPhase = 0;
    g_s.episodeTelegraphTimer = 0;
    if (::Mission::TutorialEpisodePolicy::CueStartsOnArm(
            e->start, e->trigger)) {
        BeginEpisodeCueLocked(e);
    }
    const ::Mission::LessonTask* task = CurrentTaskLocked();
    LogOut("[TUTORIAL][EPISODE] start lesson=" + g_s.lesson.lessonId +
           " task=" + (task ? task->id : std::string("?")) +
           " episode=" + e->id + " kind=" + effKind +
           " start=" + e->start + " approach=" + e->approach +
           " action=" + (e->action.empty() ? std::string("-") : e->action),
           true);
}

void ReassertDummySwordModeLocked() {
    const auto it = g_s.lesson.dummy.resources.find("barehanded");
    if (it == g_s.lesson.dummy.resources.end() || it->second != 0) return;
    const uintptr_t p2 = GetPlayerBase(2);
    if (!p2) return;
    uint8_t mode = 0;
    if (!SafeReadMemory(p2 + RUMI_MODE_BYTE_OFFSET, &mode, sizeof(mode)) ||
        mode == 0) {
        return;
    }
    short move = 0;
    (void)SafeReadMemory(p2 + MOVE_ID_OFFSET, &move, sizeof(move));
    if (move >= 200) {
        const short idle = 0;
        (void)SafeWriteMemory(p2 + MOVE_ID_OFFSET, &idle, sizeof(idle));
    } else if (move > 3) {
        return;
    }
    CharacterSettings::ApplyRumiModeNow(2, false);
    LogOut("[TUTORIAL][EPISODE] re-asserted dummy shinai mode for the next cycle",
           true);
}

void ReanchorTaskBaselineLocked() {
    const ::Mission::LessonTask* t = CurrentTaskLocked();
    if (!t) return;
    const bool wantSeeds =
        (t->playerSeed.has || t->dummySeed.has) && !t->hasGoalState;
    const bool wantPos = t->hasPos || t->hasDummyPos;
    if (!wantSeeds && !wantPos) return;
    const uintptr_t p1 = GetPlayerBase(1);
    const uintptr_t p2 = GetPlayerBase(2);
    short m1 = 0, m2 = 0;
    if (!p1 || !p2 ||
        !SafeReadMemory(p1 + MOVE_ID_OFFSET, &m1, sizeof(m1)) ||
        !SafeReadMemory(p2 + MOVE_ID_OFFSET, &m2, sizeof(m2)) ||
        m1 < 0 || m1 > 3 || m2 < 0 || m2 > 3) {
        return;
    }
    std::string ignored;
    if (wantPos)   (void)ApplyTaskPositionLocked(t, ignored);
    if (wantSeeds) (void)ApplyTaskSeedsLocked(t, ignored);
}

void FinishEpisodeCycleLocked(const ::Mission::DummyEpisode* e) {
    ++g_s.episodeCycleSerial;
    g_s.episodeTimer = 0;
    g_s.episodeChainIndex = 0;
    g_s.episodeContactFreezeSeen = false;
    g_s.episodeTelegraphPhase = 0;
    g_s.episodeTelegraphTimer = 0;
    ReassertDummySwordModeLocked();
    if (e && !e->telegraph.empty()) {
        ReanchorTaskBaselineLocked();
    }
    ResetEpisodeDiagnosticsLocked();
    if (e && e->repeatMode == "loop") {
        g_s.episodePhase = 0;
        if (::Mission::TutorialEpisodePolicy::CueStartsOnArm(
                e->start, e->trigger)) {
            BeginEpisodeCueLocked(e);
        }
    } else {
        g_s.episodePhase = 0;
        g_s.episodeFinished = true;
    }
}

void RememberEpisodeP2Locked(const ::Mission::Engine::Snapshot& s) {
    g_s.epPrevP2Move = s.p2Move;
    g_s.epPrevP2FrameIdx = s.p2FrameIdx;
}

bool PrepareScriptApproachLocked(const ::Mission::DummyEpisode* e,
                                 const ::Mission::Engine::Snapshot& s) {
    if (!e || e->approach != "dash") {
        g_s.episodePhase = 4;
        return true;
    }

    const bool p2Idle = s.p2Move >= 0 && s.p2Move <= 3;
    switch (g_s.episodePhase) {
        case 0:
            if (s.freezeActive) return false;
            if (!QueueDummyMotionLocked(MOTION_FORWARD_DASH, 0)) {
                FailEpisodeLeaseLocked(
                    "The tutorial could not queue the dummy's scripted approach.");
                return false;
            }
            g_s.episodePhase = 1;
            g_s.episodeTimer = 0;
            return false;
        case 1:
            if (!s.freezeActive) ++g_s.episodeTimer;
            if (::Mission::TutorialEpisodePolicy::IsForwardDashApproachState(
                    s.p2Move)) {
                g_s.episodePhase = 2;
                g_s.episodeTimer = 0;
            } else if (g_s.episodeTimer > kEpisodeDashMinTicks * 4) {
                FailEpisodeLeaseLocked(
                    "The dummy's scripted approach did not enter its dash state.");
            }
            return false;
        case 2:
            if (!s.freezeActive) ++g_s.episodeTimer;
            if (p2Idle && g_s.episodeTimer >= kEpisodeDashMinTicks) {
                g_s.episodePhase = 3;
                g_s.episodeTimer = 0;
            } else if (g_s.episodeTimer > kEpisodeDashMinTicks * 8) {
                FailEpisodeLeaseLocked(
                    "The dummy's scripted approach did not return to neutral.");
            }
            return false;
        case 3:
            if (!s.freezeActive) ++g_s.episodeTimer;
            if (g_s.episodeTimer < kEpisodeSettleTicks) return false;
            g_s.episodePhase = 4;
            g_s.episodeTimer = 0;
            return true;
        default:
            return true;
    }
}

void DriveScriptLocked(const ::Mission::DummyEpisode* e,
                       const ::Mission::Engine::Snapshot& s) {
    const auto& steps = e->script;
    const int cooldown = e->repeatDelay > 0 ? e->repeatDelay : 120;
    if (g_s.episodeScriptStep < 0) {
        if (!PrepareScriptApproachLocked(e, s)) {
            RememberEpisodeP2Locked(s);
            return;
        }
        g_s.episodeScriptStep = 0;
        g_s.scriptStepPhase = 0;
        g_s.episodeTimer = 0;
        g_s.scriptGapOpen = false;
    }
    if (g_s.episodeScriptStep >= static_cast<int>(steps.size())) {
        g_s.scriptGapOpen = false;
        if (!s.freezeActive) ++g_s.episodeTimer;
        if (g_s.episodeTimer >= cooldown) {
            g_s.episodeScriptStep = -1;
            FinishEpisodeCycleLocked(e);
        }
        RememberEpisodeP2Locked(s);
        return;
    }
    const auto& st = steps[g_s.episodeScriptStep];
    if (st.action.empty()) {
        g_s.scriptGapOpen = true;
        if (!s.freezeActive) ++g_s.episodeTimer;
        if (g_s.episodeTimer >= st.gapTicks) {
            g_s.scriptGapOpen = false;
            ++g_s.episodeScriptStep;
            g_s.scriptStepPhase = 0;
            g_s.episodeTimer = 0;
        }
        RememberEpisodeP2Locked(s);
        return;
    }
    g_s.scriptGapOpen = false;
    const bool dashStep =
        ::Mission::TutorialEpisodePolicy::IsDashNormalAction(st.action);
    switch (g_s.scriptStepPhase) {
        case 0: {
            if (dashStep) {
                if (!s.freezeActive) ++g_s.episodeTimer;
                if (g_s.episodeTimer == 1) {
                    (void)QueueDummyMotionLocked(MOTION_FORWARD_DASH, 0);
                } else if (
                    ::Mission::TutorialEpisodePolicy::IsForwardDashApproachState(
                        s.p2Move)) {
                    const bool low = st.action.rfind("662", 0) == 0;
                    const char btn = st.action.back();
                    const int m = btn == 'C' ? (low ? MOTION_2C : MOTION_5C)
                                : btn == 'B' ? (low ? MOTION_2B : MOTION_5B)
                                             : (low ? MOTION_2A : MOTION_5A);
                    InjectDummyMoveLocked(m);
                    g_s.scriptStepPhase = 1;
                    g_s.episodeTimer = 0;
                } else if (g_s.episodeTimer > 240) {
                    FailEpisodeLeaseLocked(
                        "The dummy's scripted dash normal did not enter a dash state.");
                    return;
                }
                break;
            }
            bool ready;
            if (st.cancel) {
                if (!s.freezeActive) ++g_s.episodeTimer;
                ready = !s.freezeActive;
            } else {
                if (!s.freezeActive) ++g_s.episodeTimer;
                ready = s.p2Move < 200;
            }
            if (ready) {
                InjectDummyMoveLocked(ActionToMotion(st.action));
                g_s.scriptStepPhase = 1;
                g_s.episodeTimer = 0;
            }
            break;
        }
        case 1: {
            if (g_s.episodeTimer >= kEpisodeFreezeHold) ReleaseDummyFreezeLocked();
            if (!s.freezeActive) ++g_s.episodeTimer;
            if (::Mission::TutorialEpisodePolicy::IsScriptAttackInstanceEdge(
                    s.p2Move, s.p2FrameIdx,
                    g_s.epPrevP2Move, g_s.epPrevP2FrameIdx)) {
                g_s.scriptStepPhase = 2;
                g_s.episodeTimer = 0;
            } else if (g_s.episodeTimer > 240) {
                ReleaseDummyFreezeLocked();
                FailEpisodeLeaseLocked(
                    "The dummy's scripted input did not enter an attack state.");
                return;
            }
            break;
        }
        case 2: {
            const bool nextCancels =
                g_s.episodeScriptStep + 1 < static_cast<int>(steps.size()) &&
                steps[g_s.episodeScriptStep + 1].cancel;
            const int cancelWait = nextCancels
                ? steps[g_s.episodeScriptStep + 1].waitTicks : 0;
            const int postResolveWait = st.cancel ? 0 : st.waitTicks;
            if (g_s.episodeTimer >= kEpisodeFreezeHold) ReleaseDummyFreezeLocked();
            if (!s.freezeActive) ++g_s.episodeTimer;
            const bool moveEnded = s.p2Move < 200;
            if (nextCancels &&
                ::Mission::TutorialEpisodePolicy::ScriptCancelDelaySatisfied(
                    g_s.episodeTimer, cancelWait, s.freezeActive)) {
                ReleaseDummyFreezeLocked();
                ++g_s.episodeScriptStep;
                g_s.scriptStepPhase = 0;
                g_s.episodeTimer = 0;
            } else if (!nextCancels && moveEnded) {
                ReleaseDummyFreezeLocked();
                if (postResolveWait <= 0) {
                    ++g_s.episodeScriptStep;
                    g_s.scriptStepPhase = 0;
                } else {
                    g_s.scriptStepPhase = 3;
                }
                g_s.episodeTimer = 0;
            }
            break;
        }
        default:
            if (!s.freezeActive) ++g_s.episodeTimer;
            if (g_s.episodeTimer >= (st.cancel ? 0 : st.waitTicks)) {
                ++g_s.episodeScriptStep;
                g_s.scriptStepPhase = 0;
                g_s.episodeTimer = 0;
            }
            break;
    }
    RememberEpisodeP2Locked(s);
}

bool DriveTelegraphLocked(const ::Mission::DummyEpisode* e,
                          const ::Mission::Engine::Snapshot& s) {
    if (e->telegraph != "jump") return true;
    if (g_s.episodeTelegraphPhase >= 3) return true;
    if (s.freezeActive) return false;
    ++g_s.episodeTelegraphTimer;
    const bool p2Idle = s.p2Move >= 0 && s.p2Move <= 3;
    switch (g_s.episodeTelegraphPhase) {
        case 0:
            if (p2Idle && g_s.episodeTelegraphTimer >= 2) {
                if (!PressDummyImmediateLocked(GAME_INPUT_UP, 3)) {
                    FailEpisodeLeaseLocked(
                        "The tutorial could not press the dummy's telegraph hop.");
                    return false;
                }
                g_s.episodeTelegraphPhase = 1;
                g_s.episodeTelegraphTimer = 0;
            } else if (g_s.episodeTelegraphTimer >= 1152) {
                g_s.episodeTelegraphPhase = 3;
            }
            break;
        case 1:
            if (s.p2Move == PREJUMP_ID || s.p2Move == STRAIGHT_JUMP_ID) {
                g_s.episodeTelegraphPhase = 2;
                g_s.episodeTelegraphTimer = 0;
            } else if (g_s.episodeTelegraphTimer >= kEpisodeDashMinTicks * 4) {
                g_s.episodeTelegraphPhase = 3;
            }
            break;
        default:
            if (!p2Idle) {
                g_s.episodeTelegraphTimer = 0;
            } else if (g_s.episodeTelegraphTimer >= kEpisodeSettleTicks) {
                g_s.episodeTelegraphPhase = 3;
                g_s.episodeTelegraphTimer = 0;
            }
            break;
    }
    return g_s.episodeTelegraphPhase >= 3;
}

void DriveDashNormalLocked(const ::Mission::DummyEpisode* e,
                           const ::Mission::Engine::Snapshot& s,
                           int cooldown) {
    if (!s.freezeActive) ++g_s.episodeTimer;
    switch (g_s.episodePhase) {
        case 0:
            if (g_s.episodeTimer == 1) {
                (void)QueueDummyMotionLocked(MOTION_FORWARD_DASH, 0);
                g_s.episodePhase = 1;
                g_s.episodeTimer = 0;
            }
            break;
        case 1:
            if (!s.freezeActive &&
                ::Mission::TutorialEpisodePolicy::IsForwardDashApproachState(
                    s.p2Move)) {
                const bool low = e->action.rfind("662", 0) == 0;
                const char btn = e->action.back();
                const int motion =
                    btn == 'C' ? (low ? MOTION_2C : MOTION_5C)
                  : btn == 'B' ? (low ? MOTION_2B : MOTION_5B)
                               : (low ? MOTION_2A : MOTION_5A);
                InjectDummyMoveLocked(motion);
                g_s.episodePhase = 2;
                g_s.episodeTimer = 0;
            } else if (g_s.episodeTimer >= kEpisodeDashMinTicks * 4) {
                FailEpisodeLeaseLocked(
                    "The dummy's dash-normal opener did not enter a dash state.");
                return;
            }
            break;
        case 2:
            if (g_s.episodeTimer >= kEpisodeAttackTicks) {
                g_s.episodePhase = 3;
                g_s.episodeTimer = 0;
            }
            break;
        default:
            if (g_s.episodeTimer >= cooldown) FinishEpisodeCycleLocked(e);
            break;
    }
}

void DriveContactChainLocked(const ::Mission::DummyEpisode* e,
                             const ::Mission::Engine::Snapshot& s,
                             int cooldown) {
    static const int kMotions[] = {MOTION_5A, MOTION_5B, MOTION_2C};
    constexpr int kCount = static_cast<int>(sizeof(kMotions) / sizeof(kMotions[0]));

    if (g_s.episodePhase == 0) {
        if (!s.freezeActive && ++g_s.episodeTimer == 1) {
            InjectDummyMoveLocked(kMotions[0]);
            g_s.episodeChainIndex = 1;
            g_s.episodeContactFreezeSeen = false;
            g_s.episodePhase = 1;
            g_s.episodeTimer = 0;
        }
        return;
    }

    if (g_s.episodePhase == 1) {
        if (s.freezeActive) {
            g_s.episodeContactFreezeSeen = true;
            return; // never advance a scripted input while the world is frozen
        }
        if (::Mission::TutorialEpisodePolicy::CanInjectContactChainFollowup(
                g_s.episodeContactFreezeSeen, s.freezeActive)) {
            g_s.episodeContactFreezeSeen = false;
            g_s.episodeTimer = 0;
            if (g_s.episodeChainIndex < kCount) {
                InjectDummyMoveLocked(kMotions[g_s.episodeChainIndex++]);
            } else {
                g_s.episodePhase = 2;
            }
            return;
        }
        if (++g_s.episodeTimer >= kEpisodeAttackTicks * 2) {
            FailEpisodeLeaseLocked(
                "The dummy's contact-driven string whiffed before its next hit.");
            return;
        }
        return;
    }

    if (!s.freezeActive && ++g_s.episodeTimer >= cooldown) {
        FinishEpisodeCycleLocked(e);
    }
}

void DriveAttackSequenceLocked(const ::Mission::DummyEpisode* e, short p2Move,
                               int motion, int cooldown) {
    const bool p2Idle = p2Move >= 0 && p2Move <= 3;
    if (e->action == "jump") {
        switch (g_s.episodePhase) {
            case 0:
                if (g_s.episodeTimer == 1) {
                    if (e->approach == "dash") (void)QueueDummyMotionLocked(MOTION_FORWARD_DASH, 0);
                    else { g_s.episodePhase = 1; g_s.episodeTimer = 0; break; }
                }
                if (g_s.episodeTimer >= kEpisodeDashMinTicks && p2Idle) {
                    g_s.episodePhase = 1; g_s.episodeTimer = 0;
                }
                break;
            case 1:
                if (g_s.episodeTimer == 1) {
                    const bool fr = GetPlayerFacingDirection(2);
                    (void)PressDummyImmediateLocked(GAME_INPUT_UP |
                        (fr ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT), 3);
                }
                if (g_s.episodeTimer >= kEpisodeDashMinTicks && p2Idle) {
                    g_s.episodePhase = 2; g_s.episodeTimer = 0;
                }
                break;
            default:
                if (g_s.episodeTimer >= cooldown) {
                    FinishEpisodeCycleLocked(e);
                }
                break;
        }
        return;
    }
    if (IsAirAction(e->action)) {
        switch (g_s.episodePhase) {
            case 0:
                if (g_s.episodeTimer == 1) {
                    if (e->approach == "dash") (void)QueueDummyMotionLocked(MOTION_FORWARD_DASH, 0);
                    else { g_s.episodePhase = 1; g_s.episodeTimer = 0; break; }
                }
                if (g_s.episodeTimer >= kEpisodeDashMinTicks && p2Idle) { g_s.episodePhase = 1; g_s.episodeTimer = 0; }
                break;
            case 1: {
                if (g_s.episodeTimer == 1) {
                    const bool fr = GetPlayerFacingDirection(2);
                    (void)PressDummyImmediateLocked(GAME_INPUT_UP | (fr ? GAME_INPUT_RIGHT : GAME_INPUT_LEFT), 3);
                }
                if (g_s.episodeTimer >= kJumpRiseTicks) { g_s.episodePhase = 2; g_s.episodeTimer = 0; }
                break;
            }
            case 2:
                if (g_s.episodeTimer == 1)
                    (void)PressDummyImmediateLocked(DetermineButtonFromMotionType(motion), 3);
                if (g_s.episodeTimer >= kEpisodeAttackTicks) { g_s.episodePhase = 3; g_s.episodeTimer = 0; }
                break;
            default: if (g_s.episodeTimer >= cooldown) FinishEpisodeCycleLocked(e); break;
        }
        return;
    }
    switch (g_s.episodePhase) {
        case 0:
            if (g_s.episodeTimer == 1) {
                if (e->approach == "dash") (void)QueueDummyMotionLocked(MOTION_FORWARD_DASH, 0);
                else if (p2Idle) {
                    g_s.episodePhase = 2;
                    g_s.episodeTimer = 0;
                    break;
                }
            }
            if (e->approach == "none") {
                if (p2Idle) {
                    g_s.episodePhase = 2;
                    g_s.episodeTimer = 0;
                } else if (g_s.episodeTimer >= 480) {
                    FailEpisodeLeaseLocked(
                        "The dummy did not become ready for its scripted action.");
                }
                break;
            }
            if (g_s.episodeTimer >= kEpisodeDashMinTicks && p2Idle) { g_s.episodePhase = 1; g_s.episodeTimer = 0; }
            break;
        case 1:
            if (g_s.episodeTimer >= kEpisodeSettleTicks) { g_s.episodePhase = 2; g_s.episodeTimer = 0; }
            break;
        case 2:
            if (g_s.episodeTimer == 1) InjectDummyMoveLocked(motion);
            if (!e->expectedMoveIds.empty()) {
                if (EpisodeExpectedAttack(e, p2Move)) {
                    ReleaseDummyFreezeLocked();
                    g_s.episodePhase = 3;
                    g_s.episodeTimer = 0;
                } else if (g_s.episodeTimer >= 480) {
                    FailEpisodeLeaseLocked(
                        "The dummy's scripted action was not recognized by the game.");
                }
            } else if (g_s.episodeTimer >= kEpisodeAttackTicks) {
                g_s.episodePhase = 3;
                g_s.episodeTimer = 0;
            }
            break;
        case 3:
            if (!e->expectedMoveIds.empty()) {
                if (p2Move >= 200) {
                    g_s.episodeTimer = 0;
                } else {
                    g_s.episodePhase = 4;
                    g_s.episodeTimer = 0;
                }
                break;
            }
            if (g_s.episodeTimer >= kEpisodeFreezeHold) ReleaseDummyFreezeLocked();
            if (g_s.episodeTimer >= cooldown) FinishEpisodeCycleLocked(e);
            break;
        default:
            if (g_s.episodeTimer >= cooldown) FinishEpisodeCycleLocked(e);
            break;
    }
}

void TickEpisodeLocked(const ::Mission::Engine::Snapshot& s) {
    g_s.episodeCueBlockedThisTick = false;
    if (!g_s.episodePlaying) return;
    const ::Mission::DummyEpisode* e = CurrentEpisodeLocked();
    if (!e || !EpisodeBehaviorReady(e)) {
        FailEpisodeLeaseLocked(
            "The active tutorial dummy episode is no longer drivable.");
        return;
    }
    const bool cueBlocked = TickEpisodeCueLocked(e, s);
    ObserveEpisodeDriveLocked(e, s);
    if (!e->variants.empty()) return;
    if (e->kind == "idle" || e->kind == "block" || e->kind == "rg" ||
        e->kind == "airtech") return;
    if (e->kind == "guard") {
        return;
    }
    const bool nativeWakeProducer =
        ::Mission::TutorialEpisodePolicy::UsesNativeWakeProducer(
            e->kind, e->start, e->trigger);
    if (cueBlocked && !nativeWakeProducer) return;
    if (g_s.episodeFinished) return;
    if (!DriveTelegraphLocked(e, s)) return;
    if (!e->script.empty()) {
        DriveScriptLocked(e, s);
        return;
    }
    if (e->kind == "block_answer" || e->kind == "rg_answer") {
        const int answer = ActionToMotion(e->action);
        const int cd = e->repeatDelay > 0 ? e->repeatDelay : 120;
        const auto releaseAnswerPuppet = []() {
            ReleaseDummyFreezeLocked();
            if (g_s.p2ControlToken != 0) {
                if (ReleaseTutorialP2Control(g_s.p2ControlToken)) {
                    g_s.p2ControlToken = 0;
                }
            }
            if (g_s.injectImmediateOnlyTouched &&
                !g_injectImmediateOnly[2].load(std::memory_order_acquire)) {
                g_injectImmediateOnly[2].store(g_s.injectImmediateOnlySaved,
                                               std::memory_order_release);
            }
            g_s.injectImmediateOnlyTouched = false;
            FullCleanupAfterToggle(2);
        };
        switch (g_s.episodePhase) {
            case 0: {
                const char* trig = e->kind == "rg_answer" ? "onRG" : "afterBlock";
                if (g_s.epPrevP2Move >= 0 &&
                    DummyTriggerEdge(trig, g_s.epPrevP2Move, s.p2Move) &&
                    g_s.p2ControlToken == 0 &&
                    AcquireTutorialP2Control(g_s.p2ControlToken)) {
                    g_s.injectImmediateOnlySaved =
                        g_injectImmediateOnly[2].load(std::memory_order_acquire);
                    if (g_s.injectImmediateOnlySaved) {
                        g_injectImmediateOnly[2].store(false, std::memory_order_release);
                        g_s.injectImmediateOnlyTouched = true;
                    }
                    g_s.episodeAnswerRgMove = e->kind == "rg_answer"
                        ? s.p2Move : static_cast<short>(-1);
                    g_s.episodePhase = 1;
                    g_s.episodeTimer = 0;
                }
                break;
            }
            case 1: {
                ++g_s.episodeTimer;
                const bool delaySatisfied = e->kind != "rg_answer" ||
                    ::Mission::TutorialEpisodePolicy::RecoilGuardAnswerDelaySatisfied(
                        g_s.episodeAnswerRgMove, g_s.episodeTimer);
                if (delaySatisfied) {
                    if (answer >= MOTION_5A && answer <= MOTION_5D) {
                        const uint8_t mask = DetermineButtonFromMotionType(answer);
                        if (!PressDummyImmediateLocked(mask, 2)) {
                            FailEpisodeLeaseLocked(
                                "The tutorial could not press the dummy's scripted answer.");
                            return;
                        }
                    } else {
                        InjectDummyMoveLocked(answer);
                    }
                    g_s.episodePhase = 2;
                    g_s.episodeTimer = 0;
                }
                break;
            }
            case 2: {
                ++g_s.episodeTimer;
                if (g_s.episodeTimer >= kEpisodeFreezeHold) ReleaseDummyFreezeLocked();
                if (::Mission::TutorialEpisodePolicy::IsScriptAttackInstanceEdge(
                        s.p2Move, s.p2FrameIdx,
                        g_s.epPrevP2Move, g_s.epPrevP2FrameIdx)) {
                    g_s.episodePhase = 3;
                    g_s.episodeTimer = 0;
                } else if (g_s.episodeTimer >= 480) {
                    FailEpisodeLeaseLocked(
                        e->kind == "rg_answer"
                            ? "The dummy's scripted Recoil Guard answer did not enter its attack state."
                            : "The dummy's scripted block answer did not enter its attack state.");
                    return;
                }
                break;
            }
            case 3: {
                ++g_s.episodeTimer;
                if (g_s.episodeTimer >= kEpisodeFreezeHold) ReleaseDummyFreezeLocked();
                const bool actionDone = s.p2Move < 200 && g_s.episodeTimer > 12;
                if (actionDone) {
                    releaseAnswerPuppet();
                    g_s.episodePhase = 4;
                    g_s.episodeTimer = 0;
                } else if (g_s.episodeTimer >= 480) {
                    releaseAnswerPuppet();
                    FailEpisodeLeaseLocked(
                        "The dummy's scripted answer did not finish cleanly.");
                    return;
                }
                break;
            }
            default:
                ++g_s.episodeTimer;
                if (g_s.episodeTimer >= cd) FinishEpisodeCycleLocked(e);
                break;
        }
        g_s.epPrevP2Move = s.p2Move;
        g_s.epPrevP2FrameIdx = s.p2FrameIdx;
        return;
    }

    const int motion = ActionToMotion(e->action);
    const int cooldown = e->repeatDelay > 0 ? e->repeatDelay : 120;

    if (::Mission::TutorialEpisodePolicy::IsDashNormalAction(e->action)) {
        DriveDashNormalLocked(e, s, cooldown);
        return;
    }
    if (::Mission::TutorialEpisodePolicy::IsContactChainAction(e->action)) {
        DriveContactChainLocked(e, s, cooldown);
        return;
    }

    if (e->start == "playerState") {
        const int watch = PredicateMoveId(e->predicate);
        ++g_s.episodeTimer;
        switch (g_s.episodePhase) {
            case 0:
                if (s.p1Move == watch && g_s.prevMove != watch) { g_s.episodePhase = 1; g_s.episodeTimer = 0; }
                break;
            case 1:
                if (g_s.episodeTimer >= e->reactDelay &&
                    s.p2Move >= 0 && s.p2Move <= 3) {
                    InjectDummyMoveLocked(motion);
                    g_s.episodePhase = 2; g_s.episodeTimer = 0;
                } else if (g_s.episodeTimer >= 1152) {
                    FailEpisodeLeaseLocked(
                        "The dummy never became free for its scripted response.");
                    return;
                }
                break;
            default:
                if (g_s.episodeTimer >= kEpisodeFreezeHold) ReleaseDummyFreezeLocked();
                if (g_s.episodeTimer >= cooldown) FinishEpisodeCycleLocked(e);
                break;
        }
        return;
    }

    if (e->start == "trigger") {
        const bool nativeWake = e->trigger == "onWakeup" &&
            g_s.wakeAutoToken != 0;
        const bool edge = DummyTriggerEdge(e->trigger, g_s.epPrevP2Move, s.p2Move);
        g_s.epPrevP2Move = s.p2Move;
        ++g_s.episodeTimer;
        switch (g_s.episodePhase) {
            case 0:
                if (edge ||
                    (nativeWake && EpisodeExpectedAttack(e, s.p2Move))) {
                    g_s.episodePhase = 1;
                    g_s.episodeTimer = 0;
                    if (!nativeWake &&
                        ::Mission::TutorialEpisodePolicy::CueStartsOnTrigger(
                            e->start, true)) {
                        BeginEpisodeCueLocked(e);
                        g_s.episodeCueBlockedThisTick =
                            g_s.episodeCueTicks > 0;
                    }
                }
                break;
            case 1:
                if (nativeWake) {
                    if (EpisodeExpectedAttack(e, s.p2Move)) {
                        g_s.episodePhase = 2;
                        g_s.episodeTimer = 0;
                    } else if (g_s.episodeTimer >= 480) {
                        FailEpisodeLeaseLocked(
                            "The dummy's wakeup reversal did not come out.");
                        return;
                    }
                } else {
                    InjectDummyMoveLocked(motion);
                    g_s.episodePhase = 2;
                    g_s.episodeTimer = 0;
                }
                break;
            default:
                if (nativeWake && g_s.episodePhase == 2 &&
                    EpisodeExpectedAttack(e, s.p2Move)) {
                    g_s.episodeTimer = 0;
                    break;
                }
                if (g_s.episodeTimer >= kEpisodeFreezeHold) ReleaseDummyFreezeLocked();
                if (g_s.episodeTimer >= cooldown) FinishEpisodeCycleLocked(e);
                break;
        }
        return;
    }

    ++g_s.episodeTimer;
    DriveAttackSequenceLocked(e, s.p2Move, motion, cooldown);
}

int FirstPendingTaskLocked() {
    for (int i = 0; i < static_cast<int>(g_s.tasks.size()); ++i) {
        if (!g_s.tasks[i].done) return i;
    }
    return -1;
}

int ContinuationRetryHeadLocked(int currentTask) {
    std::vector<std::string> continuities;
    continuities.reserve(g_s.lesson.lesson.tasks.size());
    for (const auto& task : g_s.lesson.lesson.tasks) {
        continuities.push_back(task.continuity);
    }
    return ContinuationRetryHead(continuities, currentTask);
}

void ClearRetryChainLocked(int firstTask, int lastTask) {
    if (firstTask < 0 || lastTask < firstTask) return;
    const int end = (std::min)(lastTask,
        static_cast<int>(g_s.tasks.size()) - 1);
    for (int i = firstTask; i <= end; ++i) {
        g_s.tasks[i].done = false;
        g_s.tasks[i].failed = false;
    }
}

bool CompletionMetLocked() {
    const std::string& mode = g_s.lesson.lesson.completion;
    if (mode == "pages") return true;
    if (mode == "anyTask") {
        for (const auto& t : g_s.tasks) if (t.done) return true;
        return false;
    }
    return FirstPendingTaskLocked() < 0;
}

void EnterCompleteLocked() {
    g_s.phase = Phase::Complete;
    g_s.completeSel = 0;
    g_s.cleared = true;
    SetNeutralInputLease(false);
    SetFreeze(true);
    if (!g_s.clearRecorded) {
        g_s.clearRecorded = true;
        std::string warn;
        if (!Tut::ProgressRecordClear(g_s.packId, g_s.lesson.lessonId,
                                      g_s.lesson.revision,
                                      PersistedAttemptDelta(g_s.attemptsThisRun), warn) &&
            !warn.empty()) {
            DirectDrawHook::AddMessage("Lesson complete. Progress could not be saved.",
                                       "TUTORIAL", RGB(255, 210, 120), 2600, 20, 96);
        }
        LogOut("[TUTORIAL] LESSON COMPLETE id=" + g_s.lesson.lessonId +
               " attempts=" + std::to_string(g_s.attemptsThisRun), true);
    }
}

void BeginTaskMetricsLocked();
void FinalizeTaskMetricsLocked();
double MetricValue(const TaskMetrics& m, const std::string& name);
const TaskMetrics* MetricsForTaskIdLocked(const std::string& id);
std::string FormatMetric(double v);

void ArmTaskLocked(int index, bool carryState = false) {
    g_s.task = index;
    g_s.armedCommitted = false;
    g_s.comboBaseline = -1;
    g_s.sequenceIndex = 0;
    g_s.sequenceGapTicks = 0;
    g_s.sinceP2Wake = -1;
    g_s.metricsLive = false;
    g_s.waitingComboEnd = false;
    g_s.branchPhase = 0;
    g_s.branchSeqIndex = 0;
    g_s.branchTicks = 0;
    g_s.branchComboBase = 0;
    g_s.branchActionCommitted = false;
    g_s.goalBlockSeen = false;
    g_s.sinceP2Whiff = -1;
    g_s.p1TouchedByP2Attack = false;
    g_s.absencePhase = 0;
    g_s.absenceElapsed = 0;
    g_s.absenceDummyAttackSeen = false;
    g_s.absenceBlockCount = 0;
    g_s.absenceEpisodeCycleBase = 0;
    g_s.comboWasAlive = false;
    g_s.prevCombo = 0;
    g_s.prevHitState = 0;
    g_s.holdTicks = 0;
    g_s.inputCommitGrace = 0;
    g_s.inputCommitMask = 0;
    g_s.armedMove = 0;
    g_s.contactMatches = 0;
    g_s.traceSampleValid = false;
    g_s.traceExpectedIndex = -1;
    g_s.interceptionPrevLife = -1;
    g_s.flickerContactSeen = false;
    g_s.flickerSourceDistanceSafe = false;
    g_s.flickerDamageBaseline = 0;
    g_s.flickerDefenderHpBaseline = 0;
    StopTaskEpisodeLocked();
    if (index >= 0 && index < static_cast<int>(g_s.tasks.size())) {
        g_s.tasks[index].failed = false;
    }
    g_s.choiceSel = 0;
    SetNeutralInputLease(true);
    const ::Mission::LessonTask* t = CurrentTaskLocked();
    if (t && t->kind == "choice") {
        g_s.phase = Phase::Choice;
        SetFreeze(true);
    } else if (carryState) {
        std::string setupError;
        if (!ApplyTaskPositionLocked(t, setupError) ||
            !ApplyTaskSeedsLocked(t, setupError)) {
            FailTaskSetupLocked(setupError);
            return;
        }
        BeginTaskMetricsLocked();
        g_s.phase = Phase::TaskActive;
        SetFreeze(false);
        SetNeutralInputLease(false);
        StartTaskEpisodeLocked();
    } else {
        g_s.phase = Phase::NeutralGate;
        g_s.neutralTicks = 0;
        uint8_t observedPoll = 0;
        uint32_t observedSerial = 0;
        const bool observedPollValid = TryGetLastObservedPhysicalPoll(
            1, observedPoll, &observedSerial);
        g_s.neutralObservedSerial = NeutralGateObservedSerialBaseline(
            observedPollValid, observedSerial);
        SetFreeze(false);
    }
    LogOut("[TUTORIAL][TASK] arm lesson=" + g_s.lesson.lessonId +
           " task=" + (t ? t->id : std::string("?")) +
           " index=" + std::to_string(index + 1) + "/" +
           std::to_string(g_s.tasks.size()) +
           " continuity=" + (t ? t->continuity : std::string("?")) +
           " carry=" + (carryState ? std::string("1") : std::string("0")) +
           " phase=" + PhaseName(g_s.phase), true);
}

bool AdvanceAfterTaskLocked() {
    if (CompletionMetLocked()) {
        EnterCompleteLocked();
        return false;
    }
    const int next = FirstPendingTaskLocked();
    if (next < 0) { EnterCompleteLocked(); return false; }
    ArmTaskLocked(next);
    return true;
}

void ResumeFromReviewLocked() {
    g_s.reviewResumePending = false;
    g_s.uiLatchNeedsSync = true;

    if (g_s.reviewStateInvalidated) {
        const int target = g_s.reviewResumeTask;
        g_s.reviewStateInvalidated = false;
        g_s.reviewResumeTask = -1;
        if (g_s.reviewReturn == Phase::Complete || target < 0) {
            EnterCompleteLocked();
        } else {
            ArmTaskLocked(target);
        }
        return;
    }

    if (g_s.reviewReturn == Phase::Complete ||
        (g_s.reviewReturn == Phase::Feedback &&
         g_s.feedbackSuccess && CompletionMetLocked())) {
        EnterCompleteLocked();
        return;
    }

    g_s.phase = g_s.reviewReturn;
    SetFreeze(FrozenPhase(g_s.phase));
    SetNeutralInputLease(PhaseNeedsNeutralInput(g_s.phase));
}

void StartFeedbackLocked(bool success, const std::string& text) {
    g_s.phase = Phase::Feedback;
    g_s.feedbackSuccess = success;
    g_s.feedbackText = text;
    g_s.feedbackTicks = success ? 240 : 360;
    SetNeutralInputLease(true);
    SetFreeze(false);
    StopTaskEpisodeLocked();
    const ::Mission::LessonTask* task = CurrentTaskLocked();
    LogOut("[TUTORIAL][TASK] " + std::string(success ? "pass" : "fail") +
           " lesson=" + g_s.lesson.lessonId +
           " task=" + (task ? task->id : std::string("?")) +
           " index=" + std::to_string(g_s.task + 1) +
           " sequence=" + std::to_string(g_s.sequenceIndex) +
           " attempts=" + std::to_string(g_s.attemptsThisRun) +
           " reason=\"" + LogExcerpt(text) + "\"", true);
}

std::string NextNameLocked() {
    if (g_s.lesson.nextLessonId.empty()) return std::string();
    std::lock_guard<std::mutex> rl(g_regMx);
    RegisteredLesson next;
    int skipped = 0;
    return ResolveAvailableDescendantLocked(
               g_s.packId, g_s.lesson.nextLessonId, next, skipped)
        ? next.displayName : std::string();
}

bool ResolveNextPathLocked(std::string& nextPath, std::string& message) {
    if (g_s.lesson.nextLessonId.empty()) {
        message = "this is the final lesson in the course";
        return false;
    }
    int skipped = 0;
    RegisteredLesson next;
    {
        std::lock_guard<std::mutex> rl(g_regMx);
        if (!ResolveAvailableDescendantLocked(
                g_s.packId, g_s.lesson.nextLessonId, next, skipped, &message)) {
            return false;
        }
        nextPath = next.path;
    }
    if (skipped > 0) {
        LogOut("[TUTORIAL][FLOW] next skipped unavailable count=" +
               std::to_string(skipped) + " destination=" + next.lessonId, true);
    }
    return true;
}

void ReturnToBrowserAfterUnlock();

bool LoadNextAfterUnlock(const DeferredRequest& request, std::string& message) {
    std::string msg;
    if (!::Mission::Engine::Runner::Load(request.nextPath, msg)) {
        message = "next lesson failed to load: " + msg;
        if (::Mission::Engine::Runner::IsActive()) {
            std::lock_guard<std::mutex> lk(g_mx);
            if (g_s.active && g_s.lesson.lessonId == request.sourceLessonId) {
                SetFreeze(true);
            }
        } else {
            ReturnToBrowserAfterUnlock();
        }
        DirectDrawHook::AddMessage(message.c_str(), "TUTORIAL", RGB(255, 180, 120), 2600, 20, 96);
        LogOut("[TUTORIAL] " + message, true);
        return false;
    }
    message = msg;
    return true;
}

bool RestoreCheckpointAfterUnlock(const std::string& sourceLessonId) {
    std::string message;
    if (::Mission::Engine::Runner::RequestBaselineRestore(message)) {
        std::lock_guard<std::mutex> lk(g_mx);
        if (g_s.active && g_s.lesson.lessonId == sourceLessonId &&
            FrozenPhase(g_s.phase)) {
            EnsureFreezeLocked();
        }
        return true;
    }
    if (message.empty()) message = "baseline restore was unavailable";
    LogOut("[TUTORIAL] checkpoint restore failed id=" + sourceLessonId +
           " reason=" + message, true);
    {
        std::lock_guard<std::mutex> lk(g_mx);
        if (g_s.active && g_s.lesson.lessonId == sourceLessonId) {
            g_s.phase = Phase::Error;
            g_s.feedbackSuccess = false;
            g_s.feedbackText = "Could not restore this lesson checkpoint: " + message;
            SetNeutralInputLease(true);
            SetFreeze(true);
        }
    }
    DirectDrawHook::AddMessage("Lesson checkpoint restore failed", "TUTORIAL",
                               RGB(255, 150, 120), 2600, 20, 96);
    return false;
}

void ReturnToBrowserAfterUnlock() {
    if (!CanRequestFrontendExit(FrontendExitTarget::Title)) {
        DirectDrawHook::AddMessage("Cannot leave the match right now", "TUTORIAL",
                                   RGB(255, 180, 120), 2000, 20, 96);
        return;
    }
    if (RequestFrontendExit(FrontendExitTarget::Title)) {
        HudDisable::ResetVisible();
        ::Mission::Engine::Runner::Unload();
        PracticeMenu::RequestTitleReopen(PracticeMenu::TitleScreen::Screen::Tutorial);
    } else {
        DirectDrawHook::AddMessage("Cannot leave the match right now", "TUTORIAL",
                                   RGB(255, 180, 120), 2000, 20, 96);
    }
}

struct UiInput {
    int vert = 0;
    int horiz = 0;
    bool confirm = false;
    bool cancel = false;
    bool anyHeld = false;
    bool inputActive = false;
};

UiInput PollUiEdges(bool syncOnly = false) {
    UpdateWindowActiveState();
    const bool windowActive = g_efzWindowActive.load(std::memory_order_relaxed);
    const HWND gameWindow = FindEFZWindow();
    const bool focused = gameWindow && GetForegroundWindow() == gameWindow;
    auto keyDown = [&](int vk) {
        return focused && vk > 0 &&
               (GetAsyncKeyState(vk) & 0x8000) != 0;
    };
    bool upKey    = keyDown(VK_UP);
    bool downKey  = keyDown(VK_DOWN);
    bool leftKey  = keyDown(VK_LEFT);
    bool rightKey = keyDown(VK_RIGHT);
    bool enterKey = keyDown(VK_RETURN);
    bool backKey  = keyDown(VK_BACK);
    bool boundA = false, boundB = false, boundC = false, boundS = false;
    if (detectedBindings.directionsDetected) {
        upKey    |= keyDown(detectedBindings.upKey);
        downKey  |= keyDown(detectedBindings.downKey);
        leftKey  |= keyDown(detectedBindings.leftKey);
        rightKey |= keyDown(detectedBindings.rightKey);
    }
    if (detectedBindings.attacksDetected) {
        boundA = keyDown(detectedBindings.aButton);
        boundB = keyDown(detectedBindings.bButton);
        boundC = keyDown(detectedBindings.cButton);
        boundS = keyDown(detectedBindings.dButton);
    }

    bool padUp = false, padDown = false, padLeft = false, padRight = false;
    bool padA = false, padB = false, padX = false, padY = false;
    XInputShim::Snapshot padSnapshot{};
    if (focused) XInputShim::CopySnapshot(padSnapshot);
    const unsigned mask = padSnapshot.connectedMask;
    const int selectedPad = Config::GetSettings().controllerIndex;
    for (int i = 0; i < 4; ++i) {
        if (!(mask & (1u << i))) continue;
        if (selectedPad >= 0 && selectedPad <= 3 && i != selectedPad) continue;
        const XINPUT_STATE& state = padSnapshot.states[i];
        padUp    |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0 || state.Gamepad.sThumbLY > 16000;
        padDown  |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0 || state.Gamepad.sThumbLY < -16000;
        padLeft  |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0 || state.Gamepad.sThumbLX < -16000;
        padRight |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0 || state.Gamepad.sThumbLX > 16000;
        padA     |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
        padB     |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
        padX     |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0;
        padY     |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0;
    }

    const int vert = (upKey || padUp) ? -1 : ((downKey || padDown) ? 1 : 0);
    const int horiz = (leftKey || padLeft) ? -1 : ((rightKey || padRight) ? 1 : 0);
    const bool confirm = enterKey || boundA || padA;
    const bool cancel = backKey || boundB || padB;

    static int s_prevVert = 0, s_prevHoriz = 0;
    static bool s_prevConfirm = true, s_prevCancel = true;
    static int s_hold = 0;

    UiInput out;
    out.vert = (s_prevVert == 0 && vert != 0) ? vert : 0;
    if (vert != 0 && vert == s_prevVert) {
        ++s_hold;
        if (s_hold >= 58 && ((s_hold - 58) % 19) == 0) out.vert = vert;
    } else {
        s_hold = 0;
    }
    out.horiz = (s_prevHoriz == 0 && horiz != 0) ? horiz : 0;
    out.confirm = confirm && !s_prevConfirm;
    out.cancel = cancel && !s_prevCancel;
    out.anyHeld = vert != 0 || horiz != 0 || confirm || cancel ||
                  boundC || boundS || padX || padY;
    out.inputActive = windowActive && focused;
    s_prevVert = vert;
    s_prevHoriz = horiz;
    s_prevConfirm = confirm;
    s_prevCancel = cancel;
    if (syncOnly || !out.inputActive) {
        out.vert = 0;
        out.horiz = 0;
        out.confirm = false;
        out.cancel = false;
    }
    return out;
}

struct ExpectedAction {
    const std::vector<int>* moveIds = nullptr;
    const std::vector<int>* fromMoveIds = nullptr;
    int inputMask = 0;
    const std::string* req = nullptr;
    int hitsRequired = 1;
    int minGap = 0;
    int maxGap = 0;
    int afterWakeMaxTicks = 0;
    int whiffWindowMaxTicks = 0;
    bool duringGap = false;
    const std::string* dummyState = nullptr;
    const std::vector<int>* dummyStateMoveIds = nullptr;
    const ::Mission::ContactPredicate* contact = nullptr;
    bool hasFlickerIC = false;
    int flickerProjectilePattern = 0;
    int flickerMinDistance = 0;
};

int ActionCount(const ::Mission::LessonTask& task) {
    return task.sequence.empty() ? 1 : static_cast<int>(task.sequence.size());
}

ExpectedAction GetExpectedAction(const ::Mission::LessonTask& task, int index) {
    ExpectedAction out;
    if (!task.sequence.empty()) {
        if (index < 0 || index >= static_cast<int>(task.sequence.size())) return out;
        const auto& action = task.sequence[index];
        out.moveIds = &action.moveIds;
        out.fromMoveIds = &action.fromMoveIds;
        out.inputMask = action.inputMask;
        out.req = &action.req;
        out.hitsRequired = action.hitsRequired;
        out.minGap = action.minGap;
        out.maxGap = action.maxGap;
        out.afterWakeMaxTicks = action.afterWakeMaxTicks;
        out.whiffWindowMaxTicks = action.whiffWindowMaxTicks;
        out.duringGap = action.duringGap;
        out.dummyState = &action.dummyState;
        out.dummyStateMoveIds = &action.dummyStateMoveIds;
        if (action.hasContact) out.contact = &action.contact;
        out.hasFlickerIC = action.hasFlickerIC;
        out.flickerProjectilePattern = action.flickerProjectilePattern;
        out.flickerMinDistance = action.flickerMinDistance;
    } else {
        out.moveIds = &task.moveIds;
        out.inputMask = task.inputMask;
        out.req = &task.req;
        out.hitsRequired = task.hitsRequired;
        out.afterWakeMaxTicks = task.afterWakeMaxTicks;
        out.whiffWindowMaxTicks = task.whiffWindowMaxTicks;
        out.duringGap = task.duringGap;
        out.dummyState = &task.dummyState;
        out.dummyStateMoveIds = &task.dummyStateMoveIds;
        if (task.hasContact) out.contact = &task.contact;
    }
    return out;
}

std::string TraceIntList(const std::vector<int>* values) {
    if (!values || values->empty()) return "[]";
    std::string out = "[";
    for (size_t i = 0; i < values->size(); ++i) {
        if (i != 0) out += ',';
        out += std::to_string((*values)[i]);
    }
    out += ']';
    return out;
}

std::string TraceByte(uint8_t value) {
    char out[8]{};
    _snprintf_s(out, sizeof(out), _TRUNCATE, "0x%02X",
                static_cast<unsigned>(value));
    return out;
}

const char* TraceContactSourceName(::Mission::Contact::Source source) {
    switch (source) {
        case ::Mission::Contact::Source::DirectPlayer: return "direct";
        case ::Mission::Contact::Source::Entity:       return "entity";
        default:                                       return "none";
    }
}

std::string TraceExpectedAction(const ExpectedAction& action) {
    std::string out = "req=" + (action.req ? *action.req : std::string("<missing>")) +
        " moves=" + TraceIntList(action.moveIds) +
        " from=" + TraceIntList(action.fromMoveIds) +
        " input=" + TraceByte(static_cast<uint8_t>(action.inputMask)) +
        " hits=" + std::to_string(action.hitsRequired) +
        " gap=" + std::to_string(action.minGap) + ".." +
        std::to_string(action.maxGap);
    if (action.contact) {
        out += " contact=" + action.contact->source + "/" +
            action.contact->result + " contactMoves=" +
            TraceIntList(&action.contact->moveIds) + " count=" +
            std::to_string(action.contact->count);
        if (!action.contact->targetStateBefore.empty()) {
            out += " targetBefore=" + action.contact->targetStateBefore;
        }
    } else {
        out += " contact=none";
    }
    if (action.afterWakeMaxTicks > 0) {
        out += " wakeMax=" + std::to_string(action.afterWakeMaxTicks);
    }
    if (action.whiffWindowMaxTicks > 0) {
        out += " whiffMax=" + std::to_string(action.whiffWindowMaxTicks);
    }
    if (action.duringGap) out += " duringGap=1";
    if (action.dummyState && !action.dummyState->empty()) {
        out += " dummyState=" + *action.dummyState;
    }
    if (action.dummyStateMoveIds && !action.dummyStateMoveIds->empty()) {
        out += " dummyMoves=" + TraceIntList(action.dummyStateMoveIds);
    }
    return out;
}

void CaptureTraceSampleLocked(const ::Mission::Engine::Snapshot& s) {
    g_s.traceSampleValid = true;
    g_s.traceP1Move = s.p1Move;
    g_s.traceP2Move = s.p2Move;
    g_s.traceP1Frame = s.p1FrameIdx;
    g_s.traceP2Frame = s.p2FrameIdx;
    g_s.traceCombo = s.p1Combo;
    g_s.traceComboDamage = s.p1ComboDamage;
    g_s.traceHitState = s.p1HitState;
    g_s.traceInputs = s.p1Inputs;
    g_s.traceAttackEdges = s.p1PolledAttackEdges;
    g_s.traceInputPollSerial = s.p1InputPollSerial;
    g_s.traceFreeze = s.freezeActive;
    g_s.traceP2InStun = s.p2InStun;
    g_s.traceContactCount = s.contactEventCount;
    g_s.traceContactEpoch = s.contactEventEpoch;
    g_s.traceContactOverflow = s.contactEventOverflow;
    g_s.traceDirectHookReady = s.directContactHookReady;
    g_s.traceEntityHookReady = s.entityContactHookReady;
}

std::string TraceSampleLocked() {
    if (!g_s.traceSampleValid) return "sample=<unavailable>";
    return "p1=" + std::to_string(g_s.traceP1Move) + ':' +
        std::to_string(g_s.traceP1Frame) +
        " prevP1=" + std::to_string(g_s.prevMove) + ':' +
        std::to_string(g_s.prevFrameIdx) +
        " p2=" + std::to_string(g_s.traceP2Move) + ':' +
        std::to_string(g_s.traceP2Frame) +
        " prevP2=" + std::to_string(g_s.prevP2Move) + ':' +
        std::to_string(g_s.prevP2FrameIdx) +
        " combo=" + std::to_string(g_s.traceCombo) +
        " prevCombo=" + std::to_string(g_s.prevCombo) +
        " baseline=" + std::to_string(g_s.comboBaseline) +
        " damage=" + std::to_string(g_s.traceComboDamage) +
        " hitState=" + std::to_string(g_s.traceHitState) +
        " p2Stun=" + std::to_string(g_s.traceP2InStun ? 1 : 0) +
        " freeze=" + std::to_string(g_s.traceFreeze ? 1 : 0) +
        " inputs=" + TraceByte(g_s.traceInputs) +
        " edges=" + TraceByte(g_s.traceAttackEdges) +
        " poll=" + std::to_string(g_s.traceInputPollSerial) +
        " contacts=" + std::to_string(g_s.traceContactCount) +
        " contactEpoch=" + std::to_string(g_s.traceContactEpoch) +
        " overflow=" + std::to_string(g_s.traceContactOverflow ? 1 : 0) +
        " hooks=" + std::to_string(g_s.traceDirectHookReady ? 1 : 0) +
        std::to_string(g_s.traceEntityHookReady ? 1 : 0) +
        " armed=" + std::to_string(g_s.armedCommitted ? 1 : 0) +
        " armedMove=" + std::to_string(g_s.armedMove) +
        " seqGap=" + std::to_string(g_s.sequenceGapTicks) +
        " commitGrace=" + std::to_string(g_s.inputCommitGrace) +
        " episode=" + std::to_string(g_s.episodePhase) + '/' +
        std::to_string(g_s.episodeTimer) + '/' +
        std::to_string(g_s.episodeScriptStep) + '/' +
        std::to_string(g_s.scriptStepPhase);
}

bool IsTraceRelevantMove(short move) {
    return move >= 200 || IsHitstun(move) || IsBlockstunState(move) ||
           IsLaunched(move) || IsThrown(move) || IsRecoilGuard(move) ||
           IsGroundtech(move) || IsAirtech(move);
}

void LogTraceEdgesLocked(const ::Mission::Engine::Snapshot& s,
                         const ::Mission::LessonTask& task) {
    const bool p1Edge = ::Mission::SequencePolicy::IsMoveInstanceEdge(
        s.p1Move, s.p1FrameIdx, g_s.prevMove, g_s.prevFrameIdx);
    const bool p2Edge = ::Mission::SequencePolicy::IsMoveInstanceEdge(
        s.p2Move, s.p2FrameIdx, g_s.prevP2Move, g_s.prevP2FrameIdx);
    if ((p1Edge && IsTraceRelevantMove(s.p1Move)) ||
        (p2Edge && IsTraceRelevantMove(s.p2Move)) ||
        s.p1PolledAttackEdges != 0 || s.contactEventCount != 0 ||
        s.contactEventOverflow) {
        LogOut("[TUTORIAL][TRACE][STATE] lesson=" + g_s.lesson.lessonId +
               " task=" + task.id +
               " taskIndex=" + std::to_string(g_s.task + 1) +
               " action=" + std::to_string(g_s.sequenceIndex + 1) +
               " p1Edge=" + std::to_string(p1Edge ? 1 : 0) +
               " p2Edge=" + std::to_string(p2Edge ? 1 : 0) + ' ' +
               TraceSampleLocked(), true);
    }
    for (uint8_t i = 0; i < s.contactEventCount; ++i) {
        const auto& event = s.contactEvents[i];
        LogOut("[TUTORIAL][TRACE][CONTACT] lesson=" + g_s.lesson.lessonId +
               " task=" + task.id +
               " event=" + std::to_string(event.sequence) +
               " batch=" + std::to_string(event.batchId) +
               " world=" + std::to_string(event.worldEpoch) +
               " source=" + TraceContactSourceName(event.source) +
               " result=" + ::Mission::Contact::ResultName(event.result) +
               " attacker=" + std::to_string(event.attacker) +
               " defender=" + std::to_string(event.defender) +
               " move=" + std::to_string(event.attackerMove) + ':' +
               std::to_string(event.attackerFrame) +
               " defenderMove=" + std::to_string(event.defenderMoveBefore) +
               "->" + std::to_string(event.defenderMove) +
               " timer=" + std::to_string(event.timerBefore) +
               "->" + std::to_string(event.timerAfter) +
               " combo=" + std::to_string(event.comboBefore) +
               "->" + std::to_string(event.comboAfter) +
               " hp=" + std::to_string(event.defenderHpBefore) +
               "->" + std::to_string(event.defenderHpAfter) +
               " raw=" + std::to_string(event.rawStateBefore) +
               "->" + std::to_string(event.rawStateAfter) +
               " entity=" + std::to_string(event.entitySlot) + ':' +
               std::to_string(event.entityPattern), true);
    }
}

bool ActionMoveMatches(const ExpectedAction& action, short move) {
    if (!action.moveIds) return false;
    for (int id : *action.moveIds) if (id == static_cast<int>(move)) return true;
    return false;
}

static inline bool IsThrowStartupMove(short move) {
    return move == 220 || move == 248;
}

bool ActionSourceMatches(const ExpectedAction& action, short previousMove) {
    if (!action.fromMoveIds || action.fromMoveIds->empty()) return true;
    for (int id : *action.fromMoveIds) {
        if (id == static_cast<int>(previousMove)) return true;
    }
    return false;
}

bool ActionInputMatches(const ExpectedAction& action,
                        const ::Mission::Engine::Snapshot& snapshot) {
    return action.inputMask != 0 &&
           (snapshot.p1PolledAttackEdges & static_cast<uint8_t>(action.inputMask)) != 0;
}

void FailCurrentTaskLocked(const ::Mission::LessonTask& task, const std::string& fallback) {
    const std::string reason = task.failureText.empty() ? fallback : task.failureText;
    const ExpectedAction expected = GetExpectedAction(task, g_s.sequenceIndex);
    LogOut("[TUTORIAL][TRACE][REJECT] lesson=" + g_s.lesson.lessonId +
           " task=" + task.id +
           " taskIndex=" + std::to_string(g_s.task + 1) +
           " action=" + std::to_string(g_s.sequenceIndex + 1) + "/" +
           std::to_string(ActionCount(task)) +
           " continuity=" + task.continuity +
           " reason=\"" + LogExcerpt(reason) + "\" " +
           TraceExpectedAction(expected) + ' ' + TraceSampleLocked(), true);
    ++g_s.attemptsThisRun;
    if (g_s.task >= 0 && g_s.task < static_cast<int>(g_s.tasks.size())) {
        g_s.tasks[g_s.task].failed = true;
    }
    g_s.armedCommitted = false;
    StartFeedbackLocked(false, reason);
}

void CompleteCurrentTaskLocked(const ::Mission::LessonTask& task) {
    if (g_s.task >= 0 && g_s.task < static_cast<int>(g_s.tasks.size())) {
        g_s.tasks[g_s.task].done = true;
        g_s.tasks[g_s.task].failed = false;
    }
    if (task.restoreOnSuccess) g_s.pendingSuccessRestore = true;

    const int next = FirstPendingTaskLocked();
    if (next >= 0 && next < static_cast<int>(g_s.lesson.lesson.tasks.size())) {
        const auto& nt = g_s.lesson.lesson.tasks[next];
        if (!task.restoreOnSuccess && IsCarryContinuity(nt.continuity)) {
            DirectDrawHook::AddMessage(
                (task.successText.empty() ? "Good - continue." : task.successText).c_str(),
                "TUTORIAL", RGB(160, 245, 205), 900, 20, 96);
            ArmTaskLocked(next, true);
            return;
        }
    }
    std::string text = task.successText.empty()
        ? std::string("Good.") : task.successText;
    if (task.hasCompare) {
        const TaskMetrics* vs = MetricsForTaskIdLocked(task.compareVs);
        const TaskMetrics* mine = (g_s.task >= 0 &&
            g_s.task < static_cast<int>(g_s.taskMetrics.size()) &&
            g_s.taskMetrics[g_s.task].valid) ? &g_s.taskMetrics[g_s.task] : nullptr;
        if (mine && vs) {
            text += "  [" + task.compareMetric + ": " +
                    FormatMetric(MetricValue(*mine, task.compareMetric)) +
                    " - earlier: " +
                    FormatMetric(MetricValue(*vs, task.compareMetric)) + "]";
        }
    }
    StartFeedbackLocked(true, text);
}

void BeginCompletionHoldLocked(const ::Mission::LessonTask& task) {
    LogOut("[TUTORIAL][TRACE][SATISFIED] lesson=" + g_s.lesson.lessonId +
           " task=" + task.id +
           " kind=" + task.kind +
           " taskIndex=" + std::to_string(g_s.task + 1) +
           " sequence=" + std::to_string(g_s.sequenceIndex) + "/" +
           std::to_string(ActionCount(task)) + ' ' + TraceSampleLocked(), true);
    FinalizeTaskMetricsLocked();
    if (task.hasCompare && !task.compareOp.empty()) {
        const TaskMetrics* vs = MetricsForTaskIdLocked(task.compareVs);
        if (!vs) {
            FailCurrentTaskLocked(task,
                "Complete the earlier attempt first so there is something to beat.");
            return;
        }
        const double mine = MetricValue(g_s.liveMetrics, task.compareMetric);
        const double theirs = MetricValue(*vs, task.compareMetric);
        const bool ok = ::Mission::TutorialStatePolicy::Compare(
            mine, task.compareOp, theirs);
        if (!ok) {
            FailCurrentTaskLocked(task,
                "This attempt scored " + FormatMetric(mine) + " vs " +
                FormatMetric(theirs) + " before - it must do better. Try again.");
            return;
        }
    }
    const int hold = task.completionHoldTicks >= 0
        ? task.completionHoldTicks : kCompleteHold;
    int next = -1;
    for (int i = 0; i < static_cast<int>(g_s.tasks.size()); ++i) {
        if (i != g_s.task && !g_s.tasks[i].done) { next = i; break; }
    }
    const bool carries = next >= 0 &&
        next < static_cast<int>(g_s.lesson.lesson.tasks.size()) &&
        IsCarryContinuity(g_s.lesson.lesson.tasks[next].continuity);
    if (hold <= 0 || carries) CompleteCurrentTaskLocked(task);
    else g_s.holdTicks = hold;
}

bool ContactEventMatchesAttack(const ::Mission::ContactPredicate& predicate,
                               const ::Mission::Contact::Event& event) {
    const int attacker = predicate.attacker == "dummy" ? 2 : 1;
    const int defender = predicate.target == "learner" ? 1 : 2;
    if (event.attacker != attacker) return false;
    if (event.defender != defender) return false;
    if (predicate.source == "direct" &&
        event.source != ::Mission::Contact::Source::DirectPlayer) return false;
    if (predicate.source == "entity" &&
        event.source != ::Mission::Contact::Source::Entity) return false;
    if (predicate.targetStateBefore == "downed" &&
        !IsGroundtech(event.defenderMoveBefore)) return false;
    return std::find(predicate.moveIds.begin(), predicate.moveIds.end(),
                     static_cast<int>(event.attackerMove)) != predicate.moveIds.end();
}

const ::Mission::Contact::Event* FindContactEvent(
    const ::Mission::ContactPredicate& predicate,
    const ::Mission::Engine::Snapshot& snapshot) {
    const ::Mission::Contact::Event* wrongOutcome = nullptr;
    const ::Mission::Contact::Event* unknownOutcome = nullptr;
    for (uint8_t i = 0; i < snapshot.contactEventCount; ++i) {
        const auto& event = snapshot.contactEvents[i];
        if (!ContactEventMatchesAttack(predicate, event)) continue;
        if (event.result != ::Mission::Contact::Result::Unknown &&
            ::Mission::Contact::ResultMatches(predicate.result,
                                               event.result)) {
            return &event;
        }
        if (event.result == ::Mission::Contact::Result::Unknown) {
            if (!unknownOutcome) unknownOutcome = &event;
        } else if (!wrongOutcome) {
            wrongOutcome = &event;
        }
    }
    return wrongOutcome ? wrongOutcome : unknownOutcome;
}

// Field types mirror mission_setup's write side: sp u16, hp i32, rf double, guard float.
bool ReadStateValue(int player, const std::string& field, double& out) {
    const uintptr_t base = GetPlayerBase(player);
    if (!base) return false;
    if (field == "sp") {
        uint16_t v = 0;
        if (!SafeReadMemory(base + METER_OFFSET, &v, sizeof(v))) return false;
        out = v; return true;
    }
    if (field == "hp") {
        int v = 0;
        if (!SafeReadMemory(base + HP_OFFSET, &v, sizeof(v))) return false;
        out = v; return true;
    }
    if (field == "rf") {
        // RF at +0x118 is a DOUBLE (8 bytes); a float read returns garbage.
        double v = 0.0;
        if (!SafeReadMemory(base + RF_OFFSET, &v, sizeof(v))) return false;
        out = v; return true;
    }
    if (field == "guard") {
        float v = 0.0f;
        if (!SafeReadMemory(base + PLAYER_GUARD_GAUGE_OFFSET, &v, sizeof(v))) return false;
        out = v; return true;
    }
    if (field == "distance") {
        const uintptr_t other = GetPlayerBase(player == 1 ? 2 : 1);
        if (!other) return false;
        double x1 = 0.0, x2 = 0.0;
        if (!SafeReadMemory(base + XPOS_OFFSET, &x1, sizeof(x1)) ||
            !SafeReadMemory(other + XPOS_OFFSET, &x2, sizeof(x2))) return false;
        out = x1 >= x2 ? x1 - x2 : x2 - x1;
        return true;
    }
    if (field == "ic") {
        // IC latch +0x120: 0 = red, nonzero = blue; exposed as 0/1.
        int v = 0;
        if (!SafeReadMemory(base + IC_COLOR_OFFSET, &v, sizeof(v))) return false;
        out = (v != 0) ? 1.0 : 0.0;
        return true;
    }
    if (field == "untech") {
        short v = 0;
        if (!SafeReadMemory(base + UNTECH_OFFSET, &v, sizeof(v))) return false;
        out = v; return true;
    }
    if (field == "launched" || field == "airtech" || field == "downed") {
        short move = 0;
        if (!SafeReadMemory(base + MOVE_ID_OFFSET, &move, sizeof(move))) return false;
        out = field == "launched" ? (IsLaunched(move) ? 1.0 : 0.0)
            : field == "airtech"  ? (IsAirtech(move) ? 1.0 : 0.0)
                                  : (IsGroundtech(move) ? 1.0 : 0.0);
        return true;
    }
    return false;
}

bool GoalStateMet(const ::Mission::StatePredicate& p) {
    double v = 0.0;
    if (!ReadStateValue(p.player, p.field, v)) return false;
    return ::Mission::TutorialStatePolicy::Compare(
        v, p.op, static_cast<double>(p.value));
}

void BeginTaskMetricsLocked() {
    g_s.liveMetrics = TaskMetrics{};
    (void)ReadStateValue(1, "rf", g_s.liveMetrics.p1RfStart);
    (void)ReadStateValue(2, "hp", g_s.liveMetrics.p2HpStart);
    (void)ReadStateValue(2, "guard", g_s.liveMetrics.p2GuardStart);
    g_s.metricsLive = true;
}

void FinalizeTaskMetricsLocked() {
    if (!g_s.metricsLive) return;
    (void)ReadStateValue(1, "rf", g_s.liveMetrics.p1RfEnd);
    (void)ReadStateValue(2, "hp", g_s.liveMetrics.p2HpEnd);
    (void)ReadStateValue(2, "guard", g_s.liveMetrics.p2GuardEnd);
    g_s.liveMetrics.valid = true;
    if (g_s.task >= 0 && g_s.task < static_cast<int>(g_s.taskMetrics.size())) {
        g_s.taskMetrics[g_s.task] = g_s.liveMetrics;
    }
    g_s.metricsLive = false;
}

double MetricValue(const TaskMetrics& m, const std::string& name) {
    if (name == "comboHits")   return m.comboHits;
    if (name == "comboDamage") return m.comboDamage;
    if (name == "ticks")       return m.ticks;
    if (name == "p2HpDelta")   return m.p2HpStart - m.p2HpEnd;
    if (name == "p1RfDelta")   return m.p1RfEnd - m.p1RfStart;
    if (name == "p2GuardDelta")return m.p2GuardEnd - m.p2GuardStart;
    if (name == "maxUntech")   return m.maxUntech;
    if (name == "untechTicks") return m.untechTicks;
    return 0.0;
}

const TaskMetrics* MetricsForTaskIdLocked(const std::string& id) {
    for (size_t i = 0; i < g_s.lesson.lesson.tasks.size() &&
                       i < g_s.taskMetrics.size(); ++i) {
        if (g_s.lesson.lesson.tasks[i].id == id) {
            return g_s.taskMetrics[i].valid ? &g_s.taskMetrics[i] : nullptr;
        }
    }
    return nullptr;
}

std::string FormatMetric(double v) {
    char buf[32];
    if (v == static_cast<long long>(v)) {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%lld", static_cast<long long>(v));
    } else {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.1f", v);
    }
    return buf;
}

void TickCombat(const ::Mission::Engine::Snapshot& s) {
    const ::Mission::LessonTask* t = CurrentTaskLocked();
    if (!t) { EnterCompleteLocked(); return; }
    CaptureTraceSampleLocked(s);
    LogTraceEdgesLocked(s, *t);
    TickEpisodeLocked(s);
    if (g_s.episodeCueBlockedThisTick && !t->hasAbsence) {
        return;
    }
    if (g_s.metricsLive) {
        if (!s.freezeActive) ++g_s.liveMetrics.ticks;
        if (s.p1Combo > g_s.liveMetrics.comboHits) {
            g_s.liveMetrics.comboHits = s.p1Combo;
        }
        if (s.p1ComboDamage > g_s.liveMetrics.comboDamage) {
            g_s.liveMetrics.comboDamage = s.p1ComboDamage;
        }
        double untech = 0.0;
        if (ReadStateValue(2, "untech", untech)) {
            if (static_cast<int>(untech) > g_s.liveMetrics.maxUntech) {
                g_s.liveMetrics.maxUntech = static_cast<int>(untech);
            }
            if (!s.freezeActive && untech > 0.0) {
                ++g_s.liveMetrics.untechTicks;
            }
        }
    }
    {
        const bool p2WakeEdge = ::Mission::TutorialStatePolicy::IsWakeExit(
            IsGroundtech(g_s.prevP2Move), IsGroundtech(s.p2Move));
        if (p2WakeEdge) g_s.sinceP2Wake = 0;
        else if (g_s.sinceP2Wake >= 0 && !s.freezeActive) ++g_s.sinceP2Wake;
    }
    {
        const bool p2AttackNow = s.p2Move >= 200;
        const bool p2AttackPrev = g_s.prevP2Move >= 200;
        if (p2AttackNow && !p2AttackPrev) {
            g_s.p1TouchedByP2Attack = false;
            g_s.sinceP2Whiff = -1;
        }
        if (p2AttackNow && (IsHitstun(s.p1Move) || IsBlockstunState(s.p1Move) ||
                            IsLaunched(s.p1Move))) {
            g_s.p1TouchedByP2Attack = true;
        }
        if (!p2AttackNow && p2AttackPrev && !g_s.p1TouchedByP2Attack) {
            g_s.sinceP2Whiff = 0;
        } else if (g_s.sinceP2Whiff >= 0 && !s.freezeActive) {
            ++g_s.sinceP2Whiff;
        }
    }
    if (g_s.holdTicks > 0) {
        if (--g_s.holdTicks <= 0) CompleteCurrentTaskLocked(*t);
        return;
    }
    if (g_s.waitingComboEnd) {
        const bool alive = s.p2InStun || s.p1Combo > 0;
        if (!alive) {
            g_s.waitingComboEnd = false;
            BeginCompletionHoldLocked(*t);
        }
        return;
    }
    if (t->hasBranch) {
        const bool instanceEdgeB = ::Mission::SequencePolicy::IsMoveInstanceEdge(
            s.p1Move, s.p1FrameIdx, g_s.prevMove, g_s.prevFrameIdx);
        const bool p2HitEdgeB =
            (IsHitstun(s.p2Move) || IsLaunched(s.p2Move)) &&
            !(IsHitstun(g_s.prevP2Move) || IsLaunched(g_s.prevP2Move));
        const bool p2BlockEdgeB = IsBlockstunState(s.p2Move) &&
                                  !IsBlockstunState(g_s.prevP2Move);
        if (g_s.branchPhase == 0) {
            bool starter = false;
            if (instanceEdgeB) {
                for (int id : t->branchStarterIds) {
                    if (id == static_cast<int>(s.p1Move)) { starter = true; break; }
                }
            }
            if (starter) {
                g_s.branchTicks = 0;
                g_s.branchComboBase = s.p1Combo;
                g_s.branchActionCommitted = false;
                g_s.branchPhase =
                    ::Mission::TutorialStatePolicy::BranchPhaseForOutcomeEdge(
                        p2HitEdgeB, p2BlockEdgeB);
            }
            return;
        }
        if (g_s.branchPhase == 3) {
            if (p2HitEdgeB) {
                g_s.branchPhase = 1;
                g_s.branchSeqIndex = 0;
                g_s.branchTicks = 0;
                g_s.branchActionCommitted = false;
            } else if (p2BlockEdgeB) {
                g_s.branchPhase = 2;
                g_s.branchTicks = 0;
            } else if (s.p1Move >= 0 && s.p1Move <= 3 &&
                       !s.freezeActive && ++g_s.branchTicks > 30) {
                g_s.branchPhase = 0;
                g_s.branchTicks = 0;
                g_s.feedbackSuccess = false;
                g_s.feedbackText = "It whiffed - get closer and start again.";
                g_s.feedbackTicks = 180;
            }
            return;
        }
        if (g_s.branchPhase == 1) {
            if (g_s.branchSeqIndex >= static_cast<int>(t->branchOnHit.size())) {
                if (t->endsCombo && (s.p2InStun || s.p1Combo > 0)) {
                    g_s.waitingComboEnd = true;
                    return;
                }
                BeginCompletionHoldLocked(*t);
                return;
            }
            const auto& a = t->branchOnHit[g_s.branchSeqIndex];
            const bool comboAliveB = s.p2InStun || s.p1Combo > 0;
            if (!comboAliveB &&
                (g_s.branchSeqIndex > 0 || g_s.branchActionCommitted)) {
                FailCurrentTaskLocked(*t, "The confirm dropped - the combo ended early.");
                return;
            }
            if (!s.freezeActive) ++g_s.branchTicks;
            if (a.maxGap > 0 && g_s.branchTicks > a.maxGap) {
                FailCurrentTaskLocked(*t, "Too slow - continue the confirm immediately.");
                return;
            }
            bool actionEdge = false;
            if (instanceEdgeB) {
                for (int id : a.moveIds) {
                    if (id == static_cast<int>(s.p1Move)) { actionEdge = true; break; }
                }
            }
            if (actionEdge && !a.fromMoveIds.empty() &&
                std::find(a.fromMoveIds.begin(), a.fromMoveIds.end(),
                          static_cast<int>(g_s.prevMove)) == a.fromMoveIds.end()) {
                FailCurrentTaskLocked(*t,
                    "Use the displayed cancels - that follow-up started from the wrong action.");
                return;
            }
            if (actionEdge) {
                if (a.req == "move" || a.req == "commit" || a.req == "input") {
                    ++g_s.branchSeqIndex;
                    g_s.branchTicks = 0;
                } else {
                    g_s.branchActionCommitted = true;
                    g_s.branchComboBase = s.p1Combo > g_s.prevCombo
                        ? g_s.prevCombo : s.p1Combo;
                }
            }
            if (g_s.branchActionCommitted) {
                const int gained = s.p1Combo - g_s.branchComboBase;
                const bool landed = a.req == "land" ? gained >= 1
                    : a.req == "hits" ? gained >= a.hitsRequired
                    : false;
                if (landed) {
                    g_s.branchActionCommitted = false;
                    ++g_s.branchSeqIndex;
                    g_s.branchTicks = 0;
                }
            }
            return;
        }
        const bool newAttack = instanceEdgeB && s.p1Move >= 200 &&
                               !IsThrowStartupMove(s.p1Move);
        if (newAttack) {
            FailCurrentTaskLocked(*t, "They BLOCKED - do not press the route into a guard.");
            return;
        }
        if (!s.freezeActive) ++g_s.branchTicks;
        if (g_s.branchTicks >= t->branchOnBlockHoldTicks) {
            BeginCompletionHoldLocked(*t);
        }
        return;
    }
    if (t->hasGoalState) {
        if (t->goalState.requiresBlock &&
            !IsBlockstunState(g_s.prevMove) && IsBlockstunState(s.p1Move)) {
            g_s.goalBlockSeen = true;
        }
        if (GoalStateMet(t->goalState) &&
            (!t->goalState.requiresBlock || g_s.goalBlockSeen)) {
            BeginCompletionHoldLocked(*t);
        }
        return;
    }
    if (t->hasAbsence) {
        if (g_s.absencePhase == 0) {
            double p2Untech = 0.0;
            const bool opens = t->absenceStart == "taskArmed" ||
                (t->absenceStart == "launched" &&
                 !IsLaunched(g_s.prevMove) && IsLaunched(s.p1Move)) ||
                (t->absenceStart == "dummyUntech" &&
                 ReadStateValue(2, "untech", p2Untech) &&
                 ::Mission::TutorialStatePolicy::IsUntechActive(p2Untech));
            if (opens) {
                g_s.absencePhase = 1;
                g_s.absenceElapsed = 0;
                g_s.absenceDummyAttackSeen = false;
                g_s.absenceBlockCount =
                    (IsBlockstunState(s.p1Move) &&
                     ::Mission::SequencePolicy::IsMoveInstanceEdge(
                         s.p1Move, s.p1FrameIdx, g_s.prevMove, g_s.prevFrameIdx))
                    ? 1 : 0;
                g_s.absenceEpisodeCycleBase = g_s.episodeCycleSerial;
                LogOut("[TUTORIAL][ABSENCE] opened lesson=" +
                           g_s.lesson.lessonId + " task=" + t->id +
                           " cycle=" +
                           std::to_string(g_s.absenceEpisodeCycleBase) +
                           " blocks=" +
                           std::to_string(g_s.absenceBlockCount),
                       true);
            }
            return;
        }
        if (IsBlockstunState(s.p1Move) &&
            ::Mission::SequencePolicy::IsMoveInstanceEdge(
                s.p1Move, s.p1FrameIdx, g_s.prevMove, g_s.prevFrameIdx)) {
            ++g_s.absenceBlockCount;
            LogOut("[TUTORIAL][ABSENCE] block witness lesson=" +
                       g_s.lesson.lessonId + " task=" + t->id +
                       " count=" + std::to_string(g_s.absenceBlockCount) +
                       " move=" + std::to_string(s.p1Move) +
                       " frame=" + std::to_string(s.p1FrameIdx),
                   true);
        }
        if (t->absenceEnd == "dummyAttackEnd" &&
            IsDummyAttackStart(g_s.prevP2Move, s.p2Move)) {
            g_s.absenceDummyAttackSeen = true;
        }
        bool forbidden = false;
        if (!t->absenceForbidIds.empty()) {
            for (int id : t->absenceForbidIds) {
                if (id == static_cast<int>(s.p1Move)) { forbidden = true; break; }
            }
        } else if (t->absenceForbid == "airtech") {
            forbidden = IsAirtech(s.p1Move);
        } else if (t->absenceForbid == "attack") {
            const bool instanceEdge = ::Mission::SequencePolicy::IsMoveInstanceEdge(
                s.p1Move, s.p1FrameIdx, g_s.prevMove, g_s.prevFrameIdx);
            forbidden = IsNewForbiddenAbsenceAttack(
                instanceEdge, static_cast<int>(s.p1Move));
        }
        if (forbidden) {
            g_s.absencePhase = 0;
            FailCurrentTaskLocked(*t, t->failureText.empty()
                ? std::string("Hold still - this task is about NOT doing that.")
                : t->failureText);
            return;
        }
        if ((t->absenceFailOnHit || t->absenceEnd == "dummyAttackEnd") &&
            (IsHitstun(s.p1Move) || IsLaunched(s.p1Move) || IsThrown(s.p1Move))) {
            g_s.absencePhase = 0;
            FailCurrentTaskLocked(*t, t->failureText.empty()
                ? std::string("You were hit. Block the authored attack without pressing an attack.")
                : t->failureText);
            return;
        }
        bool closed = false;
        if (t->absenceEnd == "grounded") {
            closed = !IsLaunched(s.p1Move) && !IsAirtech(s.p1Move) &&
                     !IsGroundtech(s.p1Move) && IsActionable(s.p1Move);
            if (closed) {
                // IsActionable counts airborne FALLING as actionable; ground
                // is y >= 0.
                const uintptr_t p1 = GetPlayerBase(1);
                double y = -1.0;
                closed = p1 && SafeReadMemory(p1 + YPOS_OFFSET, &y, sizeof(y)) &&
                         y >= 0.0;
            }
        } else if (t->absenceEnd == "dummyUntechEmpty") {
            double p2Untech = 0.0;
            if (ReadStateValue(2, "untech", p2Untech) &&
                ::Mission::TutorialStatePolicy::IsUntechEmpty(p2Untech)) {
                const uintptr_t p2 = GetPlayerBase(2);
                double y = 0.0;
                if (p2 && SafeReadMemory(p2 + YPOS_OFFSET, &y, sizeof(y))) {
                    if (y < 0.0) {
                        closed = true;
                    } else {
                        g_s.absencePhase = 0;
                        FailCurrentTaskLocked(*t,
                            "The dummy landed before the gauge expired. Hit the next jump higher and watch again.");
                        return;
                    }
                }
            }
        } else if (t->absenceEnd == "dummyAttackEnd") {
            closed = IsDummyAttackEnd(g_s.absenceDummyAttackSeen,
                                      g_s.prevP2Move, s.p2Move);
        } else if (t->absenceEnd == "episodeCycleEnd") {
            closed = g_s.episodeCycleSerial > g_s.absenceEpisodeCycleBase;
        } else {
            if (!s.freezeActive) ++g_s.absenceElapsed;
            closed = g_s.absenceElapsed >= t->absenceTicks;
        }
        if (closed) {
            g_s.absencePhase = 0;
            LogOut("[TUTORIAL][ABSENCE] closed lesson=" +
                       g_s.lesson.lessonId + " task=" + t->id +
                       " blocks=" + std::to_string(g_s.absenceBlockCount) +
                       " required=" + std::to_string(t->absenceMinBlocks) +
                       " cycle=" + std::to_string(g_s.episodeCycleSerial),
                   true);
            if (g_s.absenceBlockCount < t->absenceMinBlocks) {
                FailCurrentTaskLocked(
                    *t,
                    "The scripted attack did not reach your guard. Hold back and block it before the sequence ends.");
            } else {
                BeginCompletionHoldLocked(*t);
            }
        }
        return;
    }
    if (t->hasProjectileInterception) {
        const int cur = CollisionDisplay::ProbeProjectileLifeForPattern(
            2, static_cast<uint16_t>(t->incomingProjectilePattern));
        const int guardLife = CollisionDisplay::ProbeProjectileLifeForPattern(
            1, static_cast<uint16_t>(t->guardProjectilePattern));
        if (cur == CollisionDisplay::kProjectileLifeProbeUnavailable ||
            guardLife == CollisionDisplay::kProjectileLifeProbeUnavailable) {
            return;
        }
        const bool guardAlive = guardLife >= 0;
        if (g_s.interceptionPrevLife >= 1 && cur < 1 && guardAlive) {
            g_s.interceptionPrevLife = -1;
            BeginCompletionHoldLocked(*t);
            return;
        }
        g_s.interceptionPrevLife = cur;
        return;
    }
    if (g_s.comboBaseline < 0) g_s.comboBaseline = g_s.prevCombo;

    const int actionCount = ActionCount(*t);
    if (g_s.sequenceIndex < 0 || g_s.sequenceIndex >= actionCount) {
        CompleteCurrentTaskLocked(*t);
        return;
    }
    const ExpectedAction expected = GetExpectedAction(*t, g_s.sequenceIndex);
    if (!expected.req) {
        FailCurrentTaskLocked(*t, "This task has no valid action contract.");
        return;
    }
    if (g_s.traceExpectedIndex != g_s.sequenceIndex) {
        g_s.traceExpectedIndex = g_s.sequenceIndex;
        LogOut("[TUTORIAL][TRACE][EXPECT] lesson=" + g_s.lesson.lessonId +
               " task=" + t->id +
               " taskIndex=" + std::to_string(g_s.task + 1) +
               " action=" + std::to_string(g_s.sequenceIndex + 1) + "/" +
               std::to_string(actionCount) +
               " continuity=" + t->continuity + ' ' +
               TraceExpectedAction(expected), true);
    }

    const bool instanceEdge = ::Mission::SequencePolicy::IsMoveInstanceEdge(
        s.p1Move, s.p1FrameIdx, g_s.prevMove, g_s.prevFrameIdx);
    const bool destinationEdge = instanceEdge && ActionMoveMatches(expected, s.p1Move);
    const bool sourceRequired = expected.fromMoveIds && !expected.fromMoveIds->empty();
    const bool sourceMatches = ActionSourceMatches(expected, g_s.prevMove);
    const bool moveMatches = ::Mission::SequencePolicy::DirectTransitionSatisfied(
        destinationEdge, sourceRequired, sourceMatches);
    const bool inputMatches = ActionInputMatches(expected, s);
    if (*expected.req == "commit" && inputMatches) {
        g_s.inputCommitGrace = ::Mission::SequencePolicy::DelayedActionWindow::kCommitGraceTicks;
        g_s.inputCommitMask = static_cast<uint8_t>(expected.inputMask);
    } else if (!s.freezeActive && g_s.inputCommitGrace > 0) {
        --g_s.inputCommitGrace;
    }
    const ::Mission::Contact::Event* contactEvent = expected.contact
        ? FindContactEvent(*expected.contact, s) : nullptr;
    const bool contactOnly = expected.contact && expected.moveIds &&
                             expected.moveIds->empty() && expected.inputMask == 0;
    const bool committedInput = inputMatches ||
        (g_s.inputCommitGrace > 0 &&
         g_s.inputCommitMask == static_cast<uint8_t>(expected.inputMask));

    if (expected.hasFlickerIC) {
        if (!s.directContactHookReady || !s.entityContactHookReady) {
            g_s.phase = Phase::Error;
            g_s.feedbackSuccess = false;
            g_s.feedbackText =
                "The FIC contact detectors are unavailable in this game build.";
            SetNeutralInputLease(false);
            SetFreeze(true);
            StopTaskEpisodeLocked();
            return;
        }
        if (s.contactEventOverflow) {
            g_s.phase = Phase::Error;
            g_s.feedbackSuccess = false;
            g_s.feedbackText =
                "FIC contact evidence overflowed; restart the lesson for a clean attempt.";
            SetNeutralInputLease(false);
            SetFreeze(true);
            StopTaskEpisodeLocked();
            return;
        }
        for (uint8_t i = 0; i < s.contactEventCount; ++i) {
            const auto& event = s.contactEvents[i];
            if (event.attacker != 1 || event.defender != 2 ||
                event.result == ::Mission::Contact::Result::None) {
                continue;
            }
            const bool sourceDirect =
                event.source == ::Mission::Contact::Source::DirectPlayer &&
                expected.fromMoveIds &&
                std::find(expected.fromMoveIds->begin(), expected.fromMoveIds->end(),
                          static_cast<int>(event.attackerMove)) !=
                    expected.fromMoveIds->end();
            const bool sourceProjectile =
                event.source == ::Mission::Contact::Source::Entity &&
                (event.attackerMove == expected.flickerProjectilePattern ||
                 event.entityPattern == expected.flickerProjectilePattern);
            if (sourceDirect || sourceProjectile) g_s.flickerContactSeen = true;
        }
        if (g_s.flickerContactSeen) {
            FailCurrentTaskLocked(*t,
                "The attack connected first. FIC it while the projectile is still travelling.");
            return;
        }
    }
    const bool expectedStarted = *expected.req == "input" ? inputMatches
        : *expected.req == "commit" ? (moveMatches && committedInput)
        : contactOnly ? contactEvent != nullptr : moveMatches;
    const bool contactEdge = g_s.prevHitState == 0 && s.p1HitState != 0;
    const bool comboAlive = s.p2InStun || s.p1Combo > 0;
    if ((instanceEdge && IsTraceRelevantMove(s.p1Move)) || inputMatches ||
        contactEvent != nullptr) {
        LogOut("[TUTORIAL][TRACE][MATCH] lesson=" + g_s.lesson.lessonId +
               " task=" + t->id +
               " action=" + std::to_string(g_s.sequenceIndex + 1) +
               " instanceEdge=" + std::to_string(instanceEdge ? 1 : 0) +
               " destination=" + std::to_string(destinationEdge ? 1 : 0) +
               " source=" + std::to_string(sourceMatches ? 1 : 0) +
               " moveMatch=" + std::to_string(moveMatches ? 1 : 0) +
               " inputMatch=" + std::to_string(inputMatches ? 1 : 0) +
               " committedInput=" + std::to_string(committedInput ? 1 : 0) +
               " contactMatch=" + std::to_string(contactEvent ? 1 : 0) +
               " started=" + std::to_string(expectedStarted ? 1 : 0) + ' ' +
               TraceSampleLocked(), true);
    }

    if (t->continuity == "sameCombo" && g_s.sequenceIndex > 0 &&
        g_s.comboWasAlive && !comboAlive) {
        FailCurrentTaskLocked(*t, "The combo ended before the route was complete.");
        return;
    }

    if (destinationEdge && sourceRequired && !sourceMatches) {
        FailCurrentTaskLocked(*t,
            "That action did not come directly from the shown cancel. Try the sequence again.");
        return;
    }
    if (ShouldRejectUnprovenCommit(destinationEdge, *expected.req == "commit",
                                   committedInput)) {
        FailCurrentTaskLocked(*t,
            "That action used a different button. Use the button shown in the route.");
        return;
    }
    if (expected.hasFlickerIC && destinationEdge) {
        double defenderHp = 0.0;
        if (!ReadStateValue(2, "hp", defenderHp)) {
            g_s.phase = Phase::Error;
            g_s.feedbackSuccess = false;
            g_s.feedbackText =
                "The defender state needed for the FIC check could not be read.";
            SetNeutralInputLease(false);
            SetFreeze(true);
            StopTaskEpisodeLocked();
            return;
        }
        const int projectileLife =
            CollisionDisplay::ProbeProjectileLifeForPattern(
                1, static_cast<uint16_t>(expected.flickerProjectilePattern));
        if (projectileLife == CollisionDisplay::kProjectileLifeProbeUnavailable) {
            g_s.phase = Phase::Error;
            g_s.feedbackSuccess = false;
            g_s.feedbackText =
                "The FIC projectile state could not be read in this game build.";
            SetNeutralInputLease(false);
            SetFreeze(true);
            StopTaskEpisodeLocked();
            return;
        }
        const bool defenderUntouched = !s.p2InStun &&
            s.p1Combo <= g_s.comboBaseline &&
            s.p1ComboDamage == g_s.flickerDamageBaseline &&
            static_cast<int>(defenderHp) == g_s.flickerDefenderHpBaseline;
        if (!FlickerICProofSatisfied(destinationEdge, sourceMatches,
                                     committedInput,
                                     g_s.flickerSourceDistanceSafe,
                                     projectileLife >= 1,
                                     g_s.flickerContactSeen,
                                     defenderUntouched)) {
            FailCurrentTaskLocked(*t, projectileLife < 1
                ? "Wait until the projectile is visible, then input 22C during the FIC window."
                : "FIC before the projectile reaches the dummy, using the spacing shown.");
            return;
        }
    }

    if (!g_s.armedCommitted && expectedStarted) {
        if (g_s.sequenceIndex > 0 && g_s.sequenceGapTicks < expected.minGap) {
            FailCurrentTaskLocked(*t, "That action was too early. Follow the shown timing.");
            return;
        }
        g_s.armedCommitted = true;
        g_s.armedMove = moveMatches ? s.p1Move
                      : contactEvent ? contactEvent->attackerMove : s.p1Move;
        g_s.comboBaseline = s.p1Combo > g_s.prevCombo ? g_s.prevCombo
                           : s.p1Combo < g_s.prevCombo ? 0 : s.p1Combo;
        LogOut("[TUTORIAL][TRACE][ARM] lesson=" + g_s.lesson.lessonId +
               " task=" + t->id +
               " action=" + std::to_string(g_s.sequenceIndex + 1) +
               " armedMove=" + std::to_string(g_s.armedMove) +
               " baseline=" + std::to_string(g_s.comboBaseline) + ' ' +
               TraceExpectedAction(expected) + ' ' + TraceSampleLocked(), true);
    }

    if (g_s.armedCommitted) {
        const int gained = s.p1Combo - g_s.comboBaseline;
        bool satisfied = false;
        if (expected.contact) {
            const bool contactHookReady = expected.contact->source == "entity"
                ? s.entityContactHookReady : s.directContactHookReady;
            if (!contactHookReady) {
                g_s.phase = Phase::Error;
                g_s.feedbackSuccess = false;
                g_s.feedbackText = expected.contact->source == "entity"
                    ? "The strict projectile-contact detector is unavailable in this game build."
                    : "The strict contact detector is unavailable in this game build.";
                SetNeutralInputLease(false);
                SetFreeze(true);
                StopTaskEpisodeLocked();
                return;
            }
            if (s.contactEventOverflow) {
                g_s.phase = Phase::Error;
                g_s.feedbackSuccess = false;
                g_s.feedbackText = "Contact evidence overflowed; restart the lesson for a clean attempt.";
                SetNeutralInputLease(false);
                SetFreeze(true);
                StopTaskEpisodeLocked();
                return;
            }
            if (contactEvent && contactEvent->result != ::Mission::Contact::Result::Unknown) {
                if (::Mission::Contact::ResultMatches(expected.contact->result,
                                                       contactEvent->result)) {
                    ++g_s.contactMatches;
                    satisfied = g_s.contactMatches >= expected.contact->count;
                } else {
                    FailCurrentTaskLocked(*t,
                        "That made " + std::string(::Mission::Contact::ResultName(contactEvent->result)) +
                        "; this task needs " + expected.contact->result + ".");
                    return;
                }
            }
        } else if (*expected.req == "input" || *expected.req == "move" ||
                   *expected.req == "commit") satisfied = true;
        else if (*expected.req == "block") {
            satisfied = ::Mission::SequencePolicy::FreshBlockReactionSatisfied(
                IsBlockstunState(s.p2Move), IsRecoilGuard(s.p2Move),
                s.p2Move, s.p2FrameIdx,
                g_s.prevP2Move, g_s.prevP2FrameIdx);
        }
        else if (*expected.req == "rg") {
            satisfied = !IsRecoilGuard(g_s.prevP2Move) && IsRecoilGuard(s.p2Move);
        }
        else if (*expected.req == "land") satisfied = gained >= 1;
        else if (*expected.req == "hits") satisfied = gained >= expected.hitsRequired;
        else if (*expected.req == "connect") satisfied = contactEdge;
        if (satisfied && expected.afterWakeMaxTicks > 0 &&
            !::Mission::TutorialStatePolicy::IsWithinWakeWindow(
                g_s.sinceP2Wake, expected.afterWakeMaxTicks)) {
            FailCurrentTaskLocked(*t, g_s.sinceP2Wake < 0
                ? "Score the knockdown first, then attack as the dummy rises."
                : "Too late after the wakeup. Attack the moment the dummy rises.");
            return;
        }
        if (satisfied && expected.duringGap && !g_s.scriptGapOpen) {
            FailCurrentTaskLocked(*t,
                "Act inside the marked GAP of the string - it was closed.");
            return;
        }
        if (satisfied && expected.whiffWindowMaxTicks > 0 &&
            (g_s.sinceP2Whiff < 0 ||
             g_s.sinceP2Whiff > expected.whiffWindowMaxTicks)) {
            FailCurrentTaskLocked(*t, g_s.sinceP2Whiff < 0
                ? "Wait for the dummy's attack to miss, then punish."
                : "Too slow - punish right after the dummy's attack misses.");
            return;
        }
        if (satisfied) {
            bool stateHolds = true;
            if (expected.dummyStateMoveIds && !expected.dummyStateMoveIds->empty()) {
                const short observedDummyMove =
                    expected.contact && contactEvent &&
                    expected.contact->target == "dummy"
                    ? contactEvent->defenderMoveBefore : s.p2Move;
                stateHolds = false;
                for (int id : *expected.dummyStateMoveIds) {
                    if (id == static_cast<int>(observedDummyMove)) {
                        stateHolds = true;
                        break;
                    }
                }
            } else if (expected.dummyState && !expected.dummyState->empty()) {
                const std::string& st = *expected.dummyState;
                if (st == "downed") stateHolds = IsGroundtech(s.p2Move);
                else if (st == "airtech") stateHolds = IsAirtech(s.p2Move);
                else if (st == "launched") stateHolds = IsLaunched(s.p2Move);
                else if (st == "blockstun") stateHolds = IsBlockstunState(s.p2Move);
                else if (st == "airborne") {
                    stateHolds = IsLaunched(s.p2Move) || IsAirtech(s.p2Move);
                    if (!stateHolds) {
                        const uintptr_t p2 = GetPlayerBase(2);
                        double y = 0.0;
                        stateHolds = p2 &&
                            SafeReadMemory(p2 + YPOS_OFFSET, &y, sizeof(y)) &&
                            y < 0.0;
                    }
                } else stateHolds = false;
            }
            if (!stateHolds) {
                const std::string coachText = t->failureText.empty()
                    ? std::string("Catch the dummy in the shown state - watch its position.")
                    : t->failureText;
                if (g_s.lesson.lesson.wrongAction == "coach") {
                    LogOut("[TUTORIAL][TRACE][STATE_COACH] lesson=" +
                           g_s.lesson.lessonId + " task=" + t->id +
                           " action=" + std::to_string(g_s.sequenceIndex + 1) +
                           " observedDummyMove=" + std::to_string(s.p2Move) +
                           " -> re-arm (wrong dummy state)", true);
                    g_s.armedCommitted = false;
                    g_s.inputCommitGrace = 0;
                    g_s.inputCommitMask = 0;
                    g_s.contactMatches = 0;
                    g_s.feedbackSuccess = false;
                    g_s.feedbackText = coachText;
                    g_s.feedbackTicks = 240;
                    return;
                }
                FailCurrentTaskLocked(*t, coachText);
                return;
            }
        }
        if (satisfied) {
            LogOut("[TUTORIAL][TRACE][ACCEPT] lesson=" + g_s.lesson.lessonId +
                   " task=" + t->id +
                   " action=" + std::to_string(g_s.sequenceIndex + 1) + "/" +
                   std::to_string(actionCount) +
                   " gained=" + std::to_string(gained) +
                   " contactMatches=" + std::to_string(g_s.contactMatches) + ' ' +
                   TraceExpectedAction(expected) + ' ' + TraceSampleLocked(), true);
            g_s.armedCommitted = false;
            g_s.inputCommitGrace = 0;
            g_s.inputCommitMask = 0;
            g_s.contactMatches = 0;
            ++g_s.sequenceIndex;
            g_s.sequenceGapTicks = 0;
            g_s.comboWasAlive = comboAlive;
            if (g_s.sequenceIndex >= actionCount) {
                if (t->endsCombo && comboAlive) {
                    g_s.waitingComboEnd = true;
                    return;
                }
                BeginCompletionHoldLocked(*t);
                return;
            }
            const ExpectedAction next = GetExpectedAction(*t, g_s.sequenceIndex);
            if (next.hasFlickerIC) {
                double distance = 0.0;
                double defenderHp = 0.0;
                g_s.flickerContactSeen = false;
                g_s.flickerSourceDistanceSafe = next.flickerMinDistance <= 0 ||
                    (ReadStateValue(1, "distance", distance) &&
                     distance >= static_cast<double>(next.flickerMinDistance));
                g_s.flickerDamageBaseline = s.p1ComboDamage;
                if (!ReadStateValue(2, "hp", defenderHp)) {
                    g_s.phase = Phase::Error;
                    g_s.feedbackSuccess = false;
                    g_s.feedbackText =
                        "The defender state needed for the FIC check could not be read.";
                    SetNeutralInputLease(false);
                    SetFreeze(true);
                    StopTaskEpisodeLocked();
                    return;
                }
                g_s.flickerDefenderHpBaseline = static_cast<int>(defenderHp);
                if (!g_s.flickerSourceDistanceSafe) {
                    LogOut("[TUTORIAL][TRACE][FIC_SPACING] lesson=" +
                           g_s.lesson.lessonId +
                           " distance=" + std::to_string(distance) +
                           " min=" + std::to_string(next.flickerMinDistance) +
                           " -> re-arm source (coach reposition)", true);
                    --g_s.sequenceIndex;
                    g_s.armedCommitted = false;
                    g_s.feedbackSuccess = false;
                    g_s.feedbackText =
                        "Too close - step BACK to long range first, then cast.";
                    g_s.feedbackTicks = 240;
                    return;
                }
            }
            if (ShouldCarryCommitEdge(s.freezeActive,
                                      next.req && *next.req == "commit",
                                      ActionInputMatches(next, s))) {
                g_s.inputCommitGrace =
                    ::Mission::SequencePolicy::DelayedActionWindow::kCommitGraceTicks;
                g_s.inputCommitMask = static_cast<uint8_t>(next.inputMask);
            }
        } else if ((s.p1Move == 0 || s.p1Move == 1 ||
                    s.p1Move == 2 || s.p1Move == 3) &&
                   *expected.req != "move" && *expected.req != "input") {
            if (g_s.lesson.lesson.wrongAction == "coach") {
                LogOut("[TUTORIAL][TRACE][WHIFF_COACH] lesson=" +
                       g_s.lesson.lessonId + " task=" + t->id +
                       " action=" + std::to_string(g_s.sequenceIndex + 1) +
                       " -> re-arm (committed action never connected)", true);
                g_s.armedCommitted = false;
                g_s.inputCommitGrace = 0;
                g_s.inputCommitMask = 0;
                g_s.contactMatches = 0;
                g_s.feedbackSuccess = false;
                g_s.feedbackText = t->failureText.empty()
                    ? std::string("That action did not connect. Try again.")
                    : t->failureText;
                g_s.feedbackTicks = 240;
                return;
            }
            FailCurrentTaskLocked(*t, "That action did not connect. Try again.");
            return;
        }
    } else if (g_s.sequenceIndex > 0) {
        if (!s.freezeActive) ++g_s.sequenceGapTicks;
        if (expected.maxGap > 0 && g_s.sequenceGapTicks > expected.maxGap) {
            FailCurrentTaskLocked(*t, "The next action came too late. Try the sequence again.");
            return;
        }
    }

    const bool unrelatedAction =
        (instanceEdge && s.p1Move >= 200 && !ActionMoveMatches(expected, s.p1Move) &&
         !IsThrowStartupMove(s.p1Move)) ||
        (s.p1PolledAttackEdges != 0 && expected.inputMask != 0 && !inputMatches);
    if (!g_s.armedCommitted && unrelatedAction) {
        if (g_s.lesson.lesson.wrongAction == "fail") {
            FailCurrentTaskLocked(*t, "That was not the action shown for this task.");
        } else if (g_s.lesson.lesson.wrongAction == "coach") {
            g_s.feedbackSuccess = false;
            g_s.feedbackText = t->failureText.empty()
                ? std::string("Keep going - use the action shown above.") : t->failureText;
            g_s.feedbackTicks = 240;
        }
    }
}

void ApplyRfLockLocked() {
    const auto apply = [](int player, int rf, int blueIC, bool lock) {
        if (!lock || rf < 0) return false;
        StartRFFreezeOne(player, static_cast<double>(rf));
        if (blueIC >= 0) {
            SetRFFreezeColorDesired(player, true, blueIC == 1);
        }
        LogOut("[TUTORIAL][RF_LOCK] player=" + std::to_string(player) +
               " rf=" + std::to_string(rf) +
               " color=" + (blueIC < 0 ? std::string("free")
                            : blueIC == 1 ? std::string("blue")
                                          : std::string("red")), true);
        return true;
    };
    g_s.rfLockApplied[0] = apply(1, g_s.lesson.player.rf,
                                 g_s.lesson.player.blueIC,
                                 g_s.lesson.player.rfLock);
    g_s.rfLockApplied[1] = apply(2, g_s.lesson.dummy.rf,
                                 g_s.lesson.dummy.blueIC,
                                 g_s.lesson.dummy.rfLock);
}

void ReleaseRfLockLocked() {
    for (int player = 1; player <= 2; ++player) {
        if (!g_s.rfLockApplied[player - 1]) continue;
        StopRFFreezePlayer(player);
        SetRFFreezeColorDesired(player, false, false);
        g_s.rfLockApplied[player - 1] = false;
    }
}

}

bool Begin(const ::Mission::Mission& lesson) {
    if (lesson.tutorialSchema <= 0 || !lesson.hasLesson) return false;
    DirectDrawHook::RemoveMessagesByCategory("PRACTICE_HINT");
    std::lock_guard<std::mutex> lk(g_mx);
    RestoreJugglePreviewLocked();
    if (g_s.frozenLeaseHeld) SetFreeze(false);
    if (g_s.neutralInputLeaseHeld) SetNeutralInputLease(false);
    StopTaskEpisodeLocked();
    ReleaseRfLockLocked();
    g_s = State{};
    g_s.active = true;
    g_s.lesson = lesson;
    {
        std::lock_guard<std::mutex> rl(g_regMx);
        for (const auto& kv : g_registry) {
            const RegisteredLesson& registered = kv.second;
            if (registered.lessonId == lesson.lessonId &&
                (lesson.sourcePath.empty() || registered.path == lesson.sourcePath)) {
                g_s.packId = registered.packId.empty() ? kFallbackPackId : registered.packId;
                break;
            }
        }
    }
    g_s.uiModel = BuildUiModel(lesson, g_s.packId);
    g_s.tasks.assign(lesson.lesson.tasks.size(), TaskState{});
    g_s.taskMetrics.assign(lesson.lesson.tasks.size(), TaskMetrics{});
    g_s.page = 0;
    g_s.attemptsThisRun = 0;
    g_s.phase = Phase::Preparing;
    g_s.uiLatchNeedsSync = true;
    Tut::ProgressSetLastLesson(g_s.packId, lesson.lessonId);
    ApplyRfLockLocked();
    g_uiGeneration.fetch_add(1, std::memory_order_acq_rel);
    g_active.store(true, std::memory_order_release);
    LogOut("[TUTORIAL] session begin id=" + lesson.lessonId +
           " pages=" + std::to_string(lesson.lesson.pages.size()) +
           " tasks=" + std::to_string(lesson.lesson.tasks.size()), true);
    return true;
}

void End(const char* reason) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_s.active) return;
    SetFreeze(false);
    SetNeutralInputLease(false);
    StopTaskEpisodeLocked();
    ReleaseRfLockLocked();
    HudDisable::ResetVisible();
    g_s = State{};
    g_active.store(false, std::memory_order_release);
    g_uiGeneration.fetch_add(1, std::memory_order_acq_rel);
    LogOut(std::string("[TUTORIAL] session end (") + (reason ? reason : "?") + ")", true);
}

void NotifyStartupFailure(const std::string& message) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_s.active) return;
    StopTaskEpisodeLocked();
    SetNeutralInputLease(false);
    g_s.phase = Phase::Error;
    g_s.feedbackSuccess = false;
    g_s.feedbackText = message.empty()
        ? std::string("The lesson start state could not be prepared.") : message;
    g_s.uiLatchNeedsSync = true;
    SetFreeze(true);
    LogOut("[TUTORIAL] startup failure: " + g_s.feedbackText, true);
}

void NotifyStateLoaded() {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_s.active) return;
    AbandonJugglePreviewLocked();
    g_s.jugglePreviewSuppressedPage = -1;
    g_s.jugglePreviewObservedPage = -1;
    g_s.introWorldAdvanced = false;
    StopTaskEpisodeLocked();
    g_s.uiLatchNeedsSync = true;
    if (g_s.neutralInputLeaseHeld) {
        g_pollOverrideMask[1].store(0, std::memory_order_relaxed);
        g_pollOverrideActive[1].store(true, std::memory_order_release);
    }
    if (g_s.phase == Phase::TaskActive && g_s.task >= 0 &&
        g_s.task < static_cast<int>(g_s.tasks.size()) &&
        !g_s.tasks[g_s.task].done) {
        ArmTaskLocked(g_s.task);
    }
    LogOut("[TUTORIAL][INPUT_LEASE] savestate load reconciled task owners", true);
}

bool IsActive() { return g_active.load(std::memory_order_acquire); }

Phase GetPhase() {
    std::lock_guard<std::mutex> lk(g_mx);
    return g_s.phase;
}

bool HasStartupInputOwnership() {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_s.active) return false;
    if (g_s.phase == Phase::NeutralGate) {
        return g_s.neutralInputLeaseHeld &&
               g_pollOverrideActive[1].load(std::memory_order_acquire) &&
               g_pollOverrideMask[1].load(std::memory_order_acquire) == 0;
    }
    if (FrozenPhase(g_s.phase)) {
        return g_s.frozenLeaseHeld && g_s.freezeConfirmed &&
               g_s.neutralInputLeaseHeld &&
               g_pollOverrideActive[1].load(std::memory_order_acquire) &&
               g_pollOverrideMask[1].load(std::memory_order_acquire) == 0 &&
               PauseIntegration::IsPausedOrFrozen();
    }
    return false;
}

void BeginLessonRegistryRefresh() {
    std::lock_guard<std::mutex> lk(g_regMx);
    g_registryRefresh.clear();
    g_registryRefreshActive = true;
}

void RegisterLessonPath(const std::string& packId, const std::string& lessonId,
                        const std::string& path,
                        const std::string& displayName,
                        const std::string& nextLessonId, bool available) {
    if (lessonId.empty()) return;
    std::lock_guard<std::mutex> lk(g_regMx);
    const std::string resolvedPack = packId.empty() ? kFallbackPackId : packId;
    auto& target = g_registryRefreshActive ? g_registryRefresh : g_registry;
    target[RegistryKey(resolvedPack, lessonId)] = {
        resolvedPack, lessonId, path, displayName, nextLessonId, available
    };
}

void FinishLessonRegistryRefresh() {
    std::lock_guard<std::mutex> lk(g_regMx);
    if (!g_registryRefreshActive) return;
    if (!g_active.load(std::memory_order_acquire)) {
        g_registry.swap(g_registryRefresh);
    }
    g_registryRefresh.clear();
    g_registryRefreshActive = false;
}

bool LessonCleared() {
    std::lock_guard<std::mutex> lk(g_mx);
    return g_s.cleared;
}

std::string NextLessonName() {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_s.lesson.nextLessonId.empty()) return std::string();
    std::lock_guard<std::mutex> rl(g_regMx);
    RegisteredLesson next;
    int skipped = 0;
    return ResolveAvailableDescendantLocked(
               g_s.packId, g_s.lesson.nextLessonId, next, skipped)
        ? next.displayName : std::string();
}

std::string ProgressLabel() {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_s.active) return std::string();
    char text[64] = {};
    if (g_s.phase == Phase::Preparing) return "PREPARING LESSON";
    if (g_s.phase == Phase::Complete) return "LESSON COMPLETE";
    if (g_s.phase == Phase::Error) return "LESSON NEEDS ATTENTION";
    if ((g_s.phase == Phase::Intro || g_s.phase == Phase::Review ||
         g_s.phase == Phase::ConfirmExit) && !g_s.lesson.lesson.pages.empty()) {
        const int total = static_cast<int>(g_s.lesson.lesson.pages.size());
        _snprintf_s(text, sizeof(text), _TRUNCATE, "PAGE %d OF %d",
                    (std::min)(g_s.page + 1, total), total);
    } else {
        const int total = static_cast<int>(g_s.lesson.lesson.tasks.size());
        _snprintf_s(text, sizeof(text), _TRUNCATE, "TASK %d OF %d",
                    total > 0 ? (std::min)(g_s.task + 1, total) : 0, total);
    }
    return text;
}

bool CanRetryCurrentTask() {
    std::lock_guard<std::mutex> lk(g_mx);
    return g_s.active &&
           (g_s.phase == Phase::TaskActive || g_s.phase == Phase::NeutralGate ||
            g_s.phase == Phase::Feedback || g_s.phase == Phase::Choice);
}

void SyncPageHudLocked() {
    const bool onPage = g_s.phase == Phase::Intro ||
                        g_s.phase == Phase::Review ||
                        g_s.phase == Phase::ConfirmExit;
    if (!onPage) {
        RestoreJugglePreviewLocked();
        g_s.jugglePreviewSuppressedPage = -1;
        g_s.jugglePreviewObservedPage = -1;
        HudDisable::SetHidden(false);
        HudDisable::SetElementMask(0);
        return;
    }
    if (g_s.jugglePreviewObservedPage != g_s.page) {
        RestoreJugglePreviewLocked();
        g_s.jugglePreviewObservedPage = g_s.page;
        g_s.jugglePreviewSuppressedPage = -1;
    }
    HudPolicy::HudFocus focus = HudPolicy::HudFocus::None;
    if (g_s.uiModel && g_s.page >= 0 &&
        g_s.page < static_cast<int>(g_s.uiModel->pages.size())) {
        focus = g_s.uiModel->pages[static_cast<size_t>(g_s.page)].hudFocus;
    }

    const bool keepWorldIndicators = HudPolicy::IsJuggleFocus(focus);
    if (keepWorldIndicators) {
        (void)StageJugglePreviewLocked(focus);
    } else {
        RestoreJugglePreviewLocked();
    }

    unsigned visibleMask = 0;
    switch (focus) {
        case HudPolicy::HudFocus::Top:
            visibleMask = HudDisable::GroupTop;
            break;
        case HudPolicy::HudFocus::Bottom:
        case HudPolicy::HudFocus::Meters:
        case HudPolicy::HudFocus::MeterStates:
            visibleMask = HudDisable::GroupBottom;
            break;
        case HudPolicy::HudFocus::Life:
            visibleMask = HudDisable::ElemTopBar | HudDisable::ElemTimer |
                          HudDisable::ElemHpBars | HudDisable::ElemRoundDots;
            break;
        case HudPolicy::HudFocus::Sp:
            visibleMask = HudDisable::ElemBottomBar | HudDisable::ElemSpMeter;
            break;
        case HudPolicy::HudFocus::Rf:
        case HudPolicy::HudFocus::RfStates:
        case HudPolicy::HudFocus::RedIc:
        case HudPolicy::HudFocus::BlueIc:
            visibleMask = HudDisable::ElemBottomBar | HudDisable::ElemRfGauge;
            break;
        case HudPolicy::HudFocus::BlueIcMeters:
            visibleMask = HudDisable::ElemBottomBar |
                          HudDisable::ElemSpMeter |
                          HudDisable::ElemRfGauge;
            break;
        case HudPolicy::HudFocus::FinalMemory:
            visibleMask = HudDisable::ElemTopBar | HudDisable::ElemHpBars |
                          HudDisable::ElemBottomBar | HudDisable::ElemSpMeter;
            break;
        case HudPolicy::HudFocus::None:
        case HudPolicy::HudFocus::Juggle:
        case HudPolicy::HudFocus::JuggleYellow:
        case HudPolicy::HudFocus::JuggleRed:
        default:
            break;
    }

    if (visibleMask == 0) {
        HudDisable::SetHidden(!keepWorldIndicators);
        HudDisable::SetElementMask(
            keepWorldIndicators ? HudDisable::GroupAll : 0);
        return;
    }
    HudDisable::SetHidden(false);
    HudDisable::SetElementMask(HudDisable::GroupAll & ~visibleMask);
}

void Tick(const ::Mission::Engine::Snapshot& s) {
    if (!IsActive()) return;
    DeferredRequest deferred;
    std::string deferredError;
    std::unique_lock<std::mutex> lk(g_mx);
    if (!g_s.active) return;

    bool refreshSuppressesInput = false;
    if (FrozenPhase(g_s.phase)) {
        SetNeutralInputLease(true);
        if (g_s.jugglePreview.refreshActive) {
            refreshSuppressesInput = TickJugglePreviewRefreshLocked();
        }
        if (!::Mission::PauseMenu::IsOpen() &&
            !g_s.jugglePreview.refreshActive) {
            EnsureFreezeLocked();
        }
    }
    if (::Mission::PauseMenu::IsOpen()) {
        (void)PollUiEdges(true);
        g_s.uiLatchNeedsSync = true;
        g_s.prevMove = s.p1Move; g_s.prevP2Move = s.p2Move;
        g_s.prevFrameIdx = s.p1FrameIdx; g_s.prevP2FrameIdx = s.p2FrameIdx;
        g_s.prevCombo = s.p1Combo; g_s.prevHitState = s.p1HitState;
        g_s.comboWasAlive = s.p2InStun || s.p1Combo > 0;
        return;
    }

    const UiInput sampledInput = PollUiEdges(g_s.uiLatchNeedsSync);
    g_s.uiLatchNeedsSync = false;
    // Never navigate a teaching surface until the physical pause has been observed. Polling still
    // updates latches, so a confirm held through PAUSING cannot fire when the pause becomes valid.
    const UiInput in = FrozenPhase(g_s.phase) && g_s.freezeConfirmed &&
                       !refreshSuppressesInput
        ? sampledInput : UiInput{};

    if (g_s.phase == Phase::Preparing) {
        g_s.uiLatchNeedsSync = true;
        if (!g_s.lesson.lesson.pages.empty()) {
            g_s.phase = Phase::Intro;
            SetFreeze(true);
        } else if (!g_s.lesson.lesson.tasks.empty()) {
            ArmTaskLocked(0);
        } else {
            EnterCompleteLocked();
        }
        g_s.prevMove = s.p1Move; g_s.prevP2Move = s.p2Move;
        g_s.prevFrameIdx = s.p1FrameIdx; g_s.prevP2FrameIdx = s.p2FrameIdx;
        g_s.prevCombo = s.p1Combo; g_s.prevHitState = s.p1HitState;
        g_s.comboWasAlive = s.p2InStun || s.p1Combo > 0;
        SyncPageHudLocked();
        return;
    }

    if (g_s.phase == Phase::DemoSuspended) {
        std::string restoreMessage;
        const auto restoreResult =
            ::Mission::Engine::Demo::PeekTutorialRestore(restoreMessage);
        if (restoreResult == ::Mission::Engine::Demo::RestoreResult::None) {
            return;
        }

        g_s.uiLatchNeedsSync = true;
        if (restoreResult ==
            ::Mission::Engine::Demo::RestoreResult::RestoreFailed) {
            StopTaskEpisodeLocked();
            g_s.phase = Phase::Error;
            g_s.feedbackSuccess = false;
            g_s.feedbackText =
                "The demonstration could not restore the lesson start state";
            if (!restoreMessage.empty()) {
                g_s.feedbackText += ": " + restoreMessage;
            }
            g_s.feedbackText +=
                ". Open the Lesson Menu to restart the lesson or return to Lessons.";
            SetFreeze(true);
            ::Mission::Engine::Demo::AcknowledgeTutorialRestore();
            LogOut("[TUTORIAL][FLOW] demo restore failed lesson=" +
                       g_s.lesson.lessonId + " reason=\"" +
                       LogExcerpt(restoreMessage) + "\"",
                   true);
        } else if (g_s.demoReturn == Phase::Complete) {
            EnterCompleteLocked();
            ::Mission::Engine::Demo::AcknowledgeTutorialRestore();
        } else if (g_s.demoReturn == Phase::Intro ||
                   g_s.demoReturn == Phase::Review ||
                   g_s.demoReturn == Phase::ConfirmExit) {
            g_s.phase = g_s.demoReturn;
            SetFreeze(true);
            ::Mission::Engine::Demo::AcknowledgeTutorialRestore();
        } else {
            int target = g_s.demoReturnTask;
            if (target < 0 || target >= static_cast<int>(g_s.tasks.size())) {
                target = FirstPendingTaskLocked();
            }
            if (target < 0) {
                EnterCompleteLocked();
                ::Mission::Engine::Demo::AcknowledgeTutorialRestore();
            } else {
                // Acknowledge (release demo neutral) BEFORE ArmTaskLocked, or
                // the release clobbers the new owner.
                ::Mission::Engine::Demo::AcknowledgeTutorialRestore();
                ArmTaskLocked(target);
            }
        }
        g_s.prevMove = s.p1Move; g_s.prevP2Move = s.p2Move;
        g_s.prevFrameIdx = s.p1FrameIdx; g_s.prevP2FrameIdx = s.p2FrameIdx;
        g_s.prevCombo = s.p1Combo; g_s.prevHitState = s.p1HitState;
        g_s.comboWasAlive = s.p2InStun || s.p1Combo > 0;
        return;
    }

    switch (g_s.phase) {
        case Phase::Intro:
        case Phase::Review: {
            if (g_s.phase == Phase::Review && g_s.reviewResumePending) {
                if (ShouldResumeReviewAfterRelease(
                        true, in.inputActive, in.anyHeld)) {
                    ResumeFromReviewLocked();
                }
                break;
            }
            const int pageCount = static_cast<int>(g_s.lesson.lesson.pages.size());
            if (in.confirm) {
                if (g_s.page + 1 < pageCount) {
                    ++g_s.page;
                } else if (g_s.phase == Phase::Review) {
                    g_s.reviewResumePending = true;
                } else if (g_s.lesson.lesson.completion == "pages") {
                    EnterCompleteLocked();
                } else {
                    ArmTaskLocked(FirstPendingTaskLocked() < 0
                                      ? 0 : FirstPendingTaskLocked());
                    if (g_s.introWorldAdvanced &&
                        g_s.phase != Phase::Error) {
                        g_s.introWorldAdvanced = false;
                        deferred.kind = DeferredKind::RestoreCheckpoint;
                        deferred.sourceLessonId = g_s.lesson.lessonId;
                    }
                }
            } else if (in.cancel) {
                if (g_s.page > 0) {
                    --g_s.page;
                } else if (g_s.phase == Phase::Intro) {
                    g_s.phase = Phase::ConfirmExit;
                    g_s.confirmSel = 1;
                } else {
                    g_s.reviewResumePending = true;
                }
            }
            break;
        }
        case Phase::ConfirmExit: {
            if (in.horiz != 0 || in.vert != 0) g_s.confirmSel = g_s.confirmSel == 0 ? 1 : 0;
            if (in.confirm) {
                if (g_s.confirmSel == 0) {
                    deferred.kind = DeferredKind::ReturnToBrowser;
                } else {
                    g_s.phase = Phase::Intro;
                }
            } else if (in.cancel) {
                g_s.phase = Phase::Intro;
            }
            break;
        }
        case Phase::NeutralGate: {
            if (!g_s.episodePlaying && CurrentEpisodeLocked()) {
                StartTaskEpisodeLocked();
                if (g_s.phase == Phase::Error) break;
            }
            {
                uint8_t observedPoll = 0;
                uint32_t observedSerial = 0;
                const bool observedPollValid =
                    TryGetLastObservedPhysicalPoll(
                        1, observedPoll, &observedSerial);
                const bool freshObservedPoll = observedPollValid &&
                    observedSerial != g_s.neutralObservedSerial;
                if (freshObservedPoll) {
                    g_s.neutralObservedSerial = observedSerial;
                }
                if (g_s.neutralInputLeaseHeld) {
                    g_pollOverrideMask[1].store(
                        NeutralGatePublishedMask(observedPoll),
                        std::memory_order_relaxed);
                    g_pollOverrideActive[1].store(true, std::memory_order_release);
                }
                g_s.neutralTicks = NeutralGateNeutralPollCount(
                    observedPollValid, freshObservedPoll,
                    observedPoll, g_s.neutralTicks);
                if (g_s.neutralTicks >= NeutralGateRequiredFreshPolls()) {
                    std::string setupError;
                    if (!ApplyTaskPositionLocked(CurrentTaskLocked(), setupError) ||
                        !ApplyTaskSeedsLocked(CurrentTaskLocked(), setupError)) {
                        FailTaskSetupLocked(setupError);
                        break;
                    }
                    SetNeutralInputLease(false);
                    g_s.phase = Phase::TaskActive;
                    BeginTaskMetricsLocked();
                }
            }
            break;
        }
        case Phase::TaskActive: {
            if (s.valid) TickCombat(s);
            if (g_s.phase == Phase::TaskActive && g_s.feedbackTicks > 0) {
                --g_s.feedbackTicks;
                if (g_s.feedbackTicks == 0) g_s.feedbackText.clear();
            }
            break;
        }
        case Phase::Choice: {
            const ::Mission::LessonTask* t = CurrentTaskLocked();
            if (!t) { EnterCompleteLocked(); break; }
            const int count = static_cast<int>(t->options.size());
            if (in.vert != 0 && count > 0) {
                g_s.choiceSel = (g_s.choiceSel + (in.vert > 0 ? 1 : -1) + count) % count;
            }
            if (in.confirm && count > 0) {
                const std::string& picked = t->options[g_s.choiceSel].id;
                const bool ok = std::find(t->acceptedOptionIds.begin(),
                                          t->acceptedOptionIds.end(), picked)
                                != t->acceptedOptionIds.end();
                auto fb = t->optionFeedback.find(picked);
                const std::string text = fb != t->optionFeedback.end() ? fb->second
                                       : ok ? t->successText : t->failureText;
                if (ok) {
                    g_s.tasks[g_s.task].done = true;
                    g_s.tasks[g_s.task].failed = false;
                    StartFeedbackLocked(true, text.empty() ? std::string("Correct.") : text);
                } else {
                    ++g_s.attemptsThisRun;
                    g_s.tasks[g_s.task].failed = true;
                    g_s.feedbackText = text.empty() ? std::string("Not quite.") : text;
                    g_s.feedbackSuccess = false;
                    g_s.feedbackTicks = 240;
                }
            }
            if (g_s.feedbackTicks > 0) --g_s.feedbackTicks;
            break;
        }
        case Phase::Feedback: {
            if (--g_s.feedbackTicks <= 0) {
                if (g_s.feedbackSuccess) {
                    if (AdvanceAfterTaskLocked()) {
                        const ::Mission::LessonTask* nt = CurrentTaskLocked();
                        const bool carryState = nt &&
                            IsCarryContinuity(nt->continuity);
                        if (!carryState) {
                            deferred.kind = DeferredKind::RestoreCheckpoint;
                            deferred.sourceLessonId = g_s.lesson.lessonId;
                        }
                    }
                } else {
                    const int failedTask = g_s.task;
                    const int retryTask = ContinuationRetryHeadLocked(failedTask);
                    ClearRetryChainLocked(retryTask, failedTask);
                    deferred.kind = DeferredKind::RestoreCheckpoint;
                    deferred.sourceLessonId = g_s.lesson.lessonId;
                    ArmTaskLocked(retryTask);
                }
            }
            break;
        }
        case Phase::Complete: {
            const bool haveNext = !NextNameLocked().empty();
            const int rows = haveNext ? 3 : 2;
            if (in.vert != 0) g_s.completeSel = (g_s.completeSel + (in.vert > 0 ? 1 : -1) + rows) % rows;
            if (in.confirm) {
                const int sel = haveNext ? g_s.completeSel : g_s.completeSel + 1;
                if (sel == 0) {
                    if (ResolveNextPathLocked(deferred.nextPath, deferredError)) {
                        deferred.kind = DeferredKind::LoadNext;
                        deferred.sourceLessonId = g_s.lesson.lessonId;
                        SetFreeze(false);
                    }
                } else if (sel == 1) {
                    deferred.kind = DeferredKind::RestoreCheckpoint;
                    deferred.sourceLessonId = g_s.lesson.lessonId;
                    for (auto& t : g_s.tasks) t.done = false;
                    for (auto& t : g_s.tasks) t.failed = false;
                    g_s.cleared = false;
                    g_s.clearRecorded = false;
                    g_s.attemptsThisRun = 0;
                    g_s.feedbackText.clear();
                    g_s.feedbackTicks = 0;
                    g_s.pendingSuccessRestore = false;
                    g_s.episodeVariantAssignments.clear();
                    g_s.taskMetrics.assign(g_s.tasks.size(), TaskMetrics{});
                    g_s.liveMetrics = TaskMetrics{};
                    g_s.uiLatchNeedsSync = true;
                    if (!g_s.lesson.lesson.pages.empty()) { g_s.page = 0; g_s.phase = Phase::Intro; }
                    else ArmTaskLocked(0);
                } else {
                    deferred.kind = DeferredKind::ReturnToBrowser;
                }
            }
            break;
        }
        default: break;
    }

    if (g_s.pendingSuccessRestore && deferred.kind == DeferredKind::None) {
        g_s.pendingSuccessRestore = false;
        deferred.kind = DeferredKind::RestoreCheckpoint;
        deferred.sourceLessonId = g_s.lesson.lessonId;
    }

    g_s.prevMove = s.p1Move;
    g_s.prevP2Move = s.p2Move;
    g_s.prevFrameIdx = s.p1FrameIdx;
    g_s.prevP2FrameIdx = s.p2FrameIdx;
    g_s.prevCombo = s.p1Combo;
    g_s.prevHitState = s.p1HitState;
    g_s.comboWasAlive = s.p2InStun || s.p1Combo > 0;
    SyncPageHudLocked();
    lk.unlock();

    if (!deferredError.empty()) {
        DirectDrawHook::AddMessage(deferredError.c_str(), "TUTORIAL",
                                   RGB(255, 180, 120), 2200, 20, 96);
    }
    if (deferred.kind == DeferredKind::RestoreCheckpoint) {
        RestoreCheckpointAfterUnlock(deferred.sourceLessonId);
    } else if (deferred.kind == DeferredKind::LoadNext) {
        std::string message;
        LoadNextAfterUnlock(deferred, message);
    } else if (deferred.kind == DeferredKind::ReturnToBrowser) {
        ReturnToBrowserAfterUnlock();
    }
}


bool CommandRetryTask() {
    std::string lessonId;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        if (!g_s.active) return false;
        if (g_s.phase != Phase::TaskActive && g_s.phase != Phase::NeutralGate &&
            g_s.phase != Phase::Feedback && g_s.phase != Phase::Choice) return false;
        ++g_s.attemptsThisRun;
        const int currentTask = g_s.task;
        const int retryTask = ContinuationRetryHeadLocked(currentTask);
        ClearRetryChainLocked(retryTask, currentTask);
        ArmTaskLocked(retryTask);
        lessonId = g_s.lesson.lessonId;
    }
    return RestoreCheckpointAfterUnlock(lessonId);
}

bool CommandRestartLesson() {
    if (!::Mission::Engine::Runner::HasBaseline()) return false;
    std::string lessonId;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        if (!g_s.active || !SessionMenuAllowed(g_s.phase)) return false;
        ++g_s.attemptsThisRun;
        for (auto& t : g_s.tasks) { t.done = false; t.failed = false; }
        g_s.cleared = false;
        g_s.clearRecorded = false;
        g_s.feedbackText.clear();
        g_s.episodeVariantAssignments.clear();
        g_s.page = 0;
        g_s.uiLatchNeedsSync = true;
        if (!g_s.lesson.lesson.pages.empty()) { g_s.phase = Phase::Intro; SetFreeze(true); }
        else ArmTaskLocked(0);
        lessonId = g_s.lesson.lessonId;
    }
    return RestoreCheckpointAfterUnlock(lessonId);
}

bool CommandReviewLesson() {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_s.active || g_s.lesson.lesson.pages.empty() ||
        g_s.phase == Phase::Preparing || g_s.phase == Phase::Intro ||
        g_s.phase == Phase::Review || g_s.phase == Phase::ConfirmExit ||
        g_s.phase == Phase::DemoSuspended || g_s.phase == Phase::Error) return false;
    g_s.reviewReturn = g_s.phase;
    g_s.reviewResumePending = false;
    g_s.reviewStateInvalidated = false;
    g_s.reviewResumeTask = -1;
    g_s.page = 0;
    g_s.phase = Phase::Review;
    SetFreeze(true);
    LogOut("[TUTORIAL][FLOW] review enter lesson=" + g_s.lesson.lessonId +
           " return=" + PhaseName(g_s.reviewReturn) +
           " task=" + std::to_string(g_s.task + 1), true);
    return true;
}

bool CommandPlayDemo(std::string& message) {
    bool releasedFreeze = false;
    bool releasedNeutral = false;
    DemoResumeTarget resume;
    DemoResumeTarget reviewResume;
    bool launchedFromReview = false;
    int clearChainThrough = -1;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        if (!g_s.active || g_s.phase == Phase::Preparing ||
            g_s.phase == Phase::DemoSuspended || g_s.phase == Phase::Error) {
            message = "no active lesson";
            return false;
        }
        launchedFromReview = g_s.phase == Phase::Review;
        if (launchedFromReview) {
            reviewResume = DecideDemoResumeTarget(
                g_s.reviewReturn, g_s.feedbackSuccess,
                CompletionMetLocked(), g_s.task, FirstPendingTaskLocked());
            resume = {Phase::Review, -1};
        } else {
            resume = DecideDemoResumeTarget(g_s.phase, g_s.feedbackSuccess,
                                            CompletionMetLocked(), g_s.task,
                                            FirstPendingTaskLocked());
        }

        DemoResumeTarget& gameplayResume = launchedFromReview ? reviewResume : resume;
        if (gameplayResume.phase == Phase::TaskActive &&
            gameplayResume.task >= 0) {
            clearChainThrough = gameplayResume.task;
            gameplayResume.task = ContinuationRetryHeadLocked(gameplayResume.task);
        }
        StopTaskEpisodeLocked();
        releasedFreeze = g_s.frozenLeaseHeld;
        releasedNeutral = g_s.neutralInputLeaseHeld;
        if (releasedFreeze) SetFreeze(false);
        SetNeutralInputLease(false);
    }

    if (::Mission::Engine::Demo::PlayLoaded(message, true)) {
        std::lock_guard<std::mutex> lk(g_mx);
        if (g_s.active) {
            if (clearChainThrough >= 0) {
                const int head = launchedFromReview
                    ? reviewResume.task : resume.task;
                ClearRetryChainLocked(head, clearChainThrough);
            }
            if (launchedFromReview) {
                g_s.reviewStateInvalidated = true;
                g_s.reviewResumeTask = reviewResume.task;
                g_s.reviewReturn = reviewResume.phase;
            }
            g_s.demoReturn = resume.phase;
            g_s.demoReturnTask = resume.task;
            g_s.phase = Phase::DemoSuspended;
            g_s.feedbackText.clear();
            g_s.uiLatchNeedsSync = true;
            SyncPageHudLocked();
            LogOut("[TUTORIAL][FLOW] demo queued lesson=" + g_s.lesson.lessonId +
                   " return=" + PhaseName(g_s.demoReturn) +
                   " task=" + std::to_string(g_s.demoReturnTask + 1) +
                   " review=" + (launchedFromReview ? std::string("1")
                                                    : std::string("0")) +
                   " rewindThrough=" + std::to_string(clearChainThrough + 1),
                   true);
        }
        return true;
    }

    {
        std::lock_guard<std::mutex> lk(g_mx);
        if (g_s.active && releasedFreeze && FrozenPhase(g_s.phase)) SetFreeze(true);
        if (g_s.active && releasedNeutral) SetNeutralInputLease(true);
        if (g_s.active && resume.phase == Phase::TaskActive &&
            g_s.phase == Phase::TaskActive) StartTaskEpisodeLocked();
        if (g_s.active && launchedFromReview &&
            g_s.phase == Phase::Review &&
            g_s.reviewReturn == Phase::TaskActive) {
            StartTaskEpisodeLocked();
        }
    }
    return false;
}

bool CommandNextLesson(std::string& message) {
    DeferredRequest request;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        if (!g_s.active || !SessionMenuAllowed(g_s.phase) || !g_s.cleared) {
            message = "lesson is not cleared";
            return false;
        }
        if (!ResolveNextPathLocked(request.nextPath, message)) return false;
        request.kind = DeferredKind::LoadNext;
        request.sourceLessonId = g_s.lesson.lessonId;
        SetFreeze(false);
    }
    return LoadNextAfterUnlock(request, message);
}

bool WantsDraw() {
    return IsActive() && !::Mission::Engine::Demo::IsActive();
}

void Draw(void* device, ImDrawList* dl) {
    if (!device || !dl || !WantsDraw()) return;
    // EndScene must not block on the session mutex; reuse the last snapshot.
    UiSnapshot* renderSnapshot = AcquireUiSnapshot();
    if (!renderSnapshot) return;
    UiSnapshot& s = *renderSnapshot;
    {
        std::unique_lock<std::mutex> lk(g_mx, std::try_to_lock);
        if (!lk.owns_lock()) {
            if (!s.active ||
                s.generation != g_uiGeneration.load(std::memory_order_acquire)) {
                return;
            }
        } else {
            if (!g_s.active || g_s.phase == Phase::Preparing ||
                g_s.phase == Phase::DemoSuspended || !g_s.uiModel) {
                s = UiSnapshot{};
                return;
            }
            s.active = true;
            s.generation = g_uiGeneration.load(std::memory_order_relaxed);
            s.phase = g_s.phase;
            if (s.model != g_s.uiModel) s.model = g_s.uiModel;
            s.page = g_s.page;
            s.task = g_s.task;
            s.reviewResumePending = g_s.reviewResumePending;
            const int stateCount = static_cast<int>(g_s.tasks.size());
            if (s.taskStates.size() != g_s.tasks.size()) {
                s.taskStates.resize(g_s.tasks.size());
            }
            for (int i = 0; i < stateCount; ++i) {
                s.taskStates[static_cast<size_t>(i)] =
                    (g_s.tasks[i].done ? uint8_t{1} : uint8_t{0}) |
                    (g_s.tasks[i].failed ? uint8_t{2} : uint8_t{0});
            }
            s.choiceSel = g_s.choiceSel;
            s.completeSel = g_s.completeSel;
            s.confirmSel = g_s.confirmSel;
            if (!s.feedbackText || s.feedbackSource != g_s.feedbackText) {
                s.feedbackSource = g_s.feedbackText;
                s.feedbackText =
                    std::make_shared<const std::string>(
                        TextPolicy::NormalizeUiText(g_s.feedbackText));
            }
            s.feedbackSuccess = g_s.feedbackSuccess;
            s.feedbackTicks = g_s.feedbackTicks;
            s.episodeCueActive = g_s.episodeCueDisplayTicks > 0;
            s.sequenceIndex = g_s.sequenceIndex;
            s.sequenceArmed = g_s.armedCommitted;
            s.freezeConfirmed = g_s.freezeConfirmed;
            s.juggleOutlineValid =
                g_s.jugglePreview.held &&
                !g_s.jugglePreview.refreshActive &&
                g_s.jugglePreview.outlineValid;
            s.juggleOutline = g_s.jugglePreview.outline;
        }
        if (!s.active || !s.model) {
            return;
        }
    }

    const UiModel& model = *s.model;
    static const std::string kEmptyUiText;
    const std::string& feedbackText = s.feedbackText
        ? *s.feedbackText : kEmptyUiText;
    const int pageCount = static_cast<int>(model.pages.size());
    const int taskCount = static_cast<int>(model.tasks.size());
    const UiTaskModel* currentTask =
        s.task >= 0 && s.task < taskCount ? &model.tasks[s.task] : nullptr;

    CustomMenu::Scale::Update(Config::GetSettings().uiScale);
    const CustomMenu::Scale::Metrics& metrics = CustomMenu::Scale::Get();
    const float ui = metrics.uiScale;
    ImFont* readable = CustomMenu::Fonts::TutorialReadable();
    if (!readable) readable = ImGui::GetFont();
    ImFont* display = CustomMenu::Fonts::TutorialHeader();
    if (!display) display = L::HeaderFont();
    if (!display) display = readable;
    ImFont* meta = L::BodyFont();
    if (!meta) meta = readable;

    const HudPolicy::Typography typography = HudPolicy::TypographyFor(ui);
    const float prosePx = typography.prosePx;
    const float taskPx = typography.taskPx;
    const float metaPx = typography.metaPx;
    const float controlsPx = typography.controlsPx;
    const float bannerPx = typography.bannerPx;
    const float pageTitlePx = typography.pageTitlePx;

    auto richHeight = [&](ImFont* font, float px, const std::string& text,
                          float width) {
        return ::Mission::Render::MeasureRichTextHeight(
            device, font, px, text, width);
    };
    auto richDraw = [&](ImFont* font, float px, float x, float y, ImU32 color,
                        const std::string& text, float width) {
        ::Mission::Render::DrawRichText(
            device, dl, font, px, x, y, color, text, width);
    };
    auto drawControlGroups = [&] (
        const HudPolicy::Rect& region,
        const std::array<std::string, 3>& groups,
        ImU32 color) {
        constexpr float boundaries[4] = {0.0f, 0.31f, 0.70f, 1.0f};
        for (int i = 0; i < 3; ++i) {
            if (groups[static_cast<size_t>(i)].empty()) continue;
            const float slotX = region.x +
                region.w * boundaries[i];
            const float slotW = region.w *
                (boundaries[i + 1] - boundaries[i]);
            const float textW = (std::max)(1.0f, slotW - 8.0f);
            const ::Mission::Render::RichTextMetrics measured =
                ::Mission::Render::MeasureRichText(
                    device, readable, controlsPx,
                    groups[static_cast<size_t>(i)], textW);
            const float drawX = slotX +
                (std::max)(4.0f, (slotW - measured.width) * 0.5f);
            const float drawY = region.y +
                (region.h - measured.height) * 0.5f;
            richDraw(readable, controlsPx, drawX, drawY, color,
                     groups[static_cast<size_t>(i)], textW);
        }
    };
    auto toneColor = [](TutorialColorPolicy::Tone tone,
                        std::uint8_t alpha = 255) {
        const TutorialColorPolicy::Rgba color =
            TutorialColorPolicy::Color(tone);
        return IM_COL32(color.r, color.g, color.b, alpha);
    };
    auto drawPanel = [&](float x, float y, float w, float h, ImU32 accent,
                         ImU32 top = T::kBoxFill,
                         ImU32 bottom = T::kBoxFill) {
        if (top == bottom) {
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), top);
        } else {
            dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, y + h),
                                        top, top, bottom, bottom);
        }
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 3.0f, y + h), accent);
        dl->AddRect(ImVec2(x + 0.5f, y + 0.5f),
                    ImVec2(x + w - 0.5f, y + h - 0.5f),
                    T::kBoxBorder, 0.0f, 0, 1.0f);
    };
    auto drawBanner = [&](const char* badge, const char* status,
                          const HudPolicy::Rect& banner) {
        const float x = banner.x;
        const float y = banner.y;
        const float w = banner.w;
        const float h = banner.h;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), T::kBandFill);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 3.0f, y + h),
                          T::kInfoAccent);
        dl->AddLine(ImVec2(x, y + h - 1.0f), ImVec2(x + w, y + h - 1.0f),
                    T::kBoxBorder, 1.0f);

        const float badgeTextW = badge && *badge
            ? L::MeasureTextW(meta, metaPx, badge) : 0.0f;
        const float badgeW = badgeTextW > 0.0f ? badgeTextW + 22.0f : 0.0f;
        if (badgeW > 0.0f) {
            const float bx = x + 12.0f;
            L::DrawOutlinedText(dl, meta, metaPx, bx,
                                y + (h - metaPx) * 0.5f,
                                T::kInfoAccent, badge);
            dl->AddLine(ImVec2(bx + badgeW - 8.0f, y + 7.0f),
                        ImVec2(bx + badgeW - 8.0f, y + h - 7.0f),
                        T::kRuleDim, 1.0f);
        }

        const float statusW = status && *status
            ? L::MeasureTextW(meta, metaPx, status) : 0.0f;
        if (statusW > 0.0f) {
            L::DrawOutlinedText(dl, meta, metaPx, x + w - 14.0f - statusW,
                                y + (h - metaPx) * 0.5f,
                                T::kTextStatus, status);
        }
        const float titleX = x + 14.0f + badgeW;
        const float titleRight = x + w - 14.0f - statusW -
                                 (statusW > 0.0f ? 10.0f : 0.0f);
        const float titleDrawX = titleX + 2.0f;
        const float titleW =
            (std::max)(40.0f, titleRight - titleDrawX - 2.0f);
        float fitted = bannerPx;
        const float natural = display->CalcTextSizeA(
            fitted, FLT_MAX, 0.0f, model.lessonName.c_str()).x;
        if (natural > titleW) {
            fitted = (std::max)(13.0f, fitted * titleW / natural);
        }
        dl->PushClipRect(ImVec2(titleX, y + 2.0f),
                         ImVec2(titleRight, y + h - 2.0f), true);
        L::DrawOutlinedText(dl, display, fitted, titleDrawX,
                            y + (h - fitted) * 0.5f,
                            T::kTextActive, model.lessonName.c_str());
        dl->PopClipRect();
    };
    auto drawModalHeader = [&](const char* title, const char* status) {
        L::DrawTitleBand(dl, title, status, 0.0f);
        constexpr HudPolicy::Rect metaBand = HudPolicy::ModalMetaBand();
        dl->AddRectFilled(ImVec2(metaBand.x, metaBand.y),
                          ImVec2(metaBand.x + metaBand.w,
                                 metaBand.y + metaBand.h),
                          T::kStripStrong);
        dl->AddLine(ImVec2(metaBand.x, metaBand.y + metaBand.h - 1.0f),
                    ImVec2(metaBand.x + metaBand.w,
                           metaBand.y + metaBand.h - 1.0f),
                    T::kRuleDim, 1.0f);
        const float badgeX = 16.0f;
        L::DrawOutlinedText(dl, meta, metaPx, badgeX,
                            metaBand.y + (metaBand.h - metaPx) * 0.5f,
                            T::kInfoAccent,
                            model.difficultyLabel.c_str());
        const float badgeW = L::MeasureTextW(
            meta, metaPx, model.difficultyLabel.c_str());
        const float dividerX = badgeX + badgeW + 13.0f;
        dl->AddLine(ImVec2(dividerX, metaBand.y + 6.0f),
                    ImVec2(dividerX, metaBand.y + metaBand.h - 6.0f),
                    T::kRuleDim, 1.0f);
        L::DrawOutlinedText(dl, meta, metaPx, dividerX + 13.0f,
                            metaBand.y + (metaBand.h - metaPx) * 0.5f,
                            T::kTextActive, model.lessonName.c_str());
    };
    auto drawModalFooter = [&] (
        const std::string& hint,
        const std::array<std::string, 3>& controls) {
        const HudPolicy::Rect footer = HudPolicy::ModalFooter(ui);
        L::DrawInfoBox(dl, footer.x, footer.y, footer.w, footer.h);
        const float textX = footer.x + 12.0f;
        const float textW = footer.w - 24.0f;
        float controlsH = controlsPx;
        for (const std::string& group : controls) {
            if (group.empty()) continue;
            controlsH = (std::max)(controlsH,
                richHeight(readable, controlsPx, group, textW * 0.38f));
        }
        constexpr float gap = 4.0f;
        const float maxHintH = (std::max)(
            taskPx, footer.h - 14.0f - gap - controlsH);
        float hintPx = prosePx;
        float hintH = richHeight(readable, hintPx, hint, textW);
        if (hintH > maxHintH && hintH > 0.0f) {
            hintPx = (std::max)(taskPx,
                hintPx * maxHintH / hintH);
            hintH = richHeight(readable, hintPx, hint, textW);
        }
        const float total = hintH + gap + controlsH;
        const float textY = footer.y +
            (std::max)(7.0f, (footer.h - total) * 0.5f);
        dl->PushClipRect(ImVec2(textX, footer.y + 5.0f),
                         ImVec2(textX + textW,
                                footer.y + footer.h - 5.0f), true);
        richDraw(readable, hintPx, textX, textY,
                 T::kTextActive, hint, textW);
        drawControlGroups(
            {textX, textY + hintH + gap, textW, controlsH},
            controls, T::kTextActive);
        dl->PopClipRect();
    };
    auto drawSessionRichRow = [&](float x, float y, float w, float h,
                                  const std::string& label, bool selected) {
        L::DrawSessionRowChrome(dl, x, y, w, h, selected);
        const float textW = w - 36.0f;
        const float th = richHeight(readable, taskPx, label, textW);
        dl->PushClipRect(ImVec2(x + 12.0f, y + 1.0f),
                         ImVec2(x + w - 12.0f, y + h - 1.0f), true);
        richDraw(readable, taskPx, x + 18.0f,
                 y + (h - th) * 0.5f - 1.0f,
                 selected ? T::kTextActive : T::kTextInactive,
                 label, textW);
        dl->PopClipRect();
    };
    auto drawSelectRow = [&](float x, float y, float w, float h,
                             const std::string& label, bool selected) {
        L::DrawNativeBar(dl, x, y, w, h, selected, false);
        if (selected) {
            dl->AddTriangleFilled(ImVec2(x + 8.0f, y + h * 0.5f - 4.0f),
                                  ImVec2(x + 8.0f, y + h * 0.5f + 4.0f),
                                  ImVec2(x + 14.0f, y + h * 0.5f),
                                  T::kBarTextSel);
        }
        const float th = richHeight(readable, taskPx, label, w - 38.0f);
        richDraw(readable, taskPx, x + 22.0f,
                 y + (h - th) * 0.5f - 1.0f,
                 selected ? T::kBarTextSel : T::kBarText,
                 label, w - 38.0f);
    };

    if (s.phase == Phase::Intro || s.phase == Phase::Review ||
        s.phase == Phase::ConfirmExit) {
        const UiPageModel* pageModel = pageCount > 0
            ? &model.pages[static_cast<size_t>((std::max)(
                  0, (std::min)(s.page, pageCount - 1)))]
            : nullptr;
        const HudPolicy::HudFocus hudFocus = pageModel
            ? pageModel->hudFocus : HudPolicy::HudFocus::None;
        const HudPolicy::Rect dim = HudPolicy::PageBackdropDim(hudFocus);
        if (dim.w > 0.0f && dim.h > 0.0f) {
            dl->AddRectFilled(ImVec2(dim.x, dim.y),
                              ImVec2(dim.x + dim.w, dim.y + dim.h),
                              IM_COL32(0, 0, 0, 72));
        }

        for (int i = 0; i < HudPolicy::HudFocusRectCount(hudFocus); ++i) {
            const HudPolicy::Rect focusRect =
                HudPolicy::HudFocusRect(hudFocus, i);
            const TutorialColorPolicy::Tone focusTone =
                HudPolicy::HudFocusTone(hudFocus, i);
            const TutorialColorPolicy::Rgba semantic =
                TutorialColorPolicy::Color(focusTone);
            const ImU32 middle = toneColor(focusTone, 245);
            const ImU32 inner = IM_COL32(
                (std::min)(255, static_cast<int>(semantic.r) + 70),
                (std::min)(255, static_cast<int>(semantic.g) + 70),
                (std::min)(255, static_cast<int>(semantic.b) + 70), 235);
            const ImVec2 lo(focusRect.x + 0.5f, focusRect.y + 0.5f);
            const ImVec2 hi(focusRect.x + focusRect.w - 0.5f,
                            focusRect.y + focusRect.h - 0.5f);
            dl->AddRect(lo, hi, IM_COL32(0, 0, 0, 225), 0.0f, 0, 4.0f);
            dl->AddRect(ImVec2(lo.x + 1.0f, lo.y + 1.0f),
                        ImVec2(hi.x - 1.0f, hi.y - 1.0f),
                        middle, 0.0f, 0, 2.0f);
            dl->AddRect(ImVec2(lo.x + 2.0f, lo.y + 2.0f),
                        ImVec2(hi.x - 2.0f, hi.y - 2.0f),
                        inner, 0.0f, 0, 1.0f);
        }

        char pageStatus[32] = {};
        _snprintf_s(pageStatus, sizeof(pageStatus), _TRUNCATE, "PAGE %d / %d",
                    pageCount > 0 ? s.page + 1 : 0, pageCount);
        drawBanner(s.phase == Phase::Review ? "REVIEW" :
                   model.difficultyLabel.c_str(),
                   s.freezeConfirmed ? "PAUSED" : "PAUSING...",
                   HudPolicy::PageBannerFor(hudFocus));

        if (pageCount <= 0) return;
        const UiPageModel& page = *pageModel;
        const HudPolicy::Rect pageCard = HudPolicy::PageCardFor(hudFocus);
        const float x = pageCard.x;
        const float y = pageCard.y;
        const float w = pageCard.w;
        const float h = pageCard.h;
        drawPanel(x, y, w, h, T::kInfoAccent);
        const std::string& title = page.title.empty() ? model.lessonName : page.title;
        const float pageStatusW = L::MeasureTextW(meta, metaPx, pageStatus);
        L::DrawString(dl, meta, metaPx, x + w - 18.0f - pageStatusW,
                      y + 18.0f, IM_COL32(145, 205, 215, 255),
                      pageStatus);
        const float titleX = x + 18.0f;
        const float titleRight = x + w - 28.0f - pageStatusW;
        const float titleAvailable = (std::max)(64.0f, titleRight - titleX);
        float fittedTitlePx = pageTitlePx;
        const float titleNatural = display->CalcTextSizeA(
            fittedTitlePx, FLT_MAX, 0.0f, title.c_str()).x;
        if (titleNatural > titleAvailable) {
            fittedTitlePx = (std::max)(13.0f,
                fittedTitlePx * titleAvailable / titleNatural);
        }
        dl->PushClipRect(ImVec2(titleX - 1.0f, y + 5.0f),
                         ImVec2(titleRight, y + 39.0f), true);
        L::DrawOutlinedText(dl, display, fittedTitlePx, titleX,
                            y + 14.0f + (pageTitlePx - fittedTitlePx) * 0.5f,
                            T::kTextActive, title.c_str());
        dl->PopClipRect();
        const float ruleY = y + 43.0f;
        dl->AddLine(ImVec2(x + 18.0f, ruleY), ImVec2(x + w - 18.0f, ruleY),
                    IM_COL32(105, 225, 230, 120));

        const float textX = x + 18.0f;
        const float textY = ruleY + 13.0f;
        const float textW = w - 36.0f;
        const float availableH = y + h - 16.0f - textY;
        float fitted = prosePx;
        float measured = richHeight(readable, fitted, page.text, textW);
        if (measured > availableH && measured > 0.0f) {
            fitted = (std::max)(12.0f, fitted * availableH / measured);
            measured = richHeight(readable, fitted, page.text, textW);
        }
        dl->PushClipRect(ImVec2(textX, textY),
                         ImVec2(textX + textW, textY + availableH), true);
        richDraw(readable, fitted, textX, textY, T::kTextActive,
                 page.text, textW);
        dl->PopClipRect();

        const HudPolicy::Rect pageActions =
            HudPolicy::PageActionsFor(hudFocus);
        auto drawPageControlGroups = [&] (
            const std::array<std::string, 3>& controls) {
            drawPanel(pageActions.x, pageActions.y,
                      pageActions.w, pageActions.h, T::kInfoAccent);
            drawControlGroups(
                {pageActions.x + 10.0f, pageActions.y,
                 pageActions.w - 20.0f, pageActions.h},
                controls, T::kTextActive);
        };
        auto drawCentredPageControl = [&](const std::string& control) {
            drawPanel(pageActions.x, pageActions.y,
                      pageActions.w, pageActions.h, T::kInfoAccent);
            const float footerW = pageActions.w - 28.0f;
            const ::Mission::Render::RichTextMetrics measured =
                ::Mission::Render::MeasureRichText(
                    device, readable, controlsPx, control, footerW);
            const float footerX = pageActions.x + 14.0f +
                (std::max)(0.0f,
                    (footerW - measured.width) * 0.5f);
            richDraw(readable, controlsPx, footerX,
                     pageActions.y +
                         (pageActions.h - measured.height) * 0.5f - 1.0f,
                     T::kTextActive, control, footerW);
        };
        if (s.phase != Phase::ConfirmExit) {
            if (s.phase == Phase::Review && s.reviewResumePending) {
                drawCentredPageControl(
                    "RELEASE ALL CONTROLS TO RETURN TO THE TASK");
            } else {
                drawPageControlGroups(
                    s.phase == Phase::Review
                        ? page.reviewActions : page.introActions);
            }
        }

        if (HudPolicy::IsJuggleFocus(hudFocus)) {
            const HudPolicy::Rect rail = HudPolicy::JugglePreviewRail();
            const TutorialColorPolicy::Tone previewTone =
                HudPolicy::JugglePreviewTone(hudFocus);
            const ImU32 previewColor = toneColor(previewTone, 235);
            dl->AddRect(ImVec2(rail.x + 0.5f, rail.y + 0.5f),
                        ImVec2(rail.x + rail.w - 0.5f,
                               rail.y + rail.h - 0.5f),
                        previewColor, 0.0f, 0, 2.0f);
            dl->AddRectFilled(ImVec2(rail.x, rail.y),
                              ImVec2(rail.x + rail.w, rail.y + 24.0f),
                              T::kStripStrong);
            const char* previewLabel =
                hudFocus == HudPolicy::HudFocus::Juggle
                    ? "JUGGLE TIME"
                    : hudFocus == HudPolicy::HudFocus::JuggleYellow
                        ? "20 FRAMES OR LESS"
                        : "10 FRAMES OR LESS";
            L::DrawOutlinedText(dl, meta, metaPx, rail.x + 9.0f,
                                rail.y + (24.0f - metaPx) * 0.5f,
                                previewColor, previewLabel);
            if (s.juggleOutlineValid) {
                const HudPolicy::Rect outline = s.juggleOutline;
                const ImVec2 lo(outline.x, outline.y);
                const ImVec2 hi(outline.x + outline.w,
                                outline.y + outline.h);
                dl->AddRect(lo, hi, IM_COL32(0, 0, 0, 235),
                            0.0f, 0, 4.0f);
                dl->AddRect(ImVec2(lo.x + 1.0f, lo.y + 1.0f),
                            ImVec2(hi.x - 1.0f, hi.y - 1.0f),
                            previewColor, 0.0f, 0, 2.0f);
            }
        }

        if (s.phase == Phase::ConfirmExit) {
            dl->AddRectFilled(ImVec2(0, 0), ImVec2(T::kCanvasW, T::kCanvasH),
                              IM_COL32(0, 0, 0, 95));
            constexpr float cx = 172.0f;
            constexpr float cy = 177.0f;
            constexpr float cw = 296.0f;
            constexpr float ch = 126.0f;
            drawPanel(cx, cy, cw, ch, IM_COL32(255, 190, 120, 230));
            const float qw = L::MeasureTextW(display, pageTitlePx,
                                             "RETURN TO LESSONS?");
            L::DrawOutlinedText(dl, display, pageTitlePx,
                                cx + (cw - qw) * 0.5f, cy + 13.0f,
                                T::kTextActive,
                                "RETURN TO LESSONS?");
            const std::string options[2] = {"LEAVE LESSON", "KEEP READING"};
            float oy = cy + 48.0f;
            for (int i = 0; i < 2; ++i) {
                drawSelectRow(cx + 18.0f, oy, cw - 36.0f, 30.0f,
                              options[i], s.confirmSel == i);
                oy += 34.0f;
            }
            drawPageControlGroups(model.confirmActions);
        }
        return;
    }

    if (s.phase == Phase::Choice) {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(T::kCanvasW, T::kCanvasH),
                          IM_COL32(0, 0, 0, 118));
        if (!currentTask) return;
        char status[32] = {};
        _snprintf_s(status, sizeof(status), _TRUNCATE, "TASK %d / %d",
                    taskCount > 0 ? s.task + 1 : 0, taskCount);
        drawModalHeader("KNOWLEDGE CHECK", status);
        const HudPolicy::Rect content = HudPolicy::ModalContent(ui);
        const float x = content.x;
        const float y0 = content.y;
        const float w = content.w;
        const std::string& prompt = currentTask->prompt.empty()
            ? currentTask->label : currentTask->prompt;
        const float promptTextH =
            richHeight(readable, prosePx, prompt, w - 32.0f);
        const float promptH = (std::max)(68.0f, promptTextH + 38.0f);
        const bool showFeedback = !feedbackText.empty() && s.feedbackTicks > 0;
        const float feedbackH = showFeedback
            ? richHeight(readable, taskPx, feedbackText, w - 32.0f) + 20.0f
            : 0.0f;
        L::DrawInfoBox(dl, x, y0, w, promptH);
        L::DrawString(dl, meta, metaPx, x + 16.0f, y0 + 10.0f,
                      T::kInfoAccent, "CHOOSE THE BEST ANSWER");
        richDraw(readable, prosePx, x + 16.0f, y0 + 29.0f,
                 T::kTextActive, prompt, w - 32.0f);

        float y = y0 + promptH + 8.0f;
        const int optionCount =
            static_cast<int>(currentTask->options.size());
        const float reservedFeedback = showFeedback ? feedbackH + 8.0f : 0.0f;
        const float availableRows =
            (std::max)(0.0f, content.y + content.h - y - reservedFeedback);
        const float rowBudget = optionCount > 0
            ? availableRows / static_cast<float>(optionCount) : 0.0f;
        for (int i = 0; i < static_cast<int>(currentTask->options.size()); ++i) {
            const std::string& option = currentTask->options[i];
            const float desired = (std::max)(30.0f,
                richHeight(readable, taskPx, option, w - 36.0f) + 10.0f);
            const float rowH = (std::max)(26.0f,
                (std::min)(desired, rowBudget));
            drawSessionRichRow(x, y, w, rowH,
                               option, i == s.choiceSel);
            y += rowH;
        }
        if (showFeedback) {
            y += 8.0f;
            L::DrawInfoBox(dl, x, y, w, feedbackH);
            dl->AddRectFilled(
                ImVec2(x, y), ImVec2(x + 3.0f, y + feedbackH),
                s.feedbackSuccess ? IM_COL32(105, 235, 190, 230)
                                  : IM_COL32(255, 105, 105, 230));
            richDraw(readable, taskPx, x + 16.0f, y + 10.0f,
                     s.feedbackSuccess ? T::kTextActive : IM_COL32(255, 195, 165, 255),
                     feedbackText, w - 32.0f);
        }
        drawModalFooter(model.choiceHint, model.choiceActions);
        return;
    }

    if (s.phase == Phase::Complete) {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(T::kCanvasW, T::kCanvasH),
                          IM_COL32(0, 0, 0, 118));
        drawModalHeader("LESSON COMPLETE", "CLEARED");
        const HudPolicy::Rect content = HudPolicy::ModalContent(ui);
        const float x = content.x;
        const float y0 = content.y;
        const float w = content.w;
        static const std::string kDefaultSummary =
            "You completed every task in this lesson.";
        const std::string& summary = model.summary.empty()
            ? kDefaultSummary : model.summary;
        const float summaryTextH =
            richHeight(readable, prosePx, summary, w - 32.0f);
        const float summaryH = (std::max)(66.0f, summaryTextH + 38.0f);
        L::DrawInfoBox(dl, x, y0, w, summaryH);
        L::DrawString(dl, meta, metaPx, x + 16.0f, y0 + 10.0f,
                      IM_COL32(155, 235, 205, 255), "LESSON CLEARED");
        richDraw(readable, prosePx, x + 16.0f, y0 + 29.0f,
                 T::kTextActive, summary, w - 32.0f);

        float y = y0 + summaryH + 8.0f;
        const int optionCount =
            static_cast<int>(model.completeOptions.size());
        const float availableRows =
            (std::max)(0.0f, content.y + content.h - y);
        const float rowBudget = optionCount > 0
            ? availableRows / static_cast<float>(optionCount) : 0.0f;
        for (int i = 0; i < static_cast<int>(model.completeOptions.size()); ++i) {
            const float desired = (std::max)(30.0f,
                richHeight(readable, taskPx, model.completeOptions[i],
                           w - 36.0f) + 10.0f);
            const float rowH = (std::max)(26.0f,
                (std::min)(desired, rowBudget));
            drawSessionRichRow(x, y, w, rowH,
                               model.completeOptions[i], i == s.completeSel);
            y += rowH;
        }
        static const std::string kCompleteHint =
            "Choose what you want to do next.";
        const std::string& selectedHint =
            s.completeSel >= 0 &&
            s.completeSel < static_cast<int>(model.completeHints.size())
                ? model.completeHints[static_cast<size_t>(s.completeSel)]
                : kCompleteHint;
        drawModalFooter(selectedHint, model.completeActions);
        return;
    }

    if (s.phase == Phase::Error) {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(T::kCanvasW, T::kCanvasH),
                          IM_COL32(0, 0, 0, 150));
        drawModalHeader("LESSON NEEDS ATTENTION", "PAUSED");
        const HudPolicy::Rect content = HudPolicy::ModalContent(ui);
        const float x = content.x;
        const float y = content.y + 38.0f;
        const float w = content.w;
        float errorPx = prosePx;
        float textH =
            richHeight(readable, errorPx, feedbackText, w - 36.0f);
        static const std::string kErrorHelp =
            "Open the Lesson Menu for the recovery options available right now.";
        const float maxH = content.y + content.h - y;
        const float h = (std::min)(maxH,
            (std::max)(116.0f, textH + 65.0f));
        const float availableErrorH = h - 55.0f;
        if (textH > availableErrorH && availableErrorH > 0.0f) {
            errorPx = (std::max)(12.0f,
                errorPx * availableErrorH / textH);
            textH = richHeight(
                readable, errorPx, feedbackText, w - 36.0f);
        }
        drawPanel(x, y, w, h, IM_COL32(255, 105, 105, 230),
                  IM_COL32(48, 15, 20, 236), IM_COL32(18, 6, 10, 232));
        L::DrawString(dl, meta, metaPx, x + 18.0f, y + 12.0f,
                      IM_COL32(255, 175, 160, 255), "THIS LESSON COULD NOT CONTINUE");
        dl->PushClipRect(ImVec2(x + 18.0f, y + 36.0f),
                         ImVec2(x + w - 18.0f,
                                y + h - 16.0f), true);
        richDraw(readable, errorPx, x + 18.0f, y + 36.0f,
                 IM_COL32(255, 215, 195, 255),
                 feedbackText, w - 36.0f);
        dl->PopClipRect();
        drawModalFooter(kErrorHelp, model.errorActions);
        return;
    }

    if (!currentTask) return;

    const HudPolicy::Rect bounds =
        HudPolicy::RequirementBounds(model.requirementsBelowStats);
    constexpr float kCapW = 28.0f;
    constexpr float kCapOverlap = 2.0f;
    constexpr float kTextPadX = 8.0f;
    constexpr float kRowPadY = 4.0f;
    constexpr float kRowGap = 3.0f;
    constexpr float kMinMainW = 44.0f;
    constexpr float kRailMarkerW = 13.0f;
    constexpr float kRailTopGap = 5.0f;
    constexpr float kRailLineGap = 3.0f;
    constexpr float kRailItemGap = 5.0f;
    constexpr int kMaxVisibleRailActions = 8;
    const float maxTextW = bounds.w - kCapW + kCapOverlap - kTextPadX * 2.0f;

    auto requirementText = [&](int index) -> const std::string& {
        if (index == s.task && !feedbackText.empty() && s.feedbackTicks > 0 &&
            (s.phase == Phase::Feedback ||
             (s.phase == Phase::TaskActive && !s.feedbackSuccess))) {
            return feedbackText;
        }
        if (s.phase == Phase::NeutralGate && index == s.task) {
            return model.tasks[index].neutralLabel;
        }
        if (index == s.task && s.episodeCueActive &&
            !model.tasks[index].cueText.empty()) {
            return model.tasks[index].cueText;
        }
        return model.tasks[index].label;
    };
    auto requirementMetrics = [&](int index) {
        return ::Mission::Render::MeasureRichText(
            device, readable, taskPx, requirementText(index), maxTextW);
    };
    struct ActionRailMetrics {
        float width = 0.0f;
        float height = 0.0f;
    };
    auto actionRail = [&](int index, bool draw, float originX, float originY,
                          bool taskFailed, bool taskDone) {
        ActionRailMetrics result;
        if (index != s.task || index < 0 || index >= taskCount) return result;
        const std::vector<std::string>& actions = model.tasks[index].actions;
        if (actions.size() < 2) return result;

        const int totalActionCount = static_cast<int>(actions.size());
        const int visibleActionCount = (std::min)(
            totalActionCount, kMaxVisibleRailActions);
        const int currentAction = (std::max)(0, (std::min)(
            s.sequenceIndex, totalActionCount - 1));
        const int firstVisibleAction = HudPolicy::ActionWindowStart(
            totalActionCount, visibleActionCount, currentAction);
        const int endVisibleAction = firstVisibleAction + visibleActionCount;
        const bool clipped = visibleActionCount < totalActionCount;
        const float separatorW = L::MeasureTextW(meta, metaPx, ">");
        const float itemTextW = (std::max)(24.0f, maxTextW - kRailMarkerW);
        int first = firstVisibleAction;
        float lineY = originY;
        while (first < endVisibleAction) {
            int end = first;
            float lineW = 0.0f;
            float lineH = (std::max)(16.0f, metaPx + 3.0f);
            while (end < endVisibleAction) {
                const ::Mission::Render::RichTextMetrics actionMetrics =
                    ::Mission::Render::MeasureRichText(
                        device, readable, taskPx, actions[end], itemTextW);
                const float itemW = kRailMarkerW +
                    (std::min)(itemTextW, actionMetrics.width);
                const float prefixW = end == first ? 0.0f
                    : kRailItemGap + separatorW + kRailItemGap;
                if (end > first && lineW + prefixW + itemW > maxTextW) break;
                lineW += prefixW + itemW;
                lineH = (std::max)(lineH, actionMetrics.height + 3.0f);
                ++end;
            }
            if (end == first) ++end;

            result.width = (std::max)(result.width, lineW);
            if (result.height > 0.0f) result.height += kRailLineGap;
            result.height += lineH;

            if (draw) {
                float cursorX = originX;
                for (int actionIndex = first; actionIndex < end; ++actionIndex) {
                    const ::Mission::Render::RichTextMetrics actionMetrics =
                        ::Mission::Render::MeasureRichText(
                            device, readable, taskPx, actions[actionIndex], itemTextW);
                    const float itemW = kRailMarkerW +
                        (std::min)(itemTextW, actionMetrics.width);
                    if (actionIndex > first) {
                        cursorX += kRailItemGap;
                        L::DrawString(dl, meta, metaPx, cursorX,
                                      lineY + (lineH - metaPx) * 0.5f,
                                      IM_COL32(155, 165, 175, 210), ">");
                        cursorX += separatorW + kRailItemGap;
                    }

                    const HudPolicy::ActionStepVisualState visual =
                        HudPolicy::ResolveActionStepVisualState(
                            actionIndex, s.sequenceIndex, s.sequenceArmed,
                            taskFailed, taskDone);
                    ImU32 color = IM_COL32(255, 255, 255, 165);
                    ImU32 underline = 0;
                    switch (visual) {
                        case HudPolicy::ActionStepVisualState::Done:
                            color = IM_COL32(135, 235, 150, 255);
                            break;
                        case HudPolicy::ActionStepVisualState::Failed:
                            color = IM_COL32(255, 110, 110, 255);
                            underline = IM_COL32(255, 90, 90, 240);
                            break;
                        case HudPolicy::ActionStepVisualState::Armed:
                            color = IM_COL32(255, 225, 120, 255);
                            underline = IM_COL32(255, 220, 105, 240);
                            break;
                        case HudPolicy::ActionStepVisualState::Current:
                            color = T::kTextActive;
                            underline = IM_COL32(105, 235, 235, 230);
                            break;
                        case HudPolicy::ActionStepVisualState::Future:
                            break;
                    }

                    const float markX = cursorX + 5.0f;
                    const float markY = lineY + lineH * 0.5f;
                    if (visual == HudPolicy::ActionStepVisualState::Done) {
                        dl->AddLine(ImVec2(markX - 4.0f, markY),
                                    ImVec2(markX - 1.0f, markY + 3.0f),
                                    color, 1.4f);
                        dl->AddLine(ImVec2(markX - 1.0f, markY + 3.0f),
                                    ImVec2(markX + 5.0f, markY - 4.0f),
                                    color, 1.4f);
                    } else if (visual == HudPolicy::ActionStepVisualState::Failed) {
                        dl->AddLine(ImVec2(markX - 3.5f, markY - 3.5f),
                                    ImVec2(markX + 3.5f, markY + 3.5f),
                                    color, 1.4f);
                        dl->AddLine(ImVec2(markX + 3.5f, markY - 3.5f),
                                    ImVec2(markX - 3.5f, markY + 3.5f),
                                    color, 1.4f);
                    } else if (visual == HudPolicy::ActionStepVisualState::Armed) {
                        dl->AddCircleFilled(ImVec2(markX, markY), 3.5f, color, 8);
                    } else if (visual == HudPolicy::ActionStepVisualState::Current) {
                        dl->AddTriangleFilled(ImVec2(markX - 3.0f, markY - 4.0f),
                                              ImVec2(markX - 3.0f, markY + 4.0f),
                                              ImVec2(markX + 4.0f, markY), color);
                    } else {
                        char number[8]{};
                        _snprintf_s(number, sizeof(number), _TRUNCATE,
                                    "%d", actionIndex + 1);
                        const float numberW = L::MeasureTextW(meta, metaPx, number);
                        L::DrawString(dl, meta, metaPx,
                                      markX - numberW * 0.5f,
                                      markY - metaPx * 0.5f,
                                      T::kTextInactive, number);
                    }

                    richDraw(readable, taskPx, cursorX + kRailMarkerW,
                             lineY + (lineH - actionMetrics.height) * 0.5f,
                             color, actions[actionIndex], itemTextW);
                    if (underline != 0) {
                        dl->AddRectFilled(
                            ImVec2(cursorX, lineY + lineH - 1.5f),
                            ImVec2(cursorX + itemW, lineY + lineH), underline);
                    }
                    cursorX += itemW;
                }
            }
            lineY += lineH + kRailLineGap;
            first = end;
        }
        if (clipped) {
            char rangeText[48]{};
            _snprintf_s(rangeText, sizeof(rangeText), _TRUNCATE,
                        "STEPS %d-%d OF %d", firstVisibleAction + 1,
                        endVisibleAction, totalActionCount);
            const float summaryW = L::MeasureTextW(meta, metaPx, rangeText);
            const float summaryH = (std::max)(12.0f, metaPx + 2.0f);
            const float summaryY = originY + result.height + kRailLineGap;
            result.width = (std::max)(result.width, summaryW);
            result.height += kRailLineGap + summaryH;
            if (draw) {
                L::DrawString(dl, meta, metaPx, originX, summaryY,
                              IM_COL32(155, 165, 175, 220), rangeText);
            }
        }
        return result;
    };
    auto requirementHeight = [&](int index) {
        const ::Mission::Render::RichTextMetrics m = requirementMetrics(index);
        const ActionRailMetrics rail = actionRail(
            index, false, 0.0f, 0.0f, false, false);
        const float railH = rail.height > 0.0f
            ? kRailTopGap + rail.height : 0.0f;
        return (std::max)(22.0f, m.height + railH + kRowPadY * 2.0f);
    };
    const float omissionH = (std::max)(12.0f, metaPx + 4.0f);
    auto windowHeight = [&](int first, int count) {
        float height = 0.0f;
        auto addRow = [&](float rowH) {
            if (height > 0.0f) height += kRowGap;
            height += rowH;
        };
        if (first > 0) addRow(omissionH);
        for (int row = 0; row < count; ++row) {
            addRow(requirementHeight(first + row));
        }
        if (first + count < taskCount) addRow(omissionH);
        return height;
    };

    int visibleCount = (std::min)(
        taskCount, HudPolicy::MaxVisibleRequirements());
    int firstTask = HudPolicy::RequirementWindowStart(
        taskCount, visibleCount, s.task);
    while (visibleCount > 1 &&
           windowHeight(firstTask, visibleCount) > bounds.h) {
        --visibleCount;
        firstTask = HudPolicy::RequirementWindowStart(
            taskCount, visibleCount, s.task);
    }

    float cy = bounds.y;
    auto advanceRow = [&](float rowH) {
        cy += rowH + kRowGap;
    };
    auto drawOmission = [&]() {
        const float w = 34.0f;
        dl->AddRectFilled(ImVec2(bounds.x, cy),
                          ImVec2(bounds.x + w, cy + omissionH),
                          IM_COL32(0, 0, 0, 165), 2.0f);
        const float dotsW = L::MeasureTextW(meta, metaPx, "...");
        L::DrawString(dl, meta, metaPx,
                      bounds.x + (w - dotsW) * 0.5f,
                      cy + (omissionH - metaPx) * 0.5f,
                      T::kTextInactive, "...");
        advanceRow(omissionH);
    };

    dl->PushClipRect(ImVec2(bounds.x, bounds.y),
                     ImVec2(bounds.x + bounds.w, bounds.y + bounds.h), true);
    if (firstTask > 0) drawOmission();
    for (int row = 0; row < visibleCount; ++row) {
        const int index = firstTask + row;
        const uint8_t taskState = index >= 0 &&
            static_cast<size_t>(index) < s.taskStates.size()
                ? s.taskStates[static_cast<size_t>(index)] : uint8_t{0};
        const bool done = (taskState & uint8_t{1}) != 0;
        const bool formalFailure = (taskState & uint8_t{2}) != 0;
        const bool current = index == s.task;
        const bool coachedWrongAction = current &&
            s.phase == Phase::TaskActive && s.feedbackTicks > 0 &&
            !s.feedbackSuccess;
        const bool failed = formalFailure || coachedWrongAction;
        const bool waitingForRelease = current && s.phase == Phase::NeutralGate;
        const bool waitingForCue = current && s.episodeCueActive;
        const bool waiting = waitingForRelease || waitingForCue;
        const ::Mission::Render::RichTextMetrics textMetrics =
            requirementMetrics(index);
        const ActionRailMetrics railMetrics = actionRail(
            index, false, 0.0f, 0.0f, failed, done);
        const float railH = railMetrics.height > 0.0f
            ? kRailTopGap + railMetrics.height : 0.0f;
        const float rowH = (std::max)(
            22.0f, textMetrics.height + railH + kRowPadY * 2.0f);
        const float mainW = (std::max)(
            kMinMainW,
            (std::max)((std::min)(maxTextW, textMetrics.width),
                       railMetrics.width) + kTextPadX * 2.0f);
        const float mainX = bounds.x + kCapW - kCapOverlap;
        const float rowRight = mainX + mainW;

        dl->AddRectFilled(ImVec2(mainX, cy), ImVec2(rowRight, cy + rowH),
                          IM_COL32(0, 0, 0, 165), 2.0f);
        if (current && (failed || done)) {
            dl->AddRectFilled(ImVec2(mainX, cy), ImVec2(rowRight, cy + rowH),
                              failed ? IM_COL32(150, 35, 35, 90)
                                     : IM_COL32(35, 125, 70, 70),
                              2.0f);
        }

        const ImVec2 cap[6] = {
            ImVec2(bounds.x + 3.0f, cy),
            ImVec2(bounds.x + kCapW - 5.0f, cy),
            ImVec2(bounds.x + kCapW, cy + rowH * 0.5f),
            ImVec2(bounds.x + kCapW - 5.0f, cy + rowH),
            ImVec2(bounds.x + 3.0f, cy + rowH),
            ImVec2(bounds.x, cy + rowH * 0.5f)
        };
        const ImU32 waitingColor = IM_COL32(255, 220, 120, 255);
        const ImU32 capColor = failed ? IM_COL32(155, 45, 45, 205)
            : done ? IM_COL32(40, 125, 75, 205)
            : waiting ? IM_COL32(145, 105, 35, 210)
            : current ? IM_COL32(30, 135, 165, 205)
                      : IM_COL32(0, 0, 0, 190);
        dl->AddConvexPolyFilled(cap, 6, capColor);

        const float markX = bounds.x + 13.0f;
        const float markY = cy + rowH * 0.5f;
        const ImU32 doneColor = IM_COL32(135, 235, 150, 255);
        const ImU32 failedColor = IM_COL32(255, 110, 110, 255);
        if (failed) {
            dl->AddLine(ImVec2(markX - 4.0f, markY - 4.0f),
                        ImVec2(markX + 4.0f, markY + 4.0f), failedColor, 1.5f);
            dl->AddLine(ImVec2(markX + 4.0f, markY - 4.0f),
                        ImVec2(markX - 4.0f, markY + 4.0f), failedColor, 1.5f);
        } else if (done) {
            dl->AddLine(ImVec2(markX - 5.0f, markY),
                        ImVec2(markX - 1.0f, markY + 4.0f), doneColor, 1.7f);
            dl->AddLine(ImVec2(markX - 1.0f, markY + 4.0f),
                        ImVec2(markX + 6.0f, markY - 5.0f), doneColor, 1.7f);
        } else if (waiting) {
            dl->AddLine(ImVec2(markX - 4.0f, markY - 5.0f),
                        ImVec2(markX - 4.0f, markY + 5.0f), waitingColor, 1.7f);
            dl->AddLine(ImVec2(markX + 3.0f, markY - 5.0f),
                        ImVec2(markX + 3.0f, markY + 5.0f), waitingColor, 1.7f);
        } else if (current) {
            dl->AddTriangleFilled(ImVec2(markX - 3.0f, markY - 5.0f),
                                  ImVec2(markX - 3.0f, markY + 5.0f),
                                  ImVec2(markX + 5.0f, markY),
                                  IM_COL32(180, 250, 250, 255));
        } else {
            char number[8] = {};
            _snprintf_s(number, sizeof(number), _TRUNCATE, "%d", index + 1);
            const float numberW = L::MeasureTextW(meta, metaPx, number);
            L::DrawString(dl, meta, metaPx, markX - numberW * 0.5f,
                          markY - metaPx * 0.5f, T::kTextInactive, number);
        }

        const ImU32 textColor = failed ? failedColor
            : done ? doneColor
            : waiting ? waitingColor
            : current ? T::kTextActive : IM_COL32(255, 255, 255, 175);
        const float contentH = textMetrics.height + railH;
        const float textY = cy + (rowH - contentH) * 0.5f;
        richDraw(readable, taskPx, mainX + kTextPadX, textY,
                 textColor, requirementText(index), maxTextW);
        if (railMetrics.height > 0.0f) {
            (void)actionRail(index, true, mainX + kTextPadX,
                             textY + textMetrics.height + kRailTopGap,
                             failed, done);
        }

        if (current) {
            const ImU32 underline = failed ? failedColor
                : done ? doneColor
                : waiting ? waitingColor
                : IM_COL32(105, 235, 235, 230);
            dl->AddRectFilled(ImVec2(mainX, cy + rowH - 1.5f),
                              ImVec2(rowRight, cy + rowH), underline);
        }
        advanceRow(rowH);
    }
    if (firstTask + visibleCount < taskCount) drawOmission();
    dl->PopClipRect();

}

void ReleaseRenderThreadState() {
    UiSnapshotSlot* slots = nullptr;
    DWORD tlsIndex = TLS_OUT_OF_INDEXES;
    {
        std::lock_guard<std::mutex> lk(g_uiSnapshotTlsMx);
        tlsIndex = g_uiSnapshotTls.exchange(
            TLS_OUT_OF_INDEXES, std::memory_order_acq_rel);
        slots = g_uiSnapshotSlots;
        g_uiSnapshotSlots = nullptr;
        g_uiSnapshotTlsFailureLogged = false;
    }

    if (tlsIndex != TLS_OUT_OF_INDEXES) TlsFree(tlsIndex);
    while (slots) {
        UiSnapshotSlot* next = slots->next;
        delete slots;
        slots = next;
    }
}

}
