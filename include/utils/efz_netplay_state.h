// Prefer EFZNetplay_CopyState(output, outputSize, minimumVersion), returning int (__cdecl).
// Returns 1 for a coherent bounded copy; 0 leaves output untouched. outputSize must be >= 12.
// Legacy EFZNetplay_GetState and the "EFZNetplay_State" mapping expose mutable storage.
// Validate magic, version and structSize before using any copied fields.
#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Validation magic: 'EFZN' in little-endian byte order.
#define EFZ_NETPLAY_STATE_MAGIC       0x4E5A4645u
// Current struct layout version.  Increment when fields are added/changed.
#define EFZ_NETPLAY_STATE_VERSION     7u
// Well-known name for the named shared memory block.
#define EFZ_NETPLAY_STATE_SHM_NAME    "EFZNetplay_State"

// Capability bits: a set bit means that field group is actively populated; check it before
// reading those fields. A bit may be zero when its data source is unavailable.
#define EFZ_CAP_SESSION       (1u << 0)  // sessionMode, sessionPhase, localSide
#define EFZ_CAP_SCORES        (1u << 1)  // p1Wins, p2Wins, matchCounter
#define EFZ_CAP_NICKNAMES     (1u << 2)  // localNickname, p1Name, p2Name
#define EFZ_CAP_NETWORK       (1u << 3)  // pingMs, rollbackFrames
#define EFZ_CAP_MENU          (1u << 4)  // inNetplayMenu, netplayMenuScreen, netplayMenuDetail
#define EFZ_CAP_REVIVAL       (1u << 5)  // revivalVersion
#define EFZ_CAP_GAME_FLOW     (1u << 6)  // inNetplayCharacterSelect, inNetplayMatch
#define EFZ_CAP_ACTIVITY      (1u << 7)  // activityPhase, endReason
#define EFZ_CAP_CHAR_SELECT   (1u << 8)  // p1CharId, p2CharId, p1Locked, p2Locked, localCursorCharId
#define EFZ_CAP_MATCH_CONTEXT (1u << 9)  // stageId, roundIndex, roundTimerFrames, isRoundActive
#define EFZ_CAP_ASYNC_HOST    (1u << 10) // asyncHost* fields, hostPort  (v7)
#define EFZ_CAP_NET_DETAIL    (1u << 11) // avg/min/max ping, recommended/min/max delay  (v7)
#define EFZ_CAP_CONNECTION    (1u << 12) // connectionAddress  (v7)

// Session mode - mutually exclusive values for the local player's role.
enum EFZNetplaySessionMode
{
    EFZ_SESSION_NONE       = 0,  // No active netplay session (offline / idle)
    EFZ_SESSION_HOSTING    = 1,  // Hosting an online match  (P1 / host side)
    EFZ_SESSION_JOINING    = 2,  // Joined an online match   (P2 / client side)
    EFZ_SESSION_SPECTATING = 3,  // Spectating an online match
    EFZ_SESSION_TOURNAMENT = 4,  // Tournament mode session
};

// Session phase - lifecycle of the netplay connection; mirrors the internal
// NetbridgePhase enum.
enum EFZNetplaySessionPhase
{
    EFZ_PHASE_IDLE          = 0,  // No session
    EFZ_PHASE_CONNECTING    = 1,  // Connecting / waiting for peer
    EFZ_PHASE_DELAY_SETUP   = 2,  // Input-delay negotiation prompt
    EFZ_PHASE_CONNECTED     = 3,  // Fully connected, match in progress
    EFZ_PHASE_FAILED        = 4,  // Connection failed
    EFZ_PHASE_SESSION_ENDED = 5,  // Session ended gracefully
};

// Activity phase - what the local player is doing in the netplay lifecycle;
// more granular than the session phase and the v3 boolean flags.
enum EFZNetplayActivityPhase
{
    EFZ_ACTIVITY_IDLE           = 0,  // Not in any netplay flow
    EFZ_ACTIVITY_MENU           = 1,  // Netplay menu overlay is open
    EFZ_ACTIVITY_CONNECTING     = 2,  // Waiting for peer / connecting
    EFZ_ACTIVITY_DELAY_SETUP    = 3,  // Input-delay negotiation prompt
    EFZ_ACTIVITY_CHAR_SELECT    = 4,  // Online character-select screen
    EFZ_ACTIVITY_LOADING        = 5,  // Post-charselect loading screen
    EFZ_ACTIVITY_MATCH          = 6,  // Active online match / round
    EFZ_ACTIVITY_RESULTS        = 7,  // Post-match results screen (reserved)
    EFZ_ACTIVITY_HOST_IDLE      = 8,  // Background hosting during normal local play; sessionMode stays HOSTING, not a live match
};

// End reason - why the most recent session/match ended; latches until the
// next session starts or the activity phase leaves IDLE.
enum EFZNetplayEndReason
{
    EFZ_END_NONE               = 0,  // No end event (session still active or never started)
    EFZ_END_GRACEFUL           = 1,  // Session ended normally (opponent left gracefully)
    EFZ_END_DISCONNECT         = 2,  // Peer disconnected / connection lost
    EFZ_END_CANCELLED          = 3,  // Local player cancelled the session
    EFZ_END_CONNECT_FAILED     = 4,  // Connection attempt failed (timeout / refused)
    EFZ_END_PEER_PROCESS_DIED  = 5,  // Revival peer process exited unexpectedly
    EFZ_END_UNKNOWN            = 255, // Unknown / unclassified end reason
};

// Netplay menu sub-screen - only meaningful when inNetplayMenu is non-zero.
enum EFZNetplayMenuScreen
{
    EFZ_MENU_MAIN         = 0,  // Top-level: Host / Join / Player Rooms / Lobby / Battle Log / Options / Back
    EFZ_MENU_HOST         = 1,  // Host settings sub-menu
    EFZ_MENU_JOIN         = 2,  // Join settings sub-menu
    EFZ_MENU_PLAYER_ROOMS = 3,  // Player Rooms browser
    EFZ_MENU_OPTIONS      = 4,
    EFZ_MENU_NICKNAME     = EFZ_MENU_OPTIONS, // Legacy alias retained for source compatibility
    EFZ_MENU_LOBBY        = 5,  // Active lobby / room browser
    EFZ_MENU_BATTLE_LOG   = 6,
};

// Netplay menu detail - menu-specific subview; depends on netplayMenuScreen.
enum EFZNetplayMenuDetail
{
    EFZ_MENU_DETAIL_NONE = 0,

    EFZ_MENU_DETAIL_PLAYER_ROOMS_ROOT   = 1,
    EFZ_MENU_DETAIL_PLAYER_ROOMS_JOIN   = 2,
    EFZ_MENU_DETAIL_PLAYER_ROOMS_CREATE = 3,

    EFZ_MENU_DETAIL_OPTIONS_ROOT     = 16,
    EFZ_MENU_DETAIL_OPTIONS_CATEGORY = 17,
    EFZ_MENU_DETAIL_OPTIONS_EDIT     = 18,
    EFZ_MENU_DETAIL_OPTIONS_MODAL    = 19,

    EFZ_MENU_DETAIL_BATTLE_LOG_SUMMARY_PROFILE = 32,
    EFZ_MENU_DETAIL_BATTLE_LOG_SUMMARY_FULL    = 33,
    EFZ_MENU_DETAIL_BATTLE_LOG_SUMMARY_SEARCH  = 34,
    EFZ_MENU_DETAIL_BATTLE_LOG_BROWSER         = 35,
    EFZ_MENU_DETAIL_BATTLE_LOG_FILTERS         = 36,
    EFZ_MENU_DETAIL_BATTLE_LOG_SET_DETAILS     = 37,
};

// Fixed-layout C struct; integers naturally aligned, char arrays ASCII / UTF-8.
// EFZNetplay_CopyState serializes the caller's copy with producer publication.
struct EFZNetplayState
{
    uint32_t magic;              // Must equal EFZ_NETPLAY_STATE_MAGIC
    uint32_t version;            // EFZ_NETPLAY_STATE_VERSION at time of write
    uint32_t structSize;         // sizeof(EFZNetplayState) - for forward compat
    uint32_t lastUpdateTick;     // GetTickCount() at last write

    int32_t  sessionMode;        // EFZNetplaySessionMode enum value
    int32_t  sessionPhase;       // EFZNetplaySessionPhase enum value
    int32_t  localSide;          // 0 = P1 (host), 1 = P2 (joiner), -1 = unknown / spectator

    // Scores (absolute, NOT perspective-adjusted)
    int32_t  p1Wins;             // P1 win count within current set
    int32_t  p2Wins;             // P2 win count within current set
    int32_t  matchCounter;       // Current match index within the set

    char     localNickname[64];  // Our chosen nickname (UTF-8, from EfzRevival.ini)
    char     p1Name[64];         // P1 nickname (from Revival session object)
    char     p2Name[64];         // P2 nickname (from Revival session object)

    int32_t  pingMs;             // Round-trip ping in ms   (-1 = unavailable)
    int32_t  rollbackFrames;     // Agreed input-delay frames (-1 = not set)

    // UI state (v2)
    uint8_t  inNetplayMenu;      // Non-zero when the netplay menu overlay is open
    uint8_t  netplayMenuScreen;  // EFZNetplayMenuScreen enum (valid only when inNetplayMenu is non-zero)
    uint8_t  netplayMenuDetail;  // EFZNetplayMenuDetail enum (valid only when inNetplayMenu is non-zero)
    uint8_t  _pad0;              // Alignment padding - reserved, must be 0

    // EfzRevival version (v2). Null-terminated tag (e.g. "1.02j"); empty if
    // not loaded or unrecognised. Supported: 1.02e, 1.02f, 1.02g, 1.02h, 1.02i, 1.02j.
    char     revivalVersion[16];

    // Game-flow flags (v3): inNetplayMenu, inNetplayCharacterSelect and inNetplayMatch are
    // mutually exclusive in normal flow; all zero when offline, idle or transitioning.
    uint8_t  inNetplayCharacterSelect; // Non-zero during online charselect
    uint8_t  inNetplayMatch;           // Non-zero during online match
    uint8_t  _pad1[2];                 // Alignment padding - reserved, must be 0

    // Capability / sequence / identity (v4)
    uint32_t capabilityFlags;    // Bitmask of EFZ_CAP_* groups populated this tick
    uint32_t stateSeq;           // Monotonic sequence, incremented every Update(); detects stale reads
    uint32_t sessionId;          // Incremented per new session (Idle -> Connecting); 0 before first
    uint32_t setId;              // Incremented per new set (scores reset to 0-0); 0 before first

    // Activity / end reason (v4)
    uint8_t  activityPhase;      // EFZNetplayActivityPhase
    uint8_t  endReason;          // EFZNetplayEndReason; latches until next session starts
    uint8_t  _pad2[2];          // Alignment padding - reserved, must be 0

    // Character-select context (v4) - only meaningful when activityPhase ==
    // EFZ_ACTIVITY_CHAR_SELECT or inNetplayCharacterSelect is non-zero.
    uint8_t  p1CharId;           // P1 selected character ID (0xFF = unknown)
    uint8_t  p2CharId;           // P2 selected character ID (0xFF = unknown)
    uint8_t  p1Locked;           // Non-zero if P1 has locked in their pick
    uint8_t  p2Locked;           // Non-zero if P2 has locked in their pick
    uint8_t  localCursorCharId;  // Character under local player's cursor (0xFF = unavailable)
    uint8_t  _pad3[3];          // Alignment padding - reserved, must be 0

    // Match context (v4) - only meaningful when activityPhase ==
    // EFZ_ACTIVITY_MATCH or inNetplayMatch is non-zero.
    uint8_t  stageId;            // Stage / background ID (0xFF = unknown)
    uint8_t  roundIndex;         // Current round within the match (0-based, 0xFF = unknown)
    uint8_t  isRoundActive;      // Non-zero when a round is in progress
    uint8_t  _pad4;              // Alignment padding - reserved, must be 0
    uint16_t roundTimerFrames;   // Round timer in frames (0xFFFF = unknown)
    uint8_t  _pad5[2];          // Alignment padding - reserved, must be 0

    // Async hosting (v7), when EFZ_CAP_ASYNC_HOST is set: host listener alive while the local
    // player is elsewhere (HOST_IDLE); distinct from a live match (inNetplayMatch).
    uint8_t  asyncHostActive;     // Non-zero: async host listener engaged
    uint8_t  asyncHostMinimized;  // Non-zero: hosting overlay minimized (local play)
    uint8_t  asyncHostPeerFound;  // Non-zero: peer connected, prompt held for accept
    uint8_t  asyncHostTimedOut;   // Non-zero: held prompt timed out (rehost offered)
    uint16_t hostPort;            // Port we are hosting on (0 = n/a)
    uint8_t  _pad6[2];           // Alignment padding - reserved, must be 0

    // Extended network metrics (v7) - populated when EFZ_CAP_NET_DETAIL is set; -1 = unavailable.
    int32_t  avgPingMs;          // Average ping (ms)
    int32_t  minPingMs;          // Minimum ping (ms)
    int32_t  maxPingMs;          // Maximum ping (ms)
    int32_t  recommendedDelay;   // Revival-recommended input delay (frames)
    int32_t  minDelay;           // Minimum selectable input delay (frames)
    int32_t  maxDelay;           // Maximum selectable input delay (frames)

    // Connection endpoint (v7), when EFZ_CAP_CONNECTION is set: our public ip:port when hosting,
    // the target when joining; empty when unavailable.
    char     connectionAddress[64];
};

#ifdef __cplusplus
}
#endif
