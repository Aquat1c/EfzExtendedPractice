#pragma once

#include <cstdint>
#include <string>

namespace CustomSavestate {

enum class BackendMode : int {
    Custom = 0,
    Revival = 1,
    CustomWithRevivalFallback = 2,
};

enum class HotkeyHandleResult : int {
    NotHandled = 0,
    Consumed = 1,
    UseRevivalFallback = 2,
};

struct EditableFields {
    int p1Hp = 0;
    int p2Hp = 0;
    int p1Meter = 0;
    int p2Meter = 0;
    int p1CpuFlag = 0;
    int p2CpuFlag = 1;
    int localSide = 0;
    double p1Rf = 0.0;
    double p2Rf = 0.0;
    double p1X = 0.0;
    double p1Y = 0.0;
    double p2X = 0.0;
    double p2Y = 0.0;
    double p1XVel = 0.0;
    double p1YVel = 0.0;
    double p2XVel = 0.0;
    double p2YVel = 0.0;
};

struct Summary {
    bool installed = false;
    bool hasWorkingSnapshot = false;
    bool workingSnapshotDirty = false;
    bool currentPairCompatible = false;
    bool currentStageCompatible = false;
    bool currentVersionCompatible = false;
    bool currentRestoreAllowed = false;
    uint8_t savedP1CharId = 0xFF;
    uint8_t savedP2CharId = 0xFF;
    uint8_t savedStageId = 0xFF;
    uint8_t savedP1CpuFlag = 0;
    uint8_t savedP2CpuFlag = 0;
    int savedLocalSide = 0;
    unsigned int savedBgmTrack = 0;
    unsigned int saveCount = 0;
    unsigned int loadCount = 0;
    unsigned int savedRevivalVersion = 0;
    unsigned int savedP1StateSize = 0;
    unsigned int savedP2StateSize = 0;
    unsigned int savedBattleContextSize = 0;
    unsigned int savedGameStateSize = 0;
    unsigned int savedRenderBitmapSize = 0;
    unsigned int workingSnapshotStamp = 0;
};

bool Install();
void Uninstall();
bool IsInstalled();
unsigned int GetSaveCount();
unsigned int GetLoadCount();

BackendMode GetConfiguredBackendMode();
const char* BackendModeName(BackendMode mode);
HotkeyHandleResult HandlePracticeHotkey(void* practiceController, int keyCode);

bool CapturePracticeEntrySnapshot();
bool CaptureWorkingSnapshot();
bool RestoreWorkingSnapshot();
bool ProcessQueuedRestoreAtFrameBoundary(bool inPracticeMatch, bool charactersInitialized);
void TickDeferredPracticeSideRestore(bool charactersInitialized);
bool IsRestoreInProgress();
bool ConsumePostRestoreStabilizationFrame();
bool QueueWorkingSnapshotRestoreAfterHotswap();
bool ConsumeHotswapRestoreApplied();
bool SaveSelectedSlot();
bool LoadSelectedSlot();
bool SaveWorkingSnapshotToDisk(int slot);
bool LoadWorkingSnapshotFromDisk(int slot);
void ClearWorkingSnapshot();

bool HasWorkingSnapshot();
bool GetSummary(Summary& outSummary);
bool GetWorkingEditableFields(EditableFields& outFields);
bool SetWorkingEditableFields(const EditableFields& fields);

int GetActiveDiskSlot();
void SetActiveDiskSlot(int slot);
int CycleActiveDiskSlot(int delta);
bool DoesDiskSlotExist(int slot);
std::string GetDiskSlotPath(int slot);
std::string GetLastStatus();

}