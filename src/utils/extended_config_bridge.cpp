#include "../include/utils/extended_config_bridge.h"

#include "../include/core/logger.h"
#include "../include/utils/config.h"
#include "../include/utils/audio_runtime_state.h"
#include "../include/utils/audio_control.h"
#include "../include/utils/audio_file_control.h"
#include <mutex>

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace ExtendedConfigBridge {
namespace {

std::recursive_mutex g_controlMutex;
bool g_admitted = false;
HANDLE g_stop = nullptr, g_thread = nullptr, g_change = INVALID_HANDLE_VALUE;
HMODULE g_providerModule = nullptr;
const EfzAudioOwnerApiV1* g_provider = nullptr;

Status g_status{};


int ClampPercent(int value) {
    return std::clamp(value, 0, 100);
}

bool FileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
}

std::string ModulePath(HMODULE module) {
    char path[MAX_PATH] = {0};
    const DWORD len = GetModuleFileNameA(module, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return path;
}

bool FindLoadedModule(Status& status) {
    static const char* const kModuleNames[] = {
        "EFZConfigExtension.dll",
        "efz_config_extension.dll",
        "EFZ_Config_Extension.dll",
        "EFZExtendedConfig.dll",
        "ExtendedConfig.dll",
        "InGameControlsRebind.dll",
    };

    for (const char* name : kModuleNames) {
        if (HMODULE module = GetModuleHandleA(name)) {
            status.modLoaded = true;
            status.moduleName = name;
            status.modulePath = ModulePath(module);
            return true;
        }
    }
    return false;
}

void AddPath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    for (const auto& existing : paths) {
        if (_stricmp(existing.string().c_str(), path.string().c_str()) == 0) {
            return;
        }
    }
    paths.push_back(path);
}

std::filesystem::path DefaultSharedPathForModule(const Status& status) {
    if (!status.modulePath.empty()) {
        return std::filesystem::path(status.modulePath).parent_path() / "efz_extended_config.ini";
    }
    return {};
}

bool FindSharedConfigPath(Status& status) {
    static const char* const kFileNames[] = {
        "efz_extended_config.ini",
        "EFZExtendedConfig.ini",
        "EFZConfigExtension.ini",
        "extended_config.ini",
        "InGameControlsRebind.ini",
    };

    std::vector<std::filesystem::path> dirs;
    if (!status.modulePath.empty()) {
        AddPath(dirs, std::filesystem::path(status.modulePath).parent_path());
    }

    char currentDir[MAX_PATH] = {0};
    if (GetCurrentDirectoryA(MAX_PATH, currentDir) > 0) {
        const std::filesystem::path gameDir(currentDir);
        AddPath(dirs, gameDir);
        AddPath(dirs, gameDir / "mods" / "EFZConfigExtension");
        AddPath(dirs, gameDir / "mods" / "efz_config_extension");
        AddPath(dirs, gameDir / "mods" / "ExtendedConfig");
        AddPath(dirs, gameDir / "mods" / "InGameControlsRebind");
    }

    for (const auto& dir : dirs) {
        for (const char* name : kFileNames) {
            const auto candidate = dir / name;
            if (FileExists(candidate)) {
                status.sharedConfigFound = true;
                status.sharedConfigPath = candidate.string();
                return true;
            }
        }
    }

    if (status.modLoaded) {
        const auto defaultPath = DefaultSharedPathForModule(status);
        if (!defaultPath.empty()) {
            status.sharedConfigPath = defaultPath.string();
        }
    }
    return false;
}

int ReadIniInt(const char* section, const char* key, int fallback, const std::string& path) {
    return GetPrivateProfileIntA(section, key, fallback, path.c_str());
}

void ReadSharedConfig(Status& status) {
    if (status.sharedConfigPath.empty() || !FileExists(status.sharedConfigPath)) {
        return;
    }

    status.sharedConfigFound = true;
    status.version = ReadIniInt("Shared", "version", 0, status.sharedConfigPath);

    const bool sharedEnabled = ReadIniInt("Shared", "enabled", 1, status.sharedConfigPath) != 0;
    const bool audioEnabled =
        ReadIniInt("Shared", "audio", ReadIniInt("Audio", "enabled", 0, status.sharedConfigPath), status.sharedConfigPath) != 0;
    const bool controlsEnabled =
        ReadIniInt("Shared", "controls", ReadIniInt("Controls", "enabled", 0, status.sharedConfigPath), status.sharedConfigPath) != 0;

    const int bgm = ReadIniInt("Audio", "bgmVolumePercent", -1, status.sharedConfigPath);
    const int se = ReadIniInt("Audio", "seVolumePercent", -1, status.sharedConfigPath);

    status.bgmVolumeAvailable = bgm >= 0 && bgm <= 100;
    status.seVolumeAvailable = se >= 0 && se <= 100;
    if (status.bgmVolumeAvailable) {
        status.bgmVolumePercent = ClampPercent(bgm);
    }
    if (status.seVolumeAvailable) {
        status.seVolumePercent = ClampPercent(se);
    }

    status.audioRevision = ReadIniInt("Audio", "revision", 0, status.sharedConfigPath);
    status.controlsRevision = ReadIniInt("Controls", "revision", 0, status.sharedConfigPath);
    status.audioShared = sharedEnabled && audioEnabled && (status.bgmVolumeAvailable || status.seVolumeAvailable);
    status.controlsShared = sharedEnabled && controlsEnabled;
}

bool WriteIniInt(const char* section, const char* key, int value, const std::string& path) {
    char buf[32];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d", value);
    return WritePrivateProfileStringA(section, key, buf, path.c_str()) != FALSE;
}

} // namespace

bool Refresh(bool force) {
    (void)force;
    std::lock_guard<std::recursive_mutex> lock(g_controlMutex);
    if (!g_admitted) return false;
    EfzAudioFileTransaction transaction;
    if (!transaction) return false;
    if (ReadIniInt("Audio", "writeInProgress", 0, g_status.sharedConfigPath) != 0) return false;
    Status next = g_status;
    next.sharedConfigFound = false;
    next.audioShared = false;
    next.bgmVolumeAvailable = next.seVolumeAvailable = false;
    ReadSharedConfig(next);
    if (ReadIniInt("Audio", "writeInProgress", 0, g_status.sharedConfigPath) != 0) return false;
    g_status = next;
    if (next.audioShared) {
        const auto previous = AudioControl::ReadAudioSettings();
        AudioControl::PublishAudioPercents(next.bgmVolumeAvailable ? next.bgmVolumePercent : previous.bgmPercent,
                                          next.seVolumeAvailable ? next.seVolumePercent : previous.sePercent);
    }
    return next.sharedConfigFound || next.modLoaded;
}

namespace {
DWORD WINAPI AudioNotificationWorker(LPVOID) {
    HANDLE handles[] = {g_stop, g_change};
    const DWORD count = g_change == INVALID_HANDLE_VALUE ? 1 : 2;
    for (;;) {
        const DWORD result = WaitForMultipleObjects(count, handles, FALSE, INFINITE);
        if (result != WAIT_OBJECT_0 + 1) break;
        // Rearm before reading: changes during import remain signalled.
        if (!FindNextChangeNotification(g_change)) break;
        const auto before = AudioControl::ReadAudioSettings();
        // Any file in the directory wakes this; only a changed gain reaches the mirror's consumers.
        if (ImportAudioSettingsIfAvailable(true)) {
            const auto after = AudioControl::ReadAudioSettings();
            if (after.bgmPercent != before.bgmPercent || after.sePercent != before.sePercent)
                AudioControl::ApplyConfiguredVolumesNow();
        }
    }
    return 0;
}
struct TransferContext { EfzAudioOwnerV1 expected; bool committed = false; };
void __cdecl CommitTransfer(const EfzAudioOwnerV1* record, void* opaque) {
    auto& context = *static_cast<TransferContext*>(opaque);
    if (record && record->providerIncarnation == context.expected.providerIncarnation &&
        record->acknowledgementGeneration == context.expected.acknowledgementGeneration + 1)
        context.committed = AudioControl::AcknowledgeAudioOwner(*record);
}
}

bool InitializeAudioControl() {
    std::lock_guard<std::recursive_mutex> lock(g_controlMutex);
    if (g_admitted) return true;
    FindLoadedModule(g_status);
    FindSharedConfigPath(g_status);
    if (g_status.sharedConfigPath.empty()) {
        char current[MAX_PATH]{};
        if (!GetCurrentDirectoryA(MAX_PATH, current)) return false;
        g_status.sharedConfigPath = (std::filesystem::path(current) / "efz_extended_config.ini").string();
    }
    if (g_status.modLoaded) {
        HMODULE module = GetModuleHandleA(g_status.moduleName.c_str());
        auto getApi = reinterpret_cast<EfzGetAudioOwnerApiV1>(GetProcAddress(module, "EfzGetAudioOwnerV1"));
        if (getApi && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCSTR>(getApi), &g_providerModule)) {
            const auto* api = getApi();
            if (api && api->size == sizeof(*api) && api->version == 1 && api->providerIncarnation &&
                api->enter && api->leave && api->query && api->retire) {
                EfzAudioOwnerV1 record{};
                if (api->query(&record) && record.providerIncarnation == api->providerIncarnation) {
                    g_provider = api;
                    AudioControl::BindAudioBoundary(api);
                    AudioControl::EnterAudioBoundary();
                    const bool acknowledged = AudioControl::AcknowledgeAudioOwner(record);
                    AudioControl::LeaveAudioBoundary();
                    if (!acknowledged) g_provider = nullptr;
                }
            }
        }
        if (!g_provider) {
            AudioControl::MarkIncompatibleAudioOwner(EfzAudioAllLanes);
            g_status.audioCompatibilityError = "ExtendedConfig runtime has no acknowledged raw/adjusted gain contract";
            LogOut("[AUDIO][OWNER] " + g_status.audioCompatibilityError, true);
        }
    }
    auto parent = std::filesystem::path(g_status.sharedConfigPath).parent_path();
    std::error_code ec;
    while (!parent.empty() && !std::filesystem::is_directory(parent, ec)) parent = parent.parent_path();
    if (!parent.empty()) g_change = FindFirstChangeNotificationA(parent.string().c_str(), FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
    g_stop = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    g_admitted = true;
    AudioControl::PublishAudioPercents(Config::GetSettings().bgmVolumePercent, Config::GetSettings().seVolumePercent);
    Refresh(true);
    if (g_stop) g_thread = CreateThread(nullptr, 0, AudioNotificationWorker, nullptr, 0, nullptr);
    if (!g_thread) LogOut("[AUDIO] audio directory notification worker could not start", true);
    return g_thread != nullptr;
}

void SignalAudioControlStop() { if (g_stop) SetEvent(g_stop); }
void StopAudioControl() {
    // Explicit pre-unload operation only; never called under loader lock.
    SignalAudioControlStop();
    if (g_thread) { WaitForSingleObject(g_thread, INFINITE); CloseHandle(g_thread); g_thread = nullptr; }
    if (g_change != INVALID_HANDLE_VALUE) { FindCloseChangeNotification(g_change); g_change = INVALID_HANDLE_VALUE; }
    if (g_stop) { CloseHandle(g_stop); g_stop = nullptr; }
    // Provider reference/boundary stays retained until all native hooks drain.
}

bool RequestTrainingGainOwnership(uint32_t lanes, int bgmPercent, int sePercent) {
    AudioControl::PublishAudioPercents(bgmPercent, sePercent);
    if (!g_provider) return false;
    TransferContext context{};
    if (!g_provider->query(&context.expected)) return false;
    return g_provider->retire(&context.expected, lanes, CommitTransfer, &context) && context.committed;
}

bool ImportAudioSettingsIfAvailable(bool force) {
    std::lock_guard<std::recursive_mutex> lock(g_controlMutex);
    EfzAudioFileTransaction transaction;
    if (!transaction || !Refresh(force) || !g_status.audioShared) return false;
    // Refresh and mirror are one control transaction; never replay cached status.
    const auto view = AudioControl::ReadAudioSettings();
    Config::SetAudioSettingsMirror(view.bgmPercent, view.sePercent);
    return true;
}

bool PublishAudioSettings(int bgmPercent, int sePercent) {
    std::lock_guard<std::recursive_mutex> lock(g_controlMutex);
    EfzAudioFileTransaction transaction;
    if (!transaction) return false;
    const int bgm = ClampPercent(bgmPercent), se = ClampPercent(sePercent);
    if (!g_admitted || (!g_status.modLoaded && !g_status.sharedConfigFound)) {
        AudioControl::PublishAudioPercents(bgm, se);
        Config::SetAudioSettingsMirror(bgm, se);
        return false;
    }
    const auto& path = g_status.sharedConfigPath;
    if (!WriteIniInt("Audio", "writeInProgress", 1, path)) return false;
    const int previousRevision = ReadIniInt("Audio", "revision", 0, path);
    if (!WriteIniInt("Audio", "bgmVolumePercent", bgm, path) ||
        !WriteIniInt("Audio", "seVolumePercent", se, path) ||
        !WriteIniInt("Audio", "revision", previousRevision == INT_MAX ? 1 : previousRevision + 1, path) ||
        !WriteIniInt("Audio", "writeInProgress", 0, path)) return false;
    // Only a complete successful pair changes the acknowledged runtime tuple.
    AudioControl::PublishAudioPercents(bgm, se);
    Config::SetAudioSettingsMirror(bgm, se);
    Refresh(true);
    return true;
}

bool PublishAudioLaneSetting(bool bgm, int percent) {
    bool published = false;
    {
        std::lock_guard<std::recursive_mutex> lock(g_controlMutex);
        EfzAudioFileTransaction transaction;
        if (!transaction) return false;
        const auto view = AudioControl::ReadAudioSettings();
        published = PublishAudioSettings(bgm ? percent : view.bgmPercent, bgm ? view.sePercent : percent);
    }
    // The one event-driven apply: once per slider step, only lanes training
    // owns; volume is the one feature allowed to act outside Practice.
    AudioControl::ApplyConfiguredVolumesNow();
    return published;
}
Status GetStatus() { std::lock_guard<std::recursive_mutex> lock(g_controlMutex); return g_status; }
bool IsSharedAudioActiveCached() { return GetStatus().audioShared; }
bool IsSharedAudioActive() { return IsSharedAudioActiveCached(); }
} // namespace ExtendedConfigBridge
