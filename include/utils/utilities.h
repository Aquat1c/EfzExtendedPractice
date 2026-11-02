#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <chrono>
#include <cstdint>
#include "../runtime/practice_contract.h"

namespace Practice {
enum class ResetReason : uint32_t {
    BeginWorld, NativeRestoreSucceeded, ExplicitPracticeReset,
    RetireWorld, LeavePractice
};
void ResetPracticeMeasurements(const EfzTmIdentityV1& identity, ResetReason reason);
bool CapturePracticeInputBaseline(const EfzTmEntryV1& world);
void CancelPracticeInputWork();
uint32_t RetirePracticeInput(const EfzTmEntryV1& world, bool oldWorldHeld);
}

extern std::atomic<bool> menuOpen;
extern std::atomic<int> frameCounter;
extern std::atomic<bool> detailedLogging;
extern std::atomic<bool> g_deepFrameAdvDebug;
extern std::atomic<bool> autoAirtechEnabled;
extern std::atomic<int> autoAirtechDirection; // 0=forward, 1=backward
extern std::atomic<bool> autoJumpEnabled;
extern std::atomic<int> jumpDirection;        // 0=straight, 1=forward, 2=backward
extern std::atomic<bool> p1Jumping;
extern std::atomic<bool> p2Jumping;
extern std::atomic<int> jumpTarget;           // 1=P1, 2=P2, 3=Both
extern std::atomic<bool> inStartupPhase;
extern std::atomic<bool> g_featuresEnabled;

extern std::atomic<bool> autoActionEnabled;
extern std::atomic<int> autoActionType;
extern std::atomic<int> autoActionCustomID;
extern std::atomic<int> autoActionPlayer;
                                           // write-only outside the tutorial lease (engine re-resolves every tick); 3 (Both) never produced.

extern std::atomic<bool> triggerAfterBlockEnabled;
extern std::atomic<bool> triggerOnWakeupEnabled;
extern std::atomic<bool> triggerAfterHitstunEnabled;
extern std::atomic<bool> triggerAfterAirtechEnabled;
extern std::atomic<bool> triggerOnRGEnabled;
extern std::atomic<bool> triggerRandomizeEnabled;

// Delay settings (visual frames)
extern std::atomic<int> triggerAfterBlockDelay;
extern std::atomic<int> triggerOnWakeupDelay;
extern std::atomic<int> triggerAfterHitstunDelay;
extern std::atomic<int> triggerAfterAirtechDelay;
extern std::atomic<int> triggerOnRGDelay;

uintptr_t GetEFZBase();
void InvalidateEFZBaseCache();

uintptr_t GetGameStatePtr();
void InvalidateGameStatePtrCache();
uint32_t GetRuntimeLifecycleGeneration();
void RequestRuntimeLifecycleResync(const std::string& reason);
void ConsumeRuntimeLifecycleResyncRequests();
void LifecycleWatcherThread();

// Cached player base pointers (reinitialized on each character load); 0 if not in battle.
uintptr_t GetPlayerBase(int playerIndex);
void InvalidatePlayerBaseCache();
bool IsActionable(short moveID);
bool IsBlockstun(short moveID);
bool IsRecoilGuard(short moveID);
bool IsEFZWindowActive();
HWND FindEFZWindow();
void CreateDebugConsole();
void DestroyDebugConsole();
void SetConsoleVisibility(bool visible);
void ResetFrameCounter();
unsigned int GetDisplayedFrameCounter();
void ShowHotkeyInfo();
std::string FormatPosition(double x, double y);
bool IsHitstun(short moveID);
bool IsLaunched(short moveID);
bool IsAirtech(short moveID);
bool IsGroundtech(short moveID);
bool IsFrozen(short moveID);
bool IsSpecialStun(short moveID);
bool IsThrown(short moveID);

void ClearAllTriggersPersistently();

bool IsAttackMove(short moveID);
bool IsBlockstunState(short moveID);

short GetUntechValue(uintptr_t base, int player);
void WriteStartupLog(const std::string& message);
void SetStartupLogEnabled(bool enabled);          // Call after config loads
std::string GetKeyName(int virtualKey);
void DetectKeyBindings();
bool IsDashState(short moveID);
bool CanAirtech(short moveID); 

extern std::atomic<bool> g_featuresEnabled;
void EnableFeatures();
void DisableFeatures();
void ResetDisplayDataToDefaults();
void ResetPracticeMatchSessionState(const char* reason);

extern std::atomic<int> autoAirtechDelay; // 0=instant, 1+=frames to wait

extern std::atomic<bool> g_injectImmediateOnly[3]; // Index 0 unused, 1=P1, 2=P2

#ifndef MAX_TRIGGER_OPTIONS
#define MAX_TRIGGER_OPTIONS 8
#endif

#ifndef MAX_ACTION_POOL_OPTIONS
#define MAX_ACTION_POOL_OPTIONS 128
#endif

struct TriggerOption {
    bool enabled;
    int  action;
    int  strength;    // 0=A, 1=B, 2=C, 3=D, or direction index for Jump
    int  delay;       // visual frames (0 = immediate)
    int  customId;
    int  macroSlot;   // 0=None, 1..MaxSlots
    int  chargeFollowup; // 0=Off, 1=IC after contact, 2=FIC before contact
};
struct DisplayData {
    int hp1, hp2;
    int meter1, meter2;
    double rf1, rf2;
    double x1, y1;
    double x2, y2;
    bool autoAirtech;
    int airtechDirection;
    int airtechDelay;
    bool autoJump;
    int jumpDirection;
    int jumpTarget;
    
    char p1CharName[16];
    char p2CharName[16];
    
    int p1CharID;
    int p2CharID;
    
    int p1IkumiBlood;
    int p2IkumiBlood;
    int p1IkumiGenocide;
    int p2IkumiGenocide;
    int p1IkumiLevelGauge; // 0..99 (100 triggers level up)
    int p2IkumiLevelGauge;
    bool infiniteBloodMode;

    // Shiori (reuses Ikumi's per-character resource slot, player + 0x314C)
    bool infiniteShioriShield;

    int p1MisuzuFeathers;
    int p2MisuzuFeathers;
    bool infiniteFeatherMode;
    int  p1MisuzuPoisonTimer; // 0..3000
    int  p2MisuzuPoisonTimer; // 0..3000
    int  p1MisuzuPoisonLevel; // 0=inactive, nonzero=active
    int  p2MisuzuPoisonLevel; // 0=inactive, nonzero=active
    bool p1MisuzuInfinitePoison;
    bool p2MisuzuInfinitePoison;
    
    int p1MishioElement;       // 0=None, 1=Fire, 2=Lightning, 3=Awakened
    int p2MishioElement;
    int p1MishioAwakenedTimer; // internal frames
    int p2MishioAwakenedTimer;
    bool infiniteMishioElement;
    bool infiniteMishioAwakened;

    bool p1BlueIC;
    bool p2BlueIC;

    bool p2ControlEnabled;
    
    bool autoAction;
    int autoActionType;
    int autoActionCustomID;
    int autoActionPlayer;
    
    bool triggerAfterBlock;
    bool triggerOnWakeup;
    bool triggerAfterHitstun;
    bool triggerAfterAirtech;
    bool triggerOnRG;
    bool randomizeTriggers;
    
    int delayAfterBlock;
    int delayOnWakeup;
    int delayAfterHitstun;
    int delayAfterAirtech;
    int delayOnRG;
    
    int actionAfterBlock;
    int actionOnWakeup;
    int actionAfterHitstun;
    int actionAfterAirtech;
    int actionOnRG;
    
    int customAfterBlock;
    int customOnWakeup;
    int customAfterHitstun;
    int customAfterAirtech;
    int customOnRG;

    int strengthAfterBlock;
    int strengthOnWakeup;
    int strengthAfterHitstun;
    int strengthAfterAirtech;
    int strengthOnRG;

    int chargeAfterBlock;
    int chargeOnWakeup;
    int chargeAfterHitstun;
    int chargeAfterAirtech;
    int chargeOnRG;

    // Per-trigger macro selection (0=None, 1..MaxSlots)
    int macroSlotAfterBlock;
    int macroSlotOnWakeup;
    int macroSlotAfterHitstun;
    int macroSlotAfterAirtech;
    int macroSlotOnRG;

    bool p1DoppelEnlightened;
    bool p2DoppelEnlightened;

    // Doppel Nanase: how the OPPONENT escapes her command-throw follow-ups (lives on Doppel's struct).
    // Mode: 0=OFF, 1=NEVER, 2=TECH B, 3=TECH C, 4=ALWAYS, 5=RANDOM
    int p1DoppelTechMode;
    int p2DoppelTechMode;
    // Stage: 0=ALL, 1=STAGE 1, 2=STAGE 2, 3=STAGE 3
    int p1DoppelTechStage;
    int p2DoppelTechStage;

    // Sayuri memory choice: 0=OFF, 1=NOTHING, 2=LAST BLOCKED, 3+ = one move of the current opponent.
    // Presentation only: the resolved move ID lives in SayuriCounter, the list is rebuilt per opponent.
    int p1SayuriMemoryChoice;
    int p2SayuriMemoryChoice;
    // Cutter: 0=NORMAL, 1=ALWAYS READY
    int p1SayuriCutterMode;
    int p2SayuriCutterMode;
    int p1SayuriAutoCutter;
    int p2SayuriAutoCutter;
    // AUTO CUTTER reaction delay in visual frames (0 = earliest legal press)
    int p1SayuriAutoCutterDelay;
    int p2SayuriAutoCutterDelay;

    bool p1RumiBarehanded;
    bool p2RumiBarehanded;

    bool p1RumiInfiniteShinai;
    bool p2RumiInfiniteShinai;

    bool p1RumiKimchiActive;   // reflects activation flag at +0x3148
    bool p2RumiKimchiActive;
    int  p1RumiKimchiTimer;    // reflects timer at +0x314C
    int  p2RumiKimchiTimer;
    bool p1RumiInfiniteKimchi;
    bool p2RumiInfiniteKimchi;

    int  p1AkikoBulletCycle;
    int  p2AkikoBulletCycle;
    int  p1AkikoTimeslowTrigger; // 0=inactive, 1=A, 2=B, 3=C
    int  p2AkikoTimeslowTrigger;
    bool p1AkikoFreezeCycle;
    bool p2AkikoFreezeCycle;
    bool p1AkikoShowCleanHit;
    bool p2AkikoShowCleanHit;
    // Infinite timeslow is the TimerFreeze::AkikoTimeslow code patch; no per-frame digit writes.
    bool p1AkikoInfiniteTimeslow;
    bool p2AkikoInfiniteTimeslow;
    bool p1AkikoFreezeCurse;
    bool p2AkikoFreezeCurse;
    bool p1MizukaFreezeFm;
    bool p2MizukaFreezeFm;

    // Neyuki (Sleepy Nayuki) – Jam count (0..9)
    int  p1NeyukiJamCount;
    int  p2NeyukiJamCount;
    bool p1NeyukiLockJam;
    bool p2NeyukiLockJam;

    // Mio – stance control (0=Short,1=Long) and optional lock
    int  p1MioStance;
    int  p2MioStance;
    bool p1MioLockStance;
    bool p2MioLockStance;

    // Kano – magic meter (0..10000) and locking
    int  p1KanoMagic;
    int  p2KanoMagic;
    bool p1KanoLockMagic;
    bool p2KanoLockMagic;

    int  p1MaiStatus;        // status byte (0=inactive,1=active,2=unsummon,3=charge,4=awakening)
    int  p1MaiGhostTime;
    int  p1MaiGhostCharge;
    int  p1MaiAwakeningTime;
    int  p2MaiStatus;
    int  p2MaiGhostTime;
    int  p2MaiGhostCharge;
    int  p2MaiAwakeningTime;
    bool p1MaiInfiniteGhost;
    bool p2MaiInfiniteGhost;
    bool p1MaiInfiniteCharge;
    bool p2MaiInfiniteCharge;
    bool p1MaiInfiniteAwakening;
    bool p2MaiInfiniteAwakening;
    bool p1MaiNoChargeCD;
    bool p2MaiNoChargeCD;
    bool p1MaiForceSummon;
    bool p2MaiForceSummon;
    bool p1MaiForceDespawn;
    bool p2MaiForceDespawn;
    bool p1MaiAggressiveOverride;
    bool p2MaiAggressiveOverride;

    double p1MaiGhostX; // NaN if not present
    double p1MaiGhostY;
    double p2MaiGhostX; // NaN if not present
    double p2MaiGhostY;
    double p1MaiGhostSetX;
    double p1MaiGhostSetY;
    double p2MaiGhostSetX;
    double p2MaiGhostSetY;
    bool   p1MaiApplyGhostPos;
    bool   p2MaiApplyGhostPos;

    // Nayuki (Awake): Snowbunnies timer at +0x3150
    int  p1NayukiSnowbunnies;
    int  p2NayukiSnowbunnies;
    bool p1NayukiInfiniteSnow;
    bool p2NayukiInfiniteSnow;

    double p1MinagiPuppetX; // NaN if not present
    double p1MinagiPuppetY;
    double p2MinagiPuppetX; // NaN if not present
    double p2MinagiPuppetY;

    double p1MinagiPuppetSetX;
    double p1MinagiPuppetSetY;
    double p2MinagiPuppetSetX;
    double p2MinagiPuppetSetY;

    bool   p1MinagiApplyPos;
    bool   p2MinagiApplyPos;

    bool  minagiConvertNewProjectiles;
    bool  p1MinagiAlwaysReadied;
    bool  p2MinagiAlwaysReadied;

    int   p1MichiruCurrentId;          // current entity id in cached slot (or -1)
    int   p2MichiruCurrentId;
    double p1MichiruLastX;
    double p1MichiruLastY;
    double p2MichiruLastX;
    double p2MichiruLastY;
    int   p1MichiruFrame;
    int   p1MichiruSubframe;
    int   p2MichiruFrame;
    int   p2MichiruSubframe;

    bool  continuousRecoveryEnabled;
    int   continuousRecoveryApplyTo; // 1=P1, 2=P2, 3=Both
    int   recoveryHpMode;            // 0=Off, 1=Max, 2=FM preset (3332), 3=Custom
    int   recoveryHpCustom;
    int   recoveryMeterMode;         // 0=Off, 1=0, 2=1000, 3=2000, 4=3000, 5=Custom
    int   recoveryMeterCustom;
    int   recoveryRfMode;            // 0=Off, 1=0, 2=1000, 3=500, 4=999, 5=Custom
    double  recoveryRfCustom;
    bool  recoveryRfForceBlueIC;

    bool  p1ContinuousRecoveryEnabled;
    int   p1RecoveryHpMode;            // 0=Off, 1=Max, 2=FM(3332), 3=Custom
    int   p1RecoveryHpCustom;
    int   p1RecoveryMeterMode;         // 0=Off, 1=0, 2=1000, 3=2000, 4=3000, 5=Custom
    int   p1RecoveryMeterCustom;
    int   p1RecoveryRfMode;            // 0=Off, 1=0, 2=1000, 3=500, 4=999, 5=Custom
    double p1RecoveryRfCustom;
    bool  p1RecoveryRfForceBlueIC;
    bool  p2ContinuousRecoveryEnabled;
    int   p2RecoveryHpMode;
    int   p2RecoveryHpCustom;
    int   p2RecoveryMeterMode;
    int   p2RecoveryMeterCustom;
    int   p2RecoveryRfMode;
    double p2RecoveryRfCustom;
    bool  p2RecoveryRfForceBlueIC;

    // Old multi-action pools: bitmask over the old grouped UI motion indices (0..23).
    // Kept only to import older runtime state into the concrete pools below.
    uint32_t afterBlockActionPoolMask;
    uint32_t onWakeupActionPoolMask;
    uint32_t afterHitstunActionPoolMask;
    uint32_t afterAirtechActionPoolMask;
    uint32_t onRGActionPoolMask;

    // Concrete multi-action pools per trigger: low/high form a 128-bit mask over action+variant
    // entries. 623A and 623B are separate bits, normals use ACTION_* ids, jumps store direction.
    uint64_t afterBlockActionPoolMaskLo;
    uint64_t afterBlockActionPoolMaskHi;
    uint64_t onWakeupActionPoolMaskLo;
    uint64_t onWakeupActionPoolMaskHi;
    uint64_t afterHitstunActionPoolMaskLo;
    uint64_t afterHitstunActionPoolMaskHi;
    uint64_t afterAirtechActionPoolMaskLo;
    uint64_t afterAirtechActionPoolMaskHi;
    uint64_t onRGActionPoolMaskLo;
    uint64_t onRGActionPoolMaskHi;

    // Per concrete pool action delays. -1 means inherit the trigger's normal delay.
    int afterBlockActionPoolDelays[MAX_ACTION_POOL_OPTIONS];
    int onWakeupActionPoolDelays[MAX_ACTION_POOL_OPTIONS];
    int afterHitstunActionPoolDelays[MAX_ACTION_POOL_OPTIONS];
    int afterAirtechActionPoolDelays[MAX_ACTION_POOL_OPTIONS];
    int onRGActionPoolDelays[MAX_ACTION_POOL_OPTIONS];

    // Per concrete pool action charge mode (0=Off, 1=IC, 2=FIC).
    int afterBlockActionPoolCharges[MAX_ACTION_POOL_OPTIONS];
    int onWakeupActionPoolCharges[MAX_ACTION_POOL_OPTIONS];
    int afterHitstunActionPoolCharges[MAX_ACTION_POOL_OPTIONS];
    int afterAirtechActionPoolCharges[MAX_ACTION_POOL_OPTIONS];
    int onRGActionPoolCharges[MAX_ACTION_POOL_OPTIONS];

    bool     afterBlockUseActionPool;
    bool     onWakeupUseActionPool;
    bool     afterHitstunUseActionPool;
    bool     afterAirtechUseActionPool;
    bool     onRGUseActionPool;

    int           afterBlockOptionCount;
    TriggerOption afterBlockOptions[MAX_TRIGGER_OPTIONS];
    int           onWakeupOptionCount;
    TriggerOption onWakeupOptions[MAX_TRIGGER_OPTIONS];
    int           afterHitstunOptionCount;
    TriggerOption afterHitstunOptions[MAX_TRIGGER_OPTIONS];
    int           afterAirtechOptionCount;
    TriggerOption afterAirtechOptions[MAX_TRIGGER_OPTIONS];
    int           onRGOptionCount;
    TriggerOption onRGOptions[MAX_TRIGGER_OPTIONS];
};

extern DisplayData displayData;

bool HasAnyAutoActionTriggerEnabled();
bool HasAnyAutoActionTriggerEnabled(const DisplayData& data);
int ResolveAutoActionTargetPlayer();

struct KeyBindings {
    int inputDevice;      // 0=keyboard, 1=gamepad
    int gamepadIndex;
    std::string deviceName;
    
    int upKey;
    int downKey;
    int leftKey;
    int rightKey;
    
    int aButton;
    int bButton;
    int cButton;
    int dButton;
    
    bool directionsDetected;
    bool attacksDetected;
};

extern KeyBindings detectedBindings;

extern std::atomic<int> triggerAfterBlockAction;
extern std::atomic<int> triggerOnWakeupAction;
extern std::atomic<int> triggerAfterHitstunAction;
extern std::atomic<int> triggerAfterAirtechAction;
extern std::atomic<int> triggerOnRGAction;

extern std::atomic<int> triggerAfterBlockCharge;
extern std::atomic<int> triggerOnWakeupCharge;
extern std::atomic<int> triggerAfterHitstunCharge;
extern std::atomic<int> triggerAfterAirtechCharge;
extern std::atomic<int> triggerOnRGCharge;

extern std::atomic<uint32_t> triggerAfterBlockActionPoolMask;
extern std::atomic<uint32_t> triggerOnWakeupActionPoolMask;
extern std::atomic<uint32_t> triggerAfterHitstunActionPoolMask;
extern std::atomic<uint32_t> triggerAfterAirtechActionPoolMask;
extern std::atomic<uint32_t> triggerOnRGActionPoolMask;

extern std::atomic<uint64_t> triggerAfterBlockActionPoolMaskLo;
extern std::atomic<uint64_t> triggerAfterBlockActionPoolMaskHi;
extern std::atomic<uint64_t> triggerOnWakeupActionPoolMaskLo;
extern std::atomic<uint64_t> triggerOnWakeupActionPoolMaskHi;
extern std::atomic<uint64_t> triggerAfterHitstunActionPoolMaskLo;
extern std::atomic<uint64_t> triggerAfterHitstunActionPoolMaskHi;
extern std::atomic<uint64_t> triggerAfterAirtechActionPoolMaskLo;
extern std::atomic<uint64_t> triggerAfterAirtechActionPoolMaskHi;
extern std::atomic<uint64_t> triggerOnRGActionPoolMaskLo;
extern std::atomic<uint64_t> triggerOnRGActionPoolMaskHi;

extern int g_afterBlockActionPoolDelays[MAX_ACTION_POOL_OPTIONS];
extern int g_onWakeupActionPoolDelays[MAX_ACTION_POOL_OPTIONS];
extern int g_afterHitstunActionPoolDelays[MAX_ACTION_POOL_OPTIONS];
extern int g_afterAirtechActionPoolDelays[MAX_ACTION_POOL_OPTIONS];
extern int g_onRGActionPoolDelays[MAX_ACTION_POOL_OPTIONS];

extern int g_afterBlockActionPoolCharges[MAX_ACTION_POOL_OPTIONS];
extern int g_onWakeupActionPoolCharges[MAX_ACTION_POOL_OPTIONS];
extern int g_afterHitstunActionPoolCharges[MAX_ACTION_POOL_OPTIONS];
extern int g_afterAirtechActionPoolCharges[MAX_ACTION_POOL_OPTIONS];
extern int g_onRGActionPoolCharges[MAX_ACTION_POOL_OPTIONS];

extern std::atomic<bool>     triggerAfterBlockUsePool;
extern std::atomic<bool>     triggerOnWakeupUsePool;
extern std::atomic<bool>     triggerAfterHitstunUsePool;
extern std::atomic<bool>     triggerAfterAirtechUsePool;
extern std::atomic<bool>     triggerOnRGUsePool;

extern int           g_afterBlockOptionCount;
extern TriggerOption g_afterBlockOptions[MAX_TRIGGER_OPTIONS];
extern int           g_onWakeupOptionCount;
extern TriggerOption g_onWakeupOptions[MAX_TRIGGER_OPTIONS];
extern int           g_afterHitstunOptionCount;
extern TriggerOption g_afterHitstunOptions[MAX_TRIGGER_OPTIONS];
extern int           g_afterAirtechOptionCount;
extern TriggerOption g_afterAirtechOptions[MAX_TRIGGER_OPTIONS];
extern int           g_onRGOptionCount;
extern TriggerOption g_onRGOptions[MAX_TRIGGER_OPTIONS];

// Forward dash follow-up (0=None, 1=5A,2=5B,3=5C,4=2A,5=2B,6=2C)
extern std::atomic<int> forwardDashFollowup;
extern std::atomic<bool> forwardDashFollowupDashMode;

extern std::atomic<int> triggerAfterBlockCustomID;
extern std::atomic<int> triggerOnWakeupCustomID;
extern std::atomic<int> triggerAfterHitstunCustomID;
extern std::atomic<int> triggerAfterAirtechCustomID;
extern std::atomic<int> triggerOnRGCustomID;

#define DEFAULT_TRIGGER_DELAY 0

extern std::atomic<bool> g_efzWindowActive;
extern std::atomic<bool> g_guiActive;
extern std::atomic<bool> g_onlineModeActive;

void EnterNetplaySuspend();
void ExitNetplaySuspend();
void AuditNetplayMenuEntryState();

extern std::atomic<bool> g_manualInputOverride[3]; // Index 0 unused, 1 for P1, 2 for P2
extern std::atomic<uint8_t> g_manualInputMask[3];
extern std::atomic<bool> g_manualJumpHold[3];

void UpdateWindowActiveState();

extern std::atomic<bool> g_statsDisplayEnabled;
extern std::atomic<int> g_statsPageIndex;
extern std::atomic<int> g_statsPageCount;
extern int g_statsP1ValuesId;
extern int g_statsP2ValuesId;
extern int g_statsPositionId;
extern int g_statsMoveIdId;
extern int g_statsCleanHitId;
extern int g_statsNayukiId;
extern int g_statsMisuzuId;
extern int g_statsMishioId;
extern int g_statsRumiId;
extern int g_statsIkumiId;
extern int g_statsMaiId;
extern int g_statsMinagiId;
extern int g_statsAIFlagsId;
extern int g_statsBlockstunId; // Blockstun (logical frames)
extern int g_statsUntechId;    // Hitstun/Untech (logical frames)

void ManageKeyMonitoring();

// Individual strength settings (0=A, 1=B, 2=C, 3=D)
extern std::atomic<int> triggerAfterBlockStrength;
extern std::atomic<int> triggerOnWakeupStrength;
extern std::atomic<int> triggerAfterHitstunStrength;
extern std::atomic<int> triggerAfterAirtechStrength;
extern std::atomic<int> triggerOnRGStrength;

// Per-trigger macro slot selections (0=None, 1..MaxSlots)
extern std::atomic<int> triggerAfterBlockMacroSlot;
extern std::atomic<int> triggerOnWakeupMacroSlot;
extern std::atomic<int> triggerAfterHitstunMacroSlot;
extern std::atomic<int> triggerAfterAirtechMacroSlot;
extern std::atomic<int> triggerOnRGMacroSlot;

extern std::atomic<bool> g_wakeBufferingEnabled;

extern std::atomic<bool> g_showFrameAdvantageOverlay;

extern std::atomic<bool> g_counterRGEnabled;

struct AttackData {
    // Offset 0x38: attack type flags
    int attackType;  
    
    // Offset 0x3C: Active frames
    short activeFrameStart;
    short activeFrameEnd;
    
    int damage;
    int blockstun;
    int hitstun;
};

void UpdatePositionCache(double p1X, double p1Y, double p2X, double p2Y);
bool TryGetCachedYPositions(double &p1Y, double &p2Y, unsigned int maxAgeMs);

extern std::atomic<bool> g_contRecoveryEnabled;
extern std::atomic<int>  g_contRecoveryApplyTo;       // 1=P1,2=P2,3=Both (legacy)
extern std::atomic<int>  g_contRecHpMode;
extern std::atomic<int>  g_contRecHpCustom;
extern std::atomic<int>  g_contRecMeterMode;
extern std::atomic<int>  g_contRecMeterCustom;
extern std::atomic<int>  g_contRecRfMode;
extern std::atomic<double> g_contRecRfCustom;
extern std::atomic<bool> g_contRecRfForceBlueIC;

extern std::atomic<bool> g_contRecEnabledP1;
extern std::atomic<int>  g_contRecHpModeP1;
extern std::atomic<int>  g_contRecHpCustomP1;
extern std::atomic<int>  g_contRecMeterModeP1;
extern std::atomic<int>  g_contRecMeterCustomP1;
extern std::atomic<int>  g_contRecRfModeP1;
extern std::atomic<double> g_contRecRfCustomP1;
extern std::atomic<bool> g_contRecRfForceBlueICP1;
extern std::atomic<bool> g_contRecEnabledP2;
extern std::atomic<int>  g_contRecHpModeP2;
extern std::atomic<int>  g_contRecHpCustomP2;
extern std::atomic<int>  g_contRecMeterModeP2;
extern std::atomic<int>  g_contRecMeterCustomP2;
extern std::atomic<int>  g_contRecRfModeP2;
extern std::atomic<double> g_contRecRfCustomP2;
extern std::atomic<bool> g_contRecRfForceBlueICP2;
