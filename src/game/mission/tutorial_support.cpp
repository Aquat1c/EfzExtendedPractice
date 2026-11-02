#include "../../../include/game/mission/tutorial_support.h"
#include "../../../include/game/mission/mission_data.h"
#include "../../../include/game/mission/tutorial_episode_policy.h"
#include "../../../include/game/mission/tutorial_state_policy.h"

#include "../../../include/core/logger.h"
#include "../../../include/utils/utilities.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

using json = nlohmann::json;

namespace Mission::Tutorial {

namespace {

const char* const kSupported[] = {
    "pages",
    "tutorial_tasks",
    "task_checkpoints",
    "progress_store",
    "choice_tasks",
    "action_events",
    "input_edges",
    "ordered_actions",
    "coherent_state",   // gauge/state goal predicates (sp/rf/guard/hp thresholds)
    "juggle_state",     // exact +0x124 untech and launched/airtech state goals
    "dummy_script",     // dummy (P2) receives injected inputs while a task is active
    "input_commit_mapping", // consumed attack edge bound to the resulting move instance
    "block_state",      // pinned direct strike verified by the defender entering blockstun
    "rg_state",         // pinned strike verified by a FRESH defender RG entry edge (168/169/170)
    "task_state_seeds", // per-task rf/blueIC/meter/hp baseline applied on arm/re-arm
    "wakeup_state",     // afterWake window over the shipped groundtech->actionable edge
    "dummy_state",      // same-sample dummy state qualifier (airborne/downed/airtech/launched/blockstun)
    "whiff_window",     // punish window after an episode-known dummy attack misses
    "action_absence",   // succeed by NOT acting inside an authored window
    "result_comparison",// sensed metric of this attempt vs an earlier task's (card + optional gate)
    "combo_lifecycle",  // endsCombo boundary: metrics cover the combo to its end edge
    "branch_on_outcome",// starter classified hit-vs-block; matching continuation demanded
    "episode_script",   // scripted multi-action dummy strings with authored gaps
    "episode_telegraph",// neutral-hop tell before a dash-normal opener (reactable drills)
    "rf_lock",          // session-long RF freeze at the authored player/dummy rf
    "cancel_transition",// destination action must start directly out of an authored source move
    "projectile_interception", // curated Shiori shield #423 vs Sayuri projectile #401
    "flicker_ic",       // curated Sayuri 236A #401 -> Red IC transition
    "otg_contact",      // strict resolver event: target was downed before a direct hit
    "direct_contact_state", // strict direct hit + exact defender move before contact
    "guard_point_contact", // curated Misaki 66B -> Ayu 6C exact resolver branch
};

std::mutex g_mx;
bool g_loaded = false;
json g_store;
std::string g_path;
std::string g_pendingLoadWarning;

std::string StorePath() {
    char buf[MAX_PATH] = {0};
    HMODULE mod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&StorePath), &mod);
    GetModuleFileNameA(mod, buf, MAX_PATH);
    std::string p(buf);
    const size_t slash = p.find_last_of("\\/");
    if (slash != std::string::npos) p = p.substr(0, slash + 1);
    return p + "efz_tutorial_progress.json";
}

bool EnsureLoadedLocked(std::string& warning) {
    if (g_loaded) return true;
    g_loaded = true;
    g_path = StorePath();
    std::ifstream in(g_path, std::ios::binary);
    if (!in.good()) {
        g_store = json{{"format", 1}, {"curricula", json::object()}};
        return true;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    in.close();
    try {
        g_store = json::parse(ss.str());
        if (!g_store.is_object() || !g_store.contains("curricula")) {
            throw std::runtime_error("missing curricula");
        }
        return true;
    } catch (const std::exception& e) {
        const std::string bad = g_path + ".bad";
        DeleteFileA(bad.c_str());
        MoveFileA(g_path.c_str(), bad.c_str());
        g_store = json{{"format", 1}, {"curricula", json::object()}};
        warning = std::string("tutorial progress was unreadable (") + e.what() +
                  "); the old file was kept as .bad and progress restarted";
        g_pendingLoadWarning = warning;
        LogOut("[TUTORIAL][PROGRESS] " + warning, true);
        return false;
    }
}

bool WriteStoreLocked(std::string& warning) {
    const std::string tmp = g_path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.good()) { warning = "cannot write progress temp file"; return false; }
        out << g_store.dump(2);
        out.flush();
        if (!out.good()) { warning = "progress temp write failed"; return false; }
    }
    if (!MoveFileExA(tmp.c_str(), g_path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        warning = "progress atomic replace failed";
        return false;
    }
    return true;
}

json& CurriculumLocked(const std::string& packId) {
    json& cur = g_store["curricula"][packId];
    if (!cur.is_object()) cur = json::object();
    return cur;
}

// operator[] mints null nodes; value() on a null node throws type_error.306.
json& LessonLocked(json& cur, const std::string& lessonId) {
    json& lessons = cur["lessons"];
    if (!lessons.is_object()) lessons = json::object();
    json& L = lessons[lessonId];
    if (!L.is_object()) L = json::object();
    return L;
}

}

bool IsCapabilitySupported(const std::string& cap) {
    for (const char* s : kSupported) if (cap == s) return true;
    return false;
}

std::vector<std::string> MissingCapabilities(const std::vector<std::string>& requires) {
    std::vector<std::string> missing;
    for (const auto& r : requires) {
        if (!IsCapabilitySupported(r)) missing.push_back(r);
    }
    return missing;
}

std::vector<std::string> RuntimeSupportIssues(const ::Mission::Mission& lesson) {
    std::vector<std::string> issues = MissingCapabilities(lesson.requires);
    auto declares = [&](const char* cap) {
        return std::find(lesson.requires.begin(), lesson.requires.end(), cap) !=
               lesson.requires.end();
    };
    auto add = [&](const std::string& issue) {
        if (std::find(issues.begin(), issues.end(), issue) == issues.end()) {
            issues.push_back(issue);
        }
    };

    if (lesson.tutorialSchema <= 0 || !lesson.hasLesson) return issues;
    if (lesson.requiresExactBaseline && lesson.savestate.empty()) {
        add("exact start state is not authored");
    }
    if (!lesson.lesson.pages.empty() && !declares("pages")) {
        add("pages capability is not declared");
    }
    if (!lesson.lesson.tasks.empty() && !declares("tutorial_tasks")) {
        add("tutorial_tasks capability is not declared");
    }
    if (!lesson.lesson.episodes.empty() && !declares("dummy_script")) {
        add("dummy_script capability is not declared");
    }
    if (declares("dummy_script") && lesson.lesson.episodes.empty()) {
        add("dummy episodes are not authored");
    }
    bool hasAuthoredContact = false;
    bool hasAuthoredOtgContact = false;
    bool hasAuthoredDirectContactState = false;
    bool hasAuthoredGuardPointContact = false;
    bool hasAuthoredCancelTransition = false;
    bool hasAuthoredFlickerIC = false;
    bool hasAuthoredJuggleState = false;
    bool hasAuthoredWakeupWindow = false;
    bool hasAuthoredDummyState = false;
    bool hasAuthoredWhiffWindow = false;
    bool hasAuthoredAbsence = false;
    bool hasAuthoredCompare = false;
    bool hasAuthoredEndsCombo = false;
    bool hasAuthoredBranch = false;
    bool hasAuthoredScript = false;
    bool hasAuthoredGapWindow = false;
    bool hasAuthoredTelegraph = false;
    for (const auto& e : lesson.lesson.episodes) {
        hasAuthoredScript = hasAuthoredScript || !e.script.empty();
        hasAuthoredTelegraph = hasAuthoredTelegraph || !e.telegraph.empty();
    }
    for (const auto& task : lesson.lesson.tasks) {
        hasAuthoredContact = hasAuthoredContact || task.hasContact;
        hasAuthoredOtgContact = hasAuthoredOtgContact ||
            (task.hasContact && task.contact.targetStateBefore == "downed");
        hasAuthoredDirectContactState = hasAuthoredDirectContactState ||
            (task.hasContact && task.contact.source == "direct" &&
             task.contact.result == "hit" && task.contact.target == "dummy" &&
             !task.dummyStateMoveIds.empty());
        hasAuthoredGuardPointContact = hasAuthoredGuardPointContact ||
            (task.hasContact && task.contact.source == "direct" &&
             task.contact.result == "guard_point");
        hasAuthoredCompare = hasAuthoredCompare || task.hasCompare;
        hasAuthoredEndsCombo = hasAuthoredEndsCombo || task.endsCombo;
        hasAuthoredBranch = hasAuthoredBranch || task.hasBranch;
        hasAuthoredGapWindow = hasAuthoredGapWindow || task.duringGap;
        hasAuthoredJuggleState = hasAuthoredJuggleState ||
            (task.hasGoalState &&
             ::Mission::TutorialStatePolicy::IsJuggleField(task.goalState.field));
        hasAuthoredWakeupWindow = hasAuthoredWakeupWindow ||
                                  task.afterWakeMaxTicks > 0;
        hasAuthoredDummyState = hasAuthoredDummyState ||
            !task.dummyState.empty() || !task.dummyStateMoveIds.empty();
        hasAuthoredWhiffWindow = hasAuthoredWhiffWindow ||
                                 task.whiffWindowMaxTicks > 0;
        hasAuthoredAbsence = hasAuthoredAbsence || task.hasAbsence;
        for (const auto& action : task.sequence) {
            hasAuthoredContact = hasAuthoredContact || action.hasContact;
            hasAuthoredOtgContact = hasAuthoredOtgContact ||
                (action.hasContact &&
                 action.contact.targetStateBefore == "downed");
            hasAuthoredDirectContactState = hasAuthoredDirectContactState ||
                (action.hasContact && action.contact.source == "direct" &&
                 action.contact.result == "hit" &&
                 action.contact.target == "dummy" &&
                 !action.dummyStateMoveIds.empty());
            hasAuthoredGuardPointContact = hasAuthoredGuardPointContact ||
                (action.hasContact && action.contact.source == "direct" &&
                 action.contact.result == "guard_point");
            hasAuthoredCancelTransition = hasAuthoredCancelTransition ||
                                          !action.fromMoveIds.empty();
            hasAuthoredFlickerIC = hasAuthoredFlickerIC || action.hasFlickerIC;
            hasAuthoredWakeupWindow = hasAuthoredWakeupWindow ||
                                      action.afterWakeMaxTicks > 0;
            hasAuthoredDummyState = hasAuthoredDummyState ||
                !action.dummyState.empty() || !action.dummyStateMoveIds.empty();
            hasAuthoredWhiffWindow = hasAuthoredWhiffWindow ||
                                     action.whiffWindowMaxTicks > 0;
            hasAuthoredGapWindow = hasAuthoredGapWindow || action.duringGap;
        }
        for (const auto& action : task.branchOnHit) {
            hasAuthoredCancelTransition = hasAuthoredCancelTransition ||
                                          !action.fromMoveIds.empty();
        }
    }
    if (declares("typed_contact") && !hasAuthoredContact) {
        add("typed contact outcomes are not authored");
    }
    if (declares("otg_contact") && !hasAuthoredOtgContact) {
        add("OTG contact is declared without a downed pre-contact state");
    }
    if (hasAuthoredOtgContact && !declares("otg_contact")) {
        add("otg_contact capability is not declared");
    }
    if (declares("direct_contact_state") &&
        !hasAuthoredDirectContactState) {
        add("direct contact pre-state is not authored");
    }
    if (hasAuthoredDirectContactState &&
        !declares("direct_contact_state")) {
        add("direct_contact_state capability is not declared");
    }
    if (declares("guard_point_contact") && !hasAuthoredGuardPointContact) {
        add("guard-point contact is not authored");
    }
    if (hasAuthoredGuardPointContact && !declares("guard_point_contact")) {
        add("guard_point_contact capability is not declared");
    }
    if (declares("cancel_transition") && !hasAuthoredCancelTransition) {
        add("cancel transitions are not authored");
    }
    if (declares("flicker_ic") && !hasAuthoredFlickerIC) {
        add("FIC transition contract is not authored");
    }
    if (hasAuthoredFlickerIC && !declares("flicker_ic")) {
        add("flicker_ic capability is not declared");
    }
    if (declares("wakeup_state") && !hasAuthoredWakeupWindow) {
        add("wakeup window is not authored");
    }
    if (hasAuthoredWakeupWindow && !declares("wakeup_state")) {
        add("wakeup_state capability is not declared");
    }
    if (declares("dummy_state") && !hasAuthoredDummyState) {
        add("dummy state qualifier is not authored");
    }
    if (hasAuthoredDummyState && !declares("dummy_state")) {
        add("dummy_state capability is not declared");
    }
    if (declares("whiff_window") && !hasAuthoredWhiffWindow) {
        add("whiff window is not authored");
    }
    if (hasAuthoredWhiffWindow && !declares("whiff_window")) {
        add("whiff_window capability is not declared");
    }
    if (declares("action_absence") && !hasAuthoredAbsence) {
        add("absence contract is not authored");
    }
    if (hasAuthoredAbsence && !declares("action_absence")) {
        add("action_absence capability is not declared");
    }
    if (declares("juggle_state") && !hasAuthoredJuggleState) {
        add("juggle-state goal predicate is not authored");
    }
    if (declares("result_comparison") && !hasAuthoredCompare) {
        add("comparison contract is not authored");
    }
    if (hasAuthoredCompare && !declares("result_comparison")) {
        add("result_comparison capability is not declared");
    }
    if (declares("combo_lifecycle") && !hasAuthoredEndsCombo) {
        add("combo-lifecycle boundary is not authored");
    }
    if (hasAuthoredEndsCombo && !declares("combo_lifecycle")) {
        add("combo_lifecycle capability is not declared");
    }
    if (declares("branch_on_outcome") && !hasAuthoredBranch) {
        add("outcome branch is not authored");
    }
    if (hasAuthoredBranch && !declares("branch_on_outcome")) {
        add("branch_on_outcome capability is not declared");
    }
    if (declares("episode_script") && !hasAuthoredScript) {
        add("episode script is not authored");
    }
    if ((hasAuthoredScript || hasAuthoredGapWindow) && !declares("episode_script")) {
        add("episode_script capability is not declared");
    }
    if (declares("episode_telegraph") && !hasAuthoredTelegraph) {
        add("episode telegraph is not authored");
    }
    if (hasAuthoredTelegraph && !declares("episode_telegraph")) {
        add("episode_telegraph capability is not declared");
    }
    const bool hasAuthoredRfLock = lesson.player.rfLock || lesson.dummy.rfLock;
    if (declares("rf_lock") && !hasAuthoredRfLock) {
        add("rf lock is not authored");
    }
    if (hasAuthoredRfLock && !declares("rf_lock")) {
        add("rf_lock capability is not declared");
    }
    for (const auto& e : lesson.lesson.episodes) {
        if (!e.variants.empty()) {
            bool kindInPool = false;
            for (const auto& v : e.variants) {
                if (v != "idle" && v != "block" && v != "rg") {
                    add("dummy episode '" + e.id + "' variant '" + v +
                        "' is not a lease-only kind");
                }
                if (v == e.kind) kindInPool = true;
            }
            if (!kindInPool) {
                add("dummy episode '" + e.id + "' kind '" + e.kind +
                    "' must be one of its variants");
            }
            continue;
        }
        if (e.kind == "idle" || e.kind == "block" || e.kind == "rg") continue;
        if (e.kind == "guard") {
            if (e.clip != "stand" && e.clip != "crouch") {
                add("dummy episode '" + e.id + "' guard clip '" + e.clip +
                    "' must be stand or crouch");
            }
            continue;
        }
        if (e.kind == "airtech") {
            if (e.clip != "forward" && e.clip != "backward") {
                add("dummy episode '" + e.id + "' airtech clip '" + e.clip +
                    "' must be forward or backward");
            }
            continue;
        }
        if (e.kind == "block_answer" || e.kind == "rg_answer") {
            if (e.action.empty() ||
                !::Mission::TutorialEpisodePolicy::IsInjectableAction(e.action) ||
                e.action == "jump" ||
                ::Mission::TutorialEpisodePolicy::IsDashNormalAction(e.action) ||
                ::Mission::TutorialEpisodePolicy::IsContactChainAction(e.action)) {
                add("dummy episode '" + e.id + "' answer action '" + e.action +
                    "' is not implemented");
            }
            if (!::Mission::TutorialEpisodePolicy::IsSupportedRepeatMode(e.repeatMode)) {
                add("dummy episode '" + e.id + "' repeat mode '" + e.repeatMode +
                    "' is not implemented");
            }
            continue;
        }
        if (e.kind != "macro") {
            add("dummy episode '" + e.id + "' kind '" + e.kind + "' is not implemented");
            continue;
        }
        if (!e.script.empty()) {
            for (size_t si = 0; si < e.script.size(); ++si) {
                const auto& st = e.script[si];
                if (!st.action.empty() &&
                    !::Mission::TutorialEpisodePolicy::IsSupportedScriptAction(
                        st.action, si)) {
                    add("dummy episode '" + e.id + "' script action '" + st.action +
                        "' is not implemented");
                }
            }
            if (!::Mission::TutorialEpisodePolicy::IsSupportedApproach(e.approach)) {
                add("dummy episode '" + e.id + "' approach '" + e.approach +
                    "' is not implemented");
            }
            if (!::Mission::TutorialEpisodePolicy::IsSupportedTelegraph(e.telegraph)) {
                add("dummy episode '" + e.id + "' telegraph '" + e.telegraph +
                    "' is not implemented");
            }
            if (!::Mission::TutorialEpisodePolicy::IsSupportedRepeatMode(e.repeatMode)) {
                add("dummy episode '" + e.id + "' repeat mode '" + e.repeatMode +
                    "' is not implemented");
            }
            if (e.pausePolicy != "missionFreeze") {
                add("dummy episode '" + e.id + "' pause policy '" + e.pausePolicy +
                    "' is not implemented");
            }
            if (e.completeOn != "actionEnd") {
                add("dummy episode '" + e.id + "' completion event '" + e.completeOn +
                    "' is not implemented");
            }
            continue;
        }
        if (e.action.empty()) {
            add("dummy episode '" + e.id + "' has no injectable action");
            continue;
        }
        if (!::Mission::TutorialEpisodePolicy::IsInjectableAction(e.action)) {
            add("dummy episode '" + e.id + "' action '" + e.action + "' is not implemented");
            continue;
        }
        if ((::Mission::TutorialEpisodePolicy::IsDashNormalAction(e.action) ||
             ::Mission::TutorialEpisodePolicy::IsContactChainAction(e.action)) &&
            (e.start != "taskArmed" || e.approach != "none")) {
            add("dummy episode '" + e.id + "' scripted action needs taskArmed start and no approach");
            continue;
        }
        if (!::Mission::TutorialEpisodePolicy::IsSupportedApproach(e.approach)) {
            add("dummy episode '" + e.id + "' approach '" + e.approach + "' is not implemented");
            continue;
        }
        if (!::Mission::TutorialEpisodePolicy::IsSupportedTelegraph(e.telegraph)) {
            add("dummy episode '" + e.id + "' telegraph '" + e.telegraph + "' is not implemented");
            continue;
        }
        if (e.start == "trigger" && e.trigger == "onWakeup") {
            int mappedAction = 0, mappedStrength = 0;
            if (!::Mission::TutorialEpisodePolicy::WakeAutoActionForAction(
                    e.action, mappedAction, mappedStrength)) {
                add("dummy episode '" + e.id + "' wakeup reversal action '" +
                    e.action + "' is not supported");
                continue;
            }
        }
        if (!::Mission::TutorialEpisodePolicy::IsSupportedRepeatMode(e.repeatMode)) {
            add("dummy episode '" + e.id + "' repeat mode '" + e.repeatMode + "' is not implemented");
            continue;
        }
        if (e.pausePolicy != "missionFreeze") {
            add("dummy episode '" + e.id + "' pause policy '" + e.pausePolicy + "' is not implemented");
            continue;
        }
        if (e.completeOn != "actionEnd") {
            add("dummy episode '" + e.id + "' completion event '" + e.completeOn + "' is not implemented");
            continue;
        }
        if (e.action == "jump" && e.start != "taskArmed") {
            add("dummy episode '" + e.id + "' jump action only supports taskArmed start");
            continue;
        }
        if (e.start == "taskArmed") continue;
        if (e.start == "playerState") {
            int ignoredMove = 0;
            if (!::Mission::TutorialEpisodePolicy::ParseP1MovePredicate(
                    e.predicate, ignoredMove)) {
                add("dummy episode '" + e.id + "' playerState predicate '" +
                    e.predicate + "' is not implemented");
            }
            continue;
        }
        if (e.start == "trigger") {
            if (!::Mission::TutorialEpisodePolicy::IsSupportedTrigger(e.trigger)) {
                add("dummy episode '" + e.id + "' trigger '" + e.trigger + "' is not implemented");
            }
            continue;
        }
        add("dummy episode '" + e.id + "' start '" + e.start + "' is not implemented");
    }
    if (lesson.lesson.failureReset != "taskCheckpoint") {
        add("lesson-start failure reset is not implemented");
    }

    for (const auto& task : lesson.lesson.tasks) {
        const std::string prefix = "task " + task.id + ": ";
        if (task.kind == "choice") {
            if (!declares("choice_tasks")) add(prefix + "choice_tasks capability is not declared");
            continue;
        }
        if (task.checkpoint != "lessonStart") {
            add(prefix + "checkpoint '" + task.checkpoint + "' is not implemented");
        }
        if (!task.script.empty()) {
            if (!declares("dummy_script")) {
                add(prefix + "dummy_script capability is not declared");
            } else {
                const ::Mission::DummyEpisode* taskEpisode = nullptr;
                for (const auto& e : lesson.lesson.episodes) {
                    if (e.id == task.script) { taskEpisode = &e; break; }
                }
                if (!taskEpisode) {
                    add(prefix + "references unknown dummy episode '" + task.script + "'");
                } else if (task.hasAbsence && task.absenceEnd == "dummyAttackEnd" &&
                           taskEpisode->kind != "macro") {
                    add(prefix + "dummyAttackEnd needs a macro dummy episode");
                } else if (task.hasAbsence && task.absenceEnd == "episodeCycleEnd" &&
                           taskEpisode->kind != "macro" &&
                           taskEpisode->kind != "block_answer" &&
                           taskEpisode->kind != "rg_answer") {
                    add(prefix + "episodeCycleEnd needs an episode with a driven action cycle");
                }
            }
        }
        if (task.hasAbsence && task.absenceMinBlocks > 0) {
            if (!declares("block_state")) {
                add(prefix + "absence.minBlocks needs block_state capability");
            }
            const ::Mission::DummyEpisode* episode = nullptr;
            for (const auto& candidate : lesson.lesson.episodes) {
                if (candidate.id == task.script) { episode = &candidate; break; }
            }
            if (!episode ||
                (episode->kind != "macro" && episode->kind != "block_answer" &&
                 episode->kind != "rg_answer")) {
                add(prefix + "absence.minBlocks needs an attacking dummy episode");
            }
        }
        if (!task.demo.empty()) add(prefix + "per-task demonstration is not implemented");
        if (task.continuity != "independent" && task.continuity != "sameCombo" &&
            task.continuity != "continue") {
            add(prefix + "continuity '" + task.continuity + "' is not implemented");
        }
        if (task.hitsRequired > 1 && task.req != "hits") {
            add(prefix + "hits count is ignored unless req is hits");
        }
        if ((task.playerSeed.has || task.dummySeed.has) &&
            !declares("task_state_seeds")) {
            add(prefix + "task_state_seeds capability is not declared");
        }
        {
            const bool wantsWhiff = task.whiffWindowMaxTicks > 0 ||
                std::any_of(task.sequence.begin(), task.sequence.end(),
                            [](const ::Mission::LessonAction& action) {
                                return action.whiffWindowMaxTicks > 0;
                            });
            if (wantsWhiff && task.script.empty()) {
                add(prefix + "whiff window needs a scripted dummy attack");
            }
        }
        if (task.hasProjectileInterception) {
            if (!declares("projectile_interception")) {
                add(prefix + "projectile_interception capability is not declared");
            }
            if (lesson.player.character != "shiori" ||
                lesson.dummy.character != "sayuri" ||
                task.incomingProjectilePattern != 401 ||
                task.guardProjectilePattern != 423 ||
                std::find(task.moveIds.begin(), task.moveIds.end(), 309) ==
                    task.moveIds.end()) {
                add(prefix + "projectile interception is not a curated supported pair");
            }
            const ::Mission::DummyEpisode* episode = nullptr;
            for (const auto& candidate : lesson.lesson.episodes) {
                if (candidate.id == task.script) { episode = &candidate; break; }
            }
            if (!episode || episode->kind != "macro" ||
                episode->action != "236A" || episode->approach != "none") {
                add(prefix + "projectile interception needs the pinned Sayuri 236A episode");
            }
        }
        for (const auto& action : task.sequence) {
            if (!action.hasFlickerIC) continue;
            if (!declares("flicker_ic")) {
                add(prefix + "flicker_ic capability is not declared");
            }
            // Expanding this allowlist requires decomp or live proof; other pairs are rejected.
            const bool curated =
                lesson.player.character == "sayuri" &&
                lesson.dummy.character == "misaki" &&
                lesson.player.hasPos && lesson.dummy.hasPos &&
                lesson.player.rf == 500 && lesson.player.blueIC == 0 &&
                action.moveIds == std::vector<int>{167} &&
                action.fromMoveIds == std::vector<int>{250} &&
                action.inputMask == 64 && action.req == "commit" &&
                action.flickerProjectilePattern == 401 &&
                (action.flickerMinDistance == 0 ||
                 action.flickerMinDistance == 300);
            if (!curated) {
                add(prefix + "FIC transition is not a curated supported source");
            }
            const ::Mission::DummyEpisode* episode = nullptr;
            for (const auto& candidate : lesson.lesson.episodes) {
                if (candidate.id == task.script) { episode = &candidate; break; }
            }
            if (!episode || episode->kind != "idle") {
                add(prefix + "FIC needs its pinned idle dummy episode");
            }
            if (action.flickerMinDistance > 0) {
                const double authoredDistance =
                    std::abs(lesson.player.posX - lesson.dummy.posX);
                constexpr double kFicSpawnMarginUnits = 24.0;
                if (authoredDistance <
                    action.flickerMinDistance + kFicSpawnMarginUnits) {
                    add(prefix +
                        "FIC authored spacing needs margin above its proof distance");
                }
            }
        }
        const bool needsBlockState = task.req == "block" ||
            std::any_of(task.sequence.begin(), task.sequence.end(),
                        [](const ::Mission::LessonAction& action) {
                            return action.req == "block";
                        });
        if (needsBlockState) {
            const ::Mission::DummyEpisode* episode = nullptr;
            for (const auto& candidate : lesson.lesson.episodes) {
                if (candidate.id == task.script) { episode = &candidate; break; }
            }
            if (!episode || (episode->kind != "block" && episode->kind != "rg" &&
                             episode->kind != "guard" &&
                             episode->kind != "block_answer" &&
                             episode->kind != "rg_answer")) {
                add(prefix + "block_state needs a scripted guarding dummy");
            }
        }
        const bool needsRgState = task.req == "rg" ||
            std::any_of(task.sequence.begin(), task.sequence.end(),
                        [](const ::Mission::LessonAction& action) {
                            return action.req == "rg";
                        });
        if (needsRgState) {
            const ::Mission::DummyEpisode* episode = nullptr;
            for (const auto& candidate : lesson.lesson.episodes) {
                if (candidate.id == task.script) { episode = &candidate; break; }
            }
            if (!episode || (episode->kind != "rg" && episode->kind != "rg_answer")) {
                add(prefix + "rg_state needs a scripted RG dummy");
            }
        }

        if (task.hasGoalState) {
            if (task.goalState.requiresBlock && !declares("block_state")) {
                add(prefix + "goalState.requiresBlock needs block_state capability");
            }
            if (task.goalState.requiresBlock) {
                const ::Mission::DummyEpisode* episode = nullptr;
                for (const auto& candidate : lesson.lesson.episodes) {
                    if (candidate.id == task.script) { episode = &candidate; break; }
                }
                if (!episode || episode->kind != "macro" ||
                    (episode->action.empty() && episode->script.empty())) {
                    add(prefix + "goalState.requiresBlock needs an attacking dummy episode");
                }
            }
            if (::Mission::TutorialStatePolicy::IsJuggleField(task.goalState.field)) {
                if (!declares("juggle_state")) {
                    add(prefix + "juggle_state capability is not declared");
                }
            } else if (!declares("coherent_state")) {
                add(prefix + "coherent_state capability is not declared");
            }
        } else if (!task.sequence.empty()) {
            if (!declares("ordered_actions")) {
                add(prefix + "ordered_actions capability is not declared");
            }
            for (const auto& action : task.sequence) {
                if (action.inputMask != 0 && !declares("input_edges")) {
                    add(prefix + "input_edges capability is not declared");
                }
                if (!action.fromMoveIds.empty() && !declares("cancel_transition")) {
                    add(prefix + "cancel_transition capability is not declared");
                }
                if (action.req == "commit" && !declares("input_commit_mapping")) {
                    add(prefix + "input_commit_mapping capability is not declared");
                }
                if (action.req == "block" && !declares("block_state")) {
                    add(prefix + "block_state capability is not declared");
                }
                if (action.req == "rg" && !declares("rg_state")) {
                    add(prefix + "rg_state capability is not declared");
                }
                if (action.hasContact) {
                    const bool strictOtg =
                        action.contact.targetStateBefore == "downed";
                    const bool strictDirectState =
                        action.contact.source == "direct" &&
                        action.contact.result == "hit" &&
                        action.contact.target == "dummy" &&
                        !action.dummyStateMoveIds.empty();
                    const bool strictGuardPoint =
                        lesson.lessonId == "efz.systems.guard_attacks" &&
                        lesson.player.character == "ayu" &&
                        lesson.dummy.character == "misaki" &&
                        action.contact.attacker == "dummy" &&
                        action.contact.target == "learner" &&
                        action.contact.source == "direct" &&
                        action.contact.result == "guard_point" &&
                        action.contact.moveIds == std::vector<int>{231};
                    if (!strictOtg && !strictDirectState && !strictGuardPoint &&
                        !declares("typed_contact")) {
                        add(prefix + "typed_contact capability is not declared");
                    }
                    if (strictOtg && !declares("otg_contact")) {
                        add(prefix + "otg_contact capability is not declared");
                    }
                    if (strictDirectState &&
                        !declares("direct_contact_state")) {
                        add(prefix +
                            "direct_contact_state capability is not declared");
                    }
                    if (strictGuardPoint &&
                        !declares("guard_point_contact")) {
                        add(prefix +
                            "guard_point_contact capability is not declared");
                    }
                    if (action.contact.source != "direct") {
                        add(prefix + "contact source '" + action.contact.source + "' is not implemented");
                    }
                    if (strictOtg &&
                        (action.contact.source != "direct" ||
                         action.contact.result != "hit")) {
                        add(prefix + "OTG contact needs a direct hit result");
                    }
                    if (action.contact.result == "whiff") {
                        add(prefix + "authoritative whiff closure is not implemented");
                    }
                    if (action.contact.count > 1) {
                        add(prefix + "repeated contact action identity is not implemented");
                    }
                }
            }
        } else {
            if (task.inputMask != 0 && !declares("input_edges")) {
                add(prefix + "input_edges capability is not declared");
            }
            if (task.req == "commit" && !declares("input_commit_mapping")) {
                add(prefix + "input_commit_mapping capability is not declared");
            }
            if (task.req == "block" && !declares("block_state")) {
                add(prefix + "block_state capability is not declared");
            }
            if (task.req == "rg" && !declares("rg_state")) {
                add(prefix + "rg_state capability is not declared");
            }
            if (task.hasContact) {
                const bool strictOtg =
                    task.contact.targetStateBefore == "downed";
                const bool strictDirectState =
                    task.contact.source == "direct" &&
                    task.contact.result == "hit" &&
                    task.contact.target == "dummy" &&
                    !task.dummyStateMoveIds.empty();
                const bool strictGuardPoint =
                    lesson.lessonId == "efz.systems.guard_attacks" &&
                    lesson.player.character == "ayu" &&
                    lesson.dummy.character == "misaki" &&
                    task.contact.attacker == "dummy" &&
                    task.contact.target == "learner" &&
                    task.contact.source == "direct" &&
                    task.contact.result == "guard_point" &&
                    task.contact.moveIds == std::vector<int>{231};
                if (!strictOtg && !strictDirectState && !strictGuardPoint &&
                    !declares("typed_contact")) {
                    add(prefix + "typed_contact capability is not declared");
                }
                if (strictOtg && !declares("otg_contact")) {
                    add(prefix + "otg_contact capability is not declared");
                }
                if (strictDirectState &&
                    !declares("direct_contact_state")) {
                    add(prefix +
                        "direct_contact_state capability is not declared");
                }
                if (strictGuardPoint &&
                    !declares("guard_point_contact")) {
                    add(prefix +
                        "guard_point_contact capability is not declared");
                }
                if (task.contact.source != "direct") {
                    add(prefix + "contact source '" + task.contact.source + "' is not implemented");
                }
                if (strictOtg &&
                    (task.contact.source != "direct" ||
                     task.contact.result != "hit")) {
                    add(prefix + "OTG contact needs a direct hit result");
                }
                if (task.contact.result == "whiff") {
                    add(prefix + "authoritative whiff closure is not implemented");
                }
                if (task.contact.count > 1) {
                    add(prefix + "repeated contact action identity is not implemented");
                }
            }
            for (int id : task.moveIds) {
                if (id <= 0) {
                    add(prefix + "committed action is still a placeholder");
                    break;
                }
            }
        }
        if (task.hasBranch) {
            for (const auto& action : task.branchOnHit) {
                if (!action.fromMoveIds.empty() && !declares("cancel_transition")) {
                    add(prefix + "cancel_transition capability is not declared");
                }
            }
        }
    }
    return issues;
}

bool ProgressLoad(std::string& warning) {
    std::lock_guard<std::mutex> lk(g_mx);
    warning.clear();
    std::string loadWarning;
    const bool loadedCleanly = EnsureLoadedLocked(loadWarning);
    if (!g_pendingLoadWarning.empty()) {
        warning = g_pendingLoadWarning;
        g_pendingLoadWarning.clear();
        return false;
    }
    if (!loadedCleanly) warning = loadWarning;
    return loadedCleanly;
}

LessonProgress ProgressGet(const std::string& packId, const std::string& lessonId) {
    std::lock_guard<std::mutex> lk(g_mx);
    std::string w;
    EnsureLoadedLocked(w);
    LessonProgress out;
    try {
        const auto& cur = g_store["curricula"];
        if (!cur.contains(packId) || !cur[packId].is_object()) return out;
        const json lessons = cur[packId].value("lessons", json::object());
        if (!lessons.contains(lessonId) || !lessons[lessonId].is_object()) return out;
        const auto& L = lessons[lessonId];
        out.cleared = L.value("cleared", false);
        out.attempts = L.value("attempts", 0);
        out.firstClearAt = L.value("firstClearAt", 0LL);
        out.lastClearAt = L.value("lastClearAt", 0LL);
        out.revisionCleared = L.value("revisionCleared", 0);
    } catch (const std::exception&) {
        return LessonProgress{};
    }
    return out;
}

bool ProgressRecordClear(const std::string& packId, const std::string& lessonId,
                         int revision, int attemptsThisRun, std::string& warning) {
    std::lock_guard<std::mutex> lk(g_mx);
    // Progress IO must never throw out of the session tick (monitor thread).
    try {
        EnsureLoadedLocked(warning);
        json& cur = CurriculumLocked(packId);
        json& L = LessonLocked(cur, lessonId);
        const long long now = static_cast<long long>(time(nullptr));
        if (!L.value("cleared", false)) L["firstClearAt"] = now;
        L["cleared"] = true;
        L["lastClearAt"] = now;
        L["attempts"] = L.value("attempts", 0) + attemptsThisRun;
        L["revisionCleared"] = revision;
        cur["lastLesson"] = lessonId;
        if (!WriteStoreLocked(warning)) {
            LogOut("[TUTORIAL][PROGRESS] clear kept in session; write failed: " + warning, true);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        warning = std::string("progress store error: ") + e.what();
        LogOut("[TUTORIAL][PROGRESS] clear kept in session; " + warning, true);
        return false;
    }
}

void ProgressNoteAttempt(const std::string& packId, const std::string& lessonId) {
    std::lock_guard<std::mutex> lk(g_mx);
    std::string w;
    try {
        EnsureLoadedLocked(w);
        json& L = LessonLocked(CurriculumLocked(packId), lessonId);
        L["attempts"] = L.value("attempts", 0) + 1;
    } catch (const std::exception& e) {
        LogOut(std::string("[TUTORIAL][PROGRESS] attempt note dropped: ") + e.what(), true);
    }
}

std::string ProgressLastLesson(const std::string& packId) {
    std::lock_guard<std::mutex> lk(g_mx);
    std::string w;
    try {
        EnsureLoadedLocked(w);
        const auto& cur = g_store["curricula"];
        if (!cur.contains(packId) || !cur[packId].is_object()) return std::string();
        return cur[packId].value("lastLesson", std::string());
    } catch (const std::exception&) {
        return std::string();
    }
}

void ProgressSetLastLesson(const std::string& packId, const std::string& lessonId) {
    std::lock_guard<std::mutex> lk(g_mx);
    std::string w;
    try {
        EnsureLoadedLocked(w);
        json& curriculum = CurriculumLocked(packId);
        if (curriculum.value("lastLesson", std::string()) == lessonId) return;
        curriculum["lastLesson"] = lessonId;
        if (!WriteStoreLocked(w)) {
            LogOut("[TUTORIAL][PROGRESS] lastLesson kept in session; " + w, true);
        }
    } catch (const std::exception& e) {
        LogOut(std::string("[TUTORIAL][PROGRESS] lastLesson dropped: ") + e.what(), true);
    }
}

std::string RenderRichText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size() && (text[i + 1] == '{' || text[i + 1] == '}')) {
            out.push_back(text[++i]);
            continue;
        }
        if (c != '{') { out.push_back(c); continue; }
        const size_t close = text.find('}', i);
        if (close == std::string::npos) { out.push_back(c); continue; }
        const std::string token = text.substr(i + 1, close - i - 1);
        const size_t colon = token.find(':');
        std::string kind = colon == std::string::npos ? token : token.substr(0, colon);
        std::string value = colon == std::string::npos ? std::string() : token.substr(colon + 1);
        if (kind == "term") {
            const size_t bar = value.find('|');
            out += bar == std::string::npos ? value
                 : value.substr(0, bar) + " (" + value.substr(bar + 1) + ")";
        } else if (kind == "ui") {
            std::string upper = value;
            for (char& ch : upper) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
            out += "[" + upper + "]";
        } else if (kind == "dir" || kind == "btn" || kind == "input") {
            out += value;
        } else {
            out += value.empty() ? token : value;
        }
        i = close;
    }
    return out;
}

}
