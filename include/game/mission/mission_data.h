#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mission_semantic_source_policy.h"

namespace Mission {

enum class StepReq : uint8_t {
    Land = 0,   // move must HIT (combo hit count increases)
    Move,       // just perform the move (whiff / movement / setup - no contact needed)
    Hits,
    Connect,    // hit or blocked; without directContact it uses the sampled +0x168 latch
};

const char* StepReqToString(StepReq req);
StepReq     StepReqFromString(const std::string& s);

struct LessonPage {
    std::string id;
    std::string title;
    std::string text;     // rich text: {dir:6}/{btn:A}/{input:236A}/{term:..}/{ui:..}
    // showHud: ""/"none" hides all; "top"/"bottom" reveal a full band; other values name native
    // elements to reveal and outline (see ParseHudFocus).
    std::string showHud;
};

struct ChoiceOption {
    std::string id;
    std::string label;
};

struct ContactPredicate {
    std::string attacker = "learner"; // learner | dummy
    std::string target = "dummy";     // learner | dummy (must be opposite)
    std::string source = "direct";    // direct | entity
    std::string result;                // hit|block|recoil_guard|throw|special|guard_point|whiff
    // Target state sampled inside the resolver before the contact mutates it; never approximate it
    // from the next snapshot (an OTG victim is already in hitstun). Empty = no constraint.
    std::string targetStateBefore;
    std::vector<int> moveIds;
    int count = 1;
};

struct LessonAction {
    std::string id;
    std::string notation;
    std::vector<int> moveIds;
    // Credited only if the move instance starts directly out of one of these sampled states:
    // "A happened, then B happened later" is not enough.
    std::vector<int> fromMoveIds;
    int inputMask = 0;                   // EFZ input bits, 0 = moveIds action
    std::string req = "move";           // input|commit|move|land|hits|connect|block|rg
    int hitsRequired = 1;
    int minGap = 0;                      // non-frozen ticks after prior action
    int maxGap = 0;                      // 0 = no implicit timeout
    // wakeup_state: credited only within this many non-frozen ticks of the dummy's wake edge; 0 = off.
    int afterWakeMaxTicks = 0;
    // dummy_state: "airborne"|"downed"|"airtech"|"launched"|"blockstun", or explicit move IDs. Checked
    // on the sample satisfying the primary req; direct contacts use defenderMoveBefore for move IDs.
    std::string dummyState;
    std::vector<int> dummyStateMoveIds;
    // whiff_window: within N non-frozen ticks after the scripted dummy attack finished without contact.
    int whiffWindowMaxTicks = 0;
    bool duringGap = false;
    bool hasContact = false;
    ContactPredicate contact;
    bool hasFlickerIC = false;
    int flickerProjectilePattern = 0;
    int flickerMinDistance = 0;
};

// Runtime-state condition on a player: resource fields need the coherent_state capability,
// juggle fields need juggle_state.
struct StatePredicate {
    std::string field;       // sp|rf|guard|hp|distance|ic | untech|launched|airtech|downed
    int         player = 1;  // 1 = the learner, 2 = the dummy
    std::string op = "ge";   // ge | le | gt | lt | eq
    int         value = 0;
    bool        requiresBlock = false;
};

// Per-task baseline, applied on arm and re-applied on every checkpoint restore. -1 = untouched.
struct TaskStateSeed {
    bool has = false;
    int rf = -1;        // 0..1000
    int blueIC = -1;    // 0 = red, 1 = blue
    int meter = -1;
    int hp = -1;
    int guard = -1;     // Guard gauge (float 0..360 at +0x134)
};

struct LessonTask {
    std::string id;
    std::string kind = "combat";
    std::string label;
    std::string prompt;
    std::string checkpoint = "lessonStart";
    std::string script;                    // dummy episode id ("" = none)
    std::string continuity = "independent";// independent|continue|sameCombo|setupGap|newCombo|free
    std::vector<int> moveIds;
    int inputMask = 0;                     // consumed button edge (0 = moveIds)
    std::string req = "move";              // combat adapter: input|commit|move|land|hits|connect|block|rg
    int hitsRequired = 1;
    std::vector<LessonAction> sequence;
    std::vector<ChoiceOption> options;
    std::vector<std::string> acceptedOptionIds;
    std::map<std::string, std::string> optionFeedback;
    std::string successText;
    std::string failureText;
    std::string demo;                      // demo id ("" = none)
    bool           hasGoalState = false;
    StatePredicate goalState;
    bool           hasContact = false;
    ContactPredicate contact;
    // Curated, not generic entity contact: observes Sayuri #401's lifetime edge while Shiori shield
    // #423 is alive. Runtime preflight rejects every other pair.
    bool hasProjectileInterception = false;
    int incomingProjectilePattern = 0;
    int guardProjectilePattern = 0;
    int completionHoldTicks = -1;           // -1 = session default; 0 = immediate
    bool restoreOnSuccess = false;
    bool  hasPos = false;
    double posX = 0.0;
    double posY = 0.0;
    bool  hasDummyPos = false;
    double dummyPosX = 0.0;
    double dummyPosY = 0.0;
    TaskStateSeed playerSeed;
    TaskStateSeed dummySeed;
    int afterWakeMaxTicks = 0;
    std::string dummyState;
    std::vector<int> dummyStateMoveIds;
    int whiffWindowMaxTicks = 0;
    bool duringGap = false;
    // action_absence: succeed by not acting in the start/end window. A new forbidden P1 action fails;
    // an inherited attack animation is ignored. Cannot mix with move/sequence/goal contracts.
    bool hasAbsence = false;
    std::string absenceForbid;               // "airtech" | "attack" ("" = ids only)
    std::vector<int> absenceForbidIds;
    std::string absenceStart = "launched";   // launched | taskArmed | dummyUntech
    std::string absenceEnd = "grounded";     // grounded | ticks | dummyUntechEmpty | dummyAttackEnd | episodeCycleEnd
    int absenceTicks = 0;                    // when end == "ticks"
    bool absenceFailOnHit = false;            // fail if P1 enters a struck/thrown state
    int absenceMinBlocks = 0;                 // require this many fresh P1 blockstun entries
    bool endsCombo = false;
    // result_comparison: without compareOp it is only a feedback card; with it the task needs
    // metric(this) <op> metric(compareVs) at completion.
    bool hasCompare = false;
    std::string compareVs;
    std::string compareMetric;               // comboHits|comboDamage|ticks|p2HpDelta|p1RfDelta|p2GuardDelta|maxUntech|untechTicks
    std::string compareOp;                   // "" (card only) | gt|lt|ge|le
    bool hasBranch = false;
    std::vector<int> branchStarterIds;
    std::vector<LessonAction> branchOnHit;
    int branchOnBlockHoldTicks = 0;
};

struct DummyEpisode {
    std::string id;
    std::string ownerTask;
    // idle = neutral P2, guard automation off; guard = native auto-block in a fixed stance (clip), so
    // the wrong stance opens it up; *_answer = guard lease plus one injected action on its edge.
    std::string kind = "macro";       // macro | idle | block | rg | guard | block_answer | rg_answer | cue
    std::string clip;                 // macro text / held input spec / guard mode
    // Preferred over clip: a motion name (5A, j.C, 236A, ...) the dummy performs by input injection,
    // closing distance first according to approach.
    std::string action;
    // When present, a different attack (a failed special becoming 5C) cannot complete the episode.
    std::vector<int> expectedMoveIds;
    std::string approach = "dash";    // dash | none (how the dummy closes first)
    // "jump" makes the dummy hop in place before its opener. Valid only on taskArmed macro episodes
    // with an authored opener.
    std::string telegraph;            // "" | jump
    // taskArmed = loop immediately; playerState = fire after reactDelay when the learner's move
    // matches predicate (e.g. "p1move:309"); trigger = fire on an auto-action trigger edge.
    std::string start = "taskArmed";
    std::string predicate;            // playerState: "p1move:<id>" to react to
    std::string trigger;              // trigger: afterBlock|onWakeup|onRG|afterHitstun|afterAirtech
    int         reactDelay = 30;      // playerState: session ticks after detection (~10 visual frames)
    std::string cueText;
    int  cueLead = 45;
    std::string repeatMode = "afterFailure";
    int  repeatDelay = 120;
    std::string pausePolicy = "missionFreeze";
    std::string completeOn = "actionEnd";
    // One lease-only kind (idle|block|rg) is picked at random per episode lease; `kind` must be in
    // the pool. Author variant-neutral cue text so the pick does not leak.
    std::vector<std::string> variants;
    // Same non-empty group: shuffled without-replacement assignment, stable across retries, so a
    // hidden hit-confirm pair can guarantee one hit and one block.
    std::string variantGroup;
    // An action step injects its move (cancel: during the prior move's cancel window); a gap step
    // (gapTicks > 0, no action) is an authored pause that task contracts can bind to.
    struct ScriptStep {
        std::string action;   // injectable action ("" = gap step)
        // Non-frozen. A cancel step waits after the preceding action is acknowledged; an ordinary step
        // waits after its own action resolves.
        int waitTicks = 0;
        int gapTicks = 0;     // gap step length (action must be empty)
        bool cancel = false;
    };
    std::vector<ScriptStep> script;
};

struct Lesson {
    std::string completion = "allTasks";   // pages | allTasks | anyTask (REQUIRED in json)
    std::string requirementPlacement = "upperLeft"; // upperLeft | belowStats; stable for the lesson
    std::string wrongAction = "coach";     // coach | fail | ignore
    std::string failureReset = "taskCheckpoint";
    bool preserveCompletedTasks = true;
    std::vector<LessonPage>   pages;
    std::vector<LessonTask>   tasks;
    std::vector<DummyEpisode> episodes;
};

struct EntityCommandOrigin {
    bool present = false;
    int slot = -1;
    int generation = 0;
    int rootPattern = -1;
    int activationPattern = -1;
};

struct Step {
    std::string      notation;
    std::vector<int> moveIds;
    EntityCommandOrigin entityCommand;
    int              expectedAttackMask = 0;
    StepReq          req = StepReq::Land;
    int              hitsRequired = 1;
    bool             allowPartialHits = false;
    // Binds Land/Hits/Connect to the exact direct player resolver; false keeps the aggregate
    // combo/latch adapter. Entity obligations use entityContacts, never this flag.
    bool             directContact = false;
    // Empty = any committed contact.
    std::string      contactResult;
    bool             optional = false;
    int              maxDelay = 0;     // 0 = unlimited; non-frozen 192 Hz ticks the armed step may wait for its hit
    int              maxGap = 0;       // 0 = mission failTimer; non-frozen 192 Hz ticks allowed after the previous step
    bool             comboEndAfter = false; // require a combo alive->dead edge before the next step; false = one-piece combo
    int              charState = -1;        // P1 character state at move start; -1 = none. Akiko: bullet cycle 0..2
    int              damage = 0;            // recorded damage delta; 0 = no requirement
};

struct EntityContactFanoutMember {
    int slot = -1;
    int generation = 0;
    std::vector<int> patterns;
    std::string producerLifecycle;
    int producerPattern = -1;
    int producerPriorPattern = -1;
    int contactsObserved = 1;
    int comboHitsObserved = 0;
    int damageObserved = 0;
};

struct EntityContactRequirement {
    std::string notation;
    int owner = 1;
    int target = 2;
    int slot = -1;
    int generation = 0;
    std::vector<int> patterns;
    std::string result = "hit";          // Contact::ResultMatches vocabulary
    int contactsRequired = 1;
    int comboHitsRequired = 0;
    int minimumContactsRequired = 0;
    int minimumComboHitsRequired = 0;
    std::vector<EntityContactFanoutMember> fanoutMembers;
    // producerLifecycle: baseline|spawn|morph. producerPriorPattern is required only for morph.
    std::string producerLifecycle;
    int producerPattern = -1;
    int producerPriorPattern = -1;
    int opensAfterAction = -1;
    // -2/-2 = older file omitted them; -1/-1 = no single causal setter proven; else the exact source
    // action. Presentation only: never weakens slot/generation or lifecycle grading.
    int semanticSourceAction =
        ::Mission::SemanticSourcePolicy::kExplicitUnresolved;
    int semanticSourceMove =
        ::Mission::SemanticSourcePolicy::kExplicitUnresolved;
    // Contact ordinals are one-based; -1 = whole-step barrier. dueBeforeStep -1 = mission end;
    // dueBeforeStepContact 0 = before action start.
    int contactAfterAction = -1;
    int afterStep = -1;
    int afterStepContact = -1;
    int dueBeforeStep = -1;
    int dueBeforeStepContact = -1;
    int segment = 0;
    int maxDelay = 0;                    // non-frozen ticks after both gates are open
    int damage = 0;                      // exact resolver-local HP delta (diagnostic)
    bool comboEndAfter = false;
};

struct EntityLifecycleRequirement {
    std::string notation;
    int owner = 1;
    int slot = -1;
    int generation = 0;
    std::string lifecycle;            // baseline|spawn|morph|despawn
    int pattern = -1;
    int priorPattern = -1;            // required by a strict morph descriptor
    int opensAfterAction = -1;
    int segment = 0;
    int maxDelay = 0;                 // non-frozen ticks after the gate opens
};

struct PlayerSetup {
    std::string character;           // internal short name; empty = keep current
    bool   hasPos = false;
    double posX = 0.0;
    double posY = 0.0;
    int    palette = 0;
    int    rf = -1;                  // starting RF (-1 = leave as-is)
    int    meter = -1;               // starting meter (-1 = leave as-is)
    int    hp = -1;                  // starting HP (-1 = leave as-is)
    int    blueIC = -1;              // -1 keep, 0 red IC, 1 blue IC
    int    guard = -1;               // guard gauge 0..360 (-1 = leave as-is)
    bool   rfLock = false;
    std::map<std::string, int> resources;
};

struct DummySetup {
    std::string character;           // internal short name; empty = keep current
    bool   hasPos = false;
    double posX = 0.0, posY = 0.0;
    int    palette = 0;
    int    rf = -1;
    int    meter = -1;
    int    hp = -1;
    int    blueIC = -1;
    int    guard = -1;               // guard gauge 0..360 (-1 = leave as-is)
    bool   rfLock = false;
    bool   crouch = false;
    bool   jump = false;
    std::string airtech = "none";    // none / forward / back
    std::map<std::string, int> resources;
};

struct ScoreTier {
    int         minHits = 0;         // 0 = ignore
    int         minDamage = 0;       // 0 = ignore
    int         maxAttempts = 0;     // 0 = ignore (attempts allowed to earn this rank)
    std::string label;
};

struct Mission {
    int         format = 1;
    std::string id;
    std::string type = "combo";
    std::string category;            // browser topic (tutorial: start/movement/offense/systems)
    int         difficulty = 0;      // 0 unspecified, otherwise 1..5 browser rating
    int         order = 0;           // authored course order (tutorial browser); 0 = file order
    std::string name;
    std::string description;
    PlayerSetup player;
    DummySetup  dummy;
    int         stage = -1;          // -1 = default / keep
    int         bgm = -1;            // BGM track for hotswap load (-1 = default)
    std::vector<Step>        steps;
    bool        strictEntityContacts = false;
    std::vector<EntityContactRequirement> entityContacts;
    std::vector<EntityLifecycleRequirement> entityLifecycles;
    std::string demo;                // EFZMACRO text (MacroController format)
    // Embedded Revival savestate (base64, Mission::StateDump). Empty = value-level setup only.
    std::string savestate;
    int         failTimer = 60;      // non-frozen 192 Hz ticks of no progress before the combo is a drop
    std::vector<ScoreTier>   scores; // [0] = base clear; [1..] = ranks
    std::vector<std::string> hints;
    std::vector<std::string> reviewRequired;
    std::vector<std::string> recordingDiagnostics;

    int         tutorialSchema = 0;
    std::string lessonId;            // stable id ("id"); globally stable in the pack
    int         revision = 1;
    std::string summary;
    std::vector<std::string> sourceRefs;
    std::vector<std::string> requires;   // exhaustive capability list
    bool        requiresExactBaseline = false;
    bool        hasLesson = false;
    Lesson      lesson;
    std::string nextLessonId;
    std::vector<std::string> recommendedAfter;

    std::string sourcePath;          // runtime-only: file this was loaded from
};

bool ValidateLesson(const Mission& m, std::string& errorOut);

struct Scenario {
    std::string name;
    std::string file;                // mission json filename, relative to the pack folder
    std::string description;
    std::string category;            // stable PackCategory id (mirrored by Mission::category)
    std::string preview;
    bool        locked = false;      // progression gate (Trials only; invalid for tutorials)
    std::string id;                  // stable lesson id (must match the file's "id")
    int         order = 0;           // explicit global order (duplicates = lint error)
    int         difficulty = 0;
    std::string next;
    std::vector<std::string> recommendedAfter;
};

struct PackCategory {
    std::string id;
    std::string label;
    std::string description;
    int         order = 0;
};

struct Pack {
    int         format = 1;
    std::string id;                  // stable pack id (e.g. "efz.core_tutorial")
    std::string name;
    std::string author;
    std::string version;
    std::string character;
    std::string description;
    bool        editable = false;    // created by the in-game local authoring flow
    int         curriculumRevision = 0;
    std::vector<PackCategory> categories;
    std::vector<Scenario> scenarios;

    std::string folderPath;          // runtime-only: the pack folder
};

bool LoadPack   (const std::string& packJsonPath,    Pack&    out, std::string& errorOut);
bool LoadMission(const std::string& missionJsonPath, Mission& out, std::string& errorOut);
bool SaveMission(const std::string& missionJsonPath, const Mission& mission, std::string& errorOut);
bool SavePack   (const std::string& packJsonPath,    const Pack& pack,       std::string& errorOut);

// Upgrades in memory only; never changes the file on disk. True when a schedule was upgraded.
bool NormalizeFlexibleEntityFanoutEpisodes(Mission& mission,
                                            bool allowInference = false);

std::vector<std::string> DiscoverPackJsonPaths(const std::string& rootDir);

std::string ResolveMissionsRoot();

}
