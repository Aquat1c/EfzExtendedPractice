#include "audio_transaction_test_support.h"
#include "igcr/feature/audio_owner_provider.h"
#define ActivateAtInitialization(...) ActivateAtInitialization(__VA_ARGS__); PauseAfterActivation()
#define GetPrivateProfileIntA InterceptIniRead
#define WritePrivateProfileStringA InterceptIniWrite
#include "../../../ExtendedConfig/src/feature/shared_config_store.cpp"
#include "../../../ExtendedConfig/src/feature/audio_control.cpp"
#undef GetPrivateProfileIntA
#undef WritePrivateProfileStringA
namespace igcr::core::logger {
void Info(const std::string&) {}
void Error(const std::string&) {}
}
void SetupCompanionFile(const std::string& path) {
 igcr::feature::shared_config::g_path=path;
 igcr::feature::audio::g_audio_config_path=path;
}
bool CompanionControls() {return igcr::feature::shared_config::PublishControlsState();}
bool CompanionWrite(int b,int s) {return igcr::feature::shared_config::PublishAudioSettings(b,s);}
void CompanionImport() {igcr::feature::audio::ImportAudioOnNotification();}
AudioTuple CompanionTuple() {auto v=igcr::feature::audio::ReadCallbackSettings();return {v.bgm,v.se,v.owned};}

void CompanionActivate() { igcr::feature::shared_config::AudioSettings s{};s.bgm_percent=50;s.se_percent=50;igcr::feature::audio::ActivateAudioSettings(true,s); }
unsigned CompanionQueryOwned() {EfzAudioOwnerV1 r{};igcr::feature::audio::QueryOwner(&r);return r.ownedLaneMask;}

void CompanionSchemaLoad() { igcr::feature::shared_config::Initialize(nullptr); (void)igcr::feature::shared_config::LoadAudioSettings(); }

unsigned CompanionLoadAvailable() {auto s=igcr::feature::shared_config::LoadAudioSettings();return (s.bgm_available?1:0)|(s.se_available?2:0);}
void CompanionStartupSettings() {(void)igcr::feature::audio::LoadInitialAudioSettings();}
