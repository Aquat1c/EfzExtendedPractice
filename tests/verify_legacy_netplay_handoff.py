from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
utilities = (root / "src/utils/utilities.cpp").read_text(encoding="utf-8")
monitor = (root / "src/game/frame_monitor.cpp").read_text(encoding="utf-8")
frontend = (root / "src/game/game_state.cpp").read_text(encoding="utf-8")
startup = (root / "src/dllmain.cpp").read_text(encoding="utf-8")

def body(source, start, end):
    return source.split(start, 1)[1].split(end, 1)[0]

audio = (root / "src/utils/audio_control.cpp").read_text(encoding="utf-8")
audio_install = body(audio, "bool InstallHookGroup(", "bool CommonAudioEntryAcceptable(")
assert re.search(r"MinHookUtils::CreateHook\(hook.target, hook.detour, hook.original, kAudioHookOwner, hook.name,\s*nullptr, nullptr, true\)", audio_install), "audio hooks must run in offline menus before Practice starts"

enter = body(utilities, "void EnterNetplaySuspend() {", "void ExitNetplaySuspend()")
assert "RetireLegacyPractice(g_p2ControlMutex, g_onlineModeActive, [] {" in enter
retirement = (root / "include/runtime/legacy_netplay_handoff.h").read_text(encoding="utf-8")
assert retirement.index("lock(boundary)") < retirement.index("retire();") < retirement.index("online.store(true")
assert "StopBufferFreezingIgnoringTutorialLease()" in enter
assert "RestoreP2ControlState()" in enter
loop = body(monitor, "while (!g_isShuttingDown && !Practice::MonitorStopRequested()) {", "void ReinitializeOverlays()")
assert "mutationLock(g_p2ControlMutex)" in loop
assert loop.index("mutationLock(g_p2ControlMutex)") < loop.index("IsNetplaySuspendActive()")
assert re.search(r"mutationLock\.unlock\(\);\s+\(void\)Practice::WaitForMonitorSignal\(250000000\)", loop)
assert re.search(r"mutationLock\.unlock\(\);\s+auto beforeSleep", loop)
assert re.search(r"mutationLock\.lock\(\);\s+if\(Practice::MonitorStopRequested\(\)", loop)
exit_suspend = body(utilities, "void ExitNetplaySuspend() {", "void AuditNetplayMenuEntryState()")
assert exit_suspend.index("SetPracticeBattleActive(false)") < exit_suspend.index("SetNativePassthrough(false)")
assert startup.index("SetPracticeBattleActive(false)") < startup.index("SetNativePassthrough(false)")
assert "g_netplayHandoffPending.load" in utilities
exit_battle = body(frontend, "if (accepted != SCREEN_BATTLE) {", "return static_cast<char>(accepted)")
assert "MinHookUtils::SetPracticeBattleActive(false)" in exit_battle
assert "std::lock_guard<std::recursive_mutex> controlLock(g_p2ControlMutex)" in exit_battle
assert "GetCurrentGamePhase() != GamePhase::Match" in body(utilities, "void EnableFeatures() {", "void DisableFeatures()")
loading = (root / "src/game/character_hotswap.cpp").read_text(encoding="utf-8")
assert "return finishLoading(nextScreen);" in loading
assert "GetCurrentGameMode() == GameMode::Practice" in loading
bridge = (root.parent / "InGameNetplay/src/netplay/bridge/session_bridge.cpp").read_text(encoding="utf-8")
start = body(bridge, "static bool StartSessionInternal(", "bool StartSession(")
assert start.index("TrainingHandoff trainingHandoff(role != NetbridgeRole::Host);") < start.index("lock(g_mutex)")
tick = body(bridge, "void Tick()", "void TickExportOnly(")
assert "takeover::Tick(&g_status, &g_connectStartTick, &trainingRetirementRequired)" in tick
retire_tick = tick.split("if (trainingRetirementRequired)", 1)[1]
assert retire_tick.index("TrainingHandoff trainingHandoff;") < retire_tick.index("lock(g_mutex)") < retire_tick.index("takeover::Tick(&g_status, &g_connectStartTick, nullptr, true)")
assert "!g_start.IsCurrent(requestSerial)" in retire_tick
assert "phase != NetbridgePhase::Connecting && phase != NetbridgePhase::DelaySetup" in retire_tick
takeover = (root.parent / "InGameNetplay/src/netplay/bridge/revival_takeover.cpp").read_text(encoding="utf-8")
native_init = body(takeover, "const DWORD initReady = WaitForSingleObject(g_hostInitEvent, 0);", "const int initResult = g_localInitFn(initParams);")
assert native_init.index("if (!trainingRetired)") < native_init.index("SetEvent(g_hostInitEvent)") < native_init.index("DestroyCurrentSession")
assert "if (trainingRetirementRequired) *trainingRetirementRequired = true;" in native_init
assert "return;" in native_init.split("if (!trainingRetired)", 1)[1].split("LogRevival102jDeepSnapshot", 1)[0]
handoff = (root.parent / "InGameNetplay/include/netplay/bridge/training_handoff.inl").read_text(encoding="utf-8")
assert "training_handoff.inl" in bridge
assert handoff.index("state_export::SuspendForOnlineSimulation()") < handoff.index("state_ = alreadySuspended ? State::PreparedKeepSuspended : State::PreparedResumeExports;")
assert "PublishOnlineBoundary" not in handoff
assert "PublishTrainingHandoffBoundary(GetStatus())" in handoff
assert "RefreshNetplayRuntimeState();" in exit_suspend
title = (root.parent / "InGameNetplay/src/netplay/hooks/title_flow.cpp").read_text(encoding="utf-8")
online = body(title, "static bool SuspendUiHooksForOnlineSimulation(", "void HandoffSpectateSession(")
assert online.index("PrepareTrainingForOnlineSimulation()") < online.index("SuspendForOnlineSimulation()")
print("legacy handoff production wiring passed")
