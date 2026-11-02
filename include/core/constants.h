#pragma once
#define DIRECTINPUT_VERSION 0x0800
#ifndef RC_INVOKED

#define HMENU_ID(id) reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id))
#else
#endif

#define IDD_EDITDATA 1001
#define IDC_TAB_CONTROL 4000

#define MAX_HP 9999
#define MAX_METER 3000

#define MAX_RF 1000

#define EFZ_BASE_OFFSET_P1 0x390104
#define EFZ_BASE_OFFSET_P2 0x390108
#define EFZ_BASE_OFFSET_GAME_STATE 0x39010C
// Screen/state byte:
// 0=Title, 1=Character Select, 2=Loading, 3=In-game, 5=Win screen, 6=Settings, 8=Replay select
#define EFZ_BASE_OFFSET_SCREEN_STATE 0x390148
#define XPOS_OFFSET 0x20
#define YPOS_OFFSET 0x28
#define HP_OFFSET 0x108
#define HP_BAR_OFFSET 0x10C
#define METER_OFFSET 0x148
#define MOVE_ID_OFFSET 0x8
#define RF_OFFSET 0x118
#define GAME_MODE_OFFSET 0x1364
#define XVEL_OFFSET 0x30
#define YVEL_OFFSET 0x38

// 0 = red IC, 1 = blue IC
#define IC_COLOR_OFFSET 0x120

// Copies of battleContext words +1396/+1398. A: 1000<->2000 on F5, 0..2000 step 5 while F4 held.
// B: 3332 under one F5 branch, 9999 while F4 fine-tune is active.
#define PLAYER_PARAM_A_COPY_OFFSET 0x30EC
#define PLAYER_PARAM_B_COPY_OFFSET 0x30EE

// sys + 4944/4948 gate F4 fine-tune.
#define SYS_FLAG_4944 4944
#define SYS_FLAG_4948 4948

#define CHARACTER_NAME_OFFSET 0x94

// 1 = facing right, 255 = facing left
#define FACING_DIRECTION_OFFSET 0x50

#define IDLE_MOVE_ID 0
#define WALK_FWD_ID 1
#define WALK_BACK_ID 2
#define CROUCH_ID 3
#define CROUCH_TO_STAND_ID 7
#define PREJUMP_ID 8
#define LANDING_ID 13
#define STAND_GUARD_ID 151
#define CROUCH_GUARD_ID 153
#define CROUCH_GUARD_STUN1 154
#define CROUCH_GUARD_STUN2 155
#define AIR_GUARD_ID 156
#define RG_STAND_ID 168
#define RG_CROUCH_ID 169
#define RG_AIR_ID 170

#define STAND_HITSTUN_START 50
#define STAND_HITSTUN_END 55
#define CROUCH_HITSTUN_START 56
#define CROUCH_HITSTUN_END 58
#define LAUNCHED_HITSTUN_START 59
#define LAUNCHED_HITSTUN_END 71
#define SWEEP_HITSTUN 71

#define FORWARD_AIRTECH 157
#define BACKWARD_AIRTECH 158
#define GROUNDTECH_START 98
#define GROUNDTECH_END 99
#define GROUNDTECH_PRE 97
#define GROUNDTECH_RECOVERY 96 

#define STRAIGHT_JUMP_ID 4
#define FORWARD_JUMP_ID 5
#define BACKWARD_JUMP_ID 6
#define FALLING_ID 9
#define DOUBLE_JUMP_NEUTRAL_ID 14
#define DOUBLE_JUMP_FWD_ID 15
#define DOUBLE_JUMP_BACK_ID 16
#define LANDING_1_ID 10
#define LANDING_2_ID 11
#define LANDING_3_ID 12
#define LANDING_ID 13

#define FIRE_STATE 81
#define ELECTRIC_STATE 82
#define FROZEN_STATE_START 83
#define FROZEN_STATE_END 86

// Thrown states vary by character: main range 100..110, outliers 121..122, 117, 118, 113.
#define THROWN_STATE_START 100
#define THROWN_STATE_END   110
#define THROWN2_STATE_START 121
#define THROWN2_STATE_END   122
// Mai continuation
#define THROWN3_STATE_START 117
#define THROWN3_STATE_END   117
// Rumi continuation
#define THROWN4_STATE_START 118
#define THROWN4_STATE_END   118
// UNKNOWN/Mizuka continuation
#define THROWN5_STATE_START 113
#define THROWN5_STATE_END   113

#define EFZ_VISUAL_FPS 64.0
#define SUBFRAMES_PER_VISUAL_FRAME 3.0
#define INTERNAL_FRAMES_PER_SECOND (EFZ_VISUAL_FPS * SUBFRAMES_PER_VISUAL_FRAME)

#define RG_STAND_FREEZE_DEFENDER 20
#define RG_STAND_FREEZE_ATTACKER 19.66
#define RG_CROUCH_FREEZE_DEFENDER 22
#define RG_CROUCH_FREEZE_ATTACKER 19.66
#define RG_AIR_FREEZE_DEFENDER 22
#define RG_AIR_FREEZE_ATTACKER 20
#define RG_STUN_DURATION 20     // 21.66 for Sayuri, 20 for everyone else

#define RG_STAND_FREEZE_DURATION (RG_STAND_FREEZE_DEFENDER * 3)
#define RG_CROUCH_FREEZE_DURATION (RG_CROUCH_FREEZE_DEFENDER * 3) 
#define RG_AIR_FREEZE_DURATION (RG_AIR_FREEZE_DEFENDER * 3)

#define GROUND_IC_ID 167
#define AIR_IC_ID 171
#define SUPER_FLASH_DURATION 119    // internal frames: 39.66 visual frames * 3
#define IC_FLASH_DURATION 89        // internal frames: 29.66 visual frames * 3
#define SUPERFLASH_BLACK_BG_OFFSET 1  // first subframe of black bg isn't part of freeze

// Gates when the defender can air-tech (air-recovery cooldown).
#define UNTECH_OFFSET 0x124

// Defender: remaining blockstun/hitstun freeze. Attacker: hit-hitstop frames remaining.
// Gameplay timers stop while both players have +0x14A > 0.
#define BLOCKSTUN_OFFSET 0x14A
#define HITSTOP_FREEZE_OFFSET BLOCKSTUN_OFFSET

// Set by IC/super scripts to 60/90/120 internal frames; pauses the flash counter (+0x30C4).
// Not hit-hitstop (+0x14A).
#define SUPERFLASH_FREEZE_OFFSET 0x14C
// Misleading name: this is the superflash counter, not hit-hitstop.
#define HITSTOP_OFFSET SUPERFLASH_FREEZE_OFFSET

// Frames since last touching the ground; the engine's RG check treats >= 30 as in air too long.
#define AIRTIME_OFFSET 0x14E

// Counts down from 180 frames after each hit; 0 resets the combo counter.
#define PLAYER_COMBO_TIMER_OFFSET 0x104

// DWORD
#define PLAYER_COMBO_DAMAGE_OFFSET 0x100

// Short; the combo length the attacker has on the opponent.
#define PLAYER_COMBO_COUNTER_OFFSET 0x174

// Double, displayed as N/100.
#define PLAYER_COMBO_DAMAGE_SCALING_OFFSET 0x178

// DWORD, counted down once per non-frozen frame.
#define PLAYER_SUPERFLASH_COUNTER_OFFSET 0x30C4

// Lockouts; each decrements while the opponent's hitstop is 0. +0x138: 10F after a missed RG.
// +0x130 is byte-sized. +0x13C is checked by the hit handler to forbid RG.
#define PLAYER_FRAME_LOCKOUT_OFFSET    0x130
#define PLAYER_RG_COOLDOWN_OFFSET     0x138
#define PLAYER_STATE_LOCKOUT_OFFSET   0x13A
#define PLAYER_SPECIAL_STATE_OFFSET   0x13C
#define PLAYER_STATE_COOLDOWN3_OFFSET 0x13E
#define PLAYER_STATE_COOLDOWN4_OFFSET 0x140

// DWORD latch set by many producers, not a contact-result enum; never trust a raw value alone.
// 0 none, 2 block/RG/Guard Point, 3 hit (some FIC scripts on whiff), 5 entity, 6 throw/counter, 7 hit.
#define PLAYER_HIT_STATE_OFFSET   0x168

// Attacker move countdown, decremented once per resolved attack (RG/block/hit/
// throw); times the engine's "attack ended" state.
#define PLAYER_ATTACK_TIMER_OFFSET 0x16C

// DWORD: 0 = not guarding; 1 = in block/guard state, or (on the attacker) countered by RG.
#define PLAYER_GUARD_FLAG_OFFSET   0x170

// Float, max 360. Depletes per subframe: neutral 0.075, grounded hitstun 0.2, air hitstun 1.0.
// Refilled by chip damage (BlockedMoveBaseDamage / 30) per blocked move.
#define PLAYER_GUARD_GAUGE_OFFSET  0x134

// DWORD on the attacker: the attack hit a counter-hit eligible defender (frame_data+176 bit 0x2000).
// Cleared at the start of every collision pass.
#define PLAYER_COUNTER_HIT_FLAG_OFFSET 0x144

// Knockback velocities applied by the most recent hit (set on the defender).
#define PLAYER_HIT_XVEL_OFFSET 0xC0   // double
#define PLAYER_HIT_YVEL_OFFSET 0xC8   // double
#define PLAYER_PHYSICS_FLAG_OFFSET 0xD0  // DWORD set to 1 after a hit lands

#define PLAYER_WALLBOUNCE_FLAG_OFFSET 0x128 // DWORD (attack flag bit 0x400)
#define PLAYER_GROUND_BOUNCE_FLAG_OFFSET 0x12C // DWORD (attack flag bit 0x800)

// Attack-data fields, relative to the start of the 200-byte frame_data block
// (first 80 bytes = 5 hurtboxes, next 64 bytes = 4 attack boxes).
#define ATTACK_DATA_BASE_DAMAGE_OFFSET    0xA0  // short
#define ATTACK_DATA_CHIP_DAMAGE_OFFSET    0xA8  // short
#define ATTACK_DATA_FLAGS_OFFSET          0xAA  // word; same field as FRAME_ATTACK_PROPS_OFFSET
#define ATTACK_DATA_AIR_HIT_OVERRIDE      0xAC  // short - defender moveID for air-hit reaction
#define ATTACK_DATA_GROUND_HIT_OVERRIDE   0xAE  // short
#define ATTACK_DATA_HIT_FLAGS_OFFSET      0xB0  // word; same field as FRAME_HIT_PROPS_OFFSET
#define ATTACK_DATA_GUARD_FLAGS_OFFSET    0xB2  // word - stun duration scalar
#define ATTACK_DATA_METER_GAIN_BLOCK      0xB6  // short
#define ATTACK_DATA_KNOCKBACK_X           0xB8  // float
#define ATTACK_DATA_KNOCKBACK_Y           0xBC  // float
#define ATTACK_DATA_ATTACKER_HITSTOP      0xC2  // short
#define ATTACK_DATA_DEFENDER_HITSTOP      0xC4  // short

// Attack flag bits (frame_data + 0xAA, set on attacker frame):
#define FRAME_ATTACK_FLAG_THROW_ATTACK    0x0100
#define FRAME_ATTACK_FLAG_WALLBOUNCE      0x0400
#define FRAME_ATTACK_FLAG_GROUND_BOUNCE   0x0800
#define FRAME_ATTACK_FLAG_COUNTER_MOVE    0x2000

// Hit-property flag bits (frame_data + 0xB0):
#define FRAME_HIT_FLAG_COUNTER_SETUP      0x0200
#define FRAME_HIT_FLAG_AIRTHROW_VULN      0x0800
#define FRAME_HIT_FLAG_GROUND_THROW_VULN  0x1000
#define FRAME_HIT_FLAG_COUNTER_VULN       0x2000

#define FRAME_ATTACK_FLAG_STAND_BLOCKABLE   0x0001
#define FRAME_ATTACK_FLAG_CROUCH_BLOCKABLE  0x0002
#define FRAME_ATTACK_FLAG_SPECIAL_ANIM      0x0004
#define FRAME_ATTACK_FLAG_AIRBORNE          0x0040
#define FRAME_ATTACK_FLAG_BLOCK_DISABLE     0x0080
#define FRAME_HIT_FLAG_BLOCKABLE            0x0010
#define FRAME_HIT_FLAG_DEFENDER_IMMUNE      0x0020

#define AIRTECH_VULNERABLE_FRAMES 16
#define RUMI_AIRTECH_DOUBLEJUMP_LOCKOUT 31

#define CLAMP(val, min, max) ((val)<(min)?(min):((val)>(max)?(max):(val)))

#define AIRTECH_ENABLE_ADDR 0xF4FF
#define AIRTECH_ENABLE_BYTES "\x74\x71"
#define AIRTECH_FORWARD_ADDR 0xF514
#define AIRTECH_FORWARD_BYTES "\x75\x24"
#define AIRTECH_BACKWARD_ADDR 0xF54F
#define AIRTECH_BACKWARD_BYTES "\x75\x21"

#define IDD_EDITDATA 1001

#define IDC_HP1     2001
#define IDC_METER1  2002
#define IDC_RF1     2003
#define IDC_X1      2004
#define IDC_Y1      2005

#define IDC_HP2     2011
#define IDC_METER2  2012
#define IDC_RF2     2013
#define IDC_X2      2014
#define IDC_Y2      2015

#define IDC_BTN_SWAP_POS 2020
#define IDC_BTN_ROUND_START 2021
#define IDC_BTN_CONFIRM 2022
#define IDC_BTN_CANCEL 2023

#define IDC_AUTO_AIRTECH_CHECK 3001
#define IDC_AIRTECH_FORWARD 3002
#define IDC_AIRTECH_BACKWARD 3003

#define IDC_AIRTECH_DIRECTION 3010
#define IDC_JUMP_DIRECTION    4020
#define IDC_JUMP_TARGET       4021
#define IDC_AIRTECH_DELAY     3020

#define STANDING_BLOCK_LVL1 150
#define STANDING_BLOCK_LVL2 151
#define STANDING_BLOCK_LVL3 152

#define CROUCHING_BLOCK_LVL1 153
#define CROUCHING_BLOCK_LVL2_A 154
#define CROUCHING_BLOCK_LVL2_B 155

// Universal movement IDs: 164 is the backdash, 165/166 are forward/backward airdash.
#define GROUND_FORWARD_DASH_ID 163
#define GROUND_BACKWARD_DASH_ID 164
#define AIR_FORWARD_DASH_ID 165
#define AIR_BACKWARD_DASH_ID 166
#define FORWARD_DASH_START_ID 163
#define FORWARD_DASH_RECOVERY_SENTINEL_ID 178
#define KAORI_FORWARD_DASH_START_ID 250
// Kaori's 44~66 Recoil Ducking destination: entered only when a native forward
// dash command is consumed during backdash 164, frame 4/5.
#define KAORI_RECOIL_DUCK_ID 251

// Internal frames
#define FRAME_ADV_LVL1_BLOCK 9
#define FRAME_ADV_LVL2_BLOCK 14
#define FRAME_ADV_LVL3_BLOCK 21
#define FRAME_ADV_LVL1_HIT 10
#define FRAME_ADV_LVL2_HIT 16
#define FRAME_ADV_LVL3_HIT 23
#define FRAME_ADV_AIR_BLOCK 19

#define INPUT_DEVICE_KEYBOARD 0
#define INPUT_DEVICE_GAMEPAD 1
#define MAX_CONTROLLERS 4

#define BASE_ATTACK_5A        200
#define BASE_ATTACK_5B        201
#define BASE_ATTACK_F5B       202  // Far B
#define BASE_ATTACK_5C        203
#define BASE_ATTACK_2A        204
#define BASE_ATTACK_2B        205
#define BASE_ATTACK_2C        206

#define BASE_ATTACK_JA        207
#define BASE_ATTACK_JB        208
#define BASE_ATTACK_JC        209

#define BASE_ATTACK_5D        210
#define BASE_ATTACK_2D        211
#define BASE_ATTACK_JD        212

#define AIR_THROW_STARTUP_ID  248
#define AIR_THROW_SUCCESS_ID  249
#define BASE_AIRTHROW         AIR_THROW_SUCCESS_ID

#define TRIGGER_NONE          0
#define TRIGGER_AFTER_BLOCK   1
#define TRIGGER_ON_WAKEUP     2
#define TRIGGER_AFTER_HITSTUN 3
#define TRIGGER_AFTER_AIRTECH 4
#define TRIGGER_ON_RG         5

// Normals are grouped by posture with 4 buttons each (A/B/C/D) for modulo-4 button extraction.
#define ACTION_NONE           0
#define ACTION_5A             0
#define ACTION_5B             1
#define ACTION_5C             2
#define ACTION_5D             3
#define ACTION_2A             4
#define ACTION_2B             5
#define ACTION_2C             6
#define ACTION_2D             7
#define ACTION_JA             8
#define ACTION_JB             9
#define ACTION_JC            10
#define ACTION_JD            11
#define ACTION_6A            12
#define ACTION_6B            13
#define ACTION_6C            14
#define ACTION_6D            15
#define ACTION_4A            16
#define ACTION_4B            17
#define ACTION_4C            18
#define ACTION_4D            19
#define ACTION_QCF           20
#define ACTION_DP            21
#define ACTION_QCB           22
#define ACTION_421           23
#define ACTION_SUPER1        24  // Misnamed: 41236, an ordinary special family
#define ACTION_41236         ACTION_SUPER1
#define ACTION_SUPER2        25  // Misnamed: 2141236 super family
#define ACTION_2141236       ACTION_SUPER2
#define ACTION_236236        26
#define ACTION_214214        27
#define ACTION_JUMP          28
#define ACTION_BACKDASH      29
#define ACTION_FORWARD_DASH  30
#define ACTION_BLOCK         31
#define ACTION_FINAL_MEMORY  32
#define ACTION_641236        33
#define ACTION_463214        34
#define ACTION_412           35
#define ACTION_22            36
#define ACTION_4123641236    37
#define ACTION_6321463214    38
// Append only: saved ACTION_* values and random-pool bit positions must never change.
#define ACTION_1X            39
#define ACTION_3X            40
#define ACTION_J2X           41
#define ACTION_J6X           42
#define ACTION_66X           43
#define ACTION_662X          44
#define ACTION_664X          45
#define ACTION_KAORI_RECOIL_DUCK 46
#define ACTION_COUNT         47

#define DEFAULT_TRIGGER_DELAY 0

#define IDC_AUTOACTION_ENABLE     5001
#define IDC_AUTOACTION_ACTION     5003
#define IDC_AUTOACTION_CUSTOM_ID  5004
#define IDC_AUTOACTION_PLAYER     5005

#define IDC_TRIGGER_AFTER_BLOCK_CHECK   5010
#define IDC_TRIGGER_AFTER_BLOCK_DELAY   5011
#define IDC_TRIGGER_ON_WAKEUP_CHECK     5012
#define IDC_TRIGGER_ON_WAKEUP_DELAY     5013
#define IDC_TRIGGER_AFTER_HITSTUN_CHECK 5014
#define IDC_TRIGGER_AFTER_HITSTUN_DELAY 5015
#define IDC_TRIGGER_AFTER_AIRTECH_CHECK 5016
#define IDC_TRIGGER_AFTER_AIRTECH_DELAY 5017

#define IDC_TRIGGER_AFTER_BLOCK_ACTION     5018
#define IDC_TRIGGER_ON_WAKEUP_ACTION       5019
#define IDC_TRIGGER_AFTER_HITSTUN_ACTION   5020
#define IDC_TRIGGER_AFTER_AIRTECH_ACTION   5021

#define IDC_TRIGGER_ON_RG_CHECK            5027
#define IDC_TRIGGER_ON_RG_DELAY            5028
#define IDC_TRIGGER_ON_RG_ACTION           5029

#define IDC_TRIGGER_AFTER_BLOCK_CUSTOM     5022
#define IDC_TRIGGER_ON_WAKEUP_CUSTOM       5023
#define IDC_TRIGGER_AFTER_HITSTUN_CUSTOM   5024
#define IDC_TRIGGER_AFTER_AIRTECH_CUSTOM   5025

#define IDC_TRIGGER_WAKE_BUFFER_CHECK      5026

#define IDC_TRIGGER_AFTER_BLOCK_MACRO      5030
#define IDC_TRIGGER_ON_WAKEUP_MACRO        5031
#define IDC_TRIGGER_AFTER_HITSTUN_MACRO    5032
#define IDC_TRIGGER_AFTER_AIRTECH_MACRO    5033
#define IDC_TRIGGER_ON_RG_MACRO            5034

#define CHAR_ID_AKANE     0
#define CHAR_ID_AKIKO     1
#define CHAR_ID_IKUMI     2
#define CHAR_ID_MISAKI    3
#define CHAR_ID_SAYURI    4
#define CHAR_ID_KANNA     5
#define CHAR_ID_KAORI     6
#define CHAR_ID_MAKOTO    7
#define CHAR_ID_MINAGI    8
#define CHAR_ID_MIO       9
#define CHAR_ID_MISHIO    10
#define CHAR_ID_MISUZU    11
#define CHAR_ID_MIZUKA        12  // Mizuka Nagamori; resource/file name "nagamori"
#define CHAR_ID_UNKNOWN_BOSS  13  // Boss UNKNOWN; resource/file name "mizuka"
#define CHAR_ID_NANASE    14  // Actually Rumi in files
#define CHAR_ID_EXNANASE  15  // Actually Doppel in files
#define CHAR_ID_NAYUKI    16  // Actually Nayuki (Neyuki) in files
#define CHAR_ID_NAYUKIB   17  // Actually NayukiB in files
#define CHAR_ID_SHIORI    18
#define CHAR_ID_AYU       19
#define CHAR_ID_MAI       20
#define CHAR_ID_MAYU      21
#define CHAR_ID_UNKNOWN   22  // Playable UNKNOWN; resource/file name "mizukab"
#define CHAR_ID_KANO      23

#define IKUMI_LEVEL_GAUGE_OFFSET 0x3148  // 0..99 (100 triggers blood level up)
#define IKUMI_BLOOD_OFFSET   0x314C
#define IKUMI_GENOCIDE_OFFSET 0x3150

#define IKUMI_BLOOD_MAX      8
#define IKUMI_GENOCIDE_MAX   1260    // Depletes by 3 per frame

// Shiori's shield gauge reuses Ikumi's blood-meter slot (+0x314C) on a larger
// scale; frozen at 1992 it never depletes.
#define SHIORI_SHIELD_OFFSET IKUMI_BLOOD_OFFSET
#define SHIORI_SHIELD_FULL   1992

#define IKUMI_GENOCIDE_TIMER_ADDR 0x2A718    // patch site of the genocide timer decrement
#define IKUMI_GENOCIDE_TIMER_ORIGINAL "\x89\x86\x50\x31\x00\x00"
#define IKUMI_GENOCIDE_TIMER_PATCH "\x90\x90\x90\x90\x90\x90"
#define MISUZU_FEATHER_OFFSET 0x3464
#define MISUZU_FEATHER_MAX 3
#define MISUZU_POISON_TIMER_OFFSET 0x345C  // Counts down 3000->0 while poisoned
#define MISUZU_POISON_LEVEL_OFFSET 0x3460  // 0=inactive, nonzero=active (any value)
#define MISUZU_POISON_TIMER_MAX    3000

#define MISHIO_ELEMENT_OFFSET            0x3158
#define MISHIO_ELEMENT_COOLDOWN_OFFSET   0x315C
#define MISHIO_ELEMENT_CLOCK_OFFSET      0x3160
#define MISHIO_AWAKENED_TIMER_OFFSET     0x3168  // Counts down while Awakened; when <=0, element resets to 0

#define MISHIO_ELEM_NONE      0
#define MISHIO_ELEM_FIRE      1
#define MISHIO_ELEM_LIGHTNING 2
#define MISHIO_ELEM_AWAKENED  3

#define MISHIO_AWAKENED_TARGET 4500

// Enlightened Final Memory flag: DWORD toggles 0/1 when Enlightened is active.
#define DOPPEL_ENLIGHTENED_OFFSET 0x3454

// DWORD on Doppel, first press wins, from the opponent's rising-edge A/B/C button bytes.
// Outside the moves below it is generic scratch: gate on BOTH character and moveID or you corrupt state.
#define DOPPEL_TECH_LATCH_OFFSET 0x3138

#define DOPPEL_TECH_NONE 0
#define DOPPEL_TECH_A    1  // A pressed - escapes nothing (lockout)
#define DOPPEL_TECH_B    2  // B pressed - escapes the B-branch follow-ups
#define DOPPEL_TECH_C    3  // C pressed - escapes the A-branch follow-ups

// The only four Doppel moveIDs during which +0x3138 carries tech-choice meaning.
#define DOPPEL_MOVE_CAPTURE_HOLD    254  // Maiden Capture hold ("Followup Stage 1")
#define DOPPEL_MOVE_MAIDEN_CRASH    255  // Maiden Crash (Stage 2)
#define DOPPEL_MOVE_BARRAGE         300  // Relentless Granite-Breaking Barrage (Stage 3, A-line)
#define DOPPEL_MOVE_INNER_SOUL      306  // Exploding Inner-Soul Fist (Stage 3, B-line)
// Doppel's recovery state after a successful tech (the opponent goes to BACKWARD_AIRTECH 158).
#define DOPPEL_MOVE_TECHED_RECOVERY 258

// Motion tokens (MOTION_TOKEN_OFFSET +0x262) read by the follow-up branch
// selection; 236X and 214X encodings are interchangeable.
#define DOPPEL_TOKEN_A_QCF   10   // 236A  (stage 1)
#define DOPPEL_TOKEN_B_QCF   11   // 236B  (stage 1)
#define DOPPEL_TOKEN_A_QCB   30   // 214A  (stage 1)
#define DOPPEL_TOKEN_B_QCB   31   // 214B  (stage 1)
#define DOPPEL_TOKEN_A_SUPER_QCF 100  // 236236A (stages 2/3)
#define DOPPEL_TOKEN_B_SUPER_QCF 101  // 236236B (stages 2/3)
#define DOPPEL_TOKEN_A_SUPER_QCB 110  // 214214A (stages 2/3)
#define DOPPEL_TOKEN_B_SUPER_QCB 111  // 214214B (stages 2/3)

// DWORD on Sayuri: opponent's move ID caught by her 214 stance. 999 = empty (0 is a real move ID).
// Shared per-character slot (Ikumi, Akiko, Mio, Kano, NayukiB, Mai): gate on CHAR_ID_SAYURI.
#define SAYURI_COUNTER_MEMORY_OFFSET 0x3150
#define SAYURI_COUNTER_MEMORY_EMPTY  999

// DWORD on Sayuri: 1 = the blocked move is the remembered one, so A/B/C cancel into Magical Cutter.
// Same hazard as DOPPEL_TECH_LATCH_OFFSET: gate on the character and a freshly read move ID.
#define SAYURI_CUTTER_ARMED_OFFSET 0x3138

// White "ready" flash set alongside the armed flag: a flat colour blit, not a state.
#define SAYURI_FLASH_COLOR_OFFSET 0x30B8  // BYTE
#define SAYURI_FLASH_TIMER_OFFSET 0x30BC  // DWORD
#define SAYURI_FLASH_COLOR_WHITE  177
#define SAYURI_FLASH_TICKS        10

#define SAYURI_MOVE_COUNTER        256  // 214A/B/C counter stance
#define SAYURI_MOVE_COUNTER_THROW  299  // the catch: turn and throw
#define SAYURI_MOVE_MAGICAL_CUTTER 258  // rising kick out of grounded blockstun
// Magical Cutter window: grounded blockstun 150-155 only; the engine excludes air blockstun (156).

#define TOGGLE_CHARACTER_MODE_RVA 0x0008E140
// Gate flag that enables barehand specials (1=barehand, 0=shinai)
#define RUMI_WEAPON_GATE_OFFSET   0x344C
// True mode byte toggled by in-game toggleCharacterMode (1=barehand/alternate, 0=shinai/normal)
#define RUMI_MODE_BYTE_OFFSET     0x3458
#define RUMI_ALT_ANIM_PTR_OFFSET  0x345C
#define RUMI_ALT_MOVE_PTR_OFFSET  0x3460
#define RUMI_NORM_ANIM_PTR_OFFSET 0x3474
#define RUMI_NORM_MOVE_PTR_OFFSET 0x3478
#define RUMI_ACTIVE_ANIM_PTR_DST  0x0010
#define RUMI_ACTIVE_MOVE_PTR_DST  0x0164

// Nanase (Rumi) super that forcibly drops Shinai
#define RUMI_SUPER_TOSS_A 308
#define RUMI_SUPER_TOSS_B 309
#define RUMI_SUPER_TOSS_C 310

// Final Memory ("Kimchi") state
#define RUMI_KIMCHI_TIMER_OFFSET  0x3148
#define RUMI_KIMCHI_ACTIVE_OFFSET 0x314C  // 0=inactive, 1=active
// Starts at 3000 and decreases by 3 every visual frame
#define RUMI_KIMCHI_TARGET 3000
#define RUMI_KIMCHI_MOVE_ID 307

#define P1_INPUT_BUFFER_OFFSET 0x1AB
#define P1_INPUT_BUFFER_INDEX_OFFSET 0x260
#define P1_INPUT_BUFFER_SIZE 180

// Writing 99 cancels in-flight motion recognition. Follows the 2-byte buffer head at 0x260.
#define MOTION_TOKEN_OFFSET 0x262

// Reset on landing, incremented per consumed air dash / double jump.
// Semantics vary by character; display raw values, not universal limits.
#define AIR_MOBILITY_COUNTER1_OFFSET 0x159
#define AIR_MOBILITY_COUNTER2_OFFSET 0x15A

#define HORIZONTAL_INPUT_OFFSET 0x188  // Left = 255, Right = 1, Neutral = 0
#define VERTICAL_INPUT_OFFSET   0x189  // Up = 255, Down = 1, Neutral = 0
#define BUTTON_A_OFFSET         0x18A  // 0 = not pressed, 1 = pressed
#define BUTTON_B_OFFSET         0x18B
#define BUTTON_C_OFFSET         0x18C
#define BUTTON_D_OFFSET         0x18D

// Toggled when a motion/command or dash pattern is recognized.
// Clear after external input injection, or residual moveID transitions fire.
#define COMMAND_BUFFER_OFFSET   0x1A8
#define DASH_COMMAND_OFFSET     0x1A9

#define INPUT_LATCH1_OFFSET     COMMAND_BUFFER_OFFSET
#define INPUT_LATCH2_OFFSET     DASH_COMMAND_OFFSET
// Counts dash frames; clear during full input cleanup
#define DASH_TIMER_OFFSET       0x1AA

#define BLOCK_DIRECTION_OFFSET  HORIZONTAL_INPUT_OFFSET // signed: -1 (left) / +1 (right) / 0 neutral
#define BLOCK_STANCE_OFFSET     VERTICAL_INPUT_OFFSET   // 0=stand, 1=crouch (down held)

#define ANIM_TABLE_OFFSET                 0x164   // pointer
#define CURRENT_FRAME_INDEX_OFFSET        0x0A    // short
#define ANIM_ENTRY_STRIDE                 8       // bytes per state entry in anim table
#define ANIM_ENTRY_FRAMES_PTR_OFFSET      4       // within entry, pointer to frames array
#define FRAME_BLOCK_STRIDE                200     // bytes per frame block
#define FRAME_ATTACK_PROPS_OFFSET         170     // word: high/low/any bits (0xAA)
#define FRAME_HIT_PROPS_OFFSET            176     // word: blockable bit at 0x10 (0xB0)
#define FRAME_GUARD_PROPS_OFFSET          178     // word: extra guard metadata (0xB2)

#define RAW_INPUT_UP    255
#define RAW_INPUT_DOWN  1
#define RAW_INPUT_LEFT  255
#define RAW_INPUT_RIGHT 1
#define RAW_INPUT_NEUTRAL 0

#define AKIKO_BULLET_CYCLE_OFFSET        0x3150
#define AKIKO_TIMESLOW_TRIGGER_OFFSET    0x0160
#define AKIKO_TIMESLOW_INACTIVE          0
#define AKIKO_TIMESLOW_A                 1
#define AKIKO_TIMESLOW_B                 2
#define AKIKO_TIMESLOW_C                 3
#define AKIKO_TIMESLOW_INFINITE          4 // unused in the mod: breaks next timeslow activation

// On-screen counter digits XYZ (X=third, Y=second, Z=first); 000 = time-slow persists.
#define AKIKO_TIMESLOW_THIRD_OFFSET      0x3154
#define AKIKO_TIMESLOW_SECOND_OFFSET     0x3158
#define AKIKO_TIMESLOW_FIRST_OFFSET      0x315C
// Thousands odometer cell: the engine ends time-slow from THIS cell (level 4
// written at 0x00725A89), not from the three displayed digits.
#define AKIKO_TIMESLOW_FOURTH_OFFSET     0x3160
// Time-slow odometer tick (Akiko vtable slot 8): 03 8A 54 31 00 00  add ecx,[edx+3154h].
// INFINITE TIMESLOW patches it to mov ecx,[edx+3154h] (timer_freeze_patch.cpp).
#define AKIKO_TIMESLOW_ODOMETER_ADD_VA   0x007257FDu

// Curse level is on the victim, 0..3: >=1 blocks dash, >=2 specials/supers, >=3 jumps. Not 0x315C.
// Timer is on Akiko, 1000 ticks: 83 EA 01 sub edx,1; FREEZE CURSE TIMER makes it sub edx,0.
#define AKIKO_CURSE_LEVEL_OFFSET         0x015C
#define AKIKO_CURSE_TIMER_OFFSET         0x3148
#define AKIKO_CURSE_TIMER_MAX            1000
#define AKIKO_CURSE_TIMER_DECREMENT_VA   0x007257B2u

// Reuses the shared 0x3150 slot.
#define MIO_STANCE_OFFSET                0x3150
#define MIO_STANCE_SHORT                 0
#define MIO_STANCE_LONG                  1

// Magic meter, 0..10000; shares the 0x3150 slot.
#define KANO_MAGIC_OFFSET                0x3150
#define KANO_MAGIC_MAX                   10000

// Snowbunny timer; shared 0x3150 slot; counts down from ~3000 to 0 while active.
#define NAYUKIB_SNOWBUNNY_TIMER_OFFSET   0x3150
#define NAYUKIB_SNOWBUNNY_MAX            3000
#define NAYUKIB_SNOWBUNNY_ACTIVE_FLAGS_BASE  0x3154  // Array: +0x3154 + (4*index) for 8 snowbunnies

// Clean Hit last-hit move IDs for the 623 rekka: A/B 3rd hit ends at 259,
// C 6th hit ends at 254.
#define AKIKO_MOVE_623_LAST_AB           259
#define AKIKO_MOVE_623_LAST_C            254

// Jam count at +0x3148 (same slot other characters repurpose).
#define NEYUKI_JAM_COUNT_OFFSET          0x3148
#define NEYUKI_JAM_COUNT_MAX             9

// Status byte: 0 inactive, 1 active, 2 unsummon, 3 charging, 4 awakening install.
// One int timer serves all: active <= 10000, charge <= 1200, awakening <= 10000.
#define MAI_STATUS_OFFSET                0x3144
#define MAI_MULTI_TIMER_OFFSET           0x3148
// One timer under three names; code picks by status.
#define MAI_GHOST_TIME_OFFSET            MAI_MULTI_TIMER_OFFSET
#define MAI_GHOST_CHARGE_OFFSET          MAI_MULTI_TIMER_OFFSET
#define MAI_AWAKENING_TIMER_OFFSET       MAI_MULTI_TIMER_OFFSET
#define MAI_GHOST_TIME_MAX               10000
#define MAI_GHOST_CHARGE_MAX             1200
#define MAI_AWAKENING_MAX                10000
// +0x314C: transient charge helper. +0x3150 (shared slot): one-shot summon flash flag.
// At 1, the first active tick spawns an effect and clears it; used by Force Summon.
#define MAI_CHARGE_HELPER_OFFSET         0x314C
#define MAI_SUMMON_FLASH_FLAG_OFFSET     0x3150
#define MAI_SUMMON_MOVE_ID                0x0104
#define STATE_FRAME_INDEX_OFFSET          0x0A    // same field as CURRENT_FRAME_INDEX_OFFSET
#define STATE_SUBFRAME_COUNTER_OFFSET     0x0C

// Mai slot id 401 = active Mini-Mai. Slot fields: uint16 id/frame/subframe, double X/Y.
#define MAI_GHOST_SLOTS_BASE              0x04D0
#define MAI_GHOST_SLOT_STRIDE             152
#define MAI_GHOST_SLOT_ID_OFFSET          0x00
#define MAI_GHOST_SLOT_FRAME_OFFSET       0x02
#define MAI_GHOST_SLOT_SUBFRAME_OFFSET    0x04
#define MAI_GHOST_SLOT_X_OFFSET           0x18
#define MAI_GHOST_SLOT_Y_OFFSET           0x20
#define MAI_GHOST_SLOT_MAX_SCAN           12

// Michiru entity reuses the Mini-Mai slot layout; only the ID differs (400).
#define MINAGI_PUPPET_ENTITY_ID           400
#define MINAGI_PUPPET_SLOTS_BASE          MAI_GHOST_SLOTS_BASE
#define MINAGI_PUPPET_SLOT_STRIDE         MAI_GHOST_SLOT_STRIDE
#define MINAGI_PUPPET_SLOT_ID_OFFSET      MAI_GHOST_SLOT_ID_OFFSET
#define MINAGI_PUPPET_SLOT_FRAME_OFFSET   MAI_GHOST_SLOT_FRAME_OFFSET
#define MINAGI_PUPPET_SLOT_SUBFRAME_OFFSET MAI_GHOST_SLOT_SUBFRAME_OFFSET
#define MINAGI_PUPPET_SLOT_X_OFFSET       MAI_GHOST_SLOT_X_OFFSET
#define MINAGI_PUPPET_SLOT_Y_OFFSET       MAI_GHOST_SLOT_Y_OFFSET
#define MINAGI_PUPPET_SLOT_MAX_SCAN       MAI_GHOST_SLOT_MAX_SCAN

// Never write 0 into the timer while FM runs (it goes to -1, never ends) or >0 when none runs.
// +0x3148 is her bow state; never write it. FREEZE FM TIMER patches 83 EA 01 sub edx,1 to sub edx,0.
#define NAGAMORI_FM_MOVE_ID              312
#define NAGAMORI_FM_TIMER_OFFSET         0x3150
#define NAGAMORI_FM_TIMER_MAX            1200
#define NAGAMORI_FM_TIMER_DECREMENT_VA   0x004BAAEFu
